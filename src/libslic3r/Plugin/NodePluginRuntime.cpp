// OrcaSlicer Node.js Plugin Runtime Implementation
// Copyright (C) 2024 OrcaSlicer Contributors
// License: AGPL-3.0

#include "NodePluginRuntime.hpp"
#include "PluginContext.hpp"

#include <boost/process.hpp>
#include <boost/process/args.hpp>
#include <boost/process/io.hpp>
#include <boost/process/pipe.hpp>
#include <boost/process/search_path.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/beast/core/detail/base64.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace {
// Base64 encoding helper
std::string base64_encode(const std::vector<uint8_t>& data) {
    std::string result;
    result.resize(boost::beast::detail::base64::encoded_size(data.size()));
    result.resize(boost::beast::detail::base64::encode(result.data(), data.data(), data.size()));
    return result;
}

// Base64 decoding helper
std::vector<uint8_t> base64_decode(const std::string& encoded) {
    std::vector<uint8_t> result;
    result.resize(boost::beast::detail::base64::decoded_size(encoded.size()));
    auto decode_result = boost::beast::detail::base64::decode(result.data(), encoded.data(), encoded.size());
    result.resize(decode_result.first);
    return result;
}
} // anonymous namespace

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#endif

namespace Slic3r {
namespace Plugin {

namespace bp = boost::process;
namespace fs = std::filesystem;
using json = nlohmann::json;

//==============================================================================
// NodePluginInstance Implementation
//==============================================================================

NodePluginInstance::NodePluginInstance(const PluginManifest& manifest,
                                       std::shared_ptr<IPCClient> ipc)
    : m_manifest(manifest)
    , m_ipc(ipc)
{
}

NodePluginInstance::~NodePluginInstance()
{
    on_unload();
}

bool NodePluginInstance::on_load(PluginContext* ctx)
{
    m_current_ctx = ctx;
    
    try {
        // Request plugin to initialize
        json params;
        params["pluginId"] = m_manifest.id;
        params["dataDir"] = ctx->get_plugin_data_dir();
        params["resourcesDir"] = ctx->get_resources_dir();
        
        auto result = m_ipc->call("plugin.load", JsonValue(params.dump()));
        
        if (result.is_object()) {
            auto obj = result.as_object();
            if (obj.count("success") && obj.at("success").as_bool()) {
                return true;
            }
        }
        return false;
    } catch (const std::exception& e) {
        ctx->progress()->error(std::string("Failed to load plugin: ") + e.what());
        return false;
    }
}

void NodePluginInstance::on_unload()
{
    if (m_ipc && m_ipc->is_connected()) {
        try {
            m_ipc->call("plugin.unload", JsonValue());
        } catch (...) {
            // Ignore errors during unload
        }
    }
    m_current_ctx = nullptr;
}

void NodePluginInstance::on_settings_ui()
{
    if (!m_ipc || !m_ipc->is_connected()) return;
    
    try {
        m_ipc->notify("plugin.showSettings", JsonValue());
    } catch (...) {
        // Ignore
    }
}

bool NodePluginInstance::process_mesh(PluginContext* ctx,
                                       int object_idx,
                                       int volume_idx,
                                       const std::string& operation)
{
    if (!m_ipc || !m_ipc->is_connected()) return false;
    
    m_current_ctx = ctx;
    
    try {
        // Get mesh data from context
        auto mesh_view = ctx->get_mesh(object_idx, volume_idx);
        if (!mesh_view) {
            ctx->progress()->error("Could not get mesh data");
            return false;
        }
        
        // Prepare parameters
        json params;
        params["objectIdx"] = object_idx;
        params["volumeIdx"] = volume_idx;
        params["operation"] = operation;
        
        // Send mesh data
        json mesh_data;
        mesh_data["vertexCount"] = mesh_view->vertex_count;
        mesh_data["triangleCount"] = mesh_view->triangle_count;
        
        // Encode vertex data as base64 for efficient transfer
        std::vector<uint8_t> vertex_bytes(
            reinterpret_cast<const uint8_t*>(mesh_view->vertices),
            reinterpret_cast<const uint8_t*>(mesh_view->vertices + mesh_view->vertex_count * 3)
        );
        mesh_data["verticesBase64"] = base64_encode(vertex_bytes);
        
        std::vector<uint8_t> index_bytes(
            reinterpret_cast<const uint8_t*>(mesh_view->indices),
            reinterpret_cast<const uint8_t*>(mesh_view->indices + mesh_view->triangle_count * 3)
        );
        mesh_data["indicesBase64"] = base64_encode(index_bytes);
        
        params["mesh"] = mesh_data;
        
        // Call plugin
        ctx->progress()->update(0, "Processing mesh...");
        auto result = m_ipc->call("plugin.processMesh", JsonValue(params.dump()), 300000); // 5 minute timeout
        
        if (!result.is_object()) {
            ctx->progress()->error("Invalid response from plugin");
            return false;
        }
        
        auto result_obj = result.as_object();
        if (!result_obj.count("success") || !result_obj.at("success").as_bool()) {
            std::string msg = result_obj.count("message") ? 
                result_obj.at("message").as_string() : "Plugin operation failed";
            ctx->progress()->error(msg);
            return false;
        }
        
        // Check if plugin returned new mesh data
        if (result_obj.count("mesh")) {
            auto& new_mesh = result_obj.at("mesh").as_object();
            
            MeshData new_data;
            
            // Decode vertex data
            if (new_mesh.count("verticesBase64")) {
                auto decoded = base64_decode(new_mesh.at("verticesBase64").as_string());
                new_data.vertices.resize(decoded.size() / sizeof(float));
                std::memcpy(new_data.vertices.data(), decoded.data(), decoded.size());
            }
            
            // Decode index data
            if (new_mesh.count("indicesBase64")) {
                auto decoded = base64_decode(new_mesh.at("indicesBase64").as_string());
                new_data.indices.resize(decoded.size() / sizeof(int32_t));
                std::memcpy(new_data.indices.data(), decoded.data(), decoded.size());
            }
            
            // Apply to model
            if (!ctx->set_mesh(object_idx, volume_idx, new_data)) {
                ctx->progress()->error("Failed to apply mesh modifications");
                return false;
            }
        }
        
        ctx->progress()->update(100, "Done");
        return true;
        
    } catch (const std::exception& e) {
        ctx->progress()->error(std::string("Mesh processing failed: ") + e.what());
        return false;
    }
}

std::string NodePluginInstance::process_gcode(PluginContext* ctx,
                                               const std::string& gcode)
{
    if (!m_ipc || !m_ipc->is_connected()) return gcode;
    
    m_current_ctx = ctx;
    
    try {
        json params;
        params["gcode"] = gcode;
        
        auto result = m_ipc->call("plugin.processGCode", JsonValue(params.dump()), 300000);
        
        if (result.is_object()) {
            auto obj = result.as_object();
            if (obj.count("gcode")) {
                return obj.at("gcode").as_string();
            }
        }
        return gcode;
    } catch (...) {
        return gcode;
    }
}

std::vector<std::string> NodePluginInstance::import_extensions() const
{
    std::vector<std::string> extensions;
    // Query plugin for supported extensions
    // For now return empty - plugin manifest should specify these
    return extensions;
}

std::vector<std::string> NodePluginInstance::export_extensions() const
{
    std::vector<std::string> extensions;
    return extensions;
}

std::optional<MeshData> NodePluginInstance::import_mesh(PluginContext* ctx,
                                                         const std::string& path)
{
    if (!m_ipc || !m_ipc->is_connected()) return std::nullopt;
    
    try {
        json params;
        params["path"] = path;
        
        auto result = m_ipc->call("plugin.importMesh", JsonValue(params.dump()), 60000);
        
        if (result.is_object()) {
            auto obj = result.as_object();
            if (obj.count("success") && obj.at("success").as_bool() && obj.count("mesh")) {
                auto& mesh = obj.at("mesh").as_object();
                
                MeshData data;
                if (mesh.count("verticesBase64")) {
                    auto decoded = base64_decode(mesh.at("verticesBase64").as_string());
                    data.vertices.resize(decoded.size() / sizeof(float));
                    std::memcpy(data.vertices.data(), decoded.data(), decoded.size());
                }
                if (mesh.count("indicesBase64")) {
                    auto decoded = base64_decode(mesh.at("indicesBase64").as_string());
                    data.indices.resize(decoded.size() / sizeof(int32_t));
                    std::memcpy(data.indices.data(), decoded.data(), decoded.size());
                }
                
                return data;
            }
        }
    } catch (...) {
    }
    
    return std::nullopt;
}

bool NodePluginInstance::export_mesh(PluginContext* ctx,
                                      const MeshView& mesh,
                                      const std::string& path)
{
    if (!m_ipc || !m_ipc->is_connected()) return false;
    
    try {
        json params;
        params["path"] = path;
        
        json mesh_data;
        std::vector<uint8_t> vertex_bytes(
            reinterpret_cast<const uint8_t*>(mesh.vertices),
            reinterpret_cast<const uint8_t*>(mesh.vertices + mesh.vertex_count * 3)
        );
        mesh_data["verticesBase64"] = base64_encode(vertex_bytes);
        
        std::vector<uint8_t> index_bytes(
            reinterpret_cast<const uint8_t*>(mesh.indices),
            reinterpret_cast<const uint8_t*>(mesh.indices + mesh.triangle_count * 3)
        );
        mesh_data["indicesBase64"] = base64_encode(index_bytes);
        
        params["mesh"] = mesh_data;
        
        auto result = m_ipc->call("plugin.exportMesh", JsonValue(params.dump()), 60000);
        
        if (result.is_object()) {
            auto obj = result.as_object();
            return obj.count("success") && obj.at("success").as_bool();
        }
    } catch (...) {
    }
    
    return false;
}

void NodePluginInstance::handle_plugin_request(const IPCMessage& msg)
{
    if (!m_current_ctx) return;
    
    // Handle requests from plugin to host
    // These are forwarded through the IPC client's request handler
}

//==============================================================================
// Base64 Encoding/Decoding Utilities
//==============================================================================

static const char base64_chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

std::string base64_encode(const std::vector<uint8_t>& data)
{
    std::string ret;
    ret.reserve(((data.size() + 2) / 3) * 4);
    
    int i = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    
    const uint8_t* bytes_to_encode = data.data();
    size_t in_len = data.size();
    
    while (in_len--) {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            
            for (i = 0; i < 4; i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }
    
    if (i) {
        for (int j = i; j < 3; j++)
            char_array_3[j] = '\0';
        
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        
        for (int j = 0; j < i + 1; j++)
            ret += base64_chars[char_array_4[j]];
        
        while (i++ < 3)
            ret += '=';
    }
    
    return ret;
}

std::vector<uint8_t> base64_decode(const std::string& encoded)
{
    std::vector<uint8_t> ret;
    ret.reserve((encoded.size() * 3) / 4);
    
    int i = 0;
    int in_len = encoded.size();
    unsigned char char_array_4[4], char_array_3[3];
    
    auto is_base64 = [](unsigned char c) {
        return (isalnum(c) || (c == '+') || (c == '/'));
    };
    
    auto find_char = [](char c) -> int {
        const char* p = strchr(base64_chars, c);
        if (p) return p - base64_chars;
        return -1;
    };
    
    int in_ = 0;
    while (in_len-- && encoded[in_] != '=' && is_base64(encoded[in_])) {
        char_array_4[i++] = encoded[in_];
        in_++;
        if (i == 4) {
            for (i = 0; i < 4; i++)
                char_array_4[i] = find_char(char_array_4[i]);
            
            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
            
            for (i = 0; i < 3; i++)
                ret.push_back(char_array_3[i]);
            i = 0;
        }
    }
    
    if (i) {
        for (int j = 0; j < i; j++)
            char_array_4[j] = find_char(char_array_4[j]);
        
        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        
        for (int j = 0; j < i - 1; j++)
            ret.push_back(char_array_3[j]);
    }
    
    return ret;
}

//==============================================================================
// NodePluginRuntime Implementation
//==============================================================================

NodePluginRuntime::NodePluginRuntime()
{
}

NodePluginRuntime::~NodePluginRuntime()
{
    shutdown();
}

bool NodePluginRuntime::initialize()
{
    if (m_initialized) return true;
    
    // Find Node.js executable
    m_node_executable = find_node_executable();
    if (m_node_executable.empty()) {
        return false;
    }
    
    // Get Node.js version
    try {
        bp::ipstream pipe_stream;
        bp::child c(m_node_executable.string(), "--version", bp::std_out > pipe_stream);
        
        std::string line;
        if (std::getline(pipe_stream, line)) {
            m_node_version = line;
            boost::algorithm::trim(m_node_version);
        }
        c.wait();
    } catch (...) {
        m_node_version = "unknown";
    }
    
    // Create IPC server
    m_ipc_server = std::make_shared<IPCServer>();
    
    m_initialized = true;
    return true;
}

void NodePluginRuntime::shutdown()
{
    if (!m_initialized) return;
    
    // Unload all plugins
    for (auto& [id, plugin] : m_plugins) {
        plugin->on_unload();
    }
    m_plugins.clear();
    m_plugin_clients.clear();
    
    // Stop Node.js process
    stop_node_process();
    
    if (m_ipc_server) {
        m_ipc_server->stop();
        m_ipc_server.reset();
    }
    
    m_initialized = false;
}

bool NodePluginRuntime::is_available() const
{
    return m_initialized && !m_node_executable.empty();
}

std::shared_ptr<IPlugin> NodePluginRuntime::load_plugin(const PluginManifest& manifest,
                                                         const fs::path& path)
{
    if (!is_available()) return nullptr;
    
    // Ensure Node.js process is running
    if (!m_process_running) {
        if (!start_node_process()) {
            return nullptr;
        }
    }
    
    try {
        // Create pipes for IPC with this plugin
        bp::pipe stdin_pipe;
        bp::pipe stdout_pipe;
        
        // Spawn plugin-specific Node.js process
        std::string host_script = m_host_script_path.string();
        std::string plugin_path = path.string();
        
        bp::child plugin_process(
            m_node_executable.string(),
            host_script,
            "--plugin", plugin_path,
            "--id", manifest.id,
            bp::std_in < stdin_pipe,
            bp::std_out > stdout_pipe,
            bp::std_err > bp::null
        );
        
        // Create transport
        auto transport = std::make_shared<StdioTransport>(
            stdout_pipe.native_source(),
            stdin_pipe.native_sink()
        );
        
        // Create IPC client
        auto client = std::make_shared<IPCClient>(transport);
        
        // Set up request handler for host API calls
        client->set_request_handler([this, &manifest](const std::string& method, const JsonValue& params) -> JsonValue {
            return handle_host_request(manifest.id, method, params);
        });
        
        client->start();
        
        // Create plugin instance
        auto plugin = std::make_shared<NodePluginInstance>(manifest, client);
        
        // Store handles
        m_plugins[manifest.id] = plugin;
        m_plugin_clients[manifest.id] = client;
        
        // Detach process (it runs independently)
        plugin_process.detach();
        
        return plugin;
        
    } catch (const std::exception& e) {
        // Log error
        return nullptr;
    }
}

void NodePluginRuntime::unload_plugin(const std::string& plugin_id)
{
    auto it = m_plugins.find(plugin_id);
    if (it != m_plugins.end()) {
        it->second->on_unload();
        m_plugins.erase(it);
    }
    
    auto client_it = m_plugin_clients.find(plugin_id);
    if (client_it != m_plugin_clients.end()) {
        client_it->second->stop();
        m_plugin_clients.erase(client_it);
    }
}

fs::path NodePluginRuntime::find_node_executable()
{
    // Try common locations
    std::vector<fs::path> search_paths;
    
#ifdef _WIN32
    // Windows paths
    search_paths.push_back("C:/Program Files/nodejs/node.exe");
    search_paths.push_back("C:/Program Files (x86)/nodejs/node.exe");
    
    // Check PATH
    if (auto path_env = std::getenv("PATH")) {
        std::string path_str = path_env;
        std::vector<std::string> paths;
        boost::algorithm::split(paths, path_str, boost::is_any_of(";"));
        for (const auto& p : paths) {
            fs::path node_path = fs::path(p) / "node.exe";
            if (fs::exists(node_path)) {
                return node_path;
            }
        }
    }
#else
    // Unix paths
    search_paths.push_back("/usr/local/bin/node");
    search_paths.push_back("/usr/bin/node");
    search_paths.push_back("/opt/homebrew/bin/node"); // macOS ARM
    search_paths.push_back("/opt/local/bin/node"); // MacPorts
    
    // Use which command
    try {
        bp::ipstream pipe_stream;
        bp::child c("which", "node", bp::std_out > pipe_stream, bp::std_err > bp::null);
        
        std::string line;
        if (std::getline(pipe_stream, line)) {
            boost::algorithm::trim(line);
            if (!line.empty() && fs::exists(line)) {
                return fs::path(line);
            }
        }
        c.wait();
    } catch (...) {
    }
#endif
    
    // Try search paths
    for (const auto& path : search_paths) {
        if (fs::exists(path)) {
            return path;
        }
    }
    
    // Last resort: try boost::process::search_path
    try {
#ifdef _WIN32
        auto result = bp::search_path("node.exe");
#else
        auto result = bp::search_path("node");
#endif
        if (!result.empty()) {
            return fs::path(result.string());
        }
    } catch (...) {
    }
    
    return fs::path();
}

bool NodePluginRuntime::is_node_installed()
{
    return !find_node_executable().empty();
}

bool NodePluginRuntime::start_node_process()
{
    if (m_process_running) return true;
    
    // Find host script in resources
    // This should be set by the main application based on resources path
    if (m_host_script_path.empty()) {
        // Try to find it relative to executable
        // This will be set properly by PluginHost when initializing
        return false;
    }
    
    if (!fs::exists(m_host_script_path)) {
        return false;
    }
    
    m_process_running = true;
    return true;
}

void NodePluginRuntime::stop_node_process()
{
    m_process_running = false;
    
#ifdef _WIN32
    if (m_process_handle) {
        TerminateProcess(m_process_handle, 0);
        CloseHandle(m_process_handle);
        m_process_handle = nullptr;
    }
#else
    if (m_process_pid > 0) {
        kill(m_process_pid, SIGTERM);
        waitpid(m_process_pid, nullptr, 0);
        m_process_pid = -1;
    }
#endif
    
    // Close any open file descriptors/handles
#ifdef _WIN32
    if (m_stdin_write) { CloseHandle(m_stdin_write); m_stdin_write = nullptr; }
    if (m_stdout_read) { CloseHandle(m_stdout_read); m_stdout_read = nullptr; }
    if (m_stderr_read) { CloseHandle(m_stderr_read); m_stderr_read = nullptr; }
#else
    if (m_stdin_fd >= 0) { close(m_stdin_fd); m_stdin_fd = -1; }
    if (m_stdout_fd >= 0) { close(m_stdout_fd); m_stdout_fd = -1; }
    if (m_stderr_fd >= 0) { close(m_stderr_fd); m_stderr_fd = -1; }
#endif
    
    // Join I/O threads
    if (m_stdout_thread.joinable()) {
        m_stdout_thread.join();
    }
    if (m_stderr_thread.joinable()) {
        m_stderr_thread.join();
    }
}

bool NodePluginRuntime::restart_node_process()
{
    stop_node_process();
    return start_node_process();
}

void NodePluginRuntime::process_stdout_thread()
{
    // Read and process stdout from Node.js process
    // This is for the main host process if using shared process model
}

void NodePluginRuntime::process_stderr_thread()
{
    // Read stderr and log errors
}

void NodePluginRuntime::handle_ipc_message(const IPCMessage& msg)
{
    // Route message to appropriate plugin
}

JsonValue NodePluginRuntime::handle_host_request(const std::string& plugin_id,
                                                  const std::string& method,
                                                  const JsonValue& params)
{
    // Handle requests from plugin to host
    // This is called when a plugin calls sdk.getMesh(), etc.
    
    // Find the plugin's context and forward the request
    auto it = m_plugins.find(plugin_id);
    if (it == m_plugins.end()) {
        return JsonValue(JsonObject{{"error", JsonValue("Plugin not found")}});
    }
    
    // The actual implementation would forward to PluginContext
    // For now, return a placeholder
    return JsonValue(JsonObject{{"error", JsonValue("Not implemented")}});
}

//==============================================================================
// StdioTransport Implementation
//==============================================================================

StdioTransport::StdioTransport(int read_fd, int write_fd)
    : m_read_fd(read_fd)
    , m_write_fd(write_fd)
{
}

StdioTransport::~StdioTransport()
{
    close();
}

bool StdioTransport::send(const std::string& data)
{
    if (!m_connected) return false;
    
    std::lock_guard<std::mutex> lock(m_write_mutex);
    return write_message(data);
}

std::string StdioTransport::receive()
{
    if (!m_connected) return "";
    return read_message();
}

void StdioTransport::close()
{
    m_connected = false;
    
#ifdef _WIN32
    // Windows file descriptors are actually HANDLEs in disguise in this context
    // but for cross-platform simplicity we use the fd interface
#else
    if (m_read_fd >= 0) {
        ::close(m_read_fd);
        m_read_fd = -1;
    }
    if (m_write_fd >= 0) {
        ::close(m_write_fd);
        m_write_fd = -1;
    }
#endif
}

bool StdioTransport::is_connected() const
{
    return m_connected;
}

bool StdioTransport::write_message(const std::string& data)
{
    // Length-prefixed message format:
    // 4 bytes little-endian length + data
    
    uint32_t len = static_cast<uint32_t>(data.size());
    uint8_t header[4];
    header[0] = len & 0xFF;
    header[1] = (len >> 8) & 0xFF;
    header[2] = (len >> 16) & 0xFF;
    header[3] = (len >> 24) & 0xFF;
    
#ifdef _WIN32
    DWORD written;
    if (!WriteFile(reinterpret_cast<HANDLE>(_get_osfhandle(m_write_fd)), 
                   header, 4, &written, nullptr) || written != 4) {
        return false;
    }
    if (!WriteFile(reinterpret_cast<HANDLE>(_get_osfhandle(m_write_fd)),
                   data.data(), len, &written, nullptr) || written != len) {
        return false;
    }
#else
    if (write(m_write_fd, header, 4) != 4) {
        return false;
    }
    if (write(m_write_fd, data.data(), len) != static_cast<ssize_t>(len)) {
        return false;
    }
#endif
    
    return true;
}

std::string StdioTransport::read_message()
{
    // Read length prefix
    uint8_t header[4];
    
#ifdef _WIN32
    DWORD bytesRead;
    if (!ReadFile(reinterpret_cast<HANDLE>(_get_osfhandle(m_read_fd)),
                  header, 4, &bytesRead, nullptr) || bytesRead != 4) {
        m_connected = false;
        return "";
    }
#else
    ssize_t n = read(m_read_fd, header, 4);
    if (n != 4) {
        m_connected = false;
        return "";
    }
#endif
    
    uint32_t len = header[0] | (header[1] << 8) | (header[2] << 16) | (header[3] << 24);
    
    if (len > 100 * 1024 * 1024) { // 100MB sanity limit
        m_connected = false;
        return "";
    }
    
    // Read message body
    std::string data(len, '\0');
    
#ifdef _WIN32
    if (!ReadFile(reinterpret_cast<HANDLE>(_get_osfhandle(m_read_fd)),
                  &data[0], len, &bytesRead, nullptr) || bytesRead != len) {
        m_connected = false;
        return "";
    }
#else
    size_t total_read = 0;
    while (total_read < len) {
        ssize_t r = read(m_read_fd, &data[total_read], len - total_read);
        if (r <= 0) {
            m_connected = false;
            return "";
        }
        total_read += r;
    }
#endif
    
    return data;
}

//==============================================================================
// IPCClient Implementation
//==============================================================================

IPCClient::IPCClient(std::shared_ptr<IPCTransport> transport)
    : m_transport(transport)
{
}

IPCClient::~IPCClient()
{
    stop();
}

void IPCClient::start()
{
    if (m_running) return;
    
    m_running = true;
    m_message_thread = std::thread(&IPCClient::message_loop, this);
}

void IPCClient::stop()
{
    m_running = false;
    
    if (m_transport) {
        m_transport->close();
    }
    
    // Wake up any waiting callers
    {
        std::lock_guard<std::mutex> lock(m_pending_mutex);
        for (auto& [id, promise] : m_pending_requests) {
            IPCMessage error_msg;
            error_msg.type = IPCMessageType::Response;
            error_msg.id = id;
            error_msg.error = "Connection closed";
            error_msg.error_code = -1;
            promise.set_value(error_msg);
        }
        m_pending_requests.clear();
    }
    
    if (m_message_thread.joinable()) {
        m_message_thread.join();
    }
}

JsonValue IPCClient::call(const std::string& method, const JsonValue& params, int timeout_ms)
{
    if (!m_transport || !m_transport->is_connected()) {
        throw std::runtime_error("Not connected");
    }
    
    int64_t id = m_next_id++;
    
    // Create request message
    IPCMessage msg = IPCMessage::request(method, params);
    msg.id = id;
    
    // Create promise for response
    std::promise<IPCMessage> promise;
    auto future = promise.get_future();
    
    {
        std::lock_guard<std::mutex> lock(m_pending_mutex);
        m_pending_requests[id] = std::move(promise);
    }
    
    // Send request
    if (!m_transport->send(msg.serialize())) {
        std::lock_guard<std::mutex> lock(m_pending_mutex);
        m_pending_requests.erase(id);
        throw std::runtime_error("Failed to send request");
    }
    
    // Wait for response
    auto status = future.wait_for(std::chrono::milliseconds(timeout_ms));
    if (status == std::future_status::timeout) {
        std::lock_guard<std::mutex> lock(m_pending_mutex);
        m_pending_requests.erase(id);
        throw std::runtime_error("Request timeout");
    }
    
    IPCMessage response = future.get();
    
    if (response.error_code != 0) {
        throw std::runtime_error(response.error);
    }
    
    return response.result;
}

void IPCClient::call_async(const std::string& method, const JsonValue& params,
                           std::function<void(const JsonValue& result, const std::string& error)> callback)
{
    // Async call implementation - spawn thread to avoid blocking
    std::thread([this, method, params, callback]() {
        try {
            auto result = call(method, params);
            callback(result, "");
        } catch (const std::exception& e) {
            callback(JsonValue(), e.what());
        }
    }).detach();
}

void IPCClient::notify(const std::string& method, const JsonValue& params)
{
    if (!m_transport || !m_transport->is_connected()) return;
    
    IPCMessage msg = IPCMessage::notification(method, params);
    m_transport->send(msg.serialize());
}

void IPCClient::update_progress(int percent, const std::string& message)
{
    notify("progress", JsonValue(JsonObject{
        {"percent", JsonValue(percent)},
        {"message", JsonValue(message)}
    }));
}

void IPCClient::set_request_handler(RequestHandler handler)
{
    m_request_handler = handler;
}

void IPCClient::set_notification_handler(NotificationHandler handler)
{
    m_notification_handler = handler;
}

bool IPCClient::is_connected() const
{
    return m_transport && m_transport->is_connected();
}

void IPCClient::message_loop()
{
    while (m_running && m_transport && m_transport->is_connected()) {
        std::string data = m_transport->receive();
        if (data.empty()) {
            if (!m_running) break;
            continue;
        }
        
        auto msg = IPCMessage::parse(data);
        if (msg) {
            handle_message(*msg);
        }
    }
}

void IPCClient::handle_message(const IPCMessage& msg)
{
    switch (msg.type) {
    case IPCMessageType::Response:
        // Match with pending request
        {
            std::lock_guard<std::mutex> lock(m_pending_mutex);
            auto it = m_pending_requests.find(msg.id);
            if (it != m_pending_requests.end()) {
                it->second.set_value(msg);
                m_pending_requests.erase(it);
            }
        }
        break;
        
    case IPCMessageType::Request:
    case IPCMessageType::PluginRequest:
        // Handle incoming request
        if (m_request_handler) {
            JsonValue result = m_request_handler(msg.method, msg.params);
            IPCMessage response = IPCMessage::response(msg.id, result);
            m_transport->send(response.serialize());
        }
        break;
        
    case IPCMessageType::Notification:
        if (m_notification_handler) {
            m_notification_handler(msg.method, msg.params);
        }
        break;
        
    default:
        break;
    }
}

//==============================================================================
// IPCMessage Implementation
//==============================================================================

std::string IPCMessage::serialize() const
{
    json j;
    j["jsonrpc"] = "2.0";
    
    switch (type) {
    case IPCMessageType::Request:
    case IPCMessageType::PluginRequest:
        j["id"] = id;
        j["method"] = method;
        if (!params.is_null()) {
            j["params"] = json::parse(params.serialize());
        }
        break;
        
    case IPCMessageType::Response:
        j["id"] = id;
        if (error_code != 0) {
            j["error"] = {{"code", error_code}, {"message", error}};
        } else {
            if (!result.is_null()) {
                j["result"] = json::parse(result.serialize());
            } else {
                j["result"] = nullptr;
            }
        }
        break;
        
    case IPCMessageType::Notification:
        j["method"] = method;
        if (!params.is_null()) {
            j["params"] = json::parse(params.serialize());
        }
        break;
        
    case IPCMessageType::Progress:
        j["method"] = "progress";
        j["params"] = json::parse(params.serialize());
        break;
        
    default:
        break;
    }
    
    if (!plugin_id.empty()) {
        j["pluginId"] = plugin_id;
    }
    
    return j.dump();
}

std::optional<IPCMessage> IPCMessage::parse(const std::string& data)
{
    try {
        json j = json::parse(data);
        
        IPCMessage msg;
        
        if (j.contains("id")) {
            msg.id = j["id"].get<int64_t>();
        }
        
        if (j.contains("method")) {
            msg.method = j["method"].get<std::string>();
            
            if (j.contains("id")) {
                msg.type = IPCMessageType::Request;
            } else {
                msg.type = IPCMessageType::Notification;
            }
            
            if (j.contains("params")) {
                msg.params = JsonValue(j["params"].dump());
            }
        } else if (j.contains("result") || j.contains("error")) {
            msg.type = IPCMessageType::Response;
            
            if (j.contains("error")) {
                msg.error_code = j["error"]["code"].get<int>();
                msg.error = j["error"]["message"].get<std::string>();
            } else {
                msg.result = JsonValue(j["result"].dump());
            }
        }
        
        if (j.contains("pluginId")) {
            msg.plugin_id = j["pluginId"].get<std::string>();
        }
        
        return msg;
        
    } catch (...) {
        return std::nullopt;
    }
}

IPCMessage IPCMessage::request(const std::string& method, const JsonValue& params)
{
    IPCMessage msg;
    msg.type = IPCMessageType::Request;
    msg.method = method;
    msg.params = params;
    return msg;
}

IPCMessage IPCMessage::response(int64_t id, const JsonValue& result)
{
    IPCMessage msg;
    msg.type = IPCMessageType::Response;
    msg.id = id;
    msg.result = result;
    return msg;
}

IPCMessage IPCMessage::error_response(int64_t id, int code, const std::string& message)
{
    IPCMessage msg;
    msg.type = IPCMessageType::Response;
    msg.id = id;
    msg.error_code = code;
    msg.error = message;
    return msg;
}

IPCMessage IPCMessage::notification(const std::string& method, const JsonValue& params)
{
    IPCMessage msg;
    msg.type = IPCMessageType::Notification;
    msg.method = method;
    msg.params = params;
    return msg;
}

IPCMessage IPCMessage::progress(int percent, const std::string& message)
{
    IPCMessage msg;
    msg.type = IPCMessageType::Progress;
    msg.params = JsonValue(JsonObject{
        {"percent", JsonValue(percent)},
        {"message", JsonValue(message)}
    });
    return msg;
}

//==============================================================================
// JsonValue Implementation (simplified serialization)
//==============================================================================

JsonValue JsonValue::s_null;

bool JsonValue::as_bool(bool default_val) const
{
    if (std::holds_alternative<bool>(m_value)) {
        return std::get<bool>(m_value);
    }
    return default_val;
}

int64_t JsonValue::as_int(int64_t default_val) const
{
    if (std::holds_alternative<int64_t>(m_value)) {
        return std::get<int64_t>(m_value);
    }
    if (std::holds_alternative<double>(m_value)) {
        return static_cast<int64_t>(std::get<double>(m_value));
    }
    return default_val;
}

double JsonValue::as_double(double default_val) const
{
    if (std::holds_alternative<double>(m_value)) {
        return std::get<double>(m_value);
    }
    if (std::holds_alternative<int64_t>(m_value)) {
        return static_cast<double>(std::get<int64_t>(m_value));
    }
    return default_val;
}

const std::string& JsonValue::as_string() const
{
    static std::string empty;
    if (std::holds_alternative<std::string>(m_value)) {
        return std::get<std::string>(m_value);
    }
    return empty;
}

const JsonArray& JsonValue::as_array() const
{
    static JsonArray empty;
    if (std::holds_alternative<JsonArray>(m_value)) {
        return std::get<JsonArray>(m_value);
    }
    return empty;
}

const JsonObject& JsonValue::as_object() const
{
    static JsonObject empty;
    if (std::holds_alternative<JsonObject>(m_value)) {
        return std::get<JsonObject>(m_value);
    }
    return empty;
}

JsonValue& JsonValue::operator[](const std::string& key)
{
    if (!std::holds_alternative<JsonObject>(m_value)) {
        m_value = JsonObject();
    }
    return std::get<JsonObject>(m_value)[key];
}

const JsonValue& JsonValue::operator[](const std::string& key) const
{
    if (std::holds_alternative<JsonObject>(m_value)) {
        const auto& obj = std::get<JsonObject>(m_value);
        auto it = obj.find(key);
        if (it != obj.end()) {
            return it->second;
        }
    }
    return s_null;
}

JsonValue& JsonValue::operator[](size_t index)
{
    if (!std::holds_alternative<JsonArray>(m_value)) {
        m_value = JsonArray();
    }
    auto& arr = std::get<JsonArray>(m_value);
    if (index >= arr.size()) {
        arr.resize(index + 1);
    }
    return arr[index];
}

const JsonValue& JsonValue::operator[](size_t index) const
{
    if (std::holds_alternative<JsonArray>(m_value)) {
        const auto& arr = std::get<JsonArray>(m_value);
        if (index < arr.size()) {
            return arr[index];
        }
    }
    return s_null;
}

JsonValue JsonValue::parse(const std::string& json_str)
{
    try {
        json j = json::parse(json_str);
        
        std::function<JsonValue(const json&)> convert = [&convert](const json& j) -> JsonValue {
            if (j.is_null()) return JsonValue(nullptr);
            if (j.is_boolean()) return JsonValue(j.get<bool>());
            if (j.is_number_integer()) return JsonValue(j.get<int64_t>());
            if (j.is_number_float()) return JsonValue(j.get<double>());
            if (j.is_string()) return JsonValue(j.get<std::string>());
            if (j.is_array()) {
                JsonArray arr;
                for (const auto& item : j) {
                    arr.push_back(convert(item));
                }
                return JsonValue(std::move(arr));
            }
            if (j.is_object()) {
                JsonObject obj;
                for (auto& [key, val] : j.items()) {
                    obj[key] = convert(val);
                }
                return JsonValue(std::move(obj));
            }
            return JsonValue();
        };
        
        return convert(j);
    } catch (...) {
        return JsonValue();
    }
}

std::string JsonValue::serialize(bool pretty) const
{
    std::function<json(const JsonValue&)> convert = [&convert](const JsonValue& v) -> json {
        if (v.is_null()) return nullptr;
        if (v.is_bool()) return v.as_bool();
        if (std::holds_alternative<int64_t>(v.m_value)) return v.as_int();
        if (std::holds_alternative<double>(v.m_value)) return v.as_double();
        if (v.is_string()) return v.as_string();
        if (v.is_array()) {
            json arr = json::array();
            for (const auto& item : v.as_array()) {
                arr.push_back(convert(item));
            }
            return arr;
        }
        if (v.is_object()) {
            json obj = json::object();
            for (const auto& [key, val] : v.as_object()) {
                obj[key] = convert(val);
            }
            return obj;
        }
        return nullptr;
    };
    
    json j = convert(*this);
    return pretty ? j.dump(2) : j.dump();
}

//==============================================================================
// IPCServer Implementation
//==============================================================================

IPCServer::IPCServer()
{
}

IPCServer::~IPCServer()
{
    stop();
}

bool IPCServer::start()
{
    return true;
}

void IPCServer::stop()
{
    std::lock_guard<std::mutex> lock(m_clients_mutex);
    for (auto& [id, client] : m_clients) {
        client->stop();
    }
    m_clients.clear();
}

std::shared_ptr<IPCClient> IPCServer::accept_connection(int read_fd, int write_fd,
                                                          const std::string& plugin_id)
{
    auto transport = std::make_shared<StdioTransport>(read_fd, write_fd);
    auto client = std::make_shared<IPCClient>(transport);
    
    {
        std::lock_guard<std::mutex> lock(m_clients_mutex);
        m_clients[plugin_id] = client;
    }
    
    if (m_connection_handler) {
        m_connection_handler(plugin_id, client);
    }
    
    return client;
}

void IPCServer::set_connection_handler(ConnectionHandler handler)
{
    m_connection_handler = handler;
}

void IPCServer::set_disconnection_handler(DisconnectionHandler handler)
{
    m_disconnection_handler = handler;
}

std::shared_ptr<IPCClient> IPCServer::get_client(const std::string& plugin_id)
{
    std::lock_guard<std::mutex> lock(m_clients_mutex);
    auto it = m_clients.find(plugin_id);
    if (it != m_clients.end()) {
        return it->second;
    }
    return nullptr;
}

void IPCServer::remove_client(const std::string& plugin_id)
{
    std::lock_guard<std::mutex> lock(m_clients_mutex);
    auto it = m_clients.find(plugin_id);
    if (it != m_clients.end()) {
        it->second->stop();
        m_clients.erase(it);
    }
    
    if (m_disconnection_handler) {
        m_disconnection_handler(plugin_id);
    }
}

} // namespace Plugin
} // namespace Slic3r

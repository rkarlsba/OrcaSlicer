// OrcaSlicer Plugin Host Implementation
// Copyright (C) 2024 OrcaSlicer Contributors
// License: AGPL-3.0

#include "PluginHost.hpp"
#include "NodePluginRuntime.hpp"
#include "PluginContext.hpp"

#include "libslic3r/Utils.hpp"
#include "libslic3r/Platform.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <fstream>
#include <sstream>
#include <algorithm>

// JSON parsing (using nlohmann/json already in deps)
#include <nlohmann/json.hpp>

namespace Slic3r {
namespace Plugin {

using json = nlohmann::json;

//==============================================================================
// Global Plugin Host Singleton
//==============================================================================

static std::unique_ptr<PluginHost> s_plugin_host;

PluginHost& plugin_host() {
    if (!s_plugin_host) {
        s_plugin_host = std::make_unique<PluginHost>();
    }
    return *s_plugin_host;
}

PluginHost* PluginHost::instance() {
    return &plugin_host();
}

//==============================================================================
// PluginHost Implementation
//==============================================================================

PluginHost::PluginHost() {
    // Initialize default search paths
    m_search_paths.push_back(get_system_plugins_dir());
    m_search_paths.push_back(get_user_plugins_dir());
}

PluginHost::~PluginHost() {
    shutdown();
}

void PluginHost::initialize(GUI::Plater* plater, GUI::Worker* worker) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_initialized) return;
    
    m_plater = plater;
    m_worker = worker;
    
    // Initialize runtimes
    // Note: set_host_script_path and initialize() must be called BEFORE is_available(),
    // because is_available() requires m_initialized == true (set inside initialize()).
    auto node_runtime = std::make_shared<NodePluginRuntime>();
    auto host_script = std::filesystem::path(resources_dir()) / "plugins" / "node-host" / "index.js";
    node_runtime->set_host_script_path(host_script);
    fprintf(stderr, "[Plugin] host_script=%s exists=%d\n",
            host_script.c_str(), (int)std::filesystem::exists(host_script));
    if (node_runtime->initialize()) {
        m_runtimes[PluginType::JavaScript] = node_runtime;
        fprintf(stderr, "[Plugin] Node.js runtime initialized, version: %s\n",
                node_runtime->version().c_str());
        BOOST_LOG_TRIVIAL(info) << "Plugin: Node.js runtime initialized, version: "
                                << node_runtime->version();
    } else {
        fprintf(stderr, "[Plugin] Node.js runtime unavailable (node not installed or host script missing)\n");
        BOOST_LOG_TRIVIAL(warning) << "Plugin: Node.js runtime unavailable (node not installed or host script missing)";
    }
    
    // Future: Initialize Python runtime
    // auto python_runtime = std::make_shared<PythonPluginRuntime>();
    // ...
    
    // Load configurations
    load_configs();
    
    // Scan for plugins
    scan_plugin_directories();
    
    m_initialized = true;
    
    fprintf(stderr, "[Plugin] Host initialized, %zu plugin(s) discovered\n", m_plugins.size());
    BOOST_LOG_TRIVIAL(info) << "Plugin: Host initialized with " << m_plugins.size() << " plugins discovered";
    
    // Note: Plugins will be loaded lazily or on demand, not during startup
    // to avoid blocking the GUI thread. Call load_all_enabled() separately
    // from a background thread if automatic loading is desired.
}

void PluginHost::shutdown() {
    std::unique_lock<std::mutex> lock(m_mutex);
    
    if (!m_initialized) return;
    
    BOOST_LOG_TRIVIAL(info) << "Plugin: Shutting down host";
    
    // Save configurations
    save_configs();
    
    // Collect loaded instances to unload, clearing them from the map immediately
    // so the state is consistent before we release the lock.
    std::vector<std::pair<std::string, std::shared_ptr<IPlugin>>> to_unload;
    for (auto& [id, info] : m_plugins) {
        if (info.state == PluginState::Loaded && info.instance) {
            to_unload.emplace_back(id, std::move(info.instance));
            info.instance.reset();
            info.state = PluginState::Unloaded;
        }
    }
    
    // Release the lock before calling on_unload() — on_unload() sends
    // an IPC message to the node process and waits for a response, so
    // holding m_mutex here would block any concurrent plugin callbacks.
    lock.unlock();
    
    for (auto& [id, instance] : to_unload) {
        try {
            instance->on_unload();
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "Plugin: Error unloading " << id << ": " << e.what();
        }
    }
    to_unload.clear();
    
    lock.lock();
    
    // Shutdown all runtimes
    for (auto& [type, runtime] : m_runtimes) {
        runtime->shutdown();
    }
    m_runtimes.clear();
    
    m_plater = nullptr;
    m_worker = nullptr;
    m_initialized = false;
}

//------------------------------------------------------------------------------
// Plugin Discovery
//------------------------------------------------------------------------------

void PluginHost::scan_plugin_directories() {
    BOOST_LOG_TRIVIAL(debug) << "Plugin: Scanning plugin directories";
    
    for (const auto& search_path : m_search_paths) {
        if (!std::filesystem::exists(search_path)) {
            BOOST_LOG_TRIVIAL(debug) << "Plugin: Search path does not exist: " << search_path;
            continue;
        }
        
        for (const auto& entry : std::filesystem::directory_iterator(search_path)) {
            if (!entry.is_directory()) continue;
            
            auto manifest = parse_manifest(entry.path());
            if (!manifest) continue;
            
            if (m_plugins.find(manifest->id) != m_plugins.end()) {
                // Plugin already registered (possibly from another search path)
                // TODO: Handle version comparison
                continue;
            }
            
            PluginInfo info;
            info.manifest = *manifest;
            info.install_path = entry.path();
            info.state = PluginState::Unloaded;
            
            // Check if user has disabled this plugin
            auto it = m_configs.find(manifest->id);
            if (it != m_configs.end()) {
                auto enabled_it = it->second.find("enabled");
                if (enabled_it != it->second.end()) {
                    info.user_enabled = (enabled_it->second == "true" || enabled_it->second == "1");
                }
            }
            
            m_plugins[manifest->id] = std::move(info);
            BOOST_LOG_TRIVIAL(info) << "Plugin: Discovered " << manifest->id 
                                    << " v" << manifest->version 
                                    << " at " << entry.path();
        }
    }
}

std::optional<PluginManifest> PluginHost::parse_manifest(const std::filesystem::path& plugin_dir) {
    // Try plugin.json first (our format), then package.json (Node.js)
    std::vector<std::filesystem::path> manifest_paths = {
        plugin_dir / "plugin.json",
        plugin_dir / "orca-plugin.json",
        plugin_dir / "package.json"
    };
    
    for (const auto& manifest_path : manifest_paths) {
        if (!std::filesystem::exists(manifest_path)) continue;
        
        try {
            std::ifstream file(manifest_path);
            if (!file.is_open()) continue;
            
            json j;
            file >> j;
            
            PluginManifest manifest;
            
            // Handle different manifest formats
            if (manifest_path.filename() == "package.json") {
                // Node.js package.json format
                if (!j.contains("orcaPlugin")) continue;  // Not an OrcaSlicer plugin
                
                auto& orca = j["orcaPlugin"];
                manifest.id = j.value("name", "");
                manifest.name = orca.value("displayName", j.value("name", ""));
                manifest.version = j.value("version", "0.0.0");
                manifest.description = j.value("description", "");
                manifest.author = j.value("author", "");
                manifest.license = j.value("license", "");
                manifest.homepage = j.value("homepage", "");
                manifest.entry_point = j.value("main", "index.js");
                manifest.type = PluginType::JavaScript;
                
                // Parse capabilities
                uint32_t caps = 0;
                if (orca.contains("capabilities")) {
                    for (const auto& cap : orca["capabilities"]) {
                        std::string cap_str = cap.get<std::string>();
                        if (cap_str == "meshModification") caps |= static_cast<uint32_t>(PluginCapability::MeshModification);
                        else if (cap_str == "meshImport") caps |= static_cast<uint32_t>(PluginCapability::MeshImport);
                        else if (cap_str == "meshExport") caps |= static_cast<uint32_t>(PluginCapability::MeshExport);
                        else if (cap_str == "gcodePostProcess") caps |= static_cast<uint32_t>(PluginCapability::GCodePostProcess);
                        else if (cap_str == "uiExtension") caps |= static_cast<uint32_t>(PluginCapability::UIExtension);
                        else if (cap_str == "textureMaterial") caps |= static_cast<uint32_t>(PluginCapability::TextureMaterial);
                    }
                }
                manifest.capabilities = static_cast<PluginCapability>(caps);
                
                if (orca.contains("configSchema")) {
                    manifest.config_schema = orca["configSchema"].dump();
                }
                
                if (orca.contains("icon")) {
                    manifest.icon_path = (plugin_dir / orca.value("icon", "")).string();
                }
                
            } else {
                // Our native plugin.json format
                manifest.id = j.value("id", "");
                manifest.name = j.value("name", "");
                manifest.version = j.value("version", "0.0.0");
                manifest.description = j.value("description", "");
                manifest.author = j.value("author", "");
                manifest.license = j.value("license", "");
                manifest.homepage = j.value("homepage", "");
                manifest.entry_point = j.value("entryPoint", "");
                
                std::string type_str = j.value("type", "javascript");
                if (type_str == "native" || type_str == "cpp") manifest.type = PluginType::Native;
                else if (type_str == "python") manifest.type = PluginType::Python;
                else manifest.type = PluginType::JavaScript;
                
                uint32_t caps = 0;
                if (j.contains("capabilities")) {
                    for (const auto& cap : j["capabilities"]) {
                        std::string cap_str = cap.get<std::string>();
                        if (cap_str == "meshModification") caps |= static_cast<uint32_t>(PluginCapability::MeshModification);
                        else if (cap_str == "meshImport") caps |= static_cast<uint32_t>(PluginCapability::MeshImport);
                        else if (cap_str == "meshExport") caps |= static_cast<uint32_t>(PluginCapability::MeshExport);
                        else if (cap_str == "gcodePostProcess") caps |= static_cast<uint32_t>(PluginCapability::GCodePostProcess);
                        else if (cap_str == "uiExtension") caps |= static_cast<uint32_t>(PluginCapability::UIExtension);
                        else if (cap_str == "textureMaterial") caps |= static_cast<uint32_t>(PluginCapability::TextureMaterial);
                    }
                }
                manifest.capabilities = static_cast<PluginCapability>(caps);
                
                if (j.contains("configSchema")) {
                    manifest.config_schema = j["configSchema"].dump();
                }
                
                if (j.contains("icon")) {
                    manifest.icon_path = (plugin_dir / j.value("icon", "")).string();
                }
            }
            
            if (!manifest.is_valid()) {
                BOOST_LOG_TRIVIAL(warning) << "Plugin: Invalid manifest at " << manifest_path;
                continue;
            }
            
            return manifest;
            
        } catch (const json::exception& e) {
            BOOST_LOG_TRIVIAL(warning) << "Plugin: Failed to parse manifest " 
                                       << manifest_path << ": " << e.what();
        }
    }
    
    return std::nullopt;
}

std::vector<PluginInfo> PluginHost::get_plugins() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<PluginInfo> result;
    result.reserve(m_plugins.size());
    for (const auto& [id, info] : m_plugins) {
        result.push_back(info);
    }
    return result;
}

std::optional<PluginInfo> PluginHost::get_plugin(const std::string& plugin_id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_plugins.find(plugin_id);
    if (it == m_plugins.end()) return std::nullopt;
    return it->second;
}

//------------------------------------------------------------------------------
// Plugin Lifecycle
//------------------------------------------------------------------------------

bool PluginHost::load_plugin(const std::string& plugin_id) {
    std::unique_lock<std::mutex> lock(m_mutex);
    
    auto it = m_plugins.find(plugin_id);
    if (it == m_plugins.end()) {
        BOOST_LOG_TRIVIAL(error) << "Plugin: Unknown plugin " << plugin_id;
        return false;
    }
    
    auto& info = it->second;
    if (info.state == PluginState::Loaded) {
        return true;  // Already loaded
    }
    
    if (!info.user_enabled) {
        BOOST_LOG_TRIVIAL(info) << "Plugin: " << plugin_id << " is disabled by user";
        return false;
    }
    
    // Check dependencies
    if (!check_dependencies(plugin_id)) {
        info.state = PluginState::Error;
        info.error_message = "Missing dependencies";
        lock.unlock();
        fire_event(plugin_id, PluginEvent::Error);
        return false;
    }
    
    // Get runtime for this plugin type
    auto runtime = get_runtime(info.manifest.type);
    if (!runtime) {
        info.state = PluginState::Error;
        info.error_message = "No runtime available for plugin type";
        BOOST_LOG_TRIVIAL(error) << "Plugin: No runtime for " << plugin_id 
                                  << " (type " << static_cast<int>(info.manifest.type) << ")";
        lock.unlock();
        fire_event(plugin_id, PluginEvent::Error);
        return false;
    }
    
    // Load plugin
    info.state = PluginState::Loading;
    fprintf(stderr, "[Plugin] Loading %s\n", plugin_id.c_str());
    BOOST_LOG_TRIVIAL(info) << "Plugin: Loading " << plugin_id;
    
    try {
        auto instance = runtime->load_plugin(info.manifest, info.install_path);
        if (!instance) {
            info.state = PluginState::Error;
            info.error_message = "Failed to create plugin instance";
            lock.unlock();
            fire_event(plugin_id, PluginEvent::Error);
            return false;
        }
        
        // Store instance and create context while lock is held
        info.instance = instance;
        auto ctx = create_context(plugin_id);
        
        // MUST release m_mutex before calling on_load():
        // on_load() blocks until the node process responds to "loadPlugin",
        // but during that time the node process will call back into
        // registerSubmenu / registerMenuItem — those also need m_mutex.
        // Holding m_mutex here would cause a deadlock.
        lock.unlock();
        
        bool loaded = false;
        std::string load_error;
        try {
            loaded = instance->on_load(ctx.get());
            if (!loaded) load_error = "Plugin on_load() returned false";
        } catch (const std::exception& ex) {
            load_error = ex.what();
        }
        
        lock.lock();
        it = m_plugins.find(plugin_id);
        if (it == m_plugins.end()) return false;
        auto& info2 = it->second;
        
        if (!loaded) {
            info2.state = PluginState::Error;
            info2.error_message = load_error;
            info2.instance.reset();
            BOOST_LOG_TRIVIAL(error) << "Plugin: Failed to load " << plugin_id << ": " << load_error;
            lock.unlock();
            fire_event(plugin_id, PluginEvent::Error);
            return false;
        }
        
        info2.state = PluginState::Loaded;
        info2.error_message.clear();
        fprintf(stderr, "[Plugin] %s loaded successfully\n", plugin_id.c_str());
        BOOST_LOG_TRIVIAL(info) << "Plugin: Loaded " << plugin_id << " successfully";
        lock.unlock();
        fire_event(plugin_id, PluginEvent::Loaded);
        return true;
        
    } catch (const std::exception& e) {
        if (!lock.owns_lock()) lock.lock();
        it = m_plugins.find(plugin_id);
        if (it != m_plugins.end()) {
            auto& info3 = it->second;
            info3.state = PluginState::Error;
            info3.error_message = e.what();
            info3.instance.reset();
        }
        BOOST_LOG_TRIVIAL(error) << "Plugin: Exception loading " << plugin_id << ": " << e.what();
        lock.unlock();
        fire_event(plugin_id, PluginEvent::Error);
        return false;
    }
}

bool PluginHost::unload_plugin(const std::string& plugin_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_plugins.find(plugin_id);
    if (it == m_plugins.end()) return false;
    
    auto& info = it->second;
    if (info.state != PluginState::Loaded || !info.instance) {
        return true;  // Already unloaded
    }
    
    BOOST_LOG_TRIVIAL(info) << "Plugin: Unloading " << plugin_id;
    
    try {
        info.instance->on_unload();
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Plugin: Exception in on_unload for " << plugin_id << ": " << e.what();
    }
    
    // Tell runtime to unload
    auto runtime = get_runtime(info.manifest.type);
    if (runtime) {
        runtime->unload_plugin(plugin_id);
    }
    
    info.instance.reset();
    info.state = PluginState::Unloaded;
    
    fire_event(plugin_id, PluginEvent::Unloaded);
    return true;
}

bool PluginHost::reload_plugin(const std::string& plugin_id) {
    unload_plugin(plugin_id);
    return load_plugin(plugin_id);
}

void PluginHost::set_plugin_enabled(const std::string& plugin_id, bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_plugins.find(plugin_id);
    if (it == m_plugins.end()) return;
    
    it->second.user_enabled = enabled;
    m_configs[plugin_id]["enabled"] = enabled ? "true" : "false";
    
    if (!enabled && it->second.state == PluginState::Loaded) {
        // Need to unlock before calling unload_plugin
        m_mutex.unlock();
        unload_plugin(plugin_id);
        m_mutex.lock();
    }
}

void PluginHost::load_all_enabled() {
    // Make a copy of IDs to avoid holding lock during load
    std::vector<std::string> to_load;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& [id, info] : m_plugins) {
            if (info.user_enabled && info.state == PluginState::Unloaded) {
                to_load.push_back(id);
            }
        }
    }
    
    for (const auto& id : to_load) {
        load_plugin(id);
    }
}

void PluginHost::unload_all() {
    std::vector<std::string> to_unload;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& [id, info] : m_plugins) {
            if (info.state == PluginState::Loaded) {
                to_unload.push_back(id);
            }
        }
    }
    
    for (const auto& id : to_unload) {
        unload_plugin(id);
    }
}

//------------------------------------------------------------------------------
// Plugin Operations
//------------------------------------------------------------------------------

std::vector<std::string> PluginHost::get_plugins_with_capability(PluginCapability cap) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<std::string> result;
    for (const auto& [id, info] : m_plugins) {
        if (info.state == PluginState::Loaded && 
            has_capability(info.manifest.capabilities, cap)) {
            result.push_back(id);
        }
    }
    return result;
}

bool PluginHost::execute_mesh_plugin(const std::string& plugin_id,
                                      const std::string& operation,
                                      const std::map<std::string, std::string>& params) {
    std::shared_ptr<IPlugin> instance;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_plugins.find(plugin_id);
        if (it == m_plugins.end() || it->second.state != PluginState::Loaded) {
            BOOST_LOG_TRIVIAL(error) << "Plugin: " << plugin_id << " not loaded";
            return false;
        }
        instance = it->second.instance;
    }
    
    if (!instance) return false;
    
    auto ctx = create_context(plugin_id);
    
    // Get selected objects/volumes
    // This would iterate through selection and call process_mesh for each
    // TODO: Integration with Plater selection
    
    // For now, process first object/volume as demo
    return instance->process_mesh(ctx.get(), 0, 0, operation);
}

std::string PluginHost::execute_gcode_plugins(const std::string& gcode) {
    std::string result = gcode;
    
    auto plugins = get_plugins_with_capability(PluginCapability::GCodePostProcess);
    for (const auto& plugin_id : plugins) {
        std::shared_ptr<IPlugin> instance;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_plugins.find(plugin_id);
            if (it == m_plugins.end()) continue;
            instance = it->second.instance;
        }
        
        if (instance) {
            auto ctx = create_context(plugin_id);
            result = instance->process_gcode(ctx.get(), result);
        }
    }
    
    return result;
}

//------------------------------------------------------------------------------
// Event System
//------------------------------------------------------------------------------

int PluginHost::add_event_listener(PluginEventCallback callback) {
    std::lock_guard<std::mutex> lock(m_listener_mutex);
    int id = m_next_listener_id++;
    m_listeners[id] = std::move(callback);
    return id;
}

void PluginHost::remove_event_listener(int listener_id) {
    std::lock_guard<std::mutex> lock(m_listener_mutex);
    m_listeners.erase(listener_id);
}

void PluginHost::fire_event(const std::string& plugin_id, PluginEvent event) {
    // Use a SEPARATE listener mutex (not m_mutex) so this can be called freely
    // from any context including while m_mutex is held by the calling thread.
    std::vector<PluginEventCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(m_listener_mutex);
        callbacks.reserve(m_listeners.size());
        for (const auto& [id, cb] : m_listeners) {
            callbacks.push_back(cb);
        }
    }
    
    for (const auto& cb : callbacks) {
        try {
            cb(plugin_id, event);
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "Plugin: Event listener exception: " << e.what();
        }
    }
}

//------------------------------------------------------------------------------
// Configuration
//------------------------------------------------------------------------------

std::string PluginHost::get_plugin_config(const std::string& plugin_id, 
                                           const std::string& key) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto plugin_it = m_configs.find(plugin_id);
    if (plugin_it == m_configs.end()) return "";
    
    auto key_it = plugin_it->second.find(key);
    if (key_it == plugin_it->second.end()) return "";
    
    return key_it->second;
}

void PluginHost::set_plugin_config(const std::string& plugin_id,
                                    const std::string& key,
                                    const std::string& value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_configs[plugin_id][key] = value;
}

void PluginHost::save_configs() const {
    std::filesystem::path config_path = get_user_plugins_dir() / "plugins_config.json";
    
    try {
        json j;
        for (const auto& [plugin_id, config] : m_configs) {
            for (const auto& [key, value] : config) {
                j[plugin_id][key] = value;
            }
        }
        
        std::ofstream file(config_path);
        file << j.dump(2);
        
        BOOST_LOG_TRIVIAL(debug) << "Plugin: Saved configs to " << config_path;
        
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Plugin: Failed to save configs: " << e.what();
    }
}

void PluginHost::load_configs() {
    std::filesystem::path config_path = get_user_plugins_dir() / "plugins_config.json";
    
    if (!std::filesystem::exists(config_path)) return;
    
    try {
        std::ifstream file(config_path);
        json j;
        file >> j;
        
        m_configs.clear();
        for (auto& [plugin_id, config] : j.items()) {
            for (auto& [key, value] : config.items()) {
                m_configs[plugin_id][key] = value.get<std::string>();
            }
        }
        
        BOOST_LOG_TRIVIAL(debug) << "Plugin: Loaded configs from " << config_path;
        
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "Plugin: Failed to load configs: " << e.what();
    }
}

//------------------------------------------------------------------------------
// Directory Management
//------------------------------------------------------------------------------

std::filesystem::path PluginHost::get_system_plugins_dir() const {
    // TODO: Get actual resources path from Platform
    return std::filesystem::path(resources_dir()) / "plugins";
}

std::filesystem::path PluginHost::get_user_plugins_dir() const {
    // TODO: Get actual user data path from Platform
    return std::filesystem::path(data_dir()) / "plugins";
}

std::filesystem::path PluginHost::get_plugin_data_dir(const std::string& plugin_id) const {
    return get_user_plugins_dir() / "data" / plugin_id;
}

void PluginHost::add_plugin_search_path(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_search_paths.push_back(path);
}

//------------------------------------------------------------------------------
// Internal Helpers
//------------------------------------------------------------------------------

std::shared_ptr<PluginRuntime> PluginHost::get_runtime(PluginType type) {
    auto it = m_runtimes.find(type);
    if (it == m_runtimes.end()) return nullptr;
    return it->second;
}

bool PluginHost::check_dependencies(const std::string& plugin_id) const {
    auto it = m_plugins.find(plugin_id);
    if (it == m_plugins.end()) return false;
    
    for (const auto& dep : it->second.manifest.dependencies) {
        auto dep_it = m_plugins.find(dep);
        if (dep_it == m_plugins.end()) {
            BOOST_LOG_TRIVIAL(warning) << "Plugin: " << plugin_id 
                                       << " depends on missing plugin " << dep;
            return false;
        }
        if (dep_it->second.state != PluginState::Loaded) {
            // Try to load dependency first
            // Note: This is a simplified approach; real implementation should
            // handle circular dependencies
            BOOST_LOG_TRIVIAL(warning) << "Plugin: " << plugin_id 
                                       << " depends on unloaded plugin " << dep;
            return false;
        }
    }
    return true;
}

//------------------------------------------------------------------------------
// Missing implementations
//------------------------------------------------------------------------------

std::unique_ptr<PluginContext> PluginHost::create_context(const std::string& plugin_id) {
    // Create plugin context using the factory function from PluginContext.cpp
    return create_plugin_context(plugin_id, this, m_plater, m_worker);
}

bool PluginHost::uninstall_plugin(const std::string& plugin_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_plugins.find(plugin_id);
    if (it == m_plugins.end()) {
        BOOST_LOG_TRIVIAL(warning) << "Plugin: Cannot uninstall unknown plugin " << plugin_id;
        return false;
    }
    
    // Unload first if loaded
    if (it->second.state == PluginState::Loaded) {
        unload_plugin(plugin_id);
    }
    
    // Get install path
    std::filesystem::path install_path = it->second.install_path;
    
    // Try to remove from registry
    m_plugins.erase(it);
    
    // Try to remove files (only if in user plugins directory)
    auto user_dir = get_user_plugins_dir();
    if (install_path.string().find(user_dir.string()) == 0) {
        try {
            std::filesystem::remove_all(install_path);
            BOOST_LOG_TRIVIAL(info) << "Plugin: Uninstalled " << plugin_id;
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "Plugin: Failed to remove plugin files: " << e.what();
            // Still consider uninstall successful as we removed from registry
        }
    } else {
        BOOST_LOG_TRIVIAL(info) << "Plugin: Unregistered " << plugin_id << " (system plugin, files not removed)";
    }
    
    // Remove from configs
    m_configs.erase(plugin_id);
    save_configs();
    
    return true;
}

//------------------------------------------------------------------------------
// Plugin Menu System Implementation
//------------------------------------------------------------------------------

bool PluginHost::register_plugin_submenu(const std::string& plugin_id, const SubmenuInfo& submenu) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto& reg = m_menu_registrations[plugin_id];
    reg.plugin_id = plugin_id;
    
    // Check if submenu already exists
    for (auto& existing : reg.submenus) {
        if (existing.id == submenu.id) {
            existing = submenu;  // Update
            if (m_menu_changed_callback) m_menu_changed_callback();
            return true;
        }
    }
    
    // Add new submenu
    reg.submenus.push_back(submenu);
    fprintf(stderr, "[Plugin] %s registered submenu: %s\n", plugin_id.c_str(), submenu.label.c_str());
    BOOST_LOG_TRIVIAL(debug) << "Plugin " << plugin_id << " registered submenu: " << submenu.label;
    
    if (m_menu_changed_callback) m_menu_changed_callback();
    return true;
}

bool PluginHost::unregister_plugin_submenu(const std::string& plugin_id, const std::string& submenu_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_menu_registrations.find(plugin_id);
    if (it == m_menu_registrations.end()) return false;
    
    auto& reg = it->second;
    
    // Remove submenu
    auto sm_it = std::remove_if(reg.submenus.begin(), reg.submenus.end(),
        [&](const SubmenuInfo& s) { return s.id == submenu_id; });
    if (sm_it == reg.submenus.end()) return false;
    reg.submenus.erase(sm_it, reg.submenus.end());
    
    // Remove all items in this submenu
    reg.items.erase(
        std::remove_if(reg.items.begin(), reg.items.end(),
            [&](const MenuItemInfo& item) { return item.submenu_id == submenu_id; }),
        reg.items.end());
    
    if (m_menu_changed_callback) m_menu_changed_callback();
    return true;
}

bool PluginHost::register_plugin_menu_item(const std::string& plugin_id, const MenuItemInfo& item) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto& reg = m_menu_registrations[plugin_id];
    reg.plugin_id = plugin_id;
    
    // Check if item already exists
    for (auto& existing : reg.items) {
        if (existing.id == item.id) {
            existing = item;  // Update
            if (m_menu_changed_callback) m_menu_changed_callback();
            return true;
        }
    }
    
    // Add new item
    reg.items.push_back(item);
    fprintf(stderr, "[Plugin] %s registered menu item: %s (submenu: %s)\n",
            plugin_id.c_str(), item.label.c_str(), item.submenu_id.c_str());
    BOOST_LOG_TRIVIAL(debug) << "Plugin " << plugin_id << " registered menu item: " << item.label;
    
    if (m_menu_changed_callback) m_menu_changed_callback();
    return true;
}

bool PluginHost::unregister_plugin_menu_item(const std::string& plugin_id, const std::string& item_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_menu_registrations.find(plugin_id);
    if (it == m_menu_registrations.end()) return false;
    
    auto& items = it->second.items;
    auto item_it = std::remove_if(items.begin(), items.end(),
        [&](const MenuItemInfo& item) { return item.id == item_id; });
    if (item_it == items.end()) return false;
    
    items.erase(item_it, items.end());
    
    if (m_menu_changed_callback) m_menu_changed_callback();
    return true;
}

bool PluginHost::update_plugin_menu_item(const std::string& plugin_id, const std::string& item_id,
                                          const MenuItemInfo& updated_item) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_menu_registrations.find(plugin_id);
    if (it == m_menu_registrations.end()) return false;
    
    for (auto& item : it->second.items) {
        if (item.id == item_id) {
            item = updated_item;
            item.id = item_id;  // Preserve original ID
            if (m_menu_changed_callback) m_menu_changed_callback();
            return true;
        }
    }
    
    return false;
}

std::vector<PluginMenuRegistration> PluginHost::get_all_menu_registrations() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<PluginMenuRegistration> result;
    result.reserve(m_menu_registrations.size());
    
    for (const auto& [plugin_id, reg] : m_menu_registrations) {
        // Only include registrations from loaded plugins
        auto plugin_it = m_plugins.find(plugin_id);
        if (plugin_it != m_plugins.end() && plugin_it->second.state == PluginState::Loaded) {
            result.push_back(reg);
        }
    }
    
    return result;
}

std::vector<MenuItemInfo> PluginHost::get_plugin_menu_items(const std::string& plugin_id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_menu_registrations.find(plugin_id);
    if (it == m_menu_registrations.end()) return {};
    
    return it->second.items;
}

void PluginHost::handle_menu_click(const std::string& plugin_id, const std::string& item_id) {
    // Find the plugin
    std::shared_ptr<IPlugin> plugin_instance;
    MenuItemInfo clicked_item;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        auto plugin_it = m_plugins.find(plugin_id);
        if (plugin_it == m_plugins.end() || plugin_it->second.state != PluginState::Loaded) {
            BOOST_LOG_TRIVIAL(warning) << "Plugin menu click: plugin not loaded: " << plugin_id;
            return;
        }
        plugin_instance = plugin_it->second.instance;
        
        auto reg_it = m_menu_registrations.find(plugin_id);
        if (reg_it != m_menu_registrations.end()) {
            for (const auto& item : reg_it->second.items) {
                if (item.id == item_id) {
                    clicked_item = item;
                    break;
                }
            }
        }
    }
    
    if (!plugin_instance) return;
    
    // Create context and dispatch event
    auto ctx = create_context(plugin_id);
    if (!ctx) {
        BOOST_LOG_TRIVIAL(error) << "Plugin menu click: failed to create context for " << plugin_id;
        return;
    }
    
    MenuEvent event;
    event.item_id = item_id;
    event.callback_data = clicked_item.callback_data;
    event.is_checked = has_flag(clicked_item.flags, MenuItemFlags::Checked);
    
    BOOST_LOG_TRIVIAL(debug) << "Plugin menu click: " << plugin_id << " / " << item_id;
    plugin_instance->on_menu_click(ctx.get(), event);
}

void PluginHost::set_menu_changed_callback(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_menu_changed_callback = std::move(callback);
}

} // namespace Plugin
} // namespace Slic3r

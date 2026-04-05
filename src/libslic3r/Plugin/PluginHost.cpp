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
    auto node_runtime = std::make_shared<NodePluginRuntime>();
    if (node_runtime->is_available()) {
        if (node_runtime->initialize()) {
            m_runtimes[PluginType::JavaScript] = node_runtime;
            BOOST_LOG_TRIVIAL(info) << "Plugin: Node.js runtime initialized, version: " 
                                    << node_runtime->version();
        } else {
            BOOST_LOG_TRIVIAL(warning) << "Plugin: Failed to initialize Node.js runtime";
        }
    } else {
        BOOST_LOG_TRIVIAL(info) << "Plugin: Node.js not available, JS plugins disabled";
    }
    
    // Future: Initialize Python runtime
    // auto python_runtime = std::make_shared<PythonPluginRuntime>();
    // ...
    
    // Load configurations
    load_configs();
    
    // Scan for plugins
    scan_plugin_directories();
    
    m_initialized = true;
    
    BOOST_LOG_TRIVIAL(info) << "Plugin: Host initialized with " << m_plugins.size() << " plugins discovered";
}

void PluginHost::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_initialized) return;
    
    BOOST_LOG_TRIVIAL(info) << "Plugin: Shutting down host";
    
    // Save configurations
    save_configs();
    
    // Unload all plugins
    for (auto& [id, info] : m_plugins) {
        if (info.state == PluginState::Loaded && info.instance) {
            try {
                info.instance->on_unload();
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "Plugin: Error unloading " << id << ": " << e.what();
            }
            info.instance.reset();
            info.state = PluginState::Unloaded;
        }
    }
    
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
    std::lock_guard<std::mutex> lock(m_mutex);
    
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
        fire_event(plugin_id, PluginEvent::Error);
        return false;
    }
    
    // Load plugin
    info.state = PluginState::Loading;
    BOOST_LOG_TRIVIAL(info) << "Plugin: Loading " << plugin_id;
    
    try {
        info.instance = runtime->load_plugin(info.manifest, info.install_path);
        if (!info.instance) {
            info.state = PluginState::Error;
            info.error_message = "Failed to create plugin instance";
            fire_event(plugin_id, PluginEvent::Error);
            return false;
        }
        
        // Create plugin context and call on_load
        auto ctx = create_context(plugin_id);
        if (!info.instance->on_load(ctx.get())) {
            info.state = PluginState::Error;
            info.error_message = "Plugin on_load() returned false";
            info.instance.reset();
            fire_event(plugin_id, PluginEvent::Error);
            return false;
        }
        
        info.state = PluginState::Loaded;
        info.error_message.clear();
        
        BOOST_LOG_TRIVIAL(info) << "Plugin: Loaded " << plugin_id << " successfully";
        fire_event(plugin_id, PluginEvent::Loaded);
        return true;
        
    } catch (const std::exception& e) {
        info.state = PluginState::Error;
        info.error_message = e.what();
        info.instance.reset();
        BOOST_LOG_TRIVIAL(error) << "Plugin: Exception loading " << plugin_id << ": " << e.what();
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
    std::lock_guard<std::mutex> lock(m_mutex);
    int id = m_next_listener_id++;
    m_listeners[id] = std::move(callback);
    return id;
}

void PluginHost::remove_event_listener(int listener_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_listeners.erase(listener_id);
}

void PluginHost::fire_event(const std::string& plugin_id, PluginEvent event) {
    // Make a copy of listeners to avoid holding lock during callbacks
    std::vector<PluginEventCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
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

} // namespace Plugin
} // namespace Slic3r

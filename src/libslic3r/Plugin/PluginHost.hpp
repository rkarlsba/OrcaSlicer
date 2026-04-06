// OrcaSlicer Plugin Host
// Manages plugin lifecycle and provides centralized plugin management
// Copyright (C) 2024 OrcaSlicer Contributors
// License: AGPL-3.0

#ifndef slic3r_PluginHost_hpp_
#define slic3r_PluginHost_hpp_

#include "PluginAPI.hpp"

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <mutex>
#include <filesystem>

namespace Slic3r {

// Forward declarations
class Model;
class ModelObject;
class ModelVolume;
class TriangleMesh;
class DynamicPrintConfig;

namespace GUI {
class Plater;
class Worker;
}

namespace Plugin {

// Forward declarations
class PluginRuntime;
class NativePluginRuntime;
class NodePluginRuntime;
class PythonPluginRuntime;

//==============================================================================
// Plugin State
//==============================================================================

enum class PluginState {
    Unloaded,
    Loading,
    Loaded,
    Error,
    Disabled
};

struct PluginInfo {
    PluginManifest manifest;
    PluginState state = PluginState::Unloaded;
    std::string error_message;
    std::filesystem::path install_path;
    bool user_enabled = true;
    
    // Runtime instance (null when unloaded)
    std::shared_ptr<IPlugin> instance;
};

//==============================================================================
// Plugin Event Types
//==============================================================================

enum class PluginEvent {
    Loaded,
    Unloaded,
    Error,
    ConfigChanged
};

using PluginEventCallback = std::function<void(const std::string& plugin_id, PluginEvent event)>;

//==============================================================================
// Plugin Host - Central plugin management
//==============================================================================

class PluginHost {
public:
    PluginHost();
    ~PluginHost();
    
    // Non-copyable, non-movable
    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;
    
    // Singleton access (created on first use)
    static PluginHost* instance();
    
    //--------------------------------------------------------------------------
    // Initialization
    //--------------------------------------------------------------------------
    
    // Initialize plugin system with OrcaSlicer components
    void initialize(GUI::Plater* plater, GUI::Worker* worker);
    
    // Shutdown plugin system
    void shutdown();
    
    // Check if initialized
    bool is_initialized() const { return m_initialized; }
    
    //--------------------------------------------------------------------------
    // Plugin Discovery & Installation
    //--------------------------------------------------------------------------
    
    // Scan directories for plugins
    void scan_plugin_directories();
    
    // Get list of discovered plugins
    std::vector<PluginInfo> get_plugins() const;
    
    // Get specific plugin info
    std::optional<PluginInfo> get_plugin(const std::string& plugin_id) const;
    
    // Install plugin from path (zip file or directory)
    bool install_plugin(const std::filesystem::path& source);
    
    // Uninstall plugin
    bool uninstall_plugin(const std::string& plugin_id);
    
    // Update plugin from path
    bool update_plugin(const std::string& plugin_id, const std::filesystem::path& source);
    
    //--------------------------------------------------------------------------
    // Plugin Lifecycle
    //--------------------------------------------------------------------------
    
    // Load a specific plugin
    bool load_plugin(const std::string& plugin_id);
    
    // Unload a specific plugin  
    bool unload_plugin(const std::string& plugin_id);
    
    // Reload a plugin
    bool reload_plugin(const std::string& plugin_id);
    
    // Enable/disable plugin
    void set_plugin_enabled(const std::string& plugin_id, bool enabled);
    
    // Load all enabled plugins
    void load_all_enabled();
    
    // Unload all plugins
    void unload_all();
    
    //--------------------------------------------------------------------------
    // Plugin Operations
    //--------------------------------------------------------------------------
    
    // Get plugins with specific capability
    std::vector<std::string> get_plugins_with_capability(PluginCapability cap) const;
    
    // Execute mesh modification on selected objects
    bool execute_mesh_plugin(const std::string& plugin_id, 
                             const std::string& operation,
                             const std::map<std::string, std::string>& params = {});
    
    // Execute G-code post-processing
    std::string execute_gcode_plugins(const std::string& gcode);
    
    //--------------------------------------------------------------------------
    // Event System
    //--------------------------------------------------------------------------
    
    // Subscribe to plugin events
    int add_event_listener(PluginEventCallback callback);
    
    // Unsubscribe from events
    void remove_event_listener(int listener_id);
    
    //--------------------------------------------------------------------------
    // Configuration
    //--------------------------------------------------------------------------
    
    // Get/set plugin configuration
    std::string get_plugin_config(const std::string& plugin_id, const std::string& key) const;
    void set_plugin_config(const std::string& plugin_id, const std::string& key, const std::string& value);
    
    // Save/load plugin configurations
    void save_configs() const;
    void load_configs();
    
    //--------------------------------------------------------------------------
    // Directory Management
    //--------------------------------------------------------------------------
    
    // Get standard plugin directories
    std::filesystem::path get_system_plugins_dir() const;
    std::filesystem::path get_user_plugins_dir() const;
    std::filesystem::path get_plugin_data_dir(const std::string& plugin_id) const;
    
    // Add custom plugin search path
    void add_plugin_search_path(const std::filesystem::path& path);
    
    //--------------------------------------------------------------------------
    // Plugin Menu System
    //--------------------------------------------------------------------------
    
    // Register a submenu from a plugin
    bool register_plugin_submenu(const std::string& plugin_id, const SubmenuInfo& submenu);
    
    // Unregister a submenu (and all its items)
    bool unregister_plugin_submenu(const std::string& plugin_id, const std::string& submenu_id);
    
    // Register a menu item from a plugin
    bool register_plugin_menu_item(const std::string& plugin_id, const MenuItemInfo& item);
    
    // Unregister a menu item
    bool unregister_plugin_menu_item(const std::string& plugin_id, const std::string& item_id);
    
    // Update a menu item's properties
    bool update_plugin_menu_item(const std::string& plugin_id, const std::string& item_id, 
                                  const MenuItemInfo& item);
    
    // Get all menu registrations (for building the Plugins menu)
    std::vector<PluginMenuRegistration> get_all_menu_registrations() const;
    
    // Get menu items for a specific plugin
    std::vector<MenuItemInfo> get_plugin_menu_items(const std::string& plugin_id) const;
    
    // Handle menu click - dispatches to the appropriate plugin
    void handle_menu_click(const std::string& plugin_id, const std::string& item_id);
    
    // Set callback for when menu registrations change (GUI rebuilds menu)
    void set_menu_changed_callback(std::function<void()> callback);
    
private:
    //--------------------------------------------------------------------------
    // Internal Methods
    //--------------------------------------------------------------------------
    
    // Parse plugin manifest from directory
    std::optional<PluginManifest> parse_manifest(const std::filesystem::path& plugin_dir);
    
    // Get appropriate runtime for plugin type
    std::shared_ptr<PluginRuntime> get_runtime(PluginType type);
    
    // Create plugin context for execution
    std::unique_ptr<PluginContext> create_context(const std::string& plugin_id);
    
    // Fire event to listeners
    void fire_event(const std::string& plugin_id, PluginEvent event);
    
    // Check plugin dependencies
    bool check_dependencies(const std::string& plugin_id) const;
    
    //--------------------------------------------------------------------------
    // Member Variables
    //--------------------------------------------------------------------------
    
    bool m_initialized = false;
    
    // OrcaSlicer integration
    GUI::Plater* m_plater = nullptr;
    GUI::Worker* m_worker = nullptr;
    
    // Plugin registry
    mutable std::mutex m_mutex;
    mutable std::mutex m_listener_mutex;  // separate mutex for event listeners, to avoid deadlock
    std::map<std::string, PluginInfo> m_plugins;
    
    // Plugin configurations (plugin_id -> key -> value)
    std::map<std::string, std::map<std::string, std::string>> m_configs;
    
    // Event listeners
    std::map<int, PluginEventCallback> m_listeners;
    int m_next_listener_id = 1;
    
    // Plugin runtimes (one per type)
    std::map<PluginType, std::shared_ptr<PluginRuntime>> m_runtimes;
    
    // Search paths
    std::vector<std::filesystem::path> m_search_paths;
    
    // Plugin menu registrations (plugin_id -> registration)
    std::map<std::string, PluginMenuRegistration> m_menu_registrations;
    
    // Callback when menus change (GUI updates menu)
    std::function<void()> m_menu_changed_callback;
};

//==============================================================================
// Plugin Runtime - Abstract interface for language-specific runtimes
//==============================================================================

class PluginRuntime {
public:
    virtual ~PluginRuntime() = default;
    
    // Initialize the runtime (e.g., start Node.js process)
    virtual bool initialize() = 0;
    
    // Shutdown the runtime
    virtual void shutdown() = 0;
    
    // Check if runtime is available
    virtual bool is_available() const = 0;
    
    // Load a plugin
    virtual std::shared_ptr<IPlugin> load_plugin(const PluginManifest& manifest,
                                                  const std::filesystem::path& path) = 0;
    
    // Unload a plugin
    virtual void unload_plugin(const std::string& plugin_id) = 0;
    
    // Get runtime type
    virtual PluginType type() const = 0;
    
    // Get runtime name (for logging)
    virtual std::string name() const = 0;
    
    // Get version info
    virtual std::string version() const = 0;
};

//==============================================================================
// Global Plugin Host Access
//==============================================================================

// Get the global plugin host instance
PluginHost& plugin_host();

} // namespace Plugin
} // namespace Slic3r

#endif // slic3r_PluginHost_hpp_

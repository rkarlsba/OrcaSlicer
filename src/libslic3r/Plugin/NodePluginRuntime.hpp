// OrcaSlicer Node.js Plugin Runtime
// Provides JavaScript/TypeScript plugin support via Node.js child process
// Copyright (C) 2024 OrcaSlicer Contributors
// License: AGPL-3.0

#ifndef slic3r_NodePluginRuntime_hpp_
#define slic3r_NodePluginRuntime_hpp_

#include "PluginHost.hpp"
#include "PluginIPC.hpp"

#include <thread>
#include <atomic>
#include <condition_variable>

namespace Slic3r {
namespace Plugin {

//==============================================================================
// Node Plugin Instance - Wraps a JavaScript plugin loaded in Node.js
//==============================================================================

class NodePluginInstance : public IPlugin {
public:
    NodePluginInstance(const PluginManifest& manifest, 
                       std::shared_ptr<IPCClient> ipc);
    ~NodePluginInstance();
    
    // IPlugin interface
    const PluginManifest& manifest() const override { return m_manifest; }
    bool on_load(PluginContext* ctx) override;
    void on_unload() override;
    void on_settings_ui() override;
    
    bool process_mesh(PluginContext* ctx, 
                      int object_idx, 
                      int volume_idx,
                      const std::string& operation) override;
    
    std::string process_gcode(PluginContext* ctx,
                              const std::string& gcode) override;
    
    std::vector<std::string> import_extensions() const override;
    std::vector<std::string> export_extensions() const override;
    
    std::optional<MeshData> import_mesh(PluginContext* ctx,
                                        const std::string& path) override;
    
    bool export_mesh(PluginContext* ctx,
                     const MeshView& mesh,
                     const std::string& path) override;
    
private:
    PluginManifest m_manifest;
    std::shared_ptr<IPCClient> m_ipc;
    PluginContext* m_current_ctx = nullptr;
    
    // Handle requests from plugin
    void handle_plugin_request(const IPCMessage& msg);
};

//==============================================================================
// Node Plugin Runtime - Manages Node.js process for JavaScript plugins
//==============================================================================

class NodePluginRuntime : public PluginRuntime {
public:
    NodePluginRuntime();
    ~NodePluginRuntime();
    
    // PluginRuntime interface
    bool initialize() override;
    void shutdown() override;
    bool is_available() const override;
    
    std::shared_ptr<IPlugin> load_plugin(const PluginManifest& manifest,
                                          const std::filesystem::path& path) override;
    void unload_plugin(const std::string& plugin_id) override;
    
    PluginType type() const override { return PluginType::JavaScript; }
    std::string name() const override { return "Node.js"; }
    std::string version() const override { return m_node_version; }
    
    // Get Node.js executable path
    static std::filesystem::path find_node_executable();
    
    // Check if Node.js is installed
    static bool is_node_installed();
    
private:
    //--------------------------------------------------------------------------
    // Node.js Process Management
    //--------------------------------------------------------------------------
    
    bool start_node_process();
    void stop_node_process();
    bool restart_node_process();
    
    // Process I/O handling
    void process_stdout_thread();
    void process_stderr_thread();
    
    // IPC event handling
    void handle_ipc_message(const IPCMessage& msg);
    
    // Handle host API requests from plugins
    JsonValue handle_host_request(const std::string& plugin_id,
                                  const std::string& method,
                                  const JsonValue& params);
    
    //--------------------------------------------------------------------------
    // Member Variables
    //--------------------------------------------------------------------------
    
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_process_running{false};
    std::string m_node_version;
    std::filesystem::path m_node_executable;
    
    // Node.js process handle (platform-specific)
#ifdef _WIN32
    void* m_process_handle = nullptr;
    void* m_stdin_write = nullptr;
    void* m_stdout_read = nullptr;
    void* m_stderr_read = nullptr;
#else
    pid_t m_process_pid = -1;
    int m_stdin_fd = -1;
    int m_stdout_fd = -1;
    int m_stderr_fd = -1;
#endif
    
    // I/O threads
    std::thread m_stdout_thread;
    std::thread m_stderr_thread;
    
    // IPC
    std::shared_ptr<IPCServer> m_ipc_server;
    std::map<std::string, std::shared_ptr<IPCClient>> m_plugin_clients;
    
    // Loaded plugins
    std::map<std::string, std::shared_ptr<NodePluginInstance>> m_plugins;
    
    // Host script path (embedded in resources)
    std::filesystem::path m_host_script_path;
};

} // namespace Plugin
} // namespace Slic3r

#endif // slic3r_NodePluginRuntime_hpp_

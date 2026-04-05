// OrcaSlicer Native Plugin Interface
// For writing plugins directly in C++
// Copyright (C) 2024 OrcaSlicer Contributors
// License: AGPL-3.0

#ifndef slic3r_NativePluginRuntime_hpp_
#define slic3r_NativePluginRuntime_hpp_

#include "PluginHost.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Slic3r {
namespace Plugin {

//==============================================================================
// Native Plugin Runtime - Loads C++ shared libraries
//==============================================================================

class NativePluginRuntime : public PluginRuntime {
public:
    NativePluginRuntime() = default;
    ~NativePluginRuntime();
    
    // PluginRuntime interface
    bool initialize() override;
    void shutdown() override;
    bool is_available() const override { return true; }  // Always available
    
    std::shared_ptr<IPlugin> load_plugin(const PluginManifest& manifest,
                                          const std::filesystem::path& path) override;
    void unload_plugin(const std::string& plugin_id) override;
    
    PluginType type() const override { return PluginType::Native; }
    std::string name() const override { return "Native C++"; }
    std::string version() const override { return "1.0.0"; }
    
private:
    struct LoadedLibrary {
        std::string plugin_id;
        std::shared_ptr<IPlugin> instance;
        
#ifdef _WIN32
        HMODULE handle = nullptr;
#else
        void* handle = nullptr;
#endif
        PluginDestroyFn destroy_fn = nullptr;
    };
    
    std::map<std::string, LoadedLibrary> m_libraries;
};

//==============================================================================
// Plugin Development Helpers
//==============================================================================

// Helper base class for native plugins
class NativePluginBase : public IPlugin {
public:
    NativePluginBase(PluginManifest manifest) : m_manifest(std::move(manifest)) {}
    
    const PluginManifest& manifest() const override { return m_manifest; }
    
    bool on_load(PluginContext* ctx) override {
        m_context = ctx;
        return true;
    }
    
    void on_unload() override {
        m_context = nullptr;
    }
    
protected:
    PluginContext* context() const { return m_context; }
    
private:
    PluginManifest m_manifest;
    PluginContext* m_context = nullptr;
};

//==============================================================================
// Macros for Plugin Development
//==============================================================================

// Use this macro to define the plugin entry points
#define ORCA_DECLARE_PLUGIN(PluginClass, manifest_fn) \
    extern "C" { \
        ORCA_PLUGIN_ENTRY Slic3r::Plugin::IPlugin* orca_plugin_create() { \
            auto manifest = manifest_fn(); \
            return new PluginClass(std::move(manifest)); \
        } \
        ORCA_PLUGIN_ENTRY void orca_plugin_destroy(Slic3r::Plugin::IPlugin* plugin) { \
            delete plugin; \
        } \
    }

// Helper to create a manifest
inline PluginManifest make_manifest(
    const std::string& id,
    const std::string& name,
    const std::string& version,
    PluginCapability capabilities,
    const std::string& description = "",
    const std::string& author = ""
) {
    PluginManifest m;
    m.id = id;
    m.name = name;
    m.version = version;
    m.capabilities = capabilities;
    m.description = description;
    m.author = author;
    m.type = PluginType::Native;
    return m;
}

} // namespace Plugin
} // namespace Slic3r

#endif // slic3r_NativePluginRuntime_hpp_

// OrcaSlicer Plugin API
// Copyright (C) 2024 OrcaSlicer Contributors
// License: AGPL-3.0

#ifndef slic3r_PluginAPI_hpp_
#define slic3r_PluginAPI_hpp_

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <optional>
#include <variant>

#include "libslic3r/Point.hpp"
#include "libslic3r/BoundingBox.hpp"

namespace Slic3r {
namespace Plugin {

// Forward declarations
class PluginHost;
class PluginRuntime;
class PluginInstance;
struct MeshData;
struct PluginManifest;

//==============================================================================
// Plugin API Version
//==============================================================================

constexpr int PLUGIN_API_VERSION_MAJOR = 1;
constexpr int PLUGIN_API_VERSION_MINOR = 0;
constexpr int PLUGIN_API_VERSION_PATCH = 0;

//==============================================================================
// Plugin Types
//==============================================================================

enum class PluginType {
    Native,      // C++ shared library (.dll/.so/.dylib)
    JavaScript,  // Node.js-based plugins
    Python,      // Python-based plugins
    WebAssembly  // Future: WASM plugins
};

enum class PluginCapability : uint32_t {
    None                = 0,
    MeshModification    = 1 << 0,  // Can modify mesh geometry
    MeshImport          = 1 << 1,  // Can import mesh from custom formats
    MeshExport          = 1 << 2,  // Can export mesh to custom formats
    GCodePostProcess    = 1 << 3,  // Can post-process G-code
    UIExtension         = 1 << 4,  // Can add UI elements (menus, gizmos)
    SliceModification   = 1 << 5,  // Can modify slice data
    TextureMaterial     = 1 << 6,  // Can apply textures/materials
    Analysis            = 1 << 7,  // Can analyze models/slices
};

inline PluginCapability operator|(PluginCapability a, PluginCapability b) {
    return static_cast<PluginCapability>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline PluginCapability operator&(PluginCapability a, PluginCapability b) {
    return static_cast<PluginCapability>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline bool has_capability(PluginCapability set, PluginCapability cap) {
    return static_cast<uint32_t>(set & cap) != 0;
}

//==============================================================================
// Plugin Manifest - Describes a plugin
//==============================================================================

struct PluginManifest {
    std::string id;           // Unique identifier (e.g., "com.cnckitchen.bumpmesh")
    std::string name;         // Human-readable name
    std::string version;      // Semver version string
    std::string description;  // Plugin description
    std::string author;       // Author name/email
    std::string license;      // License identifier (e.g., "MIT", "GPL-3.0")
    std::string homepage;     // URL to plugin homepage
    
    PluginType type;
    PluginCapability capabilities;
    
    int api_version_major = PLUGIN_API_VERSION_MAJOR;
    int api_version_minor = PLUGIN_API_VERSION_MINOR;
    
    // Plugin entry point (depends on type)
    // Native: path to .dll/.so/.dylib
    // JavaScript: path to main.js or package.json
    // Python: path to main.py or __init__.py
    std::string entry_point;
    
    // Dependencies on other plugins
    std::vector<std::string> dependencies;
    
    // Configuration schema (JSON Schema format)
    std::string config_schema;
    
    // Icon path
    std::string icon_path;

    bool is_valid() const { return !id.empty() && !name.empty() && !entry_point.empty(); }
};

//==============================================================================
// Mesh Data Structures - For safe mesh manipulation by plugins
//==============================================================================

// Read-only mesh view for analysis
struct MeshView {
    const float* vertices;     // [x0, y0, z0, x1, y1, z1, ...]
    const float* normals;      // Per-vertex normals
    const int32_t* indices;    // Triangle indices [i0, i1, i2, ...]
    
    size_t vertex_count;       // Number of vertices
    size_t triangle_count;     // Number of triangles
    
    BoundingBoxf3 bounding_box;
    Vec3d center;
    Vec3d size;
    
    // Volume info
    bool is_manifold;
    float volume;
};

// Mutable mesh data for modification
struct MeshData {
    std::vector<float> vertices;    // [x0, y0, z0, x1, y1, z1, ...]
    std::vector<float> normals;     // Per-vertex normals (optional, will be recomputed)
    std::vector<int32_t> indices;   // Triangle indices
    
    size_t vertex_count() const { return vertices.size() / 3; }
    size_t triangle_count() const { return indices.size() / 3; }
    
    void reserve_vertices(size_t count) { 
        vertices.reserve(count * 3); 
        normals.reserve(count * 3);
    }
    void reserve_triangles(size_t count) { 
        indices.reserve(count * 3); 
    }
    
    void add_vertex(float x, float y, float z) {
        vertices.push_back(x);
        vertices.push_back(y);
        vertices.push_back(z);
    }
    
    void add_triangle(int32_t i0, int32_t i1, int32_t i2) {
        indices.push_back(i0);
        indices.push_back(i1);
        indices.push_back(i2);
    }
    
    void clear() {
        vertices.clear();
        normals.clear();
        indices.clear();
    }
};

//==============================================================================
// Texture/Image data for displacement mapping
//==============================================================================

struct ImageData {
    std::vector<uint8_t> pixels;  // RGBA format
    int width;
    int height;
    int channels;  // 1=grayscale, 3=RGB, 4=RGBA
    
    uint8_t sample(float u, float v) const;  // Bilinear sample
    float sample_float(float u, float v) const;  // Returns [0, 1]
};

//==============================================================================
// Plugin Progress Callback
//==============================================================================

class ProgressCallback {
public:
    virtual ~ProgressCallback() = default;
    
    // Update progress (0-100)
    virtual void update(int progress, const std::string& message = "") = 0;
    
    // Check if operation was cancelled
    virtual bool is_cancelled() const = 0;
    
    // Log message
    virtual void log(const std::string& message) = 0;
    
    // Report error
    virtual void error(const std::string& message) = 0;
};

//==============================================================================
// Plugin Context - Provided to plugins during execution
//==============================================================================

class PluginContext {
public:
    virtual ~PluginContext() = default;
    
    //--------------------------------------------------------------------------
    // Mesh Operations
    //--------------------------------------------------------------------------
    
    // Get current selection as mesh views
    virtual std::vector<MeshView> get_selected_meshes() const = 0;
    
    // Get a specific volume's mesh by object and volume indices
    virtual std::optional<MeshView> get_mesh(int object_idx, int volume_idx) const = 0;
    
    // Replace a volume's mesh with new data
    virtual bool set_mesh(int object_idx, int volume_idx, const MeshData& data) = 0;
    
    // Add a new object with the given mesh
    virtual int add_object(const MeshData& data, const std::string& name) = 0;
    
    // Get mesh bounds
    virtual BoundingBoxf3 get_mesh_bounds(int object_idx, int volume_idx) const = 0;
    
    //--------------------------------------------------------------------------
    // Image/Texture Operations
    //--------------------------------------------------------------------------
    
    // Load an image from file
    virtual std::optional<ImageData> load_image(const std::string& path) = 0;
    
    // Save an image to file  
    virtual bool save_image(const ImageData& image, const std::string& path) = 0;
    
    //--------------------------------------------------------------------------
    // Configuration
    //--------------------------------------------------------------------------
    
    // Get plugin configuration value
    virtual std::string get_config(const std::string& key) const = 0;
    
    // Set plugin configuration value
    virtual void set_config(const std::string& key, const std::string& value) = 0;
    
    // Get printer/print/filament setting
    virtual std::string get_setting(const std::string& key) const = 0;
    
    //--------------------------------------------------------------------------
    // Progress & Cancellation
    //--------------------------------------------------------------------------
    
    // Get progress callback
    virtual ProgressCallback* progress() = 0;
    
    //--------------------------------------------------------------------------
    // UI Operations (only valid during UI callbacks)
    //--------------------------------------------------------------------------
    
    // Show a message dialog
    virtual void show_message(const std::string& title, const std::string& message) = 0;
    
    // Show yes/no dialog, returns user choice
    virtual bool show_confirm(const std::string& title, const std::string& message) = 0;
    
    // Open file dialog, returns selected path or empty
    virtual std::string open_file_dialog(const std::string& title, 
                                         const std::string& filter) = 0;
    
    // Save file dialog, returns selected path or empty
    virtual std::string save_file_dialog(const std::string& title,
                                         const std::string& filter,
                                         const std::string& default_name) = 0;
    
    //--------------------------------------------------------------------------
    // Utility
    //--------------------------------------------------------------------------
    
    // Get plugin's data directory (for storing plugin-specific files)
    virtual std::string get_plugin_data_dir() const = 0;
    
    // Get OrcaSlicer's resources directory
    virtual std::string get_resources_dir() const = 0;
    
    // Schedule a function to run on the main UI thread
    virtual void run_on_ui_thread(std::function<void()> fn) = 0;
};

//==============================================================================
// Plugin Interface - Base class for all plugin implementations
//==============================================================================

class IPlugin {
public:
    virtual ~IPlugin() = default;
    
    // Get plugin manifest
    virtual const PluginManifest& manifest() const = 0;
    
    // Called when plugin is loaded
    virtual bool on_load(PluginContext* ctx) = 0;
    
    // Called when plugin is about to be unloaded
    virtual void on_unload() = 0;
    
    // Called to display plugin settings UI (optional)
    virtual void on_settings_ui() {}
    
    //--------------------------------------------------------------------------
    // Mesh Modification Hooks (if MeshModification capability)
    //--------------------------------------------------------------------------
    
    // Process a mesh - called when user invokes plugin on selected mesh
    virtual bool process_mesh(PluginContext* ctx, 
                              int object_idx, 
                              int volume_idx,
                              const std::string& operation) { return false; }
    
    //--------------------------------------------------------------------------
    // G-code Hooks (if GCodePostProcess capability)
    //--------------------------------------------------------------------------
    
    // Post-process G-code
    virtual std::string process_gcode(PluginContext* ctx,
                                      const std::string& gcode) { return gcode; }
    
    //--------------------------------------------------------------------------
    // Import/Export Hooks
    //--------------------------------------------------------------------------
    
    // Return list of supported import extensions (e.g., [".stlx", ".mesh"])
    virtual std::vector<std::string> import_extensions() const { return {}; }
    
    // Return list of supported export extensions
    virtual std::vector<std::string> export_extensions() const { return {}; }
    
    // Import from custom format
    virtual std::optional<MeshData> import_mesh(PluginContext* ctx,
                                                const std::string& path) { return std::nullopt; }
    
    // Export to custom format
    virtual bool export_mesh(PluginContext* ctx,
                             const MeshView& mesh,
                             const std::string& path) { return false; }
};

//==============================================================================
// Plugin Registration Macros (for native plugins)
//==============================================================================

#define ORCA_PLUGIN_ENTRY extern "C"

// Native plugin entry point signature
using PluginCreateFn = IPlugin* (*)();
using PluginDestroyFn = void (*)(IPlugin*);

// Entry point names
#define ORCA_PLUGIN_CREATE_FN "orca_plugin_create"
#define ORCA_PLUGIN_DESTROY_FN "orca_plugin_destroy"

} // namespace Plugin
} // namespace Slic3r

#endif // slic3r_PluginAPI_hpp_

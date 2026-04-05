// OrcaSlicer Plugin Context Implementation
// Provides plugins with access to OrcaSlicer functionality
// Copyright (C) 2024 OrcaSlicer Contributors
// License: AGPL-3.0

#ifndef slic3r_PluginContext_hpp_
#define slic3r_PluginContext_hpp_

#include "PluginAPI.hpp"
#include "PluginHost.hpp"

namespace Slic3r {

class Model;
class ModelObject;
class ModelVolume;
class TriangleMesh;

namespace GUI {
class Plater;
class Worker;
}

namespace Plugin {

//==============================================================================
// Progress Callback Implementation
//==============================================================================

class PluginProgressCallback : public ProgressCallback {
public:
    PluginProgressCallback(const std::string& plugin_id, GUI::Worker* worker);
    ~PluginProgressCallback();
    
    void update(int progress, const std::string& message = "") override;
    bool is_cancelled() const override;
    void log(const std::string& message) override;
    void error(const std::string& message) override;
    
    void set_cancelled() { m_cancelled = true; }
    
private:
    std::string m_plugin_id;
    GUI::Worker* m_worker;
    std::atomic<bool> m_cancelled{false};
    int m_last_progress = -1;
};

//==============================================================================
// Plugin Context Implementation
//==============================================================================

class PluginContextImpl : public PluginContext {
public:
    PluginContextImpl(const std::string& plugin_id,
                      PluginHost* host,
                      GUI::Plater* plater,
                      GUI::Worker* worker);
    ~PluginContextImpl();
    
    //--------------------------------------------------------------------------
    // Mesh Operations
    //--------------------------------------------------------------------------
    
    std::vector<MeshView> get_selected_meshes() const override;
    std::optional<MeshView> get_mesh(int object_idx, int volume_idx) const override;
    bool set_mesh(int object_idx, int volume_idx, const MeshData& data) override;
    int add_object(const MeshData& data, const std::string& name) override;
    BoundingBoxf3 get_mesh_bounds(int object_idx, int volume_idx) const override;
    
    //--------------------------------------------------------------------------
    // Image Operations
    //--------------------------------------------------------------------------
    
    std::optional<ImageData> load_image(const std::string& path) override;
    bool save_image(const ImageData& image, const std::string& path) override;
    
    //--------------------------------------------------------------------------
    // Configuration
    //--------------------------------------------------------------------------
    
    std::string get_config(const std::string& key) const override;
    void set_config(const std::string& key, const std::string& value) override;
    std::string get_setting(const std::string& key) const override;
    
    //--------------------------------------------------------------------------
    // Progress & Cancellation
    //--------------------------------------------------------------------------
    
    ProgressCallback* progress() override { return &m_progress; }
    
    //--------------------------------------------------------------------------
    // UI Operations
    //--------------------------------------------------------------------------
    
    void show_message(const std::string& title, const std::string& message) override;
    bool show_confirm(const std::string& title, const std::string& message) override;
    std::string open_file_dialog(const std::string& title, const std::string& filter) override;
    std::string save_file_dialog(const std::string& title,
                                 const std::string& filter,
                                 const std::string& default_name) override;
    
    //--------------------------------------------------------------------------
    // Utility
    //--------------------------------------------------------------------------
    
    std::string get_plugin_data_dir() const override;
    std::string get_resources_dir() const override;
    void run_on_ui_thread(std::function<void()> fn) override;
    
private:
    // Helper to get Model from Plater
    Model* get_model() const;
    
    // Helper to create MeshView from TriangleMesh
    MeshView create_mesh_view(const TriangleMesh& mesh) const;
    
    // Helper to create TriangleMesh from MeshData
    TriangleMesh create_triangle_mesh(const MeshData& data) const;
    
    std::string m_plugin_id;
    PluginHost* m_host;
    GUI::Plater* m_plater;
    GUI::Worker* m_worker;
    PluginProgressCallback m_progress;
    
    // Temporary storage for mesh data views (to keep data alive)
    mutable std::vector<std::vector<float>> m_temp_vertices;
    mutable std::vector<std::vector<float>> m_temp_normals;
    mutable std::vector<std::vector<int32_t>> m_temp_indices;
};

//==============================================================================
// Factory function used by PluginHost
//==============================================================================

std::unique_ptr<PluginContext> create_plugin_context(const std::string& plugin_id,
                                                      PluginHost* host,
                                                      GUI::Plater* plater,
                                                      GUI::Worker* worker);

} // namespace Plugin
} // namespace Slic3r

#endif // slic3r_PluginContext_hpp_

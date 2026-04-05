// OrcaSlicer Plugin Context Implementation
// Copyright (C) 2024 OrcaSlicer Contributors
// License: AGPL-3.0

#include "PluginContext.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Utils.hpp"

#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Selection.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Jobs/Worker.hpp"
#include "slic3r/GUI/Jobs/Job.hpp"
#include "slic3r/GUI/MsgDialog.hpp"

#include <wx/msgdlg.h>
#include <wx/filedlg.h>
#include <wx/image.h>
#include <wx/mstream.h>

#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>

namespace Slic3r {
namespace Plugin {

// Use wxGetApp from the GUI namespace
using GUI::wxGetApp;

//==============================================================================
// PluginProgressCallback Implementation
//==============================================================================

PluginProgressCallback::PluginProgressCallback(const std::string& plugin_id, GUI::Worker* worker)
    : m_plugin_id(plugin_id)
    , m_worker(worker)
{
}

PluginProgressCallback::~PluginProgressCallback()
{
}

void PluginProgressCallback::update(int progress, const std::string& message)
{
    // Clamp progress to valid range
    progress = std::max(0, std::min(100, progress));
    
    // Only update if progress changed
    if (progress == m_last_progress && message.empty()) {
        return;
    }
    m_last_progress = progress;
    
    // Log progress for debugging
    // BOOST_LOG_TRIVIAL(info) << "[Plugin:" << m_plugin_id << "] " << progress << "% " << message;
    
    // If we have a worker, update through the job control system
    // This would require passing the Job::Ctl reference, which we don't have directly
    // In practice, progress updates go through the IPC system back to the host
}

bool PluginProgressCallback::is_cancelled() const
{
    return m_cancelled.load();
}

void PluginProgressCallback::log(const std::string& message)
{
    BOOST_LOG_TRIVIAL(info) << "[Plugin:" << m_plugin_id << "] " << message;
}

void PluginProgressCallback::error(const std::string& message)
{
    BOOST_LOG_TRIVIAL(error) << "[Plugin:" << m_plugin_id << "] ERROR: " << message;
}

//==============================================================================
// PluginContextImpl Implementation
//==============================================================================

PluginContextImpl::PluginContextImpl(const std::string& plugin_id,
                                     PluginHost* host,
                                     GUI::Plater* plater,
                                     GUI::Worker* worker)
    : m_plugin_id(plugin_id)
    , m_host(host)
    , m_plater(plater)
    , m_worker(worker)
    , m_progress(plugin_id, worker)
{
}

PluginContextImpl::~PluginContextImpl()
{
}

//------------------------------------------------------------------------------
// Helper Methods
//------------------------------------------------------------------------------

Model* PluginContextImpl::get_model() const
{
    if (!m_plater) return nullptr;
    return &m_plater->model();
}

MeshView PluginContextImpl::create_mesh_view(const TriangleMesh& mesh) const
{
    MeshView view;
    
    const auto& its = mesh.its;
    
    // Store data in temp vectors to keep alive for the view's lifetime
    auto& temp_verts = m_temp_vertices.emplace_back();
    auto& temp_indices = m_temp_indices.emplace_back();
    
    // Convert Vec3f vertices to flat float array
    temp_verts.reserve(its.vertices.size() * 3);
    for (const auto& v : its.vertices) {
        temp_verts.push_back(v.x());
        temp_verts.push_back(v.y());
        temp_verts.push_back(v.z());
    }
    
    // Convert Vec3i32 indices to flat int32 array
    temp_indices.reserve(its.indices.size() * 3);
    for (const auto& f : its.indices) {
        temp_indices.push_back(f[0]);
        temp_indices.push_back(f[1]);
        temp_indices.push_back(f[2]);
    }
    
    view.vertices = temp_verts.data();
    view.indices = temp_indices.data();
    view.normals = nullptr; // Could compute normals if needed
    view.vertex_count = static_cast<int>(its.vertices.size());
    view.triangle_count = static_cast<int>(its.indices.size());
    
    return view;
}

TriangleMesh PluginContextImpl::create_triangle_mesh(const MeshData& data) const
{
    // Convert flat arrays back to indexed_triangle_set
    std::vector<Vec3f> vertices;
    vertices.reserve(data.vertices.size() / 3);
    for (size_t i = 0; i + 2 < data.vertices.size(); i += 3) {
        vertices.emplace_back(
            data.vertices[i],
            data.vertices[i + 1],
            data.vertices[i + 2]
        );
    }
    
    std::vector<Vec3i32> faces;
    faces.reserve(data.indices.size() / 3);
    for (size_t i = 0; i + 2 < data.indices.size(); i += 3) {
        faces.emplace_back(
            data.indices[i],
            data.indices[i + 1],
            data.indices[i + 2]
        );
    }
    
    return TriangleMesh(std::move(vertices), std::move(faces));
}

//------------------------------------------------------------------------------
// Mesh Operations
//------------------------------------------------------------------------------

std::vector<MeshView> PluginContextImpl::get_selected_meshes() const
{
    std::vector<MeshView> result;
    
    if (!m_plater) return result;
    
    // Clear temp storage for new request
    m_temp_vertices.clear();
    m_temp_normals.clear();
    m_temp_indices.clear();
    
    Model* model = get_model();
    if (!model) return result;
    
    // Get selection from Plater
    // This requires accessing the GLCanvas3D's selection
    // For now, iterate all volumes in all objects
    // A proper implementation would use the Selection class
    
    for (size_t obj_idx = 0; obj_idx < model->objects.size(); ++obj_idx) {
        const ModelObject* obj = model->objects[obj_idx];
        for (size_t vol_idx = 0; vol_idx < obj->volumes.size(); ++vol_idx) {
            const ModelVolume* vol = obj->volumes[vol_idx];
            if (vol->is_model_part()) {
                result.push_back(create_mesh_view(vol->mesh()));
            }
        }
    }
    
    return result;
}

std::optional<MeshView> PluginContextImpl::get_mesh(int object_idx, int volume_idx) const
{
    Model* model = get_model();
    if (!model) return std::nullopt;
    
    if (object_idx < 0 || static_cast<size_t>(object_idx) >= model->objects.size()) {
        return std::nullopt;
    }
    
    const ModelObject* obj = model->objects[object_idx];
    if (volume_idx < 0 || static_cast<size_t>(volume_idx) >= obj->volumes.size()) {
        return std::nullopt;
    }
    
    const ModelVolume* vol = obj->volumes[volume_idx];
    
    // Clear temp storage for this specific request if needed, or add to it
    return create_mesh_view(vol->mesh());
}

bool PluginContextImpl::set_mesh(int object_idx, int volume_idx, const MeshData& data)
{
    Model* model = get_model();
    if (!model) return false;
    
    if (object_idx < 0 || static_cast<size_t>(object_idx) >= model->objects.size()) {
        return false;
    }
    
    ModelObject* obj = model->objects[object_idx];
    if (volume_idx < 0 || static_cast<size_t>(volume_idx) >= obj->volumes.size()) {
        return false;
    }
    
    ModelVolume* vol = obj->volumes[volume_idx];
    
    // Create new mesh from data
    TriangleMesh new_mesh = create_triangle_mesh(data);
    
    // Validate the mesh
    if (new_mesh.empty()) {
        m_progress.error("Cannot set empty mesh");
        return false;
    }
    
    // Update the volume's mesh
    // This needs to be done on the UI thread
    run_on_ui_thread([vol, mesh = std::move(new_mesh)]() mutable {
        vol->set_mesh(std::move(mesh));
        vol->calculate_convex_hull();
        
        // Update the plater to reflect changes
        wxGetApp().plater()->update();
    });
    
    return true;
}

int PluginContextImpl::add_object(const MeshData& data, const std::string& name)
{
    Model* model = get_model();
    if (!model || !m_plater) return -1;
    
    // Create mesh
    TriangleMesh mesh = create_triangle_mesh(data);
    if (mesh.empty()) {
        m_progress.error("Cannot add empty mesh");
        return -1;
    }
    
    int new_obj_idx = -1;
    
    run_on_ui_thread([&new_obj_idx, model, &mesh, &name]() {
        // Create new object
        ModelObject* obj = model->add_object();
        obj->name = name.empty() ? "Plugin Object" : name;
        obj->input_file.clear();
        
        // Add volume with mesh
        ModelVolume* vol = obj->add_volume(std::move(mesh));
        vol->name = obj->name;
        
        // Center object
        obj->center_around_origin();
        
        new_obj_idx = static_cast<int>(model->objects.size() - 1);
        
        // Update UI
        wxGetApp().plater()->update();
        wxGetApp().obj_list()->update_after_undo_redo();
    });
    
    return new_obj_idx;
}

BoundingBoxf3 PluginContextImpl::get_mesh_bounds(int object_idx, int volume_idx) const
{
    Model* model = get_model();
    if (!model) return BoundingBoxf3();
    
    if (object_idx < 0 || static_cast<size_t>(object_idx) >= model->objects.size()) {
        return BoundingBoxf3();
    }
    
    const ModelObject* obj = model->objects[object_idx];
    if (volume_idx < 0 || static_cast<size_t>(volume_idx) >= obj->volumes.size()) {
        return BoundingBoxf3();
    }
    
    return obj->volumes[volume_idx]->mesh().bounding_box();
}

//------------------------------------------------------------------------------
// Image Operations
//------------------------------------------------------------------------------

std::optional<ImageData> PluginContextImpl::load_image(const std::string& path)
{
    wxImage image;
    
    // Load image based on extension
    std::string lower_path = boost::algorithm::to_lower_copy(path);
    wxBitmapType type = wxBITMAP_TYPE_ANY;
    
    if (boost::algorithm::ends_with(lower_path, ".png")) {
        type = wxBITMAP_TYPE_PNG;
    } else if (boost::algorithm::ends_with(lower_path, ".jpg") || 
               boost::algorithm::ends_with(lower_path, ".jpeg")) {
        type = wxBITMAP_TYPE_JPEG;
    } else if (boost::algorithm::ends_with(lower_path, ".bmp")) {
        type = wxBITMAP_TYPE_BMP;
    }
    
    if (!image.LoadFile(wxString::FromUTF8(path.c_str()), type)) {
        m_progress.error("Failed to load image: " + path);
        return std::nullopt;
    }
    
    ImageData data;
    data.width = image.GetWidth();
    data.height = image.GetHeight();
    data.channels = image.HasAlpha() ? 4 : 3;
    
    // Copy pixel data
    size_t pixel_count = data.width * data.height;
    data.pixels.resize(pixel_count * data.channels);
    
    const unsigned char* rgb = image.GetData();
    const unsigned char* alpha = image.HasAlpha() ? image.GetAlpha() : nullptr;
    
    if (data.channels == 4) {
        for (size_t i = 0; i < pixel_count; ++i) {
            data.pixels[i * 4 + 0] = rgb[i * 3 + 0];
            data.pixels[i * 4 + 1] = rgb[i * 3 + 1];
            data.pixels[i * 4 + 2] = rgb[i * 3 + 2];
            data.pixels[i * 4 + 3] = alpha ? alpha[i] : 255;
        }
    } else {
        std::memcpy(data.pixels.data(), rgb, pixel_count * 3);
    }
    
    return data;
}

bool PluginContextImpl::save_image(const ImageData& image, const std::string& path)
{
    if (image.width <= 0 || image.height <= 0 || image.pixels.empty()) {
        m_progress.error("Invalid image data");
        return false;
    }
    
    // Create wxImage from pixel data
    wxImage wx_image(image.width, image.height, false);
    
    if (image.channels == 4) {
        // Has alpha
        wx_image.SetAlpha();
        unsigned char* rgb = wx_image.GetData();
        unsigned char* alpha = wx_image.GetAlpha();
        
        for (int i = 0; i < image.width * image.height; ++i) {
            rgb[i * 3 + 0] = image.pixels[i * 4 + 0];
            rgb[i * 3 + 1] = image.pixels[i * 4 + 1];
            rgb[i * 3 + 2] = image.pixels[i * 4 + 2];
            alpha[i] = image.pixels[i * 4 + 3];
        }
    } else if (image.channels == 3) {
        std::memcpy(wx_image.GetData(), image.pixels.data(), image.width * image.height * 3);
    } else if (image.channels == 1) {
        // Grayscale - expand to RGB
        unsigned char* rgb = wx_image.GetData();
        for (int i = 0; i < image.width * image.height; ++i) {
            rgb[i * 3 + 0] = image.pixels[i];
            rgb[i * 3 + 1] = image.pixels[i];
            rgb[i * 3 + 2] = image.pixels[i];
        }
    }
    
    // Determine format from extension
    std::string lower_path = boost::algorithm::to_lower_copy(path);
    wxBitmapType type = wxBITMAP_TYPE_PNG;
    
    if (boost::algorithm::ends_with(lower_path, ".jpg") || 
        boost::algorithm::ends_with(lower_path, ".jpeg")) {
        type = wxBITMAP_TYPE_JPEG;
    } else if (boost::algorithm::ends_with(lower_path, ".bmp")) {
        type = wxBITMAP_TYPE_BMP;
    }
    
    return wx_image.SaveFile(wxString::FromUTF8(path.c_str()), type);
}

//------------------------------------------------------------------------------
// Configuration
//------------------------------------------------------------------------------

std::string PluginContextImpl::get_config(const std::string& key) const
{
    if (!m_host) return "";
    
    // Plugin-specific config is stored with plugin prefix
    std::string full_key = "plugin_" + m_plugin_id + "_" + key;
    
    auto* app_config = GUI::wxGetApp().app_config;
    if (app_config && app_config->has("plugins", full_key)) {
        return app_config->get("plugins", full_key);
    }
    return "";
}

void PluginContextImpl::set_config(const std::string& key, const std::string& value)
{
    if (!m_host) return;
    
    std::string full_key = "plugin_" + m_plugin_id + "_" + key;
    
    auto* app_config = GUI::wxGetApp().app_config;
    if (app_config) {
        app_config->set("plugins", full_key, value);
        app_config->save();
    }
}

std::string PluginContextImpl::get_setting(const std::string& key) const
{
    if (!m_plater) return "";
    
    // Get current print/printer/filament config
    const DynamicPrintConfig& config = wxGetApp().preset_bundle->full_config();
    
    auto opt = config.option(key);
    if (opt) {
        return opt->serialize();
    }
    return "";
}

//------------------------------------------------------------------------------
// UI Operations
//------------------------------------------------------------------------------

void PluginContextImpl::show_message(const std::string& title, const std::string& message)
{
    run_on_ui_thread([title, message]() {
        wxMessageBox(wxString::FromUTF8(message.c_str()), 
                     wxString::FromUTF8(title.c_str()),
                     wxOK | wxICON_INFORMATION);
    });
}

bool PluginContextImpl::show_confirm(const std::string& title, const std::string& message)
{
    bool result = false;
    
    // Must run on UI thread and wait for result
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    
    run_on_ui_thread([&result, &done, &mtx, &cv, &title, &message]() {
        result = wxMessageBox(wxString::FromUTF8(message.c_str()),
                              wxString::FromUTF8(title.c_str()),
                              wxYES_NO | wxICON_QUESTION) == wxYES;
        
        std::lock_guard<std::mutex> lock(mtx);
        done = true;
        cv.notify_one();
    });
    
    // Wait for result
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [&done]() { return done; });
    
    return result;
}

std::string PluginContextImpl::open_file_dialog(const std::string& title, const std::string& filter)
{
    std::string result;
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    
    run_on_ui_thread([&result, &done, &mtx, &cv, &title, &filter]() {
        wxFileDialog dialog(nullptr,
                           wxString::FromUTF8(title.c_str()),
                           wxEmptyString,
                           wxEmptyString,
                           wxString::FromUTF8(filter.c_str()),
                           wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        
        if (dialog.ShowModal() == wxID_OK) {
            result = dialog.GetPath().ToUTF8().data();
        }
        
        std::lock_guard<std::mutex> lock(mtx);
        done = true;
        cv.notify_one();
    });
    
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [&done]() { return done; });
    
    return result;
}

std::string PluginContextImpl::save_file_dialog(const std::string& title,
                                                 const std::string& filter,
                                                 const std::string& default_name)
{
    std::string result;
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    
    run_on_ui_thread([&result, &done, &mtx, &cv, &title, &filter, &default_name]() {
        wxFileDialog dialog(nullptr,
                           wxString::FromUTF8(title.c_str()),
                           wxEmptyString,
                           wxString::FromUTF8(default_name.c_str()),
                           wxString::FromUTF8(filter.c_str()),
                           wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        
        if (dialog.ShowModal() == wxID_OK) {
            result = dialog.GetPath().ToUTF8().data();
        }
        
        std::lock_guard<std::mutex> lock(mtx);
        done = true;
        cv.notify_one();
    });
    
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [&done]() { return done; });
    
    return result;
}

//------------------------------------------------------------------------------
// Utility
//------------------------------------------------------------------------------

std::string PluginContextImpl::get_plugin_data_dir() const
{
    // Create plugin-specific data directory
    boost::filesystem::path data_path = boost::filesystem::path(data_dir()) / "plugins" / m_plugin_id;
    
    if (!boost::filesystem::exists(data_path)) {
        boost::filesystem::create_directories(data_path);
    }
    
    return data_path.string();
}

std::string PluginContextImpl::get_resources_dir() const
{
    return resources_dir();
}

void PluginContextImpl::run_on_ui_thread(std::function<void()> fn)
{
    // Execute function on the main UI thread
    // wxWidgets provides CallAfter for this
    wxGetApp().CallAfter(fn);
}

//==============================================================================
// Factory Function
//==============================================================================

std::unique_ptr<PluginContext> create_plugin_context(const std::string& plugin_id,
                                                      PluginHost* host,
                                                      GUI::Plater* plater,
                                                      GUI::Worker* worker)
{
    return std::make_unique<PluginContextImpl>(plugin_id, host, plater, worker);
}

} // namespace Plugin
} // namespace Slic3r

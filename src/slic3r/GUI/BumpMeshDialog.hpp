// OrcaSlicer BumpMesh Texture Dialog
// Copyright (C) 2024 OrcaSlicer Contributors
// License: AGPL-3.0

#pragma once

#include "GUI_Utils.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"

#include <wx/dialog.h>
#include <wx/slider.h>
#include <wx/stattext.h>
#include <wx/sizer.h>
#include <wx/collpane.h>
#include <wx/scrolwin.h>
#include <wx/tglbtn.h>
#include <wx/wrapsizer.h>
#include <wx/bmpbuttn.h>

#include <string>
#include <vector>
#include <functional>

namespace Slic3r {
namespace GUI {

// Represents one texture choice with a thumbnail
struct TextureInfo {
    std::string id;
    std::string name;
    wxBitmap    thumbnail;  // Will be generated procedurally or loaded
};

//==============================================================================
// FloatSlider — a wxSlider mapped to a floating-point range with label
//==============================================================================

class FloatSlider : public wxPanel {
public:
    FloatSlider(wxWindow* parent,
                const wxString& label,
                double min_val, double max_val, double initial,
                int steps = 1000);

    double GetValue() const;
    void   SetValue(double v);

    void SetChangeCallback(std::function<void(double)> cb) { m_callback = std::move(cb); }

private:
    void on_slider(wxCommandEvent& evt);
    void update_label();

    wxSlider*     m_slider    = nullptr;
    wxStaticText* m_val_label = nullptr;
    double        m_min;
    double        m_max;
    int           m_steps;
    std::function<void(double)> m_callback;
};

//==============================================================================
// BumpMeshDialog — main texture settings dialog
//==============================================================================

class BumpMeshDialog : public DPIDialog {
public:
    BumpMeshDialog(wxWindow* parent, int object_idx);
    ~BumpMeshDialog() override = default;

    // Collected settings — will be sent to plugin when Apply is clicked
    struct Settings {
        // Displacement map
        std::string texture_id;

        // Texture smoothing
        double texture_smoothing = 5.0;

        // Projection
        int    projection_mode      = 0;  // index into mode list
        double transition_smoothing = 0.5;

        // Texture depth
        double amplitude            = 0.5;
        double boundary_falloff     = 1.0;
        bool   symmetric            = false;
        bool   preview_3d           = true;

        // Transform
        double scale_u   = 1.0;
        double scale_v   = 1.0;
        bool   lock_uv   = true;
        double offset_u  = 0.0;
        double offset_v  = 0.0;
        double rotation  = 0.0;

        // Mask angles
        double bottom_faces = 0.0;
        double top_faces    = 0.0;

        // Surface masking
        int  mask_mode  = 0; // 0=Exclude, 1=Include only
        int  mask_tool  = 0; // 0=Brush, 1=Fill
        double max_angle = 90.0;

        // Export
        double resolution       = 0.10;
        double output_triangles = 500000; // 500k default
    };

    const Settings& get_settings() const { return m_settings; }

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    void build_ui();

    wxPanel* create_displacement_section(wxWindow* parent);
    wxPanel* create_projection_section(wxWindow* parent);
    wxPanel* create_depth_section(wxWindow* parent);
    wxPanel* create_transform_section(wxWindow* parent);
    wxPanel* create_mask_angles_section(wxWindow* parent);
    wxPanel* create_surface_masking_section(wxWindow* parent);
    wxPanel* create_export_section(wxWindow* parent);

    // Section helper — collapsible group with title
    wxCollapsiblePane* add_section(wxWindow* parent, wxSizer* sizer,
                                   const wxString& title, wxPanel* content);

    void populate_textures();
    void on_texture_selected(const std::string& id);
    void on_custom_texture();
    void on_apply(wxCommandEvent& evt);
    void on_close(wxCommandEvent& evt);

    int m_object_idx;
    Settings m_settings;

    // Texture thumbnails panel
    wxScrolledWindow* m_texture_grid = nullptr;
    std::vector<TextureInfo> m_textures;

    // Lock-UV toggle
    bool m_lock_uv = true;
    FloatSlider* m_scale_u_slider = nullptr;
    FloatSlider* m_scale_v_slider = nullptr;

    // Surface masking exclusive buttons
    wxToggleButton* m_btn_exclude     = nullptr;
    wxToggleButton* m_btn_include     = nullptr;
    wxToggleButton* m_btn_brush       = nullptr;
    wxToggleButton* m_btn_fill        = nullptr;
};

} // namespace GUI
} // namespace Slic3r

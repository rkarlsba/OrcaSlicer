// OrcaSlicer BumpMesh Texture Dialog Implementation
// Copyright (C) 2024 OrcaSlicer Contributors
// License: AGPL-3.0

#include "BumpMeshDialog.hpp"
#include "I18N.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StaticLine.hpp"
#include "Widgets/CheckBox.hpp"

#include "libslic3r/Plugin/PluginHost.hpp"

#include <wx/statbox.h>
#include <wx/checkbox.h>
#include <wx/wrapsizer.h>
#include <wx/filedlg.h>
#include <wx/dcmemory.h>

namespace Slic3r {
namespace GUI {

//==============================================================================
// FloatSlider
//==============================================================================

FloatSlider::FloatSlider(wxWindow* parent,
                         const wxString& label,
                         double min_val, double max_val, double initial,
                         int steps)
    : wxPanel(parent, wxID_ANY)
    , m_min(min_val), m_max(max_val), m_steps(steps)
{
    auto* sizer = new wxBoxSizer(wxHORIZONTAL);

    auto* lbl = new wxStaticText(this, wxID_ANY, label);
    lbl->SetMinSize(wxSize(FromDIP(135), -1));
    sizer->Add(lbl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));

    int init_pos = int((initial - m_min) / (m_max - m_min) * m_steps);
    m_slider = new wxSlider(this, wxID_ANY, init_pos, 0, m_steps,
                            wxDefaultPosition, wxSize(FromDIP(200), -1));
    sizer->Add(m_slider, 1, wxALIGN_CENTER_VERTICAL);

    m_val_label = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition,
                                   wxSize(FromDIP(60), -1), wxALIGN_RIGHT);
    sizer->Add(m_val_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(5));

    SetSizer(sizer);
    update_label();

    m_slider->Bind(wxEVT_SLIDER, &FloatSlider::on_slider, this);
}

double FloatSlider::GetValue() const
{
    return m_min + (m_max - m_min) * m_slider->GetValue() / double(m_steps);
}

void FloatSlider::SetValue(double v)
{
    int pos = int((v - m_min) / (m_max - m_min) * m_steps);
    m_slider->SetValue(std::clamp(pos, 0, m_steps));
    update_label();
}

void FloatSlider::on_slider(wxCommandEvent& evt)
{
    update_label();
    if (m_callback) m_callback(GetValue());
}

void FloatSlider::update_label()
{
    double v = GetValue();
    // Choose format based on range
    if (m_max - m_min >= 100)
        m_val_label->SetLabel(wxString::Format("%.0f", v));
    else if (m_max - m_min >= 1)
        m_val_label->SetLabel(wxString::Format("%.2f", v));
    else
        m_val_label->SetLabel(wxString::Format("%.3f", v));
}

//==============================================================================
// BumpMeshDialog
//==============================================================================

BumpMeshDialog::BumpMeshDialog(wxWindow* parent, int object_idx)
    : DPIDialog(parent, wxID_ANY, _L("BumpMesh Textures"),
                wxDefaultPosition, wxSize(FromDIP(520), FromDIP(780)),
                wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_object_idx(object_idx)
{
    SetBackgroundColour(*wxWHITE);
    populate_textures();
    build_ui();
    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void BumpMeshDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    Refresh();
    Layout();
}

// Procedurally generated placeholder thumbnails
void BumpMeshDialog::populate_textures()
{
    // These match the texture names from the bumpmesh plugin
    const std::vector<std::pair<std::string, std::string>> tex_list = {
        {"basket_weave",  "Basket Weave"},
        {"brick",         "Brick"},
        {"bubble",        "Bubble"},
        {"carbon_fiber",  "Carbon Fiber"},
        {"crystal",       "Crystal"},
        {"dots",          "Dots"},
        {"grid",          "Grid"},
        {"grip_surface",  "Grip Surface"},
        {"hexagon",       "Hexagon"},
        {"hexagons",      "Hexagons"},
        {"isogrid",       "Isogrid"},
        {"knitting",      "Knitting"},
        {"knurling",      "Knurling"},
        {"leather",       "Leather"},
        {"leather_2",     "Leather 2"},
        {"noise",         "Noise"},
        {"stripes",       "Stripes"},
        {"stripes_2",     "Stripes 2"},
        {"voronoi",       "Voronoi"},
        {"weave",         "Weave"},
        {"weave_2",       "Weave 2"},
        {"weave_3",       "Weave 3"},
        {"wood",          "Wood"},
        {"woodgrain_2",   "Woodgrain 2"},
        {"woodgrain_3",   "Woodgrain 3"},
    };

    const int thumb_size = FromDIP(64);
    for (const auto& [id, name] : tex_list) {
        TextureInfo ti;
        ti.id   = id;
        ti.name = name;

        // Create a simple procedural placeholder bitmap
        wxBitmap bmp(thumb_size, thumb_size);
        {
            wxMemoryDC dc(bmp);
            // Use a hash of the id to generate a distinct colour pattern
            unsigned h = 0;
            for (char c : id) h = h * 31 + c;
            int r = 120 + (h % 80);
            int g = 120 + ((h / 80) % 80);
            int b = 120 + ((h / 6400) % 80);
            dc.SetBrush(wxBrush(wxColour(r, g, b)));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(0, 0, thumb_size, thumb_size);

            // Draw a simple pattern based on id hash
            dc.SetPen(wxPen(wxColour(r - 40, g - 40, b - 40), 1));
            int pattern = h % 5;
            switch (pattern) {
            case 0: // grid
                for (int i = 0; i < thumb_size; i += 8) {
                    dc.DrawLine(i, 0, i, thumb_size);
                    dc.DrawLine(0, i, thumb_size, i);
                }
                break;
            case 1: // diagonal
                for (int i = -thumb_size; i < thumb_size * 2; i += 8) {
                    dc.DrawLine(i, 0, i + thumb_size, thumb_size);
                }
                break;
            case 2: // dots
                for (int x = 4; x < thumb_size; x += 10)
                    for (int y = 4; y < thumb_size; y += 10)
                        dc.DrawCircle(x, y, 3);
                break;
            case 3: // hexagons (simplified)
                for (int x = 0; x < thumb_size; x += 12)
                    for (int y = 0; y < thumb_size; y += 10)
                        dc.DrawCircle(x + (y / 10 % 2) * 6, y, 5);
                break;
            default: // horizontal lines
                for (int i = 0; i < thumb_size; i += 6)
                    dc.DrawLine(0, i, thumb_size, i);
                break;
            }

            // Label
            dc.SetTextForeground(*wxWHITE);
            dc.SetFont(wxFont(7, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        }
        ti.thumbnail = bmp;
        m_textures.push_back(std::move(ti));
    }
}

void BumpMeshDialog::build_ui()
{
    auto* main_sizer = new wxBoxSizer(wxVERTICAL);

    // Scrollable content
    auto* scroll = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition,
                                        wxDefaultSize, wxVSCROLL);
    scroll->SetScrollRate(0, FromDIP(10));

    auto* content = new wxBoxSizer(wxVERTICAL);

    // -- Displacement Map --
    content->Add(create_displacement_section(scroll), 0, wxEXPAND | wxALL, FromDIP(8));
    content->Add(new StaticLine(scroll), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));

    // -- Projection --
    content->Add(create_projection_section(scroll), 0, wxEXPAND | wxALL, FromDIP(8));
    content->Add(new StaticLine(scroll), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));

    // -- Texture Depth --
    content->Add(create_depth_section(scroll), 0, wxEXPAND | wxALL, FromDIP(8));
    content->Add(new StaticLine(scroll), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));

    // -- Transform --
    content->Add(create_transform_section(scroll), 0, wxEXPAND | wxALL, FromDIP(8));
    content->Add(new StaticLine(scroll), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));

    // -- Mask Angles --
    content->Add(create_mask_angles_section(scroll), 0, wxEXPAND | wxALL, FromDIP(8));
    content->Add(new StaticLine(scroll), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));

    // -- Surface Masking --
    content->Add(create_surface_masking_section(scroll), 0, wxEXPAND | wxALL, FromDIP(8));
    content->Add(new StaticLine(scroll), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));

    // -- Export --
    content->Add(create_export_section(scroll), 0, wxEXPAND | wxALL, FromDIP(8));

    scroll->SetSizer(content);
    main_sizer->Add(scroll, 1, wxEXPAND);

    // Bottom buttons
    auto* btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    btn_sizer->AddStretchSpacer();

    auto* apply_btn = new Button(this, _L("Apply"));
    apply_btn->SetMinSize(wxSize(FromDIP(90), FromDIP(32)));
    apply_btn->Bind(wxEVT_BUTTON, &BumpMeshDialog::on_apply, this);
    btn_sizer->Add(apply_btn, 0, wxRIGHT, FromDIP(8));

    auto* close_btn = new Button(this, _L("Close"));
    close_btn->SetMinSize(wxSize(FromDIP(90), FromDIP(32)));
    close_btn->Bind(wxEVT_BUTTON, &BumpMeshDialog::on_close, this);
    btn_sizer->Add(close_btn, 0);

    main_sizer->Add(btn_sizer, 0, wxEXPAND | wxALL, FromDIP(10));
    SetSizer(main_sizer);
}

//------------------------------------------------------------------------------
// Displacement Map section
//------------------------------------------------------------------------------

wxPanel* BumpMeshDialog::create_displacement_section(wxWindow* parent)
{
    auto* panel = new wxPanel(parent);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(panel, wxID_ANY, _L("Displacement Map"));
    title->SetFont(Label::Head_14);
    sizer->Add(title, 0, wxBOTTOM, FromDIP(6));

    // Texture thumbnail grid (scrolled, wrapping)
    m_texture_grid = new wxScrolledWindow(panel, wxID_ANY, wxDefaultPosition,
                                          wxSize(-1, FromDIP(200)),
                                          wxVSCROLL | wxBORDER_SIMPLE);
    m_texture_grid->SetScrollRate(0, FromDIP(10));
    m_texture_grid->SetBackgroundColour(wxColour(245, 245, 245));

    auto* grid_sizer = new wxWrapSizer(wxHORIZONTAL);

    const int btn_size = FromDIP(72);
    for (const auto& ti : m_textures) {
        auto* btn = new wxBitmapButton(m_texture_grid, wxID_ANY, ti.thumbnail,
                                       wxDefaultPosition, wxSize(btn_size, btn_size));
        btn->SetToolTip(wxString::FromUTF8(ti.name));
        std::string tex_id = ti.id;
        btn->Bind(wxEVT_BUTTON, [this, tex_id](wxCommandEvent&) {
            on_texture_selected(tex_id);
        });
        grid_sizer->Add(btn, 0, wxALL, FromDIP(2));
    }

    m_texture_grid->SetSizer(grid_sizer);
    sizer->Add(m_texture_grid, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

    // Upload custom map button
    auto* custom_btn = new Button(panel, _L("Upload Custom Map..."));
    custom_btn->SetMinSize(wxSize(FromDIP(170), FromDIP(28)));
    custom_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_custom_texture(); });
    sizer->Add(custom_btn, 0, wxBOTTOM, FromDIP(6));

    // Texture smoothing slider
    auto* smoothing = new FloatSlider(panel, _L("Texture Smoothing"), 0.0, 20.0, 5.0);
    smoothing->SetChangeCallback([this](double v) { m_settings.texture_smoothing = v; });
    sizer->Add(smoothing, 0, wxEXPAND);

    panel->SetSizer(sizer);
    return panel;
}

//------------------------------------------------------------------------------
// Projection section
//------------------------------------------------------------------------------

wxPanel* BumpMeshDialog::create_projection_section(wxWindow* parent)
{
    auto* panel = new wxPanel(parent);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(panel, wxID_ANY, _L("Projection"));
    title->SetFont(Label::Head_14);
    sizer->Add(title, 0, wxBOTTOM, FromDIP(6));

    // Mode combo
    auto* mode_row = new wxBoxSizer(wxHORIZONTAL);
    auto* mode_lbl = new wxStaticText(panel, wxID_ANY, _L("Mode"));
    mode_lbl->SetMinSize(wxSize(FromDIP(135), -1));
    mode_row->Add(mode_lbl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));

    wxArrayString modes;
    modes.Add(_L("Triplanar"));
    modes.Add(_L("Cubic (Box)"));
    modes.Add(_L("Cylindrical"));
    modes.Add(_L("Spherical"));
    modes.Add(_L("Planar XY"));
    modes.Add(_L("Planar XZ"));
    modes.Add(_L("Planar YZ"));
    auto* mode_combo = new wxComboBox(panel, wxID_ANY, modes[0],
                                      wxDefaultPosition, wxSize(FromDIP(200), -1),
                                      modes, wxCB_READONLY);
    mode_combo->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& e) {
        m_settings.projection_mode = e.GetSelection();
    });
    mode_row->Add(mode_combo, 1, wxALIGN_CENTER_VERTICAL);
    sizer->Add(mode_row, 0, wxEXPAND | wxBOTTOM, FromDIP(4));

    // Transition smoothing
    auto* trans = new FloatSlider(panel, _L("Transition Smoothing"), 0.0, 1.0, 0.5);
    trans->SetChangeCallback([this](double v) { m_settings.transition_smoothing = v; });
    sizer->Add(trans, 0, wxEXPAND);

    panel->SetSizer(sizer);
    return panel;
}

//------------------------------------------------------------------------------
// Texture Depth section
//------------------------------------------------------------------------------

wxPanel* BumpMeshDialog::create_depth_section(wxWindow* parent)
{
    auto* panel = new wxPanel(parent);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(panel, wxID_ANY, _L("Texture Depth"));
    title->SetFont(Label::Head_14);
    sizer->Add(title, 0, wxBOTTOM, FromDIP(6));

    auto* amp = new FloatSlider(panel, _L("Amplitude"), 0.0, 1.0, 0.5);
    amp->SetChangeCallback([this](double v) { m_settings.amplitude = v; });
    sizer->Add(amp, 0, wxEXPAND | wxBOTTOM, FromDIP(2));

    auto* falloff = new FloatSlider(panel, _L("Boundary Falloff"), 0.0, 10.0, 1.0);
    falloff->SetChangeCallback([this](double v) { m_settings.boundary_falloff = v; });
    sizer->Add(falloff, 0, wxEXPAND | wxBOTTOM, FromDIP(4));

    // Checkboxes
    auto* sym_cb = new wxCheckBox(panel, wxID_ANY, _L("Symmetric Displacement"));
    sym_cb->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& e) {
        m_settings.symmetric = e.IsChecked();
    });
    sizer->Add(sym_cb, 0, wxBOTTOM, FromDIP(2));

    auto* preview_cb = new wxCheckBox(panel, wxID_ANY, _L("3D Preview"));
    preview_cb->SetValue(true);
    preview_cb->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& e) {
        m_settings.preview_3d = e.IsChecked();
    });
    sizer->Add(preview_cb, 0);

    panel->SetSizer(sizer);
    return panel;
}

//------------------------------------------------------------------------------
// Transform section
//------------------------------------------------------------------------------

wxPanel* BumpMeshDialog::create_transform_section(wxWindow* parent)
{
    auto* panel = new wxPanel(parent);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(panel, wxID_ANY, _L("Transform"));
    title->SetFont(Label::Head_14);
    sizer->Add(title, 0, wxBOTTOM, FromDIP(6));

    // Scale U
    m_scale_u_slider = new FloatSlider(panel, _L("Scale U"), 0.05, 10.0, 1.0);
    m_scale_u_slider->SetChangeCallback([this](double v) {
        m_settings.scale_u = v;
        if (m_lock_uv) {
            m_settings.scale_v = v;
            m_scale_v_slider->SetValue(v);
        }
    });
    sizer->Add(m_scale_u_slider, 0, wxEXPAND | wxBOTTOM, FromDIP(2));

    // Lock UV row
    auto* lock_row = new wxBoxSizer(wxHORIZONTAL);
    lock_row->AddSpacer(FromDIP(135 + 5)); // Align with slider area
    auto* lock_cb = new wxCheckBox(panel, wxID_ANY, _L("Lock U/V"));
    lock_cb->SetValue(true);
    lock_cb->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& e) {
        m_lock_uv = e.IsChecked();
        m_settings.lock_uv = m_lock_uv;
        if (m_lock_uv) {
            m_settings.scale_v = m_settings.scale_u;
            m_scale_v_slider->SetValue(m_settings.scale_u);
        }
    });
    lock_row->Add(lock_cb, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(lock_row, 0, wxBOTTOM, FromDIP(2));

    // Scale V
    m_scale_v_slider = new FloatSlider(panel, _L("Scale V"), 0.05, 10.0, 1.0);
    m_scale_v_slider->SetChangeCallback([this](double v) {
        m_settings.scale_v = v;
        if (m_lock_uv) {
            m_settings.scale_u = v;
            m_scale_u_slider->SetValue(v);
        }
    });
    sizer->Add(m_scale_v_slider, 0, wxEXPAND | wxBOTTOM, FromDIP(2));

    // Offset U
    auto* off_u = new FloatSlider(panel, _L("Offset U"), -1.0, 1.0, 0.0);
    off_u->SetChangeCallback([this](double v) { m_settings.offset_u = v; });
    sizer->Add(off_u, 0, wxEXPAND | wxBOTTOM, FromDIP(2));

    // Offset V
    auto* off_v = new FloatSlider(panel, _L("Offset V"), -1.0, 1.0, 0.0);
    off_v->SetChangeCallback([this](double v) { m_settings.offset_v = v; });
    sizer->Add(off_v, 0, wxEXPAND | wxBOTTOM, FromDIP(2));

    // Rotation
    auto* rot = new FloatSlider(panel, _L("Rotation"), 0.0, 360.0, 0.0);
    rot->SetChangeCallback([this](double v) { m_settings.rotation = v; });
    sizer->Add(rot, 0, wxEXPAND);

    panel->SetSizer(sizer);
    return panel;
}

//------------------------------------------------------------------------------
// Mask Angles section
//------------------------------------------------------------------------------

wxPanel* BumpMeshDialog::create_mask_angles_section(wxWindow* parent)
{
    auto* panel = new wxPanel(parent);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(panel, wxID_ANY, _L("Mask Angles"));
    title->SetFont(Label::Head_14);
    sizer->Add(title, 0, wxBOTTOM, FromDIP(6));

    auto* bottom = new FloatSlider(panel, _L("Bottom Faces"), 0.0, 90.0, 0.0);
    bottom->SetChangeCallback([this](double v) { m_settings.bottom_faces = v; });
    sizer->Add(bottom, 0, wxEXPAND | wxBOTTOM, FromDIP(2));

    auto* top = new FloatSlider(panel, _L("Top Faces"), 0.0, 90.0, 0.0);
    top->SetChangeCallback([this](double v) { m_settings.top_faces = v; });
    sizer->Add(top, 0, wxEXPAND);

    panel->SetSizer(sizer);
    return panel;
}

//------------------------------------------------------------------------------
// Surface Masking section
//------------------------------------------------------------------------------

wxPanel* BumpMeshDialog::create_surface_masking_section(wxWindow* parent)
{
    auto* panel = new wxPanel(parent);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(panel, wxID_ANY, _L("Surface Masking"));
    title->SetFont(Label::Head_14);
    sizer->Add(title, 0, wxBOTTOM, FromDIP(6));

    auto* desc = new wxStaticText(panel, wxID_ANY,
        _L("Paint areas on the model to include or exclude from texture application."));
    desc->SetForegroundColour(wxColour(100, 100, 100));
    desc->Wrap(FromDIP(450));
    sizer->Add(desc, 0, wxBOTTOM, FromDIP(6));

    // Exclusive group 1: Exclude / Include Only
    auto* group1_lbl = new wxStaticText(panel, wxID_ANY, _L("Selection mode:"));
    sizer->Add(group1_lbl, 0, wxBOTTOM, FromDIP(2));

    auto* group1 = new wxBoxSizer(wxHORIZONTAL);
    m_btn_exclude = new wxToggleButton(panel, wxID_ANY, _L("Exclude"),
                                       wxDefaultPosition, wxSize(FromDIP(100), FromDIP(28)));
    m_btn_include = new wxToggleButton(panel, wxID_ANY, _L("Include Only"),
                                       wxDefaultPosition, wxSize(FromDIP(100), FromDIP(28)));
    m_btn_exclude->SetValue(true);
    m_settings.mask_mode = 0;

    m_btn_exclude->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) {
        m_btn_exclude->SetValue(true);
        m_btn_include->SetValue(false);
        m_settings.mask_mode = 0;
    });
    m_btn_include->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) {
        m_btn_include->SetValue(true);
        m_btn_exclude->SetValue(false);
        m_settings.mask_mode = 1;
    });

    group1->Add(m_btn_exclude, 0, wxRIGHT, FromDIP(4));
    group1->Add(m_btn_include, 0);
    sizer->Add(group1, 0, wxBOTTOM, FromDIP(8));

    // Exclusive group 2: Brush / Fill
    auto* group2_lbl = new wxStaticText(panel, wxID_ANY, _L("Paint tool:"));
    sizer->Add(group2_lbl, 0, wxBOTTOM, FromDIP(2));

    auto* group2 = new wxBoxSizer(wxHORIZONTAL);
    m_btn_brush = new wxToggleButton(panel, wxID_ANY, _L("Brush"),
                                     wxDefaultPosition, wxSize(FromDIP(100), FromDIP(28)));
    m_btn_fill  = new wxToggleButton(panel, wxID_ANY, _L("Fill"),
                                     wxDefaultPosition, wxSize(FromDIP(100), FromDIP(28)));
    m_btn_brush->SetValue(true);
    m_settings.mask_tool = 0;

    m_btn_brush->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) {
        m_btn_brush->SetValue(true);
        m_btn_fill->SetValue(false);
        m_settings.mask_tool = 0;
    });
    m_btn_fill->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) {
        m_btn_fill->SetValue(true);
        m_btn_brush->SetValue(false);
        m_settings.mask_tool = 1;
    });

    group2->Add(m_btn_brush, 0, wxRIGHT, FromDIP(4));
    group2->Add(m_btn_fill, 0);
    sizer->Add(group2, 0, wxBOTTOM, FromDIP(8));

    // Max angle
    auto* max_angle = new FloatSlider(panel, _L("Max Angle"), 0.0, 360.0, 90.0);
    max_angle->SetChangeCallback([this](double v) { m_settings.max_angle = v; });
    sizer->Add(max_angle, 0, wxEXPAND);

    panel->SetSizer(sizer);
    return panel;
}

//------------------------------------------------------------------------------
// Export section
//------------------------------------------------------------------------------

wxPanel* BumpMeshDialog::create_export_section(wxWindow* parent)
{
    auto* panel = new wxPanel(parent);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(panel, wxID_ANY, _L("Export"));
    title->SetFont(Label::Head_14);
    sizer->Add(title, 0, wxBOTTOM, FromDIP(6));

    auto* res = new FloatSlider(panel, _L("Resolution"), 0.01, 5.0, 0.10, 500);
    res->SetChangeCallback([this](double v) { m_settings.resolution = v; });
    sizer->Add(res, 0, wxEXPAND | wxBOTTOM, FromDIP(2));

    // Output triangles: 10k - 20M. Use log scale internally.
    auto* tri_row = new wxBoxSizer(wxHORIZONTAL);
    auto* tri_lbl = new wxStaticText(panel, wxID_ANY, _L("Output Triangles"));
    tri_lbl->SetMinSize(wxSize(FromDIP(135), -1));
    tri_row->Add(tri_lbl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));

    auto* tri_slider = new wxSlider(panel, wxID_ANY, 500, 0, 1000,
                                    wxDefaultPosition, wxSize(FromDIP(200), -1));
    auto* tri_val = new wxStaticText(panel, wxID_ANY, "500k",
                                     wxDefaultPosition, wxSize(FromDIP(60), -1),
                                     wxALIGN_RIGHT);

    // Map slider 0-1000 to 10k-20M (log scale)
    auto update_tri = [this, tri_slider, tri_val](int pos) {
        double min_log = std::log10(10000.0);
        double max_log = std::log10(20000000.0);
        double val = std::pow(10.0, min_log + (max_log - min_log) * pos / 1000.0);
        m_settings.output_triangles = val;
        if (val >= 1000000)
            tri_val->SetLabel(wxString::Format("%.1fM", val / 1000000.0));
        else
            tri_val->SetLabel(wxString::Format("%.0fk", val / 1000.0));
    };

    tri_slider->Bind(wxEVT_SLIDER, [update_tri](wxCommandEvent& e) {
        update_tri(e.GetInt());
    });
    update_tri(500); // initial value

    tri_row->Add(tri_slider, 1, wxALIGN_CENTER_VERTICAL);
    tri_row->Add(tri_val, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(5));
    sizer->Add(tri_row, 0, wxEXPAND);

    panel->SetSizer(sizer);
    return panel;
}

//------------------------------------------------------------------------------
// Actions
//------------------------------------------------------------------------------

void BumpMeshDialog::on_texture_selected(const std::string& id)
{
    m_settings.texture_id = id;

    // Highlight selected thumbnail (simple approach: update tooltip)
    // TODO: Add visual selection indicator (border highlight)
    BOOST_LOG_TRIVIAL(info) << "BumpMesh: selected texture " << id;
}

void BumpMeshDialog::on_custom_texture()
{
    wxFileDialog dlg(this,
                     _L("Select Displacement Map Image"),
                     wxEmptyString, wxEmptyString,
                     _L("Image files (*.png;*.jpg;*.bmp;*.tga)|*.png;*.jpg;*.jpeg;*.bmp;*.tga|All files (*.*)|*.*"),
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() == wxID_OK) {
        m_settings.texture_id = "custom:" + std::string(dlg.GetPath().ToUTF8());
        BOOST_LOG_TRIVIAL(info) << "BumpMesh: custom texture " << m_settings.texture_id;
    }
}

void BumpMeshDialog::on_apply(wxCommandEvent& evt)
{
    if (m_settings.texture_id.empty()) {
        wxMessageBox(_L("Please select a texture first."),
                     _L("BumpMesh"), wxOK | wxICON_WARNING, this);
        return;
    }

    // Send settings to plugin via PluginHost
    try {
        nlohmann::json params;
        params["object_idx"]           = m_object_idx;
        params["texture_id"]           = m_settings.texture_id;
        params["texture_smoothing"]    = m_settings.texture_smoothing;
        params["projection_mode"]      = m_settings.projection_mode;
        params["transition_smoothing"] = m_settings.transition_smoothing;
        params["amplitude"]            = m_settings.amplitude;
        params["boundary_falloff"]     = m_settings.boundary_falloff;
        params["symmetric"]            = m_settings.symmetric;
        params["preview_3d"]           = m_settings.preview_3d;
        params["scale_u"]              = m_settings.scale_u;
        params["scale_v"]              = m_settings.scale_v;
        params["offset_u"]             = m_settings.offset_u;
        params["offset_v"]             = m_settings.offset_v;
        params["rotation"]             = m_settings.rotation;
        params["bottom_faces"]         = m_settings.bottom_faces;
        params["top_faces"]            = m_settings.top_faces;
        params["mask_mode"]            = m_settings.mask_mode;
        params["mask_tool"]            = m_settings.mask_tool;
        params["max_angle"]            = m_settings.max_angle;
        params["resolution"]           = m_settings.resolution;
        params["output_triangles"]     = m_settings.output_triangles;

        Plugin::plugin_host().handle_menu_click("@orcaslicer/bumpmesh",
                                                m_settings.texture_id);
        BOOST_LOG_TRIVIAL(info) << "BumpMesh: applied texture " << m_settings.texture_id
                                << " to object " << m_object_idx;
    } catch (const std::exception& e) {
        wxMessageBox(wxString::Format(_L("Failed to apply texture: %s"),
                                      wxString::FromUTF8(e.what())),
                     _L("BumpMesh"), wxOK | wxICON_ERROR, this);
    }
}

void BumpMeshDialog::on_close(wxCommandEvent& evt)
{
    EndModal(wxID_CANCEL);
}

} // namespace GUI
} // namespace Slic3r

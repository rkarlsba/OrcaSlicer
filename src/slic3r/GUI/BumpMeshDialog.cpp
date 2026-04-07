// OrcaSlicer BumpMesh Texture Dialog Implementation
// Copyright (C) 2024 OrcaSlicer Contributors
// License: AGPL-3.0

#include "BumpMeshDialog.hpp"
#include "I18N.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StaticLine.hpp"
#include "Widgets/CheckBox.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/MeshDisplacement.hpp"
#include "libslic3r/Plugin/PluginHost.hpp"
#include "libslic3r/Utils.hpp"
#include "GLCanvas3D.hpp"

#include <wx/statbox.h>
#include <wx/checkbox.h>
#include <wx/wrapsizer.h>
#include <wx/filedlg.h>
#include <wx/dcmemory.h>
#include <wx/busycursor.h>
#include <wx/image.h>
#include <wx/filename.h>
#include <wx/progdlg.h>

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

// Load texture thumbnails from the bumpmesh plugin's textures directory
void BumpMeshDialog::populate_textures()
{
    // IDs and files must match the plugin's BUILTIN_TEXTURES array in bumpmesh/index.js
    struct TexDef { const char* id; const char* name; const char* file; };
    static const TexDef tex_list[] = {
        {"basket",       "Basket Weave",  "basket.jpg"},
        {"brick",        "Brick",         "brick.jpg"},
        {"bubble",       "Bubble",        "bubble.jpg"},
        {"carbonFiber",  "Carbon Fiber",  "carbonFiber.jpg"},
        {"crystal",      "Crystal",       "crystal.jpg"},
        {"dots",         "Dots",          "dots.jpg"},
        {"grid",         "Grid",          "grid.png"},
        {"gripSurface",  "Grip Surface",  "gripSurface.jpg"},
        {"hexagon",      "Hexagon",       "hexagon.jpg"},
        {"hexagons",     "Hexagons",      "hexagons.jpg"},
        {"isogrid",      "Isogrid",       "isogrid.png"},
        {"knitting",     "Knitting",      "knitting.jpg"},
        {"knurling",     "Knurling",      "knurling.jpg"},
        {"leather",      "Leather",       "leather.jpg"},
        {"leather2",     "Leather 2",     "leather2.jpg"},
        {"noise",        "Noise",         "noise.jpg"},
        {"stripes",      "Stripes",       "stripes.png"},
        {"stripes_02",   "Stripes 2",     "stripes_02.png"},
        {"voronoi",      "Voronoi",       "voronoi.jpg"},
        {"weave",        "Weave",         "weave.jpg"},
        {"weave_02",     "Weave 2",       "weave_02.jpg"},
        {"weave_03",     "Weave 3",       "weave_03.jpg"},
        {"wood",         "Wood",          "wood.jpg"},
        {"woodgrain_02", "Woodgrain 2",   "woodgrain_02.jpg"},
        {"woodgrain_03", "Woodgrain 3",   "woodgrain_03.jpg"},
    };

    // Resolve the textures directory in the app bundle
    std::string tex_dir = Slic3r::resources_dir() + "/plugins/bumpmesh/textures";

    const int thumb_size = FromDIP(64);
    for (const auto& def : tex_list) {
        TextureInfo ti;
        ti.id   = def.id;
        ti.name = def.name;
        ti.file = def.file;

        // Try to load actual texture image from disk
        std::string img_path = tex_dir + "/" + def.file;
        wxImage img;
        if (wxFileExists(wxString::FromUTF8(img_path)) &&
            img.LoadFile(wxString::FromUTF8(img_path))) {
            // Convert to grayscale and scale to thumbnail
            img = img.ConvertToGreyscale();
            img.Rescale(thumb_size, thumb_size, wxIMAGE_QUALITY_HIGH);
            ti.thumbnail = wxBitmap(img);
        } else {
            // Fallback: plain gray placeholder
            wxBitmap bmp(thumb_size, thumb_size);
            wxMemoryDC dc(bmp);
            dc.SetBrush(wxBrush(wxColour(180, 180, 180)));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(0, 0, thumb_size, thumb_size);
            dc.SetTextForeground(wxColour(100, 100, 100));
            dc.SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
            dc.DrawText("?", thumb_size / 2 - 3, thumb_size / 2 - 5);
            ti.thumbnail = bmp;
        }
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

    auto* preview_btn = new Button(this, _L("Preview"));
    preview_btn->SetMinSize(wxSize(FromDIP(90), FromDIP(32)));
    preview_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        apply_displacement_to_model(true);
    });
    btn_sizer->Add(preview_btn, 0, wxRIGHT, FromDIP(8));

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

    // Texture thumbnail grid (scrolled, grid layout with fixed columns)
    m_texture_grid = new wxScrolledWindow(panel, wxID_ANY, wxDefaultPosition,
                                          wxSize(-1, FromDIP(220)),
                                          wxVSCROLL | wxBORDER_SIMPLE);
    m_texture_grid->SetScrollRate(0, FromDIP(10));
    m_texture_grid->SetBackgroundColour(wxColour(240, 240, 240));

    const int btn_size = FromDIP(72);
    const int cols = 6;
    auto* grid_sizer = new wxGridSizer(cols, FromDIP(4), FromDIP(4));

    m_texture_buttons.clear();
    for (size_t i = 0; i < m_textures.size(); ++i) {
        const auto& ti = m_textures[i];
        auto* btn = new wxBitmapButton(m_texture_grid, wxID_ANY, ti.thumbnail,
                                       wxDefaultPosition, wxSize(btn_size, btn_size),
                                       wxBORDER_SIMPLE);
        btn->SetToolTip(wxString::FromUTF8(ti.name));
        std::string tex_id = ti.id;
        btn->Bind(wxEVT_BUTTON, [this, tex_id](wxCommandEvent&) {
            on_texture_selected(tex_id);
        });
        grid_sizer->Add(btn, 0, wxALL, 0);
        m_texture_buttons.push_back(btn);
    }

    m_texture_grid->SetSizer(grid_sizer);
    grid_sizer->FitInside(m_texture_grid);
    sizer->Add(m_texture_grid, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

    // Selected texture label
    m_selected_label = new wxStaticText(panel, wxID_ANY, _L("No texture selected"));
    m_selected_label->SetForegroundColour(wxColour(80, 80, 80));
    sizer->Add(m_selected_label, 0, wxBOTTOM, FromDIP(4));

    // Preview image (larger view of selected texture)
    const int preview_size = FromDIP(200);
    wxBitmap empty_preview(preview_size, preview_size);
    {
        wxMemoryDC dc(empty_preview);
        dc.SetBrush(wxBrush(wxColour(220, 220, 220)));
        dc.SetPen(wxPen(wxColour(180, 180, 180), 1, wxPENSTYLE_DOT));
        dc.DrawRectangle(0, 0, preview_size, preview_size);
        dc.SetTextForeground(wxColour(140, 140, 140));
        dc.SetFont(wxFont(11, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        wxString hint = _L("Select a texture above");
        wxSize ts = dc.GetTextExtent(hint);
        dc.DrawText(hint, (preview_size - ts.x) / 2, (preview_size - ts.y) / 2);
    }
    m_preview_image = new wxStaticBitmap(panel, wxID_ANY, empty_preview,
                                         wxDefaultPosition, wxSize(preview_size, preview_size));
    sizer->Add(m_preview_image, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, FromDIP(6));

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
// Mesh displacement helpers
//------------------------------------------------------------------------------

// Resolve the texture file path for the currently selected texture
static std::string resolve_texture_path(const std::string& texture_id,
                                         const std::vector<TextureInfo>& textures)
{
    // Custom textures start with "custom:"
    if (texture_id.substr(0, 7) == "custom:")
        return texture_id.substr(7);

    // Built-in textures — find the file in textures directory
    for (const auto& ti : textures) {
        if (ti.id == texture_id && !ti.file.empty()) {
            return Slic3r::resources_dir() + "/plugins/bumpmesh/textures/" + ti.file;
        }
    }
    return {};
}

void BumpMeshDialog::restore_original_mesh()
{
    if (!m_has_original_mesh)
        return;

    Plater* plater = wxGetApp().plater();
    Model& model = plater->model();
    if (m_object_idx < 0 || m_object_idx >= (int)model.objects.size())
        return;

    ModelObject* obj = model.objects[m_object_idx];
    if (obj->volumes.empty())
        return;

    ModelVolume* vol = obj->volumes[0];

    plater->take_snapshot("BumpMesh Undo Preview");
    plater->clear_before_change_mesh(m_object_idx);

    vol->set_mesh(indexed_triangle_set(m_original_mesh));
    vol->calculate_convex_hull();
    vol->invalidate_convex_hull_2d();
    vol->set_new_unique_id();
    obj->invalidate_bounding_box();
    obj->ensure_on_bed();
    plater->changed_mesh(m_object_idx);

    // Force immediate viewport repaint
    if (GLCanvas3D* canvas = plater->canvas3D()) {
        canvas->set_as_dirty();
        canvas->request_extra_frame();
    }
    wxYield();

    m_preview_applied = false;
    BOOST_LOG_TRIVIAL(info) << "BumpMesh: restored original mesh";
}

void BumpMeshDialog::apply_displacement_to_model(bool is_preview)
{
    if (m_settings.texture_id.empty()) {
        wxMessageBox(_L("Please select a texture first."),
                     _L("BumpMesh"), wxOK | wxICON_WARNING, this);
        return;
    }

    Plater* plater = wxGetApp().plater();
    Model& model = plater->model();
    if (m_object_idx < 0 || m_object_idx >= (int)model.objects.size()) {
        wxMessageBox(_L("Invalid object index."),
                     _L("BumpMesh"), wxOK | wxICON_ERROR, this);
        return;
    }

    ModelObject* obj = model.objects[m_object_idx];
    if (obj->volumes.empty()) {
        wxMessageBox(_L("Object has no volumes."),
                     _L("BumpMesh"), wxOK | wxICON_ERROR, this);
        return;
    }

    ModelVolume* vol = obj->volumes[0];

    // If previewing again, restore original before re-applying
    if (m_preview_applied && m_has_original_mesh) {
        vol->set_mesh(indexed_triangle_set(m_original_mesh));
        vol->calculate_convex_hull();
        vol->invalidate_convex_hull_2d();
        vol->set_new_unique_id();
        obj->invalidate_bounding_box();
    }

    // Save original mesh if we haven't yet
    if (!m_has_original_mesh) {
        m_original_mesh = vol->mesh().its;
        m_has_original_mesh = true;
    }

    // Load displacement texture via wxImage (GUI layer)
    std::string tex_path = resolve_texture_path(m_settings.texture_id, m_textures);
    if (tex_path.empty()) {
        wxMessageBox(_L("Could not find texture file."),
                     _L("BumpMesh"), wxOK | wxICON_ERROR, this);
        return;
    }

    DisplacementTexture texture;
    {
        wxImage img;
        if (!img.LoadFile(wxString::FromUTF8(tex_path))) {
            wxMessageBox(_L("Failed to load texture image."),
                         _L("BumpMesh"), wxOK | wxICON_ERROR, this);
            return;
        }
        img = img.ConvertToGreyscale();
        texture.width  = img.GetWidth();
        texture.height = img.GetHeight();
        texture.pixels.resize(texture.width * texture.height);
        const unsigned char* rgb = img.GetData();
        for (int i = 0; i < texture.width * texture.height; ++i)
            texture.pixels[i] = rgb[i * 3]; // R==G==B after greyscale
    }

    if (!texture.valid()) {
        wxMessageBox(_L("Texture image is empty."),
                     _L("BumpMesh"), wxOK | wxICON_ERROR, this);
        return;
    }

    // Build displacement settings from dialog
    DisplacementSettings ds;
    ds.amplitude         = (float) m_settings.amplitude;
    ds.scale_u           = (float) m_settings.scale_u;
    ds.scale_v           = (float) m_settings.scale_v;
    ds.offset_u          = (float) m_settings.offset_u;
    ds.offset_v          = (float) m_settings.offset_v;
    ds.rotation          = (float) m_settings.rotation;
    ds.projection_mode   = m_settings.projection_mode;
    ds.symmetric         = m_settings.symmetric;
    ds.top_angle_limit   = (float) m_settings.top_faces;
    ds.bottom_angle_limit = (float) m_settings.bottom_faces;

    // For preview, use coarser subdivision (faster)
    if (is_preview) {
        // Use the resolution setting but with a coarser default
        ds.max_edge_length = (float) m_settings.resolution * 3.0f;
        if (ds.max_edge_length <= 0.0f)
            ds.max_edge_length = 0.5f;
    } else {
        ds.max_edge_length = (float) m_settings.resolution;
    }

    // Show progress dialog
    wxProgressDialog progress_dlg(
        _L("BumpMesh — Processing"),
        is_preview ? _L("Generating preview...") : _L("Applying displacement..."),
        100, this,
        wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_CAN_ABORT);
    progress_dlg.Show();

    bool cancelled = false;

    // Get the source mesh (always from original)
    const indexed_triangle_set& source = m_original_mesh;

    BOOST_LOG_TRIVIAL(info) << "BumpMesh: " << (is_preview ? "preview" : "apply")
                            << " texture='" << m_settings.texture_id
                            << "' amplitude=" << ds.amplitude
                            << " scale=" << ds.scale_u << "x" << ds.scale_v
                            << " projection=" << ds.projection_mode
                            << " max_edge=" << ds.max_edge_length;

    // Apply displacement
    indexed_triangle_set displaced = apply_displacement(
        source, texture, ds,
        [&](float frac) {
            int pct = std::clamp(int(frac * 100), 0, 100);
            if (!progress_dlg.Update(pct))
                cancelled = true;
        });

    progress_dlg.Update(100);

    if (cancelled) {
        BOOST_LOG_TRIVIAL(info) << "BumpMesh: displacement cancelled by user";
        return;
    }

    // Take undo/redo snapshot before modifying the mesh
    plater->take_snapshot(is_preview ? "BumpMesh Preview" : "BumpMesh Apply Texture");
    plater->clear_before_change_mesh(m_object_idx);

    // Replace the volume's mesh (same pattern as GLGizmoSimplify)
    TriangleMesh new_mesh(std::move(displaced));

    vol->set_mesh(std::move(new_mesh));
    vol->calculate_convex_hull();
    vol->invalidate_convex_hull_2d();
    vol->set_new_unique_id();
    obj->invalidate_bounding_box();
    obj->ensure_on_bed();

    // Notify plater to refresh 3D viewport
    plater->changed_mesh(m_object_idx);

    // Force the 3D canvas to repaint while our modal dialog is open
    if (GLCanvas3D* canvas = plater->canvas3D()) {
        canvas->set_as_dirty();
        canvas->request_extra_frame();
    }
    wxYield();

    m_preview_applied = is_preview;

    wxString action = is_preview ? _L("Preview applied") : _L("Displacement applied");
    BOOST_LOG_TRIVIAL(info) << "BumpMesh: " << action.ToStdString()
                            << ", faces=" << vol->mesh().facets_count();
}

//------------------------------------------------------------------------------
// Actions
//------------------------------------------------------------------------------

void BumpMeshDialog::on_texture_selected(const std::string& id)
{
    m_settings.texture_id = id;

    // Find the matching texture name for the label
    std::string selected_name;
    size_t selected_idx = SIZE_MAX;
    for (size_t i = 0; i < m_textures.size(); ++i) {
        if (m_textures[i].id == id) {
            selected_name = m_textures[i].name;
            selected_idx = i;
            break;
        }
    }

    // Update "Selected:" label
    if (m_selected_label) {
        if (selected_name.empty())
            m_selected_label->SetLabel(_L("Selected: ") + wxString::FromUTF8(id));
        else
            m_selected_label->SetLabel(_L("Selected: ") + wxString::FromUTF8(selected_name));
    }

    // Highlight the selected button, reset others
    for (size_t i = 0; i < m_texture_buttons.size(); ++i) {
        if (i == selected_idx) {
            m_texture_buttons[i]->SetBackgroundColour(wxColour(0, 120, 215));
            m_selected_btn = m_texture_buttons[i];
        } else {
            m_texture_buttons[i]->SetBackgroundColour(wxColour(240, 240, 240));
        }
        m_texture_buttons[i]->Refresh();
    }

    // Update the large preview image
    update_preview();

    BOOST_LOG_TRIVIAL(info) << "BumpMesh: selected texture " << id;
}

void BumpMeshDialog::update_preview()
{
    if (!m_preview_image) return;

    const int preview_size = FromDIP(200);

    // Find the selected texture and load a larger version for preview
    for (const auto& ti : m_textures) {
        if (ti.id == m_settings.texture_id) {
            std::string tex_dir = Slic3r::resources_dir() + "/plugins/bumpmesh/textures";
            std::string img_path = tex_dir + "/" + ti.file;
            wxImage img;
            if (!ti.file.empty() && wxFileExists(wxString::FromUTF8(img_path)) &&
                img.LoadFile(wxString::FromUTF8(img_path))) {
                img = img.ConvertToGreyscale();
                img.Rescale(preview_size, preview_size, wxIMAGE_QUALITY_HIGH);
                m_preview_image->SetBitmap(wxBitmap(img));
            } else {
                // Use the thumbnail scaled up
                wxImage thumb = ti.thumbnail.ConvertToImage();
                thumb.Rescale(preview_size, preview_size, wxIMAGE_QUALITY_NEAREST);
                m_preview_image->SetBitmap(wxBitmap(thumb));
            }
            m_preview_image->Refresh();
            return;
        }
    }
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
    apply_displacement_to_model(false);
    // Close the dialog after successful final apply
    if (m_has_original_mesh)
        EndModal(wxID_OK);
}

void BumpMeshDialog::on_close(wxCommandEvent& evt)
{
    // If a preview was applied, restore the original mesh on close
    if (m_preview_applied) {
        int answer = wxMessageBox(
            _L("A preview displacement is currently applied.\n\n"
               "Do you want to keep the preview changes?"),
            _L("BumpMesh"),
            wxYES_NO | wxCANCEL | wxICON_QUESTION, this);
        if (answer == wxCANCEL)
            return;
        if (answer == wxNO)
            restore_original_mesh();
    }
    EndModal(wxID_CANCEL);
}

} // namespace GUI
} // namespace Slic3r

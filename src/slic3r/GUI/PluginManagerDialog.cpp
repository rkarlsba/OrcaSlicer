// OrcaSlicer Plugin Manager Dialog Implementation
// Copyright (C) 2024 OrcaSlicer Contributors
// License: AGPL-3.0

#include "PluginManagerDialog.hpp"
#include "I18N.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "MsgDialog.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StaticLine.hpp"

#include "libslic3r/Plugin/PluginHost.hpp"
#include "libslic3r/Plugin/PluginAPI.hpp"
#include "libslic3r/Utils.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/scrolwin.h>
#include <wx/notebook.h>
#include <wx/filedlg.h>
#include <wx/dir.h>
#include <wx/wupdlock.h>

#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>

namespace Slic3r {
namespace GUI {

namespace fs = boost::filesystem;

// Colors
static const wxColour PLUGIN_ITEM_BG(255, 255, 255);
static const wxColour PLUGIN_ITEM_BG_HOVER(245, 245, 250);
static const wxColour PLUGIN_ITEM_BORDER(220, 220, 220);
static const wxColour PLUGIN_ERROR_COLOR(200, 50, 50);
static const wxColour PLUGIN_ENABLED_COLOR(50, 150, 50);
static const wxColour PLUGIN_DISABLED_COLOR(150, 150, 150);

//==============================================================================
// PluginListItem Implementation
//==============================================================================

PluginListItem::PluginListItem(wxWindow* parent,
                               const Plugin::PluginInfo& info,
                               std::function<void(const std::string&, bool)> on_enable_change,
                               std::function<void(const std::string&)> on_settings_click,
                               std::function<void(const std::string&)> on_uninstall_click)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE)
    , m_plugin_id(info.manifest.id)
    , m_plugin_name(info.manifest.name)
    , m_plugin_version(info.manifest.version)
    , m_plugin_description(info.manifest.description)
    , m_on_enable_change(on_enable_change)
    , m_on_settings_click(on_settings_click)
    , m_on_uninstall_click(on_uninstall_click)
{
    SetBackgroundColour(PLUGIN_ITEM_BG);
    SetMinSize(wxSize(-1, FromDIP(80)));
    
    auto* main_sizer = new wxBoxSizer(wxHORIZONTAL);
    
    // Left side - checkbox for enable/disable
    auto* left_sizer = new wxBoxSizer(wxVERTICAL);
    m_enable_checkbox = new ::CheckBox(this);
    m_enable_checkbox->SetValue(info.enabled);
    m_enable_checkbox->Bind(wxEVT_TOGGLEBUTTON, &PluginListItem::on_enable_toggle, this);
    left_sizer->Add(m_enable_checkbox, 0, wxALIGN_CENTER | wxALL, FromDIP(10));
    main_sizer->Add(left_sizer, 0, wxALIGN_CENTER_VERTICAL);
    
    // Center - plugin info
    auto* center_sizer = new wxBoxSizer(wxVERTICAL);
    
    // Name and version row
    auto* name_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_name_label = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(m_plugin_name));
    m_name_label->SetFont(Label::Head_14);
    name_sizer->Add(m_name_label, 0, wxALIGN_CENTER_VERTICAL);
    
    m_version_label = new wxStaticText(this, wxID_ANY, wxString::Format(" v%s", wxString::FromUTF8(m_plugin_version)));
    m_version_label->SetForegroundColour(wxColour(128, 128, 128));
    name_sizer->Add(m_version_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(5));
    
    center_sizer->Add(name_sizer, 0, wxTOP | wxBOTTOM, FromDIP(5));
    
    // Description
    m_description_label = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(m_plugin_description));
    m_description_label->SetForegroundColour(wxColour(100, 100, 100));
    m_description_label->Wrap(FromDIP(400));
    center_sizer->Add(m_description_label, 0, wxBOTTOM, FromDIP(5));
    
    // Error label (hidden by default)
    m_error_label = new wxStaticText(this, wxID_ANY, "");
    m_error_label->SetForegroundColour(PLUGIN_ERROR_COLOR);
    m_error_label->Hide();
    center_sizer->Add(m_error_label, 0);
    
    main_sizer->Add(center_sizer, 1, wxEXPAND | wxALL, FromDIP(10));
    
    // Right side - buttons
    auto* right_sizer = new wxBoxSizer(wxVERTICAL);
    
    m_settings_btn = new Button(this, _L("Settings"));
    m_settings_btn->SetMinSize(wxSize(FromDIP(80), FromDIP(28)));
    m_settings_btn->Bind(wxEVT_BUTTON, &PluginListItem::on_settings_button, this);
    right_sizer->Add(m_settings_btn, 0, wxBOTTOM, FromDIP(5));
    
    m_uninstall_btn = new Button(this, _L("Uninstall"));
    m_uninstall_btn->SetMinSize(wxSize(FromDIP(80), FromDIP(28)));
    m_uninstall_btn->Bind(wxEVT_BUTTON, &PluginListItem::on_uninstall_button, this);
    right_sizer->Add(m_uninstall_btn, 0);
    
    main_sizer->Add(right_sizer, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(10));
    
    SetSizer(main_sizer);
    
    // Hover effects
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) {
        SetBackgroundColour(PLUGIN_ITEM_BG_HOVER);
        Refresh();
    });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) {
        SetBackgroundColour(PLUGIN_ITEM_BG);
        Refresh();
    });
}

void PluginListItem::update_state(bool enabled, bool has_error)
{
    m_enable_checkbox->SetValue(enabled);
    
    if (has_error) {
        m_error_label->SetLabel(_L("Error loading plugin"));
        m_error_label->Show();
    } else {
        m_error_label->Hide();
    }
    
    Layout();
    Refresh();
}

void PluginListItem::on_paint(wxPaintEvent& event)
{
    event.Skip();
}

void PluginListItem::on_enable_toggle(wxCommandEvent& event)
{
    if (m_on_enable_change) {
        m_on_enable_change(m_plugin_id, m_enable_checkbox->GetValue());
    }
}

void PluginListItem::on_settings_button(wxCommandEvent& event)
{
    if (m_on_settings_click) {
        m_on_settings_click(m_plugin_id);
    }
}

void PluginListItem::on_uninstall_button(wxCommandEvent& event)
{
    if (m_on_uninstall_click) {
        m_on_uninstall_click(m_plugin_id);
    }
}

//==============================================================================
// PluginManagerDialog Implementation
//==============================================================================

PluginManagerDialog::PluginManagerDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, _L("Plugin Manager"),
                wxDefaultPosition, wxSize(FromDIP(700), FromDIP(500)),
                wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_refresh_timer(this)
{
    SetBackgroundColour(*wxWHITE);
    
    // Get plugin host instance
    m_plugin_host = Plugin::PluginHost::instance();
    
    create_ui();
    
    // Bind timer for auto-refresh
    Bind(wxEVT_TIMER, &PluginManagerDialog::on_refresh_timer, this);
    m_refresh_timer.Start(5000); // Refresh every 5 seconds
    
    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

PluginManagerDialog::~PluginManagerDialog()
{
    m_refresh_timer.Stop();
}

void PluginManagerDialog::create_ui()
{
    auto* main_sizer = new wxBoxSizer(wxVERTICAL);
    
    // Title
    auto* title = new wxStaticText(this, wxID_ANY, _L("Plugin Manager"));
    title->SetFont(Label::Head_16);
    main_sizer->Add(title, 0, wxALL, FromDIP(15));
    
    // Separator
    main_sizer->Add(new StaticLine(this, wxLI_HORIZONTAL), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(15));
    
    // Notebook for tabs
    m_notebook = new wxNotebook(this, wxID_ANY);
    
    // Create pages
    auto* installed_page = new wxPanel(m_notebook);
    create_installed_page(installed_page);
    m_notebook->AddPage(installed_page, _L("Installed"));
    
    auto* available_page = new wxPanel(m_notebook);
    create_available_page(available_page);
    m_notebook->AddPage(available_page, _L("Available"));
    
    auto* settings_page = new wxPanel(m_notebook);
    create_settings_page(settings_page);
    m_notebook->AddPage(settings_page, _L("Settings"));
    
    main_sizer->Add(m_notebook, 1, wxEXPAND | wxALL, FromDIP(15));
    
    // Bottom buttons
    auto* btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    
    m_install_btn = new Button(this, _L("Install from File..."));
    m_install_btn->Bind(wxEVT_BUTTON, &PluginManagerDialog::on_install_from_file, this);
    btn_sizer->Add(m_install_btn, 0, wxRIGHT, FromDIP(10));
    
    m_refresh_btn = new Button(this, _L("Refresh"));
    m_refresh_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { refresh_plugins(); });
    btn_sizer->Add(m_refresh_btn, 0, wxRIGHT, FromDIP(10));
    
    btn_sizer->AddStretchSpacer();
    
    m_close_btn = new Button(this, _L("Close"));
    m_close_btn->Bind(wxEVT_BUTTON, &PluginManagerDialog::on_close, this);
    btn_sizer->Add(m_close_btn, 0);
    
    main_sizer->Add(btn_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(15));
    
    SetSizer(main_sizer);
    
    // Initial populate
    refresh_plugins();
}

void PluginManagerDialog::create_installed_page(wxWindow* parent)
{
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    
    // Info text
    auto* info = new wxStaticText(parent, wxID_ANY, 
        _L("Installed plugins are loaded when OrcaSlicer starts. Enable or disable plugins below."));
    info->SetForegroundColour(wxColour(100, 100, 100));
    sizer->Add(info, 0, wxALL, FromDIP(10));
    
    // Scrolled window for plugin list
    m_installed_panel = new wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                              wxVSCROLL | wxBORDER_NONE);
    m_installed_panel->SetScrollRate(0, FromDIP(20));
    m_installed_panel->SetBackgroundColour(wxColour(245, 245, 245));
    
    m_installed_sizer = new wxBoxSizer(wxVERTICAL);
    m_installed_panel->SetSizer(m_installed_sizer);
    
    sizer->Add(m_installed_panel, 1, wxEXPAND | wxALL, FromDIP(5));
    
    parent->SetSizer(sizer);
}

void PluginManagerDialog::create_available_page(wxWindow* parent)
{
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    
    // Coming soon notice
    auto* info = new wxStaticText(parent, wxID_ANY,
        _L("Online plugin repository coming soon.\n\nFor now, install plugins from ZIP files using the 'Install from File...' button."));
    info->SetForegroundColour(wxColour(100, 100, 100));
    sizer->Add(info, 0, wxALL | wxALIGN_CENTER, FromDIP(20));
    
    // Placeholder for future online plugin browser
    m_available_panel = new wxScrolledWindow(parent, wxID_ANY);
    m_available_panel->SetScrollRate(0, FromDIP(20));
    
    m_available_sizer = new wxBoxSizer(wxVERTICAL);
    m_available_panel->SetSizer(m_available_sizer);
    
    sizer->Add(m_available_panel, 1, wxEXPAND);
    
    parent->SetSizer(sizer);
}

void PluginManagerDialog::create_settings_page(wxWindow* parent)
{
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    
    // Plugin directories info
    auto* dirs_title = new wxStaticText(parent, wxID_ANY, _L("Plugin Directories"));
    dirs_title->SetFont(Label::Head_14);
    sizer->Add(dirs_title, 0, wxALL, FromDIP(10));
    
    // User plugins directory
    std::string user_plugins_dir;
#ifdef _WIN32
    user_plugins_dir = (fs::path(wxStandardPaths::Get().GetUserDataDir().ToStdString()) / "plugins").string();
#elif __APPLE__
    user_plugins_dir = (fs::path(wxStandardPaths::Get().GetUserDataDir().ToStdString()) / "plugins").string();
#else
    user_plugins_dir = (fs::path(wxStandardPaths::Get().GetUserConfigDir().ToStdString()) / "OrcaSlicer" / "plugins").string();
#endif
    
    auto* user_dir_label = new wxStaticText(parent, wxID_ANY, 
        wxString::Format(_L("User plugins: %s"), wxString::FromUTF8(user_plugins_dir)));
    user_dir_label->SetForegroundColour(wxColour(100, 100, 100));
    sizer->Add(user_dir_label, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
    
    // Built-in plugins directory
    std::string builtin_dir = (fs::path(resources_dir()) / "plugins").string();
    auto* builtin_dir_label = new wxStaticText(parent, wxID_ANY,
        wxString::Format(_L("Built-in plugins: %s"), wxString::FromUTF8(builtin_dir)));
    builtin_dir_label->SetForegroundColour(wxColour(100, 100, 100));
    sizer->Add(builtin_dir_label, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
    
    sizer->Add(new StaticLine(parent, wxLI_HORIZONTAL), 0, wxEXPAND | wxALL, FromDIP(10));
    
    // Node.js status
    auto* node_title = new wxStaticText(parent, wxID_ANY, _L("JavaScript Runtime"));
    node_title->SetFont(Label::Head_14);
    sizer->Add(node_title, 0, wxALL, FromDIP(10));
    
    bool node_available = Plugin::NodePluginRuntime::is_node_installed();
    wxString node_status = node_available ? 
        _L("Node.js is installed and available for JavaScript plugins.") :
        _L("Node.js is not installed. JavaScript plugins will not work.\nInstall Node.js from https://nodejs.org/");
    
    auto* node_status_label = new wxStaticText(parent, wxID_ANY, node_status);
    node_status_label->SetForegroundColour(node_available ? wxColour(50, 150, 50) : wxColour(200, 50, 50));
    sizer->Add(node_status_label, 0, wxLEFT | wxRIGHT, FromDIP(10));
    
    if (node_available) {
        auto node_path = Plugin::NodePluginRuntime::find_node_executable();
        auto* node_path_label = new wxStaticText(parent, wxID_ANY,
            wxString::Format(_L("Path: %s"), wxString::FromUTF8(node_path.string())));
        node_path_label->SetForegroundColour(wxColour(100, 100, 100));
        sizer->Add(node_path_label, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(10));
    }
    
    parent->SetSizer(sizer);
}

void PluginManagerDialog::refresh_plugins()
{
    if (!m_plugin_host) return;
    
    // Refresh from host
    m_plugin_host->discover_plugins();
    
    populate_installed_list();
    populate_available_list();
}

void PluginManagerDialog::populate_installed_list()
{
    if (!m_installed_panel || !m_installed_sizer) return;
    
    wxWindowUpdateLocker lock(m_installed_panel);
    
    // Clear existing items
    m_installed_sizer->Clear(true);
    m_plugin_items.clear();
    
    if (!m_plugin_host) {
        auto* empty_label = new wxStaticText(m_installed_panel, wxID_ANY, 
            _L("No plugins installed"));
        empty_label->SetForegroundColour(wxColour(150, 150, 150));
        m_installed_sizer->Add(empty_label, 0, wxALL | wxALIGN_CENTER, FromDIP(20));
        m_installed_panel->Layout();
        return;
    }
    
    const auto& plugins = m_plugin_host->get_plugins();
    
    if (plugins.empty()) {
        auto* empty_label = new wxStaticText(m_installed_panel, wxID_ANY,
            _L("No plugins installed.\n\nUse 'Install from File...' to add plugins,\nor place plugin folders in the user plugins directory."));
        empty_label->SetForegroundColour(wxColour(150, 150, 150));
        m_installed_sizer->Add(empty_label, 0, wxALL | wxALIGN_CENTER, FromDIP(20));
    } else {
        for (const auto& [id, info] : plugins) {
            auto* item = new PluginListItem(m_installed_panel, info,
                [this](const std::string& id, bool enabled) { on_plugin_enable_change(id, enabled); },
                [this](const std::string& id) { on_plugin_settings(id); },
                [this](const std::string& id) { on_plugin_uninstall(id); }
            );
            
            m_installed_sizer->Add(item, 0, wxEXPAND | wxALL, FromDIP(5));
            m_plugin_items.push_back(item);
        }
    }
    
    m_installed_panel->FitInside();
    m_installed_panel->Layout();
}

void PluginManagerDialog::populate_available_list()
{
    // Future: populate from online repository
}

void PluginManagerDialog::on_install_from_file(wxCommandEvent& event)
{
    wxFileDialog dialog(this,
                        _L("Select Plugin to Install"),
                        wxEmptyString,
                        wxEmptyString,
                        _L("Plugin archives (*.zip)|*.zip|All files (*.*)|*.*"),
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }
    
    wxString path = dialog.GetPath();
    
    // TODO: Implement ZIP extraction and plugin installation
    // For now, show a message that manual installation is needed
    
    MessageDialog msg(this,
        _L("ZIP installation is not yet implemented.\n\nTo install a plugin manually:\n1. Extract the ZIP file\n2. Copy the plugin folder to the user plugins directory\n3. Restart OrcaSlicer"),
        _L("Plugin Installation"),
        wxOK | wxICON_INFORMATION);
    msg.ShowModal();
}

void PluginManagerDialog::on_plugin_enable_change(const std::string& plugin_id, bool enabled)
{
    if (!m_plugin_host) return;
    
    if (enabled) {
        m_plugin_host->enable_plugin(plugin_id);
    } else {
        m_plugin_host->disable_plugin(plugin_id);
    }
    
    // Update UI
    for (auto* item : m_plugin_items) {
        if (item->plugin_id() == plugin_id) {
            const auto& plugins = m_plugin_host->get_plugins();
            auto it = plugins.find(plugin_id);
            if (it != plugins.end()) {
                item->update_state(it->second.enabled, it->second.state == Plugin::PluginState::Error);
            }
            break;
        }
    }
}

void PluginManagerDialog::on_plugin_settings(const std::string& plugin_id)
{
    // Open plugin-specific settings dialog
    // For now, just show a message
    MessageDialog msg(this,
        wxString::Format(_L("Settings for plugin '%s' are not available yet."), wxString::FromUTF8(plugin_id)),
        _L("Plugin Settings"),
        wxOK | wxICON_INFORMATION);
    msg.ShowModal();
}

void PluginManagerDialog::on_plugin_uninstall(const std::string& plugin_id)
{
    MessageDialog confirm(this,
        wxString::Format(_L("Are you sure you want to uninstall the plugin '%s'?"), wxString::FromUTF8(plugin_id)),
        _L("Confirm Uninstall"),
        wxYES_NO | wxICON_QUESTION);
    
    if (confirm.ShowModal() != wxID_YES) {
        return;
    }
    
    if (m_plugin_host) {
        m_plugin_host->uninstall_plugin(plugin_id);
        refresh_plugins();
    }
}

void PluginManagerDialog::on_close(wxCommandEvent& event)
{
    EndModal(wxID_OK);
}

void PluginManagerDialog::on_refresh_timer(wxTimerEvent& event)
{
    // Auto-refresh plugin states (not the full discovery)
    // This updates error states and enabled/disabled status
    for (auto* item : m_plugin_items) {
        if (m_plugin_host) {
            const auto& plugins = m_plugin_host->get_plugins();
            auto it = plugins.find(item->plugin_id());
            if (it != plugins.end()) {
                item->update_state(it->second.enabled, it->second.state == Plugin::PluginState::Error);
            }
        }
    }
}

void PluginManagerDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    // Update sizes for DPI change
    SetMinSize(wxSize(FromDIP(700), FromDIP(500)));
    
    Refresh();
    Layout();
}

//==============================================================================
// PluginSettingsPanel Implementation
//==============================================================================

PluginSettingsPanel::PluginSettingsPanel(wxWindow* parent,
                                         const std::string& plugin_id,
                                         Plugin::PluginHost* host)
    : wxPanel(parent)
    , m_plugin_id(plugin_id)
    , m_host(host)
{
    create_ui();
    refresh_settings();
}

void PluginSettingsPanel::create_ui()
{
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    
    // This will be populated based on plugin's config schema
    auto* placeholder = new wxStaticText(this, wxID_ANY,
        _L("Plugin settings will be displayed here based on the plugin's configuration schema."));
    placeholder->SetForegroundColour(wxColour(150, 150, 150));
    sizer->Add(placeholder, 0, wxALL, FromDIP(10));
    
    SetSizer(sizer);
}

void PluginSettingsPanel::refresh_settings()
{
    // TODO: Query plugin for its settings and populate controls
}

void PluginSettingsPanel::apply_settings()
{
    // TODO: Apply changed settings to plugin
}

void PluginSettingsPanel::on_setting_changed(wxCommandEvent& event)
{
    // Mark settings as dirty
}

} // namespace GUI
} // namespace Slic3r

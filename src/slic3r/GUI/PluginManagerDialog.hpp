// OrcaSlicer Plugin Manager Dialog
// Copyright (C) 2024 OrcaSlicer Contributors
// License: AGPL-3.0

#ifndef slic3r_PluginManagerDialog_hpp_
#define slic3r_PluginManagerDialog_hpp_

#include "GUI_Utils.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ScrolledWindow.hpp"

#include <wx/dialog.h>
#include <wx/timer.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>

#include <memory>
#include <vector>
#include <string>

namespace Slic3r {

namespace Plugin {
class PluginHost;
class PluginInfo;
}

namespace GUI {

//==============================================================================
// Plugin List Item Widget
//==============================================================================

class PluginListItem : public wxPanel {
public:
    PluginListItem(wxWindow* parent, 
                   const Plugin::PluginInfo& info,
                   std::function<void(const std::string&, bool)> on_enable_change,
                   std::function<void(const std::string&)> on_settings_click,
                   std::function<void(const std::string&)> on_uninstall_click);
    
    void update_state(bool enabled, bool has_error);
    const std::string& plugin_id() const { return m_plugin_id; }
    
private:
    void on_paint(wxPaintEvent& event);
    void on_enable_toggle(wxCommandEvent& event);
    void on_settings_button(wxCommandEvent& event);
    void on_uninstall_button(wxCommandEvent& event);
    
    std::string m_plugin_id;
    std::string m_plugin_name;
    std::string m_plugin_version;
    std::string m_plugin_description;
    
    ::CheckBox* m_enable_checkbox = nullptr;
    Button* m_settings_btn = nullptr;
    Button* m_uninstall_btn = nullptr;
    wxStaticText* m_name_label = nullptr;
    wxStaticText* m_version_label = nullptr;
    wxStaticText* m_description_label = nullptr;
    wxStaticText* m_error_label = nullptr;
    
    std::function<void(const std::string&, bool)> m_on_enable_change;
    std::function<void(const std::string&)> m_on_settings_click;
    std::function<void(const std::string&)> m_on_uninstall_click;
};

//==============================================================================
// Plugin Manager Dialog
//==============================================================================

class PluginManagerDialog : public DPIDialog {
public:
    PluginManagerDialog(wxWindow* parent);
    ~PluginManagerDialog();
    
    void refresh_plugins();
    
protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
    
private:
    void create_ui();
    void create_installed_page(wxWindow* parent);
    void create_available_page(wxWindow* parent);
    void create_settings_page(wxWindow* parent);
    
    void populate_installed_list();
    void populate_available_list();
    
    void on_install_from_file(wxCommandEvent& event);
    void on_plugin_enable_change(const std::string& plugin_id, bool enabled);
    void on_plugin_settings(const std::string& plugin_id);
    void on_plugin_uninstall(const std::string& plugin_id);
    void on_close(wxCommandEvent& event);
    
    // Timer for auto-refresh
    void on_refresh_timer(wxTimerEvent& event);
    
    wxNotebook* m_notebook = nullptr;
    wxScrolledWindow* m_installed_panel = nullptr;
    wxScrolledWindow* m_available_panel = nullptr;
    wxPanel* m_settings_panel = nullptr;
    
    wxBoxSizer* m_installed_sizer = nullptr;
    wxBoxSizer* m_available_sizer = nullptr;
    
    std::vector<PluginListItem*> m_plugin_items;
    
    Button* m_install_btn = nullptr;
    Button* m_refresh_btn = nullptr;
    Button* m_close_btn = nullptr;
    
    wxTimer m_refresh_timer;
    
    Plugin::PluginHost* m_plugin_host = nullptr;
};

//==============================================================================
// Plugin Settings Panel - For individual plugin configuration
//==============================================================================

class PluginSettingsPanel : public wxPanel {
public:
    PluginSettingsPanel(wxWindow* parent, 
                        const std::string& plugin_id,
                        Plugin::PluginHost* host);
    
    void refresh_settings();
    void apply_settings();
    
private:
    void create_ui();
    void on_setting_changed(wxCommandEvent& event);
    
    std::string m_plugin_id;
    Plugin::PluginHost* m_host;
    
    std::map<std::string, wxWindow*> m_setting_controls;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_PluginManagerDialog_hpp_

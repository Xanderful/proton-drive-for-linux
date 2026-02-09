#include "app_window.hpp"
#include "app_window_helpers.hpp"
#include "logger.hpp"
#include "sync_manager.hpp"
#include "file_index.hpp"
#include "device_identity.hpp"
#include "sync_job_metadata.hpp"
#include "notifications.hpp"
#include "settings.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <array>
#include <memory>
#include <thread>
#include <ctime>
#include <regex>
#include <cctype>
#include <cstdio>
#include <set>
#include <unistd.h>
#include <limits.h>

namespace fs = std::filesystem;

// Import helper functions from namespace for cleaner code
using AppWindowHelpers::get_rclone_path;
using AppWindowHelpers::exec_rclone;
using AppWindowHelpers::run_rclone;
using AppWindowHelpers::exec_command;
using AppWindowHelpers::run_system;
using AppWindowHelpers::get_sync_status_for_path;
using AppWindowHelpers::has_rclone_profile;
using AppWindowHelpers::run_rclone_config_create;
using AppWindowHelpers::format_file_size;
using AppWindowHelpers::safe_exists;
using AppWindowHelpers::safe_is_directory;

// CSS styling for the drop zone and modern UI (GTK4 compatible)
static const char* APP_CSS = R"(
/* Drop zone styling */
.drop-zone {
    background: linear-gradient(135deg, #2d2d2d 0%, #1a1a1a 100%);
    border: 3px dashed alpha(#6d4aff, 0.5);
    border-radius: 20px;
    min-height: 300px;
    min-width: 400px;
    transition: all 200ms ease-in-out;
}

.drop-zone:hover {
    border-color: #6d4aff;
    background: linear-gradient(135deg, #3d3d3d 0%, #2a2a2a 100%);
}

.drop-zone-active {
    border-color: #6d4aff;
    border-style: solid;
    border-width: 4px;
    background: linear-gradient(135deg, #4d3d6d 0%, #3a2a5a 100%);
}

.drop-zone-label {
    font-size: 24px;
    font-weight: bold;
    color: #888;
}

.drop-zone-sublabel {
    font-size: 14px;
    color: #666;
}

.drop-zone-icon {
    color: #555;
}

/* Progress overlay */
.progress-overlay {
    background: alpha(#1e1e1e, 0.95);
    border-radius: 12px;
    padding: 15px;
    border: 1px solid #444;
    min-width: 280px;
}

.progress-file {
    font-size: 12px;
    color: #aaa;
    font-family: monospace;
}

.progress-speed {
    font-size: 11px;
    color: #6d4aff;
    font-weight: bold;
}

.progress-title {
    font-size: 13px;
    font-weight: bold;
    color: #fff;
}

/* Logs panel */
.logs-panel {
    background: #1a1a1a;
    border-top: 1px solid #333;
}

.logs-header {
    background: #252525;
    padding: 5px 10px;
}

.logs-text {
    font-family: monospace;
    font-size: 11px;
    background: #1a1a1a;
    color: #ccc;
}

/* Sync activity panel */
.sync-activity-panel {
    background: linear-gradient(to bottom, #252530 0%, #1e1e28 100%);
    border: 1px solid #444;
    border-radius: 12px;
    margin: 10px;
    padding: 12px;
}

.sync-activity-header {
    font-weight: bold;
    font-size: 13px;
    color: #fff;
    margin-bottom: 8px;
}

.sync-activity-item {
    background: #2a2a35;
    border-radius: 8px;
    padding: 10px;
    margin: 4px 0;
}

.sync-file-name {
    font-weight: bold;
    font-size: 12px;
    color: #fff;
}

.sync-file-path {
    font-size: 11px;
    color: #888;
    font-family: monospace;
}

.sync-file-status {
    font-size: 11px;
    color: #6d4aff;
}

.sync-file-speed {
    font-size: 11px;
    color: #4caf50;
    font-weight: bold;
}

.no-sync-label {
    font-size: 12px;
    color: #666;
    font-style: italic;
}

/* File browser */
.file-browser {
    background: #1e1e24;
}

.path-bar {
    background: #252530;
    padding: 8px 12px;
    border-bottom: 1px solid #333;
}

.path-bar button {
    background: transparent;
    border: none;
    padding: 4px 8px;
    border-radius: 4px;
}

.path-bar button:hover {
    background: alpha(#fff, 0.1);
}

.file-list {
    background: #1a1a20;
}

.file-row {
    padding: 8px 12px;
    border-bottom: 1px solid #2a2a30;
}

.file-row:hover {
    background: alpha(#6d4aff, 0.15);
}

.file-name {
    font-size: 13px;
    font-weight: 500;
    color: #fff;
}

.file-size {
    font-size: 11px;
    color: #888;
}

.file-date {
    font-size: 11px;
    color: #666;
}

.sync-badge {
    font-size: 10px;
    padding: 2px 6px;
    border-radius: 4px;
    font-weight: bold;
}

.sync-badge-synced {
    background: #2e7d32;
    color: #fff;
}

.sync-badge-cloud {
    background: #1565c0;
    color: #fff;
}

.sync-badge-pending {
    background: #f57c00;
    color: #fff;
}

.sync-badge-local {
    background: #f57c00;
    color: #fff;
}

.sync-badge-syncing {
    background: #6d4aff;
    color: #fff;
}

.sync-note {
    font-size: 11px;
    color: #ffb74d;
    font-style: italic;
}

.sync-progress-detail {
    font-size: 11px;
    color: #81c784;
    font-weight: 500;
}

.browser-header {
    background: #252530;
    padding: 6px 12px;
    border-bottom: 1px solid #444;
}

.browser-title {
    font-weight: bold;
    font-size: 12px;
    color: #aaa;
}

/* Hamburger menu */
.menu-section {
    padding: 10px;
    border-bottom: 1px solid #333;
}

.menu-section:last-child {
    border-bottom: none;
}

.menu-section-title {
    font-size: 11px;
    font-weight: bold;
    color: #888;
    letter-spacing: 1px;
}

.service-status-active {
    color: #4caf50;
    font-weight: bold;
}

.service-status-inactive {
    color: #ff9800;
    font-weight: bold;
}

/* Device cards */
.device-card {
    background: #252530;
    border-radius: 8px;
    padding: 12px;
    border: 1px solid #333;
}

.device-card:hover {
    background: #2a2a35;
    border-color: #444;
}

.device-name {
    font-size: 14px;
    font-weight: bold;
    color: #fff;
}

.device-id {
    font-size: 11px;
    color: #888;
    font-family: monospace;
}

.device-status {
    font-size: 12px;
    color: #aaa;
}

/* Menu item rows */
.menu-item-row {
    padding: 6px 8px;
    border-radius: 6px;
}

.menu-item-row:hover {
    background: alpha(#fff, 0.1);
}

/* Browse button */
.browse-button {
    background: #6d4aff;
    color: white;
    border-radius: 8px;
    padding: 12px 24px;
    font-weight: bold;
    font-size: 14px;
}

.browse-button:hover {
    background: #8b6aff;
}

/* Header styling */
.title-label {
    font-weight: bold;
    font-size: 16px;
}

/* Flat button styling */
.flat-button {
    background: transparent;
    border: none;
}

.flat-button:hover {
    background: alpha(#fff, 0.1);
}

/* Sidebar Navigation (QSYNC-style) */
.sidebar {
    background: linear-gradient(to bottom, #1a4a7a 0%, #0d2840 100%);
}

/* Collapsed sidebar state - we handle size change in code */
.sidebar.collapsed .sidebar-header {
    padding: 12px 8px;
}

.sidebar.collapsed .nav-item {
    padding: 12px 8px;
}

.sidebar.collapsed .nav-item-icon {
    margin-right: 0;
    margin-left: 0;
}

.sidebar-header {
    padding: 20px 16px;
    border-bottom: 1px solid alpha(#fff, 0.1);
}

.sidebar-title {
    font-size: 22px;
    font-weight: bold;
    color: #fff;
}

.sidebar-subtitle {
    font-size: 12px;
    color: alpha(#fff, 0.6);
}

/* Boxed list for cards */
.boxed-list {
    background: transparent;
}

.boxed-list > row {
    background: transparent;
    padding: 0;
}

.nav-item {
    padding: 12px 16px;
    border-radius: 0;
    background: transparent;
    border: none;
    border-left: 3px solid transparent;
    color: #fff;
}

.nav-item:hover {
    background: alpha(#fff, 0.1);
}

.nav-item.active,
.nav-item:checked {
    background: alpha(#fff, 0.15);
    border-left-color: #6d4aff;
}

.nav-item-icon {
    margin-right: 12px;
    color: alpha(#fff, 0.8);
}

.nav-item-label {
    font-size: 14px;
    color: #fff;
}

.nav-section-title {
    padding: 16px 16px 8px 16px;
    font-size: 11px;
    font-weight: bold;
    color: alpha(#fff, 0.5);
    letter-spacing: 1px;
}

/* Content area header */
.content-header {
    background: #252530;
    padding: 16px 24px;
    border-bottom: 1px solid #444;
}

.content-title {
    font-size: 24px;
    font-weight: bold;
    color: #fff;
}

.content-subtitle {
    font-size: 13px;
    color: #888;
    margin-top: 4px;
}

/* Data table styling */
.data-table-header {
    background: #2a2a35;
    padding: 12px 16px;
    border-bottom: 1px solid #444;
}

.data-table-header label {
    font-size: 12px;
    font-weight: bold;
    color: #888;
}

.data-table-row {
    padding: 12px 16px;
    border-bottom: 1px solid #333;
    background: #1e1e28;
}

.data-table-row:hover {
    background: alpha(#6d4aff, 0.1);
}

/* Action buttons in table */
.action-button {
    padding: 4px 8px;
    background: transparent;
    border-radius: 4px;
}

.action-button:hover {
    background: alpha(#fff, 0.1);
}

/* Search box styling */
.search-entry {
    background: #2a2a35;
    border: 1px solid #444;
    border-radius: 6px;
    padding: 8px 12px;
    color: #fff;
}

.search-entry:focus {
    border-color: #6d4aff;
}

/* Status indicators */
.status-online {
    color: #4caf50;
}

.status-offline {
    color: #888;
}

/* Setup notification banner */
.warning-banner {
    background: linear-gradient(to right, alpha(#ff6b6b, 0.15), alpha(#f57c00, 0.15));
    border: 1px solid #ff9800;
    border-radius: 8px;
    padding: 12px 16px;
}

.setup-icon {
    color: #ff9800;
    min-width: 24px;
}

.setup-title {
    font-size: 14px;
    font-weight: bold;
    color: #ffb74d;
}

.setup-text {
    font-size: 13px;
    color: #bbb;
}

/* Sync job card styling */
.sync-job-card {
    background: #252530;
    border: 1px solid #3a3a45;
    border-radius: 10px;
    padding: 12px 16px;
    margin: 4px 0;
}

.sync-job-card:hover {
    background: #2d2d3a;
    border-color: #4a4a58;
}

.sync-job-title {
    font-size: 15px;
    font-weight: 600;
    color: #fff;
}

.sync-job-path {
    font-size: 12px;
    color: #888;
    font-family: monospace;
}

.sync-icon {
    color: #6d9eeb;
}

.sync-type-badge {
    font-size: 11px;
    font-weight: 500;
    padding: 3px 10px;
    border-radius: 12px;
    background: #333;
}

.sync-type-twoway {
    background: #1e4620;
    color: #81c784;
}

.sync-type-upload {
    background: #1a3a5c;
    color: #64b5f6;
}

/* Legacy sync-job-row (keep for compatibility) */
.sync-job-row {
    background: #252530;
    border: 1px solid #333;
    border-radius: 8px;
    padding: 12px;
    margin: 4px 0;
}

.sync-job-row:hover {
    background: #2a2a35;
    border-color: #444;
}

.caption {
    font-size: 11px;
}

/* Transfer popup styles */
.transfer-popup {
    background: #1e1e28;
    border: 1px solid #3a3a45;
    border-radius: 12px;
    box-shadow: 0 8px 32px rgba(0, 0, 0, 0.4);
}

.transfer-popup-header {
    border-bottom: 1px solid #333;
}

.transfer-popup-title {
    font-size: 13px;
    font-weight: 600;
    color: #fff;
}

.transfer-progress {
    border-radius: 4px;
}

.transfer-progress progress {
    background: #6d4aff;
    border-radius: 4px;
}

.transfer-progress trough {
    background: #333;
    border-radius: 4px;
    min-height: 6px;
}

.transfer-item {
    background: #252530;
    border-radius: 6px;
    padding: 8px;
    margin: 2px 0;
}

.transfer-item:hover {
    background: #2a2a35;
}

.pill {
    font-size: 11px;
    padding: 4px 12px;
    border-radius: 16px;
    min-height: 24px;
}

.sync-badge-error {
    background: #c62828;
    color: #fff;
}

/* In-app toast banner */
.toast-banner {
    background: linear-gradient(135deg, #2d2d6b, #3d2d7b);
    border: 1px solid #6d4aff;
    border-radius: 8px;
    padding: 8px 14px;
    color: #e0e0ff;
    font-size: 13px;
}

/* Sync activity progress bar */
.sync-activity-progress {
    border-radius: 3px;
    min-height: 4px;
    margin-top: 4px;
}

.sync-activity-progress progress {
    background: #6d4aff;
    border-radius: 3px;
    min-height: 4px;
}

.sync-activity-progress trough {
    background: #333;
    border-radius: 3px;
    min-height: 4px;
}

.sync-progress-detail {
    font-size: 11px;
    color: #8888aa;
    font-family: monospace;
}

.sync-note {
    font-size: 11px;
    color: #ffb74d;
}
)";  

AppWindow& AppWindow::getInstance() {
    static AppWindow instance;
    return instance;
}

bool AppWindow::initialize() {
    if (window_) {
        return true; // Already initialized
    }
    
    // Apply CSS styling
    GtkCssProvider* css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css_provider, APP_CSS, -1);  // Compatible with GTK 4.6+
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(css_provider);
    
    build_window();
    
    // Start status polling
    g_timeout_add(2000, [](gpointer data) -> gboolean {
        auto* self = static_cast<AppWindow*>(data);
        self->poll_status();
        return G_SOURCE_CONTINUE;
    }, this);
    
    // Start sync activity polling (every 1 second for responsive UI)
    g_timeout_add(1000, [](gpointer data) -> gboolean {
        auto* self = static_cast<AppWindow*>(data);
        self->poll_sync_activity();
        return G_SOURCE_CONTINUE;
    }, this);
    
    // Add window-level drop target so drag-drop works on ANY page
    GtkDropTarget* window_drop_target = gtk_drop_target_new(G_TYPE_INVALID, GDK_ACTION_COPY);
    GType win_types[] = { GDK_TYPE_FILE_LIST };
    gtk_drop_target_set_gtypes(window_drop_target, win_types, 1);
    
    g_signal_connect(window_drop_target, "enter", G_CALLBACK(+[](GtkDropTarget*, gdouble, gdouble, gpointer data) -> GdkDragAction {
        auto* self = static_cast<AppWindow*>(data);
        self->set_drop_zone_highlight(true);
        return GDK_ACTION_COPY;
    }), this);
    
    g_signal_connect(window_drop_target, "leave", G_CALLBACK(+[](GtkDropTarget*, gpointer data) {
        auto* self = static_cast<AppWindow*>(data);
        self->set_drop_zone_highlight(false);
    }), this);
    
    g_signal_connect(window_drop_target, "drop", G_CALLBACK(+[](GtkDropTarget*, const GValue* value, gdouble, gdouble, gpointer data) -> gboolean {
        auto* self = static_cast<AppWindow*>(data);
        self->set_drop_zone_highlight(false);
        
        if (!G_VALUE_HOLDS(value, GDK_TYPE_FILE_LIST)) {
            return FALSE;
        }
        
        GSList* files = static_cast<GSList*>(g_value_get_boxed(value));
        std::vector<std::string> paths;
        
        for (GSList* l = files; l != nullptr; l = l->next) {
            GFile* file = G_FILE(l->data);
            char* path = g_file_get_path(file);
            if (path) {
                paths.push_back(path);
                g_free(path);
            }
        }
        
        if (!paths.empty()) {
            self->handle_dropped_files(paths);
        }
        
        return TRUE;
    }), this);
    
    gtk_widget_add_controller(window_, GTK_EVENT_CONTROLLER(window_drop_target));
    
    Logger::info("[AppWindow] Initialized with GTK4");
    return true;
}

void AppWindow::build_window() {
    // Create main window
    window_ = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window_), "Proton Drive");
    gtk_window_set_default_size(GTK_WINDOW(window_), 1200, 750);
    
    // Connect close handler - hide to tray or actually quit based on setting
    g_signal_connect(window_, "close-request", G_CALLBACK(+[](GtkWindow* /*win*/, gpointer data) -> gboolean {
        auto* self = static_cast<AppWindow*>(data);
        auto& settings = proton::SettingsManager::getInstance();
        if (settings.get_minimize_to_tray()) {
            self->hide();
            return TRUE; // Prevent destruction, keep running in tray
        } else {
            // Actually quit the application
            GApplication* app = g_application_get_default();
            if (app) {
                g_application_quit(app);
            }
            return FALSE; // Allow destruction
        }
    }), this);
    
    // Main horizontal box: sidebar | content
    GtkWidget* main_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_window_set_child(GTK_WINDOW(window_), main_hbox);
    
    // ===== BUILD SIDEBAR =====
    sidebar_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(sidebar_, "sidebar");
    gtk_widget_set_size_request(sidebar_, 220, -1);
    
    // Sidebar header with app name
    GtkWidget* sidebar_header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(sidebar_header, "sidebar-header");
    
    GtkWidget* app_title = gtk_label_new("Proton Drive");
    gtk_widget_add_css_class(app_title, "sidebar-title");
    gtk_label_set_xalign(GTK_LABEL(app_title), 0);
    gtk_box_append(GTK_BOX(sidebar_header), app_title);
    sidebar_text_widgets_.push_back(app_title);
    
    GtkWidget* app_subtitle = gtk_label_new("Backup Management");
    gtk_widget_add_css_class(app_subtitle, "sidebar-subtitle");
    gtk_label_set_xalign(GTK_LABEL(app_subtitle), 0);
    gtk_box_append(GTK_BOX(sidebar_header), app_subtitle);
    sidebar_text_widgets_.push_back(app_subtitle);
    
    gtk_box_append(GTK_BOX(sidebar_), sidebar_header);
    
    // Navigation items
    GtkWidget* nav_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(nav_box, TRUE);
    
    // Store all nav buttons for radio-like behavior
    std::vector<GtkWidget*>* nav_buttons = new std::vector<GtkWidget*>();
    
    // Create navigation buttons with radio-like behavior
    auto create_nav_item = [nav_buttons, this](const char* icon_name, const char* label_text, 
                                    bool active = false) -> GtkWidget* {
        GtkWidget* btn = gtk_toggle_button_new();
        gtk_widget_add_css_class(btn, "nav-item");
        if (active) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), TRUE);
        
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        
        GtkWidget* icon = gtk_image_new_from_icon_name(icon_name);
        gtk_widget_add_css_class(icon, "nav-item-icon");
        gtk_widget_set_margin_end(icon, 12);
        gtk_box_append(GTK_BOX(box), icon);
        
        GtkWidget* label = gtk_label_new(label_text);
        gtk_widget_add_css_class(label, "nav-item-label");
        gtk_box_append(GTK_BOX(box), label);
        sidebar_text_widgets_.push_back(label);
        
        gtk_button_set_child(GTK_BUTTON(btn), box);
        nav_buttons->push_back(btn);
        return btn;
    };
    
    // Overview (default active)
    GtkWidget* nav_overview = create_nav_item("go-home-symbolic", "Overview", true);
    g_object_set_data(G_OBJECT(nav_overview), "nav_buttons", nav_buttons);
    g_signal_connect(nav_overview, "toggled", G_CALLBACK(+[](GtkToggleButton* btn, gpointer data) {
        if (gtk_toggle_button_get_active(btn)) {
            auto* self = static_cast<AppWindow*>(data);
            gtk_stack_set_visible_child_name(GTK_STACK(self->main_stack_), "overview");
            // Untoggle others
            auto* buttons = static_cast<std::vector<GtkWidget*>*>(g_object_get_data(G_OBJECT(btn), "nav_buttons"));
            for (GtkWidget* other : *buttons) {
                if (GTK_WIDGET(btn) != other) {
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(other), FALSE);
                }
            }
        }
    }), this);
    gtk_box_append(GTK_BOX(nav_box), nav_overview);
    
    // Sync Jobs
    GtkWidget* nav_sync = create_nav_item("emblem-synchronizing-symbolic", "Sync Jobs", false);
    g_object_set_data(G_OBJECT(nav_sync), "nav_buttons", nav_buttons);
    g_signal_connect(nav_sync, "toggled", G_CALLBACK(+[](GtkToggleButton* btn, gpointer data) {
        if (gtk_toggle_button_get_active(btn)) {
            auto* self = static_cast<AppWindow*>(data);
            gtk_stack_set_visible_child_name(GTK_STACK(self->main_stack_), "sync-jobs");
            self->refresh_sync_jobs();  // Refresh jobs list when tab is selected
            auto* buttons = static_cast<std::vector<GtkWidget*>*>(g_object_get_data(G_OBJECT(btn), "nav_buttons"));
            for (GtkWidget* other : *buttons) {
                if (GTK_WIDGET(btn) != other) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(other), FALSE);
            }
        }
    }), this);
    gtk_box_append(GTK_BOX(nav_box), nav_sync);
    
    // Cloud Browser
    GtkWidget* nav_browser = create_nav_item("folder-remote-symbolic", "Cloud Browser", false);
    g_object_set_data(G_OBJECT(nav_browser), "nav_buttons", nav_buttons);
    g_signal_connect(nav_browser, "toggled", G_CALLBACK(+[](GtkToggleButton* btn, gpointer data) {
        if (gtk_toggle_button_get_active(btn)) {
            auto* self = static_cast<AppWindow*>(data);
            gtk_stack_set_visible_child_name(GTK_STACK(self->main_stack_), "browser");
            auto* buttons = static_cast<std::vector<GtkWidget*>*>(g_object_get_data(G_OBJECT(btn), "nav_buttons"));
            for (GtkWidget* other : *buttons) {
                if (GTK_WIDGET(btn) != other) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(other), FALSE);
            }
        }
    }), this);
    gtk_box_append(GTK_BOX(nav_box), nav_browser);
    
    // Devices
    GtkWidget* nav_devices = create_nav_item("computer-symbolic", "Devices", false);
    g_object_set_data(G_OBJECT(nav_devices), "nav_buttons", nav_buttons);
    g_signal_connect(nav_devices, "toggled", G_CALLBACK(+[](GtkToggleButton* btn, gpointer data) {
        if (gtk_toggle_button_get_active(btn)) {
            auto* self = static_cast<AppWindow*>(data);
            gtk_stack_set_visible_child_name(GTK_STACK(self->main_stack_), "devices");
            self->refresh_devices();  // Refresh device list when tab is selected
            auto* buttons = static_cast<std::vector<GtkWidget*>*>(g_object_get_data(G_OBJECT(btn), "nav_buttons"));
            for (GtkWidget* other : *buttons) {
                if (GTK_WIDGET(btn) != other) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(other), FALSE);
            }
        }
    }), this);
    gtk_box_append(GTK_BOX(nav_box), nav_devices);
    
    // Separator
    GtkWidget* nav_sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(nav_sep, 10);
    gtk_widget_set_margin_bottom(nav_sep, 10);
    gtk_widget_set_margin_start(nav_sep, 16);
    gtk_widget_set_margin_end(nav_sep, 16);
    gtk_box_append(GTK_BOX(nav_box), nav_sep);
    
    // Event Logs
    GtkWidget* nav_logs = create_nav_item("utilities-terminal-symbolic", "Event Logs", false);
    g_object_set_data(G_OBJECT(nav_logs), "nav_buttons", nav_buttons);
    g_signal_connect(nav_logs, "toggled", G_CALLBACK(+[](GtkToggleButton* btn, gpointer data) {
        if (gtk_toggle_button_get_active(btn)) {
            auto* self = static_cast<AppWindow*>(data);
            gtk_stack_set_visible_child_name(GTK_STACK(self->main_stack_), "logs");
            auto* buttons = static_cast<std::vector<GtkWidget*>*>(g_object_get_data(G_OBJECT(btn), "nav_buttons"));
            for (GtkWidget* other : *buttons) {
                if (GTK_WIDGET(btn) != other) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(other), FALSE);
            }
        }
    }), this);
    gtk_box_append(GTK_BOX(nav_box), nav_logs);
    
    // Settings
    GtkWidget* nav_settings = create_nav_item("preferences-system-symbolic", "Settings", false);
    g_object_set_data(G_OBJECT(nav_settings), "nav_buttons", nav_buttons);
    g_signal_connect(nav_settings, "toggled", G_CALLBACK(+[](GtkToggleButton* btn, gpointer data) {
        if (gtk_toggle_button_get_active(btn)) {
            auto* self = static_cast<AppWindow*>(data);
            gtk_stack_set_visible_child_name(GTK_STACK(self->main_stack_), "settings");
            auto* buttons = static_cast<std::vector<GtkWidget*>*>(g_object_get_data(G_OBJECT(btn), "nav_buttons"));
            for (GtkWidget* other : *buttons) {
                if (GTK_WIDGET(btn) != other) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(other), FALSE);
            }
        }
    }), this);
    gtk_box_append(GTK_BOX(nav_box), nav_settings);
    
    gtk_box_append(GTK_BOX(sidebar_), nav_box);
    
    // Sidebar collapse button at bottom
    GtkWidget* collapse_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_start(collapse_box, 8);
    gtk_widget_set_margin_end(collapse_box, 8);
    gtk_widget_set_margin_bottom(collapse_box, 8);
    
    sidebar_collapse_btn_ = gtk_button_new_from_icon_name("go-previous-symbolic");
    gtk_widget_add_css_class(sidebar_collapse_btn_, "flat-button");
    gtk_widget_set_tooltip_text(sidebar_collapse_btn_, "Collapse sidebar");
    g_signal_connect(sidebar_collapse_btn_, "clicked", G_CALLBACK(+[](GtkButton* /*btn*/, gpointer data) {
        auto* self = static_cast<AppWindow*>(data);
        Logger::info("[Sidebar] Collapse button clicked! Current expanded state: " + std::string(self->sidebar_expanded_ ? "true" : "false"));
        self->sidebar_expanded_ = !self->sidebar_expanded_;
        Logger::info("[Sidebar] New expanded state: " + std::string(self->sidebar_expanded_ ? "true" : "false"));
        
        if (self->sidebar_expanded_) {
            Logger::info("[Sidebar] Expanding sidebar");
            gtk_widget_remove_css_class(self->sidebar_, "collapsed");
            gtk_widget_set_size_request(self->sidebar_, 220, -1);
            gtk_button_set_icon_name(GTK_BUTTON(self->sidebar_collapse_btn_), "go-previous-symbolic");
            gtk_widget_set_tooltip_text(self->sidebar_collapse_btn_, "Collapse sidebar");
            // Show text widgets
            for (GtkWidget* w : self->sidebar_text_widgets_) {
                if (w) gtk_widget_set_visible(w, TRUE);
            }
        } else {
            Logger::info("[Sidebar] Collapsing sidebar");
            gtk_widget_add_css_class(self->sidebar_, "collapsed");
            gtk_widget_set_size_request(self->sidebar_, 60, -1);
            gtk_button_set_icon_name(GTK_BUTTON(self->sidebar_collapse_btn_), "go-next-symbolic");
            gtk_widget_set_tooltip_text(self->sidebar_collapse_btn_, "Expand sidebar");
            // Hide text widgets
            for (GtkWidget* w : self->sidebar_text_widgets_) {
                if (w) gtk_widget_set_visible(w, FALSE);
            }
        }
        Logger::info("[Sidebar] Collapse handler complete");
    }), this);
    gtk_box_append(GTK_BOX(collapse_box), sidebar_collapse_btn_);
    gtk_box_append(GTK_BOX(sidebar_), collapse_box);
    
    gtk_box_append(GTK_BOX(main_hbox), sidebar_);
    
    // ===== MAIN CONTENT AREA =====
    content_area_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(content_area_, TRUE);
    gtk_widget_set_vexpand(content_area_, TRUE);
    
    // ===== IN-APP TOAST BANNER =====
    toast_revealer_ = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(toast_revealer_), GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_transition_duration(GTK_REVEALER(toast_revealer_), 250);
    gtk_revealer_set_reveal_child(GTK_REVEALER(toast_revealer_), FALSE);
    
    GtkWidget* toast_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(toast_box, "toast-banner");
    gtk_widget_set_margin_start(toast_box, 20);
    gtk_widget_set_margin_end(toast_box, 20);
    gtk_widget_set_margin_top(toast_box, 6);
    gtk_widget_set_margin_bottom(toast_box, 6);
    
    GtkWidget* toast_icon = gtk_image_new_from_icon_name("emblem-synchronizing-symbolic");
    gtk_box_append(GTK_BOX(toast_box), toast_icon);
    
    toast_label_ = gtk_label_new("");
    gtk_widget_set_hexpand(toast_label_, TRUE);
    gtk_label_set_xalign(GTK_LABEL(toast_label_), 0);
    gtk_label_set_ellipsize(GTK_LABEL(toast_label_), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(toast_box), toast_label_);
    
    GtkWidget* toast_dismiss = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_add_css_class(toast_dismiss, "flat");
    gtk_widget_add_css_class(toast_dismiss, "circular");
    g_signal_connect(toast_dismiss, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* self = static_cast<AppWindow*>(data);
        gtk_revealer_set_reveal_child(GTK_REVEALER(self->toast_revealer_), FALSE);
    }), this);
    gtk_box_append(GTK_BOX(toast_box), toast_dismiss);
    
    gtk_revealer_set_child(GTK_REVEALER(toast_revealer_), toast_box);
    gtk_box_append(GTK_BOX(content_area_), toast_revealer_);
    
    // Create main stack for different views
    main_stack_ = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(main_stack_), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_widget_set_vexpand(main_stack_, TRUE);
    gtk_box_append(GTK_BOX(content_area_), main_stack_);
    
    // ===== OVERVIEW PAGE =====
    GtkWidget* overview_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    
    // Overview header
    GtkWidget* overview_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_add_css_class(overview_header, "content-header");
    
    GtkWidget* overview_title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget* overview_title = gtk_label_new("Overview");
    gtk_widget_add_css_class(overview_title, "content-title");
    gtk_label_set_xalign(GTK_LABEL(overview_title), 0);
    gtk_box_append(GTK_BOX(overview_title_box), overview_title);
    
    GtkWidget* overview_subtitle = gtk_label_new("Sync status and quick actions");
    gtk_widget_add_css_class(overview_subtitle, "content-subtitle");
    gtk_label_set_xalign(GTK_LABEL(overview_subtitle), 0);
    gtk_box_append(GTK_BOX(overview_title_box), overview_subtitle);
    gtk_box_append(GTK_BOX(overview_header), overview_title_box);
    
    gtk_box_append(GTK_BOX(overview_page), overview_header);
    
    // Overview content with drop zone and sync activity
    GtkWidget* overview_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(overview_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(overview_scroll, TRUE);
    
    GtkWidget* overview_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(overview_scroll), overview_content);
    
    // Setup notice banner (visibility handled by update_setup_state)
    setup_banner_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(setup_banner_, 16);
    gtk_widget_set_margin_end(setup_banner_, 16);
    gtk_widget_set_margin_top(setup_banner_, 16);
    gtk_widget_set_margin_bottom(setup_banner_, 16);
    gtk_widget_add_css_class(setup_banner_, "warning-banner");
    
    // Warning icon
    GtkWidget* setup_icon = gtk_image_new_from_icon_name("dialog-warning-symbolic");
    gtk_widget_add_css_class(setup_icon, "setup-icon");
    gtk_box_append(GTK_BOX(setup_banner_), setup_icon);
    
    // Setup message box
    GtkWidget* setup_msg_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_hexpand(setup_msg_box, TRUE);
    
    GtkWidget* setup_title = gtk_label_new("Setup Required");
    gtk_widget_add_css_class(setup_title, "setup-title");
    gtk_label_set_xalign(GTK_LABEL(setup_title), 0);
    gtk_box_append(GTK_BOX(setup_msg_box), setup_title);
    
    GtkWidget* setup_text = gtk_label_new(
        "To get started, configure your Proton Drive profile. "
        "Click the Settings button to add your Proton account."
    );
    gtk_widget_add_css_class(setup_text, "setup-text");
    gtk_label_set_xalign(GTK_LABEL(setup_text), 0);
    gtk_label_set_wrap(GTK_LABEL(setup_text), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(setup_text), PANGO_WRAP_WORD);
    gtk_box_append(GTK_BOX(setup_msg_box), setup_text);
    
    gtk_box_append(GTK_BOX(setup_banner_), setup_msg_box);
    
    // Setup button
    GtkWidget* setup_btn = gtk_button_new_with_label("Go to Settings");
    gtk_widget_add_css_class(setup_btn, "suggested-action");
    g_signal_connect(setup_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* self = static_cast<AppWindow*>(data);
        gtk_stack_set_visible_child_name(GTK_STACK(self->main_stack_), "settings");
        self->append_log("[Setup] Navigating to Settings to configure Proton profile");
    }), this);
    gtk_box_append(GTK_BOX(setup_banner_), setup_btn);
    
    gtk_box_append(GTK_BOX(overview_content), setup_banner_);
    
    build_drop_zone();
    gtk_box_append(GTK_BOX(overview_content), drop_zone_);
    
    gtk_box_append(GTK_BOX(overview_page), overview_scroll);
    gtk_stack_add_named(GTK_STACK(main_stack_), overview_page, "overview");
    
    // ===== SYNC JOBS PAGE =====
    GtkWidget* sync_jobs_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    
    // Sync jobs header
    GtkWidget* sync_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_add_css_class(sync_header, "content-header");
    
    GtkWidget* sync_title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget* sync_title = gtk_label_new("Sync Jobs");
    gtk_widget_add_css_class(sync_title, "content-title");
    gtk_label_set_xalign(GTK_LABEL(sync_title), 0);
    gtk_box_append(GTK_BOX(sync_title_box), sync_title);
    
    GtkWidget* sync_subtitle = gtk_label_new("Manage folder synchronization");
    gtk_widget_add_css_class(sync_subtitle, "content-subtitle");
    gtk_label_set_xalign(GTK_LABEL(sync_subtitle), 0);
    gtk_box_append(GTK_BOX(sync_title_box), sync_subtitle);
    gtk_widget_set_hexpand(sync_title_box, TRUE);
    gtk_box_append(GTK_BOX(sync_header), sync_title_box);
    
    // Add sync job button
    GtkWidget* add_sync_btn = gtk_button_new_with_label("+ Add Sync Job");
    gtk_widget_add_css_class(add_sync_btn, "suggested-action");
    g_signal_connect(add_sync_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<AppWindow*>(data)->on_add_sync_clicked();
    }), this);
    gtk_box_append(GTK_BOX(sync_header), add_sync_btn);
    
    gtk_box_append(GTK_BOX(sync_jobs_page), sync_header);
    
    // Active sync activity panel
    build_sync_activity_panel();
    gtk_box_append(GTK_BOX(sync_jobs_page), sync_activity_box_);
    
    // ===== CONFLICTS SECTION =====
    conflicts_revealer_ = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(conflicts_revealer_), GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_reveal_child(GTK_REVEALER(conflicts_revealer_), FALSE);
    
    GtkWidget* conflicts_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(conflicts_box, "conflicts-section");
    gtk_widget_set_margin_start(conflicts_box, 16);
    gtk_widget_set_margin_end(conflicts_box, 16);
    gtk_widget_set_margin_top(conflicts_box, 8);
    gtk_widget_set_margin_bottom(conflicts_box, 8);
    
    // Conflicts header
    GtkWidget* conflicts_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* conflicts_icon = gtk_label_new("\u26a0\ufe0f");  // Warning emoji
    gtk_box_append(GTK_BOX(conflicts_header), conflicts_icon);
    
    GtkWidget* conflicts_title = gtk_label_new("Sync Conflicts Detected");
    gtk_widget_add_css_class(conflicts_title, "heading");
    gtk_widget_add_css_class(conflicts_title, "warning");
    gtk_label_set_xalign(GTK_LABEL(conflicts_title), 0);
    gtk_widget_set_hexpand(conflicts_title, TRUE);
    gtk_box_append(GTK_BOX(conflicts_header), conflicts_title);
    
    // "Dismiss" button
    GtkWidget* dismiss_btn = gtk_button_new_with_label("Dismiss");
    gtk_widget_add_css_class(dismiss_btn, "flat");
    g_signal_connect(dismiss_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* self = static_cast<AppWindow*>(data);
        self->clear_conflicts();
    }), this);
    gtk_box_append(GTK_BOX(conflicts_header), dismiss_btn);
    
    gtk_box_append(GTK_BOX(conflicts_box), conflicts_header);
    
    // Info text
    GtkWidget* conflicts_info = gtk_label_new("rclone detected files that changed on both local and cloud. Review and resolve:");
    gtk_widget_add_css_class(conflicts_info, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(conflicts_info), 0);
    gtk_label_set_wrap(GTK_LABEL(conflicts_info), TRUE);
    gtk_box_append(GTK_BOX(conflicts_box), conflicts_info);
    
    // Conflicts list
    conflicts_list_ = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(conflicts_list_), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(conflicts_list_, "boxed-list");
    gtk_box_append(GTK_BOX(conflicts_box), conflicts_list_);
    
    // Action buttons row
    GtkWidget* conflicts_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(conflicts_actions, GTK_ALIGN_END);
    
    GtkWidget* open_folder_btn = gtk_button_new_with_label("Open in File Manager");
    g_signal_connect(open_folder_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* self = static_cast<AppWindow*>(data);
        std::lock_guard<std::mutex> lock(self->conflicts_mutex_);
        if (!self->detected_conflicts_.empty()) {
            // Get directory of first conflict
            std::filesystem::path p(self->detected_conflicts_[0]);
            std::string dir = p.parent_path().string();
            std::string cmd = "xdg-open \"" + dir + "\" &";
            std::system(cmd.c_str());
        }
    }), this);
    gtk_box_append(GTK_BOX(conflicts_actions), open_folder_btn);
    
    GtkWidget* delete_conflicts_btn = gtk_button_new_with_label("Delete All Conflicts");
    gtk_widget_add_css_class(delete_conflicts_btn, "destructive-action");
    g_signal_connect(delete_conflicts_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* self = static_cast<AppWindow*>(data);
        std::lock_guard<std::mutex> lock(self->conflicts_mutex_);
        int deleted = 0;
        for (const auto& path : self->detected_conflicts_) {
            std::error_code ec;
            if (std::filesystem::remove(path, ec)) {
                deleted++;
            }
        }
        self->append_log("[Conflicts] Deleted " + std::to_string(deleted) + " conflict file(s)");
        self->show_toast("Deleted " + std::to_string(deleted) + " conflict file(s)");
        self->detected_conflicts_.clear();
        self->refresh_conflicts();
    }), this);
    gtk_box_append(GTK_BOX(conflicts_actions), delete_conflicts_btn);
    
    gtk_box_append(GTK_BOX(conflicts_box), conflicts_actions);
    
    gtk_revealer_set_child(GTK_REVEALER(conflicts_revealer_), conflicts_box);
    gtk_box_append(GTK_BOX(sync_jobs_page), conflicts_revealer_);
    
    // Jobs list
    GtkWidget* jobs_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(jobs_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(jobs_scroll, TRUE);
    
    jobs_list_ = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(jobs_list_), GTK_SELECTION_NONE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(jobs_scroll), jobs_list_);
    gtk_box_append(GTK_BOX(sync_jobs_page), jobs_scroll);
    
    gtk_stack_add_named(GTK_STACK(main_stack_), sync_jobs_page, "sync-jobs");
    
    // ===== DEVICES PAGE =====
    GtkWidget* devices_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    
    GtkWidget* devices_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_add_css_class(devices_header, "content-header");
    
    GtkWidget* devices_title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget* devices_title = gtk_label_new("Devices");
    gtk_widget_add_css_class(devices_title, "content-title");
    gtk_label_set_xalign(GTK_LABEL(devices_title), 0);
    gtk_box_append(GTK_BOX(devices_title_box), devices_title);
    
    GtkWidget* devices_subtitle = gtk_label_new("Connected devices and sync status");
    gtk_widget_add_css_class(devices_subtitle, "content-subtitle");
    gtk_label_set_xalign(GTK_LABEL(devices_subtitle), 0);
    gtk_box_append(GTK_BOX(devices_title_box), devices_subtitle);
    gtk_widget_set_hexpand(devices_title_box, TRUE);
    gtk_box_append(GTK_BOX(devices_header), devices_title_box);
    
    // Refresh devices button
    GtkWidget* refresh_devices_btn = gtk_button_new_from_icon_name("view-refresh-symbolic");
    gtk_widget_add_css_class(refresh_devices_btn, "flat-button");
    gtk_widget_set_tooltip_text(refresh_devices_btn, "Refresh devices");
    g_signal_connect(refresh_devices_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<AppWindow*>(data)->refresh_devices();
    }), this);
    gtk_box_append(GTK_BOX(devices_header), refresh_devices_btn);
    gtk_box_append(GTK_BOX(devices_page), devices_header);
    
    // Devices list in scrollable container
    GtkWidget* devices_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(devices_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(devices_scroll, TRUE);
    
    devices_list_ = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(devices_list_), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(devices_list_, "boxed-list");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(devices_scroll), devices_list_);
    gtk_box_append(GTK_BOX(devices_page), devices_scroll);
    
    gtk_stack_add_named(GTK_STACK(main_stack_), devices_page, "devices");
    
    // ===== BROWSER PAGE =====
    build_file_browser();
    gtk_stack_add_named(GTK_STACK(main_stack_), file_browser_box_, "browser");
    
    // ===== LOGS PAGE =====
    build_logs_panel();
    GtkWidget* logs_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    
    GtkWidget* logs_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_add_css_class(logs_header, "content-header");
    
    GtkWidget* logs_title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget* logs_title = gtk_label_new("Event Logs");
    gtk_widget_add_css_class(logs_title, "content-title");
    gtk_label_set_xalign(GTK_LABEL(logs_title), 0);
    gtk_box_append(GTK_BOX(logs_title_box), logs_title);
    
    GtkWidget* logs_subtitle = gtk_label_new("Application activity and sync events");
    gtk_widget_add_css_class(logs_subtitle, "content-subtitle");
    gtk_label_set_xalign(GTK_LABEL(logs_subtitle), 0);
    gtk_box_append(GTK_BOX(logs_title_box), logs_subtitle);
    gtk_widget_set_hexpand(logs_title_box, TRUE);
    gtk_box_append(GTK_BOX(logs_header), logs_title_box);
    
    // Clear logs button
    GtkWidget* clear_logs_btn = gtk_button_new_with_label("Clear");
    gtk_widget_add_css_class(clear_logs_btn, "flat-button");
    g_signal_connect(clear_logs_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* self = static_cast<AppWindow*>(data);
        if (self->logs_view_) {
            GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->logs_view_));
            gtk_text_buffer_set_text(buffer, "", 0);
        }
    }), this);
    gtk_box_append(GTK_BOX(logs_header), clear_logs_btn);
    gtk_box_append(GTK_BOX(logs_page), logs_header);
    
    gtk_widget_set_vexpand(logs_scroll_, TRUE);
    gtk_box_append(GTK_BOX(logs_page), logs_scroll_);
    
    gtk_stack_add_named(GTK_STACK(main_stack_), logs_page, "logs");
    
    // ===== SETTINGS PAGE =====
    GtkWidget* settings_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    
    GtkWidget* settings_header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(settings_header, "content-header");
    GtkWidget* settings_title = gtk_label_new("Settings");
    gtk_widget_add_css_class(settings_title, "content-title");
    gtk_label_set_xalign(GTK_LABEL(settings_title), 0);
    gtk_box_append(GTK_BOX(settings_header), settings_title);
    gtk_box_append(GTK_BOX(settings_page), settings_header);
    
    // Settings content
    GtkWidget* settings_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(settings_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(settings_scroll, TRUE);
    
    GtkWidget* settings_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(settings_content, 24);
    gtk_widget_set_margin_end(settings_content, 24);
    gtk_widget_set_margin_top(settings_content, 16);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(settings_scroll), settings_content);
    
    // Setup guide section (visibility handled by update_setup_state)
    setup_guide_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(setup_guide_, "warning-banner");
    gtk_widget_set_margin_bottom(setup_guide_, 16);
    
    // Title
    GtkWidget* setup_guide_title = gtk_label_new("Welcome to Proton Drive!");
    gtk_widget_add_css_class(setup_guide_title, "setup-title");
    gtk_label_set_xalign(GTK_LABEL(setup_guide_title), 0);
    gtk_box_append(GTK_BOX(setup_guide_), setup_guide_title);
    
    // Getting started text
    GtkWidget* setup_guide_text = gtk_label_new(
        "To start syncing your files, you'll need to configure a Proton profile. "
        "Follow these steps:\n\n"
        "1. Click \"+ Add Profile\" below\n"
        "2. Enter your Proton account credentials\n"
        "3. Click Configure to save"
    );
    gtk_widget_add_css_class(setup_guide_text, "setup-text");
    gtk_label_set_xalign(GTK_LABEL(setup_guide_text), 0);
    gtk_label_set_wrap(GTK_LABEL(setup_guide_text), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(setup_guide_text), PANGO_WRAP_WORD);
    gtk_box_append(GTK_BOX(setup_guide_), setup_guide_text);
    
    // Button box
    GtkWidget* setup_btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(setup_btn_box, GTK_ALIGN_START);
    
    GtkWidget* add_profile_btn_setup = gtk_button_new_with_label("+ Add Profile");
    gtk_widget_add_css_class(add_profile_btn_setup, "suggested-action");
    g_signal_connect(add_profile_btn_setup, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<AppWindow*>(data)->on_add_profile_clicked();
    }), this);
    gtk_box_append(GTK_BOX(setup_btn_box), add_profile_btn_setup);
    
    GtkWidget* refresh_btn = gtk_button_new_with_label("Refresh");
    gtk_widget_add_css_class(refresh_btn, "flat-button");
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* self = static_cast<AppWindow*>(data);
        self->append_log("[Setup] Refreshing profiles...");
        self->refresh_profiles();
    }), this);
    gtk_box_append(GTK_BOX(setup_btn_box), refresh_btn);
    
    gtk_box_append(GTK_BOX(setup_guide_), setup_btn_box);
    
    // Add horizontal separator
    GtkWidget* setup_sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(setup_guide_), setup_sep);
    
    gtk_box_append(GTK_BOX(settings_content), setup_guide_);
    
    // Profiles section
    GtkWidget* profiles_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget* profiles_label = gtk_label_new("RCLONE PROFILES");
    gtk_widget_add_css_class(profiles_label, "menu-section-title");
    gtk_label_set_xalign(GTK_LABEL(profiles_label), 0);
    gtk_box_append(GTK_BOX(profiles_section), profiles_label);
    
    profiles_list_ = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(profiles_list_), GTK_SELECTION_NONE);
    gtk_box_append(GTK_BOX(profiles_section), profiles_list_);
    
    GtkWidget* profile_button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    
    GtkWidget* add_profile_btn = gtk_button_new_with_label("+ Add Profile");
    gtk_widget_add_css_class(add_profile_btn, "flat-button");
    g_signal_connect(add_profile_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<AppWindow*>(data)->on_add_profile_clicked();
    }), this);
    gtk_box_append(GTK_BOX(profile_button_box), add_profile_btn);
    
    GtkWidget* test_connection_btn = gtk_button_new_with_label("Test Connection");
    gtk_widget_add_css_class(test_connection_btn, "flat-button");
    g_signal_connect(test_connection_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<AppWindow*>(data)->on_test_connection_clicked();
    }), this);
    gtk_box_append(GTK_BOX(profile_button_box), test_connection_btn);
    
    gtk_box_append(GTK_BOX(profiles_section), profile_button_box);
    
    gtk_box_append(GTK_BOX(settings_content), profiles_section);
    
    // Service controls
    GtkWidget* service_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget* service_label = gtk_label_new("SYNC SERVICE");
    gtk_widget_add_css_class(service_label, "menu-section-title");
    gtk_label_set_xalign(GTK_LABEL(service_label), 0);
    gtk_box_append(GTK_BOX(service_section), service_label);
    
    service_status_label_ = gtk_label_new("Checking...");
    gtk_label_set_xalign(GTK_LABEL(service_status_label_), 0);
    gtk_box_append(GTK_BOX(service_section), service_status_label_);
    
    GtkWidget* service_btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    start_btn_ = gtk_button_new_with_label("Start All");
    g_signal_connect(start_btn_, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<AppWindow*>(data)->on_start_clicked();
    }), this);
    
    stop_btn_ = gtk_button_new_with_label("Stop All");
    g_signal_connect(stop_btn_, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<AppWindow*>(data)->on_stop_clicked();
    }), this);
    
    gtk_box_append(GTK_BOX(service_btns), start_btn_);
    gtk_box_append(GTK_BOX(service_btns), stop_btn_);
    gtk_box_append(GTK_BOX(service_section), service_btns);
    
    gtk_box_append(GTK_BOX(settings_content), service_section);

    // Sync Settings section
    GtkWidget* sync_settings_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget* sync_settings_label = gtk_label_new("SYNC SETTINGS");
    gtk_widget_add_css_class(sync_settings_label, "menu-section-title");
    gtk_label_set_xalign(GTK_LABEL(sync_settings_label), 0);
    gtk_box_append(GTK_BOX(sync_settings_section), sync_settings_label);
    
    // Sync interval setting
    GtkWidget* interval_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* interval_label = gtk_label_new("Sync interval:");
    gtk_widget_set_hexpand(interval_label, FALSE);
    gtk_box_append(GTK_BOX(interval_row), interval_label);
    
    GtkStringList* interval_options = gtk_string_list_new(nullptr);
    gtk_string_list_append(interval_options, "5 minutes");
    gtk_string_list_append(interval_options, "10 minutes");
    gtk_string_list_append(interval_options, "15 minutes");
    gtk_string_list_append(interval_options, "30 minutes");
    gtk_string_list_append(interval_options, "1 hour");
    gtk_string_list_append(interval_options, "Manual only");
    
    GtkWidget* interval_dropdown = gtk_drop_down_new(G_LIST_MODEL(interval_options), nullptr);
    gtk_widget_set_size_request(interval_dropdown, 150, -1);
    
    // Set current value from settings
    auto& settings = proton::SettingsManager::getInstance();
    int current_interval = settings.get_sync_interval_minutes();
    guint selected = 2; // Default to 15 minutes
    if (current_interval <= 5) selected = 0;
    else if (current_interval <= 10) selected = 1;
    else if (current_interval <= 15) selected = 2;
    else if (current_interval <= 30) selected = 3;
    else if (current_interval <= 60) selected = 4;
    else selected = 5; // Manual
    gtk_drop_down_set_selected(GTK_DROP_DOWN(interval_dropdown), selected);
    
    g_signal_connect(interval_dropdown, "notify::selected", G_CALLBACK(+[](GObject* self, GParamSpec*, gpointer data) {
        auto* app = static_cast<AppWindow*>(data);
        guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(self));
        int minutes = 15; // default
        switch (sel) {
            case 0: minutes = 5; break;
            case 1: minutes = 10; break;
            case 2: minutes = 15; break;
            case 3: minutes = 30; break;
            case 4: minutes = 60; break;
            case 5: minutes = 9999; break; // Manual only
        }
        auto& s = proton::SettingsManager::getInstance();
        s.set_sync_interval_minutes(minutes);
        app->append_log("[Settings] Sync interval changed to " + std::to_string(minutes) + " minutes");
        
        // Update systemd timers
        std::string interval_str = std::to_string(minutes) + "m";
        if (minutes >= 9999) interval_str = "1d"; // For manual, set to 1 day to minimize
        
        // Update all job timers
        std::string update_cmd = "for timer in ~/.config/systemd/user/proton-drive-job-*.timer; do "
                                 "if [ -f \"$timer\" ]; then "
                                 "sed -i 's/OnUnitActiveSec=.*/OnUnitActiveSec=" + interval_str + "/' \"$timer\"; "
                                 "fi; done && systemctl --user daemon-reload 2>/dev/null";
        std::system(update_cmd.c_str());
        
        app->show_toast("Sync interval updated to " + interval_str);
    }), this);
    
    gtk_box_append(GTK_BOX(interval_row), interval_dropdown);
    
    GtkWidget* interval_note = gtk_label_new("(Changes apply to all sync jobs)");
    gtk_widget_add_css_class(interval_note, "dim-label");
    gtk_widget_add_css_class(interval_note, "caption");
    gtk_box_append(GTK_BOX(interval_row), interval_note);
    
    gtk_box_append(GTK_BOX(sync_settings_section), interval_row);
    
    // Start at login toggle
    GtkWidget* autostart_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* autostart_sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(autostart_sw), settings.get_start_on_login());
    g_signal_connect(autostart_sw, "state-set", G_CALLBACK(+[](GtkSwitch*, gboolean state, gpointer data) -> gboolean {
        auto* app = static_cast<AppWindow*>(data);
        auto& s = proton::SettingsManager::getInstance();
        s.set_start_on_login(state);
        app->append_log(std::string("[Settings] Start on login ") + (state ? "enabled" : "disabled"));
        return FALSE;
    }), this);
    gtk_box_append(GTK_BOX(autostart_row), autostart_sw);
    GtkWidget* autostart_label = gtk_label_new("Start at login");
    gtk_box_append(GTK_BOX(autostart_row), autostart_label);
    gtk_box_append(GTK_BOX(sync_settings_section), autostart_row);
    
    // Close to tray toggle
    GtkWidget* tray_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* tray_sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(tray_sw), settings.get_minimize_to_tray());
    g_signal_connect(tray_sw, "state-set", G_CALLBACK(+[](GtkSwitch*, gboolean state, gpointer data) -> gboolean {
        auto* app = static_cast<AppWindow*>(data);
        auto& s = proton::SettingsManager::getInstance();
        s.set_minimize_to_tray(state);
        app->append_log(std::string("[Settings] Close to tray ") + (state ? "enabled (minimize on close)" : "disabled (quit on close)"));
        return FALSE;
    }), this);
    gtk_box_append(GTK_BOX(tray_row), tray_sw);
    
    GtkWidget* tray_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget* tray_label = gtk_label_new("Minimize to tray on close");
    gtk_label_set_xalign(GTK_LABEL(tray_label), 0);
    gtk_box_append(GTK_BOX(tray_vbox), tray_label);
    GtkWidget* tray_desc = gtk_label_new("When disabled, closing the window will quit the application");
    gtk_widget_add_css_class(tray_desc, "dim-label");
    gtk_widget_add_css_class(tray_desc, "caption");
    gtk_label_set_xalign(GTK_LABEL(tray_desc), 0);
    gtk_box_append(GTK_BOX(tray_vbox), tray_desc);
    gtk_box_append(GTK_BOX(tray_row), tray_vbox);
    gtk_box_append(GTK_BOX(sync_settings_section), tray_row);
    
    // Desktop notifications toggle
    GtkWidget* notify_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* notify_sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(notify_sw), settings.get_show_notifications());
    g_signal_connect(notify_sw, "state-set", G_CALLBACK(+[](GtkSwitch*, gboolean state, gpointer data) -> gboolean {
        auto* app = static_cast<AppWindow*>(data);
        auto& s = proton::SettingsManager::getInstance();
        s.set_show_notifications(state);
        proton::NotificationManager::getInstance().set_enabled(state);
        app->append_log(std::string("[Settings] Notifications ") + (state ? "enabled" : "disabled"));
        return FALSE;
    }), this);
    gtk_box_append(GTK_BOX(notify_row), notify_sw);
    GtkWidget* notify_label = gtk_label_new("Desktop notifications");
    gtk_box_append(GTK_BOX(notify_row), notify_label);
    gtk_box_append(GTK_BOX(sync_settings_section), notify_row);
    
    gtk_box_append(GTK_BOX(settings_content), sync_settings_section);

    // Local Cleanup section
    GtkWidget* cleanup_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget* cleanup_label = gtk_label_new("LOCAL CLEANUP");
    gtk_widget_add_css_class(cleanup_label, "menu-section-title");
    gtk_label_set_xalign(GTK_LABEL(cleanup_label), 0);
    gtk_box_append(GTK_BOX(cleanup_section), cleanup_label);

    GtkWidget* cleanup_info = gtk_label_new(
        "Analyze synced folders by size and move selected items to the cloud or delete them locally."
    );
    gtk_label_set_xalign(GTK_LABEL(cleanup_info), 0);
    gtk_label_set_wrap(GTK_LABEL(cleanup_info), TRUE);
    gtk_box_append(GTK_BOX(cleanup_section), cleanup_info);

    GtkWidget* cleanup_btn = gtk_button_new_with_label("Open Local Cleanup");
    gtk_widget_add_css_class(cleanup_btn, "suggested-action");
    g_signal_connect(cleanup_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<AppWindow*>(data)->show_local_cleanup_dialog();
    }), this);
    gtk_box_append(GTK_BOX(cleanup_section), cleanup_btn);

    gtk_box_append(GTK_BOX(settings_content), cleanup_section);
    
    // Search Index section
    GtkWidget* index_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget* index_label = gtk_label_new("SEARCH INDEX");
    gtk_widget_add_css_class(index_label, "menu-section-title");
    gtk_label_set_xalign(GTK_LABEL(index_label), 0);
    gtk_box_append(GTK_BOX(index_section), index_label);
    
    GtkWidget* index_status = gtk_label_new("The search index enables fast file search across your cloud files.");
    gtk_label_set_xalign(GTK_LABEL(index_status), 0);
    gtk_label_set_wrap(GTK_LABEL(index_status), TRUE);
    gtk_box_append(GTK_BOX(index_section), index_status);
    
    index_status_label_ = gtk_label_new("Status: Unknown");
    gtk_label_set_xalign(GTK_LABEL(index_status_label_), 0);
    gtk_box_append(GTK_BOX(index_section), index_status_label_);
    
    GtkWidget* reindex_btn = gtk_button_new_with_label("Force Rebuild Index");
    gtk_widget_add_css_class(reindex_btn, "destructive-action");
    g_signal_connect(reindex_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer data) {
        auto* self = static_cast<AppWindow*>(data);
        gtk_widget_set_sensitive(GTK_WIDGET(btn), FALSE);
        gtk_button_set_label(btn, "Indexing...");
        self->append_log("[Search] Starting full reindex of cloud files...");
        
        // Update status label
        if (self->index_status_label_) {
            gtk_label_set_text(GTK_LABEL(self->index_status_label_), "Status: Indexing in progress...");
        }
        
        auto& file_index = FileIndex::getInstance();
        file_index.set_progress_callback([self, btn](int percent, const std::string& status) {
            // Schedule UI update on main thread
            struct UpdateData { AppWindow* self; GtkButton* btn; int percent; std::string status; };
            auto* d = new UpdateData{self, btn, percent, status};
            g_idle_add(+[](gpointer user_data) -> gboolean {
                auto* data = static_cast<UpdateData*>(user_data);
                std::string label = "Indexing... " + std::to_string(data->percent) + "%";
                gtk_button_set_label(data->btn, label.c_str());
                if (data->self->index_status_label_) {
                    gtk_label_set_text(GTK_LABEL(data->self->index_status_label_), 
                                       ("Status: " + data->status).c_str());
                }
                if (data->percent >= 100) {
                    gtk_widget_set_sensitive(GTK_WIDGET(data->btn), TRUE);
                    gtk_button_set_label(data->btn, "Force Rebuild Index");
                    data->self->append_log("[Search] Indexing complete!");
                }
                delete data;
                return G_SOURCE_REMOVE;
            }, d);
        });
        
        file_index.start_background_index(true);  // Force full reindex
    }), this);
    gtk_box_append(GTK_BOX(index_section), reindex_btn);
    
    gtk_box_append(GTK_BOX(settings_content), index_section);
    gtk_box_append(GTK_BOX(settings_page), settings_scroll);
    
    gtk_stack_add_named(GTK_STACK(main_stack_), settings_page, "settings");
    
    // Set default view to Overview
    gtk_stack_set_visible_child_name(GTK_STACK(main_stack_), "overview");
    
    // Add content area directly to main_hbox (no progress overlay)
    gtk_box_append(GTK_BOX(main_hbox), content_area_);
    
    // Initial state refresh
    refresh_profiles();
    refresh_sync_jobs();
    refresh_devices();
    poll_status();
    
    // Start cloud monitoring for automatic sync
    start_cloud_monitoring();
    
    // Log startup
    append_log("Proton Drive started");
    
    // Initialize local path
    const char* home = getenv("HOME");
    current_local_path_ = home ? std::string(home) + "/ProtonDrive" : "/tmp";
    
    // Refresh file lists
    refresh_cloud_files();
    refresh_local_files();
}

void AppWindow::build_header_bar() {
    header_bar_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(header_bar_, 10);
    gtk_widget_set_margin_end(header_bar_, 10);
    gtk_widget_set_margin_top(header_bar_, 8);
    gtk_widget_set_margin_bottom(header_bar_, 8);
    
    // Hamburger menu button
    hamburger_btn_ = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(hamburger_btn_), "open-menu-symbolic");
    gtk_widget_set_tooltip_text(hamburger_btn_, "Menu");
    gtk_box_append(GTK_BOX(header_bar_), hamburger_btn_);
    
    // Build hamburger menu popover
    build_hamburger_menu();
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(hamburger_btn_), hamburger_menu_);
    
    // Title
    GtkWidget* title = gtk_label_new("Proton Drive");
    gtk_widget_add_css_class(title, "title-label");
    gtk_box_append(GTK_BOX(header_bar_), title);
    
    // Spacer
    GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(header_bar_), spacer);
    
    // Browse cloud button
    GtkWidget* browse_btn = gtk_button_new_from_icon_name("folder-remote-symbolic");
    gtk_widget_set_tooltip_text(browse_btn, "Browse Cloud Files");
    gtk_widget_add_css_class(browse_btn, "flat-button");
    g_signal_connect(browse_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* self = static_cast<AppWindow*>(data);
        self->on_browse_clicked();
    }), this);
    gtk_box_append(GTK_BOX(header_bar_), browse_btn);
    
    // Logs toggle button
    logs_toggle_ = gtk_toggle_button_new();
    gtk_button_set_icon_name(GTK_BUTTON(logs_toggle_), "utilities-terminal-symbolic");
    gtk_widget_set_tooltip_text(logs_toggle_, "Toggle Logs");
    gtk_widget_add_css_class(logs_toggle_, "flat-button");
    g_signal_connect(logs_toggle_, "toggled", G_CALLBACK(+[](GtkToggleButton* btn, gpointer data) {
        auto* self = static_cast<AppWindow*>(data);
        gboolean active = gtk_toggle_button_get_active(btn);
        gtk_revealer_set_reveal_child(GTK_REVEALER(self->logs_revealer_), active);
    }), this);
    gtk_box_append(GTK_BOX(header_bar_), logs_toggle_);
}

void AppWindow::set_drop_zone_highlight(bool active) {
    if (active) {
        gtk_widget_add_css_class(drop_zone_, "drop-zone-active");
    } else {
        gtk_widget_remove_css_class(drop_zone_, "drop-zone-active");
    }
}

void AppWindow::handle_dropped_files(const std::vector<std::string>& paths) {
    for (const auto& path : paths) {
        append_log("[Drop] Received: " + path);
        
        if (safe_is_directory(path)) {
            // Local folder dropped: show sync setup dialog (local → cloud)
            append_log("[Drop] Local folder detected: " + path);
            show_sync_from_local_dialog(path);
        } else {
            // Single file: upload directly to current cloud folder
            append_log("[Upload] Uploading file to cloud: " + path);
            std::vector<std::string> single_file = {path};
            handle_cloud_drop(single_file, current_cloud_path_);
        }
    }
}

void AppWindow::handle_cloud_drop(const std::vector<std::string>& local_paths, const std::string& cloud_folder) {
    if (local_paths.empty()) return;
    
    std::string rclone_path = AppWindowHelpers::get_rclone_path();
    std::string dest = "proton:" + cloud_folder;
    if (dest.back() != '/') dest += "/";
    
    // Count total files for progress
    int total_files = static_cast<int>(local_paths.size());
    
    // Show immediate notification that upload is starting
    proton::NotificationManager::getInstance().notify(
        "Upload Started",
        "Uploading " + std::to_string(total_files) + " file(s) to " + cloud_folder,
        proton::NotificationType::INFO
    );
    
    // Log start in the UI
    append_log("[Upload] Starting upload of " + std::to_string(total_files) + " file(s) to " + cloud_folder + "...");
    
    // Invalidate cache now so refresh will get fresh data
    invalidate_cloud_cache();
    
    for (const auto& local_path : local_paths) {
        std::string filename = fs::path(local_path).filename().string();
        bool is_dir = safe_is_directory(local_path);
        
        append_log(std::string("[Upload] ") + (is_dir ? "Copying folder" : "Uploading") + ": " + filename);
        
        // Add to transfer popup
        add_transfer_item(filename, true);  // true = upload
        
        // For folders, offer to create a sync job
        std::string remote_path = cloud_folder;
        if (remote_path.back() != '/') remote_path += "/";
        remote_path += filename;
        
        // Use copy for files, copyto for keeping structure
        std::string cmd;
        if (is_dir) {
            // For folders, use copy to preserve folder name
            cmd = rclone_path + " copy " + AppWindowHelpers::shell_escape(local_path) + 
                  " " + AppWindowHelpers::shell_escape(dest + filename) + " --progress 2>&1";
        } else {
            // For files, use copyto
            cmd = rclone_path + " copyto " + AppWindowHelpers::shell_escape(local_path) + 
                  " " + AppWindowHelpers::shell_escape(dest + filename) + " --progress 2>&1";
        }
        
        Logger::info("[CloudDrop] Executing: " + cmd);
        
        // Run upload in background thread
        auto* self = this;
        std::string target_folder = cloud_folder;  // Capture for lambda
        std::string local_path_copy = local_path;
        std::string remote_path_copy = remote_path;
        std::thread([self, cmd, filename, is_dir, target_folder, total_files, local_path_copy, remote_path_copy]() {
            FILE* pipe = popen(cmd.c_str(), "r");
            if (pipe) {
                char buffer[256];
                while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                    // Could parse progress here for detailed feedback
                }
                int ret = pclose(pipe);
                
                // Update transfer progress (simple completion for now)
                struct UpdateData {
                    AppWindow* self;
                    std::string filename;
                    bool success;
                };
                auto* update_data = new UpdateData{self, filename, (ret == 0)};
                
                g_idle_add(+[](gpointer user_data) -> gboolean {
                    auto* d = static_cast<UpdateData*>(user_data);
                    d->self->complete_transfer_item(d->filename, d->success);
                    delete d;
                    return G_SOURCE_REMOVE;
                }, update_data);
                
                // Capture results for main thread callback
                struct UploadResult {
                    AppWindow* self;
                    std::string filename;
                    std::string target_folder;
                    std::string local_path;
                    std::string remote_path;
                    bool is_dir;
                    bool success;
                };
                auto* result = new UploadResult{self, filename, target_folder, local_path_copy, remote_path_copy, is_dir, (ret == 0)};
                
                // Handle completion on main thread
                g_idle_add(+[](gpointer data) -> gboolean {
                    auto* r = static_cast<UploadResult*>(data);
                    
                    if (r->success) {
                        Logger::info("[CloudDrop] Upload complete: " + r->filename);
                        r->self->append_log("[Upload] ✓ Completed: " + r->filename);
                        
                        // Show success notification
                        proton::NotificationManager::getInstance().notify(
                            "Upload Complete",
                            r->filename + " uploaded to " + r->target_folder,
                            proton::NotificationType::UPLOAD_COMPLETE
                        );
                        
                        // For folders, automatically create a sync job
                        if (r->is_dir) {
                            auto& registry = SyncJobRegistry::getInstance();
                            std::string job_id = registry.createJob(r->local_path, r->remote_path, "bisync");
                            Logger::info("[CloudDrop] Created sync job for folder: " + r->filename + " (job: " + job_id + ")");
                            r->self->append_log("[Sync] Created two-way sync job for: " + r->filename);
                            r->self->refresh_sync_jobs();
                        }
                    } else {
                        Logger::error("[CloudDrop] Upload failed: " + r->filename);
                        r->self->append_log("[Upload] ✗ Failed: " + r->filename);
                        
                        // Show error notification
                        proton::NotificationManager::getInstance().notify(
                            "Upload Failed",
                            "Failed to upload " + r->filename,
                            proton::NotificationType::ERROR
                        );
                    }
                    
                    // Always refresh cloud browser to show new files
                    r->self->invalidate_cloud_cache();
                    r->self->refresh_cloud_files_async(true);  // Force refresh
                    
                    delete r;
                    return G_SOURCE_REMOVE;
                }, result);
            }
        }).detach();
    }
}

// Button handlers
void AppWindow::poll_status() {
    // Check if any sync job timers are active
    std::string output = exec_command("systemctl --user list-units 'proton-drive-job-*.timer' --state=active --no-legend 2>/dev/null | wc -l");
    
    // Trim whitespace
    output.erase(0, output.find_first_not_of(" \t\n"));
    output.erase(output.find_last_not_of(" \t\n") + 1);
    
    int active_timers = 0;
    try {
        active_timers = std::stoi(output);
    } catch (...) {}
    
    update_service_status(active_timers > 0);
    
    // Update index status in Settings if visible
    if (index_status_label_) {
        auto& file_index = FileIndex::getInstance();
        auto stats = file_index.get_stats();
        
        std::string status_text;
        if (stats.is_indexing) {
            status_text = "Status: Indexing... " + std::to_string(stats.index_progress_percent) + "%";
        } else if (stats.total_files == 0 && stats.total_folders == 0) {
            status_text = "Status: Empty - click Rebuild Index to populate";
        } else {
            status_text = "Status: " + std::to_string(stats.total_files) + " files, " + 
                         std::to_string(stats.total_folders) + " folders indexed";
            if (!stats.last_full_index.empty()) {
                status_text += " (last: " + stats.last_full_index.substr(0, 16) + ")";
            }
        }
        gtk_label_set_text(GTK_LABEL(index_status_label_), status_text.c_str());
    }
}

// View switching
void AppWindow::show_drop_zone() {
    // Already showing drop zone as main content
}

void AppWindow::show_cloud_browser() {
    // TODO: Switch to cloud browser view in stack
}

// Public methods
void AppWindow::show() {
    if (window_) {
        gtk_widget_set_visible(window_, TRUE);
        gtk_window_present(GTK_WINDOW(window_));
    }
}

void AppWindow::hide() {
    if (window_) {
        gtk_widget_set_visible(window_, FALSE);
    }
}

void AppWindow::toggle_visibility() {
    if (is_visible()) {
        hide();
    } else {
        show();
    }
}

bool AppWindow::is_visible() const {
    return window_ && gtk_widget_get_visible(window_);
}

void AppWindow::set_toggle_callback(std::function<void()> callback) {
    toggle_callback_ = callback;
}

void AppWindow::shutdown() {
    Logger::info("[AppWindow] Shutting down...");
    
    // Stop cloud monitoring thread
    if (cloud_monitoring_active_.load()) {
        Logger::info("[AppWindow] Stopping cloud monitor thread...");
        stop_cloud_monitoring();
        Logger::info("[AppWindow] Cloud monitor stopped");
    }
    
    Logger::info("[AppWindow] Shutdown complete");
}

void AppWindow::update_sync_progress(const std::string& file, 
                                      double fraction,
                                      const std::string& speed,
                                      const std::string& transferred) {
    // Progress overlay disabled - sync activity shows in main panel instead
    (void)file; (void)fraction; (void)speed; (void)transferred;
}

void AppWindow::append_log(const std::string& message) {
    if (!logs_view_) return;
    
    // Add timestamp
    time_t now = time(nullptr);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", localtime(&now));
    
    std::string full_msg = std::string(time_buf) + " " + message + "\n";
    
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(logs_view_));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, full_msg.c_str(), -1);
    
    // Auto-scroll
    gtk_text_buffer_get_end_iter(buffer, &end);
    GtkTextMark* mark = gtk_text_buffer_create_mark(buffer, nullptr, &end, FALSE);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(logs_view_), mark, 0.0, TRUE, 0.0, 1.0);
    
    Logger::info("[AppWindow] " + message);
}

void AppWindow::show_toast(const std::string& message, int duration_ms) {
    if (!toast_revealer_ || !toast_label_) return;
    
    gtk_label_set_text(GTK_LABEL(toast_label_), message.c_str());
    gtk_revealer_set_reveal_child(GTK_REVEALER(toast_revealer_), TRUE);
    
    // Cancel any existing auto-dismiss timer
    if (toast_timeout_id_ > 0) {
        g_source_remove(toast_timeout_id_);
        toast_timeout_id_ = 0;
    }
    
    // Auto-dismiss after duration
    struct ToastData { AppWindow* self; };
    auto* td = new ToastData{this};
    toast_timeout_id_ = g_timeout_add(duration_ms, +[](gpointer user_data) -> gboolean {
        auto* d = static_cast<ToastData*>(user_data);
        if (d->self->toast_revealer_) {
            gtk_revealer_set_reveal_child(GTK_REVEALER(d->self->toast_revealer_), FALSE);
        }
        d->self->toast_timeout_id_ = 0;
        delete d;
        return G_SOURCE_REMOVE;
    }, td);
    
    Logger::debug("[Toast] " + message);
}


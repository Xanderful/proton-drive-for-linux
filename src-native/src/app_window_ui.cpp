// app_window_ui.cpp - UI building methods for AppWindow
// Extracted from app_window.cpp to reduce file size

#include "app_window.hpp"
#include "app_window_helpers.hpp"
#include "sync_manager.hpp"
#include "logger.hpp"
#include <fstream>
#include <filesystem>
#include <gtk/gtk.h>

namespace fs = std::filesystem;
using namespace AppWindowHelpers;

void AppWindow::build_drop_zone() {
    // Create the drop zone container
    drop_zone_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_add_css_class(drop_zone_, "drop-zone");
    
    gtk_widget_set_margin_start(drop_zone_, 40);
    gtk_widget_set_margin_end(drop_zone_, 40);
    gtk_widget_set_margin_top(drop_zone_, 40);
    gtk_widget_set_margin_bottom(drop_zone_, 40);
    gtk_widget_set_valign(drop_zone_, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(drop_zone_, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(drop_zone_, TRUE);
    gtk_widget_set_vexpand(drop_zone_, TRUE);
    
    // Center content
    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_widget_set_halign(content, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(content, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(content, 40);
    gtk_widget_set_margin_bottom(content, 40);
    gtk_widget_set_margin_start(content, 40);
    gtk_widget_set_margin_end(content, 40);
    gtk_box_append(GTK_BOX(drop_zone_), content);
    
    // Upload icon
    GtkWidget* icon = gtk_image_new_from_icon_name("folder-download-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 80);
    gtk_widget_add_css_class(icon, "drop-zone-icon");
    gtk_box_append(GTK_BOX(content), icon);
    
    // Main label
    GtkWidget* label = gtk_label_new("Drop files here to upload");
    gtk_widget_add_css_class(label, "drop-zone-label");
    gtk_box_append(GTK_BOX(content), label);
    
    // Sub label
    GtkWidget* sublabel = gtk_label_new("or drag folders to sync with Proton Drive");
    gtk_widget_add_css_class(sublabel, "drop-zone-sublabel");
    gtk_box_append(GTK_BOX(content), sublabel);
    
    // Browse button
    GtkWidget* browse_btn = gtk_button_new_with_label("Browse Cloud Files");
    gtk_widget_add_css_class(browse_btn, "browse-button");
    gtk_widget_set_margin_top(browse_btn, 20);
    g_signal_connect(browse_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<AppWindow*>(data)->on_browse_clicked();
    }), this);
    gtk_box_append(GTK_BOX(content), browse_btn);
    
    // Set up GTK4 drag-and-drop using GtkDropTarget
    GtkDropTarget* drop_target = gtk_drop_target_new(G_TYPE_INVALID, GDK_ACTION_COPY);
    
    // Accept file lists
    GType types[] = { GDK_TYPE_FILE_LIST };
    gtk_drop_target_set_gtypes(drop_target, types, 1);
    
    // Connect signals
    g_signal_connect(drop_target, "enter", G_CALLBACK(+[](GtkDropTarget*, gdouble, gdouble, gpointer data) -> GdkDragAction {
        auto* self = static_cast<AppWindow*>(data);
        self->set_drop_zone_highlight(true);
        return GDK_ACTION_COPY;
    }), this);
    
    g_signal_connect(drop_target, "leave", G_CALLBACK(+[](GtkDropTarget*, gpointer data) {
        auto* self = static_cast<AppWindow*>(data);
        self->set_drop_zone_highlight(false);
    }), this);
    
    g_signal_connect(drop_target, "drop", G_CALLBACK(+[](GtkDropTarget*, const GValue* value, gdouble, gdouble, gpointer data) -> gboolean {
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
    
    gtk_widget_add_controller(drop_zone_, GTK_EVENT_CONTROLLER(drop_target));
}

// build_progress_overlay() removed - user requested no syncing overlay

void AppWindow::build_logs_panel() {
    // Just create logs_scroll_ and logs_view_ without adding to any container
    // They will be used directly in the logs stack page
    
    logs_scroll_ = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(logs_scroll_), 
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(logs_scroll_, TRUE);
    
    logs_view_ = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(logs_view_), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(logs_view_), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(logs_view_), 10);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(logs_view_), 10);
    gtk_widget_add_css_class(logs_view_, "logs-text");
    
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(logs_scroll_), logs_view_);
}

// Static callback for cloud browser right-click
static void on_cloud_right_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)gesture;
    Logger::info("[CloudBrowser] *** RIGHT-CLICK DETECTED *** at x=" + std::to_string(x) + ", y=" + std::to_string(y));
    auto* self = static_cast<AppWindow*>(data);
    
    GtkWidget* listbox = self->get_cloud_tree();
    
    // For GtkListBox, we need to find which row is at this position
    // Try using pick() to find the widget at these coordinates
    GtkWidget* picked = gtk_widget_pick(listbox, x, y, GTK_PICK_DEFAULT);
    Logger::info("[CloudBrowser] Picked widget: " + std::string(picked ? G_OBJECT_TYPE_NAME(picked) : "null"));
    
    // Walk up to find the GtkListBoxRow
    GtkListBoxRow* row = nullptr;
    GtkWidget* current = picked;
    while (current && current != listbox) {
        if (GTK_IS_LIST_BOX_ROW(current)) {
            row = GTK_LIST_BOX_ROW(current);
            Logger::info("[CloudBrowser] Found GtkListBoxRow in ancestor chain");
            break;
        }
        current = gtk_widget_get_parent(current);
    }
    
    if (row) {
        Logger::info("[CloudBrowser] Row found, checking path data...");
        gtk_list_box_select_row(GTK_LIST_BOX(listbox), row);
        const char* path = static_cast<const char*>(g_object_get_data(G_OBJECT(row), "path"));
        gboolean is_dir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "is_dir"));
        if (path) {
            Logger::info("[CloudBrowser] Showing context menu for: " + std::string(path));
            self->show_cloud_context_menu(path, is_dir, x, y);
        } else {
            Logger::warn("[CloudBrowser] Row has no path data");
        }
    } else {
        Logger::info("[CloudBrowser] No row found at click position");
    }
}

// Static callback for local browser right-click
// Note: Currently unused but kept for future local file context menu support
[[maybe_unused]] static void on_local_right_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)gesture;
    Logger::info("[LocalBrowser] *** RIGHT-CLICK DETECTED *** at x=" + std::to_string(x) + ", y=" + std::to_string(y));
    auto* self = static_cast<AppWindow*>(data);
    
    GtkWidget* listbox = self->get_local_tree();
    
    // Use pick() to find the widget at these coordinates
    GtkWidget* picked = gtk_widget_pick(listbox, x, y, GTK_PICK_DEFAULT);
    
    // Walk up to find the GtkListBoxRow
    GtkListBoxRow* row = nullptr;
    GtkWidget* current = picked;
    while (current && current != listbox) {
        if (GTK_IS_LIST_BOX_ROW(current)) {
            row = GTK_LIST_BOX_ROW(current);
            break;
        }
        current = gtk_widget_get_parent(current);
    }

    if (row) {
        Logger::info("[LocalBrowser] Found row, showing context menu");
        gtk_list_box_select_row(GTK_LIST_BOX(listbox), row);
        const char* path = static_cast<const char*>(g_object_get_data(G_OBJECT(row), "path"));
        gboolean is_dir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "is_dir"));
        if (path) {
            self->show_local_context_menu(path, is_dir, x, y);
        }
    } else {
        Logger::info("[LocalBrowser] No row found at click position");
    }
}

void AppWindow::build_file_browser() {
    file_browser_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(file_browser_box_, "file-browser");
    gtk_widget_set_hexpand(file_browser_box_, TRUE);
    gtk_widget_set_vexpand(file_browser_box_, TRUE);
    
    // Page header
    GtkWidget* browser_header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(browser_header, "content-header");
    
    // Title row with search
    GtkWidget* title_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    
    GtkWidget* title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_hexpand(title_box, TRUE);
    
    GtkWidget* browser_title = gtk_label_new("Cloud Browser");
    gtk_widget_add_css_class(browser_title, "content-title");
    gtk_label_set_xalign(GTK_LABEL(browser_title), 0);
    gtk_box_append(GTK_BOX(title_box), browser_title);
    
    GtkWidget* browser_subtitle = gtk_label_new("Browse and manage your Proton Drive files");
    gtk_widget_add_css_class(browser_subtitle, "content-subtitle");
    gtk_label_set_xalign(GTK_LABEL(browser_subtitle), 0);
    gtk_box_append(GTK_BOX(title_box), browser_subtitle);
    
    gtk_box_append(GTK_BOX(title_row), title_box);
    
    // Search entry
    search_entry_ = gtk_search_entry_new();
    gtk_widget_set_size_request(search_entry_, 250, -1);
    gtk_widget_set_valign(search_entry_, GTK_ALIGN_CENTER);  // Center vertically
    gtk_widget_add_css_class(search_entry_, "search-entry");
    g_object_set(search_entry_, "placeholder-text", "Search all files...", NULL);
    g_signal_connect(search_entry_, "search-changed", G_CALLBACK(+[](GtkSearchEntry* entry, gpointer data) {
        auto* self = static_cast<AppWindow*>(data);
        const char* text = gtk_editable_get_text(GTK_EDITABLE(entry));
        if (text && strlen(text) >= 2) {
            self->perform_search(text);
        } else if (!text || strlen(text) == 0) {
            // Clear search, show normal view
            self->refresh_cloud_files();
        }
    }), this);
    g_signal_connect(search_entry_, "stop-search", G_CALLBACK(+[](GtkSearchEntry*, gpointer data) {
        auto* self = static_cast<AppWindow*>(data);
        self->refresh_cloud_files();
    }), this);
    gtk_box_append(GTK_BOX(title_row), search_entry_);
    
    gtk_box_append(GTK_BOX(browser_header), title_row);
    gtk_box_append(GTK_BOX(file_browser_box_), browser_header);
    
    // Stack switcher for Cloud/Local views
    browser_stack_ = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(browser_stack_), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    
    // No stack switcher needed - only cloud view now
    // Local sync status is shown via badges in cloud browser
    
    // ===== CLOUD FILES VIEW =====
    GtkWidget* cloud_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    
    // Cloud path bar
    GtkWidget* cloud_path_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(cloud_path_box, "path-bar");
    
    GtkWidget* cloud_home_btn = gtk_button_new_from_icon_name("go-home-symbolic");
    gtk_widget_set_tooltip_text(cloud_home_btn, "Go to root");
    g_signal_connect(cloud_home_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<AppWindow*>(data)->navigate_cloud("/");
    }), this);
    gtk_box_append(GTK_BOX(cloud_path_box), cloud_home_btn);
    
    GtkWidget* cloud_up_btn = gtk_button_new_from_icon_name("go-up-symbolic");
    gtk_widget_set_tooltip_text(cloud_up_btn, "Go up");
    g_signal_connect(cloud_up_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* self = static_cast<AppWindow*>(data);
        fs::path p(self->current_cloud_path_);
        if (p.has_parent_path() && self->current_cloud_path_ != "/") {
            self->navigate_cloud(p.parent_path().string());
        }
    }), this);
    gtk_box_append(GTK_BOX(cloud_path_box), cloud_up_btn);
    
    path_bar_ = gtk_label_new("/");
    gtk_label_set_ellipsize(GTK_LABEL(path_bar_), PANGO_ELLIPSIZE_START);
    gtk_widget_set_hexpand(path_bar_, TRUE);
    gtk_label_set_xalign(GTK_LABEL(path_bar_), 0);
    gtk_box_append(GTK_BOX(cloud_path_box), path_bar_);
    
    GtkWidget* cloud_refresh_btn = gtk_button_new_from_icon_name("view-refresh-symbolic");
    gtk_widget_set_tooltip_text(cloud_refresh_btn, "Refresh (force reload from cloud)");
    g_signal_connect(cloud_refresh_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<AppWindow*>(data)->refresh_cloud_files_async(true);  // Force refresh
    }), this);
    gtk_box_append(GTK_BOX(cloud_path_box), cloud_refresh_btn);
    
    // New folder button
    GtkWidget* new_folder_btn = gtk_button_new_from_icon_name("folder-new-symbolic");
    gtk_widget_set_tooltip_text(new_folder_btn, "Create new folder");
    g_signal_connect(new_folder_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto* self = static_cast<AppWindow*>(data);
        
        // Create dialog to prompt for folder name
        GtkWidget* dialog = gtk_window_new();
        gtk_window_set_title(GTK_WINDOW(dialog), "Create New Folder");
        gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(self->get_window()));
        gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
        gtk_window_set_default_size(GTK_WINDOW(dialog), 400, -1);
        
        GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_widget_set_margin_start(vbox, 20);
        gtk_widget_set_margin_end(vbox, 20);
        gtk_widget_set_margin_top(vbox, 20);
        gtk_widget_set_margin_bottom(vbox, 20);
        gtk_window_set_child(GTK_WINDOW(dialog), vbox);
        
        GtkWidget* label = gtk_label_new("Enter folder name:");
        gtk_label_set_xalign(GTK_LABEL(label), 0);
        gtk_box_append(GTK_BOX(vbox), label);
        
        GtkWidget* entry = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "New Folder");
        gtk_box_append(GTK_BOX(vbox), entry);
        
        GtkWidget* btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_halign(btn_box, GTK_ALIGN_END);
        
        GtkWidget* cancel_btn = gtk_button_new_with_label("Cancel");
        g_signal_connect_swapped(cancel_btn, "clicked", G_CALLBACK(gtk_window_destroy), dialog);
        gtk_box_append(GTK_BOX(btn_box), cancel_btn);
        
        GtkWidget* create_btn = gtk_button_new_with_label("Create");
        gtk_widget_add_css_class(create_btn, "suggested-action");
        
        // Store current cloud path for callback
        char* path_copy = g_strdup(self->current_cloud_path_.c_str());
        g_object_set_data_full(G_OBJECT(create_btn), "cloud_path", path_copy, g_free);
        g_object_set_data(G_OBJECT(create_btn), "app_window", self);
        g_object_set_data(G_OBJECT(create_btn), "entry", entry);
        
        g_signal_connect(create_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer) {
            auto* self = static_cast<AppWindow*>(g_object_get_data(G_OBJECT(btn), "app_window"));
            auto* entry = GTK_ENTRY(g_object_get_data(G_OBJECT(btn), "entry"));
            const char* path = static_cast<const char*>(g_object_get_data(G_OBJECT(btn), "cloud_path"));
            
            std::string folder_name = gtk_editable_get_text(GTK_EDITABLE(entry));
            if (folder_name.empty()) {
                folder_name = "New Folder";
            }
            
            // Build full cloud path
            std::string current_path = path ? path : "/";
            if (current_path.back() != '/') current_path += "/";
            std::string new_folder_path = current_path + folder_name;
            
            // Create folder using rclone mkdir
            std::string cmd = "mkdir proton:" + AppWindowHelpers::shell_escape(new_folder_path);
            self->append_log("[CloudBrowser] Creating folder: " + new_folder_path);
            
            std::thread([self, cmd, new_folder_path]() {
                std::string output = AppWindowHelpers::exec_rclone(cmd);
                
                g_idle_add(+[](gpointer data) -> gboolean {
                    auto* self = static_cast<AppWindow*>(data);
                    self->append_log("[CloudBrowser] Folder created successfully");
                    self->refresh_cloud_files_async(true);
                    return G_SOURCE_REMOVE;
                }, self);
            }).detach();
            
            GtkWidget* dlg = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_WINDOW);
            if (dlg) gtk_window_destroy(GTK_WINDOW(dlg));
        }), nullptr);
        
        gtk_box_append(GTK_BOX(btn_box), create_btn);
        gtk_box_append(GTK_BOX(vbox), btn_box);
        
        gtk_window_present(GTK_WINDOW(dialog));
    }), this);
    gtk_box_append(GTK_BOX(cloud_path_box), new_folder_btn);
    
    gtk_box_append(GTK_BOX(cloud_box), cloud_path_box);
    
    // Cloud file list
    cloud_scroll_ = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(cloud_scroll_),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(cloud_scroll_, TRUE);
    
    cloud_tree_ = gtk_list_box_new();
    gtk_widget_add_css_class(cloud_tree_, "file-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(cloud_tree_), GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(cloud_tree_), FALSE);  // Double-click to activate
    g_signal_connect(cloud_tree_, "row-activated", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer data) {
        auto* self = static_cast<AppWindow*>(data);
        const char* path = static_cast<const char*>(g_object_get_data(G_OBJECT(row), "path"));
        gboolean is_dir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "is_dir"));
        if (path) {
            self->on_cloud_row_activated(path, is_dir);
        }
    }), this);
    
    // Add right-click context menu support (GTK4 style)
    // Attach gesture to the listbox itself, not the scroll window
    GtkGesture* cloud_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(cloud_click), 3); // Right-click
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(cloud_click), GTK_PHASE_BUBBLE);
    g_signal_connect(cloud_click, "pressed", G_CALLBACK(on_cloud_right_click), this);
    gtk_widget_add_controller(cloud_tree_, GTK_EVENT_CONTROLLER(cloud_click));
    Logger::info("[CloudBrowser] Right-click gesture attached to cloud_tree_ (listbox)");
    
    // Also add a debug gesture for ANY click to verify gestures work
    GtkGesture* cloud_any_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(cloud_any_click), 0); // Any button
    g_signal_connect(cloud_any_click, "pressed", G_CALLBACK(+[](GtkGestureClick*, gint n_press, gdouble x, gdouble y, gpointer) {
        Logger::debug("[CloudBrowser] ANY click: n=" + std::to_string(n_press) + " x=" + std::to_string(x) + " y=" + std::to_string(y));
    }), nullptr);
    gtk_widget_add_controller(cloud_tree_, GTK_EVENT_CONTROLLER(cloud_any_click));
    Logger::info("[CloudBrowser] Debug any-click gesture attached to cloud_tree_");
    
    // Enable drag-and-drop on cloud list (accept local files/folders)
    GtkDropTarget* cloud_drop = gtk_drop_target_new(G_TYPE_INVALID, GDK_ACTION_COPY);
    GType drop_types[] = { GDK_TYPE_FILE_LIST };
    gtk_drop_target_set_gtypes(cloud_drop, drop_types, 1);
    g_signal_connect(cloud_drop, "drop", G_CALLBACK(+[](GtkDropTarget*, const GValue* value, gdouble /*x*/, gdouble y, gpointer data) -> gboolean {
        auto* self = static_cast<AppWindow*>(data);
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
            // Check if dropped on a folder row
            GtkListBoxRow* row = gtk_list_box_get_row_at_y(GTK_LIST_BOX(self->cloud_tree_), (int)y);
            std::string target_folder = self->current_cloud_path_;
            
            if (row) {
                gboolean is_dir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "is_dir"));
                const char* row_path = static_cast<const char*>(g_object_get_data(G_OBJECT(row), "path"));
                if (is_dir && row_path) {
                    target_folder = row_path;
                    Logger::info("[CloudDrop] Dropped on folder: " + target_folder);
                }
            }
            
            self->handle_cloud_drop(paths, target_folder);
        }
        return TRUE;
    }), this);
    gtk_widget_add_controller(cloud_tree_, GTK_EVENT_CONTROLLER(cloud_drop));
    
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(cloud_scroll_), cloud_tree_);
    gtk_box_append(GTK_BOX(cloud_box), cloud_scroll_);
    
    gtk_stack_add_titled(GTK_STACK(browser_stack_), cloud_box, "cloud", "☁ Cloud");
    
    // Local view removed - sync status shown via badges in cloud browser
    // Keep local_tree_ and local_scroll_ as null-initialized for safety
    local_tree_ = nullptr;
    local_scroll_ = nullptr;
    
    gtk_box_append(GTK_BOX(file_browser_box_), browser_stack_);
}

void AppWindow::build_sync_activity_panel() {
    sync_activity_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(sync_activity_box_, "sync-activity-panel");
    gtk_widget_set_margin_start(sync_activity_box_, 20);
    gtk_widget_set_margin_end(sync_activity_box_, 20);
    gtk_widget_set_margin_bottom(sync_activity_box_, 10);
    
    // Collapsible header row with toggle button
    GtkWidget* header_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(header_row, "sync-activity-header-row");
    
    // Expander arrow button
    GtkWidget* expand_btn = gtk_button_new_from_icon_name("pan-down-symbolic");
    gtk_widget_add_css_class(expand_btn, "flat");
    gtk_widget_add_css_class(expand_btn, "circular");
    gtk_widget_set_tooltip_text(expand_btn, "Collapse/Expand");
    gtk_box_append(GTK_BOX(header_row), expand_btn);
    
    GtkWidget* sync_icon = gtk_image_new_from_icon_name("emblem-synchronizing-symbolic");
    gtk_box_append(GTK_BOX(header_row), sync_icon);
    
    GtkWidget* header_label = gtk_label_new("Active Syncs");
    gtk_widget_add_css_class(header_label, "sync-activity-header");
    gtk_widget_set_hexpand(header_label, TRUE);
    gtk_label_set_xalign(GTK_LABEL(header_label), 0);
    gtk_box_append(GTK_BOX(header_row), header_label);
    
    gtk_box_append(GTK_BOX(sync_activity_box_), header_row);
    
    // List for sync items - make it expandable, wrapped in a revealer for collapse
    sync_activity_list_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_vexpand(sync_activity_list_, FALSE);
    
    GtkWidget* revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), 200);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), TRUE);
    gtk_revealer_set_child(GTK_REVEALER(revealer), sync_activity_list_);
    gtk_box_append(GTK_BOX(sync_activity_box_), revealer);
    
    // Connect expand button to toggle revealer
    g_object_set_data(G_OBJECT(expand_btn), "revealer", revealer);
    g_signal_connect(expand_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer) {
        GtkWidget* rev = GTK_WIDGET(g_object_get_data(G_OBJECT(btn), "revealer"));
        if (!rev) return;
        gboolean revealed = gtk_revealer_get_reveal_child(GTK_REVEALER(rev));
        gtk_revealer_set_reveal_child(GTK_REVEALER(rev), !revealed);
        // Update button icon
        gtk_button_set_icon_name(btn, revealed ? "pan-end-symbolic" : "pan-down-symbolic");
    }), nullptr);
    
    // No sync label (shown when nothing is syncing)
    no_sync_label_ = gtk_label_new("No active syncs");
    gtk_widget_add_css_class(no_sync_label_, "no-sync-label");
    gtk_widget_set_name(no_sync_label_, "no_sync_label");
    gtk_label_set_xalign(GTK_LABEL(no_sync_label_), 0);
    gtk_box_append(GTK_BOX(sync_activity_list_), no_sync_label_);
}

void AppWindow::build_transfer_popup() {
    // Create the transfer popup as a floating panel in bottom-right corner
    transfer_popup_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(transfer_popup_, "transfer-popup");
    gtk_widget_set_size_request(transfer_popup_, 380, -1);
    gtk_widget_set_halign(transfer_popup_, GTK_ALIGN_END);
    gtk_widget_set_valign(transfer_popup_, GTK_ALIGN_END);
    gtk_widget_set_margin_end(transfer_popup_, 20);
    gtk_widget_set_margin_bottom(transfer_popup_, 20);
    gtk_widget_set_visible(transfer_popup_, FALSE);
    
    // Header row with title and close button
    GtkWidget* header_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(header_row, "transfer-popup-header");
    gtk_widget_set_margin_start(header_row, 12);
    gtk_widget_set_margin_end(header_row, 8);
    gtk_widget_set_margin_top(header_row, 10);
    gtk_widget_set_margin_bottom(header_row, 6);
    
    transfer_header_label_ = gtk_label_new("Transferring...");
    gtk_widget_add_css_class(transfer_header_label_, "transfer-popup-title");
    gtk_widget_set_hexpand(transfer_header_label_, TRUE);
    gtk_label_set_xalign(GTK_LABEL(transfer_header_label_), 0);
    gtk_box_append(GTK_BOX(header_row), transfer_header_label_);
    
    // Collapse/expand button
    GtkWidget* collapse_btn = gtk_button_new_from_icon_name("pan-down-symbolic");
    gtk_widget_add_css_class(collapse_btn, "flat");
    gtk_widget_add_css_class(collapse_btn, "circular");
    gtk_widget_set_tooltip_text(collapse_btn, "Collapse/Expand");
    gtk_box_append(GTK_BOX(header_row), collapse_btn);
    
    // Close button
    GtkWidget* close_btn = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_add_css_class(close_btn, "flat");
    gtk_widget_add_css_class(close_btn, "circular");
    gtk_widget_set_tooltip_text(close_btn, "Close");
    g_signal_connect(close_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<AppWindow*>(data)->hide_transfer_popup();
    }), this);
    gtk_box_append(GTK_BOX(header_row), close_btn);
    
    gtk_box_append(GTK_BOX(transfer_popup_), header_row);
    
    // Overall progress bar
    transfer_progress_bar_ = gtk_progress_bar_new();
    gtk_widget_add_css_class(transfer_progress_bar_, "transfer-progress");
    gtk_widget_set_margin_start(transfer_progress_bar_, 12);
    gtk_widget_set_margin_end(transfer_progress_bar_, 12);
    gtk_widget_set_margin_bottom(transfer_progress_bar_, 8);
    gtk_box_append(GTK_BOX(transfer_popup_), transfer_progress_bar_);
    
    // Scrollable list for transfer items
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), 
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 200);
    gtk_widget_set_vexpand(scroll, FALSE);
    
    transfer_list_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(transfer_list_, 8);
    gtk_widget_set_margin_end(transfer_list_, 8);
    gtk_widget_set_margin_bottom(transfer_list_, 8);
    
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), transfer_list_);
    gtk_box_append(GTK_BOX(transfer_popup_), scroll);
    
    // Create revealer for collapse/expand
    GtkWidget* revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), 200);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), TRUE);
    
    // Move scroll to revealer content
    g_object_ref(scroll);
    gtk_box_remove(GTK_BOX(transfer_popup_), scroll);
    gtk_revealer_set_child(GTK_REVEALER(revealer), scroll);
    g_object_unref(scroll);
    gtk_box_append(GTK_BOX(transfer_popup_), revealer);
    
    // Connect collapse button to toggle revealer
    g_object_set_data(G_OBJECT(collapse_btn), "revealer", revealer);
    g_signal_connect(collapse_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer) {
        GtkWidget* rev = GTK_WIDGET(g_object_get_data(G_OBJECT(btn), "revealer"));
        if (!rev) return;
        gboolean revealed = gtk_revealer_get_reveal_child(GTK_REVEALER(rev));
        gtk_revealer_set_reveal_child(GTK_REVEALER(rev), !revealed);
        gtk_button_set_icon_name(btn, revealed ? "pan-up-symbolic" : "pan-down-symbolic");
    }), nullptr);
    
    Logger::info("[TransferPopup] Built transfer progress popup");
}

void AppWindow::show_transfer_popup() {
    if (!transfer_popup_) {
        build_transfer_popup();
        // Add to the main overlay if not already added
        if (drop_zone_overlay_) {
            gtk_overlay_add_overlay(GTK_OVERLAY(drop_zone_overlay_), transfer_popup_);
        }
    }
    gtk_widget_set_visible(transfer_popup_, TRUE);
    transfer_popup_visible_ = true;
    Logger::info("[TransferPopup] Showing transfer popup");
}

void AppWindow::hide_transfer_popup() {
    if (transfer_popup_) {
        gtk_widget_set_visible(transfer_popup_, FALSE);
    }
    transfer_popup_visible_ = false;
    // Clear completed transfers when closing
    active_transfers_.clear();
    completed_transfers_ = 0;
    total_transfers_ = 0;
    Logger::info("[TransferPopup] Hiding transfer popup");
}

void AppWindow::add_transfer_item(const std::string& filename, bool is_upload) {
    // Show transfer popup when first transfer is added
    if (active_transfers_.empty()) {
        show_transfer_popup();
    }
    
    TransferItem item;
    item.filename = filename;
    item.status = is_upload ? "Uploading" : "Downloading";
    item.progress = 0.0;
    item.bytes_transferred = 0;
    item.total_bytes = 0;
    item.speed = "";
    
    active_transfers_.push_back(item);
    total_transfers_++;
    
    show_transfer_popup();
    refresh_transfer_list();
    
    Logger::info("[TransferPopup] Added transfer: " + filename + " (" + item.status + ")");
}

void AppWindow::update_transfer_progress(const std::string& filename, double progress, 
                                          const std::string& speed, int64_t bytes_transferred) {
    for (auto& item : active_transfers_) {
        if (item.filename == filename && item.status != "Completed" && item.status != "Failed") {
            item.progress = progress;
            item.speed = speed;
            item.bytes_transferred = bytes_transferred;
            break;
        }
    }
    
    refresh_transfer_list();
}

void AppWindow::complete_transfer_item(const std::string& filename, bool success) {
    for (auto& item : active_transfers_) {
        if (item.filename == filename && item.status != "Completed" && item.status != "Failed") {
            item.status = success ? "Completed" : "Failed";
            item.progress = success ? 1.0 : item.progress;
            completed_transfers_++;
            break;
        }
    }
    
    refresh_transfer_list();
    
    // Check if all transfers are done
    bool all_done = true;
    for (const auto& item : active_transfers_) {
        if (item.status != "Completed" && item.status != "Failed") {
            all_done = false;
            break;
        }
    }
    
    if (all_done && !active_transfers_.empty()) {
        // Update header to show "Done"
        if (transfer_header_label_) {
            std::string header = "Done";
            if (total_transfers_ > 0) {
                header += " - " + std::to_string(completed_transfers_) + " of " + 
                          std::to_string(total_transfers_) + " transfers completed";
            }
            gtk_label_set_text(GTK_LABEL(transfer_header_label_), header.c_str());
        }
        if (transfer_progress_bar_) {
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(transfer_progress_bar_), 1.0);
        }
    }
    
    Logger::info("[TransferPopup] Completed transfer: " + filename + " (success=" + (success ? "yes" : "no") + ")");
}

void AppWindow::refresh_transfer_list() {
    if (!transfer_list_) return;
    
    // Clear existing items
    while (TRUE) {
        GtkWidget* child = gtk_widget_get_first_child(transfer_list_);
        if (!child) break;
        gtk_box_remove(GTK_BOX(transfer_list_), child);
    }
    
    // Update header
    if (transfer_header_label_) {
        std::string header;
        int active_count = 0;
        for (const auto& item : active_transfers_) {
            if (item.status != "Completed" && item.status != "Failed") {
                active_count++;
            }
        }
        
        if (active_count > 0) {
            header = "Transferring...";
            if (total_transfers_ > 0) {
                header = std::to_string(completed_transfers_) + " of " + 
                         std::to_string(total_transfers_) + " transfers completed";
            }
        } else if (!active_transfers_.empty()) {
            header = "Done";
        }
        gtk_label_set_text(GTK_LABEL(transfer_header_label_), header.c_str());
    }
    
    // Update overall progress
    if (transfer_progress_bar_ && total_transfers_ > 0) {
        double overall = 0.0;
        for (const auto& item : active_transfers_) {
            overall += item.progress;
        }
        overall /= active_transfers_.size();
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(transfer_progress_bar_), overall);
    }
    
    // Add items
    for (const auto& item : active_transfers_) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_add_css_class(row, "transfer-item");
        gtk_widget_set_margin_top(row, 4);
        gtk_widget_set_margin_bottom(row, 4);
        
        // Status icon
        const char* icon_name = "document-save-symbolic";
        if (item.status == "Completed") {
            icon_name = "emblem-ok-symbolic";
        } else if (item.status == "Failed") {
            icon_name = "dialog-error-symbolic";
        } else if (item.status == "Downloading") {
            icon_name = "folder-download-symbolic";
        }
        GtkWidget* icon = gtk_image_new_from_icon_name(icon_name);
        gtk_box_append(GTK_BOX(row), icon);
        
        // Info box
        GtkWidget* info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_hexpand(info_box, TRUE);
        
        // Filename with ellipsis
        GtkWidget* name_label = gtk_label_new(item.filename.c_str());
        gtk_label_set_xalign(GTK_LABEL(name_label), 0);
        gtk_label_set_ellipsize(GTK_LABEL(name_label), PANGO_ELLIPSIZE_MIDDLE);
        gtk_label_set_max_width_chars(GTK_LABEL(name_label), 30);
        gtk_box_append(GTK_BOX(info_box), name_label);
        
        // Status/speed text
        std::string status_text = item.status;
        if (!item.speed.empty() && item.status != "Completed" && item.status != "Failed") {
            status_text += " • " + item.speed;
        }
        GtkWidget* status_label = gtk_label_new(status_text.c_str());
        gtk_label_set_xalign(GTK_LABEL(status_label), 0);
        gtk_widget_add_css_class(status_label, "dim-label");
        gtk_widget_add_css_class(status_label, "caption");
        gtk_box_append(GTK_BOX(info_box), status_label);
        
        gtk_box_append(GTK_BOX(row), info_box);
        
        // Share button for completed items
        if (item.status == "Completed") {
            GtkWidget* share_btn = gtk_button_new_with_label("Share");
            gtk_widget_add_css_class(share_btn, "pill");
            gtk_widget_add_css_class(share_btn, "suggested-action");
            gtk_box_append(GTK_BOX(row), share_btn);
        }
        
        gtk_box_append(GTK_BOX(transfer_list_), row);
    }
}


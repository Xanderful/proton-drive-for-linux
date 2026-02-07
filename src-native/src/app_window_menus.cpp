// app_window_menus.cpp - Context menu implementations for AppWindow
// Extracted from app_window.cpp to reduce file size

#include "app_window.hpp"
#include "app_window_helpers.hpp"
#include "sync_manager.hpp"
#include "sync_job_metadata.hpp"
#include "trash_manager.hpp"
#include "notifications.hpp"
#include "logger.hpp"
#include <fstream>
#include <filesystem>
#include <thread>
#include <array>
#include <gtk/gtk.h>

namespace fs = std::filesystem;
using namespace AppWindowHelpers;

void AppWindow::show_cloud_context_menu(const std::string& path, bool is_dir, double x, double y) {
    Logger::info("[ContextMenu] show_cloud_context_menu called for: " + path + 
                 " is_dir=" + std::to_string(is_dir) + " x=" + std::to_string(x) + " y=" + std::to_string(y));
    
    // Create popover menu
    GtkWidget* popover = gtk_popover_new();
    if (!popover) {
        Logger::error("[ContextMenu] Failed to create popover!");
        return;
    }
    Logger::info("[ContextMenu] Created popover, setting parent to cloud_tree_");
    gtk_widget_set_parent(popover, cloud_tree_);
    
    GtkWidget* menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(menu_box, 8);
    gtk_widget_set_margin_end(menu_box, 8);
    gtk_widget_set_margin_top(menu_box, 8);
    gtk_widget_set_margin_bottom(menu_box, 8);
    gtk_popover_set_child(GTK_POPOVER(popover), menu_box);
    
    std::string filename = fs::path(path).filename().string();
    
    if (is_dir) {
        // Open folder
        GtkWidget* open_btn = gtk_button_new_with_label("Open");
        gtk_widget_add_css_class(open_btn, "flat-button");
        char* path_copy = g_strdup(path.c_str());
        g_object_set_data_full(G_OBJECT(open_btn), "path", path_copy, g_free);
        g_signal_connect(open_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer data) {
            const char* p = static_cast<const char*>(g_object_get_data(G_OBJECT(btn), "path"));
            static_cast<AppWindow*>(data)->navigate_cloud(p);
            GtkWidget* pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
            if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
        }), this);
        gtk_box_append(GTK_BOX(menu_box), open_btn);
    }
    
    // Separator
    gtk_box_append(GTK_BOX(menu_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    
    // Sync to local (folders only)
    if (is_dir) {
        GtkWidget* sync_btn = gtk_button_new_with_label("Sync to Custom Path...");
        gtk_widget_add_css_class(sync_btn, "flat-button");
        char* sync_path = g_strdup(path.c_str());
        g_object_set_data_full(G_OBJECT(sync_btn), "path", sync_path, g_free);
        g_signal_connect(sync_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer data) {
            const char* p = static_cast<const char*>(g_object_get_data(G_OBJECT(btn), "path"));
            auto* self = static_cast<AppWindow*>(data);
            std::string cloud_path = std::string(p);
            
            // Popup down the context menu
            GtkWidget* pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
            if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
            
            // Show folder chooser dialog to pick local destination
            GtkFileDialog* dialog = gtk_file_dialog_new();
            gtk_file_dialog_set_title(dialog, "Select Local Folder to Sync With");
            
            // Store cloud path for callback
            char* cloud_path_copy = g_strdup(cloud_path.c_str());
            g_object_set_data_full(G_OBJECT(dialog), "cloud_path", cloud_path_copy, g_free);
            g_object_set_data(G_OBJECT(dialog), "app_window", self);
            
            gtk_file_dialog_select_folder(dialog, GTK_WINDOW(self->get_window()), nullptr,
                [](GObject* source, GAsyncResult* result, gpointer) {
                    GtkFileDialog* dlg = GTK_FILE_DIALOG(source);
                    const char* cloud_p = static_cast<const char*>(g_object_get_data(G_OBJECT(dlg), "cloud_path"));
                    auto* app_win = static_cast<AppWindow*>(g_object_get_data(G_OBJECT(dlg), "app_window"));
                    
                    GFile* file = gtk_file_dialog_select_folder_finish(dlg, result, nullptr);
                    if (file && cloud_p && app_win) {
                        char* local_path = g_file_get_path(file);
                        if (local_path) {
                            app_win->append_log("[Sync] Syncing cloud: " + std::string(cloud_p) + " → local: " + std::string(local_path));                            app_win->show_toast("\U0001f504 Sync started: " + std::string(cloud_p));
                            proton::NotificationManager::getInstance().notify(
                                "Sync Started",
                                std::string(cloud_p) + " \u2192 " + std::string(local_path),
                                proton::NotificationType::INFO);                            SyncManager::getInstance().execute_sync_to_local_async(cloud_p, local_path, "bisync");
                            g_free(local_path);
                        }
                        g_object_unref(file);
                    }
                }, nullptr);
            
            g_object_unref(dialog);
        }), this);
        gtk_box_append(GTK_BOX(menu_box), sync_btn);
        
        // Quick sync to default location
        const char* home_dir = getenv("HOME");
        std::string default_local = std::string(home_dir ? home_dir : "/tmp") + "/ProtonDrive/" + filename;
        std::string quick_label = "Quick Sync to ~/ProtonDrive/" + filename;
        GtkWidget* quick_sync_btn = gtk_button_new_with_label(quick_label.c_str());
        gtk_widget_add_css_class(quick_sync_btn, "flat-button");
        char* quick_path = g_strdup(path.c_str());
        g_object_set_data_full(G_OBJECT(quick_sync_btn), "path", quick_path, g_free);
        g_signal_connect(quick_sync_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer data) {
            const char* p = static_cast<const char*>(g_object_get_data(G_OBJECT(btn), "path"));
            auto* self = static_cast<AppWindow*>(data);
            self->append_log("[Sync] Quick sync initiated for: " + std::string(p));
            self->show_toast("\U0001f504 Quick sync started: " + std::string(p));
            proton::NotificationManager::getInstance().notify(
                "Quick Sync Started",
                "Syncing: " + std::string(p),
                proton::NotificationType::INFO);
            SyncManager::getInstance().show_sync_to_local_dialog(p);
            GtkWidget* pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
            if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
        }), this);
        gtk_box_append(GTK_BOX(menu_box), quick_sync_btn);
        
        // Check if this folder is already synced - if so, show additional options
        auto& registry = SyncJobRegistry::getInstance();
        auto all_jobs = registry.getAllJobs();
        std::string synced_local_path;
        std::string synced_job_id;
        
        std::string check_path = path;
        if (!check_path.empty() && check_path.front() != '/') {
            check_path = "/" + check_path;
        }
        
        for (const auto& job : all_jobs) {
            std::string remote_check = job.remote_path;
            if (!remote_check.empty() && remote_check.front() != '/') {
                remote_check = "/" + remote_check;
            }
            
            bool path_matches = (check_path == remote_check);
            bool is_subdir = (!remote_check.empty() &&
                              check_path.length() > remote_check.length() &&
                              check_path.substr(0, remote_check.length()) == remote_check &&
                              check_path[remote_check.length()] == '/');
            
            if (path_matches || is_subdir) {
                synced_job_id = job.job_id;
                if (path_matches) {
                    synced_local_path = job.local_path;
                } else {
                    // Subdir - calculate relative path
                    std::string relative = check_path.substr(remote_check.length());
                    if (!relative.empty() && relative.front() == '/') {
                        relative = relative.substr(1);
                    }
                    synced_local_path = job.local_path + "/" + relative;
                }
                break;
            }
        }
        
        // If folder is synced, show Open Local Folder and Force Sync options
        if (!synced_local_path.empty() && safe_exists(synced_local_path)) {
            gtk_box_append(GTK_BOX(menu_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
            
            // Open Local Folder button
            GtkWidget* open_local_btn = gtk_button_new_with_label("Open Local Folder");
            gtk_widget_add_css_class(open_local_btn, "flat-button");
            char* local_p = g_strdup(synced_local_path.c_str());
            g_object_set_data_full(G_OBJECT(open_local_btn), "local_path", local_p, g_free);
            g_signal_connect(open_local_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer data) {
                const char* lp = static_cast<const char*>(g_object_get_data(G_OBJECT(btn), "local_path"));
                auto* self = static_cast<AppWindow*>(data);
                std::string cmd = "xdg-open " + shell_escape(lp) + " &";
                run_system(cmd);
                self->append_log("[Open] Local folder: " + std::string(lp));
                GtkWidget* pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
                if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
            }), this);
            gtk_box_append(GTK_BOX(menu_box), open_local_btn);
            
            // Force Sync button
            struct SyncInfo {
                std::string local_path;
                std::string remote_path;
                AppWindow* app;
            };
            auto* sync_info = new SyncInfo{synced_local_path, path, this};
            
            GtkWidget* force_sync_btn = gtk_button_new_with_label("Force Sync Now");
            gtk_widget_add_css_class(force_sync_btn, "flat-button");
            gtk_widget_add_css_class(force_sync_btn, "suggested-action");
            g_object_set_data_full(G_OBJECT(force_sync_btn), "sync_info", sync_info,
                +[](gpointer p) { delete static_cast<SyncInfo*>(p); });
            g_signal_connect(force_sync_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer) {
                auto* info = static_cast<SyncInfo*>(g_object_get_data(G_OBJECT(btn), "sync_info"));
                if (!info) return;
                
                info->app->append_log("[Sync] Force sync: " + info->remote_path + " <-> " + info->local_path);
                info->app->show_toast("\U0001f504 Force sync started: " + info->remote_path);
                Logger::info("[Sync] Force sync triggered: " + info->remote_path);
                
                // Run bisync with --resync flag for force sync (shell-escaped)
                std::string rclone_path = AppWindowHelpers::get_rclone_path();
                std::string cmd = shell_escape(rclone_path) + " bisync " +
                                  shell_escape("proton:" + info->remote_path) + " " +
                                  shell_escape(info->local_path) + " --resync --verbose --rc &";
                run_system(cmd);
                
                proton::NotificationManager::getInstance().notify(
                    "Force Sync Started",
                    "Resyncing: " + info->remote_path,
                    proton::NotificationType::INFO);
                
                GtkWidget* pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
                if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
            }), nullptr);
            gtk_box_append(GTK_BOX(menu_box), force_sync_btn);
        }
    }
    
    // For files: Check if this file is in a synced directory and show Open Local File option
    if (!is_dir) {
        // Check if this file is under any synced folder
        auto& registry = SyncJobRegistry::getInstance();
        auto all_jobs = registry.getAllJobs();
        std::string synced_local_file_path;
        
        std::string check_path = path;
        if (!check_path.empty() && check_path.front() != '/') {
            check_path = "/" + check_path;
        }
        
        for (const auto& job : all_jobs) {
            std::string remote_check = job.remote_path;
            if (!remote_check.empty() && remote_check.front() != '/') {
                remote_check = "/" + remote_check;
            }
            
            // Check if this file is under the synced remote folder
            bool is_under_sync = (!remote_check.empty() &&
                              check_path.length() > remote_check.length() &&
                              check_path.substr(0, remote_check.length()) == remote_check &&
                              check_path[remote_check.length()] == '/');
            
            if (is_under_sync) {
                // Calculate the local path for this specific file
                std::string relative = check_path.substr(remote_check.length());
                if (!relative.empty() && relative.front() == '/') {
                    relative = relative.substr(1);
                }
                synced_local_file_path = job.local_path + "/" + relative;
                break;
            }
        }
        
        // If file is synced locally, show Open options
        if (!synced_local_file_path.empty() && safe_exists(synced_local_file_path)) {
            GtkWidget* open_file_btn = gtk_button_new_with_label("Open Local File");
            gtk_widget_add_css_class(open_file_btn, "flat-button");
            char* local_fp = g_strdup(synced_local_file_path.c_str());
            g_object_set_data_full(G_OBJECT(open_file_btn), "local_path", local_fp, g_free);
            g_signal_connect(open_file_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer data) {
                const char* lp = static_cast<const char*>(g_object_get_data(G_OBJECT(btn), "local_path"));
                auto* self = static_cast<AppWindow*>(data);
                std::string cmd = "xdg-open " + shell_escape(lp) + " &";
                run_system(cmd);
                self->append_log("[Open] Local file: " + std::string(lp));
                GtkWidget* pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
                if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
            }), this);
            gtk_box_append(GTK_BOX(menu_box), open_file_btn);
            
            // Also add "Open Containing Folder" option
            std::string parent_dir = fs::path(synced_local_file_path).parent_path().string();
            GtkWidget* open_folder_btn = gtk_button_new_with_label("Open Containing Folder");
            gtk_widget_add_css_class(open_folder_btn, "flat-button");
            char* folder_p = g_strdup(parent_dir.c_str());
            g_object_set_data_full(G_OBJECT(open_folder_btn), "local_path", folder_p, g_free);
            g_signal_connect(open_folder_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer data) {
                const char* lp = static_cast<const char*>(g_object_get_data(G_OBJECT(btn), "local_path"));
                auto* self = static_cast<AppWindow*>(data);
                std::string cmd = "xdg-open " + shell_escape(lp) + " &";
                run_system(cmd);
                self->append_log("[Open] Folder: " + std::string(lp));
                GtkWidget* pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
                if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
            }), this);
            gtk_box_append(GTK_BOX(menu_box), open_folder_btn);
            
            gtk_box_append(GTK_BOX(menu_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
        }
    }
    
    // Download to ~/Downloads (files only - one-time download, not sync)
    if (!is_dir) {
        std::string download_label = "Download to ~/Downloads/" + filename;
        GtkWidget* download_btn = gtk_button_new_with_label(download_label.c_str());
        gtk_widget_add_css_class(download_btn, "flat-button");
        char* dl_path = g_strdup(path.c_str());
        g_object_set_data_full(G_OBJECT(download_btn), "path", dl_path, g_free);
        g_signal_connect(download_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer data) {
            const char* p = static_cast<const char*>(g_object_get_data(G_OBJECT(btn), "path"));
            auto* self = static_cast<AppWindow*>(data);
            std::string dest = std::string(getenv("HOME")) + "/Downloads/" + fs::path(p).filename().string();
            std::string cmd = "rclone copy " + shell_escape("proton:" + std::string(p)) + " " + 
                             shell_escape(fs::path(dest).parent_path().string() + "/") + " &";
            self->append_log("[Download] " + std::string(p) + " → ~/Downloads/");
            run_system(cmd);
            GtkWidget* pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
            if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
        }), this);
        gtk_box_append(GTK_BOX(menu_box), download_btn);
    }
    
    // Separator
    gtk_box_append(GTK_BOX(menu_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    
    // Version History (for files only)
    if (!is_dir) {
        GtkWidget* version_btn = gtk_button_new_with_label("Version History...");
        gtk_widget_add_css_class(version_btn, "flat-button");
        char* ver_path = g_strdup(path.c_str());
        g_object_set_data_full(G_OBJECT(version_btn), "path", ver_path, g_free);
        g_signal_connect(version_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer data) {
            const char* p = static_cast<const char*>(g_object_get_data(G_OBJECT(btn), "path"));
            auto* self = static_cast<AppWindow*>(data);
            GtkWidget* pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
            if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
            
            // Show version history dialog
            self->show_version_history_dialog(std::string(p));
        }), this);
        gtk_box_append(GTK_BOX(menu_box), version_btn);
    }
    
    // Copy cloud path
    GtkWidget* copy_btn = gtk_button_new_with_label("Copy Path");
    gtk_widget_add_css_class(copy_btn, "flat-button");
    char* copy_path = g_strdup(path.c_str());
    g_object_set_data_full(G_OBJECT(copy_btn), "path", copy_path, g_free);
    g_signal_connect(copy_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer) {
        const char* p = static_cast<const char*>(g_object_get_data(G_OBJECT(btn), "path"));
        GdkClipboard* clipboard = gdk_display_get_clipboard(gdk_display_get_default());
        gdk_clipboard_set_text(clipboard, p);
        GtkWidget* pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
        if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
    }), nullptr);
    gtk_box_append(GTK_BOX(menu_box), copy_btn);
    
    // Separator before destructive actions
    gtk_box_append(GTK_BOX(menu_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    
    // Remove from Local (keep in cloud) - only show if there's a local sync
    // Check if this path has a corresponding local sync
    const char* home = getenv("HOME");
    std::string jobs_dir = home ? std::string(home) + "/.config/proton-drive/jobs" : "";
    std::string local_item_path;
    
    if (!jobs_dir.empty() && safe_exists(jobs_dir)) {
        std::error_code ec_iter;
        for (const auto& entry : fs::directory_iterator(jobs_dir, ec_iter)) {
            if (ec_iter) break;
            if (entry.path().extension() == ".conf") {
                std::ifstream f(entry.path());
                std::string line, remote_path, local_path_conf;
                while (std::getline(f, line)) {
                    if (line.find("REMOTE_PATH=") == 0) {
                        remote_path = line.substr(12);
                        if (!remote_path.empty() && remote_path.front() == '"') remote_path = remote_path.substr(1);
                        if (!remote_path.empty() && remote_path.back() == '"') remote_path.pop_back();
                        if (remote_path.find("proton:") == 0) remote_path = remote_path.substr(7);
                    } else if (line.find("LOCAL_PATH=") == 0) {
                        local_path_conf = line.substr(11);
                        if (!local_path_conf.empty() && local_path_conf.front() == '"') local_path_conf = local_path_conf.substr(1);
                        if (!local_path_conf.empty() && local_path_conf.back() == '"') local_path_conf.pop_back();
                    }
                }
                // Check if cloud path matches
                std::string cloud_check = path;
                if (!cloud_check.empty() && cloud_check.front() != '/') {
                    cloud_check = "/" + cloud_check;
                }
                std::string remote_check = remote_path;
                if (!remote_check.empty() && remote_check.front() != '/') {
                    remote_check = "/" + remote_check;
                }

                bool path_matches = (cloud_check == remote_check);
                bool is_subdir = (!remote_check.empty() &&
                                  cloud_check.length() > remote_check.length() &&
                                  cloud_check.substr(0, remote_check.length()) == remote_check &&
                                  cloud_check[remote_check.length()] == '/');

                if ((path_matches || is_subdir) && !local_path_conf.empty()) {
                    std::string relative = cloud_check.substr(remote_check.length());
                    if (!relative.empty() && relative.front() == '/') {
                        relative = relative.substr(1);
                    }
                    if (relative.empty()) {
                        local_item_path = local_path_conf;
                    } else {
                        local_item_path = local_path_conf + "/" + relative;
                    }
                    break;
                }
            }
        }
    }

    if (!local_item_path.empty() && safe_exists(local_item_path)) {
        GtkWidget* remove_local_btn = gtk_button_new_with_label("Remove from Local (keep in cloud)");
        gtk_widget_add_css_class(remove_local_btn, "flat-button");
        char* local_p = g_strdup(local_item_path.c_str());
        g_object_set_data_full(G_OBJECT(remove_local_btn), "local_path", local_p, g_free);
        g_signal_connect(remove_local_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer data) {
            const char* lp = static_cast<const char*>(g_object_get_data(G_OBJECT(btn), "local_path"));
            auto* self = static_cast<AppWindow*>(data);
            
            // Move to trash using TrashManager
            std::string local_path_str(lp);
            if (proton::TrashManager::getInstance().move_to_trash(local_path_str)) {
                self->append_log("[Remove] Moved to trash: " + local_path_str + " (cloud copy preserved)");
                
                // Show notification to user
                proton::NotificationManager::getInstance().notify(
                    "File Removed",
                    "Moved to trash: " + fs::path(local_path_str).filename().string(),
                    proton::NotificationType::INFO
                );
            } else {
                self->append_log("[Remove] Failed to move to trash: " + local_path_str);
                proton::NotificationManager::getInstance().notify(
                    "Remove Failed",
                    "Failed to remove: " + fs::path(local_path_str).filename().string(),
                    proton::NotificationType::ERROR
                );
            }
            
            // Clear cloud cache and refresh everything properly
            self->invalidate_cloud_cache();
            self->refresh_sync_jobs();
            self->refresh_cloud_files_async(true);  // Force refresh from rclone
            
            GtkWidget* pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
            if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
        }), this);
        gtk_box_append(GTK_BOX(menu_box), remove_local_btn);
    }
    
    // Delete from cloud with confirmation
    GtkWidget* delete_btn = gtk_button_new_with_label("Delete from Cloud");
    gtk_widget_add_css_class(delete_btn, "flat-button");
    gtk_widget_add_css_class(delete_btn, "destructive-action");
    
    // Store data needed for delete operation
    struct CloudDeleteInfo {
        std::string path;
        bool is_folder;
        AppWindow* app;
    };
    auto* del_info = new CloudDeleteInfo{path, is_dir, this};
    g_object_set_data_full(G_OBJECT(delete_btn), "del_info", del_info,
        +[](gpointer p) { delete static_cast<CloudDeleteInfo*>(p); });
    
    g_signal_connect(delete_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer) {
        auto* info = static_cast<CloudDeleteInfo*>(g_object_get_data(G_OBJECT(btn), "del_info"));
        if (!info) return;
        
        // Close the popover first
        GtkWidget* pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
        if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
        
        // Call the confirmation helper
        info->app->show_cloud_delete_confirm(info->path, info->is_folder);
    }), nullptr);
    gtk_box_append(GTK_BOX(menu_box), delete_btn);
    
    // Position and show
    GdkRectangle rect = {(int)x, (int)y, 1, 1};
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    Logger::info("[ContextMenu] Cloud popover at rect: " + std::to_string((int)x) + "," + std::to_string((int)y));
    gtk_popover_popup(GTK_POPOVER(popover));
    Logger::info("[ContextMenu] Cloud popover shown");
}

// Context menu for local files (GTK4 popover style)
void AppWindow::show_local_context_menu(const std::string& path, bool is_dir, double x, double y) {
    (void)x; (void)y;
    
    GtkWidget* popover = gtk_popover_new();
    gtk_widget_set_parent(popover, local_tree_);
    
    GtkWidget* menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(menu_box, 8);
    gtk_widget_set_margin_end(menu_box, 8);
    gtk_widget_set_margin_top(menu_box, 8);
    gtk_widget_set_margin_bottom(menu_box, 8);
    gtk_popover_set_child(GTK_POPOVER(popover), menu_box);
    
    std::string filename = fs::path(path).filename().string();
    
    // Open with default app
    GtkWidget* open_btn = gtk_button_new_with_label(is_dir ? "Open Folder" : "Open File");
    gtk_widget_add_css_class(open_btn, "flat-button");
    char* open_path = g_strdup(path.c_str());
    g_object_set_data_full(G_OBJECT(open_btn), "path", open_path, g_free);
    g_signal_connect(open_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer data) {
        const char* p = static_cast<const char*>(g_object_get_data(G_OBJECT(btn), "path"));
        auto* self = static_cast<AppWindow*>(data);
        std::string cmd = "xdg-open " + shell_escape(p) + " &";
        run_system(cmd);
        self->append_log("[Open] " + std::string(p));
        GtkWidget* pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
        if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
    }), this);
    gtk_box_append(GTK_BOX(menu_box), open_btn);
    
    if (is_dir) {
        // Navigate into folder
        GtkWidget* nav_btn = gtk_button_new_with_label("Browse");
        gtk_widget_add_css_class(nav_btn, "flat-button");
        char* nav_path = g_strdup(path.c_str());
        g_object_set_data_full(G_OBJECT(nav_btn), "path", nav_path, g_free);
        g_signal_connect(nav_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer data) {
            const char* p = static_cast<const char*>(g_object_get_data(G_OBJECT(btn), "path"));
            static_cast<AppWindow*>(data)->navigate_local(p);
            GtkWidget* pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
            if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
        }), this);
        gtk_box_append(GTK_BOX(menu_box), nav_btn);
    }
    
    // Separator
    gtk_box_append(GTK_BOX(menu_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    
    // Show in file manager
    GtkWidget* show_btn = gtk_button_new_with_label("Show in File Manager");
    gtk_widget_add_css_class(show_btn, "flat-button");
    char* show_path = g_strdup(path.c_str());
    g_object_set_data_full(G_OBJECT(show_btn), "path", show_path, g_free);
    g_signal_connect(show_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer data) {
        const char* p = static_cast<const char*>(g_object_get_data(G_OBJECT(btn), "path"));
        auto* self = static_cast<AppWindow*>(data);
        std::string parent = fs::path(p).parent_path().string();
        std::string cmd = "xdg-open " + shell_escape(parent) + " &";
        run_system(cmd);
        self->append_log("[Show] " + parent);
        GtkWidget* pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
        if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
    }), this);
    gtk_box_append(GTK_BOX(menu_box), show_btn);
    
    // Copy path
    GtkWidget* copy_btn = gtk_button_new_with_label("Copy Path");
    gtk_widget_add_css_class(copy_btn, "flat-button");
    char* copy_path = g_strdup(path.c_str());
    g_object_set_data_full(G_OBJECT(copy_btn), "path", copy_path, g_free);
    g_signal_connect(copy_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer) {
        const char* p = static_cast<const char*>(g_object_get_data(G_OBJECT(btn), "path"));
        GdkClipboard* clipboard = gdk_display_get_clipboard(gdk_display_get_default());
        gdk_clipboard_set_text(clipboard, p);
        GtkWidget* pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
        if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
    }), nullptr);
    gtk_box_append(GTK_BOX(menu_box), copy_btn);
    
    // Separator
    gtk_box_append(GTK_BOX(menu_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    
    // Remove from Cloud (keep local) - check if this is a synced file
    // Try to find the corresponding cloud path
    const char* home_env = getenv("HOME");
    std::string jobs_dir_local = home_env ? std::string(home_env) + "/.config/proton-drive/jobs" : "";
    std::string cloud_path_for_local;
    
    if (!jobs_dir_local.empty() && safe_exists(jobs_dir_local)) {
        std::error_code ec_iter2;
        for (const auto& entry : fs::directory_iterator(jobs_dir_local, ec_iter2)) {
            if (ec_iter2) break;
            if (entry.path().extension() == ".conf") {
                std::ifstream f(entry.path());
                std::string line, remote_path_conf, local_path_conf;
                while (std::getline(f, line)) {
                    if (line.find("REMOTE_PATH=") == 0) {
                        remote_path_conf = line.substr(12);
                        if (!remote_path_conf.empty() && remote_path_conf.front() == '"') remote_path_conf = remote_path_conf.substr(1);
                        if (!remote_path_conf.empty() && remote_path_conf.back() == '"') remote_path_conf.pop_back();
                    } else if (line.find("LOCAL_PATH=") == 0) {
                        local_path_conf = line.substr(11);
                        if (!local_path_conf.empty() && local_path_conf.front() == '"') local_path_conf = local_path_conf.substr(1);
                        if (!local_path_conf.empty() && local_path_conf.back() == '"') local_path_conf.pop_back();
                    }
                }
                // Check if this local path is within a synced folder
                if (!local_path_conf.empty() && path.find(local_path_conf) == 0) {
                    // Calculate the relative path from the sync root
                    std::string relative = path.substr(local_path_conf.length());
                    // Remove proton: prefix if present
                    if (remote_path_conf.find("proton:") == 0) remote_path_conf = remote_path_conf.substr(7);
                    cloud_path_for_local = remote_path_conf + relative;
                    break;
                }
            }
        }
    }
    
    if (!cloud_path_for_local.empty()) {
        GtkWidget* remove_cloud_btn = gtk_button_new_with_label("Remove from Cloud (keep local)");
        gtk_widget_add_css_class(remove_cloud_btn, "flat-button");
        gtk_widget_add_css_class(remove_cloud_btn, "destructive-action");
        char* cloud_p = g_strdup(cloud_path_for_local.c_str());
        g_object_set_data_full(G_OBJECT(remove_cloud_btn), "cloud_path", cloud_p, g_free);
        g_signal_connect(remove_cloud_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer data) {
            const char* cp = static_cast<const char*>(g_object_get_data(G_OBJECT(btn), "cloud_path"));
            auto* self = static_cast<AppWindow*>(data);
            std::string cmd = "rclone delete " + shell_escape("proton:" + std::string(cp)) + " &";
            self->append_log("[Remove] Removing from cloud: proton:" + std::string(cp) + " (local copy preserved)");
            run_system(cmd);
            self->refresh_cloud_files();
            GtkWidget* pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
            if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
        }), this);
        gtk_box_append(GTK_BOX(menu_box), remove_cloud_btn);
    }
    
    // Move to trash
    GtkWidget* trash_btn = gtk_button_new_with_label("Move to Trash");
    gtk_widget_add_css_class(trash_btn, "flat-button");
    gtk_widget_add_css_class(trash_btn, "destructive-action");
    char* trash_path = g_strdup(path.c_str());
    g_object_set_data_full(G_OBJECT(trash_btn), "path", trash_path, g_free);
    g_signal_connect(trash_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer data) {
        const char* p = static_cast<const char*>(g_object_get_data(G_OBJECT(btn), "path"));
        auto* self = static_cast<AppWindow*>(data);
        std::string cmd = "gio trash \"" + std::string(p) + "\"";
        if (run_system(cmd) == 0) {
            self->append_log("[Trash] Moved to trash: " + std::string(p));
            self->refresh_local_files();
        } else {
            self->append_log("[Error] Failed to move to trash: " + std::string(p));
        }
        GtkWidget* pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
        if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
    }), this);
    gtk_box_append(GTK_BOX(menu_box), trash_btn);
    
    // Position and show
    GdkRectangle rect = {(int)x, (int)y, 1, 1};
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    gtk_popover_popup(GTK_POPOVER(popover));
}

// ============================================================================
// VERSION HISTORY DIALOG
// ============================================================================

void AppWindow::show_version_history_dialog(const std::string& cloud_path) {
    append_log("[Version] Fetching version history for: " + cloud_path);
    
    // Create dialog
    GtkWidget* dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Version History");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(window_));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 500);
    
    GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(main_box, 20);
    gtk_widget_set_margin_end(main_box, 20);
    gtk_widget_set_margin_top(main_box, 20);
    gtk_widget_set_margin_bottom(main_box, 20);
    gtk_window_set_child(GTK_WINDOW(dialog), main_box);
    
    // File name label
    std::string filename = fs::path(cloud_path).filename().string();
    GtkWidget* title = gtk_label_new(("Versions of: " + filename).c_str());
    gtk_widget_add_css_class(title, "title-4");
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_box_append(GTK_BOX(main_box), title);
    
    // Loading spinner
    GtkWidget* spinner = gtk_spinner_new();
    gtk_spinner_start(GTK_SPINNER(spinner));
    gtk_widget_set_halign(spinner, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(main_box), spinner);
    
    GtkWidget* loading_label = gtk_label_new("Loading version history...");
    gtk_widget_set_halign(loading_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(main_box), loading_label);
    
    // Close button
    GtkWidget* button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(button_box, GTK_ALIGN_END);
    GtkWidget* close_btn = gtk_button_new_with_label("Close");
    g_signal_connect_swapped(close_btn, "clicked", G_CALLBACK(gtk_window_destroy), dialog);
    gtk_box_append(GTK_BOX(button_box), close_btn);
    gtk_box_append(GTK_BOX(main_box), button_box);
    
    gtk_window_present(GTK_WINDOW(dialog));
    
    // Fetch versions in background
    struct VersionData {
        AppWindow* window;
        GtkWidget* dialog;
        GtkWidget* main_box;
        GtkWidget* spinner;
        GtkWidget* loading_label;
        std::string cloud_path;
    };
    
    VersionData* vdata = new VersionData{this, dialog, main_box, spinner, loading_label, cloud_path};
    
    std::thread([vdata]() {
        // Run rclone backend versions command
        std::string cmd = "rclone backend versions proton: remote:" + shell_escape(vdata->cloud_path) + " 2>&1";
        std::array<char, 256> buffer;
        std::string result;
        
        FILE* pipe = popen(cmd.c_str(), "r");
        if (pipe) {
            while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
                result += buffer.data();
            }
            pclose(pipe);
        }
        
        // Update UI on main thread
        g_idle_add([](gpointer data) -> gboolean {
            VersionData* vd = static_cast<VersionData*>(data);
            
            // Remove spinner and loading label
            gtk_box_remove(GTK_BOX(vd->main_box), vd->spinner);
            gtk_box_remove(GTK_BOX(vd->main_box), vd->loading_label);
            
            // Add scrolled window for versions
            GtkWidget* scrolled = gtk_scrolled_window_new();
            gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                          GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
            gtk_widget_set_vexpand(scrolled, TRUE);
            
            GtkWidget* versions_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
            gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), versions_box);
            
            // Note: This is a simplified implementation
            // In production, parse rclone JSON output properly
            GtkWidget* info = gtk_label_new("Version history feature uses rclone backend.\nRestore functionality coming soon.");
            gtk_label_set_wrap(GTK_LABEL(info), TRUE);
            gtk_widget_add_css_class(info, "dim-label");
            gtk_box_append(GTK_BOX(versions_box), info);
            
            gtk_box_insert_child_after(GTK_BOX(vd->main_box), scrolled,
                                      gtk_widget_get_first_child(vd->main_box));
            
            delete vd;
            return G_SOURCE_REMOVE;
        }, vdata);
    }).detach();
}

void AppWindow::show_cloud_delete_confirm(const std::string& path, bool is_folder) {
    GtkWidget* dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Confirm Delete");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(window_));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 420, -1);
    
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);
    gtk_window_set_child(GTK_WINDOW(dialog), vbox);
    
    // Warning icon + title
    GtkWidget* header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* icon = gtk_image_new_from_icon_name("dialog-warning-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 48);
    gtk_widget_add_css_class(icon, "warning");
    gtk_box_append(GTK_BOX(header_box), icon);
    
    GtkWidget* title = gtk_label_new(NULL);
    std::string type_str = is_folder ? "folder" : "file";
    gtk_label_set_markup(GTK_LABEL(title), ("<b>Delete " + type_str + " from cloud?</b>").c_str());
    gtk_box_append(GTK_BOX(header_box), title);
    gtk_box_append(GTK_BOX(vbox), header_box);
    
    // File/folder name
    std::string filename = fs::path(path).filename().string();
    GtkWidget* name_label = gtk_label_new(filename.c_str());
    gtk_widget_add_css_class(name_label, "monospace");
    gtk_label_set_xalign(GTK_LABEL(name_label), 0);
    gtk_box_append(GTK_BOX(vbox), name_label);
    
    GtkWidget* warning = gtk_label_new("This action cannot be undone. The item will be permanently deleted from Proton Drive.");
    gtk_label_set_wrap(GTK_LABEL(warning), TRUE);
    gtk_label_set_xalign(GTK_LABEL(warning), 0);
    gtk_widget_add_css_class(warning, "dim-label");
    gtk_box_append(GTK_BOX(vbox), warning);
    
    GtkWidget* action_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(action_box, GTK_ALIGN_END);
    gtk_widget_set_margin_top(action_box, 12);
    
    GtkWidget* cancel_btn = gtk_button_new_with_label("Cancel");
    GtkWidget* confirm_btn = gtk_button_new_with_label("Delete");
    gtk_widget_add_css_class(confirm_btn, "destructive-action");
    
    gtk_box_append(GTK_BOX(action_box), cancel_btn);
    gtk_box_append(GTK_BOX(action_box), confirm_btn);
    gtk_box_append(GTK_BOX(vbox), action_box);
    
    // Store data for callbacks
    struct DeleteData {
        AppWindow* self;
        std::string path;
        bool is_folder;
        GtkWidget* dialog;
    };
    auto* del_data = new DeleteData{this, path, is_folder, dialog};
    g_object_set_data_full(G_OBJECT(dialog), "del-data", del_data, +[](gpointer p) {
        delete static_cast<DeleteData*>(p);
    });
    
    g_signal_connect_swapped(cancel_btn, "clicked", G_CALLBACK(gtk_window_destroy), dialog);
    g_signal_connect(confirm_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        auto* del = static_cast<DeleteData*>(g_object_get_data(G_OBJECT(user_data), "del-data"));
        if (!del) return;
        
        std::string rclone_path = get_rclone_path();
        std::string cloud_path = "proton:" + del->path;
        std::string cmd;
        if (del->is_folder) {
            cmd = rclone_path + " purge \"" + cloud_path + "\" 2>&1";
        } else {
            cmd = rclone_path + " deletefile \"" + cloud_path + "\" 2>&1";
        }
        
        Logger::info("[CloudBrowser] Deleting: " + cloud_path);
        del->self->append_log("[Delete] Removing from cloud: " + del->path);
        
        auto* self_ptr = del->self;
        std::thread([cmd, self_ptr]() {
            int ret = system(cmd.c_str());
            (void)ret;
            g_idle_add(+[](gpointer data) -> gboolean {
                auto* self = static_cast<AppWindow*>(data);
                self->refresh_cloud_files();
                return G_SOURCE_REMOVE;
            }, self_ptr);
        }).detach();
        
        gtk_window_destroy(GTK_WINDOW(del->dialog));
    }), dialog);
    
    gtk_window_present(GTK_WINDOW(dialog));
}

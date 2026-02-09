// app_window_sync.cpp - Profile and sync job management for AppWindow
// Extracted from app_window.cpp to reduce file size

#include "app_window.hpp"
#include "app_window_helpers.hpp"
#include "sync_manager.hpp"
#include "device_identity.hpp"
#include "notifications.hpp"
#include "logger.hpp"
#include <fstream>
#include <filesystem>
#include <set>
#include <algorithm>
#include <gtk/gtk.h>

namespace fs = std::filesystem;
using namespace AppWindowHelpers;

void AppWindow::refresh_profiles() {
    if (!profiles_list_) return;
    
    // Clear existing - GTK4 way
    // Clear using proper GTK4 method
    while (TRUE) {
        GtkListBoxRow* row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(profiles_list_), 0);
        if (!row) break;
        gtk_list_box_remove(GTK_LIST_BOX(profiles_list_), GTK_WIDGET(row));
    }
    
    // Load profiles from rclone using correct path
    try {
        std::string remotes = exec_rclone("listremotes");
        Logger::info("[Profiles] rclone listremotes returned: " + remotes);
        std::stringstream ss(remotes);
        std::string remote;
        
        while (std::getline(ss, remote)) {
            if (remote.empty()) continue;
            if (remote.back() == ':') remote.pop_back();
            
            GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            gtk_widget_add_css_class(row, "menu-item-row");
            gtk_widget_set_margin_start(row, 5);
            gtk_widget_set_margin_end(row, 5);
            gtk_widget_set_margin_top(row, 2);
            gtk_widget_set_margin_bottom(row, 2);
            
            GtkWidget* icon = gtk_image_new_from_icon_name("network-server-symbolic");
            GtkWidget* label = gtk_label_new(remote.c_str());
            gtk_label_set_xalign(GTK_LABEL(label), 0);
            gtk_widget_set_hexpand(label, TRUE);
            
            // Edit button
            GtkWidget* edit_btn = gtk_button_new_from_icon_name("document-edit-symbolic");
            gtk_widget_add_css_class(edit_btn, "flat-button");
            gtk_widget_set_tooltip_text(edit_btn, "Edit profile (update 2FA, password)");
            g_object_set_data_full(G_OBJECT(edit_btn), "profile_name", g_strdup(remote.c_str()), g_free);
            g_signal_connect(edit_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer) {
                const char* name = static_cast<const char*>(g_object_get_data(G_OBJECT(btn), "profile_name"));
                if (name) {
                    AppWindow::getInstance().on_edit_profile_clicked(std::string(name));
                }
            }), nullptr);
            
            // Delete button
            GtkWidget* del_btn = gtk_button_new_from_icon_name("user-trash-symbolic");
            gtk_widget_add_css_class(del_btn, "flat-button");
            gtk_widget_set_tooltip_text(del_btn, "Delete profile");
            g_object_set_data_full(G_OBJECT(del_btn), "profile_name", g_strdup(remote.c_str()), g_free);
            g_signal_connect(del_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer) {
                const char* name = static_cast<const char*>(g_object_get_data(G_OBJECT(btn), "profile_name"));
                if (name) {
                    run_rclone("config delete " + std::string(name));
                    AppWindow::getInstance().refresh_profiles();
                }
            }), nullptr);
            
            gtk_box_append(GTK_BOX(row), icon);
            gtk_box_append(GTK_BOX(row), label);
            gtk_box_append(GTK_BOX(row), edit_btn);
            gtk_box_append(GTK_BOX(row), del_btn);
            
            gtk_list_box_append(GTK_LIST_BOX(profiles_list_), row);
        }
    } catch (...) {
        append_log("[Error] Failed to load profiles");
    }

    update_setup_state();
}

void AppWindow::update_setup_state() {
    bool missing = !has_rclone_profile();
    if (setup_banner_) {
        gtk_widget_set_visible(setup_banner_, missing);
    }
    if (setup_guide_) {
        gtk_widget_set_visible(setup_guide_, missing);
    }
    if (missing != setup_missing_) {
        setup_missing_ = missing;
        if (missing) {
            Logger::info("[Setup] Proton profile not configured");
        } else {
            Logger::info("[Setup] Proton profile configured");
        }
    }
}

void AppWindow::refresh_sync_jobs() {
    if (!jobs_list_) return;
    
    // Clear existing using proper GTK4 method
    while (TRUE) {
        GtkListBoxRow* row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(jobs_list_), 0);
        if (!row) break;
        gtk_list_box_remove(GTK_LIST_BOX(jobs_list_), GTK_WIDGET(row));
    }
    
    // Load jobs from SyncJobRegistry (sync_jobs.json) - this is the source of truth
    auto& registry = SyncJobRegistry::getInstance();
    auto all_jobs = registry.getAllJobs();
    
    // Deduplicate jobs by local+remote path combination (cleanup any old duplicates)
    std::set<std::string> seen_paths;
    std::vector<std::string> duplicate_ids;
    for (const auto& job : all_jobs) {
        std::string key = job.local_path + "|" + job.remote_path;
        if (seen_paths.find(key) != seen_paths.end()) {
            duplicate_ids.push_back(job.job_id);
            Logger::info("[SyncJobs] Removing duplicate job: " + job.job_id);
        } else {
            seen_paths.insert(key);
        }
    }
    // Remove duplicates
    for (const auto& dup_id : duplicate_ids) {
        registry.deleteJob(dup_id);
    }
    // Reload if we removed duplicates
    if (!duplicate_ids.empty()) {
        all_jobs = registry.getAllJobs();
    }
    
    int job_count = 0;
    for (const auto& job : all_jobs) {
        std::string local_path = job.local_path;
        std::string remote_path = job.remote_path;
        std::string sync_type = job.sync_type;
        std::string job_id = job.job_id;
        
        if (local_path.empty() && remote_path.empty()) continue;
        
        // Check if local path definitely doesn't exist (not just I/O error)
        // Only delete orphaned jobs when we're CERTAIN the folder is missing
        if (!local_path.empty() && safe_definitely_missing(local_path)) {
            // Auto-cleanup: delete orphaned sync job (only when folder definitely doesn't exist)
            Logger::info("[SyncJobs] Local folder definitely missing, removing orphan job: " + job_id);
            registry.deleteJob(job_id);
            continue;  // Skip adding this job to the list
        }
        
        job_count++;
        
        // Main row container with card-like styling
        GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_add_css_class(card, "sync-job-card");
        gtk_widget_set_margin_start(card, 12);
        gtk_widget_set_margin_end(card, 12);
        gtk_widget_set_margin_top(card, 6);
        gtk_widget_set_margin_bottom(card, 6);
        
        // Top row: icon + cloud path + sync type badge
        GtkWidget* top_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        
        // Icon based on sync type
        const char* icon_name = (sync_type == "bisync") ? "emblem-synchronizing-symbolic" : "go-up-symbolic";
        GtkWidget* icon = gtk_image_new_from_icon_name(icon_name);
        gtk_image_set_pixel_size(GTK_IMAGE(icon), 28);
        gtk_widget_add_css_class(icon, "sync-icon");
        
        // Cloud folder name (extracted from path)
        std::string folder_name = remote_path;
        if (folder_name.size() > 1 && folder_name[0] == '/') {
            folder_name = folder_name.substr(1);  // Remove leading /
        }
        GtkWidget* cloud_label = gtk_label_new(folder_name.c_str());
        gtk_widget_add_css_class(cloud_label, "sync-job-title");
        gtk_label_set_xalign(GTK_LABEL(cloud_label), 0);
        gtk_widget_set_hexpand(cloud_label, TRUE);
        gtk_label_set_ellipsize(GTK_LABEL(cloud_label), PANGO_ELLIPSIZE_END);
        
        // Sync type badge
        std::string type_text = (sync_type == "bisync") ? "↔ Two-Way" : "↑ Upload";
        GtkWidget* type_badge = gtk_label_new(type_text.c_str());
        gtk_widget_add_css_class(type_badge, "sync-type-badge");
        if (sync_type == "bisync") {
            gtk_widget_add_css_class(type_badge, "sync-type-twoway");
        } else {
            gtk_widget_add_css_class(type_badge, "sync-type-upload");
        }
        
        gtk_box_append(GTK_BOX(top_row), icon);
        gtk_box_append(GTK_BOX(top_row), cloud_label);
        gtk_box_append(GTK_BOX(top_row), type_badge);
        gtk_box_append(GTK_BOX(card), top_row);
        
        // Bottom row: local path + action buttons
        GtkWidget* bottom_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_start(bottom_row, 38);  // Indent to align with text
        
        // Local path (shortened)
        std::string local_display = local_path;
        const char* home = getenv("HOME");
        if (home && local_display.find(home) == 0) {
            local_display = "~" + local_display.substr(strlen(home));
        }
        GtkWidget* local_label = gtk_label_new(local_display.c_str());
        gtk_widget_add_css_class(local_label, "sync-job-path");
        gtk_label_set_xalign(GTK_LABEL(local_label), 0);
        gtk_widget_set_hexpand(local_label, TRUE);
        gtk_label_set_ellipsize(GTK_LABEL(local_label), PANGO_ELLIPSIZE_MIDDLE);
        
        // Edit button
        GtkWidget* edit_btn = gtk_button_new_from_icon_name("document-edit-symbolic");
        gtk_widget_add_css_class(edit_btn, "flat");
        gtk_widget_add_css_class(edit_btn, "circular");
        gtk_widget_set_tooltip_text(edit_btn, "Edit sync job");
        
        // Store job info for edit
        struct JobEditInfo {
            std::string job_id;
            AppWindow* app;
        };
        auto* edit_info = new JobEditInfo{job_id, this};
        g_object_set_data_full(G_OBJECT(edit_btn), "edit_info", edit_info, 
            +[](gpointer data) { delete static_cast<JobEditInfo*>(data); });
        g_signal_connect(edit_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer) {
            auto* info = static_cast<JobEditInfo*>(g_object_get_data(G_OBJECT(btn), "edit_info"));
            if (!info || info->job_id.empty()) return;
            info->app->show_sync_job_edit_dialog(info->job_id);
        }), nullptr);
        
        // Delete button
        GtkWidget* del_btn = gtk_button_new_from_icon_name("user-trash-symbolic");
        gtk_widget_add_css_class(del_btn, "flat");
        gtk_widget_add_css_class(del_btn, "circular");
        gtk_widget_set_tooltip_text(del_btn, "Remove sync job");
        
        // Store job info for deletion confirmation
        struct JobDeleteInfo {
            std::string job_id;
            std::string folder_name;
            AppWindow* app;
        };
        auto* del_info = new JobDeleteInfo{job_id, folder_name, this};
        g_object_set_data_full(G_OBJECT(del_btn), "del_info", del_info, 
            +[](gpointer data) { delete static_cast<JobDeleteInfo*>(data); });
        g_signal_connect(del_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer) {
            auto* info = static_cast<JobDeleteInfo*>(g_object_get_data(G_OBJECT(btn), "del_info"));
            if (!info || info->job_id.empty()) return;
            info->app->show_sync_job_delete_confirm(info->job_id, info->folder_name);
        }), nullptr);
        
        gtk_box_append(GTK_BOX(bottom_row), local_label);
        gtk_box_append(GTK_BOX(bottom_row), edit_btn);
        gtk_box_append(GTK_BOX(bottom_row), del_btn);
        gtk_box_append(GTK_BOX(card), bottom_row);
        
        // Add right-click gesture for context menu
        GtkGesture* right_click = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right_click), GDK_BUTTON_SECONDARY);
        
        struct RightClickInfo {
            std::string job_id;
            AppWindow* app;
            GtkWidget* card_widget;  // Store card widget for positioning
        };
        auto* rc_info = new RightClickInfo{job_id, this, card};
        g_object_set_data_full(G_OBJECT(card), "rc_info", rc_info, 
            +[](gpointer data) { delete static_cast<RightClickInfo*>(data); });
        
        g_signal_connect(right_click, "pressed", G_CALLBACK(+[](GtkGestureClick* gesture, gint, gdouble x, gdouble y, gpointer /*data*/) {
            GtkWidget* widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
            auto* info = static_cast<RightClickInfo*>(g_object_get_data(G_OBJECT(widget), "rc_info"));
            if (info) {
                Logger::info("[SyncJob] Right-click at x=" + std::to_string(x) + ", y=" + std::to_string(y) + " on card widget");
                info->app->show_sync_job_context_menu(info->job_id, x, y, info->card_widget);
            }
        }), nullptr);
        gtk_widget_add_controller(card, GTK_EVENT_CONTROLLER(right_click));
        
        // Tooltip with full details
        std::string tooltip = "Cloud: proton:" + remote_path + "\nLocal: " + local_path + 
                              "\nSync Type: " + (sync_type == "bisync" ? "Two-Way" : "Upload Only") +
                              "\nJob ID: " + job_id + "\n\nRight-click for more options";
        gtk_widget_set_tooltip_text(card, tooltip.c_str());
        
        gtk_list_box_append(GTK_LIST_BOX(jobs_list_), card);
    }
    
    // Show empty state if no jobs
    if (job_count == 0) {
        GtkWidget* empty_row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_widget_set_margin_start(empty_row, 20);
        gtk_widget_set_margin_end(empty_row, 20);
        gtk_widget_set_margin_top(empty_row, 60);
        gtk_widget_set_margin_bottom(empty_row, 60);
        gtk_widget_set_halign(empty_row, GTK_ALIGN_CENTER);
        
        GtkWidget* empty_icon = gtk_image_new_from_icon_name("folder-remote-symbolic");
        gtk_image_set_pixel_size(GTK_IMAGE(empty_icon), 64);
        gtk_widget_add_css_class(empty_icon, "dim-label");
        gtk_box_append(GTK_BOX(empty_row), empty_icon);
        
        GtkWidget* empty_label = gtk_label_new("No sync jobs configured");
        gtk_widget_add_css_class(empty_label, "title-2");
        gtk_widget_add_css_class(empty_label, "dim-label");
        gtk_box_append(GTK_BOX(empty_row), empty_label);
        
        GtkWidget* hint_label = gtk_label_new("Drag a folder here or use Cloud Browser to set up sync");
        gtk_widget_add_css_class(hint_label, "caption");
        gtk_widget_add_css_class(hint_label, "dim-label");
        gtk_box_append(GTK_BOX(empty_row), hint_label);
        
        gtk_list_box_append(GTK_LIST_BOX(jobs_list_), empty_row);
    }
    
    Logger::info("[SyncJobs] Found " + std::to_string(job_count) + " configured job(s)");
}

void AppWindow::refresh_devices() {
    if (!devices_list_) return;
    
    Logger::info("[Devices] Refreshing device list...");
    
    // Clear existing using proper GTK4 method
    while (TRUE) {
        GtkListBoxRow* row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(devices_list_), 0);
        if (!row) break;
        gtk_list_box_remove(GTK_LIST_BOX(devices_list_), GTK_WIDGET(row));
    }
    
    // Get this device info
    auto& device_identity = DeviceIdentity::getInstance();
    std::string this_device_id = device_identity.getDeviceId();
    std::string this_device_name = device_identity.getDeviceName();
    
    // Create "This Device" card (always shown first)
    {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_add_css_class(row, "device-card");
        gtk_widget_set_margin_start(row, 16);
        gtk_widget_set_margin_end(row, 16);
        gtk_widget_set_margin_top(row, 8);
        gtk_widget_set_margin_bottom(row, 8);
        
        GtkWidget* header_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        
        GtkWidget* icon = gtk_image_new_from_icon_name("computer-symbolic");
        gtk_image_set_pixel_size(GTK_IMAGE(icon), 32);
        gtk_box_append(GTK_BOX(header_row), icon);
        
        GtkWidget* info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_hexpand(info_box, TRUE);
        
        GtkWidget* name_label = gtk_label_new(this_device_name.c_str());
        gtk_widget_add_css_class(name_label, "device-name");
        gtk_label_set_xalign(GTK_LABEL(name_label), 0);
        gtk_box_append(GTK_BOX(info_box), name_label);
        
        GtkWidget* id_label = gtk_label_new(("ID: " + this_device_id).c_str());
        gtk_widget_add_css_class(id_label, "device-id");
        gtk_label_set_xalign(GTK_LABEL(id_label), 0);
        gtk_label_set_ellipsize(GTK_LABEL(id_label), PANGO_ELLIPSIZE_MIDDLE);
        gtk_box_append(GTK_BOX(info_box), id_label);
        
        gtk_box_append(GTK_BOX(header_row), info_box);
        
        GtkWidget* this_device_badge = gtk_label_new("This Device");
        gtk_widget_add_css_class(this_device_badge, "sync-badge");
        gtk_widget_add_css_class(this_device_badge, "sync-badge-synced");
        gtk_box_append(GTK_BOX(header_row), this_device_badge);
        
        gtk_box_append(GTK_BOX(row), header_row);
        
        // Count sync jobs using registry (single source of truth)
        auto& registry_ref = SyncJobRegistry::getInstance();
        int job_count = static_cast<int>(registry_ref.getAllJobs().size());
        
        GtkWidget* status_label = gtk_label_new(
            (std::to_string(job_count) + " sync job(s) configured").c_str());
        gtk_widget_add_css_class(status_label, "device-status");
        gtk_label_set_xalign(GTK_LABEL(status_label), 0);
        gtk_widget_set_margin_start(status_label, 40);
        gtk_box_append(GTK_BOX(row), status_label);
        
        gtk_list_box_append(GTK_LIST_BOX(devices_list_), row);
    }
    
    // Get shared devices from sync job registry (local data only - fast)
    auto& registry = SyncJobRegistry::getInstance();
    std::set<std::string> seen_devices;
    seen_devices.insert(this_device_id);
    
    for (const auto& job : registry.getAllJobs()) {
        // Check origin device
        if (!job.origin_device_id.empty() && 
            seen_devices.find(job.origin_device_id) == seen_devices.end()) {
            seen_devices.insert(job.origin_device_id);
            
            GtkWidget* row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
            gtk_widget_add_css_class(row, "device-card");
            gtk_widget_set_margin_start(row, 16);
            gtk_widget_set_margin_end(row, 16);
            gtk_widget_set_margin_top(row, 8);
            gtk_widget_set_margin_bottom(row, 8);
            
            GtkWidget* header_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            
            GtkWidget* icon = gtk_image_new_from_icon_name("network-workgroup-symbolic");
            gtk_image_set_pixel_size(GTK_IMAGE(icon), 32);
            gtk_box_append(GTK_BOX(header_row), icon);
            
            GtkWidget* info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
            gtk_widget_set_hexpand(info_box, TRUE);
            
            std::string name = job.origin_device_name.empty() ? "Unknown Device" : job.origin_device_name;
            GtkWidget* name_label = gtk_label_new(name.c_str());
            gtk_widget_add_css_class(name_label, "device-name");
            gtk_label_set_xalign(GTK_LABEL(name_label), 0);
            gtk_box_append(GTK_BOX(info_box), name_label);
            
            GtkWidget* id_label = gtk_label_new(("ID: " + job.origin_device_id).c_str());
            gtk_widget_add_css_class(id_label, "device-id");
            gtk_label_set_xalign(GTK_LABEL(id_label), 0);
            gtk_label_set_ellipsize(GTK_LABEL(id_label), PANGO_ELLIPSIZE_MIDDLE);
            gtk_box_append(GTK_BOX(info_box), id_label);
            
            gtk_box_append(GTK_BOX(header_row), info_box);
            gtk_box_append(GTK_BOX(row), header_row);
            
            GtkWidget* status_label = gtk_label_new(
                ("Syncs: " + job.remote_path).c_str());
            gtk_widget_add_css_class(status_label, "device-status");
            gtk_label_set_xalign(GTK_LABEL(status_label), 0);
            gtk_widget_set_margin_start(status_label, 40);
            gtk_box_append(GTK_BOX(row), status_label);
            
            gtk_list_box_append(GTK_LIST_BOX(devices_list_), row);
        }
        
        // Check shared devices
        for (const auto& shared_dev : job.shared_devices) {
            if (seen_devices.find(shared_dev.device_id) == seen_devices.end()) {
                seen_devices.insert(shared_dev.device_id);
                
                GtkWidget* row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
                gtk_widget_add_css_class(row, "device-card");
                gtk_widget_set_margin_start(row, 16);
                gtk_widget_set_margin_end(row, 16);
                gtk_widget_set_margin_top(row, 8);
                gtk_widget_set_margin_bottom(row, 8);
                
                GtkWidget* header_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
                
                GtkWidget* icon = gtk_image_new_from_icon_name("network-workgroup-symbolic");
                gtk_image_set_pixel_size(GTK_IMAGE(icon), 32);
                gtk_box_append(GTK_BOX(header_row), icon);
                
                GtkWidget* info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
                gtk_widget_set_hexpand(info_box, TRUE);
                
                std::string name = shared_dev.device_name.empty() ? "Unknown Device" : shared_dev.device_name;
                GtkWidget* name_label = gtk_label_new(name.c_str());
                gtk_widget_add_css_class(name_label, "device-name");
                gtk_label_set_xalign(GTK_LABEL(name_label), 0);
                gtk_box_append(GTK_BOX(info_box), name_label);
                
                GtkWidget* id_label = gtk_label_new(("ID: " + shared_dev.device_id).c_str());
                gtk_widget_add_css_class(id_label, "device-id");
                gtk_label_set_xalign(GTK_LABEL(id_label), 0);
                gtk_label_set_ellipsize(GTK_LABEL(id_label), PANGO_ELLIPSIZE_MIDDLE);
                gtk_box_append(GTK_BOX(info_box), id_label);
                
                gtk_box_append(GTK_BOX(header_row), info_box);
                
                GtkWidget* shared_badge = gtk_label_new("Shared Sync");
                gtk_widget_add_css_class(shared_badge, "sync-badge");
                gtk_widget_add_css_class(shared_badge, "sync-badge-cloud");
                gtk_box_append(GTK_BOX(header_row), shared_badge);
                
                gtk_box_append(GTK_BOX(row), header_row);
                
                // Last sync time
                if (shared_dev.last_sync_time > 0) {
                    char time_buf[64];
                    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M", 
                                  std::localtime(&shared_dev.last_sync_time));
                    GtkWidget* time_label = gtk_label_new(
                        ("Last sync: " + std::string(time_buf)).c_str());
                    gtk_widget_add_css_class(time_label, "device-status");
                    gtk_label_set_xalign(GTK_LABEL(time_label), 0);
                    gtk_widget_set_margin_start(time_label, 40);
                    gtk_box_append(GTK_BOX(row), time_label);
                }
                
                gtk_list_box_append(GTK_LIST_BOX(devices_list_), row);
            }
        }
    }
    
    // If no other devices found in local registry, search cloud asynchronously
    if (seen_devices.size() == 1) {
        // Prevent concurrent cloud discovery
        if (cloud_discovery_running_.exchange(true)) {
            Logger::debug("[Devices] Cloud discovery already in progress, skipping");
            // Show placeholder while existing discovery runs
            GtkWidget* info_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            gtk_widget_set_margin_start(info_row, 16);
            gtk_widget_set_margin_end(info_row, 16);
            gtk_widget_set_margin_top(info_row, 16);
            gtk_widget_set_margin_bottom(info_row, 16);
            gtk_widget_set_halign(info_row, GTK_ALIGN_CENTER);
            
            GtkWidget* spinner = gtk_spinner_new();
            gtk_spinner_start(GTK_SPINNER(spinner));
            gtk_box_append(GTK_BOX(info_row), spinner);
            
            GtkWidget* searching_label = gtk_label_new("Searching for other devices...");
            gtk_widget_add_css_class(searching_label, "dim-label");
            gtk_box_append(GTK_BOX(info_row), searching_label);
            
            gtk_list_box_append(GTK_LIST_BOX(devices_list_), info_row);
            return;
        }
        
        Logger::info("[Devices] No other devices in local registry, starting async cloud discovery...");
        
        // Show "Searching..." placeholder immediately (non-blocking)
        GtkWidget* searching_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_start(searching_row, 16);
        gtk_widget_set_margin_end(searching_row, 16);
        gtk_widget_set_margin_top(searching_row, 16);
        gtk_widget_set_margin_bottom(searching_row, 16);
        gtk_widget_set_halign(searching_row, GTK_ALIGN_CENTER);
        
        GtkWidget* spinner = gtk_spinner_new();
        gtk_spinner_start(GTK_SPINNER(spinner));
        gtk_box_append(GTK_BOX(searching_row), spinner);
        
        GtkWidget* searching_label = gtk_label_new("Searching for other devices...");
        gtk_widget_add_css_class(searching_label, "dim-label");
        gtk_box_append(GTK_BOX(searching_row), searching_label);
        
        gtk_list_box_append(GTK_LIST_BOX(devices_list_), searching_row);
        
        // Spawn background thread for cloud discovery (avoids blocking UI)
        std::thread([this, this_device_id]() {
            Logger::debug("[Devices] Background cloud discovery thread started");
            
            auto& registry = SyncJobRegistry::getInstance();
            auto cloud_configs = registry.getCloudDeviceConfigs();
            
            Logger::debug("[Devices] Cloud discovery returned " + 
                         std::to_string(cloud_configs.size()) + " config(s)");
            
            // Marshal results back to GTK main thread via g_idle_add
            struct DiscoveryResult {
                std::vector<SyncJobRegistry::CloudDeviceConfig> configs;
                std::string this_device_id;
                AppWindow* self;
            };
            auto* result = new DiscoveryResult{std::move(cloud_configs), this_device_id, this};
            
            g_idle_add([](gpointer data) -> gboolean {
                auto* r = static_cast<DiscoveryResult*>(data);
                AppWindow* self = r->self;
                
                if (!self->devices_list_) {
                    Logger::warn("[Devices] devices_list_ is null in cloud discovery callback");
                    self->cloud_discovery_running_ = false;
                    delete r;
                    return G_SOURCE_REMOVE;
                }
                
                // Remove the "Searching..." placeholder (last item in the list)
                GtkListBoxRow* last_row = nullptr;
                int idx = 0;
                while (auto* row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(self->devices_list_), idx)) {
                    last_row = row;
                    idx++;
                }
                if (last_row) {
                    gtk_list_box_remove(GTK_LIST_BOX(self->devices_list_), GTK_WIDGET(last_row));
                }
                
                // Add discovered cloud devices
                bool found_other = false;
                for (const auto& config : r->configs) {
                    if (config.device_id == r->this_device_id) continue;
                    found_other = true;
                    
                    Logger::info("[Devices] Discovered remote device: " + config.device_name + 
                                " (ID: " + config.device_id + ") with " + 
                                std::to_string(config.jobs.size()) + " jobs");
                    
                    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
                    gtk_widget_add_css_class(row, "device-card");
                    gtk_widget_set_margin_start(row, 16);
                    gtk_widget_set_margin_end(row, 16);
                    gtk_widget_set_margin_top(row, 8);
                    gtk_widget_set_margin_bottom(row, 8);
                    
                    GtkWidget* header_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
                    
                    GtkWidget* icon = gtk_image_new_from_icon_name("network-workgroup-symbolic");
                    gtk_image_set_pixel_size(GTK_IMAGE(icon), 32);
                    gtk_box_append(GTK_BOX(header_row), icon);
                    
                    GtkWidget* info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
                    gtk_widget_set_hexpand(info_box, TRUE);
                    
                    std::string name = config.device_name.empty() ? "Unknown Device" : config.device_name;
                    GtkWidget* name_label = gtk_label_new(name.c_str());
                    gtk_widget_add_css_class(name_label, "device-name");
                    gtk_label_set_xalign(GTK_LABEL(name_label), 0);
                    gtk_box_append(GTK_BOX(info_box), name_label);
                    
                    GtkWidget* id_label = gtk_label_new(("ID: " + config.device_id).c_str());
                    gtk_widget_add_css_class(id_label, "device-id");
                    gtk_label_set_xalign(GTK_LABEL(id_label), 0);
                    gtk_label_set_ellipsize(GTK_LABEL(id_label), PANGO_ELLIPSIZE_MIDDLE);
                    gtk_box_append(GTK_BOX(info_box), id_label);
                    
                    gtk_box_append(GTK_BOX(header_row), info_box);
                    
                    GtkWidget* cloud_badge = gtk_label_new("Cloud Device");
                    gtk_widget_add_css_class(cloud_badge, "sync-badge");
                    gtk_widget_add_css_class(cloud_badge, "sync-badge-cloud");
                    gtk_box_append(GTK_BOX(header_row), cloud_badge);
                    
                    gtk_box_append(GTK_BOX(row), header_row);
                    
                    // Show last updated time
                    if (config.last_updated > 0) {
                        char time_buf[64];
                        std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M", 
                                      std::localtime(&config.last_updated));
                        GtkWidget* time_label = gtk_label_new(
                            ("Last seen: " + std::string(time_buf)).c_str());
                        gtk_widget_add_css_class(time_label, "device-status");
                        gtk_label_set_xalign(GTK_LABEL(time_label), 0);
                        gtk_widget_set_margin_start(time_label, 40);
                        gtk_box_append(GTK_BOX(row), time_label);
                    }
                    
                    // Show synced folder count
                    std::string job_info = std::to_string(config.jobs.size()) + " sync job(s)";
                    GtkWidget* jobs_label = gtk_label_new(job_info.c_str());
                    gtk_widget_add_css_class(jobs_label, "device-status");
                    gtk_label_set_xalign(GTK_LABEL(jobs_label), 0);
                    gtk_widget_set_margin_start(jobs_label, 40);
                    gtk_box_append(GTK_BOX(row), jobs_label);
                    
                    gtk_list_box_append(GTK_LIST_BOX(self->devices_list_), row);
                }
                
                // If no other devices found, show helpful message
                if (!found_other) {
                    GtkWidget* info_row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
                    gtk_widget_set_margin_start(info_row, 16);
                    gtk_widget_set_margin_end(info_row, 16);
                    gtk_widget_set_margin_top(info_row, 16);
                    gtk_widget_set_margin_bottom(info_row, 16);
                    
                    GtkWidget* info_label = gtk_label_new(
                        "No other devices found. Set up sync on another device\n"
                        "with the same Proton account to see them here.");
                    gtk_widget_add_css_class(info_label, "dim-label");
                    gtk_label_set_justify(GTK_LABEL(info_label), GTK_JUSTIFY_CENTER);
                    gtk_box_append(GTK_BOX(info_row), info_label);
                    
                    gtk_list_box_append(GTK_LIST_BOX(self->devices_list_), info_row);
                }
                
                int total_devices = 1 + (found_other ? static_cast<int>(r->configs.size()) : 0);
                Logger::info("[Devices] Found " + std::to_string(total_devices) + " device(s)");
                
                self->cloud_discovery_running_ = false;
                delete r;
                return G_SOURCE_REMOVE;
            }, result);
        }).detach();
    } else {
        Logger::info("[Devices] Found " + std::to_string(seen_devices.size()) + " device(s) from local registry");
    }
}

void AppWindow::update_service_status(bool is_running) {
    if (!service_status_label_) return;
    
    gtk_widget_remove_css_class(service_status_label_, "service-status-active");
    gtk_widget_remove_css_class(service_status_label_, "service-status-inactive");
    
    if (is_running) {
        // Count active timers
        std::string count_str = exec_command("systemctl --user list-units 'proton-drive-job-*.timer' --state=active --no-legend 2>/dev/null | wc -l");
        count_str.erase(0, count_str.find_first_not_of(" \t\n"));
        count_str.erase(count_str.find_last_not_of(" \t\n") + 1);
        
        std::string status_text = "● Active (" + count_str + " sync jobs)";
        gtk_label_set_text(GTK_LABEL(service_status_label_), status_text.c_str());
        gtk_widget_add_css_class(service_status_label_, "service-status-active");
        gtk_widget_set_sensitive(start_btn_, FALSE);
        gtk_widget_set_sensitive(stop_btn_, TRUE);
    } else {
        gtk_label_set_text(GTK_LABEL(service_status_label_), "○ Inactive (No sync jobs)");
        gtk_widget_add_css_class(service_status_label_, "service-status-inactive");
        gtk_widget_set_sensitive(start_btn_, TRUE);
        gtk_widget_set_sensitive(stop_btn_, FALSE);
    }
}

void AppWindow::show_sync_job_delete_confirm(const std::string& job_id, const std::string& folder_name) {
    // Get job info for displaying local path and for potential deletion
    auto& reg = SyncJobRegistry::getInstance();
    auto job_opt = reg.getJobById(job_id);
    std::string local_path;
    if (job_opt) {
        local_path = job_opt->local_path;
    }
    
    GtkWidget* dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Remove Sync Job");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(window_));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 450, -1);
    
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);
    gtk_window_set_child(GTK_WINDOW(dialog), vbox);
    
    GtkWidget* header = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(header), ("<b>Remove sync for \"" + folder_name + "\"?</b>").c_str());
    gtk_label_set_xalign(GTK_LABEL(header), 0);
    gtk_box_append(GTK_BOX(vbox), header);
    
    GtkWidget* desc = gtk_label_new("This will stop syncing this folder.");
    gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
    gtk_label_set_xalign(GTK_LABEL(desc), 0);
    gtk_widget_add_css_class(desc, "dim-label");
    gtk_box_append(GTK_BOX(vbox), desc);
    
    // Checkbox to also delete local files
    GtkWidget* delete_files_check = gtk_check_button_new_with_label("Also delete local files");
    gtk_widget_set_margin_top(delete_files_check, 8);
    gtk_box_append(GTK_BOX(vbox), delete_files_check);
    
    // Show local path if available
    if (!local_path.empty()) {
        std::string display_path = local_path;
        const char* home = getenv("HOME");
        if (home && display_path.find(home) == 0) {
            display_path = "~" + display_path.substr(strlen(home));
        }
        GtkWidget* path_label = gtk_label_new(("Local folder: " + display_path).c_str());
        gtk_label_set_xalign(GTK_LABEL(path_label), 0);
        gtk_widget_add_css_class(path_label, "dim-label");
        gtk_widget_add_css_class(path_label, "caption");
        gtk_widget_set_margin_start(path_label, 24);
        gtk_box_append(GTK_BOX(vbox), path_label);
    }
    
    // Warning for delete option
    GtkWidget* warning_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(warning_label), "<span color='#cc6600'>⚠ Warning: Deleting local files cannot be undone!</span>");
    gtk_label_set_xalign(GTK_LABEL(warning_label), 0);
    gtk_widget_set_margin_start(warning_label, 24);
    gtk_widget_set_visible(warning_label, FALSE);
    gtk_box_append(GTK_BOX(vbox), warning_label);
    
    // Show/hide warning based on checkbox
    g_signal_connect(delete_files_check, "toggled", G_CALLBACK(+[](GtkCheckButton* btn, gpointer data) {
        GtkWidget* warn = GTK_WIDGET(data);
        gtk_widget_set_visible(warn, gtk_check_button_get_active(btn));
    }), warning_label);
    
    GtkWidget* action_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(action_box, GTK_ALIGN_END);
    gtk_widget_set_margin_top(action_box, 12);
    
    GtkWidget* cancel_btn = gtk_button_new_with_label("Cancel");
    GtkWidget* confirm_btn = gtk_button_new_with_label("Remove");
    gtk_widget_add_css_class(confirm_btn, "destructive-action");
    
    gtk_box_append(GTK_BOX(action_box), cancel_btn);
    gtk_box_append(GTK_BOX(action_box), confirm_btn);
    gtk_box_append(GTK_BOX(vbox), action_box);
    
    // Store data for confirm callback
    struct ConfirmData {
        AppWindow* self;
        std::string job_id;
        std::string local_path;
        GtkWidget* dialog;
        GtkWidget* delete_check;
    };
    auto* conf_data = new ConfirmData{this, job_id, local_path, dialog, delete_files_check};
    g_object_set_data_full(G_OBJECT(dialog), "conf-data", conf_data, +[](gpointer p) {
        delete static_cast<ConfirmData*>(p);
    });
    
    g_signal_connect_swapped(cancel_btn, "clicked", G_CALLBACK(gtk_window_destroy), dialog);
    g_signal_connect(confirm_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        auto* conf = static_cast<ConfirmData*>(g_object_get_data(G_OBJECT(user_data), "conf-data"));
        if (!conf) return;
        
        // Check if user wants to delete local files too
        bool delete_files = gtk_check_button_get_active(GTK_CHECK_BUTTON(conf->delete_check));
        
        auto& reg = SyncJobRegistry::getInstance();
        reg.deleteJob(conf->job_id);
        Logger::info("[SyncJobs] Removed job: " + conf->job_id);
        
        // Delete local files if requested
        if (delete_files && !conf->local_path.empty() && safe_exists(conf->local_path)) {
            Logger::info("[SyncJobs] Also deleting local folder: " + conf->local_path);
            std::error_code ec;
            fs::remove_all(conf->local_path, ec);
            if (ec) {
                Logger::error("[SyncJobs] Failed to delete local folder: " + ec.message());
            } else {
                conf->self->append_log("[Sync] Deleted local folder: " + conf->local_path);
            }
        }
        
        conf->self->refresh_sync_jobs();
        conf->self->refresh_cloud_files();
        gtk_window_destroy(GTK_WINDOW(conf->dialog));
    }), dialog);
    
    gtk_window_present(GTK_WINDOW(dialog));
}

void AppWindow::show_sync_job_context_menu(const std::string& job_id, double x, double y, GtkWidget* card_widget) {
    auto& registry = SyncJobRegistry::getInstance();
    auto job_opt = registry.getJobById(job_id);
    if (!job_opt) {
        Logger::warn("[SyncJob] Context menu: job not found: " + job_id);
        return;
    }
    
    auto& job = *job_opt;
    
    Logger::info("[SyncJob] Creating context menu at widget-relative position: x=" + std::to_string(x) + ", y=" + std::to_string(y));
    
    // Create popover menu - use the card widget as parent so coordinates work correctly
    GtkWidget* popover = gtk_popover_new();
    gtk_widget_set_parent(popover, card_widget);
    
    GtkWidget* menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(menu_box, 8);
    gtk_widget_set_margin_end(menu_box, 8);
    gtk_widget_set_margin_top(menu_box, 8);
    gtk_widget_set_margin_bottom(menu_box, 8);
    gtk_popover_set_child(GTK_POPOVER(popover), menu_box);
    
    // Open Local Folder
    GtkWidget* open_btn = gtk_button_new_with_label("Open Local Folder");
    gtk_widget_add_css_class(open_btn, "flat-button");
    char* local_path_copy = g_strdup(job.local_path.c_str());
    g_object_set_data_full(G_OBJECT(open_btn), "local_path", local_path_copy, g_free);
    g_signal_connect(open_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer data) {
        const char* lp = static_cast<const char*>(g_object_get_data(G_OBJECT(btn), "local_path"));
        auto* self = static_cast<AppWindow*>(data);
        std::string cmd = "xdg-open " + shell_escape(lp) + " &";
        run_system(cmd);
        self->append_log("[Open] Local folder: " + std::string(lp));
        GtkWidget* pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
        if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
    }), this);
    gtk_box_append(GTK_BOX(menu_box), open_btn);
    
    // Sync Now
    struct SyncNowInfo {
        std::string local_path;
        std::string remote_path;
        std::string sync_type;
        AppWindow* app;
    };
    auto* sync_info = new SyncNowInfo{job.local_path, job.remote_path, job.sync_type, this};
    
    GtkWidget* sync_btn = gtk_button_new_with_label("Sync Now");
    gtk_widget_add_css_class(sync_btn, "flat-button");
    g_object_set_data_full(G_OBJECT(sync_btn), "sync_info", sync_info,
        +[](gpointer p) { delete static_cast<SyncNowInfo*>(p); });
    g_signal_connect(sync_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer) {
        auto* info = static_cast<SyncNowInfo*>(g_object_get_data(G_OBJECT(btn), "sync_info"));
        if (!info) return;
        
        info->app->append_log("[Sync] Manual sync: " + info->remote_path);
        Logger::info("[Sync] Manual sync triggered: " + info->remote_path);
        SyncManager::getInstance().execute_sync_to_local_async(info->remote_path, info->local_path, info->sync_type);
        
        proton::NotificationManager::getInstance().notify(
            "Sync Started",
            "Syncing: " + info->remote_path,
            proton::NotificationType::INFO);
        
        GtkWidget* pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
        if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
    }), nullptr);
    gtk_box_append(GTK_BOX(menu_box), sync_btn);
    
    // Force Resync
    auto* resync_info = new SyncNowInfo{job.local_path, job.remote_path, job.sync_type, this};
    
    GtkWidget* resync_btn = gtk_button_new_with_label("Force Resync");
    gtk_widget_add_css_class(resync_btn, "flat-button");
    g_object_set_data_full(G_OBJECT(resync_btn), "sync_info", resync_info,
        +[](gpointer p) { delete static_cast<SyncNowInfo*>(p); });
    g_signal_connect(resync_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer) {
        auto* info = static_cast<SyncNowInfo*>(g_object_get_data(G_OBJECT(btn), "sync_info"));
        if (!info) return;
        
        info->app->append_log("[Sync] Force resync: " + info->remote_path);
        Logger::info("[Sync] Force resync triggered: " + info->remote_path);
        
        std::string rclone_path = AppWindowHelpers::get_rclone_path();
        std::string cmd = shell_escape(rclone_path) + " bisync " + 
                          shell_escape("proton:" + info->remote_path) + " " +
                          shell_escape(info->local_path) + " --resync --verbose --rc &";
        run_system(cmd);
        
        proton::NotificationManager::getInstance().notify(
            "Force Resync Started",
            "Resyncing: " + info->remote_path,
            proton::NotificationType::INFO);
        
        GtkWidget* pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
        if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
    }), nullptr);
    gtk_box_append(GTK_BOX(menu_box), resync_btn);
    
    gtk_box_append(GTK_BOX(menu_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    
    // Edit Sync Job
    GtkWidget* edit_btn = gtk_button_new_with_label("Edit Sync Job...");
    gtk_widget_add_css_class(edit_btn, "flat-button");
    char* job_id_copy = g_strdup(job_id.c_str());
    g_object_set_data_full(G_OBJECT(edit_btn), "job_id", job_id_copy, g_free);
    g_signal_connect(edit_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer data) {
        const char* jid = static_cast<const char*>(g_object_get_data(G_OBJECT(btn), "job_id"));
        auto* self = static_cast<AppWindow*>(data);
        GtkWidget* pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
        if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
        self->show_sync_job_edit_dialog(std::string(jid));
    }), this);
    gtk_box_append(GTK_BOX(menu_box), edit_btn);
    
    gtk_box_append(GTK_BOX(menu_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    
    // Remove Sync Job
    std::string folder_name = job.remote_path;
    if (!folder_name.empty() && folder_name[0] == '/') folder_name = folder_name.substr(1);
    
    struct DeleteInfo {
        std::string job_id;
        std::string folder_name;
        AppWindow* app;
    };
    auto* del_info = new DeleteInfo{job_id, folder_name, this};
    
    GtkWidget* del_btn = gtk_button_new_with_label("Remove Sync Job...");
    gtk_widget_add_css_class(del_btn, "flat-button");
    gtk_widget_add_css_class(del_btn, "destructive-action");
    g_object_set_data_full(G_OBJECT(del_btn), "del_info", del_info,
        +[](gpointer p) { delete static_cast<DeleteInfo*>(p); });
    g_signal_connect(del_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer) {
        auto* info = static_cast<DeleteInfo*>(g_object_get_data(G_OBJECT(btn), "del_info"));
        if (!info) return;
        GtkWidget* pop = gtk_widget_get_ancestor(GTK_WIDGET(btn), GTK_TYPE_POPOVER);
        if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
        info->app->show_sync_job_delete_confirm(info->job_id, info->folder_name);
    }), nullptr);
    gtk_box_append(GTK_BOX(menu_box), del_btn);
    
    // Position the popover at the click location
    GdkRectangle rect = {(int)x, (int)y, 1, 1};
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), TRUE);
    gtk_popover_popup(GTK_POPOVER(popover));
}

void AppWindow::show_sync_job_edit_dialog(const std::string& job_id) {
    auto& registry = SyncJobRegistry::getInstance();
    auto job_opt = registry.getJobById(job_id);
    if (!job_opt) {
        Logger::warn("[SyncJob] Edit dialog: job not found: " + job_id);
        return;
    }
    
    auto job = *job_opt;  // Copy so we can modify
    
    GtkWidget* dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Edit Sync Job");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(window_));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 500, -1);
    
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);
    gtk_window_set_child(GTK_WINDOW(dialog), vbox);
    
    // Cloud path (read-only)
    GtkWidget* cloud_label = gtk_label_new("Cloud Folder:");
    gtk_label_set_xalign(GTK_LABEL(cloud_label), 0);
    gtk_box_append(GTK_BOX(vbox), cloud_label);
    
    GtkWidget* cloud_entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(cloud_entry), job.remote_path.c_str());
    gtk_editable_set_editable(GTK_EDITABLE(cloud_entry), FALSE);
    gtk_widget_add_css_class(cloud_entry, "dim-label");
    gtk_box_append(GTK_BOX(vbox), cloud_entry);
    
    // Local path (read-only, show current)
    GtkWidget* local_label = gtk_label_new("Local Folder:");
    gtk_label_set_xalign(GTK_LABEL(local_label), 0);
    gtk_widget_set_margin_top(local_label, 8);
    gtk_box_append(GTK_BOX(vbox), local_label);
    
    GtkWidget* local_entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(local_entry), job.local_path.c_str());
    gtk_editable_set_editable(GTK_EDITABLE(local_entry), FALSE);
    gtk_widget_add_css_class(local_entry, "dim-label");
    gtk_box_append(GTK_BOX(vbox), local_entry);
    
    // Sync type selection
    GtkWidget* type_label = gtk_label_new("Sync Type:");
    gtk_label_set_xalign(GTK_LABEL(type_label), 0);
    gtk_widget_set_margin_top(type_label, 12);
    gtk_box_append(GTK_BOX(vbox), type_label);
    
    // Use GTK4 GtkDropDown instead of deprecated GtkComboBoxText
    const char* sync_type_strings[] = {
        "↔ Two-Way Sync (changes sync both ways)",
        "↑ Upload Only (local → cloud)",
        "↓ Download Only (cloud → local)",
        nullptr
    };
    GtkStringList* type_model = gtk_string_list_new(sync_type_strings);
    GtkWidget* type_dropdown = gtk_drop_down_new(G_LIST_MODEL(type_model), nullptr);
    
    // Set current value based on sync_type
    guint selected_index = 0;
    if (job.sync_type == "bisync") {
        selected_index = 0;
    } else if (job.sync_type == "sync") {
        selected_index = 1;
    } else {
        selected_index = 2;
    }
    gtk_drop_down_set_selected(GTK_DROP_DOWN(type_dropdown), selected_index);
    gtk_box_append(GTK_BOX(vbox), type_dropdown);
    
    // Buttons
    GtkWidget* action_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(action_box, GTK_ALIGN_END);
    gtk_widget_set_margin_top(action_box, 16);
    
    GtkWidget* cancel_btn = gtk_button_new_with_label("Cancel");
    GtkWidget* save_btn = gtk_button_new_with_label("Save");
    gtk_widget_add_css_class(save_btn, "suggested-action");
    
    gtk_box_append(GTK_BOX(action_box), cancel_btn);
    gtk_box_append(GTK_BOX(action_box), save_btn);
    gtk_box_append(GTK_BOX(vbox), action_box);
    
    // Store data for callback
    struct EditData {
        AppWindow* self;
        std::string job_id;
        GtkWidget* dialog;
        GtkWidget* type_dropdown;
    };
    auto* edit_data = new EditData{this, job_id, dialog, type_dropdown};
    g_object_set_data_full(G_OBJECT(dialog), "edit-data", edit_data, +[](gpointer p) {
        delete static_cast<EditData*>(p);
    });
    
    g_signal_connect_swapped(cancel_btn, "clicked", G_CALLBACK(gtk_window_destroy), dialog);
    
    // Define static callback to avoid lambda issues with G_CALLBACK macro
    static auto save_callback = [](GtkButton*, gpointer user_data) {
        auto* ed = static_cast<EditData*>(g_object_get_data(G_OBJECT(user_data), "edit-data"));
        if (!ed) return;
        
        // Get new sync type from dropdown selection
        guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(ed->type_dropdown));
        const char* type_ids[] = {"bisync", "sync", "copy"};
        const char* new_type = (selected < 3) ? type_ids[selected] : "bisync";
        
        // Update job
        auto& reg = SyncJobRegistry::getInstance();
        auto job = reg.getJobById(ed->job_id);
        if (job) {
            job->sync_type = std::string(new_type);
            reg.updateJob(*job);
            Logger::info("[SyncJob] Updated job " + ed->job_id + " sync_type to: " + new_type);
            ed->self->append_log("[Sync] Updated sync type for " + job->remote_path + " to " + new_type);
        }
        
        ed->self->refresh_sync_jobs();
        gtk_window_destroy(GTK_WINDOW(ed->dialog));
    };
    g_signal_connect(save_btn, "clicked", G_CALLBACK(+save_callback), dialog);
    
    gtk_window_present(GTK_WINDOW(dialog));
}

// ===== CONFLICT RESOLUTION METHODS =====

void AppWindow::store_conflict(const std::string& path) {
    std::lock_guard<std::mutex> lock(conflicts_mutex_);
    // Avoid duplicates
    for (const auto& existing : detected_conflicts_) {
        if (existing == path) return;
    }
    detected_conflicts_.push_back(path);
    Logger::debug("[Conflicts] Stored conflict: " + path);
}

void AppWindow::clear_conflicts() {
    {
        std::lock_guard<std::mutex> lock(conflicts_mutex_);
        detected_conflicts_.clear();
    }
    refresh_conflicts();
    append_log("[Conflicts] Dismissed all conflict notifications");
}

void AppWindow::refresh_conflicts() {
    if (!conflicts_list_ || !conflicts_revealer_) return;
    
    // Clear existing list
    GtkWidget* child = gtk_widget_get_first_child(conflicts_list_);
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(GTK_LIST_BOX(conflicts_list_), child);
        child = next;
    }
    
    std::vector<std::string> conflicts_copy;
    {
        std::lock_guard<std::mutex> lock(conflicts_mutex_);
        conflicts_copy = detected_conflicts_;
    }
    
    if (conflicts_copy.empty()) {
        gtk_revealer_set_reveal_child(GTK_REVEALER(conflicts_revealer_), FALSE);
        return;
    }
    
    // Show the revealer
    gtk_revealer_set_reveal_child(GTK_REVEALER(conflicts_revealer_), TRUE);
    
    // Add conflict items
    for (const auto& conflict_path : conflicts_copy) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_set_margin_start(row, 8);
        gtk_widget_set_margin_end(row, 8);
        gtk_widget_set_margin_top(row, 4);
        gtk_widget_set_margin_bottom(row, 4);
        
        // Conflict icon
        GtkWidget* icon = gtk_label_new("\xf0\x9f\x93\x84");  // 📄 file icon
        gtk_box_append(GTK_BOX(row), icon);
        
        // File path (truncated)
        std::filesystem::path p(conflict_path);
        std::string display = p.filename().string();
        if (display.length() > 50) {
            display = "..." + display.substr(display.length() - 47);
        }
        
        GtkWidget* path_label = gtk_label_new(display.c_str());
        gtk_label_set_xalign(GTK_LABEL(path_label), 0);
        gtk_widget_set_hexpand(path_label, TRUE);
        gtk_label_set_ellipsize(GTK_LABEL(path_label), PANGO_ELLIPSIZE_MIDDLE);
        gtk_widget_set_tooltip_text(path_label, conflict_path.c_str());
        gtk_box_append(GTK_BOX(row), path_label);
        
        // "Keep" button (removes the conflict file, keeps original)
        struct ConflictData {
            AppWindow* self;
            std::string path;
        };
        auto* data = new ConflictData{this, conflict_path};
        
        GtkWidget* keep_local_btn = gtk_button_new_with_label("Delete");
        gtk_widget_add_css_class(keep_local_btn, "flat");
        gtk_widget_add_css_class(keep_local_btn, "small");
        g_signal_connect(keep_local_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
            auto* d = static_cast<ConflictData*>(user_data);
            std::error_code ec;
            if (std::filesystem::remove(d->path, ec)) {
                d->self->show_toast("Deleted: " + std::filesystem::path(d->path).filename().string());
                d->self->append_log("[Conflicts] Deleted conflict file: " + d->path);
                // Remove from list
                {
                    std::lock_guard<std::mutex> lock(d->self->conflicts_mutex_);
                    auto& conflicts = d->self->detected_conflicts_;
                    conflicts.erase(std::remove(conflicts.begin(), conflicts.end(), d->path), conflicts.end());
                }
                d->self->refresh_conflicts();
            } else {
                d->self->show_toast("Failed to delete file");
            }
            delete d;
        }), data);
        gtk_box_append(GTK_BOX(row), keep_local_btn);
        
        // "Open" button
        auto* open_data = new ConflictData{this, conflict_path};
        GtkWidget* open_btn = gtk_button_new_with_label("Open");
        gtk_widget_add_css_class(open_btn, "flat");
        gtk_widget_add_css_class(open_btn, "small");
        g_signal_connect(open_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
            auto* d = static_cast<ConflictData*>(user_data);
            std::string cmd = "xdg-open \"" + d->path + "\" &";
            std::system(cmd.c_str());
            delete d;
        }), open_data);
        gtk_box_append(GTK_BOX(row), open_btn);
        
        gtk_list_box_append(GTK_LIST_BOX(conflicts_list_), row);
    }
}

void AppWindow::show_conflict_resolution(const std::string& conflict_path) {
    // Store and refresh conflicts
    store_conflict(conflict_path);
    
    // Schedule UI update on main thread
    struct UpdateData {
        AppWindow* self;
    };
    auto* data = new UpdateData{this};
    g_idle_add([](gpointer user_data) -> gboolean {
        auto* d = static_cast<UpdateData*>(user_data);
        d->self->refresh_conflicts();
        delete d;
        return G_SOURCE_REMOVE;
    }, data);
}

// Context menu for cloud files (GTK4 popover style)

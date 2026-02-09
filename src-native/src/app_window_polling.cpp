// app_window_polling.cpp - Sync activity polling for AppWindow
// Extracted from app_window.cpp to reduce file size

#include "app_window.hpp"
#include "app_window_helpers.hpp"
#include "sync_manager.hpp"
#include "sync_job_metadata.hpp"
#include "notifications.hpp"
#include "logger.hpp"
#include <fstream>
#include <filesystem>
#include <set>
#include <sstream>
#include <thread>
#include <chrono>
#include <signal.h>
#include <cerrno>
#include <cstring>
#include <gtk/gtk.h>

namespace fs = std::filesystem;
using namespace AppWindowHelpers;

void AppWindow::poll_sync_activity() {
    // Only log on first call or errors
    static bool first_call = true;
    if (first_call) {
        Logger::debug("[poll_sync_activity] First poll");
        first_call = false;
    }
    
    if (!sync_activity_list_ || !no_sync_label_) {
        Logger::error("[poll_sync_activity] sync_activity_list_ or no_sync_label_ is null!");
        return;
    }
    
    auto extract_number = [](const std::string& s, const std::string& key, double& out) -> bool {
        std::string needle = "\"" + key + "\":";
        size_t pos = s.find(needle);
        if (pos == std::string::npos) return false;
        pos += needle.size();
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) pos++;
        size_t end = pos;
        while (end < s.size() && (std::isdigit(static_cast<unsigned char>(s[end])) || s[end] == '.' || s[end] == '-')) end++;
        if (end == pos) return false;
        try {
            out = std::stod(s.substr(pos, end - pos));
            return true;
        } catch (...) {
            return false;
        }
    };
    auto format_bytes = [](double bytes) -> std::string {
        const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        int idx = 0;
        while (bytes >= 1024.0 && idx < 4) {
            bytes /= 1024.0;
            idx++;
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.1f %s", bytes, units[idx]);
        return std::string(buf);
    };
    auto format_speed = [&](double bytes_per_sec) -> std::string {
        return format_bytes(bytes_per_sec) + "/s";
    };
    auto format_eta = [](double seconds) -> std::string {
        if (seconds < 0) return "";
        int s = static_cast<int>(seconds);
        int h = s / 3600;
        int m = (s % 3600) / 60;
        int sec = s % 60;
        char buf[64];
        if (h > 0) {
            std::snprintf(buf, sizeof(buf), "%dh %dm %ds", h, m, sec);
        } else if (m > 0) {
            std::snprintf(buf, sizeof(buf), "%dm %ds", m, sec);
        } else {
            std::snprintf(buf, sizeof(buf), "%ds", sec);
        }
        return std::string(buf);
    };
    auto extract_rc_addr = [](const std::string& cmd) -> std::string {
        size_t pos = cmd.find("--rc-addr=");
        if (pos != std::string::npos) {
            pos += std::string("--rc-addr=").size();
            size_t end = cmd.find(' ', pos);
            return cmd.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        }
        pos = cmd.find("--rc-addr");
        if (pos != std::string::npos) {
            pos += std::string("--rc-addr").size();
            while (pos < cmd.size() && cmd[pos] == ' ') pos++;
            size_t end = cmd.find(' ', pos);
            return cmd.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        }
        return "";
    };
    
    // Collect sync process data first (without touching widgets)
    struct SyncInfo {
        std::string pid;
        std::string elapsed;
        std::string remote_path;
        bool is_initial_sync = false;
        std::string status_text;
        std::string detail;
        std::string rc_addr;
        double progress = -1.0;  // 0.0 to 1.0, -1 = unknown
        std::vector<std::string> transferring_files;  // Files currently being transferred
        double speed_bytes = 0;
    };
    std::vector<SyncInfo> active_syncs;
    
    // Check for active rclone processes with etime (elapsed time)
    // Filter out internal config sync processes (.proton-sync-config) to avoid polluting the UI
    std::string ps_output = exec_command("ps -eo pid,etime,cmd 2>/dev/null | grep -E 'rclone.*(sync|copy|bisync)' | grep -v grep | grep -v '.proton-sync-config'");
    
    if (!ps_output.empty()) {
        // Parse each rclone process
        std::stringstream ps_ss(ps_output);
        std::string ps_line;
        
        while (std::getline(ps_ss, ps_line) && active_syncs.size() < 5) {
            // Skip empty lines
            if (ps_line.empty()) continue;
            
            // Trim leading whitespace
            size_t start = ps_line.find_first_not_of(" \t");
            if (start != std::string::npos) {
                ps_line = ps_line.substr(start);
            }
            
            // Parse: PID ELAPSED CMD...
            std::istringstream iss(ps_line);
            SyncInfo info;
            iss >> info.pid >> info.elapsed;
            
            // Get rest of line (command)
            std::string cmd;
            std::getline(iss, cmd);
            
            info.is_initial_sync = (cmd.find("--resync") != std::string::npos);
            info.rc_addr = extract_rc_addr(cmd);
            
            // Find proton: path
            std::string sync_info = "Syncing...";
            std::string local_dest = "";
            size_t proton_pos = cmd.find("proton:");
            if (proton_pos != std::string::npos) {
                // Find end of proton path (either quote or space)
                size_t start_quote = cmd.rfind('"', proton_pos);
                size_t end_quote = cmd.find('"', proton_pos);
                size_t space_after = cmd.find(' ', proton_pos);
                
                std::string remote_path;
                if (start_quote != std::string::npos && end_quote != std::string::npos) {
                    // Quoted path
                    remote_path = cmd.substr(proton_pos, end_quote - proton_pos);
                } else if (space_after != std::string::npos) {
                    remote_path = cmd.substr(proton_pos, space_after - proton_pos);
                } else {
                    remote_path = cmd.substr(proton_pos);
                }
                
                if (remote_path.length() > 7) {
                    sync_info = remote_path.substr(7);  // Remove "proton:"
                    if (sync_info.empty()) sync_info = "/";
                } else {
                    sync_info = remote_path;
                }
                
                // Try to find local destination path (after the proton path)
                size_t after_proton = end_quote != std::string::npos ? end_quote + 1 : space_after;
                if (after_proton != std::string::npos) {
                    size_t local_start = cmd.find("/home", after_proton);
                    if (local_start != std::string::npos) {
                        size_t local_end = cmd.find('"', local_start);
                        if (local_end == std::string::npos) local_end = cmd.find(' ', local_start);
                        if (local_end == std::string::npos) local_end = cmd.length();
                        local_dest = cmd.substr(local_start, local_end - local_start);
                        // Shorten for display
                        const char* home = getenv("HOME");
                        if (home && local_dest.find(home) == 0) {
                            local_dest = "~" + local_dest.substr(strlen(home));
                        }
                    }
                }
            }
            info.remote_path = sync_info;
            
            // Build status text
            if (info.is_initial_sync) {
                info.status_text = "Initial sync: " + sync_info + " (" + info.elapsed + ")";
            } else {
                info.status_text = "Syncing: " + sync_info + " (" + info.elapsed + ")";
            }
            
            // Add local destination if found
            if (!local_dest.empty()) {
                info.detail = "→ " + local_dest;
            }
            
            // Try to get detail from rclone RC synchronously (quick timeout)
            if (!info.rc_addr.empty()) {
                std::string rc_cmd = "timeout 0.3 rclone rc core/stats --rc-addr=" + info.rc_addr + " 2>/dev/null";
                std::string rc_output = exec_command(rc_cmd.c_str());
                if (!rc_output.empty()) {
                    double bytes = 0, total_bytes = 0, speed = 0, eta = -1;
                    double transfers = -1, transferring = -1, checks = -1, checking = -1;
                    bool has_bytes = extract_number(rc_output, "bytes", bytes);
                    bool has_total = extract_number(rc_output, "totalBytes", total_bytes);
                    bool has_speed = extract_number(rc_output, "speed", speed);
                    bool has_eta = extract_number(rc_output, "eta", eta);
                    bool has_transfers = extract_number(rc_output, "transfers", transfers);
                    bool has_transferring = extract_number(rc_output, "transferring", transferring);
                    bool has_checks = extract_number(rc_output, "checks", checks);
                    bool has_checking = extract_number(rc_output, "checking", checking);
                    
                    std::vector<std::string> parts;
                    if (has_bytes) {
                        if (has_total && total_bytes > 0) {
                            parts.push_back("Transferred: " + format_bytes(bytes) + " / " + format_bytes(total_bytes));
                        } else {
                            parts.push_back("Transferred: " + format_bytes(bytes));
                        }
                    }
                    if (has_speed && speed > 0) {
                        parts.push_back("Speed: " + format_speed(speed));
                    }
                    if (has_eta && eta >= 0) {
                        std::string eta_str = format_eta(eta);
                        if (!eta_str.empty()) parts.push_back("ETA: " + eta_str);
                    }
                    if (has_transfers || has_transferring) {
                        int total = has_transfers ? static_cast<int>(transfers) : -1;
                        int active = has_transferring ? static_cast<int>(transferring) : -1;
                        if (total >= 0 && active >= 0) {
                            parts.push_back("Transfers: " + std::to_string(active) + "/" + std::to_string(total));
                        } else if (total >= 0) {
                            parts.push_back("Transfers: " + std::to_string(total));
                        }
                    }
                    if (has_checks || has_checking) {
                        int total = has_checks ? static_cast<int>(checks) : -1;
                        int active = has_checking ? static_cast<int>(checking) : -1;
                        if (total >= 0 && active >= 0) {
                            parts.push_back("Checks: " + std::to_string(active) + "/" + std::to_string(total));
                        } else if (total >= 0) {
                            parts.push_back("Checks: " + std::to_string(total));
                        }
                    }
                    
                    if (!parts.empty()) {
                        // Append to existing detail (which may have local dest)
                        if (!info.detail.empty()) info.detail += " • ";
                        for (size_t p = 0; p < parts.size(); p++) {
                            if (p > 0) info.detail += " • ";
                            info.detail += parts[p];
                        }
                    }                    // Calculate progress fraction
                    if (has_total && total_bytes > 0 && has_bytes) {
                        info.progress = std::min(1.0, bytes / total_bytes);
                    }
                    if (has_speed) {
                        info.speed_bytes = speed;
                    }
                    
                    // Parse transferring array to get individual file names
                    // rclone core/stats returns "transferring": [{"name":"file.txt", ...}, ...]
                    size_t trans_arr_pos = rc_output.find("\"transferring\"");
                    if (trans_arr_pos != std::string::npos) {
                        size_t arr_start = rc_output.find("[", trans_arr_pos);
                        size_t arr_end = rc_output.find("]", arr_start);
                        if (arr_start != std::string::npos && arr_end != std::string::npos) {
                            std::string arr_content = rc_output.substr(arr_start, arr_end - arr_start + 1);
                            // Find each "name":"..." entry
                            size_t name_pos = 0;
                            while ((name_pos = arr_content.find("\"name\"", name_pos)) != std::string::npos) {
                                size_t colon = arr_content.find(":", name_pos);
                                if (colon == std::string::npos) break;
                                size_t val_start = arr_content.find("\"", colon + 1);
                                if (val_start == std::string::npos) break;
                                size_t val_end = arr_content.find("\"", val_start + 1);
                                if (val_end == std::string::npos) break;
                                std::string fname = arr_content.substr(val_start + 1, val_end - val_start - 1);
                                if (!fname.empty()) {
                                    info.transferring_files.push_back(fname);
                                }
                                name_pos = val_end + 1;
                            }
                        }
                    }
                }
            }
            
            active_syncs.push_back(info);
        }
    }
    
    // Now update widgets - use stable slots to prevent flicker
    // Count existing item widgets (excluding no_sync_label_)
    std::vector<GtkWidget*> existing_items;
    GtkWidget* child = gtk_widget_get_first_child(sync_activity_list_);
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        const char* name = gtk_widget_get_name(child);
        if (!(name && strcmp(name, "no_sync_label") == 0)) {
            existing_items.push_back(child);
        }
        child = next;
    }
    
    // Update existing items or create new ones as needed
    for (size_t i = 0; i < active_syncs.size(); i++) {
        const auto& info = active_syncs[i];
        
        if (i < existing_items.size()) {
            // Reuse existing widget - just update labels
            GtkWidget* item = existing_items[i];
            GtkWidget* label = GTK_WIDGET(g_object_get_data(G_OBJECT(item), "status_label"));
            GtkWidget* note = GTK_WIDGET(g_object_get_data(G_OBJECT(item), "note_label"));
            GtkWidget* detail_label = GTK_WIDGET(g_object_get_data(G_OBJECT(item), "detail_label"));
            GtkWidget* progress_bar = GTK_WIDGET(g_object_get_data(G_OBJECT(item), "progress_bar"));
            GtkWidget* cancel_btn = GTK_WIDGET(g_object_get_data(G_OBJECT(item), "cancel_btn"));
            
            if (label && GTK_IS_LABEL(label)) {
                gtk_label_set_text(GTK_LABEL(label), info.status_text.c_str());
            }
            
            // Update cancel button PID if it exists
            if (cancel_btn && GTK_IS_BUTTON(cancel_btn)) {
                struct CancelInfo {
                    std::string pid;
                    std::string remote_path;
                };
                auto* cancel_info = new CancelInfo{info.pid, info.remote_path};
                g_object_set_data_full(G_OBJECT(cancel_btn), "cancel_info", cancel_info,
                    +[](gpointer data) { delete static_cast<CancelInfo*>(data); });
            }
            if (note) {
                gtk_widget_set_visible(note, info.is_initial_sync);
            }
            if (detail_label && GTK_IS_LABEL(detail_label)) {
                if (!info.detail.empty()) {
                    gtk_label_set_text(GTK_LABEL(detail_label), info.detail.c_str());
                    gtk_widget_set_visible(detail_label, TRUE);
                } else {
                    gtk_widget_set_visible(detail_label, FALSE);
                }
            }
            if (progress_bar && GTK_IS_PROGRESS_BAR(progress_bar)) {
                if (info.progress >= 0) {
                    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), info.progress);
                    gtk_widget_set_visible(progress_bar, TRUE);
                } else {
                    // Pulse to show activity even without progress data
                    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progress_bar));
                    gtk_widget_set_visible(progress_bar, TRUE);
                }
            }
            gtk_widget_set_visible(item, TRUE);
        } else {
            // Create new item widget
            GtkWidget* item = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
            gtk_widget_add_css_class(item, "sync-activity-item");
            gtk_widget_set_margin_start(item, 4);
            gtk_widget_set_margin_end(item, 4);
            gtk_widget_set_margin_top(item, 4);
            gtk_widget_set_margin_bottom(item, 4);
            
            GtkWidget* top_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            
            GtkWidget* spinner = gtk_spinner_new();
            gtk_spinner_start(GTK_SPINNER(spinner));
            gtk_box_append(GTK_BOX(top_row), spinner);
            
            GtkWidget* label = gtk_label_new(info.status_text.c_str());
            gtk_widget_add_css_class(label, "sync-file-status");
            gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
            gtk_widget_set_hexpand(label, TRUE);
            gtk_label_set_xalign(GTK_LABEL(label), 0);
            gtk_box_append(GTK_BOX(top_row), label);
            g_object_set_data(G_OBJECT(item), "status_label", label);
            
            // Cancel button for this sync
            GtkWidget* cancel_btn = gtk_button_new_from_icon_name("process-stop-symbolic");
            gtk_widget_add_css_class(cancel_btn, "flat");
            gtk_widget_add_css_class(cancel_btn, "circular");
            gtk_widget_add_css_class(cancel_btn, "error");
            gtk_widget_set_tooltip_text(cancel_btn, "Cancel this sync");
            
            // Store PID for cancellation
            struct CancelInfo {
                std::string pid;
                std::string remote_path;
            };
            auto* cancel_info = new CancelInfo{info.pid, info.remote_path};
            g_object_set_data_full(G_OBJECT(cancel_btn), "cancel_info", cancel_info,
                +[](gpointer data) { delete static_cast<CancelInfo*>(data); });
            
            g_signal_connect(cancel_btn, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer) {
                auto* ci = static_cast<CancelInfo*>(g_object_get_data(G_OBJECT(btn), "cancel_info"));
                if (!ci || ci->pid.empty()) return;
                
                // Security: Validate PID is purely numeric to prevent injection
                for (char c : ci->pid) {
                    if (!std::isdigit(static_cast<unsigned char>(c))) {
                        Logger::error("[Cancel] Invalid PID (non-numeric): " + ci->pid);
                        return;
                    }
                }
                
                Logger::info("[Cancel] Stopping sync PID " + ci->pid + " (" + ci->remote_path + ")");
                
                // Use kill() syscall directly instead of shell command to prevent injection
                pid_t pid = static_cast<pid_t>(std::stol(ci->pid));
                if (kill(pid, SIGTERM) != 0) {
                    Logger::warn("[Cancel] SIGTERM failed for PID " + ci->pid + ": " + std::strerror(errno));
                }
                
                // Wait briefly and force kill if still alive
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                kill(pid, SIGKILL);
                
                Logger::info("[Cancel] Sent termination signal to PID " + ci->pid);
            }), nullptr);
            g_object_set_data(G_OBJECT(item), "cancel_btn", cancel_btn);
            
            gtk_box_append(GTK_BOX(top_row), cancel_btn);
            
            gtk_box_append(GTK_BOX(item), top_row);
            
            GtkWidget* note = gtk_label_new("⚡ Scanning all files (first-time sync)");
            gtk_widget_add_css_class(note, "sync-note");
            gtk_label_set_xalign(GTK_LABEL(note), 0);
            gtk_widget_set_margin_start(note, 28);
            gtk_widget_set_visible(note, info.is_initial_sync);
            gtk_box_append(GTK_BOX(item), note);
            g_object_set_data(G_OBJECT(item), "note_label", note);
            
            GtkWidget* detail_label = gtk_label_new("");
            gtk_widget_add_css_class(detail_label, "sync-progress-detail");
            gtk_label_set_xalign(GTK_LABEL(detail_label), 0);
            gtk_widget_set_margin_start(detail_label, 28);
            gtk_widget_set_visible(detail_label, FALSE);
            if (!info.detail.empty()) {
                gtk_label_set_text(GTK_LABEL(detail_label), info.detail.c_str());
                gtk_widget_set_visible(detail_label, TRUE);
            }
            gtk_box_append(GTK_BOX(item), detail_label);
            g_object_set_data(G_OBJECT(item), "detail_label", detail_label);
            
            // Progress bar for sync activity
            GtkWidget* progress_bar = gtk_progress_bar_new();
            gtk_widget_add_css_class(progress_bar, "sync-activity-progress");
            gtk_widget_set_margin_start(progress_bar, 28);
            gtk_widget_set_margin_end(progress_bar, 8);
            if (info.progress >= 0) {
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), info.progress);
            } else {
                gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progress_bar));
            }
            gtk_box_append(GTK_BOX(item), progress_bar);
            g_object_set_data(G_OBJECT(item), "progress_bar", progress_bar);
            
            gtk_box_append(GTK_BOX(sync_activity_list_), item);
        }
    }
    
    // Hide extra existing items (don't destroy - reuse next time)
    for (size_t i = active_syncs.size(); i < existing_items.size(); i++) {
        gtk_widget_set_visible(existing_items[i], FALSE);
    }
    
    // Show/hide no sync label
    gtk_widget_set_visible(no_sync_label_, active_syncs.empty());
    
    // Trigger transfer popup for individual files being transferred
    // This ensures users see file-level progress during bisync/copy operations
    for (const auto& info : active_syncs) {
        if (!info.transferring_files.empty()) {
            for (const auto& fname : info.transferring_files) {
                // Check if this file is already in the transfer list
                bool already_tracked = false;
                for (const auto& item : active_transfers_) {
                    if (item.filename == fname && item.status != "Completed" && item.status != "Failed") {
                        already_tracked = true;
                        break;
                    }
                }
                if (!already_tracked) {
                    bool is_upload = (info.remote_path.find("Syncing") != std::string::npos);
                    add_transfer_item(fname, is_upload);
                }
            }
            
            // Update overall progress for tracked files
            if (info.progress >= 0) {
                for (const auto& fname : info.transferring_files) {
                    std::string speed_str;
                    if (info.speed_bytes > 0) {
                        double spd = info.speed_bytes;
                        const char* units[] = {"B/s", "KB/s", "MB/s", "GB/s"};
                        int idx = 0;
                        while (spd >= 1024.0 && idx < 3) { spd /= 1024.0; idx++; }
                        char buf[64];
                        std::snprintf(buf, sizeof(buf), "%.1f %s", spd, units[idx]);
                        speed_str = buf;
                    }
                    update_transfer_progress(fname, info.progress, speed_str, 0);
                }
            }
        }
    }
    
    // Complete transfers that are no longer in the active transferring list
    if (!active_transfers_.empty()) {
        std::set<std::string> currently_transferring;
        for (const auto& info : active_syncs) {
            for (const auto& fname : info.transferring_files) {
                currently_transferring.insert(fname);
            }
        }
        for (const auto& item : active_transfers_) {
            if (item.status != "Completed" && item.status != "Failed" &&
                currently_transferring.find(item.filename) == currently_transferring.end()) {
                complete_transfer_item(item.filename, true);
            }
        }
    }
    
    // Log state changes to Event Logs panel
    static size_t last_sync_count = 999;
    static std::set<std::string> previous_syncs;
    
    // Build current sync set (by remote path)
    std::set<std::string> current_syncs;
    for (const auto& info : active_syncs) {
        current_syncs.insert(info.remote_path);
    }
    
    // Detect newly started syncs
    for (const auto& path : current_syncs) {
        if (previous_syncs.find(path) == previous_syncs.end()) {
            // Find this sync's info for detailed logging
            for (const auto& info : active_syncs) {
                if (info.remote_path == path) {
                    if (info.is_initial_sync) {
                        append_log("[Sync] Started initial sync: " + path);
                        show_toast("\u26a1 Initial sync started: " + path);
                    } else {
                        append_log("[Sync] Started syncing: " + path);
                        show_toast("\U0001f504 Syncing: " + path);
                    }
                    break;
                }
            }
        }
    }
    
    // Detect completed syncs
    for (const auto& path : previous_syncs) {
        if (current_syncs.find(path) == current_syncs.end()) {
            append_log("[Sync] Completed: " + path);
            show_toast("\u2705 Sync complete: " + path);
            // Don't send desktop notification for successful completion
            // (only errors trigger notifications, success is shown in UI/logs)
            // Refresh both cloud and local browsers when sync completes
            refresh_local_files();
            refresh_cloud_files();
            
            // Check for conflict files after sync completes
            // rclone bisync creates .conflict. files when there are conflicts
            auto& registry = SyncJobRegistry::getInstance();
            std::string clean_path = path;
            if (clean_path.front() == '/') clean_path = clean_path.substr(1);
            for (const auto& job : registry.getAllJobs()) {
                std::string job_remote = job.remote_path;
                if (!job_remote.empty() && job_remote.front() == '/') {
                    job_remote = job_remote.substr(1);
                }
                if (job_remote == clean_path && !job.local_path.empty()) {
                    // Scan for conflict files
                    std::error_code ec;
                    std::vector<std::string> conflict_files;
                    try {
                        for (const auto& entry : fs::recursive_directory_iterator(job.local_path, 
                                fs::directory_options::skip_permission_denied, ec)) {
                            if (ec) break;
                            std::string fname = entry.path().filename().string();
                            if (fname.find(".conflict.") != std::string::npos || 
                                fname.find(".sync-conflict-") != std::string::npos) {
                                conflict_files.push_back(entry.path().string());
                                if (conflict_files.size() >= 10) break; // Limit scan
                            }
                        }
                    } catch (...) {}
                    
                    if (!conflict_files.empty()) {
                        Logger::warn("[Sync] Found " + std::to_string(conflict_files.size()) + 
                                    " conflict file(s) in " + job.local_path);
                        append_log("\u26a0\ufe0f Found " + std::to_string(conflict_files.size()) + 
                                  " conflict file(s) - manual review needed");
                        for (const auto& cf : conflict_files) {
                            append_log("   Conflict: " + cf);
                            store_conflict(cf);  // Store for UI display
                        }
                        
                        // Refresh conflicts UI from main thread
                        g_idle_add([](gpointer data) -> gboolean {
                            static_cast<AppWindow*>(data)->refresh_conflicts();
                            return G_SOURCE_REMOVE;
                        }, this);
                        
                        proton::NotificationManager::getInstance().notify(
                            "Sync Conflicts Detected",
                            std::to_string(conflict_files.size()) + " conflict file(s) in " + path,
                            proton::NotificationType::SYNC_ERROR);
                    }
                    break;
                }
            }
        }
    }
    
    previous_syncs = current_syncs;
    
    if (active_syncs.size() != last_sync_count) {
        Logger::debug("[poll_sync_activity] Active syncs: " + std::to_string(active_syncs.size()));
        last_sync_count = active_syncs.size();
    }
}


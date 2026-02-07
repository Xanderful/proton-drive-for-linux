/**
 * app_window_cloud.cpp
 * 
 * Cloud browser implementation for AppWindow.
 * Handles cloud file listing, navigation, and search.
 */

#include "app_window.hpp"
#include "app_window_helpers.hpp"
#include "logger.hpp"
#include "file_index.hpp"
#include "sync_job_metadata.hpp"
#include <thread>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

using AppWindowHelpers::exec_rclone;
using AppWindowHelpers::exec_rclone_with_timeout;
using AppWindowHelpers::run_system;
using AppWindowHelpers::get_sync_status_for_path;
using AppWindowHelpers::format_file_size;
using AppWindowHelpers::shell_escape;
using AppWindowHelpers::safe_exists;

void AppWindow::invalidate_cloud_cache() {
    cloud_cache_.clear();
    Logger::debug("[CloudBrowser] Cloud cache invalidated");
}

void AppWindow::refresh_cloud_files() {
    refresh_cloud_files_async(false);
}

void AppWindow::refresh_cloud_files_async(bool force_refresh) {
    if (!cloud_tree_) return;
    
    Logger::info("[CloudBrowser] Refreshing cloud files at path: " + current_cloud_path_ + (force_refresh ? " (forced)" : ""));
    
    gtk_label_set_text(GTK_LABEL(path_bar_), current_cloud_path_.c_str());
    
    // If force_refresh, clear the cache for this path
    if (force_refresh) {
        cloud_cache_.erase(current_cloud_path_);
    }
    
    // Check memory cache first (not force refresh)
    if (!force_refresh) {
        auto it = cloud_cache_.find(current_cloud_path_);
        if (it != cloud_cache_.end() && it->second.valid()) {
            Logger::debug("[CloudBrowser] Using cached data for: " + current_cloud_path_);
            populate_cloud_tree(it->second.json_data);
            return;
        }
    }
    
    // For force refresh, skip FileIndex cache entirely and go straight to rclone
    // This ensures we get fresh data after uploads/deletes
    if (force_refresh) {
        // Don't clear the tree - keep existing items visible while loading
        // Just fetch fresh data in background
        std::string path_copy = current_cloud_path_;
        std::thread([this, path_copy]() {
            std::string output = exec_rclone_with_timeout("lsjson --fast-list " + shell_escape("proton:" + path_copy), 30);
            Logger::info("[CloudBrowser] rclone lsjson returned " + std::to_string(output.size()) + " bytes (force refresh)");
            
            CloudFileCache cache_entry;
            cache_entry.path = path_copy;
            cache_entry.json_data = output;
            cache_entry.timestamp = std::chrono::steady_clock::now();
            
            struct UpdateData {
                AppWindow* self;
                std::string path;
                std::string json_data;
                CloudFileCache cache_entry;
            };
            auto* data = new UpdateData{this, path_copy, output, cache_entry};
            
            g_idle_add(+[](gpointer user_data) -> gboolean {
                auto* d = static_cast<UpdateData*>(user_data);
                if (d->self->current_cloud_path_ == d->path) {
                    d->self->cloud_cache_[d->path] = d->cache_entry;
                    d->self->populate_cloud_tree(d->json_data);
                }
                delete d;
                return G_SOURCE_REMOVE;
            }, data);
        }).detach();
        return;
    }
    
    // Try FileIndex cache for non-forced refreshes
    auto& file_index = FileIndex::getInstance();
    std::string index_path = "proton:" + current_cloud_path_;
    auto cached_contents = file_index.get_directory_contents(index_path);
    
    if (!cached_contents.empty()) {
        Logger::debug("[CloudBrowser] Using FileIndex cache for: " + current_cloud_path_ + " (" + std::to_string(cached_contents.size()) + " items)");
        populate_cloud_tree_from_index(cached_contents);
        
        // Also fetch fresh data in background for next time
        std::string path_copy = current_cloud_path_;
        std::thread([this, path_copy]() {
            std::string output = exec_rclone_with_timeout("lsjson --fast-list " + shell_escape("proton:" + path_copy), 30);
            Logger::info("[CloudBrowser] rclone lsjson returned " + std::to_string(output.size()) + " bytes (background)");
            
            CloudFileCache cache_entry;
            cache_entry.path = path_copy;
            cache_entry.json_data = output;
            cache_entry.timestamp = std::chrono::steady_clock::now();
            
            struct UpdateData {
                AppWindow* self;
                std::string path;
                std::string json_data;
                CloudFileCache cache_entry;
            };
            auto* data = new UpdateData{this, path_copy, output, cache_entry};
            
            g_idle_add(+[](gpointer user_data) -> gboolean {
                auto* d = static_cast<UpdateData*>(user_data);
                // Update cache and UI if still on same path
                if (d->self->current_cloud_path_ == d->path) {
                    d->self->cloud_cache_[d->path] = d->cache_entry;
                    // Also update the UI with fresh data
                    d->self->populate_cloud_tree(d->json_data);
                    Logger::info("[CloudBrowser] Updated view with fresh data from rclone");
                }
                delete d;
                return G_SOURCE_REMOVE;
            }, data);
        }).detach();
        return;
    }
    
    // No cache at all - show loading indicator and fetch
    while (TRUE) {
        GtkListBoxRow* row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(cloud_tree_), 0);
        if (!row) break;
        gtk_list_box_remove(GTK_LIST_BOX(cloud_tree_), GTK_WIDGET(row));
    }
    
    GtkWidget* loading_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(loading_row, 12);
    gtk_widget_set_margin_top(loading_row, 20);
    GtkWidget* spinner = gtk_spinner_new();
    gtk_spinner_start(GTK_SPINNER(spinner));
    gtk_box_append(GTK_BOX(loading_row), spinner);
    GtkWidget* loading_label = gtk_label_new("Loading...");
    gtk_widget_add_css_class(loading_label, "dim-label");
    gtk_box_append(GTK_BOX(loading_row), loading_label);
    gtk_list_box_append(GTK_LIST_BOX(cloud_tree_), loading_row);
    
    std::string path_copy = current_cloud_path_;
    
    // Safety timeout: if loading takes >35 seconds, show error state
    struct LoadingTimeoutData {
        AppWindow* self;
        std::string path;
    };
    auto* timeout_data = new LoadingTimeoutData{this, path_copy};
    g_timeout_add(35000, +[](gpointer user_data) -> gboolean {
        auto* d = static_cast<LoadingTimeoutData*>(user_data);
        if (!d->self->cloud_tree_) { delete d; return G_SOURCE_REMOVE; }
        // Check if first row is still the loading spinner (if tree hasn't been updated)
        GtkListBoxRow* first_row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(d->self->cloud_tree_), 0);
        if (first_row && d->self->current_cloud_path_ == d->path) {
            GtkWidget* child = gtk_list_box_row_get_child(first_row);
            // Check if it's still showing loading (only 1 row = loading spinner)
            GtkListBoxRow* second_row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(d->self->cloud_tree_), 1);
            if (!second_row && child) {
                // Still loading - show error message
                while (TRUE) {
                    GtkListBoxRow* r = gtk_list_box_get_row_at_index(GTK_LIST_BOX(d->self->cloud_tree_), 0);
                    if (!r) break;
                    gtk_list_box_remove(GTK_LIST_BOX(d->self->cloud_tree_), GTK_WIDGET(r));
                }
                GtkWidget* err_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
                gtk_widget_set_margin_start(err_row, 12);
                gtk_widget_set_margin_top(err_row, 20);
                GtkWidget* icon = gtk_image_new_from_icon_name("dialog-warning-symbolic");
                gtk_box_append(GTK_BOX(err_row), icon);
                GtkWidget* err_label = gtk_label_new("Timed out loading cloud files. Click refresh to try again.");
                gtk_widget_add_css_class(err_label, "dim-label");
                gtk_box_append(GTK_BOX(err_row), err_label);
                gtk_list_box_append(GTK_LIST_BOX(d->self->cloud_tree_), err_row);
                Logger::warn("[CloudBrowser] Loading timed out for: " + d->path);
                d->self->append_log("[CloudBrowser] Timed out loading " + d->path);
            }
        }
        delete d;
        return G_SOURCE_REMOVE;
    }, timeout_data);
    
    std::thread([this, path_copy]() {
        std::string output = exec_rclone_with_timeout("lsjson --fast-list " + shell_escape("proton:" + path_copy), 30);
        Logger::info("[CloudBrowser] rclone lsjson returned " + std::to_string(output.size()) + " bytes");
        
        CloudFileCache cache_entry;
        cache_entry.path = path_copy;
        cache_entry.json_data = output;
        cache_entry.timestamp = std::chrono::steady_clock::now();
        
        struct UpdateData {
            AppWindow* self;
            std::string path;
            std::string json_data;
            CloudFileCache cache_entry;
        };
        auto* data = new UpdateData{this, path_copy, output, cache_entry};
        
        g_idle_add(+[](gpointer user_data) -> gboolean {
            auto* d = static_cast<UpdateData*>(user_data);
            if (d->self->current_cloud_path_ == d->path) {
                d->self->cloud_cache_[d->path] = d->cache_entry;
                d->self->populate_cloud_tree(d->json_data);
            }
            delete d;
            return G_SOURCE_REMOVE;
        }, data);
    }).detach();
}

void AppWindow::populate_cloud_tree(const std::string& output) {
    if (!cloud_tree_) return;
    
    while (TRUE) {
        GtkListBoxRow* row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(cloud_tree_), 0);
        if (!row) break;
        gtk_list_box_remove(GTK_LIST_BOX(cloud_tree_), GTK_WIDGET(row));
    }
    
    if (output.empty()) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_start(row, 12);
        gtk_widget_set_margin_end(row, 12);
        gtk_widget_set_margin_top(row, 8);
        gtk_widget_set_margin_bottom(row, 8);
        
        GtkWidget* label = gtk_label_new("No files or unable to list cloud (check profile)");
        gtk_widget_add_css_class(label, "no-sync-label");
        gtk_box_append(GTK_BOX(row), label);
        gtk_list_box_append(GTK_LIST_BOX(cloud_tree_), row);
        return;
    }
    
    std::vector<std::tuple<std::string, int64_t, bool, std::string>> files;
    // Also collect files for indexing
    std::vector<IndexedFile> files_to_index;
    
    size_t pos = 0;
    while ((pos = output.find("{", pos)) != std::string::npos) {
        size_t end = output.find("}", pos);
        if (end == std::string::npos) break;
        
        std::string obj = output.substr(pos, end - pos + 1);
        
        std::string name, mod_time;
        int64_t size = 0;
        bool is_dir = false;
        
        size_t name_pos = obj.find("\"Name\":");
        if (name_pos != std::string::npos) {
            size_t start = obj.find("\"", name_pos + 7);
            size_t name_end = obj.find("\"", start + 1);
            if (start != std::string::npos && name_end != std::string::npos) {
                name = obj.substr(start + 1, name_end - start - 1);
            }
        }
        
        size_t size_pos = obj.find("\"Size\":");
        if (size_pos != std::string::npos) {
            size_t start = size_pos + 7;
            size_t size_end = obj.find_first_of(",}", start);
            if (size_end != std::string::npos) {
                try {
                    size = std::stoll(obj.substr(start, size_end - start));
                } catch (...) {}
            }
        }
        
        is_dir = obj.find("\"IsDir\":true") != std::string::npos;
        
        size_t mod_pos = obj.find("\"ModTime\":");
        if (mod_pos != std::string::npos) {
            size_t start = obj.find("\"", mod_pos + 10);
            size_t mod_end = obj.find("\"", start + 1);
            if (start != std::string::npos && mod_end != std::string::npos) {
                mod_time = obj.substr(start + 1, mod_end - start - 1);
                if (mod_time.length() > 10) {
                    mod_time = mod_time.substr(0, 10);
                }
            }
        }
        
        if (!name.empty()) {
            files.emplace_back(name, size, is_dir, mod_time);
            
            // Prepare file for indexing
            std::string full_path = current_cloud_path_;
            if (full_path.back() != '/') full_path += "/";
            full_path += name;
            
            IndexedFile indexed;
            indexed.path = "proton:" + full_path;
            indexed.name = name;
            indexed.is_directory = is_dir;
            indexed.size = size;
            indexed.mod_time = mod_time;
            indexed.is_synced = false;
            indexed.local_path = "";
            
            files_to_index.push_back(indexed);
        }
        
        pos = end + 1;
    }
    
    // Add discovered files to the index in background and prune stale entries
    if (!files_to_index.empty()) {
        std::string index_parent = "proton:" + current_cloud_path_;
        std::thread([files_to_index, index_parent]() {
            try {
                auto& file_index = FileIndex::getInstance();
                
                // Collect paths we just saw for stale entry pruning
                std::vector<std::string> paths_seen;
                paths_seen.reserve(files_to_index.size());
                
                for (const auto& file : files_to_index) {
                    file_index.add_or_update_file(
                        file.path,
                        file.name,
                        file.size,
                        file.mod_time,
                        file.is_directory,
                        false,  // is_synced
                        ""      // local_path
                    );
                    paths_seen.push_back(file.path);
                }
                
                // Prune entries in this directory that no longer exist
                file_index.prune_stale_entries(index_parent, paths_seen);
                
                Logger::info("[CloudBrowser] Indexed " + std::to_string(files_to_index.size()) + 
                           " files and pruned stale entries for: " + index_parent);
            } catch (const std::exception& e) {
                Logger::warn("[CloudBrowser] Failed to index files: " + std::string(e.what()));
            }
        }).detach();
    }
    
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        if (std::get<2>(a) != std::get<2>(b)) return std::get<2>(a) > std::get<2>(b);
        return std::get<0>(a) < std::get<0>(b);
    });
    
    for (const auto& [name, size, is_dir, mod_time] : files) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_add_css_class(row, "file-row");
        gtk_widget_set_margin_start(row, 8);
        gtk_widget_set_margin_end(row, 8);
        gtk_widget_set_margin_top(row, 4);
        gtk_widget_set_margin_bottom(row, 4);
        
        const char* icon_name = is_dir ? "folder-symbolic" : "text-x-generic-symbolic";
        GtkWidget* icon = gtk_image_new_from_icon_name(icon_name);
        gtk_box_append(GTK_BOX(row), icon);
        
        GtkWidget* info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_hexpand(info_box, TRUE);
        
        GtkWidget* name_label = gtk_label_new(name.c_str());
        gtk_widget_add_css_class(name_label, "file-name");
        gtk_label_set_xalign(GTK_LABEL(name_label), 0);
        gtk_label_set_ellipsize(GTK_LABEL(name_label), PANGO_ELLIPSIZE_END);
        gtk_box_append(GTK_BOX(info_box), name_label);
        
        if (!is_dir) {
            std::string details = format_file_size(size);
            if (!mod_time.empty()) details += " • " + mod_time;
            GtkWidget* details_label = gtk_label_new(details.c_str());
            gtk_widget_add_css_class(details_label, "file-size");
            gtk_label_set_xalign(GTK_LABEL(details_label), 0);
            gtk_box_append(GTK_BOX(info_box), details_label);
        }
        
        gtk_box_append(GTK_BOX(row), info_box);
        
        std::string full_path = current_cloud_path_;
        if (full_path.back() != '/') full_path += "/";
        full_path += name;
        
        auto [sync_status_text, sync_badge_class] = get_sync_status_for_path(full_path);
        
        Logger::debug("[CloudBrowser] File: " + name + " status: " + sync_status_text + " class: " + sync_badge_class);
        
        if (sync_status_text.empty()) {
            sync_status_text = "☁ Cloud";
            sync_badge_class = "sync-badge-cloud";
        }
        
        // Auto-download files that are pending (in a sync job but not downloaded yet)
        // Uses deduplication to prevent infinite loops - only downloads each file once
        if (sync_badge_class == "sync-badge-pending" && !is_dir) {
            // Check if already downloading this file (deduplication)
            {
                std::lock_guard<std::mutex> lock(download_mutex_);
                if (active_downloads_.count(full_path) > 0) {
                    Logger::debug("[AutoDownload] Already downloading, skipping: " + name);
                    // Skip - already in progress
                } else {
                    // Mark as downloading BEFORE spawning thread
                    active_downloads_.insert(full_path);
                    Logger::info("[AutoDownload] Queued for download: " + name + " (active: " + std::to_string(active_downloads_.size()) + ")");
                    
                    std::string path_copy = full_path;
                    std::thread([this, path_copy, name]() {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        
                        // Find the local path for this file
                        auto& registry = SyncJobRegistry::getInstance();
                        auto jobs = registry.getAllJobs();
                        bool download_attempted = false;
                        
                        for (const auto& job : jobs) {
                            std::string remote_check = job.remote_path;
                            if (!remote_check.empty() && remote_check.front() != '/') {
                                remote_check = "/" + remote_check;
                            }
                            
                            std::string check_path = path_copy;
                            if (!check_path.empty() && check_path.front() != '/') {
                                check_path = "/" + check_path;
                            }
                            
                            if (check_path.find(remote_check) == 0) {
                                // Calculate local path
                                std::string relative = check_path.substr(remote_check.length());
                                if (!relative.empty() && relative.front() == '/') {
                                    relative = relative.substr(1);
                                }
                                std::string local_file = job.local_path;
                                if (!relative.empty()) {
                                    if (local_file.back() != '/') local_file += "/";
                                    local_file += relative;
                                }
                                
                                // Ensure parent directory exists
                                fs::path parent = fs::path(local_file).parent_path();
                                if (!parent.empty() && !safe_exists(parent.string())) {
                                    std::error_code ec_mk;
                                    fs::create_directories(parent, ec_mk);
                                }
                                
                                // Download the file
                                Logger::info("[AutoDownload] Downloading: " + path_copy + " -> " + local_file);
                                download_attempted = true;
                                
                                struct DownloadData {
                                    AppWindow* self;
                                    std::string filename;
                                };
                                auto* dl_data = new DownloadData{this, name};
                                
                                g_idle_add(+[](gpointer user_data) -> gboolean {
                                    auto* d = static_cast<DownloadData*>(user_data);
                                    d->self->add_transfer_item(d->filename, false);  // false = download
                                    d->self->append_log("[AutoDownload] Downloading: " + d->filename);
                                    delete d;
                                    return G_SOURCE_REMOVE;
                                }, dl_data);
                                
                                std::string rclone_path = AppWindowHelpers::get_rclone_path();
                                std::string cmd = "timeout 300 " + rclone_path + " copyto " +
                                    shell_escape("proton:" + path_copy) + " " + 
                                    shell_escape(local_file) + " --progress 2>&1";
                                
                                FILE* pipe = popen(cmd.c_str(), "r");
                                bool success = false;
                                if (pipe) {
                                    char buffer[256];
                                    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                                        // Could parse progress here
                                    }
                                    int ret = pclose(pipe);
                                    success = (ret == 0);
                                }
                                
                                // Remove from active downloads and check if we should refresh
                                bool should_refresh = false;
                                {
                                    std::lock_guard<std::mutex> lock(download_mutex_);
                                    active_downloads_.erase(path_copy);
                                    // Only refresh when ALL downloads are complete
                                    should_refresh = active_downloads_.empty();
                                    Logger::info("[AutoDownload] Completed: " + name + " (remaining: " + std::to_string(active_downloads_.size()) + ")");
                                }
                                
                                // Update UI on main thread
                                struct CompleteData {
                                    AppWindow* self;
                                    std::string filename;
                                    bool success;
                                    bool should_refresh;
                                };
                                auto* comp_data = new CompleteData{this, name, success, should_refresh};
                                
                                g_idle_add(+[](gpointer user_data) -> gboolean {
                                    auto* d = static_cast<CompleteData*>(user_data);
                                    d->self->complete_transfer_item(d->filename, d->success);
                                    if (d->success) {
                                        d->self->append_log("[AutoDownload] Completed: " + d->filename);
                                    } else {
                                        d->self->append_log("[AutoDownload] Failed: " + d->filename);
                                    }
                                    // Only refresh once ALL downloads in the batch are done
                                    if (d->should_refresh) {
                                        d->self->append_log("[AutoDownload] All downloads complete, refreshing view...");
                                        d->self->refresh_cloud_files_async(true);
                                    }
                                    delete d;
                                    return G_SOURCE_REMOVE;
                                }, comp_data);
                                break;
                            }
                        }
                        
                        // If no job matched, still remove from tracking
                        if (!download_attempted) {
                            std::lock_guard<std::mutex> lock(download_mutex_);
                            active_downloads_.erase(path_copy);
                        }
                    }).detach();
                }
            }
        }
        
        GtkWidget* badge = gtk_label_new(sync_status_text.c_str());
        gtk_widget_add_css_class(badge, "sync-badge");
        gtk_widget_add_css_class(badge, sync_badge_class.c_str());
        gtk_box_append(GTK_BOX(row), badge);
        
        gtk_list_box_append(GTK_LIST_BOX(cloud_tree_), row);
        
        GtkListBoxRow* list_row = GTK_LIST_BOX_ROW(gtk_widget_get_parent(row));
        if (list_row) {
            char* path_data = g_strdup(full_path.c_str());
            g_object_set_data_full(G_OBJECT(list_row), "path", path_data, g_free);
            g_object_set_data(G_OBJECT(list_row), "is_dir", GINT_TO_POINTER(is_dir ? 1 : 0));
        } else {
            Logger::error("[CloudBrowser] Failed to get GtkListBoxRow wrapper for path: " + full_path);
        }
    }
}

void AppWindow::populate_cloud_tree_from_index(const std::vector<IndexedFile>& files) {
    if (!cloud_tree_) return;
    
    while (TRUE) {
        GtkListBoxRow* row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(cloud_tree_), 0);
        if (!row) break;
        gtk_list_box_remove(GTK_LIST_BOX(cloud_tree_), GTK_WIDGET(row));
    }
    
    if (files.empty()) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_start(row, 12);
        gtk_widget_set_margin_end(row, 12);
        gtk_widget_set_margin_top(row, 8);
        gtk_widget_set_margin_bottom(row, 8);
        
        GtkWidget* label = gtk_label_new("Folder is empty");
        gtk_widget_add_css_class(label, "no-sync-label");
        gtk_box_append(GTK_BOX(row), label);
        gtk_list_box_append(GTK_LIST_BOX(cloud_tree_), row);
        return;
    }
    
    for (const auto& file : files) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_add_css_class(row, "file-row");
        gtk_widget_set_margin_start(row, 8);
        gtk_widget_set_margin_end(row, 8);
        gtk_widget_set_margin_top(row, 4);
        gtk_widget_set_margin_bottom(row, 4);
        
        const char* icon_name = file.is_directory ? "folder-symbolic" : "text-x-generic-symbolic";
        GtkWidget* icon = gtk_image_new_from_icon_name(icon_name);
        gtk_box_append(GTK_BOX(row), icon);
        
        GtkWidget* info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_hexpand(info_box, TRUE);
        
        GtkWidget* name_label = gtk_label_new(file.name.c_str());
        gtk_widget_add_css_class(name_label, "file-name");
        gtk_label_set_xalign(GTK_LABEL(name_label), 0);
        gtk_label_set_ellipsize(GTK_LABEL(name_label), PANGO_ELLIPSIZE_END);
        gtk_box_append(GTK_BOX(info_box), name_label);
        
        if (!file.is_directory) {
            std::string details = format_file_size(file.size);
            if (!file.mod_time.empty()) {
                std::string date = file.mod_time.substr(0, 10);
                details += " • " + date;
            }
            GtkWidget* details_label = gtk_label_new(details.c_str());
            gtk_widget_add_css_class(details_label, "file-size");
            gtk_label_set_xalign(GTK_LABEL(details_label), 0);
            gtk_box_append(GTK_BOX(info_box), details_label);
        }
        
        gtk_box_append(GTK_BOX(row), info_box);
        
        std::string display_path = file.path;
        if (display_path.find("proton:") == 0) {
            display_path = display_path.substr(7);
        }
        
        auto [sync_status_text, sync_badge_class] = get_sync_status_for_path(file.path);
        
        if (sync_status_text.empty()) {
            sync_status_text = "☁ Cloud";
            sync_badge_class = "sync-badge-cloud";
        }
        
        GtkWidget* badge = gtk_label_new(sync_status_text.c_str());
        gtk_widget_add_css_class(badge, "sync-badge");
        gtk_widget_add_css_class(badge, sync_badge_class.c_str());
        gtk_box_append(GTK_BOX(row), badge);
        
        gtk_list_box_append(GTK_LIST_BOX(cloud_tree_), row);
        
        GtkListBoxRow* list_row = GTK_LIST_BOX_ROW(gtk_widget_get_parent(row));
        if (list_row) {
            char* path_data = g_strdup(display_path.c_str());
            g_object_set_data_full(G_OBJECT(list_row), "path", path_data, g_free);
            g_object_set_data(G_OBJECT(list_row), "is_dir", GINT_TO_POINTER(file.is_directory ? 1 : 0));
        } else {
            Logger::error("[CloudBrowser] Failed to get GtkListBoxRow wrapper for path: " + display_path);
        }
    }
    
    Logger::debug("[CloudBrowser] Populated cloud tree from FileIndex with " + std::to_string(files.size()) + " items");
}

void AppWindow::perform_search(const std::string& query) {
    if (!cloud_tree_ || query.empty()) return;
    
    Logger::info("[Search] Searching for: " + query);
    
    while (TRUE) {
        GtkListBoxRow* row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(cloud_tree_), 0);
        if (!row) break;
        gtk_list_box_remove(GTK_LIST_BOX(cloud_tree_), GTK_WIDGET(row));
    }
    
    GtkWidget* loading_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(loading_row, 12);
    gtk_widget_set_margin_top(loading_row, 20);
    GtkWidget* spinner = gtk_spinner_new();
    gtk_spinner_start(GTK_SPINNER(spinner));
    gtk_box_append(GTK_BOX(loading_row), spinner);
    GtkWidget* loading_label = gtk_label_new("Searching...");
    gtk_widget_add_css_class(loading_label, "dim-label");
    gtk_box_append(GTK_BOX(loading_row), loading_label);
    gtk_list_box_append(GTK_LIST_BOX(cloud_tree_), loading_row);
    
    auto& file_index = FileIndex::getInstance();
    auto stats = file_index.get_stats();
    Logger::info("[Search] Index stats: " + std::to_string(stats.total_files) + " files, " + 
                 std::to_string(stats.total_folders) + " folders" +
                 (stats.is_indexing ? " (indexing: " + std::to_string(stats.index_progress_percent) + "%)" : ""));
    
    std::vector<IndexedFile> results = file_index.search(query, 50, true);
    Logger::info("[Search] Found " + std::to_string(results.size()) + " results for \"" + query + "\"");
    
    while (TRUE) {
        GtkListBoxRow* row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(cloud_tree_), 0);
        if (!row) break;
        gtk_list_box_remove(GTK_LIST_BOX(cloud_tree_), GTK_WIDGET(row));
    }
    
    if (results.empty()) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_start(row, 12);
        gtk_widget_set_margin_top(row, 20);
        
        if (stats.is_indexing) {
            GtkWidget* spinner2 = gtk_spinner_new();
            gtk_spinner_start(GTK_SPINNER(spinner2));
            gtk_box_append(GTK_BOX(row), spinner2);
            std::string msg = "No results yet for \"" + query + "\" (indexing " + 
                             std::to_string(stats.index_progress_percent) + "% complete)";
            GtkWidget* label = gtk_label_new(msg.c_str());
            gtk_widget_add_css_class(label, "dim-label");
            gtk_box_append(GTK_BOX(row), label);
            
            pending_search_query_ = query;
            g_timeout_add(3000, +[](gpointer data) -> gboolean {
                auto* self = static_cast<AppWindow*>(data);
                if (self->pending_search_query_.empty()) return G_SOURCE_REMOVE;
                
                auto& idx = FileIndex::getInstance();
                auto st = idx.get_stats();
                if (!st.is_indexing) {
                    std::string q = self->pending_search_query_;
                    self->pending_search_query_.clear();
                    self->perform_search(q);
                    return G_SOURCE_REMOVE;
                }
                self->perform_search(self->pending_search_query_);
                return G_SOURCE_REMOVE;
            }, this);
        } else if (stats.total_files == 0 && stats.total_folders == 0) {
            GtkWidget* icon = gtk_image_new_from_icon_name("dialog-information-symbolic");
            gtk_box_append(GTK_BOX(row), icon);
            GtkWidget* label = gtk_label_new("Search index is empty. Go to Settings → Rebuild Index.");
            gtk_widget_add_css_class(label, "dim-label");
            gtk_box_append(GTK_BOX(row), label);
        } else {
            GtkWidget* label = gtk_label_new(("No results for \"" + query + "\"").c_str());
            gtk_widget_add_css_class(label, "dim-label");
            gtk_box_append(GTK_BOX(row), label);
        }
        gtk_list_box_append(GTK_LIST_BOX(cloud_tree_), row);
        return;
    }
    
    gtk_label_set_text(GTK_LABEL(path_bar_), ("Search: " + query).c_str());
    
    for (const auto& file : results) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_add_css_class(row, "file-row");
        gtk_widget_set_margin_start(row, 8);
        gtk_widget_set_margin_end(row, 8);
        gtk_widget_set_margin_top(row, 4);
        gtk_widget_set_margin_bottom(row, 4);
        
        const char* icon_name = file.is_directory ? "folder-symbolic" : "text-x-generic-symbolic";
        GtkWidget* icon = gtk_image_new_from_icon_name(icon_name);
        gtk_box_append(GTK_BOX(row), icon);
        
        GtkWidget* info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_hexpand(info_box, TRUE);
        
        GtkWidget* name_label = gtk_label_new(file.name.c_str());
        gtk_widget_add_css_class(name_label, "file-name");
        gtk_label_set_xalign(GTK_LABEL(name_label), 0);
        gtk_label_set_ellipsize(GTK_LABEL(name_label), PANGO_ELLIPSIZE_END);
        gtk_box_append(GTK_BOX(info_box), name_label);
        
        GtkWidget* path_label = gtk_label_new(file.path.c_str());
        gtk_widget_add_css_class(path_label, "file-size");
        gtk_label_set_xalign(GTK_LABEL(path_label), 0);
        gtk_label_set_ellipsize(GTK_LABEL(path_label), PANGO_ELLIPSIZE_MIDDLE);
        gtk_box_append(GTK_BOX(info_box), path_label);
        
        gtk_box_append(GTK_BOX(row), info_box);
        
        if (!file.is_directory) {
            GtkWidget* size_label = gtk_label_new(format_file_size(file.size).c_str());
            gtk_widget_add_css_class(size_label, "file-size");
            gtk_box_append(GTK_BOX(row), size_label);
        }
        
        gtk_list_box_append(GTK_LIST_BOX(cloud_tree_), row);
        
        GtkListBoxRow* list_row = GTK_LIST_BOX_ROW(gtk_widget_get_parent(row));
        if (list_row) {
            std::string full_path = file.path;
            char* path_data = g_strdup(full_path.c_str());
            g_object_set_data_full(G_OBJECT(list_row), "path", path_data, g_free);
            g_object_set_data(G_OBJECT(list_row), "is_dir", GINT_TO_POINTER(file.is_directory ? 1 : 0));
        }
    }
}

void AppWindow::refresh_local_files() {
    if (!local_tree_) return;
    
    while (TRUE) {
        GtkListBoxRow* row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(local_tree_), 0);
        if (!row) break;
        gtk_list_box_remove(GTK_LIST_BOX(local_tree_), GTK_WIDGET(row));
    }
    
    GtkWidget* local_path_label = GTK_WIDGET(g_object_get_data(G_OBJECT(file_browser_box_), "local_path_label"));
    if (local_path_label) {
        const char* home = getenv("HOME");
        std::string display_path = current_local_path_;
        if (home && current_local_path_.find(home) == 0) {
            display_path = "~" + current_local_path_.substr(strlen(home));
        }
        gtk_label_set_text(GTK_LABEL(local_path_label), display_path.c_str());
    }
    
    if (!safe_exists(current_local_path_)) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_start(row, 12);
        gtk_widget_set_margin_end(row, 12);
        gtk_widget_set_margin_top(row, 8);
        gtk_widget_set_margin_bottom(row, 8);
        
        GtkWidget* label = gtk_label_new("Folder does not exist");
        gtk_widget_add_css_class(label, "no-sync-label");
        gtk_box_append(GTK_BOX(row), label);
        gtk_list_box_append(GTK_LIST_BOX(local_tree_), row);
        return;
    }
    
    std::vector<std::tuple<std::string, int64_t, bool, std::string>> files;
    
    try {
        for (const auto& entry : fs::directory_iterator(current_local_path_)) {
            std::string name = entry.path().filename().string();
            bool is_dir = entry.is_directory();
            int64_t size = 0;
            std::string mod_time;
            
            if (!is_dir) {
                try { size = static_cast<int64_t>(entry.file_size()); } catch (...) {}
            }
            
            try {
                auto ftime = entry.last_write_time();
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
                );
                std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);
                char buf[32];
                std::strftime(buf, sizeof(buf), "%Y-%m-%d", std::localtime(&cftime));
                mod_time = buf;
            } catch (...) {}
            
            files.emplace_back(name, size, is_dir, mod_time);
        }
    } catch (const std::exception& e) {
        append_log("[Error] Failed to list local files: " + std::string(e.what()));
    }
    
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        if (std::get<2>(a) != std::get<2>(b)) return std::get<2>(a) > std::get<2>(b);
        return std::get<0>(a) < std::get<0>(b);
    });
    
    if (files.empty()) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_start(row, 12);
        gtk_widget_set_margin_top(row, 8);
        
        GtkWidget* label = gtk_label_new("Empty folder");
        gtk_widget_add_css_class(label, "no-sync-label");
        gtk_box_append(GTK_BOX(row), label);
        gtk_list_box_append(GTK_LIST_BOX(local_tree_), row);
        return;
    }
    
    for (const auto& [name, size, is_dir, mod_time] : files) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_add_css_class(row, "file-row");
        gtk_widget_set_margin_start(row, 8);
        gtk_widget_set_margin_end(row, 8);
        gtk_widget_set_margin_top(row, 4);
        gtk_widget_set_margin_bottom(row, 4);
        
        const char* icon_name = is_dir ? "folder-symbolic" : "text-x-generic-symbolic";
        GtkWidget* icon = gtk_image_new_from_icon_name(icon_name);
        gtk_box_append(GTK_BOX(row), icon);
        
        GtkWidget* info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_hexpand(info_box, TRUE);
        
        GtkWidget* name_label = gtk_label_new(name.c_str());
        gtk_widget_add_css_class(name_label, "file-name");
        gtk_label_set_xalign(GTK_LABEL(name_label), 0);
        gtk_label_set_ellipsize(GTK_LABEL(name_label), PANGO_ELLIPSIZE_END);
        gtk_box_append(GTK_BOX(info_box), name_label);
        
        if (!is_dir) {
            std::string details = format_file_size(size);
            if (!mod_time.empty()) details += " • " + mod_time;
            GtkWidget* details_label = gtk_label_new(details.c_str());
            gtk_widget_add_css_class(details_label, "file-size");
            gtk_label_set_xalign(GTK_LABEL(details_label), 0);
            gtk_box_append(GTK_BOX(info_box), details_label);
        }
        
        gtk_box_append(GTK_BOX(row), info_box);
        
        GtkWidget* badge = gtk_label_new("✓ Synced");
        gtk_widget_add_css_class(badge, "sync-badge");
        gtk_widget_add_css_class(badge, "sync-badge-synced");
        gtk_box_append(GTK_BOX(row), badge);
        
        gtk_list_box_append(GTK_LIST_BOX(local_tree_), row);
        
        std::string full_path = current_local_path_ + "/" + name;
        GtkListBoxRow* list_row = GTK_LIST_BOX_ROW(gtk_widget_get_parent(row));
        if (list_row) {
            char* path_data = g_strdup(full_path.c_str());
            g_object_set_data_full(G_OBJECT(list_row), "path", path_data, g_free);
            g_object_set_data(G_OBJECT(list_row), "is_dir", GINT_TO_POINTER(is_dir ? 1 : 0));
        }
    }
}

void AppWindow::navigate_cloud(const std::string& path) {
    current_cloud_path_ = path;
    if (current_cloud_path_.find("proton:") == 0) {
        current_cloud_path_ = current_cloud_path_.substr(7);
    }
    if (current_cloud_path_.empty()) current_cloud_path_ = "/";
    refresh_cloud_files();
    append_log("[Browse] Cloud: " + current_cloud_path_);
}

void AppWindow::navigate_local(const std::string& path) {
    current_local_path_ = path;
    refresh_local_files();
    append_log("[Browse] Local: " + current_local_path_);
}

void AppWindow::on_cloud_row_activated(const std::string& path, bool is_dir) {
    Logger::info("[CloudBrowser] Row activated (double-click): " + path + " is_dir=" + (is_dir ? "yes" : "no"));
    if (is_dir) {
        navigate_cloud(path);
    } else {
        // Check if file is already synced locally
        auto& registry = SyncJobRegistry::getInstance();
        auto jobs = registry.getAllJobs();
        
        std::string local_file_path;
        for (const auto& job : jobs) {
            // Check if this file is under a synced folder
            std::string remote_prefix = job.remote_path;
            if (path == remote_prefix || path.find(remote_prefix + "/") == 0) {
                // Calculate relative path from sync root
                std::string relative = path.substr(remote_prefix.length());
                if (!relative.empty() && relative[0] == '/') {
                    relative = relative.substr(1);
                }
                local_file_path = job.local_path;
                if (!relative.empty()) {
                    if (local_file_path.back() != '/') local_file_path += "/";
                    local_file_path += relative;
                }
                break;
            }
        }
        
        if (!local_file_path.empty() && safe_exists(local_file_path)) {
            // File is synced locally, open it directly
            Logger::info("[CloudBrowser] Opening synced local file: " + local_file_path);
            std::string cmd = "xdg-open " + shell_escape(local_file_path) + " &";
            run_system(cmd);
            append_log("[Open] Synced file: " + fs::path(local_file_path).filename().string());
        } else {
            // File is cloud-only, download and open
            Logger::info("[CloudBrowser] Downloading cloud-only file: " + path);
            append_log("[Download] Downloading " + fs::path(path).filename().string() + "...");
            
            // Download to temp location
            const char* home = std::getenv("HOME");
            std::string download_dir = home ? std::string(home) + "/Downloads" : "/tmp";
            std::string filename = fs::path(path).filename().string();
            std::string download_path = download_dir + "/" + filename;
            
            // Spawn download thread
            std::thread([this, path, download_path]() {
                std::string cmd = "copyto " + shell_escape("proton:" + path) + " " + shell_escape(download_path);
                std::string output = exec_rclone_with_timeout(cmd, 120);
                
                // Open file on main thread
                struct OpenData {
                    AppWindow* self;
                    std::string path;
                    std::string filename;
                    bool success;
                };
                auto* data = new OpenData{this, download_path, fs::path(path).filename().string(), safe_exists(download_path)};
                
                g_idle_add(+[](gpointer user_data) -> gboolean {
                    auto* d = static_cast<OpenData*>(user_data);
                    if (d->success) {
                        std::string cmd = "xdg-open " + shell_escape(d->path) + " &";
                        run_system(cmd);
                        d->self->append_log("[Open] Downloaded: " + d->filename);
                        Logger::info("[CloudBrowser] Opened downloaded file: " + d->path);
                    } else {
                        d->self->append_log("[Error] Failed to download " + d->filename);
                        Logger::error("[CloudBrowser] Download failed for: " + d->filename);
                    }
                    delete d;
                    return G_SOURCE_REMOVE;
                }, data);
            }).detach();
        }
    }
}

void AppWindow::on_local_row_activated(const std::string& path, bool is_dir) {
    if (is_dir) {
        navigate_local(path);
    } else {
        if (safe_exists(path)) {
            std::string cmd = "xdg-open " + shell_escape(path) + " &";
            int result = run_system(cmd);
            if (result == 0) {
                append_log("[Open] " + fs::path(path).filename().string());
                Logger::info("[LocalBrowser] Opened file: " + path);
            } else {
                append_log("[Error] Failed to open " + fs::path(path).filename().string());
                Logger::error("[LocalBrowser] xdg-open failed for: " + path);
            }
        } else {
            append_log("[Error] File not found: " + fs::path(path).filename().string());
            Logger::error("[LocalBrowser] File does not exist: " + path);
        }
    }
}

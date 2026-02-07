// app_window_monitor.cpp - Cloud change monitoring for automatic sync
// Monitors synced folders for new/changed files and triggers auto-downloads

#include "app_window.hpp"
#include "app_window_helpers.hpp"
#include "sync_job_metadata.hpp"
#include "notifications.hpp"
#include "logger.hpp"
#include <thread>
#include <chrono>
#include <filesystem>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace AppWindowHelpers;

void AppWindow::start_cloud_monitoring() {
    if (cloud_monitoring_active_.load()) {
        Logger::debug("[CloudMonitor] Already running");
        return;
    }
    
    cloud_monitoring_active_.store(true);
    cloud_monitor_thread_ = std::thread([this]() {
        try {
            Logger::info("[CloudMonitor] Started monitoring cloud changes");
            
            // Do first check immediately
            try {
                Logger::info("[CloudMonitor] Running initial scan now...");
                monitor_cloud_changes();
            } catch (const std::exception& e) {
                Logger::error("[CloudMonitor] Exception in initial scan: " + std::string(e.what()));
            } catch (...) {
                Logger::error("[CloudMonitor] Unknown exception in initial scan");
            }
            
            while (cloud_monitoring_active_.load()) {
                try {
                    // Wait 60 seconds between checks (reduced API load)
                    Logger::debug("[CloudMonitor] Waiting 60s before next scan...");
                    for (int i = 0; i < 60 && cloud_monitoring_active_.load(); ++i) {
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                    
                    if (!cloud_monitoring_active_.load()) break;
                    
                    Logger::info("[CloudMonitor] Starting periodic scan...");
                    monitor_cloud_changes();
                } catch (const std::exception& e) {
                    Logger::error("[CloudMonitor] Exception in monitor loop: " + std::string(e.what()));
                } catch (...) {
                    Logger::error("[CloudMonitor] Unknown exception in monitor loop");
                }
            }
            
            Logger::info("[CloudMonitor] Stopped monitoring");
        } catch (const std::exception& e) {
            Logger::error("[CloudMonitor] Fatal exception in thread: " + std::string(e.what()));
        } catch (...) {
            Logger::error("[CloudMonitor] Fatal unknown exception in thread, terminating thread");
        }
    });
}

void AppWindow::stop_cloud_monitoring() {
    cloud_monitoring_active_.store(false);
    
    // Kill any rclone lsjson processes we spawned (only our UID, only lsjson)
    // Use -u flag with current user's PID to avoid killing other users' processes
    std::string uid = std::to_string(getuid());
    [[maybe_unused]] int result = std::system(
        ("pkill -U " + uid + " -f 'rclone.*lsjson.*proton:' 2>/dev/null").c_str());
    
    if (cloud_monitor_thread_.joinable()) {
        // Try to join with a timeout (3 seconds max)
        auto start = std::chrono::steady_clock::now();
        while (cloud_monitor_thread_.joinable()) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed > std::chrono::seconds(3)) {
                // Thread still running after timeout, detach it
                Logger::warn("[CloudMonitor] Thread did not stop in time, detaching");
                cloud_monitor_thread_.detach();
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Join if still joinable (thread completed)
        if (cloud_monitor_thread_.joinable()) {
            cloud_monitor_thread_.join();
        }
    }
}

void AppWindow::monitor_cloud_changes() {
    try {
        auto& registry = SyncJobRegistry::getInstance();
        auto jobs = registry.getAllJobs();
        
        Logger::info("[CloudMonitor] === SCAN START ===");
        Logger::info("[CloudMonitor] Total sync jobs: " + std::to_string(jobs.size()));
        
        if (jobs.empty()) {
            Logger::info("[CloudMonitor] No sync jobs configured - nothing to monitor");
            return;
        }
        
        for (const auto& job : jobs) {
            if (!cloud_monitoring_active_.load()) break;
        
        std::string remote_path = job.remote_path;
        if (remote_path.empty() || remote_path.front() != '/') {
            remote_path = "/" + remote_path;
        }
        
        Logger::info("[CloudMonitor] Job: " + job.job_id);
        Logger::info("[CloudMonitor]   Remote: " + remote_path);
        Logger::info("[CloudMonitor]   Local: " + job.local_path);
        
        // Check if we need to scan this job (avoid hammering the API)
        auto now = std::time(nullptr);
        auto last_check_it = last_cloud_check_.find(job.job_id);
        if (last_check_it != last_cloud_check_.end()) {
            auto elapsed = now - last_check_it->second;
            Logger::debug("[CloudMonitor]   Last checked: " + std::to_string(elapsed) + "s ago");
            // Skip if checked less than 30 seconds ago (for testing)
            if (elapsed < 30) {
                Logger::debug("[CloudMonitor]   Skipping (too soon)");
                continue;
            }
        }
        last_cloud_check_[job.job_id] = now;
        
        Logger::info("[CloudMonitor] >>> Scanning cloud folder: proton:" + remote_path);
        
        // Get list of files in this remote path (with timeout to prevent hangs)
        std::string rclone_path = get_rclone_path();
        std::string cmd = "timeout 20 " + rclone_path + " lsjson --recursive --max-depth 1 " + 
                          shell_escape("proton:" + remote_path) + " 2>&1";
        Logger::debug("[CloudMonitor] Running: " + cmd);
        
        std::string output;
        try {
            output = exec_command(cmd.c_str());
        } catch (const std::exception& e) {
            Logger::error("[CloudMonitor] exec_command failed: " + std::string(e.what()));
            continue;
        } catch (...) {
            Logger::error("[CloudMonitor] exec_command failed with unknown exception");
            continue;
        }
        
        Logger::info("[CloudMonitor] rclone output length: " + std::to_string(output.length()) + " bytes");
        
        if (output.empty()) {
            Logger::info("[CloudMonitor] ⚠️  No output from rclone - possible auth issue?");
            continue;
        }
        
        if (output.find('[') == std::string::npos) {
            Logger::info("[CloudMonitor] ⚠️  Invalid JSON output: " + output.substr(0, 200));
            continue;
        }
        
        Logger::info("[CloudMonitor] ✓ Got valid JSON response");
        
        // Parse JSON to find files that need downloading
        std::vector<std::pair<std::string, std::string>> pending_files;  // <cloud_path, filename>
        int total_files = 0;
        int total_dirs = 0;
        
        size_t pos = 0;
        while ((pos = output.find("\"Name\":", pos)) != std::string::npos) {
            total_files++;
            // Extract file name
            size_t name_start = output.find("\"", pos + 7) + 1;
            size_t name_end = output.find("\"", name_start);
            if (name_end == std::string::npos) break;
            
            std::string name = output.substr(name_start, name_end - name_start);
            
            // Check if it's a file (not directory)
            size_t is_dir_pos = output.find("\"IsDir\":", name_end);
            bool is_dir = false;
            if (is_dir_pos != std::string::npos && is_dir_pos < output.find("\"Name\":", name_end)) {
                std::string is_dir_val = output.substr(is_dir_pos + 8, 5);
                is_dir = (is_dir_val.find("true") != std::string::npos);
            }
            
            if (is_dir) {
                total_dirs++;
                Logger::debug("[CloudMonitor]   📁 " + name + " (directory, skipping)");
            } else if (!is_dir) {
                Logger::debug("[CloudMonitor]   📄 " + name + " (file, checking...)");
                std::string cloud_path = remote_path;
                if (cloud_path.back() != '/') cloud_path += "/";
                cloud_path += name;
                
                // Check if file exists locally
                std::string local_file = job.local_path;
                if (local_file.back() != '/') local_file += "/";
                local_file += name;
                
                Logger::debug("[CloudMonitor]      Local path: " + local_file);
                
                if (!safe_exists(local_file)) {
                    pending_files.push_back({cloud_path, name});
                    Logger::info("[CloudMonitor]      ⬇️  PENDING - file missing locally!");
                } else {
                    Logger::debug("[CloudMonitor]      ✓ Already exists locally");
                }
            }
            
            if (is_dir) {
                // Close the is_dir check properly
            }
            
            pos = name_end;
        }
        
        Logger::info("[CloudMonitor] Scan complete for " + remote_path + ":");
        Logger::info("[CloudMonitor]   Total items: " + std::to_string(total_files));
        Logger::info("[CloudMonitor]   Directories: " + std::to_string(total_dirs));
        Logger::info("[CloudMonitor]   Files: " + std::to_string(total_files - total_dirs));
        Logger::info("[CloudMonitor]   Pending downloads: " + std::to_string(pending_files.size()));
        
        // Trigger downloads for pending files
        if (!pending_files.empty()) {
            Logger::info("[CloudMonitor] 🚀 Queuing " + std::to_string(pending_files.size()) + 
                        " files for auto-download");
            
            for (const auto& [cloud_path, filename] : pending_files) {
                // Check if already downloading
                {
                    std::lock_guard<std::mutex> lock(download_mutex_);
                    if (active_downloads_.count(cloud_path) > 0) {
                        Logger::debug("[CloudMonitor] Already downloading: " + filename);
                        continue;
                    }
                    active_downloads_.insert(cloud_path);
                }
                
                // Spawn download thread
                std::string path_copy = cloud_path;
                std::string name_copy = filename;
                std::string local_dest = job.local_path;
                if (local_dest.back() != '/') local_dest += "/";
                local_dest += filename;
                
                std::thread([this, path_copy, name_copy, local_dest]() {
                    try {
                        // Ensure parent directory exists
                        fs::path parent = fs::path(local_dest).parent_path();
                        if (!parent.empty() && !safe_exists(parent.string())) {
                            std::error_code ec_mkd;
                            fs::create_directories(parent, ec_mkd);
                        }
                        
                        Logger::info("[CloudMonitor] Auto-downloading: " + path_copy + " -> " + local_dest);
                        
                        // Show in transfer popup
                        struct DownloadStartData {
                            AppWindow* self;
                            std::string filename;
                        };
                        auto* dl_start_data = new DownloadStartData{this, name_copy};
                        
                        g_idle_add(+[](gpointer user_data) -> gboolean {
                            auto* d = static_cast<DownloadStartData*>(user_data);
                            d->self->add_transfer_item(d->filename, false);  // false = download
                            d->self->append_log("[AutoSync] Downloading: " + d->filename);
                            delete d;
                            return G_SOURCE_REMOVE;
                        }, dl_start_data);
                        
                        // Download file using shell_escape for safety
                        std::string rclone_path = get_rclone_path();
                        std::string cmd = "timeout 300 " + rclone_path + " copyto " + 
                                          shell_escape("proton:" + path_copy) + " " + 
                                          shell_escape(local_dest) + " --progress 2>&1";
                        
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
                        
                        // Remove from active downloads
                        bool should_refresh = false;
                        {
                            std::lock_guard<std::mutex> lock(download_mutex_);
                            active_downloads_.erase(path_copy);
                            should_refresh = active_downloads_.empty();
                        }
                        
                        // Update UI
                        struct DownloadCompleteData {
                            AppWindow* self;
                            std::string filename;
                            bool success;
                            bool should_refresh;
                        };
                        auto* complete_data = new DownloadCompleteData{this, name_copy, success, should_refresh};
                        
                        g_idle_add(+[](gpointer user_data) -> gboolean {
                            auto* d = static_cast<DownloadCompleteData*>(user_data);
                            d->self->complete_transfer_item(d->filename, d->success);
                            if (d->success) {
                                d->self->append_log("[AutoSync] Downloaded: " + d->filename);
                            } else {
                                d->self->append_log("[AutoSync] Failed: " + d->filename);
                            }
                            // Notify user and update index when batch completes
                            if (d->should_refresh) {
                                d->self->append_log("[AutoSync] Batch complete");
                                proton::NotificationManager::getInstance().notify(
                                    "Auto-Sync Complete",
                                    "New files from cloud have been downloaded",
                                    proton::NotificationType::SYNC_COMPLETE);
                            }
                            delete d;
                            return G_SOURCE_REMOVE;
                        }, complete_data);
                    } catch (const std::exception& e) {
                        Logger::error("[CloudMonitor] Download thread exception: " + std::string(e.what()));
                        // Clean up download tracking
                        try {
                            std::lock_guard<std::mutex> lock(download_mutex_);
                            active_downloads_.erase(path_copy);
                        } catch (...) {}
                    } catch (...) {
                        Logger::error("[CloudMonitor] Download thread unknown exception");
                        // Clean up download tracking
                        try {
                            std::lock_guard<std::mutex> lock(download_mutex_);
                            active_downloads_.erase(path_copy);
                        } catch (...) {}
                    }
                }).detach();
            }
        } else {
            Logger::info("[CloudMonitor] ✓ All files already synced for this job");
        }
    }
    
    Logger::info("[CloudMonitor] === SCAN COMPLETE ===");
    } catch (const std::exception& e) {
        Logger::error("[CloudMonitor] Exception in monitor_cloud_changes: " + std::string(e.what()));
    } catch (...) {
        Logger::error("[CloudMonitor] Unknown exception in monitor_cloud_changes");
    }
}

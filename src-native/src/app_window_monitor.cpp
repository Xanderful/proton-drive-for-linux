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
            if (elapsed < 30) {
                Logger::debug("[CloudMonitor]   Skipping (too soon)");
                continue;
            }
        }
        last_cloud_check_[job.job_id] = now;
        
        Logger::info("[CloudMonitor] >>> Scanning cloud folder: proton:" + remote_path);
        
        // ================================================================
        // Use rclone check --one-way to efficiently detect missing files.
        // This compares cloud->local and only reports files that need 
        // downloading, without re-listing the entire directory tree.
        // Falls back to lsjson if check is unavailable.
        // ================================================================
        std::string rclone_path = get_rclone_path();
        
        // First: use rclone copy --dry-run to detect what would be copied
        // This is much faster than lsjson for large folders because rclone
        // does the comparison internally (mod_time + size) and only reports diffs.
        std::string escaped_remote = shell_escape("proton:" + remote_path);
        std::string escaped_local = shell_escape(job.local_path);
        
        // Use rclone copy with --dry-run to get list of files that need syncing
        // --no-traverse prevents listing the destination (local) when checking small changes
        std::string cmd = "timeout 60 " + rclone_path + 
                          " copy " + escaped_remote + " " + escaped_local + 
                          " --dry-run --log-level INFO 2>&1 | grep -E 'NOTICE:.*Not copying|INFO.*Copied'";
        Logger::debug("[CloudMonitor] Running dry-run: " + cmd);
        
        // Also do a simpler check: lsjson depth 1 for the direct folder
        std::string lsjson_cmd = "timeout 20 " + rclone_path + " lsjson --max-depth 1 " + 
                          escaped_remote + " 2>/dev/null";
        
        std::string output;
        try {
            output = exec_command(lsjson_cmd.c_str());
        } catch (const std::exception& e) {
            Logger::error("[CloudMonitor] exec_command failed: " + std::string(e.what()));
            continue;
        } catch (...) {
            Logger::error("[CloudMonitor] exec_command failed with unknown exception");
            continue;
        }
        
        if (output.empty() || output.find('[') == std::string::npos) {
            Logger::info("[CloudMonitor] ⚠️  No valid JSON from cloud scan");
            continue;
        }
        
        Logger::info("[CloudMonitor] ✓ Got valid JSON response (" + std::to_string(output.length()) + " bytes)");
        
        // Parse JSON to find files - compare mod_time and size, not just existence
        struct CloudFile {
            std::string name;
            int64_t size = 0;
            std::string mod_time;
            bool is_dir = false;
        };
        
        std::vector<CloudFile> cloud_files;
        std::vector<std::pair<std::string, std::string>> pending_files;  // <cloud_path, filename>
        
        // Fast JSON parsing - extract Name, Size, ModTime, IsDir
        size_t pos = 0;
        while ((pos = output.find('{', pos)) != std::string::npos) {
            size_t end = output.find('}', pos);
            if (end == std::string::npos) break;
            std::string obj = output.substr(pos, end - pos + 1);
            
            CloudFile cf;
            
            // Extract Name
            size_t npos = obj.find("\"Name\":");
            if (npos != std::string::npos) {
                size_t ns = obj.find('"', npos + 7) + 1;
                size_t ne = obj.find('"', ns);
                if (ne != std::string::npos) cf.name = obj.substr(ns, ne - ns);
            }
            
            // Extract Size
            size_t spos = obj.find("\"Size\":");
            if (spos != std::string::npos) {
                size_t ss = spos + 7;
                while (ss < obj.size() && obj[ss] == ' ') ss++;
                size_t se = ss;
                while (se < obj.size() && (std::isdigit(static_cast<unsigned char>(obj[se])) || obj[se] == '-')) se++;
                if (se > ss) { try { cf.size = std::stoll(obj.substr(ss, se - ss)); } catch (...) {} }
            }
            
            // Extract ModTime
            size_t mpos = obj.find("\"ModTime\":");
            if (mpos != std::string::npos) {
                size_t ms = obj.find('"', mpos + 10) + 1;
                size_t me = obj.find('"', ms);
                if (me != std::string::npos) cf.mod_time = obj.substr(ms, me - ms);
            }
            
            // Extract IsDir
            size_t dpos = obj.find("\"IsDir\":");
            if (dpos != std::string::npos) {
                std::string dval = obj.substr(dpos + 8, 5);
                cf.is_dir = (dval.find("true") != std::string::npos);
            }
            
            if (!cf.name.empty()) {
                if (!cf.is_dir) {
                    // Check if file exists locally AND matches size
                    std::string local_file = job.local_path;
                    if (local_file.back() != '/') local_file += "/";
                    local_file += cf.name;
                    
                    bool needs_download = false;
                    if (!safe_exists(local_file)) {
                        needs_download = true;
                        Logger::debug("[CloudMonitor]   ⬇️  " + cf.name + " - missing locally");
                    } else {
                        // File exists - check size to detect changes
                        std::error_code ec;
                        auto local_size = fs::file_size(local_file, ec);
                        if (!ec && static_cast<int64_t>(local_size) != cf.size) {
                            needs_download = true;
                            Logger::debug("[CloudMonitor]   ⬇️  " + cf.name + 
                                        " - size mismatch (local=" + std::to_string(local_size) + 
                                        " cloud=" + std::to_string(cf.size) + ")");
                        }
                    }
                    
                    if (needs_download) {
                        std::string cloud_path = remote_path;
                        if (cloud_path.back() != '/') cloud_path += "/";
                        cloud_path += cf.name;
                        pending_files.push_back({cloud_path, cf.name});
                    }
                }
                cloud_files.push_back(cf);
            }
            
            pos = end + 1;
        }
        
        int total_files = 0, total_dirs = 0;
        for (const auto& cf : cloud_files) {
            if (cf.is_dir) total_dirs++; else total_files++;
        }
        
        Logger::info("[CloudMonitor] Scan complete for " + remote_path + ":");
        Logger::info("[CloudMonitor]   Dirs: " + std::to_string(total_dirs) + 
                    "  Files: " + std::to_string(total_files) + 
                    "  Pending: " + std::to_string(pending_files.size()));
        
        // Use batch rclone copy for pending files instead of individual downloads
        if (!pending_files.empty()) {
            Logger::info("[CloudMonitor] 🚀 " + std::to_string(pending_files.size()) + 
                        " files need downloading");
            
            // For small batches (≤5 files), use individual copyto for precise tracking
            // For large batches, use rclone copy which handles everything in one call
            if (pending_files.size() <= 5) {
                for (const auto& [cloud_path, filename] : pending_files) {
                    {
                        std::lock_guard<std::mutex> lock(download_mutex_);
                        if (active_downloads_.count(cloud_path) > 0) {
                            Logger::debug("[CloudMonitor] Already downloading: " + filename);
                            continue;
                        }
                        active_downloads_.insert(cloud_path);
                    }
                    
                    std::string path_copy = cloud_path;
                    std::string name_copy = filename;
                    std::string local_dest = job.local_path;
                    if (local_dest.back() != '/') local_dest += "/";
                    local_dest += filename;
                    
                    std::thread([this, path_copy, name_copy, local_dest]() {
                        try {
                            fs::path parent = fs::path(local_dest).parent_path();
                            if (!parent.empty() && !safe_exists(parent.string())) {
                                std::error_code ec_mkd;
                                fs::create_directories(parent, ec_mkd);
                            }
                            
                            struct DLStart { AppWindow* self; std::string name; };
                            auto* ds = new DLStart{this, name_copy};
                            g_idle_add(+[](gpointer p) -> gboolean {
                                auto* d = static_cast<DLStart*>(p);
                                d->self->add_transfer_item(d->name, false);
                                d->self->append_log("[AutoSync] Downloading: " + d->name);
                                delete d;
                                return G_SOURCE_REMOVE;
                            }, ds);
                            
                            std::string rclone_path = get_rclone_path();
                            std::string cmd = "timeout 300 " + rclone_path + " copyto " + 
                                              shell_escape("proton:" + path_copy) + " " + 
                                              shell_escape(local_dest) + " 2>&1";
                            
                            FILE* pipe = popen(cmd.c_str(), "r");
                            bool success = false;
                            if (pipe) {
                                char buf[256];
                                while (fgets(buf, sizeof(buf), pipe)) {}
                                success = (pclose(pipe) == 0);
                            }
                            
                            bool should_refresh = false;
                            {
                                std::lock_guard<std::mutex> lock(download_mutex_);
                                active_downloads_.erase(path_copy);
                                should_refresh = active_downloads_.empty();
                            }
                            
                            struct DLDone { AppWindow* self; std::string name; bool ok; bool refresh; };
                            auto* dd = new DLDone{this, name_copy, success, should_refresh};
                            g_idle_add(+[](gpointer p) -> gboolean {
                                auto* d = static_cast<DLDone*>(p);
                                d->self->complete_transfer_item(d->name, d->ok);
                                d->self->append_log(std::string("[AutoSync] ") + (d->ok ? "Downloaded: " : "Failed: ") + d->name);
                                delete d;
                                return G_SOURCE_REMOVE;
                            }, dd);
                        } catch (const std::exception& e) {
                            Logger::error("[CloudMonitor] Download error: " + std::string(e.what()));
                            std::lock_guard<std::mutex> lock(download_mutex_);
                            active_downloads_.erase(path_copy);
                        } catch (...) {
                            Logger::error("[CloudMonitor] Download unknown error");
                            std::lock_guard<std::mutex> lock(download_mutex_);
                            active_downloads_.erase(path_copy);
                        }
                    }).detach();
                }
            } else {
                // Batch download: use rclone copy for the entire folder
                // This is O(1) command invocations instead of O(n)
                Logger::info("[CloudMonitor] Using batch rclone copy for " + 
                            std::to_string(pending_files.size()) + " files");
                
                std::string batch_remote = remote_path;
                std::string batch_local = job.local_path;
                
                {
                    std::lock_guard<std::mutex> lock(download_mutex_);
                    active_downloads_.insert("batch:" + batch_remote);
                }
                
                struct BatchStart { AppWindow* self; int count; };
                auto* bs = new BatchStart{this, (int)pending_files.size()};
                g_idle_add(+[](gpointer p) -> gboolean {
                    auto* d = static_cast<BatchStart*>(p);
                    d->self->append_log("[AutoSync] Batch downloading " + std::to_string(d->count) + " files...");
                    delete d;
                    return G_SOURCE_REMOVE;
                }, bs);
                
                std::thread([this, batch_remote, batch_local, pending_files]() {
                    try {
                        std::string rclone_path = get_rclone_path();
                        // rclone copy handles creating directories, comparing files, etc.
                        std::string cmd = "timeout 600 " + rclone_path + " copy " + 
                                          shell_escape("proton:" + batch_remote) + " " +
                                          shell_escape(batch_local) + 
                                          " --update --transfers 4 --checkers 8 2>&1";
                        
                        Logger::info("[CloudMonitor] Batch cmd: rclone copy (transfers=4)");
                        
                        FILE* pipe = popen(cmd.c_str(), "r");
                        bool success = false;
                        if (pipe) {
                            char buf[512];
                            while (fgets(buf, sizeof(buf), pipe)) {
                                Logger::debug("[CloudMonitor] rclone: " + std::string(buf));
                            }
                            success = (pclose(pipe) == 0);
                        }
                        
                        {
                            std::lock_guard<std::mutex> lock(download_mutex_);
                            active_downloads_.erase("batch:" + batch_remote);
                        }
                        
                        struct BatchDone { AppWindow* self; bool ok; int count; };
                        auto* bd = new BatchDone{this, success, (int)pending_files.size()};
                        g_idle_add(+[](gpointer p) -> gboolean {
                            auto* d = static_cast<BatchDone*>(p);
                            if (d->ok) {
                                d->self->append_log("[AutoSync] Batch complete: " + std::to_string(d->count) + " files");
                            } else {
                                d->self->append_log("[AutoSync] Batch download had errors");
                            }
                            d->self->refresh_cloud_files();
                            d->self->refresh_local_files();
                            delete d;
                            return G_SOURCE_REMOVE;
                        }, bd);
                    } catch (const std::exception& e) {
                        Logger::error("[CloudMonitor] Batch download error: " + std::string(e.what()));
                        std::lock_guard<std::mutex> lock(download_mutex_);
                        active_downloads_.erase("batch:" + batch_remote);
                    }
                }).detach();
            }
        } else {
            Logger::info("[CloudMonitor] ✓ All files synced for this job");
        }
    }
    
    Logger::info("[CloudMonitor] === SCAN COMPLETE ===");
    } catch (const std::exception& e) {
        Logger::error("[CloudMonitor] Exception in monitor_cloud_changes: " + std::string(e.what()));
    } catch (...) {
        Logger::error("[CloudMonitor] Unknown exception in monitor_cloud_changes");
    }
}

// Device Identity & Conflict Management Methods
#include "sync_manager.hpp"
#include "app_window.hpp"
#include "device_identity.hpp"
#include "sync_job_metadata.hpp"
#include "logger.hpp"
#include "cloud_browser.hpp"
#include <gtk/gtk.h>
#include <string>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <thread>
#include <fstream>
#include <functional>
#include <atomic>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>
#include <algorithm>

// In native UI mode, these dialogs are replaced by the new AppWindow UI
// Provide stub implementations that log and skip the old GTK3 dialogs

void SyncManager::show_device_info() {
    Logger::info("[SyncManager] Device info dialog requested (handled by AppWindow)");
}

void SyncManager::show_conflict_dialog(const SyncJobRegistry::ConflictInfo& conflict, 
                                        const std::string& local_path,
                                        const std::string& remote_path) {
    (void)conflict; (void)local_path; (void)remote_path;
    Logger::info("[SyncManager] Conflict dialog requested");
    // In NATIVE_UI_MODE, conflicts are shown in the AppWindow logs
}

void SyncManager::show_cloud_folder_conflict_dialog(const SyncJobRegistry::ConflictInfo& conflict,
                                                     const std::string& local_path,
                                                     const std::string& remote_path,
                                                     const std::string& sync_type) {
    (void)conflict; (void)local_path; (void)remote_path; (void)sync_type;
    Logger::info("[SyncManager] Cloud folder conflict dialog requested");
}

void SyncManager::show_local_conflict_dialog(const SyncJobRegistry::LocalConflictInfo& conflict,
                                              const std::string& remote_path,
                                              const std::string& local_path,
                                              const std::string& sync_type) {
    (void)conflict; (void)local_path; (void)remote_path; (void)sync_type;
    Logger::info("[SyncManager] Local conflict dialog requested");
}

void SyncManager::enable_shared_sync_for_job(const std::string& job_id) {
    Logger::info("[SyncManager] Enable shared sync for job: " + job_id);
}

// Shell-escape a string for safe use in shell commands (single-quote wrapping)
static std::string sm_shell_escape(const std::string& arg) {
    std::string escaped = "'";
    for (char c : arg) {
        if (c == '\'') {
            escaped += "'\\''";
        } else {
            escaped += c;
        }
    }
    escaped += "'";
    return escaped;
}

void SyncManager::execute_sync_to_local_async(const std::string& remote_path,
                                               const std::string& local_path,
                                               const std::string& sync_type) {
    Logger::info("[SyncManager] Execute sync: " + remote_path + " -> " + local_path + " (" + sync_type + ")");
    
    // Validate inputs
    if (remote_path.empty() || local_path.empty()) {
        Logger::error("[SyncManager] Invalid empty path for sync");
        return;
    }
    
    // Get rclone path (same as in app_window)
    const char* appdir = std::getenv("APPDIR");
    std::string rclone_path = "rclone";
    if (appdir) {
        std::string bundled = std::string(appdir) + "/usr/bin/rclone";
        struct stat buffer;
        if (stat(bundled.c_str(), &buffer) == 0) {
            rclone_path = bundled;
            Logger::info("[SyncManager] Using bundled rclone: " + bundled);
        }
    }
    
    // Check for dev build
    if (rclone_path == "rclone") {
        char exe_path[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
        if (len != -1) {
            exe_path[len] = '\0';
            std::string dev_path = std::string(exe_path);
            size_t pos = dev_path.rfind("/src-native/build");
            if (pos != std::string::npos) {
                dev_path = dev_path.substr(0, pos) + "/dist/AppDir/usr/bin/rclone";
                struct stat buffer;
                if (stat(dev_path.c_str(), &buffer) == 0) {
                    rclone_path = dev_path;
                    Logger::info("[SyncManager] Using dev build rclone: " + dev_path);
                }
            }
        }
    }
    
    // Check if bisync state files exist
    const char* home = std::getenv("HOME");
    std::string state_file = "";
    if (home) {
        // Convert paths to state file names (mimic rclone's naming)
        std::string path1_escaped = remote_path;
        std::string path2_escaped = local_path;
        // Simple escaping - replace / with _
        std::replace(path1_escaped.begin(), path1_escaped.end(), '/', '_');
        std::replace(path2_escaped.begin(), path2_escaped.end(), '/', '_');
        state_file = std::string(home) + "/.cache/rclone/bisync/proton" + path1_escaped + ".." + path2_escaped + ".path1.lst";
    }
    
    // Create parent directory if it doesn't exist
    std::filesystem::path local_path_obj(local_path);
    std::filesystem::path parent_dir = local_path_obj.parent_path();
    try {
        if (!parent_dir.empty() && parent_dir != "/") {
            std::error_code ec_mk;
            std::filesystem::create_directories(parent_dir, ec_mk);
            if (!ec_mk) {
                Logger::info("[SyncManager] Ensured directory exists: " + parent_dir.string());
            }
        }
    } catch (const std::exception& e) {
        Logger::error("[SyncManager] Failed to create parent directory: " + std::string(e.what()));
        return;
    }
    
    // Build rclone command
    std::string cmd;
    
    // Check if this is the first run (for bisync, check state files)
    struct stat buffer;
    bool first_run = (stat(state_file.c_str(), &buffer) != 0);
    
    // Use a user-specific log file (not /tmp to avoid symlink attacks)
    std::string log_dir;
    if (home) {
        log_dir = std::string(home) + "/.cache/proton-drive";
        { std::error_code ec_log; std::filesystem::create_directories(log_dir, ec_log); }
    } else {
        log_dir = "/tmp";
    }
    
    // Build rclone command safely using shell_escape
    std::string escaped_remote = sm_shell_escape("proton:" + remote_path);
    std::string escaped_local = sm_shell_escape(local_path);
    std::string escaped_rclone = sm_shell_escape(rclone_path);
    
    // Use a unique RC port for each sync to allow multiple concurrent syncs
    // Ports 5572-5600 range, using atomic counter for uniqueness
    static std::atomic<int> port_counter{0};
    int port_offset = port_counter.fetch_add(1) % 29;  // 29 ports to cycle through
    int rc_port = 5572 + port_offset;
    std::string rc_addr = "--rc-addr=localhost:" + std::to_string(rc_port);
    
    Logger::info("[SyncManager] Using RC port " + std::to_string(rc_port) + " for this sync");
    
    if (sync_type == "bisync") {
        if (first_run) {
            Logger::info("[SyncManager] First bisync run: yes - will use copy instead to avoid --resync issues");
            cmd = escaped_rclone + " copy " + escaped_remote + " " + escaped_local +
                  " --verbose --rc " + rc_addr + " --log-file " + sm_shell_escape(log_dir + "/rclone-copy.log") + " &";
            Logger::info("[SyncManager] Starting copy (first run)");
        } else {
            Logger::info("[SyncManager] First bisync run: no - will use full bisync");
            cmd = escaped_rclone + " bisync " + escaped_remote + " " + escaped_local +
                  " --verbose --rc " + rc_addr + " --log-file " + sm_shell_escape(log_dir + "/rclone-bisync.log") + " &";
            Logger::info("[SyncManager] Starting bisync");
        }
    } else if (sync_type == "copy") {
        cmd = escaped_rclone + " copy " + escaped_remote + " " + escaped_local +
              " --verbose --rc " + rc_addr + " --log-file " + sm_shell_escape(log_dir + "/rclone-copy.log") + " &";
        Logger::info("[SyncManager] Starting copy");
    } else {
        Logger::error("[SyncManager] Unknown sync type: " + sync_type);
        return;
    }
    
    // Execute the command asynchronously
    Logger::info("[SyncManager] Executing sync command (paths escaped for safety)");
    Logger::debug("[SyncManager] Sync remote: " + remote_path);
    Logger::debug("[SyncManager] Sync local: " + local_path);
    Logger::debug("[SyncManager] Sync type: " + sync_type);
    int result = std::system(cmd.c_str());
    Logger::info("[SyncManager] Command execution returned: " + std::to_string(result));
    
    // Notify user that sync has started
    Logger::info("[SyncManager] Sync started: " + remote_path + " → " + local_path);
    
    // Register the job in SyncJobRegistry (single source of truth)
    // This ensures it shows up in Sync Jobs immediately
    {
        auto& registry = SyncJobRegistry::getInstance();
        
        // Check if a job already exists for this remote path
        bool already_registered = false;
        std::string existing_job_id;
        for (const auto& existing : registry.getAllJobs()) {
            if (existing.remote_path == remote_path && existing.local_path == local_path) {
                already_registered = true;
                existing_job_id = existing.job_id;
                Logger::debug("[SyncManager] Job already registered for: " + remote_path);
                break;
            }
        }
        
        if (!already_registered) {
            std::string job_id = registry.createJob(local_path, remote_path, sync_type);
            Logger::info("[SyncManager] Registered sync job in registry: " + job_id);
            
            // Also create .conf file so job persists across restarts
            // Use create-with-id to ensure same ID is used
            std::string script_path = get_script_path("manage-sync-job.sh");
            std::string conf_cmd = "bash " + sm_shell_escape(script_path) + " create-with-id " +
                                   sm_shell_escape(job_id) + " " +
                                   sm_shell_escape(local_path) + " " +
                                   sm_shell_escape("proton:" + remote_path) + " " +
                                   sm_shell_escape(sync_type) + " '15m'";
            int conf_result = std::system(conf_cmd.c_str());
            if (conf_result != 0) {
                Logger::warn("[SyncManager] Failed to create .conf file for job: " + job_id);
            } else {
                Logger::info("[SyncManager] Created .conf file for job: " + job_id);
            }

            // Refresh the sync jobs UI on the main thread
            g_idle_add([](gpointer) -> gboolean {
                AppWindow::getInstance().refresh_sync_jobs();
                return G_SOURCE_REMOVE;
            }, nullptr);
        }
    }
}


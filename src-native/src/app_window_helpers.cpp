/**
 * app_window_helpers.cpp
 * 
 * Shared helper functions for AppWindow components.
 * Extracted from app_window.cpp for better code organization.
 */

#include "app_window_helpers.hpp"
#include "logger.hpp"
#include "sync_job_metadata.hpp"
#include <gtk/gtk.h>
#include <filesystem>
#include <fstream>
#include <array>
#include <memory>
#include <vector>
#include <unistd.h>
#include <limits.h>

namespace fs = std::filesystem;

namespace AppWindowHelpers {

std::string shell_escape(const std::string& arg) {
    // The safest way to pass untrusted data to shell: use single quotes
    // and escape any embedded single quotes by ending the quote, adding 
    // an escaped single quote, and starting a new quoted section.
    // Example: "it's here" -> 'it'"'"'s here'
    std::string result = "'";
    for (char c : arg) {
        if (c == '\'') {
            result += "'\"'\"'";  // End quote, add escaped quote, start new quote
        } else {
            result += c;
        }
    }
    result += "'";
    return result;
}

std::string get_rclone_path() {
    // Check for AppImage bundled rclone first
    const char* appdir = std::getenv("APPDIR");
    if (appdir) {
        std::string bundled_path = std::string(appdir) + "/usr/bin/rclone";
        if (safe_exists(bundled_path)) {
            Logger::info("[rclone] Using bundled rclone: " + bundled_path);
            return bundled_path;
        }
    }
    
    // Check common system locations and dev build paths
    const std::vector<std::string> paths = {
        "/usr/bin/rclone",
        "/usr/local/bin/rclone",
        "/snap/bin/rclone",
        "./dist/AppDir/usr/bin/rclone",
    };
    
    // Also check relative to executable location
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        std::string exe_dir = fs::path(exe_path).parent_path().string();
        std::string dev_path = fs::path(exe_dir).parent_path().parent_path().string() + "/dist/AppDir/usr/bin/rclone";
        if (safe_exists(dev_path)) {
            Logger::info("[rclone] Using dev build rclone: " + dev_path);
            return dev_path;
        }
    }
    
    for (const auto& p : paths) {
        if (safe_exists(p)) {
            Logger::info("[rclone] Using system rclone: " + p);
            return p;
        }
    }
    
    Logger::info("[rclone] No explicit path found, using PATH lookup");
    return "rclone";
}

std::string exec_rclone(const std::string& args) {
    std::string rclone_path = get_rclone_path();
    std::string cmd = rclone_path + " " + args + " 2>/dev/null";
    Logger::debug("[rclone] Executing: " + cmd);
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        return "";
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

std::string exec_rclone_with_timeout(const std::string& args, int timeout_seconds) {
    std::string rclone_path = get_rclone_path();
    std::string cmd = "timeout " + std::to_string(timeout_seconds) + " " + 
                      rclone_path + " " + args + " 2>/dev/null";
    Logger::debug("[rclone] Executing (timeout " + std::to_string(timeout_seconds) + "s): " + cmd);
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        Logger::warn("[rclone] popen failed for timed command");
        return "";
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

int run_rclone(const std::string& args) {
    std::string rclone_path = get_rclone_path();
    std::string cmd = rclone_path + " " + args + " 2>/dev/null";
    Logger::debug("[rclone] Running: " + cmd);
    return std::system(cmd.c_str());
}

std::string exec_command(const char* cmd) {
    try {
        std::array<char, 128> buffer;
        std::string result;
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
        if (!pipe) {
            Logger::warn("[exec_command] popen failed for command");
            return "";
        }
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            result += buffer.data();
        }
        return result;
    } catch (const std::exception& e) {
        Logger::error("[exec_command] Exception: " + std::string(e.what()));
        return "";
    } catch (...) {
        Logger::error("[exec_command] Unknown exception");
        return "";
    }
}

int run_system(const std::string& cmd) {
    return std::system(cmd.c_str());
}

// ============================================================================
// Safe filesystem operations (never throw)
// ============================================================================

bool safe_exists(const std::string& path) {
    std::error_code ec;
    bool result = fs::exists(path, ec);
    if (ec) {
        Logger::debug("[FilesystemSafe] exists() I/O error for " + path + ": " + ec.message());
        return false;
    }
    return result;
}

bool safe_definitely_missing(const std::string& path) {
    std::error_code ec;
    bool result = fs::exists(path, ec);
    if (ec) {
        // I/O error - we can't be sure if it exists or not
        Logger::debug("[FilesystemSafe] definitely_missing() I/O error for " + path + ": " + ec.message() + " - returning false (not definitely missing)");
        return false;  // Not definitely missing, could be I/O issue
    }
    return !result;  // Return true only if path definitely doesn't exist
}

bool safe_is_directory(const std::string& path) {
    std::error_code ec;
    bool result = fs::is_directory(path, ec);
    if (ec) {
        Logger::debug("[FilesystemSafe] is_directory() I/O error for " + path + ": " + ec.message());
        return false;
    }
    return result;
}

bool safe_is_regular_file(const std::string& path) {
    std::error_code ec;
    bool result = fs::is_regular_file(path, ec);
    if (ec) {
        Logger::debug("[FilesystemSafe] is_regular_file() I/O error for " + path + ": " + ec.message());
        return false;
    }
    return result;
}

// ============================================================================

std::pair<std::string, std::string> get_sync_status_for_path(const std::string& cloud_path) {
    const char* home = std::getenv("HOME");
    if (!home) {
        return {"", ""};
    }
    
    std::string cloud_check = cloud_path;
    // Strip proton: prefix if present for consistent comparison
    if (cloud_check.find("proton:") == 0) {
        cloud_check = cloud_check.substr(7);
    }
    if (!cloud_check.empty() && cloud_check.front() == '/') {
        cloud_check = cloud_check.substr(1);
    }
    
    Logger::debug("[SyncStatus] Checking path: " + cloud_path + " -> normalized: " + cloud_check);
    
    // Use SyncJobRegistry as the single source of truth (matches Sync Jobs tab)
    auto& registry = SyncJobRegistry::getInstance();
    auto all_jobs = registry.getAllJobs();
    
    bool found_matching_job = false;
    std::string local_path_found;
    std::string local_file_path;
    
    for (const auto& job : all_jobs) {
        std::string remote_check = job.remote_path;
        if (!remote_check.empty() && remote_check.front() == '/') {
            remote_check = remote_check.substr(1);
        }
        
        bool path_matches = (cloud_check == remote_check);
        bool is_subdir = (!remote_check.empty() && 
                          cloud_check.length() > remote_check.length() &&
                          cloud_check.substr(0, remote_check.length()) == remote_check &&
                          cloud_check[remote_check.length()] == '/');
        
        if (path_matches || is_subdir) {
            Logger::debug("[SyncStatus] Matched job remote: " + remote_check);
            found_matching_job = true;
            local_path_found = job.local_path;
            if (is_subdir) {
                std::string relative = cloud_check.substr(remote_check.length());
                if (!relative.empty() && relative.front() == '/') {
                    relative = relative.substr(1);
                }
                local_file_path = job.local_path + "/" + relative;
            } else {
                local_file_path = job.local_path;
            }
            break;
        }
    }
    
    if (!found_matching_job) {
        return {"", ""};
    }
    
    // Check if the job folder exists
    if (!safe_exists(local_path_found)) {
        return {"", ""};
    }
    
    // Check if the actual local file/folder exists
    // CRITICAL: Use safe_exists to prevent crash on I/O errors
    if (!local_file_path.empty() && !safe_exists(local_file_path)) {
        return {"⏳ Pending", "sync-badge-pending"};
    }
    
    // Check if rclone is currently running for this sync job
    std::string ps_cmd = "pgrep -a rclone 2>/dev/null | grep -E '(bisync|sync|copy)' | grep -F " + 
                        shell_escape(local_path_found) + " 2>/dev/null | wc -l";
    FILE* pipe = popen(ps_cmd.c_str(), "r");
    if (pipe) {
        char buffer[32] = {0};
        if (fgets(buffer, sizeof(buffer), pipe)) {
            int count = std::atoi(buffer);
            pclose(pipe);
            if (count > 0) {
                return {"⏳ Pending", "sync-badge-pending"};
            }
        } else {
            pclose(pipe);
        }
    }
    
    return {"✓ Synced", "sync-badge-synced"};
}

void ensure_valid_cwd_for_shell() {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        const char* home = std::getenv("HOME");
        if (home && chdir(home) == 0) {
            // Successfully switched to home
        } else if (chdir("/tmp") != 0) {
            // Even /tmp failed, nothing we can do
            Logger::warn("[AppWindow] Failed to set valid working directory");
        }
    }
}

bool has_rclone_profile() {
    std::string remotes = exec_rclone("listremotes");
    return remotes.find("proton:") != std::string::npos;
}

bool run_rclone_config_create(const std::string& username,
                               const std::string& password,
                               const std::string& twofa,
                               std::string* output,
                               std::string* error_message) {
    Logger::info("[ProfileConfig] Starting rclone config create");
    
    std::string rclone_path = get_rclone_path();
    
    std::vector<std::string> args_storage;
    args_storage.push_back(rclone_path);
    args_storage.push_back("config");
    args_storage.push_back("create");
    args_storage.push_back("proton");
    args_storage.push_back("protondrive");
    args_storage.push_back("username=" + username);
    args_storage.push_back("password=" + password);
    if (!twofa.empty()) {
        args_storage.push_back("2fa=" + twofa);
    }

    std::vector<char*> argv;
    argv.reserve(args_storage.size() + 1);
    for (auto& arg : args_storage) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    GError* error = nullptr;
    gchar* stdout_buf = nullptr;
    gchar* stderr_buf = nullptr;
    int exit_status = 0;

    bool ok = g_spawn_sync(nullptr,
                           argv.data(),
                           nullptr,
                           G_SPAWN_SEARCH_PATH,
                           nullptr,
                           nullptr,
                           &stdout_buf,
                           &stderr_buf,
                           &exit_status,
                           &error);

    if (stdout_buf && output) {
        *output = stdout_buf;
    }
    if (stderr_buf && error_message) {
        *error_message = stderr_buf;
    }

    if (stdout_buf) g_free(stdout_buf);
    if (stderr_buf) g_free(stderr_buf);

    if (!ok) {
        Logger::error("[ProfileConfig] Failed to spawn rclone");
        if (error_message) {
            *error_message = error && error->message ? error->message : "Failed to start rclone";
        }
        if (error) g_error_free(error);
        return false;
    }

    GError* exit_error = nullptr;
    // Use modern API (g_spawn_check_wait_status) when available, fallback to deprecated version
#if GLIB_CHECK_VERSION(2, 70, 0)
    if (!g_spawn_check_wait_status(exit_status, &exit_error)) {
#else
    if (!g_spawn_check_exit_status(exit_status, &exit_error)) {
#endif
        Logger::error("[ProfileConfig] rclone exited with non-zero status");
        if (error_message) {
            *error_message = exit_error && exit_error->message ? exit_error->message : "rclone exited with error";
        }
        if (exit_error) g_error_free(exit_error);
        return false;
    }

    return true;
}

std::string format_file_size(int64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_idx = 0;
    double size = static_cast<double>(bytes);
    while (size >= 1024 && unit_idx < 4) {
        size /= 1024;
        unit_idx++;
    }
    char buf[32];
    if (unit_idx == 0) {
        snprintf(buf, sizeof(buf), "%ld B", bytes);
    } else {
        snprintf(buf, sizeof(buf), "%.1f %s", size, units[unit_idx]);
    }
    return buf;
}

} // namespace AppWindowHelpers

#include "sync_manager.hpp"
#include "settings.hpp"
#include "logger.hpp"
#include "file_watcher.hpp"
#include "file_index.hpp"
#include "cloud_browser.hpp"
#include "sync_job_metadata.hpp"
#include "device_identity.hpp"
#include "app_window_helpers.hpp"
#include "gtk_compat.hpp"  // GTK4 compatibility layer
#include "app_window.hpp"
#include <iostream>
#include <fstream>
#include <regex>
#include <array>
#include <memory>
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <utility>
#include <vector>
#include <gdk/gdk.h>
#include <signal.h>

namespace fs = std::filesystem;

// Helper function to check if a widget is safe to update
// Returns false if widget is null, destroyed, or its window is not visible/mapped
// This prevents crashes when screen is locked or window is minimized
// Note: Only used in WebView mode (GTK3), marked unused for NATIVE_UI_MODE builds
[[maybe_unused]] static bool is_widget_safe_for_update(GtkWidget* widget) {
    if (!widget) return false;
    if (!GTK_IS_WIDGET(widget)) return false;
    
    // Check if widget is realized (has an associated GDK window)
    if (!gtk_widget_get_realized(widget)) return false;
    
    // Check if widget is mapped (visible on screen)
    if (!gtk_widget_get_mapped(widget)) return false;
    
    // Use compatibility helper for window visibility check
    return is_window_visible_and_active(widget);
}

// Structure to safely pass UI updates from background threads
struct ProgressUpdate {
    std::string transferred_text;
    std::string speed_text;
    std::string status_text;
    double progress_fraction;
    std::string progress_text;
    SyncManager* manager;
};

// Helper to execute shell command and get output
// Note: Only used in WebView mode (GTK3), marked unused for NATIVE_UI_MODE builds
[[maybe_unused]] static std::string exec_command(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

// Helper to run command without capturing output (system)
// Note: Only used in WebView mode (GTK3), marked unused for NATIVE_UI_MODE builds
[[maybe_unused]] static int run_system(const std::string& cmd) {
    return std::system(cmd.c_str());
}

std::string SyncManager::get_script_path(const std::string& script_name) {
    try {
        std::filesystem::path exe_path = std::filesystem::read_symlink("/proc/self/exe");
        std::filesystem::path bin_dir = exe_path.parent_path();
        
        // Check relative to binary (e.g. installed, or build dir)
        std::vector<std::filesystem::path> search_paths = {
            bin_dir / "scripts" / script_name, // If next to binary
            bin_dir / "../scripts" / script_name, // If binary in build/
            bin_dir / "../../scripts" / script_name, // If binary in src-native/build/
            "/usr/share/proton-drive/scripts/" + script_name, // System install
            "scripts/" + script_name, // CWD
            "../scripts/" + script_name // Parent of CWD
        };

        for (const auto& path : search_paths) {
            if (AppWindowHelpers::safe_exists(path.string())) {
                return std::filesystem::absolute(path).string();
            }
        }
    } catch (...) {}
    
    // Fallback to searching CWD blindly
    return "scripts/" + script_name;
}

SyncManager& SyncManager::getInstance() {
    static SyncManager instance;
    return instance;
}

// ============================================================================
// NATIVE UI MODE STUBS
// In native GTK4 mode, the AppWindow handles all UI. SyncManager provides
// only core sync logic and utility functions.
// ============================================================================

GtkWidget* SyncManager::get_widget() {
    return nullptr; // No widget in native mode - AppWindow handles UI
}

void SyncManager::init() {
    Logger::info("[SyncManager] Initializing in native UI mode");
    std::cout.flush();
    
    // Initialize device identity and sync registry (non-UI)
    DeviceIdentity::getInstance();
    
    SyncJobRegistry::getInstance();
    
    SyncJobRegistry::getInstance().loadJobs();
    
    // Initialize file watcher for real-time sync
    init_file_watcher();
}

void SyncManager::shutdown() {
    Logger::info("[SyncManager] Shutting down...");
    if (file_watcher_) {
        file_watcher_->stop();
        Logger::info("[SyncManager] File watcher stopped");
    }
}

void SyncManager::build_ui() {
    // No-op in native mode
}

void SyncManager::update_ui_state() {
    // No-op in native mode - AppWindow handles this
}

void SyncManager::load_profiles() {
    // No-op in native mode - AppWindow handles this
}

void SyncManager::load_jobs() {
    // No-op in native mode - AppWindow handles this
}

void SyncManager::show_sync_to_local_dialog(const std::string& remote_path) {
    Logger::info("[SyncManager] Sync to local dialog requested for: " + remote_path);
    // In native mode, this would be handled by AppWindow
    // Use the full path structure to preserve folder hierarchy
    std::string home = std::getenv("HOME") ? std::getenv("HOME") : "/tmp";
    // Preserve the full cloud path under ~/ProtonDrive so scope is clear
    // e.g., /Music/Google Music/Justifide -> ~/ProtonDrive/Music/Google Music/Justifide
    std::string relative = remote_path;
    if (!relative.empty() && relative.front() == '/') {
        relative = relative.substr(1);
    }
    std::string local_path = home + "/ProtonDrive/" + relative;
    execute_sync_to_local_async(remote_path, local_path, "bisync");
}

void SyncManager::show_selective_sync_dialog(const std::string& remote_path) {
    Logger::info("[SyncManager] Selective sync dialog requested for: " + remote_path);
    (void)remote_path;
}

void SyncManager::show_conflict_resolution_dialog(const std::string& local_file, 
                                                   const std::string& remote_file,
                                                   const std::string& conflict_type) {
    Logger::info("[SyncManager] Conflict resolution dialog requested");
    (void)local_file; (void)remote_file; (void)conflict_type;
}

void SyncManager::show_preferences_dialog() {
    Logger::info("[SyncManager] Preferences dialog requested (handled by AppWindow)");
}

void SyncManager::refresh_job_statuses() {
    // No-op in native mode
}

gboolean SyncManager::on_status_timeout(gpointer data) {
    (void)data;
    return G_SOURCE_REMOVE; // Don't repeat in native mode
}

void SyncManager::append_log(const std::string& text) {
    Logger::info("[Sync] " + text);
    AppWindow::getInstance().append_log(text);
}

void SyncManager::read_external_log() {
    // No-op in native mode
}

void SyncManager::poll_rc_api_stats() {
    // No-op in native mode
}

int SyncManager::find_active_rc_port() {
    return 0;
}

void SyncManager::run_setup_flow() {
    Logger::info("[SyncManager] Setup flow requested");
}

void SyncManager::run_job_flow(const std::string& edit_id, 
                               const std::string& edit_local,
                               const std::string& edit_remote,
                               const std::string& edit_type) {
    Logger::info("[SyncManager] Job flow requested");
    (void)edit_id; (void)edit_local; (void)edit_remote; (void)edit_type;
}

void SyncManager::update_progress_ui(const std::string& transferred, 
                                      const std::string& speed, 
                                      const std::string& file, 
                                      double fraction) {
    AppWindow::getInstance().update_sync_progress(file, fraction, speed, transferred);
}

void SyncManager::load_settings_to_dialog(GtkWidget* dialog) {
    (void)dialog;
}

void SyncManager::save_settings_from_dialog(GtkWidget* dialog) {
    (void)dialog;
}

void SyncManager::on_start_clicked(GtkWidget* widget, gpointer data) {
    (void)widget; (void)data;
}

void SyncManager::on_stop_clicked(GtkWidget* widget, gpointer data) {
    (void)widget; (void)data;
}

void SyncManager::on_refresh_clicked(GtkWidget* widget, gpointer data) {
    (void)widget; (void)data;
}

void SyncManager::on_add_profile_clicked(GtkWidget* widget, gpointer data) {
    (void)widget; (void)data;
}

void SyncManager::on_add_job_clicked(GtkWidget* widget, gpointer data) {
    (void)widget; (void)data;
}

void SyncManager::on_remove_job_clicked(GtkWidget* widget, gpointer data) {
    (void)widget; (void)data;
}

void SyncManager::on_troubleshoot_clicked(GtkWidget* widget, gpointer data) {
    (void)widget; (void)data;
}

void SyncManager::on_menu_sync_now(GtkWidget* widget, gpointer data) {
    (void)widget; (void)data;
}

void SyncManager::on_menu_force_resync(GtkWidget* widget, gpointer data) {
    (void)widget; (void)data;
}

void SyncManager::on_menu_edit_job(GtkWidget* widget, gpointer data) {
    (void)widget; (void)data;
}

void SyncManager::on_menu_edit_profile(GtkWidget* widget, gpointer data) {
    (void)widget; (void)data;
}

void SyncManager::on_menu_remove_profile(GtkWidget* widget, gpointer data) {
    (void)widget; (void)data;
}

void SyncManager::on_remove_profile_clicked(GtkWidget* widget, gpointer data) {
    (void)widget; (void)data;
}

void SyncManager::on_open_drive_clicked(GtkWidget* widget, gpointer data) {
    (void)widget; (void)data;
}

void SyncManager::on_settings_clicked(GtkWidget* widget, gpointer data) {
    (void)widget; (void)data;
}

void SyncManager::on_view_toggle_clicked(GtkWidget* widget, gpointer data) {
    (void)widget; (void)data;
}

void SyncManager::on_pause_clicked(GtkWidget* widget, gpointer data) {
    (void)widget; (void)data;
}

void SyncManager::on_resume_clicked(GtkWidget* widget, gpointer data) {
    (void)widget; (void)data;
}

void SyncManager::on_stop_sync_clicked(GtkWidget* widget, gpointer data) {
    (void)widget; (void)data;
}

void SyncManager::check_and_recover_stall(double bytes, double speed, int errors, const std::string& last_error) {
    (void)bytes; (void)speed; (void)errors; (void)last_error;
}

void SyncManager::restart_bisync_with_resync() {
    Logger::info("[SyncManager] Restart bisync with resync requested");
}

// ============================================================================
// File Watcher Integration - Real-time sync on file changes
// ============================================================================

void SyncManager::init_file_watcher() {
    Logger::debug("[FileWatcher Init] Creating FileWatcher instance...");
    std::cout.flush();
    file_watcher_ = std::make_unique<FileWatcher>();
    Logger::debug("[FileWatcher Init] FileWatcher created");
    std::cout.flush();
    
    // Set up the callback to trigger sync when files change
    Logger::debug("[FileWatcher Init] Setting sync callback...");
    std::cout.flush();
    file_watcher_->set_sync_callback([this](const std::string& job_id) {
        trigger_job_sync(job_id);
    });
    Logger::debug("[FileWatcher Init] Sync callback set");
    std::cout.flush();
    
    // Get debounce delay from settings (default 3 seconds)
    Logger::debug("[FileWatcher Init] Getting settings manager...");
    std::cout.flush();
    proton::SettingsManager& settings = proton::SettingsManager::getInstance();
    Logger::debug("[FileWatcher Init] Settings manager obtained");
    std::cout.flush();
    int debounce = settings.get_sync_interval_minutes();
    // Use shorter debounce for file watcher (3-10 seconds based on interval)
    if (debounce <= 1) {
        file_watcher_->set_debounce_delay(2);
    } else if (debounce <= 5) {
        file_watcher_->set_debounce_delay(3);
    } else {
        file_watcher_->set_debounce_delay(5);
    }
    Logger::debug("[FileWatcher Init] Debounce delay set");
    std::cout.flush();
    
    // Start the watcher
    Logger::debug("[FileWatcher Init] About to start file watcher...");
    std::cout.flush();
    if (file_watcher_->start()) {
        Logger::debug("[FileWatcher Init] File watcher started successfully");
        std::cout.flush();
        append_log("📁 File watcher started (real-time sync enabled)");
        
        // Set up watches for existing jobs
        Logger::debug("[FileWatcher Init] Setting up watches for jobs...");
        std::cout.flush();
        setup_watches_for_jobs();
        Logger::debug("[FileWatcher Init] Watches setup complete");
        std::cout.flush();
    } else {
        Logger::error("[SyncManager] Failed to start file watcher");
        append_log("⚠️ File watcher failed to start");
    }
    Logger::debug("[FileWatcher Init] init_file_watcher() complete");
    std::cout.flush();
}

void SyncManager::setup_watches_for_jobs() {
    if (!file_watcher_ || !file_watcher_->is_running()) {
        return;
    }
    
    // Use the SyncJobRegistry to get all jobs (more reliable than parsing conf files)
    auto& registry = SyncJobRegistry::getInstance();
    auto all_jobs = registry.getAllJobs();
    
    for (const auto& job : all_jobs) {
        // Use safe_exists to prevent crash on I/O errors (corrupted dirs, network issues)
        if (!job.local_path.empty() && AppWindowHelpers::safe_exists(job.local_path)) {
            if (file_watcher_->add_watch(job.job_id, job.local_path)) {
                Logger::info("[SyncManager] Watching: " + job.local_path + " (job " + job.job_id + ")");
            } else {
                Logger::warn("[SyncManager] Failed to add watch for: " + job.local_path);
            }
        } else if (!job.local_path.empty()) {
            Logger::debug("[SyncManager] Skipping watch for inaccessible path: " + job.local_path + " (job " + job.job_id + ")");
        }
    }
}

void SyncManager::trigger_job_sync(const std::string& job_id) {
    // Use g_idle_add to run on main thread
    struct SyncData {
        SyncManager* self;
        std::string job_id;
    };
    
    SyncData* data = new SyncData{this, job_id};
    
    g_idle_add([](gpointer user_data) -> gboolean {
        SyncData* d = static_cast<SyncData*>(user_data);
        
        d->self->append_log("🔄 Auto-sync triggered for job " + d->job_id + " (file change detected)");
        
        // Trigger the sync via systemd (result intentionally ignored)
        std::string cmd = "systemctl --user start proton-drive-job-" + d->job_id + ".service 2>/dev/null &";
        [[maybe_unused]] int result = system(cmd.c_str());
        
        // Refresh cloud browser to show updated files
        CloudBrowser::getInstance().refresh();
        
        // Also update the file index incrementally for this job's folder
        // This runs in a background thread to avoid blocking the UI
        std::string job_id_copy = d->job_id;
        std::thread([job_id_copy]() {
            // Read the job config to get local and remote paths
            std::string config_path = std::string(getenv("HOME")) + 
                                      "/.config/proton-drive/jobs/" + job_id_copy + ".conf";
            
            std::ifstream config(config_path);
            if (!config.is_open()) return;
            
            std::string local_path, remote_path;
            std::string line;
            while (std::getline(config, line)) {
                // Parse key=value format (LOCAL_PATH="value" or LOCAL_PATH=value)
                if (line.find("LOCAL_PATH=") == 0) {
                    local_path = line.substr(11);
                    // Remove quotes
                    if (!local_path.empty() && local_path.front() == '"') local_path = local_path.substr(1);
                    if (!local_path.empty() && local_path.back() == '"') local_path.pop_back();
                } else if (line.find("REMOTE_PATH=") == 0) {
                    remote_path = line.substr(12);
                    // Remove quotes
                    if (!remote_path.empty() && remote_path.front() == '"') remote_path = remote_path.substr(1);
                    if (!remote_path.empty() && remote_path.back() == '"') remote_path.pop_back();
                }
            }
            
            if (!local_path.empty() && !remote_path.empty()) {
                // Update file index for this sync folder
                auto& index = FileIndex::getInstance();
                index.update_files_from_sync(job_id_copy, local_path, remote_path);
            }
        }).detach();
        
        delete d;
        return FALSE;
    }, data);
}
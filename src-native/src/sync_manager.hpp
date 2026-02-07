#pragma once

#include <gtk/gtk.h>
#include <string>
#include <functional>
#include <vector>
#include <memory>
#include "device_identity.hpp"
#include "sync_job_metadata.hpp"

// Forward declaration
class FileWatcher;

class SyncManager {
public:
    static SyncManager& getInstance();

    // Get the main widget (sidebar container)
    GtkWidget* get_widget();

    // Initialize (build) the UI
    void init();
    
    // Shutdown - stop file watcher and cleanup
    void shutdown();
    
    // Set callback for view toggle (called by webview)
    static void set_view_toggle_callback(std::function<void()> callback);
    
    // Sync from cloud to local - public so CloudBrowser can call it
    void show_sync_to_local_dialog(const std::string& remote_path);
    
    // Create and execute a sync job (used by drag-and-drop and dialogs)
    void execute_sync_to_local(const std::string& remote_path, 
                               const std::string& local_path,
                               const std::string& sync_type);
    
    // Async version - shows immediate feedback, runs in background
    void execute_sync_to_local_async(const std::string& remote_path, 
                                     const std::string& local_path,
                                     const std::string& sync_type);
    
    // Load Sync Jobs (public for async callbacks)
    void load_jobs();
    
    // Script path helper (public for async callbacks)
    static std::string get_script_path(const std::string& script_name);

private:
    SyncManager() = default;
    ~SyncManager() = default;

    // GUI Elements
    GtkWidget* container_ = nullptr;      // Main VBox
    GtkWidget* status_label_ = nullptr;
    GtkWidget* start_btn_ = nullptr;
    GtkWidget* stop_btn_ = nullptr;
    
    // Dashboard Elements
    GtkWidget* dashboard_frame_ = nullptr;
    GtkWidget* lbl_transferred_ = nullptr;
    GtkWidget* lbl_speed_ = nullptr;
    GtkWidget* lbl_current_file_ = nullptr;
    GtkWidget* progress_bar_ = nullptr;
    GtkWidget* pause_btn_ = nullptr;
    GtkWidget* resume_btn_ = nullptr;
    
    GtkWidget* log_view_ = nullptr;
    GtkWidget* profiles_list_ = nullptr;  // ListBox for profiles
    GtkWidget* jobs_list_ = nullptr;      // ListBox for sync jobs
    
    // Helper to build UI
    void build_ui();
    
    // Updates UI state based on service status
    void update_ui_state();
    
    // Load Rclone profiles into the list
    void load_profiles();
    
    // Device & Conflict Management
    void show_device_info();
    void show_conflict_dialog(const SyncJobRegistry::ConflictInfo& conflict, 
                             const std::string& local_path,
                             const std::string& remote_path);
    void show_cloud_folder_conflict_dialog(const SyncJobRegistry::ConflictInfo& conflict,
                                           const std::string& local_path,
                                           const std::string& remote_path,
                                           const std::string& sync_type);
    
    // Sync from cloud to local dialogs (private helpers)
    void show_local_conflict_dialog(const SyncJobRegistry::LocalConflictInfo& conflict,
                                    const std::string& remote_path,
                                    const std::string& local_path,
                                    const std::string& sync_type);
                                           
    void enable_shared_sync_for_job(const std::string& job_id);

    // Callbacks
    static void on_start_clicked(GtkWidget* widget, gpointer data);
    static void on_stop_clicked(GtkWidget* widget, gpointer data);
    static void on_refresh_clicked(GtkWidget* widget, gpointer data);
    static void on_add_profile_clicked(GtkWidget* widget, gpointer data);
    static void on_add_job_clicked(GtkWidget* widget, gpointer data);
    static void on_remove_job_clicked(GtkWidget* widget, gpointer data);
    static void on_troubleshoot_clicked(GtkWidget* widget, gpointer data);
    // GTK4 gesture handlers
    static void on_job_gesture_pressed(GtkGestureClick* gesture, int n_press, double x, double y, gpointer data);
    static void on_profile_gesture_pressed(GtkGestureClick* gesture, int n_press, double x, double y, gpointer data);
    static void on_menu_sync_now(GtkWidget* widget, gpointer data);
    static void on_menu_force_resync(GtkWidget* widget, gpointer data);
    static void on_menu_edit_job(GtkWidget* widget, gpointer data);

    static void on_menu_edit_profile(GtkWidget* widget, gpointer data);
    static void on_menu_remove_profile(GtkWidget* widget, gpointer data);

    static void on_remove_profile_clicked(GtkWidget* widget, gpointer data);
    static void on_open_drive_clicked(GtkWidget* widget, gpointer data);
    static void on_settings_clicked(GtkWidget* widget, gpointer data);
    static void on_view_toggle_clicked(GtkWidget* widget, gpointer data);
    
    // Preferences dialog
    void show_preferences_dialog();
    
    // Pause/Resume/Stop callbacks
    static void on_pause_clicked(GtkWidget* widget, gpointer data);
    static void on_resume_clicked(GtkWidget* widget, gpointer data);
    static void on_stop_sync_clicked(GtkWidget* widget, gpointer data);
    
    // Pause/resume state
    bool is_paused_ = false;
    std::vector<int> paused_pids_;  // PIDs paused via SIGSTOP
    
    // Stall detection state
    double last_bytes_transferred_ = 0;
    int stall_count_ = 0;  // Consecutive polls with no progress
    int error_count_ = 0;  // Error count from last poll
    static constexpr int STALL_THRESHOLD = 120;  // 2 minutes of no progress
    static constexpr int ERROR_THRESHOLD = 10;   // Restart after 10 errors
    void check_and_recover_stall(double bytes, double speed, int errors, const std::string& last_error);
    void restart_bisync_with_resync();
    
    // Helpers
    void append_log(const std::string& text);
    
    // Status updates
    void refresh_job_statuses();
    static gboolean on_status_timeout(gpointer data);

    
    // Log reading
    long log_file_pos_ = 0;
    void read_external_log();
    
    // RC API polling for live stats
    void poll_rc_api_stats();
    int find_active_rc_port();
    
    void run_setup_flow();
    void run_job_flow(const std::string& edit_id = "", 
                      const std::string& edit_local = "",
                      const std::string& edit_remote = "",
                      const std::string& edit_type = "");
                      
    void update_progress_ui(const std::string& transferred, 
                            const std::string& speed, 
                            const std::string& file, 
                            double fraction);
    
    // New features
    void show_selective_sync_dialog(const std::string& remote_path);
    void show_conflict_resolution_dialog(const std::string& local_file, 
                                         const std::string& remote_file,
                                         const std::string& conflict_type);
    void show_version_history_dialog(const std::string& file_path);
    void load_settings_to_dialog(GtkWidget* dialog);
    void save_settings_from_dialog(GtkWidget* dialog);
    
    // Conflict tracking
    struct ConflictInfo {
        std::string local_path;
        std::string remote_path;
        std::string local_time;
        std::string remote_time;
        std::string local_size;
        std::string remote_size;
    };
    std::vector<ConflictInfo> pending_conflicts_;
    
    // File watcher for real-time sync
    std::unique_ptr<FileWatcher> file_watcher_;
    void init_file_watcher();
    void setup_watches_for_jobs();
    void trigger_job_sync(const std::string& job_id);
};

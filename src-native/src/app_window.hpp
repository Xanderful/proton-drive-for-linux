#pragma once

#include <gtk/gtk.h>
#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <chrono>
#include "file_index.hpp"

/**
 * Main Application Window (GTK4)
 * 
 * Modern sync-focused UI design:
 * - Hamburger menu for settings, profiles, and synced folders
 * - Prominent drag-and-drop zone as primary interaction
 * - Compact sync progress in corner
 * - Logs panel at bottom
 * - Cloud browser for file browsing
 */
class AppWindow {
public:
    static AppWindow& getInstance();

    /**
     * Initialize the main window
     * @return true on success
     */
    bool initialize();

    /**
     * Get the GTK window widget
     */
    GtkWidget* get_window() const { return window_; }

    /**
     * Show the window
     */
    void show();

    /**
     * Hide the window
     */
    void hide();

    /**
     * Toggle window visibility
     */
    void toggle_visibility();

    /**
     * Check if window is visible
     */
    bool is_visible() const;

    /**
     * Set callback for tray toggle
     */
    void set_toggle_callback(std::function<void()> callback);

    /**
     * Update sync progress display
     */
    void update_sync_progress(const std::string& file, 
                              double fraction,
                              const std::string& speed,
                              const std::string& transferred);

    /**
     * Append log message
     */
    void append_log(const std::string& message);

    /**
     * Refresh profiles list
     */
    void refresh_profiles();

    /**
     * Refresh synced folders list
     */
    void refresh_sync_jobs();

    /**
     * Update service status display
     */
    void update_service_status(bool is_running);

    /**
     * Get cloud tree widget for external callbacks
     */
    GtkWidget* get_cloud_tree() const { return cloud_tree_; }

    /**
     * Get local tree widget for external callbacks
     */
    GtkWidget* get_local_tree() const { return local_tree_; }

    /**
     * Show cloud context menu (public for static callbacks)
     */
    void show_cloud_context_menu(const std::string& path, bool is_dir, double x, double y);

    /**
     * Show in-app toast notification banner
     */
    void show_toast(const std::string& message, int duration_ms = 4000);

    /**
     * Show local context menu (public for static callbacks)
     */
    void show_local_context_menu(const std::string& path, bool is_dir, double x, double y);

    /**
     * Show version history dialog for a cloud file
     */
    void show_version_history_dialog(const std::string& cloud_path);

    /**
     * Show cloud delete confirmation dialog
     */
    void show_cloud_delete_confirm(const std::string& path, bool is_folder);

    /**
     * Show local cleanup dialog for synced folders
     */
    void show_local_cleanup_dialog();

    /**
     * Show sync job delete confirmation dialog
     */
    void show_sync_job_delete_confirm(const std::string& job_id, const std::string& folder_name);

    /**
     * Perform sync after user resolves conflict (public for dialog callback)
     */
    void perform_sync_after_conflict_resolution(const std::string& local_path,
                                                 const std::string& remote_path,
                                                 const std::string& sync_type,
                                                 const std::string& resolution);

    /**
     * Shutdown - stop all background threads and cleanup resources
     * Must be called before application exit to prevent crashes
     */
    void shutdown();

private:
    AppWindow() = default;
    ~AppWindow() = default;

    // Prevent copying
    AppWindow(const AppWindow&) = delete;
    AppWindow& operator=(const AppWindow&) = delete;

    // Window components
    GtkWidget* window_ = nullptr;
    GtkWidget* header_bar_ = nullptr;
    GtkWidget* hamburger_btn_ = nullptr;
    GtkWidget* hamburger_menu_ = nullptr;
    GtkWidget* sidebar_ = nullptr;          // Sidebar widget for collapse
    GtkWidget* sidebar_collapse_btn_ = nullptr;
    bool sidebar_expanded_ = true;
    std::vector<GtkWidget*> sidebar_text_widgets_;
    
    // Main content
    GtkWidget* main_stack_ = nullptr;      // Stack to switch between drop zone and browser
    GtkWidget* content_area_ = nullptr;    // Outermost content container
    GtkWidget* drop_zone_ = nullptr;       // Primary drop zone for file uploads
    GtkWidget* drop_zone_overlay_ = nullptr;
    GtkWidget* setup_banner_ = nullptr;    // Overview setup banner
    GtkWidget* setup_guide_ = nullptr;     // Settings setup guide
    bool setup_missing_ = false;
    
    // In-app toast banner (top of content area)
    GtkWidget* toast_revealer_ = nullptr;
    GtkWidget* toast_label_ = nullptr;
    guint toast_timeout_id_ = 0;
    
    // Active sync panel (visible in main view)
    GtkWidget* sync_activity_box_ = nullptr;
    GtkWidget* sync_activity_list_ = nullptr;
    GtkWidget* sync_activity_revealer_ = nullptr;
    GtkWidget* no_sync_label_ = nullptr;
    
    // File browser panel
    GtkWidget* file_browser_box_ = nullptr;
    GtkWidget* cloud_tree_ = nullptr;
    GtkWidget* local_tree_ = nullptr;
    GtkWidget* path_bar_ = nullptr;
    GtkWidget* cloud_scroll_ = nullptr;
    GtkWidget* local_scroll_ = nullptr;
    GtkWidget* browser_stack_ = nullptr;
    GtkWidget* browser_switcher_ = nullptr;
    GtkWidget* search_entry_ = nullptr;
    GtkWidget* search_status_label_ = nullptr;
    std::string pending_search_query_;
    bool search_waiting_for_index_ = false;
    std::string current_cloud_path_ = "/";
    std::string current_local_path_;
    
    // Cloud file cache to avoid blocking UI
    struct CloudFileCache {
        std::string path;
        std::string json_data;
        std::chrono::steady_clock::time_point timestamp;
        bool valid() const {
            auto age = std::chrono::steady_clock::now() - timestamp;
            return !json_data.empty() && age < std::chrono::minutes(5);
        }
    };
    std::map<std::string, CloudFileCache> cloud_cache_;
    
    // Logs panel (bottom)
    GtkWidget* logs_revealer_ = nullptr;
    GtkWidget* logs_view_ = nullptr;
    GtkWidget* logs_scroll_ = nullptr;
    GtkWidget* logs_toggle_ = nullptr;
    
    // Transfer progress popup
    GtkWidget* transfer_popup_ = nullptr;
    GtkWidget* transfer_list_ = nullptr;
    GtkWidget* transfer_progress_bar_ = nullptr;
    GtkWidget* transfer_header_label_ = nullptr;
    bool transfer_popup_visible_ = false;
    struct TransferItem {
        std::string filename;
        std::string status;  // "Uploading", "Downloading", "Completed", "Failed"
        double progress;     // 0.0 to 1.0
        int64_t bytes_transferred;
        int64_t total_bytes;
        std::string speed;   // e.g., "1.5 MB/s"
    };
    std::vector<TransferItem> active_transfers_;
    int completed_transfers_ = 0;
    int total_transfers_ = 0;
    
    // Auto-download tracking to prevent duplicate downloads
    std::set<std::string> active_downloads_;  // Tracks paths currently being downloaded
    std::mutex download_mutex_;                // Protects active_downloads_
    
    // Cloud monitoring state
    std::atomic<bool> cloud_monitoring_active_{false};
    std::atomic<bool> cloud_discovery_running_{false};  // Tracks async device discovery
    std::thread cloud_monitor_thread_;
    std::map<std::string, std::time_t> last_cloud_check_;  // job_id -> last check time
    
    // Hamburger menu contents
    GtkWidget* service_status_label_ = nullptr;
    GtkWidget* index_status_label_ = nullptr;  // Search index status
    GtkWidget* start_btn_ = nullptr;
    GtkWidget* stop_btn_ = nullptr;
    GtkWidget* profiles_list_ = nullptr;
    GtkWidget* jobs_list_ = nullptr;
    GtkWidget* devices_list_ = nullptr;  // For devices page
    
    // Build helpers
    void build_window();
    void build_devices_page();  // Build the devices page content
    void refresh_devices();     // Refresh device list
    void build_header_bar();
    void build_hamburger_menu();
    void build_drop_zone();
    void build_logs_panel();
    void build_sync_activity_panel();
    void build_file_browser();
    void update_setup_state();
    
    // Hamburger menu section builders
    GtkWidget* build_service_section();
    GtkWidget* build_profiles_section();
    GtkWidget* build_jobs_section();
    GtkWidget* build_settings_section();
    
    // Drop zone styling
    void set_drop_zone_highlight(bool active);
    
    // Button click handlers  
    void on_start_clicked();
    void on_stop_clicked();
    void on_add_sync_clicked();
    void on_add_profile_clicked();
    void on_settings_clicked();
    void on_browse_clicked();
    
    // View switching
    void show_drop_zone();
    void show_cloud_browser();
    
    // File browser methods
    void refresh_cloud_files();
    void refresh_cloud_files_async(bool force_refresh = false);
    void populate_cloud_tree(const std::string& json_data);
    void populate_cloud_tree_from_index(const std::vector<IndexedFile>& files);
    void refresh_local_files();
    void navigate_cloud(const std::string& path);
    void navigate_local(const std::string& path);
    void on_cloud_row_activated(const std::string& path, bool is_dir);
    void on_local_row_activated(const std::string& path, bool is_dir);
    void perform_search(const std::string& query);
    void invalidate_cloud_cache();  // Clear all cached cloud data
    
    // Cloud monitoring for automatic sync
    void start_cloud_monitoring();
    void stop_cloud_monitoring();
    void monitor_cloud_changes();  // Check for new/changed files in synced folders
    
    // Status polling
    void poll_status();
    void poll_sync_activity();
    
    // Conflict resolution UI
    void refresh_conflicts();
    void show_conflict_resolution(const std::string& conflict_path);
    void store_conflict(const std::string& path);
    void clear_conflicts();
    GtkWidget* conflicts_revealer_ = nullptr;
    GtkWidget* conflicts_list_ = nullptr;
    std::vector<std::string> detected_conflicts_;
    std::mutex conflicts_mutex_;
    
    // Transfer progress popup
    void build_transfer_popup();
    void show_transfer_popup();
    void hide_transfer_popup();
    void add_transfer_item(const std::string& filename, bool is_upload);
    void update_transfer_progress(const std::string& filename, double progress, 
                                   const std::string& speed, int64_t bytes_transferred);
    void complete_transfer_item(const std::string& filename, bool success);
    void refresh_transfer_list();
    
    // File drop handling
    void handle_dropped_files(const std::vector<std::string>& paths);
    void handle_cloud_drop(const std::vector<std::string>& local_paths, const std::string& cloud_folder);
    void show_sync_from_local_dialog(const std::string& local_path);
    void start_local_to_cloud_sync(const std::string& local_path,
                                   const std::string& remote_path,
                                   const std::string& sync_type);
    
    // Sync job context menu
    void show_sync_job_context_menu(const std::string& job_id, double x, double y, GtkWidget* card_widget);
    void show_sync_job_edit_dialog(const std::string& job_id);
    
    // Callbacks
    std::function<void()> toggle_callback_;
};

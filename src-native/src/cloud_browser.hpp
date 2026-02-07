#ifndef CLOUD_BROWSER_HPP
#define CLOUD_BROWSER_HPP

#include <gtk/gtk.h>
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>

// Forward declaration
struct IndexedFile;

/**
 * CloudBrowser - Browse Proton Drive cloud contents
 * 
 * Features:
 * - Tree view of cloud folders/files
 * - Selective sync checkboxes
 * - Shows sync status (synced, cloud-only, local-only)
 * - Right-click context menu for operations
 */

struct CloudItem {
    std::string name;
    std::string path;           // Full remote path (e.g., "proton:/Documents/Work")
    std::string local_path;     // Local sync path if synced
    int64_t size;
    std::string mod_time;
    bool is_directory;
    bool is_synced;             // Has local copy
    bool is_selected_for_sync;  // User wants to sync this
    
    // Device tracking
    std::string synced_device_id;       // Device ID that syncs this item
    std::string synced_device_name;     // Device name that syncs this item
    bool is_synced_on_this_device;      // True if synced on current machine
    bool is_partially_synced;           // True if folder has synced subfolders but isn't fully synced
    
    enum class SyncStatus {
        NOT_SYNCED,         // Cloud only, not part of any sync
        SYNCED,             // Both local and cloud (fully synced)
        PARTIALLY_SYNCED,   // Folder has synced subfolders
        LOCAL_ONLY,         // Only on local (pending upload)
        SYNCING,            // Currently syncing
        CONFLICT,           // File conflict
        CLOUD_ONLY          // Excluded from sync (stays in cloud only)
    };
    SyncStatus status;
};

class CloudBrowser {
public:
    static CloudBrowser& getInstance();
    
    // Get the widget to embed in UI
    GtkWidget* get_widget();
    
    // Refresh cloud listing
    void refresh();
    void refresh_async();
    void force_refresh_from_cloud();
    
    // Navigate to path
    void navigate_to(const std::string& path);
    void go_up();
    void go_home();
    
    // Get current path
    std::string get_current_path() const { return current_path_; }
    
    // Sync operations
    void sync_selected_items();
    void unsync_item(const std::string& path);
    void sync_to_cloud(const std::string& remote_path, const std::string& local_path);
    void include_item(const std::string& job_id, const std::string& exclude_path, const std::string& item_path);
    void remove_local_copy(const std::string& remote_path, const std::string& local_path);
    void download_item(const std::string& remote_path, const std::string& local_path);
    
    // File operations
    void open_file(const CloudItem& item);
    
    // Trash operations  
    void move_to_trash(const std::string& path);
    void restore_from_trash(const std::string& path);
    void empty_trash();
    std::vector<CloudItem> get_trash_contents();
    
    // Search operations
    void search_files(const std::string& query);
    void clear_search();
    void start_indexing();
    bool is_search_active() const { return is_search_mode_; }
    
    // Set callback for status updates
    void set_status_callback(std::function<void(const std::string&)> callback) {
        status_callback_ = callback;
    }
    
    // Set callback for sync request (legacy - simple rclone sync)
    void set_sync_callback(std::function<void(const std::string&, const std::string&)> callback) {
        sync_callback_ = callback;
    }
    
    // Set callback for sync-to-local dialog (shows advanced dialog with location choice)
    void set_sync_to_local_dialog_callback(std::function<void(const std::string&)> callback) {
        sync_to_local_dialog_callback_ = callback;
    }

private:
    CloudBrowser() = default;
    ~CloudBrowser() = default;
    
    void build_ui();
    void populate_tree(const std::vector<CloudItem>& items);
    void on_row_activated(GtkTreeView* tree, GtkTreePath* path, GtkTreeViewColumn* col);
    
#ifndef USE_GTK4
    // GTK3-only event handlers (these types don't exist in GTK4)
    void on_row_right_click(GtkWidget* widget, GdkEventButton* event);
    void show_context_menu(const CloudItem& item, GdkEventButton* event);
    
    // Drag-and-drop handler (GTK3-style)
    void on_drag_data_received(GtkWidget* widget, GdkDragContext* context,
                               gint x, gint y, GtkSelectionData* data,
                               guint info, guint time);
#endif

    void upload_files_to_cloud(const std::vector<std::string>& files, 
                               const std::string& target_folder);
    
    // List remote path using rclone
    std::vector<CloudItem> list_remote(const std::string& path);
    
    // Convert IndexedFile to CloudItem (for cached results)
    std::vector<CloudItem> convert_indexed_to_cloud_items(const std::vector<IndexedFile>& indexed_files);
    
    // Check if path is synced locally
    bool is_path_synced(const std::string& remote_path);
    std::string get_local_sync_path(const std::string& remote_path);
    
    // Search helpers
    void perform_search(const std::string& query);
    void show_search_results(const std::vector<IndexedFile>& results);
    void on_search_result_activated(const IndexedFile& file);
    
    GtkWidget* container_ = nullptr;
    GtkWidget* tree_view_ = nullptr;
    GtkWidget* path_bar_ = nullptr;
    GtkWidget* status_bar_ = nullptr;
    GtkWidget* search_entry_ = nullptr;
    GtkWidget* search_box_ = nullptr;
    GtkWidget* index_progress_bar_ = nullptr;
    GtkListStore* list_store_ = nullptr;
    
    std::string current_path_ = "proton:/";
    std::string remote_name_ = "proton";
    std::vector<CloudItem> current_items_;
    std::string last_search_query_;
    bool is_search_mode_ = false;
    
    std::atomic<bool> is_loading_{false};
    std::thread loading_thread_;
    
    std::atomic<bool> is_opening_file_{false};
    std::string currently_opening_path_;
    
    std::function<void(const std::string&)> status_callback_;
    std::function<void(const std::string&, const std::string&)> sync_callback_;
    std::function<void(const std::string&)> sync_to_local_dialog_callback_;
    
    // Trash settings
    static constexpr int TRASH_RETENTION_DAYS = 30;
    std::string trash_path_ = ".proton-drive-trash";
};

#endif // CLOUD_BROWSER_HPP
#ifndef FILE_INDEX_HPP
#define FILE_INDEX_HPP

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>

/**
 * FileIndex - SQLite-based cache for cloud file metadata
 * 
 * Features:
 * - Caches all file/folder names and paths from Proton Drive
 * - Enables fast full-text search without network calls
 * - Background indexing with progress updates
 * - Tracks sync status (cloud-only, synced locally)
 * - Persists to ~/.cache/proton-drive/file_index.db
 * 
 * Usage:
 *   auto& index = FileIndex::getInstance();
 *   index.start_background_index();  // Crawls cloud in background
 *   auto results = index.search("report.pdf");  // Fast local search
 */

struct IndexedFile {
    int64_t id;
    std::string name;           // File/folder name
    std::string path;           // Full remote path (e.g., "proton:/Documents/Work/report.pdf")
    std::string parent_path;    // Parent directory path
    int64_t size;               // Size in bytes (-1 for folders)
    std::string mod_time;       // ISO8601 modification time
    bool is_directory;
    bool is_synced;             // Has local copy
    std::string local_path;     // Local path if synced
    std::string extension;      // File extension (lowercase)
    
    // Search ranking
    double relevance_score;     // Calculated during search
};

struct IndexStats {
    int64_t total_files;
    int64_t total_folders;
    int64_t total_size_bytes;
    std::string last_full_index;    // ISO8601 timestamp
    std::string last_partial_index; // ISO8601 timestamp
    bool is_indexing;
    int index_progress_percent;     // 0-100
    std::string index_status;       // Human-readable status
};

class FileIndex {
public:
    static FileIndex& getInstance();
    
    // Initialize database (call once at startup)
    bool initialize();
    
    // Search files by name or path (uses FTS5 full-text search)
    // query: search terms (supports wildcards: * and ?)
    // limit: max results (0 = no limit)
    // include_folders: whether to include folders in results
    std::vector<IndexedFile> search(const std::string& query, 
                                     int limit = 100,
                                     bool include_folders = true);
    
    // Search with filters
    std::vector<IndexedFile> search_with_filters(
        const std::string& query,
        const std::string& extension_filter = "",  // e.g., "pdf" or "pdf,doc,docx"
        const std::string& path_prefix = "",       // e.g., "proton:/Documents"
        bool synced_only = false,
        bool cloud_only = false,
        int limit = 100
    );
    
    // Get files in a specific directory (for browsing)
    std::vector<IndexedFile> get_directory_contents(const std::string& path);
    
    // Get recently modified files
    std::vector<IndexedFile> get_recent_files(int limit = 50);
    
    // Get index statistics
    IndexStats get_stats();
    
    // Background indexing
    void start_background_index(bool full_reindex = false);
    void stop_background_index();
    bool is_indexing() const { return is_indexing_.load(); }
    int get_index_progress() const { return index_progress_.load(); }
    
    // Set callback for index progress updates
    using ProgressCallback = std::function<void(int percent, const std::string& status)>;
    void set_progress_callback(ProgressCallback callback) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        progress_callback_ = callback;
    }
    
    // Update sync status for a file
    void update_sync_status(const std::string& remote_path, 
                            bool is_synced, 
                            const std::string& local_path = "");
    
    // Incremental index updates (called when files are synced)
    // These avoid the full 5-10 minute reindex
    void add_or_update_file(const std::string& remote_path,
                            const std::string& name,
                            int64_t size,
                            const std::string& mod_time,
                            bool is_directory,
                            bool is_synced = false,
                            const std::string& local_path = "");
    
    void remove_file(const std::string& remote_path);
    
    // Update multiple files at once (batch from sync job)
    void update_files_from_sync(const std::string& job_id,
                                const std::string& local_path,
                                const std::string& remote_path);
    
    // Clear the entire index (for troubleshooting)
    void clear_index();
    
    // Remove index entries that no longer exist in cloud
    // paths_seen: set of full paths (e.g. "proton:/Documents") that were found in a scan
    // parent_path: the parent whose children should be pruned
    void prune_stale_entries(const std::string& parent_path, 
                             const std::vector<std::string>& paths_seen);
    
    // Check if index needs refresh (e.g., older than X hours)
    bool needs_refresh(int max_age_hours = 24) const;
    
    // Check if a path exists in the index (even if it's an empty folder)
    bool path_exists(const std::string& path) const;
    
    // Check if database is encrypted
    bool is_encrypted() const { return is_encrypted_; }
    
    // Set encryption key (must be called BEFORE initialize())
    // Key should be derived from user's Proton session or passphrase
    void set_encryption_key(const std::string& key);
    
    // Graceful shutdown - encrypts database before app exits
    // MUST be called before gtk_main_quit() to avoid crash
    void shutdown();

private:
    FileIndex();
    ~FileIndex();
    
    // Prevent copying
    FileIndex(const FileIndex&) = delete;
    FileIndex& operator=(const FileIndex&) = delete;
    
    // Database operations
    bool create_tables();
    bool insert_file(const IndexedFile& file);
    bool insert_files_batch(const std::vector<IndexedFile>& files);
    void update_last_index_time();
    
    // Indexing worker
    void index_worker(bool full_reindex);
    std::vector<IndexedFile> fetch_remote_listing(const std::string& path, bool recursive);
    
    // Helper to get extension from filename
    static std::string get_extension(const std::string& filename);
    
    // Report progress
    void report_progress(int percent, const std::string& status);
    
    // Database handle (opaque pointer for SQLite)
    void* db_ = nullptr;
    std::string db_path_;
    
    // Encryption
    bool is_encrypted_ = false;
    std::string encryption_key_;
    bool shutdown_complete_ = false;
    
    // Indexing state
    std::atomic<bool> is_indexing_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<int> index_progress_{0};
    std::thread index_thread_;
    
    // Callback
    std::mutex callback_mutex_;
    ProgressCallback progress_callback_;
    
    // Remote name for rclone
    std::string remote_name_ = "proton";
};

#endif // FILE_INDEX_HPP

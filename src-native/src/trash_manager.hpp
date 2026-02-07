#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <map>
#include <mutex>

namespace proton {

/**
 * Trash Manager for Proton Drive Linux
 * 
 * Manages a local trash directory for synced files that are removed locally.
 * Files are moved to ~/.cache/proton-drive/trash/ with metadata
 * and automatically cleaned up after a configurable retention period (default 30 days).
 * 
 * Features:
 * - Safe file deletion with trash storage
 * - Metadata tracking (original path, deletion time, cloud path)
 * - Automatic cleanup of old items
 * - Restore capability
 * - Integration with system trash (gio trash) as fallback
 */
class TrashManager {
public:
    struct TrashItem {
        std::string trash_path;           // Path in trash directory
        std::string original_path;        // Original local path
        std::string cloud_path;           // Cloud path (if applicable)
        std::chrono::system_clock::time_point deleted_at;
        size_t size_bytes;                // File/folder size
        bool is_directory;
    };

    static TrashManager& getInstance();
    
    /**
     * Initialize trash directory and load metadata
     */
    bool initialize();
    
    /**
     * Move a file/folder to trash
     * @param local_path Path to move to trash
     * @param cloud_path Associated cloud path (optional)
     * @return true on success
     */
    bool move_to_trash(const std::string& local_path, const std::string& cloud_path = "");
    
    /**
     * Restore a file/folder from trash
     * @param trash_path Path in trash to restore
     * @param restore_path Where to restore to (defaults to original path)
     * @return true on success
     */
    bool restore(const std::string& trash_path, const std::string& restore_path = "");
    
    /**
     * Permanently delete a trash item
     * @param trash_path Path in trash to delete
     * @return true on success
     */
    bool delete_permanent(const std::string& trash_path);
    
    /**
     * Clean up trash items older than retention days
     * @param retention_days Items older than this will be deleted (0 = use default from settings)
     * @return Number of items cleaned up
     */
    int cleanup_old_items(int retention_days = 0);
    
    /**
     * Get all items currently in trash
     */
    std::vector<TrashItem> get_trash_items() const;
    
    /**
     * Get total size of trash directory
     */
    size_t get_trash_size() const;
    
    /**
     * Empty entire trash
     * @return Number of items deleted
     */
    int empty_trash();
    
    /**
     * Get trash directory path
     */
    std::string get_trash_dir() const { return trash_dir_; }

private:
    TrashManager();
    ~TrashManager() = default;
    
    TrashManager(const TrashManager&) = delete;
    TrashManager& operator=(const TrashManager&) = delete;
    
    bool load_metadata();
    bool save_metadata();
    size_t calculate_size(const std::string& path) const;
    std::string generate_unique_trash_name(const std::string& original_path);
    
    std::string trash_dir_;
    std::string metadata_file_;
    std::map<std::string, TrashItem> items_;  // trash_path -> TrashItem
    mutable std::mutex mutex_;
};

} // namespace proton

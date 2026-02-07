#ifndef SYNC_JOB_METADATA_HPP
#define SYNC_JOB_METADATA_HPP

#include <string>
#include <vector>
#include <optional>
#include <ctime>

/**
 * SyncJobMetadata - Tracks sync job ownership and conflict detection
 * 
 * Each sync job has:
 *   - job_id: Unique identifier for this sync configuration
 *   - local_path: Local folder being synced
 *   - remote_path: Remote folder on Proton Drive
 *   - origin_device_id: Device that created this sync job
 *   - origin_device_name: Human-readable name of origin device
 *   - shared_devices: List of device IDs that share this sync (multi-device sync)
 *   - sync_mode: "exclusive" (one device only) or "shared" (multi-device)
 * 
 * Conflict Detection Logic:
 *   1. If job has sync_mode="shared", allow multiple devices to sync
 *   2. If job has sync_mode="exclusive" and another device tries to sync:
 *      - Show conflict warning
 *      - Offer to: join shared sync, create separate folder, or cancel
 *   3. Track last_sync_device to know which device last modified files
 */

struct DeviceInfo {
    std::string device_id;
    std::string device_name;
    std::time_t last_sync_time;
    
    std::string toJson() const;
    static DeviceInfo fromJson(const std::string& json);
};

struct SyncJobMetadata {
    std::string job_id;
    std::string local_path;
    std::string remote_path;
    std::string sync_type;  // "bisync", "sync-up", "sync-down"
    
    // Device tracking
    std::string origin_device_id;
    std::string origin_device_name;
    std::string sync_mode;  // "exclusive" or "shared"
    std::vector<DeviceInfo> shared_devices;
    
    // Sync state
    std::time_t created_at;
    std::time_t last_sync_time;
    std::string last_sync_device_id;
    std::string last_sync_status;  // "success", "failed", "conflict"
    
    // Helpers
    std::string toJson() const;
    static SyncJobMetadata fromJson(const std::string& json);
    
    bool isSharedSync() const { return sync_mode == "shared"; }
    bool isOriginDevice(const std::string& device_id) const { 
        return origin_device_id == device_id; 
    }
    bool isAuthorizedDevice(const std::string& device_id) const;
    void addSharedDevice(const DeviceInfo& device);
    void removeSharedDevice(const std::string& device_id);
};

/**
 * SyncJobRegistry - Manages all sync jobs with conflict detection
 */
class SyncJobRegistry {
public:
    static SyncJobRegistry& getInstance();
    
    // Load/save jobs from config
    void loadJobs();
    void saveJobs();
    
    // Job management
    std::string createJob(const std::string& local_path, 
                          const std::string& remote_path,
                          const std::string& sync_type);
    void updateJob(const SyncJobMetadata& job);
    void deleteJob(const std::string& job_id);
    SyncJobMetadata* getJob(const std::string& job_id);
    std::optional<SyncJobMetadata> getJobById(const std::string& job_id) const;
    std::vector<SyncJobMetadata> getAllJobs() const;
    
    // Conflict detection
    enum class ConflictType {
        NONE,                    // No conflict
        SAME_REMOTE_EXCLUSIVE,   // Another device has exclusive sync to same remote
        SAME_LOCAL_DIFFERENT_REMOTE, // Same local path syncing to different remotes
        DIFFERENT_DEVICE_UNSHARED,   // Job exists but not shared with this device
        CLOUD_FOLDER_SAME_DEVICE,    // Cloud folder exists, created by this device
        CLOUD_FOLDER_DIFFERENT_DEVICE // Cloud folder exists, created by another device
    };
    
    struct ConflictInfo {
        ConflictType type;
        std::string conflicting_job_id;
        std::string conflicting_device_id;
        std::string conflicting_device_name;
        std::string message;
        std::string existing_remote_path;  // The existing cloud folder path
    };
    
    // Cloud folder metadata (stored in .proton-sync-meta.json in cloud folders)
    struct CloudFolderMeta {
        std::string device_id;
        std::string device_name;
        std::string created_at;
        std::string folder_name;
        bool is_valid = false;
    };
    
    ConflictInfo checkForConflicts(const std::string& local_path,
                                   const std::string& remote_path) const;
    
    // Check if local path is already synced (returns job_id if found, empty if not)
    std::optional<SyncJobMetadata> findJobByLocalPath(const std::string& local_path) const;
    
    // Check if local path is inside or contains an existing synced folder
    bool isPathNestedWithSyncedFolder(const std::string& local_path, std::string& conflicting_path) const;
    
    // Case-insensitive path matching for cloud paths
    bool cloudPathsConflict(const std::string& path1, const std::string& path2) const;
    static bool checkCloudFolderExists(const std::string& remote_path);
    static CloudFolderMeta getCloudFolderMetadata(const std::string& remote_path);
    static bool writeCloudFolderMetadata(const std::string& remote_path, const CloudFolderMeta& meta);
    
    // Automatic cleanup of stale entries (no .conf file)
    void cleanupStaleEntries();
    
    // Enhanced conflict check that includes cloud folder inspection
    ConflictInfo checkForCloudFolderConflicts(const std::string& local_path,
                                              const std::string& remote_path) const;
    
    // Multi-device operations
    void enableSharedSync(const std::string& job_id);
    void joinSharedSync(const std::string& job_id);
    void leaveSharedSync(const std::string& job_id);
    
    // Record sync activity
    void recordSyncStart(const std::string& job_id);
    void recordSyncComplete(const std::string& job_id, bool success);
    
    // Local filesystem safety checks
    struct LocalPathStatus {
        bool exists = false;
        bool is_writable = false;
        bool is_valid_mount = false;          // True if path's mount point is accessible
        bool is_compatible_filesystem = true; // False for FAT32 with large files, read-only, etc.
        bool has_sufficient_space = true;     // False if disk nearly full
        int64_t available_bytes = 0;
        int64_t required_bytes = 0;           // Estimated space needed
        std::string mount_point;
        std::string filesystem_type;
        std::string error_message;            // Describes any issues
    };
    
    // Local path conflict types
    enum class LocalConflictType {
        NONE,                       // No local conflict
        FOLDER_EXISTS_SAME_DEVICE,  // Folder exists, was synced by this device
        FOLDER_EXISTS_DIFFERENT_DEVICE, // Folder exists, synced by different device
        FOLDER_EXISTS_UNKNOWN,      // Folder exists, no metadata (user-created)
        FILE_EXISTS,                // File with same name exists
        PARENT_NOT_WRITABLE,        // Parent directory not writable
        INVALID_FILESYSTEM,         // Filesystem not compatible
        INSUFFICIENT_SPACE,         // Not enough disk space
        UNMOUNTABLE_PATH            // Path is on unmounted/inaccessible drive
    };
    
    struct LocalConflictInfo {
        LocalConflictType type = LocalConflictType::NONE;
        std::string existing_path;
        std::string existing_device_id;
        std::string existing_device_name;
        std::string message;
        LocalPathStatus path_status;
    };
    
    // Local metadata for sync folders (stored in .proton-sync-local.json)
    struct LocalFolderMeta {
        std::string device_id;
        std::string device_name;
        std::string remote_path;      // The cloud path this syncs with
        std::string created_at;
        bool is_valid = false;
    };
    
    // Check local path safety (permissions, filesystem, space)
    static LocalPathStatus checkLocalPath(const std::string& local_path, int64_t required_bytes = 0);
    
    // Check for conflicts at a local path when syncing from cloud
    static LocalConflictInfo checkLocalPathConflicts(const std::string& local_path);
    
    // Get metadata from local sync folder
    static LocalFolderMeta getLocalFolderMetadata(const std::string& local_path);
    
    // Write metadata to local sync folder
    static bool writeLocalFolderMetadata(const std::string& local_path, const LocalFolderMeta& meta);
    
    // Get default sync location
    static std::string getDefaultSyncLocation();
    
    // Ensure default sync location exists (creates if needed)
    static bool ensureDefaultSyncLocation();
    
    // ========== Multi-Device Config Sync ==========
    
    /**
     * Export current config to cloud for multi-device sync
     * Stores config in proton:/.proton-sync-config/device_<id>.json
     */
    bool exportConfigToCloud();
    
    /**
     * Import config from cloud (merge with local)
     * Reads configs from other devices and offers to sync same folders
     */
    std::vector<SyncJobMetadata> importConfigFromCloud();
    
    /**
     * Get list of devices that have synced config to cloud
     */
    struct CloudDeviceConfig {
        std::string device_id;
        std::string device_name;
        std::time_t last_updated;
        std::vector<SyncJobMetadata> jobs;
    };
    std::vector<CloudDeviceConfig> getCloudDeviceConfigs();

private:
    SyncJobRegistry() = default;
    ~SyncJobRegistry() = default;
    SyncJobRegistry(const SyncJobRegistry&) = delete;
    SyncJobRegistry& operator=(const SyncJobRegistry&) = delete;
    
    std::vector<SyncJobMetadata> jobs_;
    std::string config_path_;
    
    // Private helper function
    void migrateOrphanConfFiles();
};

#endif // SYNC_JOB_METADATA_HPP

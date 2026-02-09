#include "sync_job_metadata.hpp"
#include "device_identity.hpp"
#include "app_window_helpers.hpp"
#include "logger.hpp"
#include <fstream>
#include <sstream>
#include <random>
#include <iomanip>
#include <filesystem>
#include <algorithm>
#include <array>
#include <memory>
#include <chrono>
#include <thread>
#include <atomic>
#include <cstdio>
#include <unistd.h>
#include <sys/statfs.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <cerrno>
#include <cstring>

namespace fs = std::filesystem;

// ============ DeviceInfo Implementation ============

std::string DeviceInfo::toJson() const {
    std::stringstream ss;
    ss << "{";
    ss << "\"device_id\":\"" << device_id << "\",";
    ss << "\"device_name\":\"" << device_name << "\",";
    ss << "\"last_sync_time\":" << last_sync_time;
    ss << "}";
    return ss.str();
}

// ============ SyncJobMetadata Implementation ============

std::string SyncJobMetadata::toJson() const {
    std::stringstream ss;
    ss << "  {\n";
    ss << "    \"job_id\": \"" << job_id << "\",\n";
    ss << "    \"local_path\": \"" << local_path << "\",\n";
    ss << "    \"remote_path\": \"" << remote_path << "\",\n";
    ss << "    \"sync_type\": \"" << sync_type << "\",\n";
    ss << "    \"origin_device_id\": \"" << origin_device_id << "\",\n";
    ss << "    \"origin_device_name\": \"" << origin_device_name << "\",\n";
    ss << "    \"sync_mode\": \"" << sync_mode << "\",\n";
    ss << "    \"created_at\": " << created_at << ",\n";
    ss << "    \"last_sync_time\": " << last_sync_time << ",\n";
    ss << "    \"last_sync_device_id\": \"" << last_sync_device_id << "\",\n";
    ss << "    \"last_sync_status\": \"" << last_sync_status << "\",\n";
    ss << "    \"shared_devices\": [\n";
    for (size_t i = 0; i < shared_devices.size(); i++) {
        ss << "      " << shared_devices[i].toJson();
        if (i < shared_devices.size() - 1) ss << ",";
        ss << "\n";
    }
    ss << "    ]\n";
    ss << "  }";
    return ss.str();
}

bool SyncJobMetadata::isAuthorizedDevice(const std::string& device_id) const {
    // Origin device is always authorized
    if (origin_device_id == device_id) return true;
    
    // In shared mode, check shared_devices list
    if (sync_mode == "shared") {
        for (const auto& dev : shared_devices) {
            if (dev.device_id == device_id) return true;
        }
    }
    
    return false;
}

void SyncJobMetadata::addSharedDevice(const DeviceInfo& device) {
    // Don't add duplicates
    for (const auto& dev : shared_devices) {
        if (dev.device_id == device.device_id) return;
    }
    shared_devices.push_back(device);
}

void SyncJobMetadata::removeSharedDevice(const std::string& device_id) {
    shared_devices.erase(
        std::remove_if(shared_devices.begin(), shared_devices.end(),
            [&device_id](const DeviceInfo& dev) {
                return dev.device_id == device_id;
            }),
        shared_devices.end()
    );
}

// ============ SyncJobRegistry Implementation ============

SyncJobRegistry& SyncJobRegistry::getInstance() {
    static SyncJobRegistry instance;
    return instance;
}

void SyncJobRegistry::loadJobs() {
    const char* home = std::getenv("HOME");
    if (!home) return;
    
    config_path_ = std::string(home) + "/.config/proton-drive/sync_jobs.json";
    
    std::ifstream file(config_path_);
    if (!file.is_open()) {
        Logger::info("[SyncJobRegistry] No existing jobs file, will check for .conf files to migrate");
        // Don't return - let cleanupStaleEntries and migrateOrphanConfFiles run
        // so orphan .conf files can be imported on first run
    } else {
        // Parse JSON (simplified parser)
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        file.close();
    
        jobs_.clear();
        
        // Find each job block
        size_t pos = 0;
        int job_count = 0;
        while ((pos = content.find("\"job_id\"", pos)) != std::string::npos) {
            SyncJobMetadata job;
            size_t search_pos = pos;  // Keep original position for loop increment
            
            // Extract job_id
            size_t start = content.find(": \"", search_pos) + 3;
            size_t end = content.find("\"", start);
            if (start < end) job.job_id = content.substr(start, end - start);
            
            // Extract local_path
            search_pos = content.find("\"local_path\"", pos);
            if (search_pos != std::string::npos) {
                start = content.find(": \"", search_pos) + 3;
                end = content.find("\"", start);
                if (start < end) job.local_path = content.substr(start, end - start);
            }
            
            // Extract remote_path
            search_pos = content.find("\"remote_path\"", pos);
            if (search_pos != std::string::npos) {
                start = content.find(": \"", search_pos) + 3;
                end = content.find("\"", start);
                if (start < end) job.remote_path = content.substr(start, end - start);
            }
            
            // Extract sync_type
            search_pos = content.find("\"sync_type\"", pos);
            if (search_pos != std::string::npos) {
                start = content.find(": \"", search_pos) + 3;
                end = content.find("\"", start);
                if (start < end) job.sync_type = content.substr(start, end - start);
            }
            
            // Extract origin_device_id
            search_pos = content.find("\"origin_device_id\"", pos);
            if (search_pos != std::string::npos) {
                start = content.find(": \"", search_pos) + 3;
                end = content.find("\"", start);
                if (start < end) job.origin_device_id = content.substr(start, end - start);
            }
            
            // Extract origin_device_name
            search_pos = content.find("\"origin_device_name\"", pos);
            if (search_pos != std::string::npos) {
                start = content.find(": \"", search_pos) + 3;
                end = content.find("\"", start);
                if (start < end) job.origin_device_name = content.substr(start, end - start);
            }
            
            // Extract sync_mode
            search_pos = content.find("\"sync_mode\"", pos);
            if (search_pos != std::string::npos) {
                start = content.find(": \"", search_pos) + 3;
                end = content.find("\"", start);
                if (start < end) job.sync_mode = content.substr(start, end - start);
            }
            
            if (!job.job_id.empty()) {
                jobs_.push_back(job);
                job_count++;
            }
            
            pos++;  // Increment from original job_id position, not from last field search
       }
        
        Logger::info("[SyncJobRegistry] Loaded " + std::to_string(jobs_.size()) + " sync jobs");
    }
    
    // CRITICAL: Clean up stale entries that don't have corresponding .conf files
    // This handles cases where jobs were deleted before our dual-cleanup fix was implemented
    cleanupStaleEntries();
    
    // MIGRATION: Import orphan .conf files that were created by old code before registry was added
    migrateOrphanConfFiles();
}

void SyncJobRegistry::migrateOrphanConfFiles() {
    const char* home = std::getenv("HOME");
    if (!home) return;
    
    std::string job_dir = std::string(home) + "/.config/proton-drive/jobs";
    
    // Check if jobs directory exists (use safe_exists to prevent crash)
    if (!AppWindowHelpers::safe_exists(job_dir) || !AppWindowHelpers::safe_is_directory(job_dir)) {
        return;  // No jobs directory, nothing to migrate
    }
    
    int migrated_count = 0;
    
    // Scan all .conf files in jobs directory
    try {
        for (const auto& entry : fs::directory_iterator(job_dir)) {
            if (!entry.is_regular_file()) continue;
            
            std::string filename = entry.path().filename().string();
            if (filename.find(".conf") == std::string::npos) continue;
            
            // Extract job ID from filename (remove .conf extension)
            std::string job_id = filename.substr(0, filename.find(".conf"));
            
            // Check if this job already exists in registry
            bool exists_in_registry = false;
            for (const auto& job : jobs_) {
                if (job.job_id == job_id) {
                    exists_in_registry = true;
                    break;
                }
            }
            
            if (exists_in_registry) {
                continue;  // Already in registry, skip
            }
            
            // Parse the .conf file
            std::ifstream conf_file(entry.path());
            if (!conf_file.is_open()) continue;
            
            std::string local_path, remote_path, sync_type;
            std::string line;
            
            while (std::getline(conf_file, line)) {
                if (line.empty() || line[0] == '#') continue;
                
                // Parse REMOTE_PATH="proton:/path"
                if (line.find("REMOTE_PATH=") == 0) {
                    size_t start = line.find('"') + 1;
                    size_t end = line.rfind('"');
                    if (start < end) {
                        remote_path = line.substr(start, end - start);
                        // Remove "proton:" prefix
                        if (remote_path.find("proton:") == 0) {
                            remote_path = remote_path.substr(7);
                        }
                    }
                }
                // Parse LOCAL_PATH="/path"
                else if (line.find("LOCAL_PATH=") == 0) {
                    size_t start = line.find('"') + 1;
                    size_t end = line.rfind('"');
                    if (start < end) {
                        local_path = line.substr(start, end - start);
                    }
                }
                // Parse SYNC_TYPE="bisync"
                else if (line.find("SYNC_TYPE=") == 0) {
                    size_t start = line.find('"') + 1;
                    size_t end = line.rfind('"');
                    if (start < end) {
                        sync_type = line.substr(start, end - start);
                    }
                }
            }
            conf_file.close();
            
            // Validate required fields
            if (local_path.empty() || remote_path.empty() || sync_type.empty()) {
                Logger::warn("[SyncJobRegistry] Skipping invalid .conf file: " + filename + 
                           " (missing required fields)");
                continue;
            }
            
            // Check if local path still exists (use safe_exists to handle I/O errors)
            // If there's an I/O error, we'll still migrate the job so it appears in the UI
            bool local_exists = AppWindowHelpers::safe_exists(local_path);
            if (!local_exists && !local_path.empty()) {
                Logger::info("[SyncJobRegistry] Migrating .conf with inaccessible local path: " + filename + 
                           " (local: " + local_path + ", may have I/O errors)");
                // Don't skip - let it be added so user can see and manage it
            }
            
            // Create metadata entry for this orphan job
            SyncJobMetadata job;
            job.job_id = job_id;
            job.local_path = local_path;
            job.remote_path = remote_path;
            job.sync_type = sync_type;
            job.origin_device_id = DeviceIdentity::getInstance().getDeviceId();
            job.origin_device_name = DeviceIdentity::getInstance().getDeviceName();
            job.sync_mode = "exclusive";
            job.created_at = std::time(nullptr);
            job.last_sync_time = 0;
            job.last_sync_status = "migrated";
            
            jobs_.push_back(job);
            migrated_count++;
            
            Logger::info("[SyncJobRegistry] Migrated orphan .conf: " + job_id + 
                        " (local: " + local_path + ", remote: " + remote_path + ")");
        }
    } catch (const std::exception& e) {
        Logger::error("[SyncJobRegistry] Error during .conf migration: " + std::string(e.what()));
    }
    
    if (migrated_count > 0) {
        saveJobs();  // Persist migrated jobs
        Logger::info("[SyncJobRegistry] Successfully migrated " + std::to_string(migrated_count) + 
                    " orphan .conf files to registry");
    }
}

void SyncJobRegistry::cleanupStaleEntries() {
    const char* home = std::getenv("HOME");
    if (!home) return;
    
    std::string job_dir = std::string(home) + "/.config/proton-drive/jobs";
    std::string cache_dir = std::string(home) + "/.cache/rclone/bisync";
    
    bool found_stale = false;
    size_t original_count = jobs_.size();
    
    // Check each job in registry to see if its .conf file exists
    jobs_.erase(
        std::remove_if(jobs_.begin(), jobs_.end(),
            [&](const SyncJobMetadata& job) {
                std::string conf_path = job_dir + "/" + job.job_id + ".conf";
                bool conf_exists = AppWindowHelpers::safe_exists(conf_path);
                
                if (!conf_exists) {
                    found_stale = true;
                    Logger::info("[SyncJobRegistry] Found stale entry: " + job.job_id + 
                                " (local: " + job.local_path + ", remote: " + job.remote_path + ")");
                    
                    // Also clean up bisync cache for this stale job
                    if (!job.local_path.empty() && !job.remote_path.empty()) {
                        // Convert paths to bisync cache filename pattern
                        std::string local_cleaned = job.local_path;
                        if (local_cleaned[0] == '/') local_cleaned = local_cleaned.substr(1);
                        std::replace(local_cleaned.begin(), local_cleaned.end(), '/', '_');
                        
                        std::string remote_cleaned = job.remote_path;
                        size_t colon_pos = remote_cleaned.find(":/");
                        if (colon_pos != std::string::npos) {
                            remote_cleaned.replace(colon_pos, 2, "_");
                        }
                        std::replace(remote_cleaned.begin(), remote_cleaned.end(), '/', '_');
                        
                        std::string cache_pattern = local_cleaned + ".." + remote_cleaned;
                        
                        // Remove cache files
                        try {
                            for (const auto& entry : fs::directory_iterator(cache_dir)) {
                                if (entry.path().filename().string().find(cache_pattern) == 0) {
                                    fs::remove(entry.path());
                                    Logger::info("[SyncJobRegistry] Cleaned stale cache: " + 
                                               entry.path().filename().string());
                                }
                            }
                        } catch (const std::exception& e) {
                            // Cache dir might not exist, that's okay
                        }
                    }
                    
                    return true;  // Remove this job from registry
                }
                return false;  // Keep this job
            }),
        jobs_.end()
    );
    
    if (found_stale) {
        size_t removed_count = original_count - jobs_.size();
        Logger::info("[SyncJobRegistry] Cleaned " + std::to_string(removed_count) + 
                    " stale entries from registry");
        saveJobs();  // Persist the cleaned registry
    }
}

void SyncJobRegistry::saveJobs() {
    if (config_path_.empty()) {
        const char* home = std::getenv("HOME");
        if (!home) return;
        config_path_ = std::string(home) + "/.config/proton-drive/sync_jobs.json";
    }
    
    // Ensure directory exists
    std::error_code ec_dir;
    fs::path config_dir = fs::path(config_path_).parent_path();
    if (!fs::exists(config_dir, ec_dir)) {
        fs::create_directories(config_dir, ec_dir);
    }
    
    std::ofstream file(config_path_);
    if (!file.is_open()) {
        Logger::error("[SyncJobRegistry] Failed to save jobs to " + config_path_);
        return;
    }
    
    file << "{\n";
    file << "  \"version\": 2,\n";
    file << "  \"device_id\": \"" << DeviceIdentity::getInstance().getDeviceId() << "\",\n";
    file << "  \"jobs\": [\n";
    
    for (size_t i = 0; i < jobs_.size(); i++) {
        file << jobs_[i].toJson();
        if (i < jobs_.size() - 1) file << ",";
        file << "\n";
    }
    
    file << "  ]\n";
    file << "}\n";
    file.close();
    
    Logger::info("[SyncJobRegistry] Saved " + std::to_string(jobs_.size()) + " sync jobs");
    
    // Export config to cloud for multi-device discovery (async, non-blocking)
    // Debounce: only export if at least 30 seconds since last export
    if (!jobs_.empty()) {
        static std::atomic<std::time_t> last_export_time{0};
        std::time_t now = std::time(nullptr);
        std::time_t last = last_export_time.load();
        if (now - last >= 30) {
            last_export_time.store(now);
            std::thread([this]() {
                try {
                    exportConfigToCloud();
                } catch (const std::exception& e) {
                    Logger::warn("[SyncJobRegistry] Cloud export failed: " + std::string(e.what()));
                }
            }).detach();
        } else {
            Logger::debug("[SyncJobRegistry] Skipping cloud export (debounce, last was " + 
                         std::to_string(now - last) + "s ago)");
        }
    }
}

std::string SyncJobRegistry::createJob(const std::string& local_path,
                                        const std::string& remote_path,
                                        const std::string& sync_type) {
    // Generate job ID
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    for (int i = 0; i < 8; i++) {
        ss << std::hex << dis(gen);
    }
    std::string job_id = ss.str();
    
    // Create job with current device as origin
    SyncJobMetadata job;
    job.job_id = job_id;
    job.local_path = local_path;
    job.remote_path = remote_path;
    job.sync_type = sync_type;
    job.origin_device_id = DeviceIdentity::getInstance().getDeviceId();
    job.origin_device_name = DeviceIdentity::getInstance().getDeviceName();
    job.sync_mode = "exclusive";  // Default to exclusive, user can enable sharing
    job.created_at = std::time(nullptr);
    job.last_sync_time = 0;
    job.last_sync_status = "pending";
    
    jobs_.push_back(job);
    saveJobs();
    
    Logger::info("[SyncJobRegistry] Created job " + job_id + " for " + local_path);
    return job_id;
}

void SyncJobRegistry::updateJob(const SyncJobMetadata& job) {
    for (auto& existing : jobs_) {
        if (existing.job_id == job.job_id) {
            existing = job;
            saveJobs();
            return;
        }
    }
}

void SyncJobRegistry::deleteJob(const std::string& job_id) {
    // Also delete the corresponding .conf file to prevent re-import on restart
    const char* home = std::getenv("HOME");
    if (home) {
        std::string conf_path = std::string(home) + "/.config/proton-drive/jobs/" + job_id + ".conf";
        std::error_code ec;
        if (fs::exists(conf_path, ec)) {
            fs::remove(conf_path, ec);
            if (ec) {
                Logger::warn("[SyncJobRegistry] Failed to delete .conf file for " + job_id + ": " + ec.message());
            } else {
                Logger::info("[SyncJobRegistry] Deleted .conf file for " + job_id);
            }
        }
    }
    
    jobs_.erase(
        std::remove_if(jobs_.begin(), jobs_.end(),
            [&job_id](const SyncJobMetadata& job) {
                return job.job_id == job_id;
            }),
        jobs_.end()
    );
    saveJobs();
    Logger::info("[SyncJobRegistry] Deleted job " + job_id);
}

SyncJobMetadata* SyncJobRegistry::getJob(const std::string& job_id) {
    for (auto& job : jobs_) {
        if (job.job_id == job_id) {
            return &job;
        }
    }
    return nullptr;
}

std::optional<SyncJobMetadata> SyncJobRegistry::getJobById(const std::string& job_id) const {
    for (const auto& job : jobs_) {
        if (job.job_id == job_id) {
            return job;
        }
    }
    return std::nullopt;
}

std::vector<SyncJobMetadata> SyncJobRegistry::getAllJobs() const {
    return jobs_;
}

std::optional<SyncJobMetadata> SyncJobRegistry::findJobByLocalPath(const std::string& local_path) const {
    fs::path check_path = fs::path(local_path).lexically_normal();
    
    for (const auto& job : jobs_) {
        fs::path job_path = fs::path(job.local_path).lexically_normal();
        if (check_path == job_path) {
            return job;
        }
    }
    return std::nullopt;
}

bool SyncJobRegistry::isPathNestedWithSyncedFolder(const std::string& local_path, 
                                                    std::string& conflicting_path) const {
    fs::path check_path = fs::path(local_path).lexically_normal();
    
    for (const auto& job : jobs_) {
        fs::path job_path = fs::path(job.local_path).lexically_normal();
        
        // Check if check_path is inside job_path (syncing a subfolder of existing sync)
        auto rel = check_path.lexically_relative(job_path);
        if (!rel.empty() && rel.native()[0] != '.') {
            conflicting_path = job.local_path;
            return true;
        }
        
        // Check if job_path is inside check_path (syncing a parent of existing sync)
        rel = job_path.lexically_relative(check_path);
        if (!rel.empty() && rel.native()[0] != '.') {
            conflicting_path = job.local_path;
            return true;
        }
    }
    return false;
}

bool SyncJobRegistry::cloudPathsConflict(const std::string& path1, const std::string& path2) const {
    // Case-insensitive comparison for cloud paths (Proton Drive is case-insensitive)
    std::string p1 = path1;
    std::string p2 = path2;
    std::transform(p1.begin(), p1.end(), p1.begin(), ::tolower);
    std::transform(p2.begin(), p2.end(), p2.begin(), ::tolower);
    
    // Normalize paths
    while (!p1.empty() && p1.back() == '/') p1.pop_back();
    while (!p2.empty() && p2.back() == '/') p2.pop_back();
    
    return p1 == p2;
}

SyncJobRegistry::ConflictInfo SyncJobRegistry::checkForConflicts(
    const std::string& local_path,
    const std::string& remote_path) const {
    
    ConflictInfo info;
    info.type = ConflictType::NONE;
    
    std::string this_device_id = DeviceIdentity::getInstance().getDeviceId();
    
    // Normalize local path for comparison
    fs::path normalized_local = fs::path(local_path).lexically_normal();
    
    for (const auto& job : jobs_) {
        fs::path job_local = fs::path(job.local_path).lexically_normal();
        
        // Check for EXACT DUPLICATE: same local path AND same remote path on THIS device
        // This prevents creating duplicate jobs
        if (job_local == normalized_local && cloudPathsConflict(job.remote_path, remote_path)) {
            info.type = ConflictType::SAME_LOCAL_DIFFERENT_REMOTE;  // Reuse type for duplicate detection
            info.conflicting_job_id = job.job_id;
            info.message = "This folder is already synced to '" + job.remote_path + "'.";
            return info;
        }
        
        // Check for same local path syncing to DIFFERENT remote
        if (job_local == normalized_local && !cloudPathsConflict(job.remote_path, remote_path)) {
            info.type = ConflictType::SAME_LOCAL_DIFFERENT_REMOTE;
            info.conflicting_job_id = job.job_id;
            info.message = "Local folder '" + local_path + "' is already synced to '" + job.remote_path + 
                          "'. Cannot sync to multiple remote folders.";
            return info;
        }
        
        // Check for same remote path (case-insensitive for cloud) from different device (exclusive mode)
        if (cloudPathsConflict(job.remote_path, remote_path) && 
            job.origin_device_id != this_device_id &&
            job.sync_mode == "exclusive") {
            info.type = ConflictType::SAME_REMOTE_EXCLUSIVE;
            info.conflicting_job_id = job.job_id;
            info.conflicting_device_id = job.origin_device_id;
            info.conflicting_device_name = job.origin_device_name;
            info.message = "Remote folder '" + remote_path + "' is already synced exclusively by device '" + 
                          job.origin_device_name + "'. Enable shared sync or use a different remote folder.";
            return info;
        }
        
        // Check if job exists but not shared with this device (case-insensitive for cloud paths)
        if (cloudPathsConflict(job.remote_path, remote_path) && 
            job.origin_device_id != this_device_id &&
            job.sync_mode == "shared" &&
            !job.isAuthorizedDevice(this_device_id)) {
            info.type = ConflictType::DIFFERENT_DEVICE_UNSHARED;
            info.conflicting_job_id = job.job_id;
            info.conflicting_device_id = job.origin_device_id;
            info.conflicting_device_name = job.origin_device_name;
            info.message = "Remote folder '" + remote_path + "' is shared by device '" + 
                          job.origin_device_name + "'. Request access to join the shared sync.";
            return info;
        }
    }
    
    return info;
}

// ========================
// Local Filesystem Safety Checks
// ========================

std::string SyncJobRegistry::getDefaultSyncLocation() {
    const char* home = getenv("HOME");
    if (home) {
        return std::string(home) + "/ProtonDrive";
    }
    return "/tmp/ProtonDrive";
}

bool SyncJobRegistry::ensureDefaultSyncLocation() {
    std::string default_path = getDefaultSyncLocation();
    
    // Check if it exists
    if (AppWindowHelpers::safe_exists(default_path)) {
        // Verify it's a directory
        if (!AppWindowHelpers::safe_is_directory(default_path)) {
            Logger::error("[ProtonDrive] Default location exists but is not a directory: " + default_path);
            return false;
        }
        Logger::info("[ProtonDrive] Default sync location exists: " + default_path);
        return true;
    }
    
    // Try to create it
    std::error_code ec_create;
    std::filesystem::create_directories(default_path, ec_create);
    if (ec_create) {
        Logger::error("[ProtonDrive] Failed to create default sync location: " + ec_create.message());
        return false;
    }
    Logger::info("[ProtonDrive] Created default sync location: " + default_path);
    return true;
}

SyncJobRegistry::LocalPathStatus SyncJobRegistry::checkLocalPath(const std::string& local_path, int64_t required_bytes) {
    LocalPathStatus status;
    status.required_bytes = required_bytes;
    
    // Check if path exists
    fs::path path(local_path);
    status.exists = AppWindowHelpers::safe_exists(path.string());
    
    // Check if parent directory exists and is writable
    fs::path parent = path.parent_path();
    if (!AppWindowHelpers::safe_exists(parent.string())) {
        status.is_writable = false;
        status.is_valid_mount = false;
        status.error_message = "Parent directory does not exist: " + parent.string();
        return status;
    }
    
    // Check write permission using access()
    if (status.exists) {
        status.is_writable = (access(local_path.c_str(), W_OK) == 0);
    } else {
        // Check if we can write to parent
        status.is_writable = (access(parent.string().c_str(), W_OK) == 0);
    }
    
    if (!status.is_writable) {
        status.error_message = "Cannot write to path: permission denied";
    }
    
    // Get mount point and filesystem info using statfs
    struct statfs fs_stat;
    std::string check_path = status.exists ? local_path : parent.string();
    
    if (statfs(check_path.c_str(), &fs_stat) == 0) {
        status.is_valid_mount = true;
        status.available_bytes = static_cast<int64_t>(fs_stat.f_bavail) * fs_stat.f_bsize;
        
        // Determine filesystem type from f_type
        switch (fs_stat.f_type) {
            case 0x4d44:  // MSDOS_SUPER_MAGIC (FAT32)
                status.filesystem_type = "vfat";
                break;
            case 0xEF53:  // EXT4_SUPER_MAGIC
                status.filesystem_type = "ext4";
                break;
            case 0x58465342:  // XFS_SUPER_MAGIC
                status.filesystem_type = "xfs";
                break;
            case 0x9123683E:  // BTRFS_SUPER_MAGIC
                status.filesystem_type = "btrfs";
                break;
            case 0x01021994:  // TMPFS_MAGIC
                status.filesystem_type = "tmpfs";
                break;
            case 0x6969:  // NFS_SUPER_MAGIC
                status.filesystem_type = "nfs";
                break;
            case 0xFF534D42:  // CIFS_MAGIC
                status.filesystem_type = "cifs";
                break;
            case 0x65735546:  // FUSE_SUPER_MAGIC
                status.filesystem_type = "fuse";
                break;
            default:
                status.filesystem_type = "unknown";
        }
        
        // Check for filesystem compatibility issues
        if (status.filesystem_type == "vfat") {
            // FAT32 has 4GB file size limit
            if (required_bytes > 4LL * 1024 * 1024 * 1024) {
                status.is_compatible_filesystem = false;
                status.error_message = "FAT32 filesystem has 4GB file size limit. Cannot sync large files.";
            }
        }
        
        // Check if read-only
        if (fs_stat.f_flags & MS_RDONLY) {
            status.is_compatible_filesystem = false;
            status.is_writable = false;
            status.error_message = "Filesystem is mounted read-only";
        }
        
        // Check disk space
        if (required_bytes > 0 && status.available_bytes < required_bytes) {
            status.has_sufficient_space = false;
            double needed_gb = required_bytes / (1024.0 * 1024.0 * 1024.0);
            double avail_gb = status.available_bytes / (1024.0 * 1024.0 * 1024.0);
            status.error_message = "Insufficient disk space. Need " + 
                                  std::to_string(needed_gb).substr(0, 4) + " GB, have " +
                                  std::to_string(avail_gb).substr(0, 4) + " GB available.";
        }
        
        // Warn if disk is more than 90% full
        int64_t total_bytes = static_cast<int64_t>(fs_stat.f_blocks) * fs_stat.f_bsize;
        if (total_bytes > 0) {
            double usage = 1.0 - (static_cast<double>(status.available_bytes) / total_bytes);
            if (usage > 0.9 && status.error_message.empty()) {
                status.error_message = "Warning: Disk is more than 90% full";
            }
        }
    } else {
        status.is_valid_mount = false;
        status.is_compatible_filesystem = false;
        status.error_message = "Cannot access filesystem: " + std::string(strerror(errno));
    }
    
    // Try to find mount point by walking up the path
    fs::path mp = check_path;
    while (!mp.empty() && mp != mp.parent_path()) {
        struct stat st1, st2;
        if (stat(mp.string().c_str(), &st1) == 0 && 
            stat(mp.parent_path().string().c_str(), &st2) == 0) {
            if (st1.st_dev != st2.st_dev) {
                status.mount_point = mp.string();
                break;
            }
        }
        mp = mp.parent_path();
    }
    if (status.mount_point.empty()) {
        status.mount_point = "/";
    }
    
    return status;
}

SyncJobRegistry::LocalFolderMeta SyncJobRegistry::getLocalFolderMetadata(const std::string& local_path) {
    LocalFolderMeta meta;
    meta.is_valid = false;
    
    fs::path meta_path = fs::path(local_path) / ".proton-sync-local.json";
    
    if (!AppWindowHelpers::safe_exists(meta_path.string())) {
        return meta;
    }
    
    std::ifstream file(meta_path.string());
    if (!file.is_open()) {
        return meta;
    }
    
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();
    
    // Parse simple JSON
    try {
        size_t pos = content.find("\"device_id\"");
        if (pos != std::string::npos) {
            size_t start = content.find(": \"", pos) + 3;
            size_t end = content.find("\"", start);
            if (start < end) {
                meta.device_id = content.substr(start, end - start);
            }
        }
        
        pos = content.find("\"device_name\"");
        if (pos != std::string::npos) {
            size_t start = content.find(": \"", pos) + 3;
            size_t end = content.find("\"", start);
            if (start < end) {
                meta.device_name = content.substr(start, end - start);
            }
        }
        
        pos = content.find("\"remote_path\"");
        if (pos != std::string::npos) {
            size_t start = content.find(": \"", pos) + 3;
            size_t end = content.find("\"", start);
            if (start < end) {
                meta.remote_path = content.substr(start, end - start);
            }
        }
        
        pos = content.find("\"created_at\"");
        if (pos != std::string::npos) {
            size_t start = content.find(": \"", pos) + 3;
            size_t end = content.find("\"", start);
            if (start < end) {
                meta.created_at = content.substr(start, end - start);
            }
        }
        
        if (!meta.device_id.empty()) {
            meta.is_valid = true;
        }
    } catch (...) {
        Logger::error("[SyncJobRegistry] Failed to parse local folder metadata");
    }
    
    return meta;
}

bool SyncJobRegistry::writeLocalFolderMetadata(const std::string& local_path, const LocalFolderMeta& meta) {
    // Ensure directory exists
    if (!AppWindowHelpers::safe_exists(local_path)) {
        std::error_code ec_mkd;
        fs::create_directories(local_path, ec_mkd);
        if (ec_mkd) {
            Logger::error("[SyncJobRegistry] Failed to create directory: " + ec_mkd.message());
            return false;
        }
    }
    
    fs::path meta_path = fs::path(local_path) / ".proton-sync-local.json";
    
    std::ofstream file(meta_path.string());
    if (!file.is_open()) {
        Logger::error("[SyncJobRegistry] Failed to create local metadata file");
        return false;
    }
    
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    char time_buf[64];
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&time_t_now));
    
    file << "{\n";
    file << "  \"device_id\": \"" << meta.device_id << "\",\n";
    file << "  \"device_name\": \"" << meta.device_name << "\",\n";
    file << "  \"remote_path\": \"" << meta.remote_path << "\",\n";
    file << "  \"created_at\": \"" << time_buf << "\",\n";
    file << "  \"sync_version\": 1\n";
    file << "}\n";
    file.close();
    
    Logger::info("[SyncJobRegistry] Wrote local folder metadata to " + meta_path.string());
    return true;
}

SyncJobRegistry::LocalConflictInfo SyncJobRegistry::checkLocalPathConflicts(const std::string& local_path) {
    LocalConflictInfo info;
    info.type = LocalConflictType::NONE;
    
    // First check filesystem safety
    info.path_status = checkLocalPath(local_path);
    
    // Check for unmountable/inaccessible path
    if (!info.path_status.is_valid_mount) {
        info.type = LocalConflictType::UNMOUNTABLE_PATH;
        info.message = "The selected path is not accessible: " + info.path_status.error_message;
        return info;
    }
    
    // Check for incompatible filesystem
    if (!info.path_status.is_compatible_filesystem) {
        info.type = LocalConflictType::INVALID_FILESYSTEM;
        info.message = "Incompatible filesystem: " + info.path_status.error_message;
        return info;
    }
    
    // Check for write permission
    if (!info.path_status.is_writable && info.path_status.exists) {
        info.type = LocalConflictType::PARENT_NOT_WRITABLE;
        info.message = "Cannot write to this location. Please check permissions.";
        return info;
    }
    
    // Check if path exists
    if (!info.path_status.exists) {
        // Path doesn't exist - no conflict
        return info;
    }
    
    // Path exists - check what it is
    fs::path path(local_path);
    
    if (fs::is_regular_file(path)) {
        info.type = LocalConflictType::FILE_EXISTS;
        info.existing_path = local_path;
        info.message = "A file with this name already exists at this location.";
        return info;
    }
    
    if (AppWindowHelpers::safe_is_directory(path.string())) {
        // Check for our metadata file
        LocalFolderMeta meta = getLocalFolderMetadata(local_path);
        std::string this_device_id = DeviceIdentity::getInstance().getDeviceId();
        
        if (meta.is_valid) {
            if (meta.device_id == this_device_id) {
                info.type = LocalConflictType::FOLDER_EXISTS_SAME_DEVICE;
                info.existing_device_id = meta.device_id;
                info.existing_device_name = meta.device_name;
                info.existing_path = local_path;
                info.message = "This folder was previously synced from this device.\n"
                              "Would you like to resume syncing to this folder?";
            } else {
                info.type = LocalConflictType::FOLDER_EXISTS_DIFFERENT_DEVICE;
                info.existing_device_id = meta.device_id;
                info.existing_device_name = meta.device_name;
                info.existing_path = local_path;
                info.message = "This folder was synced from a different device: '" + 
                              meta.device_name + "'.\n\nChoose an option:\n" +
                              "• Merge: Combine with existing files\n" +
                              "• Rename: Create a new folder with device name\n" +
                              "• Overwrite: Replace existing folder contents";
            }
        } else {
            // Folder exists but no metadata - user-created folder
            info.type = LocalConflictType::FOLDER_EXISTS_UNKNOWN;
            info.existing_path = local_path;
            
            // Check if folder is empty
            bool is_empty = fs::is_empty(path);
            if (is_empty) {
                info.message = "An empty folder with this name already exists.\n"
                              "Would you like to sync into this folder?";
            } else {
                info.message = "A folder with this name already exists and contains files.\n\n"
                              "Choose an option:\n" +
                              std::string("• Merge: Combine cloud files with existing local files\n") +
                              "• Rename: Create a new folder (add device name suffix)\n" +
                              "• Overwrite: Replace local folder contents with cloud files";
            }
        }
    }
    
    return info;
}
void SyncJobRegistry::enableSharedSync(const std::string& job_id) {
    SyncJobMetadata* job = getJob(job_id);
    if (job) {
        job->sync_mode = "shared";
        saveJobs();
        Logger::info("[SyncJobRegistry] Enabled shared sync for job " + job_id);
    }
}

void SyncJobRegistry::joinSharedSync(const std::string& job_id) {
    SyncJobMetadata* job = getJob(job_id);
    if (job && job->sync_mode == "shared") {
        DeviceInfo this_device;
        this_device.device_id = DeviceIdentity::getInstance().getDeviceId();
        this_device.device_name = DeviceIdentity::getInstance().getDeviceName();
        this_device.last_sync_time = std::time(nullptr);
        job->addSharedDevice(this_device);
        saveJobs();
        Logger::info("[SyncJobRegistry] Joined shared sync for job " + job_id);
    }
}

void SyncJobRegistry::leaveSharedSync(const std::string& job_id) {
    SyncJobMetadata* job = getJob(job_id);
    if (job) {
        std::string this_device_id = DeviceIdentity::getInstance().getDeviceId();
        job->removeSharedDevice(this_device_id);
        saveJobs();
        Logger::info("[SyncJobRegistry] Left shared sync for job " + job_id);
    }
}

void SyncJobRegistry::recordSyncStart(const std::string& job_id) {
    SyncJobMetadata* job = getJob(job_id);
    if (job) {
        job->last_sync_device_id = DeviceIdentity::getInstance().getDeviceId();
        job->last_sync_status = "running";
        saveJobs();
    }
}

void SyncJobRegistry::recordSyncComplete(const std::string& job_id, bool success) {
    SyncJobMetadata* job = getJob(job_id);
    if (job) {
        job->last_sync_time = std::time(nullptr);
        job->last_sync_status = success ? "success" : "failed";
        saveJobs();
    }
}

// ============ Cloud Folder Metadata Functions ============

// Shell-escape a string for safe use in shell commands (single-quote wrapping)
static std::string meta_shell_escape(const std::string& arg) {
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

// Helper to execute shell command and get output
static std::string exec_cmd(const char* cmd) {
    std::array<char, 256> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        return "";
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

bool SyncJobRegistry::checkCloudFolderExists(const std::string& remote_path) {
    // Use rclone lsjson to check if folder exists
    // remote_path format: "proton:/FolderName"
    std::string cmd = "rclone lsjson --dirs-only " + meta_shell_escape(remote_path) + " 2>/dev/null";
    std::string output = exec_cmd(cmd.c_str());
    
    // If the folder exists, lsjson returns valid JSON (even empty [])
    // If it doesn't exist or there's an error, it returns an error
    if (output.empty() || output.find("error") != std::string::npos) {
        // Check if the path itself is a valid directory
        cmd = "rclone lsd " + meta_shell_escape(remote_path) + " 2>/dev/null";
        output = exec_cmd(cmd.c_str());
        return !output.empty() && output.find("Failed") == std::string::npos;
    }
    
    return true;
}

SyncJobRegistry::CloudFolderMeta SyncJobRegistry::getCloudFolderMetadata(const std::string& remote_path) {
    CloudFolderMeta meta;
    meta.is_valid = false;
    
    // Check for .proton-sync-meta.json in the cloud folder
    std::string meta_path = remote_path;
    if (!meta_path.empty() && meta_path.back() != '/') {
        meta_path += "/";
    }
    meta_path += ".proton-sync-meta.json";
    
    // Try to read the metadata file using rclone cat
    std::string cmd = "rclone cat " + meta_shell_escape(meta_path) + " 2>/dev/null";
    std::string content = exec_cmd(cmd.c_str());
    
    if (content.empty()) {
        Logger::info("[SyncJobRegistry] No metadata file found at " + meta_path);
        return meta;
    }
    
    // Parse simple JSON
    try {
        // Extract device_id
        size_t pos = content.find("\"device_id\"");
        if (pos != std::string::npos) {
            size_t start = content.find(": \"", pos) + 3;
            size_t end = content.find("\"", start);
            if (start < end) {
                meta.device_id = content.substr(start, end - start);
            }
        }
        
        // Extract device_name
        pos = content.find("\"device_name\"");
        if (pos != std::string::npos) {
            size_t start = content.find(": \"", pos) + 3;
            size_t end = content.find("\"", start);
            if (start < end) {
                meta.device_name = content.substr(start, end - start);
            }
        }
        
        // Extract folder_name
        pos = content.find("\"folder_name\"");
        if (pos != std::string::npos) {
            size_t start = content.find(": \"", pos) + 3;
            size_t end = content.find("\"", start);
            if (start < end) {
                meta.folder_name = content.substr(start, end - start);
            }
        }
        
        // Extract created_at
        pos = content.find("\"created_at\"");
        if (pos != std::string::npos) {
            size_t start = content.find(": \"", pos) + 3;
            size_t end = content.find("\"", start);
            if (start < end) {
                meta.created_at = content.substr(start, end - start);
            }
        }
        
        if (!meta.device_id.empty()) {
            meta.is_valid = true;
            Logger::info("[SyncJobRegistry] Found cloud folder metadata from device: " + 
                        meta.device_name + " (" + meta.device_id + ")");
        }
    } catch (...) {
        Logger::error("[SyncJobRegistry] Failed to parse cloud folder metadata");
    }
    
    return meta;
}

bool SyncJobRegistry::writeCloudFolderMetadata(const std::string& remote_path, const CloudFolderMeta& meta) {
    // Create temp file with metadata
    std::string temp_path = "/tmp/proton-sync-meta-" + std::to_string(getpid()) + ".json";
    
    std::ofstream file(temp_path);
    if (!file.is_open()) {
        Logger::error("[SyncJobRegistry] Failed to create temp metadata file");
        return false;
    }
    
    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    char time_buf[64];
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&time_t_now));
    
    file << "{\n";
    file << "  \"device_id\": \"" << meta.device_id << "\",\n";
    file << "  \"device_name\": \"" << meta.device_name << "\",\n";
    file << "  \"folder_name\": \"" << meta.folder_name << "\",\n";
    file << "  \"created_at\": \"" << time_buf << "\",\n";
    file << "  \"sync_version\": 1\n";
    file << "}\n";
    file.close();
    
    // Upload to cloud using rclone
    std::string dest_path = remote_path;
    if (!dest_path.empty() && dest_path.back() != '/') {
        dest_path += "/";
    }
    dest_path += ".proton-sync-meta.json";
    
    // First, try to delete any existing metadata file (Proton Drive doesn't allow overwriting)
    std::string delete_cmd = "rclone deletefile " + meta_shell_escape(dest_path) + " 2>/dev/null";
    [[maybe_unused]] int del_result = system(delete_cmd.c_str());
    
    // Small delay to let Proton Drive process the delete
    usleep(500000);  // 500ms
    
    std::string cmd = "rclone copyto " + meta_shell_escape(temp_path) + " " + meta_shell_escape(dest_path) + " 2>&1";
    std::string output = exec_cmd(cmd.c_str());
    
    // Clean up temp file
    fs::remove(temp_path);
    
    if (output.find("Failed") != std::string::npos || output.find("error") != std::string::npos) {
        Logger::error("[SyncJobRegistry] Failed to upload metadata: " + output);
        return false;
    }
    
    Logger::info("[SyncJobRegistry] Wrote cloud folder metadata to " + dest_path);
    return true;
}

SyncJobRegistry::ConflictInfo SyncJobRegistry::checkForCloudFolderConflicts(
    const std::string& local_path,
    const std::string& remote_path) const {
    
    // First check local job registry conflicts
    ConflictInfo info = checkForConflicts(local_path, remote_path);
    if (info.type != ConflictType::NONE) {
        return info;
    }
    
    // Now check if cloud folder exists
    std::string rclone_path = remote_path;
    if (rclone_path.rfind("proton:", 0) != 0) {
        rclone_path = "proton:" + rclone_path;
    }
    if (!checkCloudFolderExists(rclone_path)) {
        // No cloud folder exists - no conflict
        info.type = ConflictType::NONE;
        return info;
    }
    
    // Cloud folder exists - check its metadata
    CloudFolderMeta cloud_meta = getCloudFolderMetadata(rclone_path);
    
    std::string this_device_id = DeviceIdentity::getInstance().getDeviceId();
    std::string this_device_name = DeviceIdentity::getInstance().getDeviceName();
    
    if (cloud_meta.is_valid) {
        // Metadata exists - check if from same device
        if (cloud_meta.device_id == this_device_id) {
            info.type = ConflictType::CLOUD_FOLDER_SAME_DEVICE;
            info.conflicting_device_id = cloud_meta.device_id;
            info.conflicting_device_name = cloud_meta.device_name;
            info.existing_remote_path = remote_path;
            info.message = "A folder named '" + fs::path(remote_path).filename().string() + 
                          "' already exists in the cloud from this device (" + 
                          cloud_meta.device_name + "). Would you like to merge with it?";
        } else {
            info.type = ConflictType::CLOUD_FOLDER_DIFFERENT_DEVICE;
            info.conflicting_device_id = cloud_meta.device_id;
            info.conflicting_device_name = cloud_meta.device_name;
            info.existing_remote_path = remote_path;
            info.message = "A folder named '" + fs::path(remote_path).filename().string() + 
                          "' already exists in the cloud from a different device: '" + 
                          cloud_meta.device_name + "'.\n\nChoose an option:\n" +
                          "• Merge: Sync with the existing folder\n" +
                          "• Create New: Create '" + fs::path(remote_path).filename().string() + 
                          "-" + this_device_name + "'\n" +
                          "• Overwrite: Replace the existing folder contents";
        }
    } else {
        // Cloud folder exists but no metadata - treat as different device (legacy folder)
        info.type = ConflictType::CLOUD_FOLDER_DIFFERENT_DEVICE;
        info.conflicting_device_id = "";
        info.conflicting_device_name = "Unknown (legacy folder)";
        info.existing_remote_path = remote_path;
        info.message = "A folder named '" + fs::path(remote_path).filename().string() + 
                      "' already exists in the cloud (created before device tracking).\n\n" +
                      "Choose an option:\n" +
                      "• Merge: Sync with the existing folder\n" +
                      "• Create New: Create '" + fs::path(remote_path).filename().string() + 
                      "-" + this_device_name + "'";
    }
    
    return info;
}

// ========== Multi-Device Config Sync Implementation ==========

// Helper function to execute rclone command for config sync
static std::string exec_rclone_config_cmd(const std::string& args) {
    // Use the centralized rclone path resolution for consistency
    std::string rclone_path = AppWindowHelpers::get_rclone_path();
    
    std::string cmd = rclone_path + " " + args + " 2>&1";
    Logger::debug("[ConfigSync] Executing: " + rclone_path + " " + args);
    std::array<char, 4096> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        Logger::error("[ConfigSync] Failed to execute rclone command");
        return "";
    }
    
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

bool SyncJobRegistry::exportConfigToCloud() {
    Logger::info("[ConfigSync] Exporting config to cloud...");
    
    auto& device_id = DeviceIdentity::getInstance();
    std::string device_id_str = device_id.getDeviceId();
    std::string device_name = device_id.getDeviceName();
    
    // Create config JSON
    std::stringstream ss;
    ss << "{\n";
    ss << "  \"device_id\": \"" << device_id_str << "\",\n";
    ss << "  \"device_name\": \"" << device_name << "\",\n";
    ss << "  \"last_updated\": " << std::time(nullptr) << ",\n";
    ss << "  \"jobs\": [\n";
    
    for (size_t i = 0; i < jobs_.size(); i++) {
        ss << jobs_[i].toJson();
        if (i < jobs_.size() - 1) ss << ",";
        ss << "\n";
    }
    
    ss << "  ]\n";
    ss << "}\n";
    
    // Write to temp file
    const char* home = std::getenv("HOME");
    if (!home) return false;
    
    std::string temp_file = std::string(home) + "/.cache/proton-drive/device_config_" + device_id_str + ".json";
    std::ofstream f(temp_file);
    if (!f) {
        Logger::error("[ConfigSync] Failed to write temp config file");
        return false;
    }
    f << ss.str();
    f.close();
    
    // Upload to cloud
    std::string cloud_path = "proton:/.proton-sync-config/";
    
    // Ensure the folder exists
    exec_rclone_config_cmd("mkdir " + meta_shell_escape(cloud_path));
    
    // Upload the config
    exec_rclone_config_cmd("copyto " + meta_shell_escape(temp_file) + " " + meta_shell_escape(cloud_path + "device_" + device_id_str + ".json"));
    
    // Clean up temp file
    std::error_code ec_rm;
    std::filesystem::remove(temp_file, ec_rm);
    
    Logger::info("[ConfigSync] Config exported to cloud successfully");
    return true;
}

std::vector<SyncJobMetadata> SyncJobRegistry::importConfigFromCloud() {
    std::vector<SyncJobMetadata> imported_jobs;
    
    Logger::info("[ConfigSync] Importing configs from cloud...");
    
    auto configs = getCloudDeviceConfigs();
    std::string this_device_id = DeviceIdentity::getInstance().getDeviceId();
    
    for (const auto& config : configs) {
        // Skip our own config
        if (config.device_id == this_device_id) continue;
        
        Logger::info("[ConfigSync] Found config from device: " + config.device_name);
        
        for (const auto& remote_job : config.jobs) {
            // Check if we already have a job for the same remote path
            bool already_exists = false;
            for (const auto& local_job : jobs_) {
                if (local_job.remote_path == remote_job.remote_path) {
                    already_exists = true;
                    break;
                }
            }
            
            if (!already_exists) {
                // This is a new job from another device that we could sync
                imported_jobs.push_back(remote_job);
                Logger::info("[ConfigSync] Discovered shared folder: " + remote_job.remote_path + 
                           " from " + config.device_name);
            }
        }
    }
    
    return imported_jobs;
}

std::vector<SyncJobRegistry::CloudDeviceConfig> SyncJobRegistry::getCloudDeviceConfigs() {
    std::vector<CloudDeviceConfig> configs;
    
    Logger::info("[ConfigSync] Fetching cloud device configs...");
    
    // List config files from cloud
    std::string output = exec_rclone_config_cmd("lsjson " + meta_shell_escape("proton:/.proton-sync-config/"));
    
    Logger::debug("[ConfigSync] lsjson output: " + (output.empty() ? "(empty)" : output.substr(0, 500)));
    
    if (output.empty()) {
        Logger::warn("[ConfigSync] lsjson returned empty output - cloud config folder may not exist");
        return configs;
    }
    
    // Check for error messages (stderr mixed in with 2>&1)
    if (output.find("directory not found") != std::string::npos ||
        output.find("ERROR") != std::string::npos ||
        output.find("Failed") != std::string::npos) {
        Logger::warn("[ConfigSync] Cloud config query returned error: " + output.substr(0, 200));
        return configs;
    }
    
    // Parse the JSON to find device config files
    const char* home = std::getenv("HOME");
    if (!home) return configs;
    
    std::string cache_dir = std::string(home) + "/.cache/proton-drive/config-sync";
    std::error_code ec_mkdir;
    std::filesystem::create_directories(cache_dir, ec_mkdir);
    if (ec_mkdir) {
        Logger::error("[ConfigSync] Failed to create cache dir: " + ec_mkdir.message());
        return configs;
    }
    
    // Download all config files
    std::string copy_output = exec_rclone_config_cmd("copy " + meta_shell_escape("proton:/.proton-sync-config/") + " " + meta_shell_escape(cache_dir));
    if (!copy_output.empty()) {
        Logger::debug("[ConfigSync] copy command output: " + copy_output.substr(0, 300));
    }
    
    // Count downloaded files for diagnostics
    int file_count = 0;
    std::error_code ec_count;
    for (const auto& entry : std::filesystem::directory_iterator(cache_dir, ec_count)) {
        if (!ec_count) {
            file_count++;
            Logger::debug("[ConfigSync] Found cached file: " + entry.path().filename().string());
        }
    }
    Logger::info("[ConfigSync] Downloaded " + std::to_string(file_count) + " file(s) from cloud config");
    
    if (file_count == 0) {
        Logger::warn("[ConfigSync] No config files downloaded from cloud");
        // Clean up empty cache dir
        std::error_code ec;
        std::filesystem::remove_all(cache_dir, ec);
        return configs;
    }
    
    // Parse each config file
    std::error_code ec_iter;
    for (const auto& entry : std::filesystem::directory_iterator(cache_dir, ec_iter)) {
        if (ec_iter) {
            Logger::warn("[ConfigSync] Error iterating config dir: " + ec_iter.message());
            break;
        }
        if (entry.path().extension() != ".json") continue;
        if (entry.path().filename().string().find("device_") != 0) continue;
        
        Logger::debug("[ConfigSync] Parsing config file: " + entry.path().filename().string());
        
        try {
            std::ifstream cfg_file(entry.path());
            std::string content((std::istreambuf_iterator<char>(cfg_file)),
                               std::istreambuf_iterator<char>());
            
            if (content.empty()) {
                Logger::warn("[ConfigSync] Empty config file: " + entry.path().string());
                continue;
            }
            
            Logger::debug("[ConfigSync] Config content (" + std::to_string(content.size()) + " bytes): " + 
                         content.substr(0, 200));
            
            CloudDeviceConfig config;
            
            // Simple JSON parsing for device config
            auto extract_str = [&content](const std::string& key) -> std::string {
                std::string needle = "\"" + key + "\":";
                size_t pos = content.find(needle);
                if (pos == std::string::npos) return "";
                pos = content.find("\"", pos + needle.length());
                if (pos == std::string::npos) return "";
                size_t end_pos = content.find("\"", pos + 1);
                if (end_pos == std::string::npos) return "";
                return content.substr(pos + 1, end_pos - pos - 1);
            };
            
            auto extract_num = [&content](const std::string& key) -> long {
                std::string needle = "\"" + key + "\":";
                size_t pos = content.find(needle);
                if (pos == std::string::npos) return 0;
                pos += needle.length();
                while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t')) pos++;
                size_t end_pos = pos;
                while (end_pos < content.size() && (std::isdigit(content[end_pos]) || content[end_pos] == '-')) end_pos++;
                try {
                    return std::stol(content.substr(pos, end_pos - pos));
                } catch (...) {
                    return 0;
                }
            };
            
            config.device_id = extract_str("device_id");
            config.device_name = extract_str("device_name");
            config.last_updated = static_cast<std::time_t>(extract_num("last_updated"));
            
            Logger::debug("[ConfigSync] Parsed device: id=" + config.device_id + 
                         ", name=" + config.device_name);
            
            // Parse jobs array (extract remote paths and sync types)
            size_t jobs_pos = content.find("\"jobs\":");
            if (jobs_pos != std::string::npos) {
                size_t job_start = content.find("{", jobs_pos);
                while (job_start != std::string::npos) {
                    size_t job_end = content.find("}", job_start);
                    if (job_end == std::string::npos) break;
                    
                    std::string job_content = content.substr(job_start, job_end - job_start + 1);
                    
                    SyncJobMetadata job;
                    job.job_id = "";  // Will generate new ID if user decides to sync
                    
                    // Extract remote_path
                    size_t rp_pos = job_content.find("\"remote_path\":");
                    if (rp_pos != std::string::npos) {
                        size_t rp_start = job_content.find("\"", rp_pos + 14);
                        size_t rp_end = job_content.find("\"", rp_start + 1);
                        if (rp_start != std::string::npos && rp_end != std::string::npos) {
                            job.remote_path = job_content.substr(rp_start + 1, rp_end - rp_start - 1);
                        }
                    }
                    
                    // Extract sync_type
                    size_t st_pos = job_content.find("\"sync_type\":");
                    if (st_pos != std::string::npos) {
                        size_t st_start = job_content.find("\"", st_pos + 12);
                        size_t st_end = job_content.find("\"", st_start + 1);
                        if (st_start != std::string::npos && st_end != std::string::npos) {
                            job.sync_type = job_content.substr(st_start + 1, st_end - st_start - 1);
                        }
                    }
                    
                    if (!job.remote_path.empty()) {
                        job.origin_device_id = config.device_id;
                        job.origin_device_name = config.device_name;
                        config.jobs.push_back(job);
                    }
                    
                    job_start = content.find("{", job_end);
                    // Make sure we don't go past the jobs array
                    size_t array_end = content.find("]", jobs_pos);
                    if (array_end != std::string::npos && job_start > array_end) break;
                }
            }
            
            if (!config.device_id.empty()) {
                configs.push_back(config);
                Logger::info("[ConfigSync] Loaded config from device: " + config.device_name + 
                             " with " + std::to_string(config.jobs.size()) + " jobs");
            } else {
                Logger::warn("[ConfigSync] Skipping config with empty device_id from: " + 
                            entry.path().filename().string());
            }
        } catch (const std::exception& e) {
            Logger::warn("[ConfigSync] Failed to parse config file: " + entry.path().string() + 
                        " - " + e.what());
        }
    }
    
    // Clean up cache
    std::error_code ec;
    std::filesystem::remove_all(cache_dir, ec);
    
    Logger::info("[ConfigSync] Total configs loaded: " + std::to_string(configs.size()));
    
    return configs;
}

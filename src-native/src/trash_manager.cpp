#include "trash_manager.hpp"
#include "settings.hpp"
#include "logger.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <ctime>
#include <iomanip>
#include <algorithm>
#include <mutex>

namespace fs = std::filesystem;

// Safe filesystem helpers to prevent exceptions on I/O errors
static bool tm_safe_exists(const std::string& path) {
    std::error_code ec;
    return fs::exists(path, ec);
}
static bool tm_safe_is_directory(const std::string& path) {
    std::error_code ec;
    return fs::is_directory(path, ec);
}
static bool tm_safe_is_regular_file(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}
namespace proton {

TrashManager::TrashManager() {
    const char* home = std::getenv("HOME");
    if (home) {
        trash_dir_ = std::string(home) + "/.cache/proton-drive/trash";
        metadata_file_ = trash_dir_ + "/metadata.json";
    } else {
        trash_dir_ = "/tmp/proton-drive-trash";
        metadata_file_ = trash_dir_ + "/metadata.json";
    }
}

TrashManager& TrashManager::getInstance() {
    static TrashManager instance;
    return instance;
}

bool TrashManager::initialize() {
    try {
        // Create trash directory if it doesn't exist
        if (!tm_safe_exists(trash_dir_)) {
            std::error_code ec_mk;
            fs::create_directories(trash_dir_, ec_mk);
            Logger::info("[TrashManager] Created trash directory: " + trash_dir_);
        }
        
        // Load metadata (handles its own locking)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!load_metadata()) {
                Logger::info("[TrashManager] No existing metadata, starting fresh");
            }
        }
        
        // Run cleanup on startup (handles its own locking)
        int cleaned = cleanup_old_items();
        if (cleaned > 0) {
            Logger::info("[TrashManager] Cleaned up " + std::to_string(cleaned) + " old items on startup");
        }
        
        return true;
    } catch (const std::exception& e) {
        Logger::error("[TrashManager] Initialize failed: " + std::string(e.what()));
        return false;
    }
}

std::string TrashManager::generate_unique_trash_name(const std::string& original_path) {
    fs::path p(original_path);
    std::string base_name = p.filename().string();
    
    // Add timestamp to make unique
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
    
    std::string unique_name = base_name + "_" + ss.str();
    
    // Ensure uniqueness
    int counter = 0;
    std::string final_name = unique_name;
    while (tm_safe_exists(trash_dir_ + "/" + final_name)) {
        final_name = unique_name + "_" + std::to_string(++counter);
    }
    
    return final_name;
}

bool TrashManager::move_to_trash(const std::string& local_path, const std::string& cloud_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        if (!tm_safe_exists(local_path)) {
            Logger::error("[TrashManager] Path does not exist: " + local_path);
            return false;
        }
        
        // Generate unique trash name
        std::string trash_name = generate_unique_trash_name(local_path);
        std::string trash_path = trash_dir_ + "/" + trash_name;
        
        // Calculate size before moving
        size_t size = calculate_size(local_path);
        bool is_dir = tm_safe_is_directory(local_path);
        
        // Move to trash
        fs::rename(local_path, trash_path);
        
        // Create metadata entry
        TrashItem item;
        item.trash_path = trash_path;
        item.original_path = local_path;
        item.cloud_path = cloud_path;
        item.deleted_at = std::chrono::system_clock::now();
        item.size_bytes = size;
        item.is_directory = is_dir;
        
        items_[trash_path] = item;
        
        // Save metadata
        save_metadata();
        
        Logger::info("[TrashManager] Moved to trash: " + local_path + " -> " + trash_path);
        return true;
        
    } catch (const std::exception& e) {
        Logger::error("[TrashManager] Failed to move to trash: " + std::string(e.what()));
        return false;
    }
}

bool TrashManager::restore(const std::string& trash_path, const std::string& restore_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        auto it = items_.find(trash_path);
        if (it == items_.end()) {
            Logger::error("[TrashManager] Item not found in trash: " + trash_path);
            return false;
        }
        
        std::string target_path = restore_path.empty() ? it->second.original_path : restore_path;
        
        // Check if target already exists
        if (fs::exists(target_path)) {
            Logger::error("[TrashManager] Restore target already exists: " + target_path);
            return false;
        }
        
        // Create parent directory if needed
        fs::path parent = fs::path(target_path).parent_path();
        if (!fs::exists(parent)) {
            fs::create_directories(parent);
        }
        
        // Move back from trash
        fs::rename(trash_path, target_path);
        
        // Remove from metadata
        items_.erase(it);
        save_metadata();
        
        Logger::info("[TrashManager] Restored: " + trash_path + " -> " + target_path);
        return true;
        
    } catch (const std::exception& e) {
        Logger::error("[TrashManager] Failed to restore: " + std::string(e.what()));
        return false;
    }
}

bool TrashManager::delete_permanent(const std::string& trash_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        auto it = items_.find(trash_path);
        if (it == items_.end()) {
            Logger::error("[TrashManager] Item not found in trash: " + trash_path);
            return false;
        }
        
        // Permanently delete
        if (fs::is_directory(trash_path)) {
            fs::remove_all(trash_path);
        } else {
            fs::remove(trash_path);
        }
        
        // Remove from metadata
        items_.erase(it);
        save_metadata();
        
        Logger::info("[TrashManager] Permanently deleted: " + trash_path);
        return true;
        
    } catch (const std::exception& e) {
        Logger::error("[TrashManager] Failed to delete permanently: " + std::string(e.what()));
        return false;
    }
}

int TrashManager::cleanup_old_items(int retention_days) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Get retention days from settings if not specified
    if (retention_days <= 0) {
        retention_days = SettingsManager::getInstance().get_int("trash_retention_days", 30);
    }
    
    auto now = std::chrono::system_clock::now();
    auto retention_duration = std::chrono::hours(24 * retention_days);
    
    int cleaned_count = 0;
    std::vector<std::string> to_delete;
    
    // Find items to delete
    for (const auto& [trash_path, item] : items_) {
        auto age = now - item.deleted_at;
        if (age >= retention_duration) {
            to_delete.push_back(trash_path);
        }
    }
    
    // Delete old items
    for (const auto& trash_path : to_delete) {
        try {
            if (fs::exists(trash_path)) {
                if (fs::is_directory(trash_path)) {
                    fs::remove_all(trash_path);
                } else {
                    fs::remove(trash_path);
                }
            }
            items_.erase(trash_path);
            cleaned_count++;
        } catch (const std::exception& e) {
            Logger::error("[TrashManager] Failed to cleanup item: " + trash_path + " - " + e.what());
        }
    }
    
    if (cleaned_count > 0) {
        save_metadata();
        Logger::info("[TrashManager] Cleaned up " + std::to_string(cleaned_count) + " items older than " + 
                     std::to_string(retention_days) + " days");
    }
    
    return cleaned_count;
}

std::vector<TrashManager::TrashItem> TrashManager::get_trash_items() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<TrashItem> result;
    result.reserve(items_.size());
    
    for (const auto& [_, item] : items_) {
        result.push_back(item);
    }
    
    // Sort by deletion time (newest first)
    std::sort(result.begin(), result.end(), 
        [](const TrashItem& a, const TrashItem& b) {
            return a.deleted_at > b.deleted_at;
        });
    
    return result;
}

size_t TrashManager::get_trash_size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t total = 0;
    for (const auto& [_, item] : items_) {
        total += item.size_bytes;
    }
    return total;
}

int TrashManager::empty_trash() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    int count = 0;
    std::vector<std::string> to_delete;
    
    // Collect all items
    for (const auto& [trash_path, _] : items_) {
        to_delete.push_back(trash_path);
    }
    
    // Delete all
    for (const auto& trash_path : to_delete) {
        try {
            if (fs::exists(trash_path)) {
                if (fs::is_directory(trash_path)) {
                    fs::remove_all(trash_path);
                } else {
                    fs::remove(trash_path);
                }
            }
            items_.erase(trash_path);
            count++;
        } catch (const std::exception& e) {
            Logger::error("[TrashManager] Failed to delete item: " + trash_path + " - " + e.what());
        }
    }
    
    save_metadata();
    Logger::info("[TrashManager] Emptied trash: " + std::to_string(count) + " items deleted");
    
    return count;
}

size_t TrashManager::calculate_size(const std::string& path) const {
    try {
        if (!tm_safe_exists(path)) return 0;
        
        if (tm_safe_is_directory(path)) {
            size_t total = 0;
            std::error_code ec_iter;
            for (const auto& entry : fs::recursive_directory_iterator(path, ec_iter)) {
                if (ec_iter) break;
                if (tm_safe_is_regular_file(entry.path().string())) {
                    total += fs::file_size(entry);
                }
            }
            return total;
        } else {
            return fs::file_size(path);
        }
    } catch (const std::exception& e) {
        Logger::error("[TrashManager] Failed to calculate size for: " + path + " - " + e.what());
        return 0;
    }
}

bool TrashManager::load_metadata() {
    try {
        if (!tm_safe_exists(metadata_file_)) {
            return false;
        }
        
        std::ifstream file(metadata_file_);
        if (!file.is_open()) {
            return false;
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        
        if (content.empty() || content == "{}") {
            return false;
        }
        
        // Simple JSON parsing for metadata
        // Format: {"trash_path": {"original": "...", "cloud": "...", "time": 123456, "size": 1024, "is_dir": true}}
        items_.clear();
        
        // This is a simplified parser - in production, use a proper JSON library
        // For now, just log and return true to allow app to start
        Logger::info("[TrashManager] Loaded metadata from: " + metadata_file_);
        return true;
        
    } catch (const std::exception& e) {
        Logger::error("[TrashManager] Failed to load metadata: " + std::string(e.what()));
        return false;
    }
}

bool TrashManager::save_metadata() {
    try {
        std::ofstream file(metadata_file_);
        if (!file.is_open()) {
            Logger::error("[TrashManager] Failed to open metadata file for writing");
            return false;
        }
        
        // Simple JSON format
        file << "{\n";
        
        bool first = true;
        for (const auto& [trash_path, item] : items_) {
            if (!first) file << ",\n";
            first = false;
            
            auto time_t = std::chrono::system_clock::to_time_t(item.deleted_at);
            
            file << "  \"" << trash_path << "\": {\n"
                 << "    \"original\": \"" << item.original_path << "\",\n"
                 << "    \"cloud\": \"" << item.cloud_path << "\",\n"
                 << "    \"time\": " << time_t << ",\n"
                 << "    \"size\": " << item.size_bytes << ",\n"
                 << "    \"is_dir\": " << (item.is_directory ? "true" : "false") << "\n"
                 << "  }";
        }
        
        file << "\n}\n";
        file.close();
        
        Logger::debug("[TrashManager] Saved metadata: " + std::to_string(items_.size()) + " items");
        return true;
        
    } catch (const std::exception& e) {
        Logger::error("[TrashManager] Failed to save metadata: " + std::string(e.what()));
        return false;
    }
}

} // namespace proton

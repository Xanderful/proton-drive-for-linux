#include "settings.hpp"
#include "logger.hpp"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <sys/stat.h>
#include <filesystem>

namespace proton {

SettingsManager::SettingsManager() {
    // Determine config path
    const char* home = std::getenv("HOME");
    if (home) {
        config_path_ = std::string(home) + "/.config/proton-drive-linux/settings.json";
    }
}

SettingsManager& SettingsManager::getInstance() {
    static SettingsManager instance;
    return instance;
}

std::string SettingsManager::get_config_dir() const {
    const char* home = std::getenv("HOME");
    if (home) {
        return std::string(home) + "/.config/proton-drive-linux";
    }
    return "";
}

void SettingsManager::ensure_defaults() {
    // Set default values if not present
    if (settings_.find("start_on_login") == settings_.end()) 
        settings_["start_on_login"] = "false";
    if (settings_.find("minimize_to_tray") == settings_.end()) 
        settings_["minimize_to_tray"] = "true";
    if (settings_.find("show_notifications") == settings_.end()) 
        settings_["show_notifications"] = "true";
    if (settings_.find("play_notification_sound") == settings_.end()) 
        settings_["play_notification_sound"] = "false";
    if (settings_.find("sync_interval_minutes") == settings_.end()) 
        settings_["sync_interval_minutes"] = "15";
    if (settings_.find("sync_on_startup") == settings_.end()) 
        settings_["sync_on_startup"] = "true";
    if (settings_.find("pause_sync_on_battery") == settings_.end()) 
        settings_["pause_sync_on_battery"] = "false";
    if (settings_.find("pause_sync_on_metered") == settings_.end()) 
        settings_["pause_sync_on_metered"] = "true";
    if (settings_.find("upload_limit") == settings_.end()) 
        settings_["upload_limit"] = "0";
    if (settings_.find("download_limit") == settings_.end()) 
        settings_["download_limit"] = "0";
    if (settings_.find("confirm_large_upload") == settings_.end()) 
        settings_["confirm_large_upload"] = "true";
    if (settings_.find("large_file_threshold") == settings_.end()) 
        settings_["large_file_threshold"] = "104857600";  // 100MB
    if (settings_.find("debug_logging") == settings_.end()) 
        settings_["debug_logging"] = "false";
    if (settings_.find("max_parallel_transfers") == settings_.end()) 
        settings_["max_parallel_transfers"] = "4";
    
    // Safety settings defaults
    if (settings_.find("max_delete_percent") == settings_.end()) 
        settings_["max_delete_percent"] = "50";
    if (settings_.find("conflict_resolve") == settings_.end()) 
        settings_["conflict_resolve"] = "both";
    if (settings_.find("conflict_behavior") == settings_.end()) 
        settings_["conflict_behavior"] = "wait";
    if (settings_.find("enable_versioning") == settings_.end()) 
        settings_["enable_versioning"] = "false";
    if (settings_.find("check_access_enabled") == settings_.end()) 
        settings_["check_access_enabled"] = "false";
    
    // Trash settings
    if (settings_.find("trash_retention_days") == settings_.end()) 
        settings_["trash_retention_days"] = "30";
        
    // Set download folder default
    if (settings_.find("download_folder") == settings_.end()) {
        const char* home = std::getenv("HOME");
        if (home) {
            settings_["download_folder"] = std::string(home) + "/Downloads";
        }
    }
}

bool SettingsManager::load() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (config_path_.empty()) {
        Logger::error("[Settings] No config path set");
        ensure_defaults();
        loaded_ = true;
        return false;
    }
    
    std::ifstream file(config_path_);
    if (!file.is_open()) {
        Logger::info("[Settings] No settings file found, using defaults");
        ensure_defaults();
        loaded_ = true;
        return false;
    }
    
    // Simple JSON parsing (key-value pairs)
    std::string line;
    std::string content;
    while (std::getline(file, line)) {
        content += line;
    }
    file.close();
    
    // Parse JSON-like format: {"key": "value", ...}
    // This is a simple parser; for complex needs, use a proper JSON library
    size_t pos = 0;
    while ((pos = content.find("\"", pos)) != std::string::npos) {
        size_t key_start = pos + 1;
        size_t key_end = content.find("\"", key_start);
        if (key_end == std::string::npos) break;
        
        std::string key = content.substr(key_start, key_end - key_start);
        
        // Find colon
        size_t colon = content.find(":", key_end);
        if (colon == std::string::npos) break;
        
        // Find value (skip whitespace and quotes)
        size_t val_start = content.find_first_not_of(" \t\n\"", colon + 1);
        if (val_start == std::string::npos) break;
        
        std::string value;
        if (content[val_start] == '"') {
            val_start++;
            size_t val_end = content.find("\"", val_start);
            if (val_end == std::string::npos) break;
            value = content.substr(val_start, val_end - val_start);
            pos = val_end + 1;
        } else {
            // Non-string value (number, boolean)
            size_t val_end = content.find_first_of(",}", val_start);
            if (val_end == std::string::npos) val_end = content.length();
            value = content.substr(val_start, val_end - val_start);
            // Trim
            while (!value.empty() && (value.back() == ' ' || value.back() == '\n' || value.back() == '\t')) {
                value.pop_back();
            }
            pos = val_end;
        }
        
        settings_[key] = value;
    }
    
    ensure_defaults();
    loaded_ = true;
    Logger::info("[Settings] Loaded " + std::to_string(settings_.size()) + " settings");
    return true;
}

bool SettingsManager::save() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (config_path_.empty()) {
        Logger::error("[Settings] No config path set");
        return false;
    }
    
    // Ensure directory exists
    std::string dir = get_config_dir();
    std::filesystem::create_directories(dir);
    
    std::ofstream file(config_path_);
    if (!file.is_open()) {
        Logger::error("[Settings] Failed to open settings file for writing");
        return false;
    }
    
    // Write as JSON
    file << "{\n";
    bool first = true;
    for (const auto& [key, value] : settings_) {
        if (!first) file << ",\n";
        first = false;
        
        // Check if value is numeric or boolean
        bool is_numeric = !value.empty() && 
            (std::isdigit(value[0]) || value[0] == '-');
        bool is_bool = (value == "true" || value == "false");
        
        if (is_numeric || is_bool) {
            file << "  \"" << key << "\": " << value;
        } else {
            file << "  \"" << key << "\": \"" << value << "\"";
        }
    }
    file << "\n}\n";
    file.close();
    
    Logger::info("[Settings] Saved " + std::to_string(settings_.size()) + " settings");
    return true;
}

void SettingsManager::notify_change(const std::string& key) {
    if (change_callback_) {
        change_callback_(key);
    }
}

void SettingsManager::set_change_callback(SettingsChangeCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    change_callback_ = std::move(callback);
}

// General settings
bool SettingsManager::get_start_on_login() const {
    return get_bool("start_on_login", false);
}

void SettingsManager::set_start_on_login(bool enabled) {
    set_bool("start_on_login", enabled);
    
    // Also update autostart desktop file
    std::string autostart_dir = std::string(std::getenv("HOME")) + "/.config/autostart";
    std::string autostart_file = autostart_dir + "/proton-drive.desktop";
    
    if (enabled) {
        std::filesystem::create_directories(autostart_dir);
        std::ofstream file(autostart_file);
        if (file.is_open()) {
            file << "[Desktop Entry]\n";
            file << "Type=Application\n";
            file << "Name=Proton Drive\n";
            file << "Exec=/usr/bin/proton-drive\n";
            file << "Icon=proton-drive\n";
            file << "X-GNOME-Autostart-enabled=true\n";
            file << "StartupWMClass=proton-drive\n";
            file.close();
        }
    } else {
        std::filesystem::remove(autostart_file);
    }
}

bool SettingsManager::get_minimize_to_tray() const {
    return get_bool("minimize_to_tray", true);
}

void SettingsManager::set_minimize_to_tray(bool enabled) {
    set_bool("minimize_to_tray", enabled);
}

bool SettingsManager::get_show_notifications() const {
    return get_bool("show_notifications", true);
}

void SettingsManager::set_show_notifications(bool enabled) {
    set_bool("show_notifications", enabled);
}

bool SettingsManager::get_play_notification_sound() const {
    return get_bool("play_notification_sound", false);
}

void SettingsManager::set_play_notification_sound(bool enabled) {
    set_bool("play_notification_sound", enabled);
}

// Sync settings
int SettingsManager::get_sync_interval_minutes() const {
    return get_int("sync_interval_minutes", 15);
}

void SettingsManager::set_sync_interval_minutes(int minutes) {
    set_int("sync_interval_minutes", std::max(1, minutes));
}

bool SettingsManager::get_sync_on_startup() const {
    return get_bool("sync_on_startup", true);
}

void SettingsManager::set_sync_on_startup(bool enabled) {
    set_bool("sync_on_startup", enabled);
}

bool SettingsManager::get_pause_sync_on_battery() const {
    return get_bool("pause_sync_on_battery", false);
}

void SettingsManager::set_pause_sync_on_battery(bool enabled) {
    set_bool("pause_sync_on_battery", enabled);
}

bool SettingsManager::get_pause_sync_on_metered() const {
    return get_bool("pause_sync_on_metered", true);
}

void SettingsManager::set_pause_sync_on_metered(bool enabled) {
    set_bool("pause_sync_on_metered", enabled);
}

// Bandwidth limits
size_t SettingsManager::get_upload_limit() const {
    return static_cast<size_t>(get_int("upload_limit", 0));
}

void SettingsManager::set_upload_limit(size_t bytes_per_second) {
    set_int("upload_limit", static_cast<int>(bytes_per_second));
}

size_t SettingsManager::get_download_limit() const {
    return static_cast<size_t>(get_int("download_limit", 0));
}

void SettingsManager::set_download_limit(size_t bytes_per_second) {
    set_int("download_limit", static_cast<int>(bytes_per_second));
}

// File handling
std::string SettingsManager::get_download_folder() const {
    return get_string("download_folder", std::string(std::getenv("HOME")) + "/Downloads");
}

void SettingsManager::set_download_folder(const std::string& path) {
    set_string("download_folder", path);
}

bool SettingsManager::get_confirm_large_upload() const {
    return get_bool("confirm_large_upload", true);
}

void SettingsManager::set_confirm_large_upload(bool enabled) {
    set_bool("confirm_large_upload", enabled);
}

size_t SettingsManager::get_large_file_threshold() const {
    return static_cast<size_t>(get_int("large_file_threshold", 100 * 1024 * 1024));
}

void SettingsManager::set_large_file_threshold(size_t bytes) {
    set_int("large_file_threshold", static_cast<int>(bytes));
}

// Advanced
bool SettingsManager::get_debug_logging() const {
    return get_bool("debug_logging", false);
}

void SettingsManager::set_debug_logging(bool enabled) {
    set_bool("debug_logging", enabled);
}

int SettingsManager::get_max_parallel_transfers() const {
    return get_int("max_parallel_transfers", 4);
}

void SettingsManager::set_max_parallel_transfers(int count) {
    set_int("max_parallel_transfers", std::max(1, std::min(10, count)));
}

// Generic accessors
std::string SettingsManager::get_string(const std::string& key, const std::string& default_value) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = settings_.find(key);
    if (it != settings_.end()) {
        return it->second;
    }
    return default_value;
}

void SettingsManager::set_string(const std::string& key, const std::string& value) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        settings_[key] = value;
    }
    notify_change(key);
}

int SettingsManager::get_int(const std::string& key, int default_value) const {
    std::string str = get_string(key, "");
    if (str.empty()) return default_value;
    try {
        return std::stoi(str);
    } catch (...) {
        return default_value;
    }
}

void SettingsManager::set_int(const std::string& key, int value) {
    set_string(key, std::to_string(value));
}

bool SettingsManager::get_bool(const std::string& key, bool default_value) const {
    std::string str = get_string(key, "");
    if (str.empty()) return default_value;
    return (str == "true" || str == "1" || str == "yes");
}

void SettingsManager::set_bool(const std::string& key, bool value) {
    set_string(key, value ? "true" : "false");
}

} // namespace proton

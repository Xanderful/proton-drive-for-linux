#pragma once

#include <string>
#include <map>
#include <mutex>
#include <functional>

namespace proton {

/**
 * Application Settings Manager
 * 
 * Manages user preferences and configuration similar to 
 * Nextcloud/ownCloud desktop clients:
 * - Bandwidth limits
 * - Notification preferences
 * - Startup behavior
 * - Sync settings
 * 
 * Settings are persisted to ~/.config/proton-drive-linux/settings.json
 */
class SettingsManager {
public:
    static SettingsManager& getInstance();
    
    // Load/save settings
    bool load();
    bool save();
    
    // General settings
    bool get_start_on_login() const;
    void set_start_on_login(bool enabled);
    
    bool get_minimize_to_tray() const;
    void set_minimize_to_tray(bool enabled);
    
    bool get_show_notifications() const;
    void set_show_notifications(bool enabled);
    
    bool get_play_notification_sound() const;
    void set_play_notification_sound(bool enabled);
    
    // Sync settings
    int get_sync_interval_minutes() const;
    void set_sync_interval_minutes(int minutes);
    
    bool get_sync_on_startup() const;
    void set_sync_on_startup(bool enabled);
    
    bool get_pause_sync_on_battery() const;
    void set_pause_sync_on_battery(bool enabled);
    
    bool get_pause_sync_on_metered() const;
    void set_pause_sync_on_metered(bool enabled);
    
    // Bandwidth limits (0 = unlimited)
    size_t get_upload_limit() const;
    void set_upload_limit(size_t bytes_per_second);
    
    size_t get_download_limit() const;
    void set_download_limit(size_t bytes_per_second);
    
    // File handling
    std::string get_download_folder() const;
    void set_download_folder(const std::string& path);
    
    bool get_confirm_large_upload() const;
    void set_confirm_large_upload(bool enabled);
    
    size_t get_large_file_threshold() const;  // bytes
    void set_large_file_threshold(size_t bytes);
    
    // Advanced
    bool get_debug_logging() const;
    void set_debug_logging(bool enabled);
    
    int get_max_parallel_transfers() const;
    void set_max_parallel_transfers(int count);
    
    // Settings change callback
    using SettingsChangeCallback = std::function<void(const std::string& key)>;
    void set_change_callback(SettingsChangeCallback callback);
    
    // Generic getters/setters for custom settings
    std::string get_string(const std::string& key, const std::string& default_value = "") const;
    void set_string(const std::string& key, const std::string& value);
    
    int get_int(const std::string& key, int default_value = 0) const;
    void set_int(const std::string& key, int value);
    
    bool get_bool(const std::string& key, bool default_value = false) const;
    void set_bool(const std::string& key, bool value);
    
    // Get the config directory path
    std::string get_config_dir() const;
    
private:
    SettingsManager();
    ~SettingsManager() = default;
    
    SettingsManager(const SettingsManager&) = delete;
    SettingsManager& operator=(const SettingsManager&) = delete;
    
    void notify_change(const std::string& key);
    void ensure_defaults();
    
    mutable std::mutex mutex_;
    std::map<std::string, std::string> settings_;
    std::string config_path_;
    SettingsChangeCallback change_callback_;
    bool loaded_ = false;
};

} // namespace proton

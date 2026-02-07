#pragma once

#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <map>
#include <gio/gio.h>

namespace proton {

/**
 * Desktop notification types for user feedback
 */
enum class NotificationType {
    INFO,
    SUCCESS,
    WARNING,
    ERROR,
    SYNC_COMPLETE,
    SYNC_ERROR,
    UPLOAD_COMPLETE,
    DOWNLOAD_COMPLETE
};

/**
 * Notification action callback
 */
using NotificationAction = std::function<void(const std::string& action_id)>;

/**
 * Desktop Notifications Manager
 * 
 * Provides system notifications for:
 * - Sync completion/errors
 * - Upload/download progress
 * - Connection status changes
 * - File conflict notifications
 * 
 * Uses GNotification (GIO) for cross-desktop compatibility
 */
class NotificationManager {
public:
    static NotificationManager& getInstance();
    
    // Initialize with GApplication for notifications
    void init(GApplication* app);
    
    // Simple notifications
    void notify(const std::string& title, const std::string& body, 
                NotificationType type = NotificationType::INFO);
    
    // Notification with actions (e.g., "Show File", "Dismiss")
    void notify_with_action(const std::string& title, const std::string& body,
                           const std::string& action_id, const std::string& action_label,
                           NotificationAction callback);
    
    // Sync-specific notifications
    void notify_sync_complete(int files_synced, int errors);
    void notify_sync_error(const std::string& error_message);
    void notify_file_conflict(const std::string& filename);
    void notify_upload_complete(const std::string& filename, size_t bytes);
    void notify_download_complete(const std::string& filename, size_t bytes);
    void notify_connection_status(bool connected);
    
    // Progress notifications (for long operations)
    void show_progress(const std::string& id, const std::string& title,
                      double progress, const std::string& status);
    void hide_progress(const std::string& id);
    
    // Settings
    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool is_enabled() const { return enabled_; }
    void set_sound_enabled(bool enabled) { sound_enabled_ = enabled; }
    
private:
    NotificationManager() = default;
    ~NotificationManager() = default;
    
    NotificationManager(const NotificationManager&) = delete;
    NotificationManager& operator=(const NotificationManager&) = delete;
    
    std::string get_icon_for_type(NotificationType type) const;
    
    GApplication* app_ = nullptr;
    bool enabled_ = true;
    bool sound_enabled_ = true;
    
    // Store callbacks for action handling
    std::map<std::string, NotificationAction> action_callbacks_;
};

/**
 * Format bytes to human readable string
 */
std::string format_bytes(size_t bytes);

} // namespace proton

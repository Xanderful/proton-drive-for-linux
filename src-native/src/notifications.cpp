#include "notifications.hpp"
#include "logger.hpp"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <chrono>
#include <map>

namespace proton {

NotificationManager& NotificationManager::getInstance() {
    static NotificationManager instance;
    return instance;
}

void NotificationManager::init(GApplication* app) {
    app_ = app;
    if (app_) {
        Logger::info("[Notifications] Notification manager initialized");
    }
}

std::string NotificationManager::get_icon_for_type(NotificationType type) const {
    switch (type) {
        case NotificationType::SUCCESS:
        case NotificationType::SYNC_COMPLETE:
            return "emblem-ok-symbolic";
        case NotificationType::WARNING:
            return "dialog-warning-symbolic";
        case NotificationType::ERROR:
        case NotificationType::SYNC_ERROR:
            return "dialog-error-symbolic";
        case NotificationType::UPLOAD_COMPLETE:
            return "go-up-symbolic";
        case NotificationType::DOWNLOAD_COMPLETE:
            return "go-down-symbolic";
        case NotificationType::INFO:
        default:
            return "proton-drive";
    }
}

void NotificationManager::notify(const std::string& title, const std::string& body, 
                                  NotificationType type) {
    if (!enabled_ || !app_) {
        Logger::debug("[Notifications] Skipped: " + title);
        return;
    }
    
    GNotification* notification = g_notification_new(title.c_str());
    g_notification_set_body(notification, body.c_str());
    
    // Set icon based on type
    GIcon* icon = g_themed_icon_new(get_icon_for_type(type).c_str());
    g_notification_set_icon(notification, icon);
    
    // Set priority based on type
    switch (type) {
        case NotificationType::ERROR:
        case NotificationType::SYNC_ERROR:
            g_notification_set_priority(notification, G_NOTIFICATION_PRIORITY_URGENT);
            break;
        case NotificationType::WARNING:
            g_notification_set_priority(notification, G_NOTIFICATION_PRIORITY_HIGH);
            break;
        default:
            g_notification_set_priority(notification, G_NOTIFICATION_PRIORITY_NORMAL);
            break;
    }
    
    // Generate unique ID for this notification
    std::string notification_id = "proton-drive-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    
    g_application_send_notification(app_, notification_id.c_str(), notification);
    
    g_object_unref(icon);
    g_object_unref(notification);
    
    Logger::info("[Notifications] Sent: " + title);
}

void NotificationManager::notify_with_action(const std::string& title, const std::string& body,
                                              const std::string& action_id, 
                                              const std::string& action_label,
                                              NotificationAction callback) {
    if (!enabled_ || !app_) return;
    
    GNotification* notification = g_notification_new(title.c_str());
    g_notification_set_body(notification, body.c_str());
    
    // Add action button
    std::string action_name = "app." + action_id;
    g_notification_add_button(notification, action_label.c_str(), action_name.c_str());
    
    // Store callback for later
    action_callbacks_[action_id] = std::move(callback);
    
    g_application_send_notification(app_, action_id.c_str(), notification);
    g_object_unref(notification);
}

void NotificationManager::notify_sync_complete(int files_synced, int errors) {
    std::ostringstream oss;
    if (errors == 0) {
        oss << "Successfully synced " << files_synced << " file" 
            << (files_synced == 1 ? "" : "s");
        notify("Sync Complete", oss.str(), NotificationType::SYNC_COMPLETE);
    } else {
        oss << "Synced " << files_synced << " file" << (files_synced == 1 ? "" : "s")
            << " with " << errors << " error" << (errors == 1 ? "" : "s");
        notify("Sync Complete with Errors", oss.str(), NotificationType::WARNING);
    }
}

void NotificationManager::notify_sync_error(const std::string& error_message) {
    notify("Sync Error", error_message, NotificationType::SYNC_ERROR);
}

void NotificationManager::notify_file_conflict(const std::string& filename) {
    std::string body = "A conflict was detected for: " + filename;
    notify_with_action("File Conflict", body, "resolve-conflict", "Resolve",
        [filename](const std::string&) {
            Logger::info("[Notifications] User requested to resolve conflict for: " + filename);
        });
}

void NotificationManager::notify_upload_complete(const std::string& filename, size_t bytes) {
    std::string body = filename + " (" + format_bytes(bytes) + ")";
    notify("Upload Complete", body, NotificationType::UPLOAD_COMPLETE);
}

void NotificationManager::notify_download_complete(const std::string& filename, size_t bytes) {
    std::string body = filename + " (" + format_bytes(bytes) + ")";
    notify("Download Complete", body, NotificationType::DOWNLOAD_COMPLETE);
}

void NotificationManager::notify_connection_status(bool connected) {
    if (connected) {
        notify("Connected", "Connected to Proton Drive", NotificationType::SUCCESS);
    } else {
        notify("Disconnected", "Lost connection to Proton Drive", NotificationType::WARNING);
    }
}

void NotificationManager::show_progress(const std::string& id, const std::string& title,
                                         double progress, const std::string& status) {
    // GNotification doesn't support progress directly
    // We update the notification body with progress info
    if (!enabled_ || !app_) return;
    
    std::ostringstream oss;
    oss << status << " (" << std::fixed << std::setprecision(0) << (progress * 100) << "%)";
    
    GNotification* notification = g_notification_new(title.c_str());
    g_notification_set_body(notification, oss.str().c_str());
    
    GIcon* icon = g_themed_icon_new("emblem-synchronizing-symbolic");
    g_notification_set_icon(notification, icon);
    
    g_application_send_notification(app_, id.c_str(), notification);
    
    g_object_unref(icon);
    g_object_unref(notification);
}

void NotificationManager::hide_progress(const std::string& id) {
    if (app_) {
        g_application_withdraw_notification(app_, id.c_str());
    }
}

std::string format_bytes(size_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_index = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1024.0 && unit_index < 4) {
        size /= 1024.0;
        unit_index++;
    }
    
    std::ostringstream oss;
    if (unit_index == 0) {
        oss << bytes << " B";
    } else {
        oss << std::fixed << std::setprecision(1) << size << " " << units[unit_index];
    }
    return oss.str();
}

} // namespace proton

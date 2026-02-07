#pragma once

#include <gtk/gtk.h>
#include <string>
#include <functional>
#include <thread>
#include <atomic>

#ifdef HAVE_APPINDICATOR
// Forward declarations for AppIndicator
typedef struct _AppIndicator AppIndicator;
#endif

class TrayIcon {
public:
    TrayIcon(GtkApplication* app = nullptr);
    ~TrayIcon();

    // Initialize the tray icon
    void init();
    
    // Stop background threads (call before destruction)
    void stop();
    
    // Set callback for when "Show/Hide App" is clicked
    void set_toggle_window_callback(std::function<void()> callback);

private:
    GtkApplication* app_;
    
#ifdef HAVE_APPINDICATOR
    AppIndicator* indicator_ = nullptr;
    GtkWidget* menu_ = nullptr;
    GtkWidget* status_item_ = nullptr;
    GtkWidget* sync_toggle_item_ = nullptr;
    
    std::function<void()> toggle_callback_;
    std::atomic<bool> stop_thread_{false};
    std::thread status_thread_;
    
    // Check sync service status
    bool is_sync_running();
    
    // Menu callbacks
    static void on_toggle_window(GtkWidget* widget, gpointer data);
    static void on_open_folder(GtkWidget* widget, gpointer data);
    static void on_toggle_sync(GtkWidget* widget, gpointer data);
    static void on_setup_sync(GtkWidget* widget, gpointer data);
    static void on_quit(GtkWidget* widget, gpointer data);
    
    // Background thread to update status
    void monitor_status();
    void update_menu_status(bool is_running);
#endif
};

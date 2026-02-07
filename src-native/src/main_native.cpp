/**
 * Proton Drive Linux Desktop Client
 * 
 * A native C++ sync application using GTK3
 * 
 * This is the new native-only version that removes the embedded webview
 * in favor of a native GTK UI with:
 * - Hamburger menu for settings, profiles, and sync jobs
 * - Drag-and-drop zone as the primary interface
 * - Cloud browser for file navigation
 * - Sync progress overlay
 * - Logs panel at the bottom
 */

#include "app_window.hpp"
#include "tray.hpp"
#include "logger.hpp"
#include "file_index.hpp"
#include "sync_job_metadata.hpp"
#include "sync_manager.hpp"
#include "trash_manager.hpp"
#include "settings.hpp"
#include "notifications.hpp"
#include <iostream>
#include <memory>
#include <cstdlib>
#include <string>
#include <vector>
#include <gtk/gtk.h>
#include <glib-unix.h>
#include <filesystem>
#include <thread>
#include <unistd.h>
#include <linux/limits.h>
#include <fstream>
#include <sys/file.h>
#include <sys/stat.h>
#include <signal.h>
#include <execinfo.h>
#include <cxxabi.h>
#include <sys/resource.h>
#include <sqlite3.h>
#include <curl/curl.h>
#include <openssl/opensslv.h>

namespace fs = std::filesystem;

/**
 * Crash handler - writes stack trace to log file
 */
static void crash_handler(int sig) {
    // Get home directory for crash dump location
    const char* home = getenv("HOME");
    std::string crash_file = home ? std::string(home) + "/.cache/proton-drive/crash.log" : "/tmp/proton-drive-crash.log";
    
    // Ensure directory exists
    if (home) {
        std::string cache_dir = std::string(home) + "/.cache/proton-drive";
        mkdir(cache_dir.c_str(), 0755);
    }
    
    FILE* f = fopen(crash_file.c_str(), "a");
    if (!f) f = stderr;
    
    // Get current time
    time_t now = time(nullptr);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    fprintf(f, "\n========== CRASH REPORT ==========\n");
    fprintf(f, "Time: %s\n", time_buf);
    fprintf(f, "Signal: %d (%s)\n", sig, 
            sig == SIGSEGV ? "SIGSEGV (Segmentation fault)" :
            sig == SIGABRT ? "SIGABRT (Abort)" :
            sig == SIGFPE ? "SIGFPE (Floating point exception)" :
            sig == SIGBUS ? "SIGBUS (Bus error)" :
            sig == SIGILL ? "SIGILL (Illegal instruction)" : "Unknown");
    
    // Get stack trace
    void* stack[64];
    int stack_size = backtrace(stack, 64);
    char** symbols = backtrace_symbols(stack, stack_size);
    
    fprintf(f, "\nStack trace (%d frames):\n", stack_size);
    for (int i = 0; i < stack_size; i++) {
        // Try to demangle C++ symbols
        std::string symbol(symbols[i]);
        size_t start = symbol.find('(');
        size_t end = symbol.find('+', start);
        
        if (start != std::string::npos && end != std::string::npos) {
            std::string mangled = symbol.substr(start + 1, end - start - 1);
            int status;
            char* demangled = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);
            if (status == 0 && demangled) {
                fprintf(f, "  [%d] %s\n      -> %s\n", i, symbols[i], demangled);
                free(demangled);
            } else {
                fprintf(f, "  [%d] %s\n", i, symbols[i]);
            }
        } else {
            fprintf(f, "  [%d] %s\n", i, symbols[i]);
        }
    }
    free(symbols);
    
    fprintf(f, "\nCrash dump saved to: %s\n", crash_file.c_str());
    fprintf(f, "===================================\n\n");
    
    if (f != stderr) {
        fclose(f);
        fprintf(stderr, "\n*** CRASH: Signal %d. Stack trace saved to %s ***\n", sig, crash_file.c_str());
    }
    
    // Re-raise the signal to get core dump if enabled
    signal(sig, SIG_DFL);
    raise(sig);
}

/**
 * Graceful shutdown handler for SIGTERM/SIGINT (GLib version)
 */
static GtkApplication* global_app = nullptr;
static std::unique_ptr<TrayIcon> global_tray_icon = nullptr;

static gboolean shutdown_handler_glib(gpointer /*user_data*/) {
    Logger::info("[Shutdown] Main signal handler called - initiating full application shutdown...");
    
    // Save any pending state
    auto& settings = proton::SettingsManager::getInstance();
    settings.save();
    Logger::info("[Shutdown] Settings saved");
    
    // Stop file watcher
    try {
        auto& syncMgr = SyncManager::getInstance();
        syncMgr.shutdown();
        Logger::info("[Shutdown] SyncManager shutdown complete");
    } catch (const std::exception& e) {
        Logger::error("[Shutdown] SyncManager shutdown error: " + std::string(e.what()));
    }
    
    // Stop AppWindow background threads (CloudMonitor, etc)
    try {
        auto& appWindow = AppWindow::getInstance();
        appWindow.shutdown();
        Logger::info("[Shutdown] AppWindow shutdown complete");
    } catch (const std::exception& e) {
        Logger::error("[Shutdown] AppWindow shutdown error: " + std::string(e.what()));
    }
    
    // Quit the GTK application - this will cleanly exit the main loop
    if (global_app) {
        Logger::info("[Shutdown] Quitting GTK application...");
        g_application_quit(G_APPLICATION(global_app));
    } else {
        Logger::warn("[Shutdown] No global_app set, calling gtk_main_quit");
        #ifndef USE_GTK4
        gtk_main_quit();
        #endif
    }
    
    Logger::info("[Shutdown] Cleanup complete, application will exit");
    return G_SOURCE_REMOVE;
}

/**
 * Install crash handlers
 */
static void install_crash_handlers() {
    // Enable core dumps
    struct rlimit core_limit;
    core_limit.rlim_cur = RLIM_INFINITY;
    core_limit.rlim_max = RLIM_INFINITY;
    setrlimit(RLIMIT_CORE, &core_limit);
    
    // Install signal handlers for crashes
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGFPE, crash_handler);
    signal(SIGBUS, crash_handler);
    signal(SIGILL, crash_handler);
    
    // Install GLib Unix signal handlers for graceful shutdown
    // These work properly with GTK main loop unlike traditional signal()
    g_unix_signal_add(SIGTERM, shutdown_handler_glib, nullptr);
    g_unix_signal_add(SIGINT, shutdown_handler_glib, nullptr);
    
    Logger::info("[Crash] Signal handlers installed");
}

/**
 * Ensure only one instance of the application is running
 * Returns true if this is the only instance, false otherwise
 */
bool ensure_single_instance() {
    std::string runtime_dir = "/run/user/" + std::to_string(getuid());
    std::string lock_file = runtime_dir + "/proton-drive.lock";
    
    // Fallback to XDG_RUNTIME_DIR or /tmp if /run/user doesn't exist
    if (!fs::exists(runtime_dir)) {
        const char* xdg_runtime = std::getenv("XDG_RUNTIME_DIR");
        if (xdg_runtime) {
            runtime_dir = xdg_runtime;
            lock_file = runtime_dir + "/proton-drive.lock";
        } else {
            lock_file = "/tmp/proton-drive-" + std::to_string(getuid()) + ".lock";
        }
    }
    
    // Try to open/create the lock file
    int lock_fd = open(lock_file.c_str(), O_CREAT | O_RDWR, 0600);
    if (lock_fd < 0) {
        Logger::error("[SingleInstance] Failed to create lock file: " + lock_file);
        return true; // Fail open - allow to run
    }
    
    // Try to acquire an exclusive lock (non-blocking)
    if (flock(lock_fd, LOCK_EX | LOCK_NB) < 0) {
        // Lock already held by another instance
        close(lock_fd);
        
        // Try to read the PID from the lock file
        std::ifstream lock_stream(lock_file);
        std::string pid_str;
        if (lock_stream >> pid_str) {
            Logger::info("[SingleInstance] Another instance is already running (PID: " + pid_str + ")");
            std::cerr << "Proton Drive is already running (PID: " << pid_str << ")\n";
        } else {
            Logger::info("[SingleInstance] Another instance is already running");
            std::cerr << "Proton Drive is already running\n";
        }
        return false;
    }
    
    // We got the lock - write our PID to the file
    if (ftruncate(lock_fd, 0) == 0) {
        std::string pid = std::to_string(getpid());
        if (write(lock_fd, pid.c_str(), pid.length()) < 0) {
            Logger::error("[SingleInstance] Failed to write PID to lock file");
        }
    }
    
    Logger::info("[SingleInstance] Acquired lock file: " + lock_file);
    
    // Keep lock_fd open for the lifetime of the application
    // It will be automatically released when the process exits
    return true;
}

/**
 * Check for required dependencies on startup
 * Returns true if all dependencies are satisfied, false otherwise
 */
bool check_dependencies(std::vector<std::string>& missing_deps, std::vector<std::string>& warnings) {
    bool all_ok = true;
    
    // 1. Check for rclone (CRITICAL - required for sync)
    std::string rclone_check = "command -v rclone >/dev/null 2>&1";
    if (system(rclone_check.c_str()) != 0) {
        missing_deps.push_back("rclone - Required for cloud sync operations");
        all_ok = false;
        Logger::error("[Dependency] CRITICAL: rclone not found in PATH");
    } else {
        // Check rclone version
        FILE* pipe = popen("rclone version 2>&1 | head -1", "r");
        if (pipe) {
            char buffer[256];
            if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                Logger::info("[Dependency] Found: " + std::string(buffer));
            }
            pclose(pipe);
        }
    }
    
    // 2. Check for SQLite3 library (CRITICAL - required for file index)
    // We check this by trying to open/create a test database
    const char* home = getenv("HOME");
    std::string test_db = home ? std::string(home) + "/.cache/proton-drive/.dep-check.db" : "/tmp/.proton-dep-check.db";
    
    sqlite3* test_sqlite = nullptr;
    int rc = sqlite3_open(test_db.c_str(), &test_sqlite);
    if (rc != SQLITE_OK) {
        missing_deps.push_back("SQLite3 - Required for file search index");
        all_ok = false;
        Logger::error("[Dependency] CRITICAL: SQLite3 library not functional: " + std::string(sqlite3_errmsg(test_sqlite)));
    } else {
        Logger::info("[Dependency] SQLite3 library OK (version: " + std::string(sqlite3_libversion()) + ")");
        sqlite3_close(test_sqlite);
        // Clean up test db
        unlink(test_db.c_str());
    }
    
    // 3. Check for curl (CRITICAL - required for API calls)
    // Already linked at compile time, but verify it's functional
    CURL* curl_test = curl_easy_init();
    if (!curl_test) {
        missing_deps.push_back("libcurl - Required for network operations");
        all_ok = false;
        Logger::error("[Dependency] CRITICAL: libcurl not functional");
    } else {
        curl_version_info_data* cv = curl_version_info(CURLVERSION_NOW);
        Logger::info("[Dependency] libcurl OK (version: " + std::string(cv->version) + ")");
        curl_easy_cleanup(curl_test);
    }
    
    // 4. Check for openssl (used for encryption)
    #ifdef OPENSSL_VERSION_TEXT
        Logger::info("[Dependency] OpenSSL OK (" + std::string(OPENSSL_VERSION_TEXT) + ")");
    #else
        warnings.push_back("OpenSSL version unknown - encryption may be unavailable");
        Logger::warn("[Dependency] OpenSSL version not detectable");
    #endif
    
    // 5. Check for GTK4 (will be verified by gtk_init later, but log version)
    Logger::info("[Dependency] GTK version: " + std::to_string(gtk_get_major_version()) + "." +
                 std::to_string(gtk_get_minor_version()) + "." +
                 std::to_string(gtk_get_micro_version()));
    
    // 6. Optional: Check for systemd (for sync service management)
    std::string systemctl_check = "command -v systemctl >/dev/null 2>&1";
    if (system(systemctl_check.c_str()) != 0) {
        warnings.push_back("systemctl not found - recurring sync jobs require systemd");
        Logger::warn("[Dependency] systemctl not found - sync scheduling may be limited");
    } else {
        Logger::info("[Dependency] systemd/systemctl found");
    }
    
    // 7. Optional: Check for xdg-open (for opening browser)
    std::string xdg_check = "command -v xdg-open >/dev/null 2>&1";
    if (system(xdg_check.c_str()) != 0) {
        warnings.push_back("xdg-open not found - OAuth login may require manual browser");
        Logger::warn("[Dependency] xdg-open not found - browser launch may fail");
    }
    
    return all_ok;
}

/**
 * Show GTK4 error dialog for missing dependencies
 * This blocks until user clicks OK
 */
void show_dependency_error_dialog(const std::vector<std::string>& missing_deps) {
    // Also log to console for debugging
    std::cerr << "\n";
    std::cerr << "╔════════════════════════════════════════════════════════════════╗\n";
    std::cerr << "║  ERROR: Proton Drive cannot start - Missing Dependencies       ║\n";
    std::cerr << "╚════════════════════════════════════════════════════════════════╝\n";
    std::cerr << "\n";
    
    for (const auto& dep : missing_deps) {
        std::cerr << "  ✗ " << dep << "\n";
    }
    std::cerr << "\n";
    
    // Initialize GTK if needed (minimal init for dialog only)
    if (!gtk_is_initialized()) {
        gtk_init();
    }
    
    // Build error message
    std::string message = "Proton Drive cannot start. The following required dependencies are missing:\n\n";
    for (const auto& dep : missing_deps) {
        message += "• " + dep + "\n";
    }
    message += "\n─────────────────────────────────────────────\n\n";
    message += "Installation Instructions:\n\n";
    message += "Ubuntu/Debian:\n";
    message += "  sudo apt install rclone libsqlite3-0 libcurl4\n\n";
    message += "Fedora/RHEL:\n";
    message += "  sudo dnf install rclone sqlite libcurl\n\n";
    message += "Arch:\n";
    message += "  sudo pacman -S rclone sqlite curl";
    
    // Create a simple window with the error message
    GtkWidget* window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), "Proton Drive - Missing Dependencies");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 400);
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    
    // Create a box layout
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);
    gtk_window_set_child(GTK_WINDOW(window), box);
    
    // Error icon and title
    GtkWidget* header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* icon = gtk_image_new_from_icon_name("dialog-error");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 48);
    GtkWidget* title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span size='x-large' weight='bold'>Missing Required Dependencies</span>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(header_box), icon);
    gtk_box_append(GTK_BOX(header_box), title);
    gtk_box_append(GTK_BOX(box), header_box);
    
    // Scrollable text view with message
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    
    GtkWidget* text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text_view), 10);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(text_view), 10);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(text_view), 10);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(text_view), 10);
    
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    gtk_text_buffer_set_text(buffer, message.c_str(), -1);
    
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), text_view);
    gtk_box_append(GTK_BOX(box), scroll);
    
    // OK button
    GtkWidget* button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(button_box, GTK_ALIGN_END);
    GtkWidget* ok_button = gtk_button_new_with_label("OK");
    gtk_widget_set_size_request(ok_button, 100, -1);
    
    // Connect button click to close window
    g_signal_connect_swapped(ok_button, "clicked", G_CALLBACK(gtk_window_destroy), window);
    
    gtk_box_append(GTK_BOX(button_box), ok_button);
    gtk_box_append(GTK_BOX(box), button_box);
    
    // Show window and run modal loop
    gtk_widget_set_visible(window, TRUE);
    gtk_window_present(GTK_WINDOW(window));
    
    // Block until window is closed (run a local event loop)
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    g_signal_connect_swapped(window, "destroy", G_CALLBACK(g_main_loop_quit), loop);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
}

int main(int argc, char* argv[]) {
    // Install crash handlers FIRST
    install_crash_handlers();
    
    // Suppress GTK IM module warnings - Proton Drive doesn't need special input methods
    // Unset GTK_IM_MODULE to avoid loading unnecessary input method modules
    unsetenv("GTK_IM_MODULE");
    // Also unset GTK_IM_MODULE_FILE if it's pointing to GTK3 cache that interferes with GTK4
    unsetenv("GTK_IM_MODULE_FILE");
    
    // Check for single instance before doing anything else
    if (!ensure_single_instance()) {
        return 1;
    }
    
    // Parse arguments and build new argv without consumed options
    bool debug_mode = false;
    std::vector<char*> new_argv;
    new_argv.push_back(argv[0]);  // Program name
    
    for (int i = 1; i < argc; i++) {
        std::string arg(argv[i]);
        if (arg == "--debug") {
            debug_mode = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Proton Drive Linux Desktop Client\n\n"
                      << "Usage: proton-drive [options]\n\n"
                      << "Options:\n"
                      << "  --debug    Enable debug logging\n"
                      << "  --help     Show this help message\n";
            return 0;
        } else {
            // Keep unconsumed arguments for GTK
            new_argv.push_back(argv[i]);
        }
    }
    
    int new_argc = new_argv.size();
    
    // Init logger with file output
    const char* home = std::getenv("HOME");
    std::string log_dir = home ? std::string(home) + "/.cache/proton-drive" : "/tmp";
    std::string log_file = log_dir + "/proton-drive.log";
    
    // Ensure log directory exists
    std::filesystem::create_directories(log_dir);
    
    Logger::init(debug_mode ? LogLevel::DEBUG : LogLevel::INFO, log_file);
    Logger::info("Proton Drive Linux - Starting (Native UI)...");
    if (debug_mode) Logger::debug("Debug mode enabled");
    
    // Check for required dependencies BEFORE initializing anything else
    std::vector<std::string> missing_deps;
    std::vector<std::string> warnings;
    Logger::info("[Init] Checking dependencies...");
    
    if (!check_dependencies(missing_deps, warnings)) {
        // Critical dependencies missing
        Logger::error("[Init] CRITICAL: Missing required dependencies!");
        for (const auto& dep : missing_deps) {
            Logger::error("[Init]   - " + dep);
            std::cerr << "MISSING: " << dep << "\n";
        }
        
        // Show GTK error dialog
        show_dependency_error_dialog(missing_deps);
        
        Logger::error("[Init] Cannot continue without required dependencies. Exiting.");
        return 1;
    }
    
    // Log warnings for optional dependencies
    if (!warnings.empty()) {
        Logger::warn("[Init] Non-critical warnings:");
        for (const auto& warn : warnings) {
            Logger::warn("[Init]   - " + warn);
        }
    }
    
    Logger::info("[Init] All required dependencies satisfied ✓");
    
    // Initialize settings
    auto& settings = proton::SettingsManager::getInstance();
    settings.load();
    Logger::info("[Init] Settings loaded");
    
    // Initialize trash manager
    auto& trash = proton::TrashManager::getInstance();
    if (trash.initialize()) {
        Logger::info("[Init] Trash manager initialized");
    } else {
        Logger::warn("[Init] Failed to initialize trash manager");
    }
    
    // Initialize file index for cloud file search
    auto& file_index = FileIndex::getInstance();
    if (file_index.initialize()) {
        Logger::info("[Init] File index initialized");
        
        // Check if index is empty or stale
        auto stats = file_index.get_stats();
        bool is_empty = (stats.total_files == 0 && stats.total_folders == 0);
        bool is_stale = file_index.needs_refresh(2);  // 2 hours instead of 24
        
        if (is_empty) {
            Logger::info("[Init] File index is empty, starting initial index...");
            file_index.start_background_index(true);  // Full index
        } else if (is_stale) {
            Logger::info("[Init] File index is stale (>2 hours), starting background refresh...");
            file_index.start_background_index(false);  // Incremental update
        } else {
            Logger::info("[Init] File index is up-to-date (" + std::to_string(stats.total_files) + 
                        " files, " + std::to_string(stats.total_folders) + " folders)");
        }
    } else {
        Logger::warn("[Init] Failed to initialize file index - search may be limited");
    }
    
    // Ensure default ProtonDrive folder exists
    if (!SyncJobRegistry::ensureDefaultSyncLocation()) {
        Logger::warn("[Init] Could not create default ProtonDrive folder - user will need to select custom location for syncs");
    }
    
    // Initialize SyncManager (starts file watcher for real-time sync)
    SyncManager::getInstance().init();
    Logger::info("[Init] SyncManager initialized");
    
    // Initialize GTK
#ifdef USE_GTK4
    gtk_init();
    Logger::debug("[Init] GTK4 initialized");
    GtkApplication* app = gtk_application_new("me.proton.drive", G_APPLICATION_FLAGS_NONE);  // Compatible with GTK 4.6+
    global_app = app;  // Store for signal handler
    Logger::debug("[Init] GtkApplication created");
    
    // In GTK4, we need to handle the 'activate' signal to create windows
    g_signal_connect(app, "activate", G_CALLBACK(+[](GtkApplication* app, gpointer user_data) {
        (void)user_data;
        
        Logger::debug("[Activate] GTK activate signal received");
        
        // Initialize the main application window
        Logger::debug("[Activate] About to get AppWindow singleton...");
        AppWindow& app_window = AppWindow::getInstance();
        Logger::debug("[Activate] Got AppWindow singleton, about to initialize...");
        if (!app_window.initialize()) {
            Logger::error("Fatal: Failed to initialize application window. Exiting.");
            g_application_quit(G_APPLICATION(app));
            return;
        }
        Logger::debug("[Activate] AppWindow initialized");
        
        // Initialize notification manager
        proton::NotificationManager::getInstance().init(G_APPLICATION(app));
        Logger::debug("[Activate] NotificationManager initialized");
        
        // Get the GTK window and set it as the app window
        Logger::debug("[Activate] Getting window widget...");
        GtkWidget* window = app_window.get_window();
        Logger::debug("[Activate] Adding window to application...");
        gtk_application_add_window(app, GTK_WINDOW(window));
        Logger::debug("[Activate] Window added to application");
        
        // Initialize system tray icon using StatusNotifierItem D-Bus protocol
        Logger::debug("[Activate] Creating tray icon...");
        global_tray_icon = std::make_unique<TrayIcon>(app);
        Logger::debug("[Activate] Setting tray toggle callback...");
        global_tray_icon->set_toggle_window_callback([window]() {
            if (gtk_widget_get_visible(window)) {
                gtk_widget_set_visible(window, FALSE);
            } else {
                gtk_widget_set_visible(window, TRUE);
                gtk_window_present(GTK_WINDOW(window));
            }
        });
        Logger::debug("[Activate] Initializing tray icon...");
        global_tray_icon->init();
        Logger::debug("[Activate] Tray icon initialized");
        
        // Show the main window
        Logger::debug("[Activate] Showing main window...");
        app_window.show();
        Logger::debug("[Activate] Main window shown");
        
        // Log startup complete
        app_window.append_log("Proton Drive started");
        
        // Schedule periodic file index refresh every 2 hours (7200000 ms)
        g_timeout_add_seconds(7200, +[](gpointer) -> gboolean {
            auto& file_index = FileIndex::getInstance();
            if (!file_index.is_indexing()) {
                Logger::info("[Index] Scheduled 2-hour refresh starting...");
                file_index.start_background_index(false);  // Incremental
            }
            return G_SOURCE_CONTINUE;
        }, nullptr);
        
        // Set progress callback for index completion notification
        FileIndex::getInstance().set_progress_callback([](int percent, const std::string& status) {
            if (percent == 100) {
                // Notify on index build/refresh complete (via g_idle_add for thread safety)
                struct NotifyData {
                    std::string status;
                };
                auto* nd = new NotifyData{status};
                g_idle_add(+[](gpointer user_data) -> gboolean {
                    auto* d = static_cast<NotifyData*>(user_data);
                    proton::NotificationManager::getInstance().notify(
                        "Search Index Updated", d->status,
                        proton::NotificationType::INFO);
                    auto& aw = AppWindow::getInstance();
                    aw.append_log("[Index] " + d->status);
                    delete d;
                    return G_SOURCE_REMOVE;
                }, nd);
            }
        });
    }), nullptr);
    
    // Run the application
    int status = g_application_run(G_APPLICATION(app), new_argc, new_argv.data());
    
    // Shutdown sequence - ensure all threads are stopped before static destruction
    Logger::info("[Shutdown] Application run completed, cleaning up...");
    
    // Kill any background rclone processes we spawned (only for current user)
    std::string uid_str = std::to_string(getuid());
    [[maybe_unused]] int kill_result = std::system(
        ("pkill -U " + uid_str + " -f 'rclone.*lsjson.*proton:' 2>/dev/null").c_str());
    kill_result = std::system(
        ("pkill -U " + uid_str + " -f 'rclone.*copyto.*proton:' 2>/dev/null").c_str());
    
    // Stop TrayIcon background thread (status monitoring)
    if (global_tray_icon) {
        Logger::debug("[Shutdown] Stopping tray icon...");
        global_tray_icon->stop();
        global_tray_icon.reset();
        Logger::debug("[Shutdown] Tray icon stopped");
    }
    
    // Stop AppWindow threads first (CloudMonitor)
    try {
        AppWindow::getInstance().shutdown();
    } catch (...) {
        Logger::error("[Shutdown] AppWindow shutdown threw exception");
    }
    
    // Shutdown FileIndex (encrypts database)
    FileIndex::getInstance().shutdown();
    
    g_object_unref(app);
    
    Logger::info("Proton Drive Linux - Exiting.");
    return status;
#else
    gtk_init(&argc, &argv);
    
    // Initialize the main application window
    AppWindow& app_window = AppWindow::getInstance();
    if (!app_window.initialize()) {
        Logger::error("Fatal: Failed to initialize application window. Exiting.");
        return 1;
    }
    
    // Get the GTK window
    GtkWidget* window = app_window.get_window();
    
    // Initialize tray icon
    TrayIcon tray_icon;
    tray_icon.init();
    
    // Set up tray toggle callback
    tray_icon.set_toggle_window_callback([&app_window]() {
        app_window.toggle_visibility();
    });
    
    // Show the main window
    app_window.show();
    
    // Log startup complete
    app_window.append_log("Proton Drive started");
    
    // Run main loop
    gtk_main();
    
    // Gracefully shutdown FileIndex
    FileIndex::getInstance().shutdown();
    
    Logger::info("Proton Drive Linux - Exiting.");
    return 0;
#endif
}

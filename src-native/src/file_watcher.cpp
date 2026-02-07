// file_watcher.cpp - inotify-based file watcher implementation
// Uses native Linux inotify API for maximum compatibility

#include "file_watcher.hpp"
#include "logger.hpp"

#include <sys/inotify.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>
#include <cerrno>
#include <poll.h>
#include <csignal>
#include <atomic>

// Size of inotify event buffer
#define EVENT_BUF_LEN (1024 * (sizeof(struct inotify_event) + 256))

// Events we care about for sync
#define WATCH_EVENTS (IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE)

// Global flag for graceful shutdown (set by main signal handler)
static std::atomic<bool> g_graceful_shutdown_requested{false};

FileWatcher::FileWatcher() 
    : inotify_fd_(-1)
    , debounce_seconds_(3)
    , running_(false) {
}

FileWatcher::~FileWatcher() {
    stop();
    if (inotify_fd_ >= 0) {
        close(inotify_fd_);
    }
}

bool FileWatcher::start() {
    if (running_.load()) {
        return true;  // Already running
    }
    
    // Initialize inotify
    inotify_fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd_ < 0) {
        Logger::error("[FileWatcher] Failed to initialize inotify: " + std::string(strerror(errno)));
        return false;
    }
    
    running_.store(true);
    
    try {
        // Start the watcher thread
        watcher_thread_ = std::thread(&FileWatcher::watch_loop, this);
        
        // Start the debounce thread
        debounce_thread_ = std::thread(&FileWatcher::debounce_loop, this);
        
        Logger::info("[FileWatcher] Started file watcher (inotify fd=" + std::to_string(inotify_fd_) + ")");
        return true;
    } catch (const std::system_error& e) {
        Logger::error("[FileWatcher] Failed to create threads: " + std::string(e.what()));
        running_.store(false);
        if (inotify_fd_ >= 0) {
            close(inotify_fd_);
            inotify_fd_ = -1;
        }
        return false;
    } catch (const std::exception& e) {
        Logger::error("[FileWatcher] Unexpected exception starting watcher: " + std::string(e.what()));
        running_.store(false);
        if (inotify_fd_ >= 0) {
            close(inotify_fd_);
            inotify_fd_ = -1;
        }
        return false;
    }
}

void FileWatcher::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    // Wake up the watcher thread by closing the fd
    if (inotify_fd_ >= 0) {
        // Remove all watches first
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& pair : wd_to_job_) {
            inotify_rm_watch(inotify_fd_, pair.first);
        }
        wd_to_job_.clear();
        job_to_wds_.clear();
        job_to_path_.clear();
        wd_to_path_.clear();
    }
    
    // Wait for threads to finish
    if (watcher_thread_.joinable()) {
        watcher_thread_.join();
    }
    if (debounce_thread_.joinable()) {
        debounce_thread_.join();
    }
    
    if (inotify_fd_ >= 0) {
        close(inotify_fd_);
        inotify_fd_ = -1;
    }
    
    Logger::info("[FileWatcher] Stopped file watcher");
}

bool FileWatcher::add_watch(const std::string& job_id, const std::string& path) {
    if (inotify_fd_ < 0) {
        Logger::error("[FileWatcher] Cannot add watch - inotify not initialized");
        return false;
    }
    
    // Remove existing watch for this job if any
    remove_watch(job_id);
    
    std::lock_guard<std::mutex> lock(mutex_);
    job_to_path_[job_id] = path;
    
    // Add recursive watches
    if (!add_watch_recursive(job_id, path)) {
        Logger::error("[FileWatcher] Failed to add watch for job " + job_id + " path: " + path);
        return false;
    }
    
    size_t watch_count = job_to_wds_[job_id].size();
    Logger::info("[FileWatcher] Added watch for job " + job_id + " (" + std::to_string(watch_count) + " directories)");
    return true;
}

bool FileWatcher::add_watch_recursive(const std::string& job_id, const std::string& path) {
    // Add watch for this directory
    int wd = inotify_add_watch(inotify_fd_, path.c_str(), WATCH_EVENTS);
    if (wd < 0) {
        if (errno == ENOENT) {
            // Directory doesn't exist, that's okay
            return true;
        }
        if (errno == ENOSPC) {
            Logger::error("[FileWatcher] inotify watch limit reached! Increase /proc/sys/fs/inotify/max_user_watches");
            return false;
        }
        if (errno == EACCES) {
            // Permission denied - skip silently (common for postgres-data, docker volumes, etc.)
            Logger::debug("[FileWatcher] Skipping (permission denied): " + path);
            return true;  // Continue with other directories
        }
        Logger::warn("[FileWatcher] Failed to watch " + path + ": " + std::string(strerror(errno)));
        return true;  // Continue anyway, don't fail the whole job
    }
    
    wd_to_job_[wd] = job_id;
    wd_to_path_[wd] = path;
    job_to_wds_[job_id].insert(wd);
    
    // Recursively add watches for subdirectories
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        return true;  // Can't open, but we did add the parent watch
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Skip hidden files/directories (like .git, .rclone, etc.)
        if (entry->d_name[0] == '.') {
            continue;
        }
        
        std::string full_path = path + "/" + entry->d_name;
        
        struct stat st;
        if (stat(full_path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            // Recursively watch subdirectories
            add_watch_recursive(job_id, full_path);
        }
    }
    
    closedir(dir);
    return true;
}

void FileWatcher::remove_watch(const std::string& job_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = job_to_wds_.find(job_id);
    if (it == job_to_wds_.end()) {
        return;
    }
    
    // Remove all watch descriptors for this job
    for (int wd : it->second) {
        if (inotify_fd_ >= 0) {
            inotify_rm_watch(inotify_fd_, wd);
        }
        wd_to_job_.erase(wd);
        wd_to_path_.erase(wd);
    }
    
    job_to_wds_.erase(job_id);
    job_to_path_.erase(job_id);
    
    // Clear any pending sync
    {
        std::lock_guard<std::mutex> plock(pending_mutex_);
        pending_syncs_.erase(job_id);
    }
    
    Logger::info("[FileWatcher] Removed watch for job " + job_id);
}

bool FileWatcher::is_watching(const std::string& job_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return job_to_wds_.find(job_id) != job_to_wds_.end();
}

void FileWatcher::set_sync_callback(SyncCallback callback) {
    sync_callback_ = std::move(callback);
}

void FileWatcher::set_debounce_delay(int seconds) {
    debounce_seconds_ = seconds;
}

std::string FileWatcher::get_status(const std::string& job_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = job_to_wds_.find(job_id);
    if (it == job_to_wds_.end()) {
        return "Not watching";
    }
    
    return "Watching (" + std::to_string(it->second.size()) + " dirs)";
}

void FileWatcher::watch_loop() {
    char buffer[EVENT_BUF_LEN];
    
    Logger::info("[FileWatcher] Watch loop started");
    
    while (running_.load()) {
        // Use poll to wait for events with a timeout
        struct pollfd pfd;
        pfd.fd = inotify_fd_;
        pfd.events = POLLIN;
        
        int poll_result = poll(&pfd, 1, 500);  // 500ms timeout
        
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;  // Interrupted, try again
            }
            Logger::error("[FileWatcher] poll() error: " + std::string(strerror(errno)));
            break;
        }
        
        if (poll_result == 0) {
            // Timeout, no events - just loop and check running_
            continue;
        }
        
        // Read events
        ssize_t len = read(inotify_fd_, buffer, EVENT_BUF_LEN);
        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;  // No data available
            }
            if (errno == EINTR) {
                continue;  // Interrupted
            }
            Logger::error("[FileWatcher] read() error: " + std::string(strerror(errno)));
            break;
        }
        
        // Process events
        ssize_t i = 0;
        while (i < len) {
            struct inotify_event* event = reinterpret_cast<struct inotify_event*>(buffer + i);
            
            if (event->len > 0) {
                handle_event(event->wd, event->mask, event->name);
            } else {
                handle_event(event->wd, event->mask, nullptr);
            }
            
            i += sizeof(struct inotify_event) + event->len;
        }
    }
    
    Logger::info("[FileWatcher] Watch loop ended");
}

void FileWatcher::handle_event(int wd, uint32_t mask, const char* name) {
    std::string job_id;
    std::string dir_path;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = wd_to_job_.find(wd);
        if (it == wd_to_job_.end()) {
            return;  // Unknown watch descriptor
        }
        job_id = it->second;
        
        auto path_it = wd_to_path_.find(wd);
        if (path_it != wd_to_path_.end()) {
            dir_path = path_it->second;
        }
    }
    
    // Skip temporary files and common editor backup files
    if (name != nullptr) {
        std::string filename(name);
        
        // Skip hidden files (start with .)
        if (!filename.empty() && filename[0] == '.') {
            return;
        }
        
        // Skip common temporary file patterns
        if (filename.find(".swp") != std::string::npos ||
            filename.find(".tmp") != std::string::npos ||
            filename.find("~") != std::string::npos ||
            filename.find(".part") != std::string::npos) {
            return;
        }
    }
    
    // If a new directory was created, add a watch for it
    if ((mask & IN_CREATE) && (mask & IN_ISDIR) && name != nullptr) {
        std::string new_dir = dir_path + "/" + std::string(name);
        Logger::debug("[FileWatcher] New directory created: " + new_dir);
        
        std::lock_guard<std::mutex> lock(mutex_);
        add_watch_recursive(job_id, new_dir);
    }
    
    // Log the event (debug level to avoid spam)
    std::string event_type;
    if (mask & IN_CREATE) event_type = "CREATE";
    else if (mask & IN_DELETE) event_type = "DELETE";
    else if (mask & IN_MODIFY) event_type = "MODIFY";
    else if (mask & IN_CLOSE_WRITE) event_type = "CLOSE_WRITE";
    else if (mask & IN_MOVED_FROM) event_type = "MOVED_FROM";
    else if (mask & IN_MOVED_TO) event_type = "MOVED_TO";
    else event_type = "OTHER";
    
    std::string filename = name ? name : "(dir)";
    Logger::debug("[FileWatcher] Event: " + event_type + " - " + filename + " (job: " + job_id + ")");
    
    // Schedule a sync for this job
    schedule_sync(job_id);
}

void FileWatcher::schedule_sync(const std::string& job_id) {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    
    // Update the trigger time (this provides debouncing - we keep pushing it forward)
    bool is_new = pending_syncs_.find(job_id) == pending_syncs_.end();
    pending_syncs_[job_id] = std::chrono::steady_clock::now();
    
    if (is_new) {
        Logger::debug("[FileWatcher] Scheduled sync for job " + job_id + " (waiting " + std::to_string(debounce_seconds_) + "s)");
    }
}

void FileWatcher::debounce_loop() {
    Logger::info("[FileWatcher] Debounce loop started (delay: " + std::to_string(debounce_seconds_) + "s)");
    
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        auto now = std::chrono::steady_clock::now();
        std::vector<std::string> ready_jobs;
        
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            
            for (auto it = pending_syncs_.begin(); it != pending_syncs_.end(); ) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second);
                
                if (elapsed.count() >= debounce_seconds_) {
                    // Enough time has passed since the last change, trigger sync
                    ready_jobs.push_back(it->first);
                    it = pending_syncs_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        
        // Trigger syncs for ready jobs
        for (const auto& job_id : ready_jobs) {
            Logger::info("[FileWatcher] Triggering sync for job " + job_id + " (file changes detected)");
            
            if (sync_callback_) {
                try {
                    sync_callback_(job_id);
                } catch (const std::exception& e) {
                    Logger::error("[FileWatcher] Sync callback error: " + std::string(e.what()));
                }
            }
        }
    }
    
    Logger::info("[FileWatcher] Debounce loop ended");
}

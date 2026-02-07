// file_watcher.hpp - inotify-based file watcher for real-time sync
// Uses native Linux inotify API (kernel 2.6.13+, universally available)

#ifndef FILE_WATCHER_HPP
#define FILE_WATCHER_HPP

#include <string>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>

class FileWatcher {
public:
    using SyncCallback = std::function<void(const std::string& job_id)>;
    
    FileWatcher();
    ~FileWatcher();
    
    // Start watching a directory for a specific job
    // Returns true if successfully started watching
    bool add_watch(const std::string& job_id, const std::string& path);
    
    // Stop watching a job's directory
    void remove_watch(const std::string& job_id);
    
    // Check if a job is being watched
    bool is_watching(const std::string& job_id) const;
    
    // Set the callback to trigger when files change
    void set_sync_callback(SyncCallback callback);
    
    // Set debounce delay in seconds (default 3 seconds)
    void set_debounce_delay(int seconds);
    
    // Start the watcher thread
    bool start();
    
    // Stop the watcher thread
    void stop();
    
    // Check if watcher is running
    bool is_running() const { return running_.load(); }
    
    // Get status info for a job
    std::string get_status(const std::string& job_id) const;

private:
    // inotify file descriptor
    int inotify_fd_;
    
    // Map from watch descriptor to job_id
    std::unordered_map<int, std::string> wd_to_job_;
    
    // Map from job_id to watch descriptors (multiple for recursive)
    std::unordered_map<std::string, std::unordered_set<int>> job_to_wds_;
    
    // Map from job_id to root path
    std::unordered_map<std::string, std::string> job_to_path_;
    
    // Map from watch descriptor to path (for recursive watching)
    std::unordered_map<int, std::string> wd_to_path_;
    
    // Pending syncs with their trigger times (for debouncing)
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> pending_syncs_;
    
    // Callback to trigger sync
    SyncCallback sync_callback_;
    
    // Debounce delay
    int debounce_seconds_;
    
    // Thread management
    std::thread watcher_thread_;
    std::thread debounce_thread_;
    std::atomic<bool> running_;
    mutable std::mutex mutex_;
    std::mutex pending_mutex_;
    
    // Add recursive watches for a directory
    bool add_watch_recursive(const std::string& job_id, const std::string& path);
    
    // Main watcher loop
    void watch_loop();
    
    // Debounce loop - checks pending syncs and triggers them
    void debounce_loop();
    
    // Handle an inotify event
    void handle_event(int wd, uint32_t mask, const char* name);
    
    // Schedule a sync for a job (with debouncing)
    void schedule_sync(const std::string& job_id);
};

#endif // FILE_WATCHER_HPP

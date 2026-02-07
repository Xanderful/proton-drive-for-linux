#pragma once

#include <string>
#include <chrono>
#include <vector>
#include <mutex>
#include <atomic>
#include <deque>
#include <map>

namespace proton {

/**
 * Transfer type
 */
enum class TransferType {
    UPLOAD,
    DOWNLOAD
};

/**
 * Single transfer record
 */
struct TransferRecord {
    std::string filename;
    TransferType type;
    size_t bytes;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    bool completed = false;
    bool success = false;
    std::string error;
};

/**
 * Bandwidth Monitor
 * 
 * Tracks transfer speeds and provides statistics similar to 
 * Nextcloud/ownCloud desktop clients:
 * - Real-time upload/download speeds
 * - Recent transfer history
 * - Bandwidth throttling (optional)
 * - Transfer queue monitoring
 */
class BandwidthMonitor {
public:
    static BandwidthMonitor& getInstance();
    
    // Record transfer events
    void start_transfer(const std::string& id, const std::string& filename,
                       TransferType type, size_t total_bytes);
    void update_progress(const std::string& id, size_t bytes_transferred);
    void complete_transfer(const std::string& id, bool success, 
                          const std::string& error = "");
    
    // Get current speeds (bytes per second)
    double get_current_upload_speed() const;
    double get_current_download_speed() const;
    
    // Get formatted speed strings
    std::string get_upload_speed_string() const;
    std::string get_download_speed_string() const;
    
    // Get transfer counts
    int get_active_uploads() const { return active_uploads_.load(); }
    int get_active_downloads() const { return active_downloads_.load(); }
    int get_pending_transfers() const { return pending_transfers_.load(); }
    
    // Get recent transfer history
    std::vector<TransferRecord> get_recent_transfers(size_t limit = 10) const;
    
    // Get cumulative stats
    struct CumulativeStats {
        size_t total_uploaded = 0;
        size_t total_downloaded = 0;
        int files_uploaded = 0;
        int files_downloaded = 0;
        int errors = 0;
        std::chrono::steady_clock::time_point session_start;
    };
    CumulativeStats get_session_stats() const;
    
    // Bandwidth throttling (0 = unlimited)
    void set_upload_limit(size_t bytes_per_second);
    void set_download_limit(size_t bytes_per_second);
    size_t get_upload_limit() const { return upload_limit_.load(); }
    size_t get_download_limit() const { return download_limit_.load(); }
    
    // Reset session stats
    void reset_session();

private:
    BandwidthMonitor();
    ~BandwidthMonitor() = default;
    
    BandwidthMonitor(const BandwidthMonitor&) = delete;
    BandwidthMonitor& operator=(const BandwidthMonitor&) = delete;
    
    // Speed calculation using sliding window
    void add_speed_sample(TransferType type, size_t bytes, 
                         std::chrono::milliseconds duration);
    double calculate_speed(const std::deque<std::pair<size_t, std::chrono::steady_clock::time_point>>& samples) const;
    
    mutable std::mutex mutex_;
    
    // Active transfers
    std::map<std::string, TransferRecord> active_transfers_;
    
    // Recent completed transfers
    std::deque<TransferRecord> completed_transfers_;
    static constexpr size_t MAX_HISTORY = 100;
    
    // Speed samples (rolling window)
    std::deque<std::pair<size_t, std::chrono::steady_clock::time_point>> upload_samples_;
    std::deque<std::pair<size_t, std::chrono::steady_clock::time_point>> download_samples_;
    static constexpr size_t SPEED_WINDOW_SECONDS = 5;
    
    // Counters
    std::atomic<int> active_uploads_{0};
    std::atomic<int> active_downloads_{0};
    std::atomic<int> pending_transfers_{0};
    
    // Cumulative stats
    CumulativeStats session_stats_;
    
    // Throttling limits (0 = unlimited)
    std::atomic<size_t> upload_limit_{0};
    std::atomic<size_t> download_limit_{0};
};

/**
 * Format speed to human readable string (e.g., "1.5 MB/s")
 */
std::string format_speed(double bytes_per_second);

} // namespace proton

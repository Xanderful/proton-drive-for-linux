#include "bandwidth_monitor.hpp"
#include "logger.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace proton {

BandwidthMonitor::BandwidthMonitor() {
    session_stats_.session_start = std::chrono::steady_clock::now();
}

BandwidthMonitor& BandwidthMonitor::getInstance() {
    static BandwidthMonitor instance;
    return instance;
}

void BandwidthMonitor::start_transfer(const std::string& id, const std::string& filename,
                                       TransferType type, size_t total_bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    TransferRecord record;
    record.filename = filename;
    record.type = type;
    record.bytes = total_bytes;
    record.start_time = std::chrono::steady_clock::now();
    record.completed = false;
    
    active_transfers_[id] = record;
    
    if (type == TransferType::UPLOAD) {
        active_uploads_++;
    } else {
        active_downloads_++;
    }
    
    Logger::debug("[Bandwidth] Started " + 
                 std::string(type == TransferType::UPLOAD ? "upload" : "download") +
                 ": " + filename);
}

void BandwidthMonitor::update_progress(const std::string& id, size_t bytes_transferred) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = active_transfers_.find(id);
    if (it == active_transfers_.end()) return;
    
    auto& record = it->second;
    auto now = std::chrono::steady_clock::now();
    
    // Calculate bytes since last update for speed calculation
    // For simplicity, we use a rolling window approach
    if (record.type == TransferType::UPLOAD) {
        upload_samples_.push_back({bytes_transferred, now});
    } else {
        download_samples_.push_back({bytes_transferred, now});
    }
    
    // Clean old samples outside the window
    auto cutoff = now - std::chrono::seconds(SPEED_WINDOW_SECONDS);
    while (!upload_samples_.empty() && upload_samples_.front().second < cutoff) {
        upload_samples_.pop_front();
    }
    while (!download_samples_.empty() && download_samples_.front().second < cutoff) {
        download_samples_.pop_front();
    }
}

void BandwidthMonitor::complete_transfer(const std::string& id, bool success, 
                                          const std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = active_transfers_.find(id);
    if (it == active_transfers_.end()) return;
    
    auto record = it->second;
    record.end_time = std::chrono::steady_clock::now();
    record.completed = true;
    record.success = success;
    record.error = error;
    
    // Update counters
    if (record.type == TransferType::UPLOAD) {
        active_uploads_--;
        if (success) {
            session_stats_.total_uploaded += record.bytes;
            session_stats_.files_uploaded++;
        }
    } else {
        active_downloads_--;
        if (success) {
            session_stats_.total_downloaded += record.bytes;
            session_stats_.files_downloaded++;
        }
    }
    
    if (!success) {
        session_stats_.errors++;
    }
    
    // Add to history
    completed_transfers_.push_back(record);
    while (completed_transfers_.size() > MAX_HISTORY) {
        completed_transfers_.pop_front();
    }
    
    active_transfers_.erase(it);
    
    Logger::debug("[Bandwidth] Completed " + 
                 std::string(record.type == TransferType::UPLOAD ? "upload" : "download") +
                 ": " + record.filename + (success ? " (success)" : " (failed)"));
}

double BandwidthMonitor::calculate_speed(
    const std::deque<std::pair<size_t, std::chrono::steady_clock::time_point>>& samples) const {
    
    if (samples.size() < 2) return 0.0;
    
    auto now = std::chrono::steady_clock::now();
    auto cutoff = now - std::chrono::seconds(SPEED_WINDOW_SECONDS);
    
    size_t total_bytes = 0;
    for (const auto& sample : samples) {
        if (sample.second >= cutoff) {
            total_bytes += sample.first;
        }
    }
    
    // Calculate speed over the window period
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - samples.front().second);
    
    if (duration.count() > 0) {
        return (total_bytes * 1000.0) / duration.count();
    }
    return 0.0;
}

double BandwidthMonitor::get_current_upload_speed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return calculate_speed(upload_samples_);
}

double BandwidthMonitor::get_current_download_speed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return calculate_speed(download_samples_);
}

std::string BandwidthMonitor::get_upload_speed_string() const {
    return format_speed(get_current_upload_speed());
}

std::string BandwidthMonitor::get_download_speed_string() const {
    return format_speed(get_current_download_speed());
}

std::vector<TransferRecord> BandwidthMonitor::get_recent_transfers(size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<TransferRecord> result;
    size_t count = std::min(limit, completed_transfers_.size());
    
    auto it = completed_transfers_.rbegin();
    for (size_t i = 0; i < count && it != completed_transfers_.rend(); ++i, ++it) {
        result.push_back(*it);
    }
    
    return result;
}

BandwidthMonitor::CumulativeStats BandwidthMonitor::get_session_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return session_stats_;
}

void BandwidthMonitor::set_upload_limit(size_t bytes_per_second) {
    upload_limit_.store(bytes_per_second);
    Logger::info("[Bandwidth] Upload limit set to " + 
                (bytes_per_second == 0 ? "unlimited" : format_speed(bytes_per_second)));
}

void BandwidthMonitor::set_download_limit(size_t bytes_per_second) {
    download_limit_.store(bytes_per_second);
    Logger::info("[Bandwidth] Download limit set to " + 
                (bytes_per_second == 0 ? "unlimited" : format_speed(bytes_per_second)));
}

void BandwidthMonitor::reset_session() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    session_stats_ = CumulativeStats();
    session_stats_.session_start = std::chrono::steady_clock::now();
    completed_transfers_.clear();
    upload_samples_.clear();
    download_samples_.clear();
    
    Logger::info("[Bandwidth] Session stats reset");
}

std::string format_speed(double bytes_per_second) {
    const char* units[] = {"B/s", "KB/s", "MB/s", "GB/s"};
    int unit_index = 0;
    double speed = bytes_per_second;
    
    while (speed >= 1024.0 && unit_index < 3) {
        speed /= 1024.0;
        unit_index++;
    }
    
    std::ostringstream oss;
    if (unit_index == 0) {
        oss << static_cast<int>(speed) << " " << units[unit_index];
    } else {
        oss << std::fixed << std::setprecision(1) << speed << " " << units[unit_index];
    }
    return oss.str();
}

} // namespace proton

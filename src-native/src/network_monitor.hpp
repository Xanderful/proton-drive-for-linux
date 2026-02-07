#pragma once

#include <string>
#include <functional>
#include <atomic>
#include <thread>

namespace proton {

/**
 * Network Monitor
 * 
 * Monitors network connectivity and status to:
 * - Pause sync when offline
 * - Detect metered connections
 * - Resume sync when back online
 */
class NetworkMonitor {
public:
    static NetworkMonitor& getInstance();
    
    // Start/stop monitoring
    void start();
    void stop();
    
    // Status queries
    bool is_online() const;
    bool is_metered() const;
    
    // Callbacks
    using StatusCallback = std::function<void(bool online, bool metered)>;
    void set_status_callback(StatusCallback callback);
    
    // Check connectivity (one-shot check)
    static bool check_connectivity();
    
private:
    NetworkMonitor() = default;
    ~NetworkMonitor();
    
    NetworkMonitor(const NetworkMonitor&) = delete;
    NetworkMonitor& operator=(const NetworkMonitor&) = delete;
    
    void monitor_loop();
    bool check_metered_connection();
    
    std::atomic<bool> online_{true};
    std::atomic<bool> metered_{false};
    std::atomic<bool> running_{false};
    std::thread monitor_thread_;
    StatusCallback callback_;
};

} // namespace proton

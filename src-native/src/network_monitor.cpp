#include "network_monitor.hpp"
#include "logger.hpp"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <unistd.h>
#include <limits.h>
#include <chrono>
#include <array>
#include <memory>

namespace proton {

// Helper to ensure valid working directory for popen() calls
static void ensure_valid_cwd() {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        // Current directory is invalid, switch to home or /tmp
        const char* home = std::getenv("HOME");
        if (home && chdir(home) == 0) {
            // Successfully switched
        } else if (chdir("/tmp") != 0) {
            // Both attempts failed, but we'll continue anyway
        }
    }
}

NetworkMonitor& NetworkMonitor::getInstance() {
    static NetworkMonitor instance;
    return instance;
}

NetworkMonitor::~NetworkMonitor() {
    stop();
}

void NetworkMonitor::start() {
    if (running_) return;
    
    running_ = true;
    try {
        monitor_thread_ = std::thread(&NetworkMonitor::monitor_loop, this);
        Logger::info("[NetworkMonitor] Started monitoring");
    } catch (const std::system_error& e) {
        Logger::error("[NetworkMonitor] Failed to create monitor thread: " + std::string(e.what()));
        running_ = false;
    } catch (const std::exception& e) {
        Logger::error("[NetworkMonitor] Unexpected exception starting monitor: " + std::string(e.what()));
        running_ = false;
    }
}

void NetworkMonitor::stop() {
    running_ = false;
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    Logger::info("[NetworkMonitor] Stopped monitoring");
}

bool NetworkMonitor::is_online() const {
    return online_;
}

bool NetworkMonitor::is_metered() const {
    return metered_;
}

void NetworkMonitor::set_status_callback(StatusCallback callback) {
    callback_ = std::move(callback);
}

bool NetworkMonitor::check_connectivity() {
    // Try multiple methods to check connectivity
    
    // Method 1: Check /sys/class/net for carrier status
    std::vector<std::string> interfaces = {"eth0", "enp0s3", "wlan0", "wlp2s0", "eno1"};
    
    for (const auto& iface : interfaces) {
        std::string carrier_path = "/sys/class/net/" + iface + "/carrier";
        std::ifstream carrier_file(carrier_path);
        if (carrier_file.good()) {
            std::string status;
            std::getline(carrier_file, status);
            if (status == "1") {
                return true;  // Interface has carrier
            }
        }
    }
    
    // Method 2: Try to reach a known endpoint (non-blocking ping)
    // Using curl with short timeout
    int result = std::system("curl -s --connect-timeout 2 -o /dev/null https://account.proton.me/api/ping 2>/dev/null");
    
    return (result == 0);
}

bool NetworkMonitor::check_metered_connection() {
    // Check NetworkManager for metered connection status
    // Uses nmcli to query
    ensure_valid_cwd();
    
    std::array<char, 128> buffer;
    std::string result;
    
    std::unique_ptr<FILE, decltype(&pclose)> pipe(
        popen("nmcli -t -f GENERAL.METERED dev show 2>/dev/null | head -1 | cut -d: -f2", "r"),
        pclose
    );
    
    if (pipe) {
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            result += buffer.data();
        }
        
        // Trim whitespace
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
            result.pop_back();
        }
        
        // "yes" or "guess-yes" indicates metered
        if (result == "yes" || result == "guess-yes") {
            return true;
        }
    }
    
    return false;
}

void NetworkMonitor::monitor_loop() {
    bool was_online = true;
    bool was_metered = false;
    
    while (running_) {
        // Check connectivity every 10 seconds
        bool now_online = check_connectivity();
        bool now_metered = check_metered_connection();
        
        // Update atomic flags
        online_ = now_online;
        metered_ = now_metered;
        
        // Notify on status change
        if (now_online != was_online || now_metered != was_metered) {
            if (now_online && !was_online) {
                Logger::info("[NetworkMonitor] Network connection restored");
            } else if (!now_online && was_online) {
                Logger::warn("[NetworkMonitor] Network connection lost");
            }
            
            if (now_metered && !was_metered) {
                Logger::info("[NetworkMonitor] Metered connection detected");
            }
            
            if (callback_) {
                callback_(now_online, now_metered);
            }
            
            was_online = now_online;
            was_metered = now_metered;
        }
        
        // Sleep for 10 seconds, but check running_ more frequently for fast shutdown
        for (int i = 0; i < 100 && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

} // namespace proton

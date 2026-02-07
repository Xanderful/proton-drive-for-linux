#include "device_identity.hpp"
#include "logger.hpp"
#include <fstream>
#include <sstream>
#include <random>
#include <iomanip>
#include <cstdlib>
#include <unistd.h>
#include <filesystem>
#include <array>
#include <memory>

namespace fs = std::filesystem;

DeviceIdentity& DeviceIdentity::getInstance() {
    static DeviceIdentity instance;
    return instance;
}

DeviceIdentity::DeviceIdentity() {
    // Config stored in ~/.config/proton-drive/device.json
    const char* home = std::getenv("HOME");
    if (home) {
        config_path_ = std::string(home) + "/.config/proton-drive/device.json";
    } else {
        config_path_ = "/tmp/proton-drive-device.json";
    }
    loadOrCreate();
}

void DeviceIdentity::loadOrCreate() {
    // Ensure directory exists
    fs::path config_dir = fs::path(config_path_).parent_path();
    if (!fs::exists(config_dir)) {
        fs::create_directories(config_dir);
    }
    
    // Try to load existing config
    std::ifstream file(config_path_);
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            // Simple JSON parsing (avoiding external dependency)
            if (line.find("\"device_id\"") != std::string::npos) {
                size_t start = line.find(": \"") + 3;
                size_t end = line.rfind("\"");
                if (start < end) {
                    device_id_ = line.substr(start, end - start);
                }
            } else if (line.find("\"device_name\"") != std::string::npos) {
                size_t start = line.find(": \"") + 3;
                size_t end = line.rfind("\"");
                if (start < end) {
                    device_name_ = line.substr(start, end - start);
                }
            } else if (line.find("\"first_seen\"") != std::string::npos) {
                size_t start = line.find(": ") + 2;
                size_t end = line.find(",", start);
                if (end == std::string::npos) end = line.find("}", start);
                if (start < end) {
                    first_seen_ = std::stol(line.substr(start, end - start));
                }
            }
        }
        file.close();
    }
    
    // Generate if not loaded
    if (device_id_.empty()) {
        device_id_ = generateDeviceId();
        device_name_ = detectHostname();
        first_seen_ = std::time(nullptr);
        save();
        Logger::info("[DeviceIdentity] Created new device ID: " + device_id_);
    } else {
        Logger::info("[DeviceIdentity] Loaded device ID: " + device_id_);
    }
}

void DeviceIdentity::save() const {
    std::ofstream file(config_path_);
    if (file.is_open()) {
        file << "{\n";
        file << "  \"device_id\": \"" << device_id_ << "\",\n";
        file << "  \"device_name\": \"" << device_name_ << "\",\n";
        file << "  \"first_seen\": " << first_seen_ << ",\n";
        file << "  \"machine_id\": \"" << getMachineId() << "\"\n";
        file << "}\n";
        file.close();
    }
}

std::string DeviceIdentity::generateDeviceId() const {
    // Combine machine-id with random component for uniqueness
    std::string machine_id = getMachineId();
    
    // Generate random suffix
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    // Use first 8 chars of machine-id hash
    std::hash<std::string> hasher;
    size_t hash = hasher(machine_id);
    ss << std::hex << std::setfill('0') << std::setw(8) << (hash & 0xFFFFFFFF);
    ss << "-";
    
    // Add random component (8 hex chars)
    for (int i = 0; i < 8; i++) {
        ss << std::hex << dis(gen);
    }
    
    return ss.str();
}

std::string DeviceIdentity::detectHostname() const {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return std::string(hostname);
    }
    return "unknown-device";
}

std::string DeviceIdentity::getMachineId() const {
    // Try /etc/machine-id first (systemd)
    std::ifstream machine_id_file("/etc/machine-id");
    if (machine_id_file.is_open()) {
        std::string id;
        std::getline(machine_id_file, id);
        machine_id_file.close();
        if (!id.empty()) {
            return id;
        }
    }
    
    // Fallback to /var/lib/dbus/machine-id
    std::ifstream dbus_id_file("/var/lib/dbus/machine-id");
    if (dbus_id_file.is_open()) {
        std::string id;
        std::getline(dbus_id_file, id);
        dbus_id_file.close();
        if (!id.empty()) {
            return id;
        }
    }
    
    // Last resort: use hostname + username
    std::string fallback = detectHostname();
    const char* user = std::getenv("USER");
    if (user) {
        fallback += "-" + std::string(user);
    }
    return fallback;
}

std::string DeviceIdentity::getDeviceId() const {
    return device_id_;
}

std::string DeviceIdentity::getDeviceName() const {
    return device_name_;
}

void DeviceIdentity::setDeviceName(const std::string& name) {
    device_name_ = name;
    save();
}

std::string DeviceIdentity::getDeviceFingerprint() const {
    return getMachineId();
}

std::time_t DeviceIdentity::getFirstSeenTimestamp() const {
    return first_seen_;
}

bool DeviceIdentity::isSameDevice(const std::string& other_device_id) const {
    return device_id_ == other_device_id;
}

std::string DeviceIdentity::toJson() const {
    std::stringstream ss;
    ss << "{";
    ss << "\"device_id\":\"" << device_id_ << "\",";
    ss << "\"device_name\":\"" << device_name_ << "\",";
    ss << "\"first_seen\":" << first_seen_;
    ss << "}";
    return ss.str();
}

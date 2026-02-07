#ifndef DEVICE_IDENTITY_HPP
#define DEVICE_IDENTITY_HPP

#include <string>
#include <ctime>

/**
 * DeviceIdentity - Manages unique device identification for multi-device sync
 * 
 * Problem: When multiple computers sync folders with the same name (e.g., ~/Downloads),
 * we need to distinguish between:
 *   1. Same folder synced to multiple devices (expected, no conflict)
 *   2. Different folders with same name from different devices (potential conflict)
 * 
 * Solution: Each device gets a unique ID, and sync jobs track which device created them.
 */
class DeviceIdentity {
public:
    static DeviceIdentity& getInstance();
    
    // Get or generate the device's unique ID
    std::string getDeviceId() const;
    
    // Get a human-readable device name (hostname or user-set name)
    std::string getDeviceName() const;
    
    // Set a custom device name
    void setDeviceName(const std::string& name);
    
    // Get device fingerprint (combination of hardware identifiers)
    std::string getDeviceFingerprint() const;
    
    // Full device info as JSON for sync metadata
    std::string toJson() const;
    
    // Check if this device ID matches another
    bool isSameDevice(const std::string& other_device_id) const;
    
    // Get the timestamp when this device was first registered
    std::time_t getFirstSeenTimestamp() const;

private:
    DeviceIdentity();
    ~DeviceIdentity() = default;
    DeviceIdentity(const DeviceIdentity&) = delete;
    DeviceIdentity& operator=(const DeviceIdentity&) = delete;
    
    void loadOrCreate();
    void save() const;
    std::string generateDeviceId() const;
    std::string detectHostname() const;
    std::string getMachineId() const;
    
    std::string device_id_;
    std::string device_name_;
    std::time_t first_seen_;
    std::string config_path_;
};

#endif // DEVICE_IDENTITY_HPP

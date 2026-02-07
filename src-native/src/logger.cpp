#include "logger.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>

LogLevel Logger::current_level = LogLevel::INFO;
std::ofstream Logger::log_file;
std::mutex Logger::log_mutex;

void Logger::init(LogLevel level, const std::string& log_file_path) {
    std::lock_guard<std::mutex> lock(log_mutex);
    current_level = level;
    if (!log_file_path.empty()) {
        log_file.open(log_file_path, std::ios::app);
    }
}

void Logger::log(LogLevel level, const std::string& message) {
    if (level < current_level) return;

    std::lock_guard<std::mutex> lock(log_mutex);
    
    // Timestamp
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    
    std::string level_str;
    switch (level) {
        case LogLevel::DEBUG: level_str = "[DEBUG]"; break;
        case LogLevel::INFO:  level_str = "[INFO] "; break;
        case LogLevel::WARN:  level_str = "[WARN] "; break;
        case LogLevel::ERROR: level_str = "[ERROR]"; break;
    }

    if (log_file.is_open()) {
        log_file << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") 
                 << " " << level_str << " " << message << std::endl;
    }
    
    std::cout << std::put_time(std::localtime(&time), "%H:%M:%S") 
              << " " << level_str << " " << message << std::endl;
}

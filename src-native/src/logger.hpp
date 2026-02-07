#pragma once
#include <string>
#include <fstream>
#include <mutex>

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

class Logger {
public:
    static void init(LogLevel level, const std::string& log_file_path = "");
    static void log(LogLevel level, const std::string& message);
    
    static void debug(const std::string& message) { log(LogLevel::DEBUG, message); }
    static void info(const std::string& message) { log(LogLevel::INFO, message); }
    static void warn(const std::string& message) { log(LogLevel::WARN, message); }
    static void error(const std::string& message) { log(LogLevel::ERROR, message); }

private:
    static LogLevel current_level;
    static std::ofstream log_file;
    static std::mutex log_mutex;
};

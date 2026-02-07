#pragma once

#include <string>
#include <utility>
#include <cstdint>

namespace AppWindowHelpers {

/**
 * Escape a string for safe use in shell commands.
 * Uses single-quoting and escapes embedded single quotes.
 * This is the safest way to pass untrusted data to shell commands.
 */
std::string shell_escape(const std::string& arg);

/**
 * Get path to rclone binary (AppImage bundled or system)
 */
std::string get_rclone_path();

/**
 * Execute rclone command and capture output
 */
std::string exec_rclone(const std::string& args);

/**
 * Execute rclone command with a timeout (in seconds).
 * Returns empty string if timeout exceeded or command fails.
 */
std::string exec_rclone_with_timeout(const std::string& args, int timeout_seconds);

/**
 * Run rclone command without capturing output
 */
int run_rclone(const std::string& args);

/**
 * Execute shell command and capture output
 */
std::string exec_command(const char* cmd);

/**
 * Run shell command without capturing output
 */
int run_system(const std::string& cmd);

/**
 * Get sync status for a cloud path
 * Returns {status_text, css_class} or empty if cloud-only
 */
std::pair<std::string, std::string> get_sync_status_for_path(const std::string& cloud_path);

/**
 * Ensure valid working directory for shell commands
 */
void ensure_valid_cwd_for_shell();

/**
 * Check if rclone proton profile exists
 */
bool has_rclone_profile();

/**
 * Run rclone config create for proton profile
 */
bool run_rclone_config_create(const std::string& username,
                               const std::string& password,
                               const std::string& twofa,
                               std::string* output,
                               std::string* error_message);

/**
 * Format file size for display
 */
std::string format_file_size(int64_t size);

/**
 * Safe filesystem operations that never throw exceptions.
 * These wrappers use std::error_code to handle I/O errors gracefully.
 * Use these instead of direct fs::exists(), fs::is_directory(), etc.
 */

/**
 * Safely check if a path exists (returns false on I/O errors)
 */
bool safe_exists(const std::string& path);

/**
 * Check if path definitely doesn't exist (returns true only if confirmed not to exist)
 * Returns false if path exists OR if there was an I/O error (unknown state)
 * Use this for safe deletion decisions - only delete when this returns true
 */
bool safe_definitely_missing(const std::string& path);

/**
 * Safely check if a path is a directory (returns false on I/O errors)
 */
bool safe_is_directory(const std::string& path);

/**
 * Safely check if a path is a regular file (returns false on I/O errors)
 */
bool safe_is_regular_file(const std::string& path);

} // namespace AppWindowHelpers

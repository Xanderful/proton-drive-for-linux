#include "file_index.hpp"
#include "logger.hpp"
#include "crypto.hpp"
#include <sqlite3.h>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <array>
#include <memory>
#include <regex>
#include <algorithm>
#include <ctime>
#include <cstring>
#include <iomanip>
#include <unistd.h>
#include <limits.h>
#include <openssl/rand.h>

namespace fs = std::filesystem;

// Safe filesystem helpers using error_code to prevent exceptions on I/O errors
static bool fi_safe_exists(const std::string& path) {
    std::error_code ec;
    return fs::exists(path, ec);
}
// Helper to ensure valid working directory for popen() calls
// Some processes (e.g., from deleted directories) may have invalid cwd
static void ensure_valid_cwd() {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        // Current directory is invalid, switch to home or /tmp
        const char* home = std::getenv("HOME");
        if (home && chdir(home) == 0) {
            Logger::debug("[FileIndex] Switched to HOME directory");
        } else if (chdir("/tmp") == 0) {
            Logger::debug("[FileIndex] Switched to /tmp directory");
        } else {
            Logger::warn("[FileIndex] Could not set valid working directory");
        }
    }
}

// Helper to find rclone binary - checks AppImage bundled location first, then system PATH
static std::string get_rclone_path() {
    const char* appdir = std::getenv("APPDIR");
    if (appdir) {
        std::string bundled_path = std::string(appdir) + "/usr/bin/rclone";
        if (fi_safe_exists(bundled_path)) {
            Logger::info("[FileIndex] Using bundled rclone: " + bundled_path);
            return bundled_path;
        }
    }

    // Check if rclone exists next to our executable (bundled in same dir)
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        std::string exe_dir = fs::path(exe_path).parent_path().string();
        
        // Same directory as executable (AppDir case)
        std::string same_dir_path = exe_dir + "/rclone";
        if (fi_safe_exists(same_dir_path)) {
            Logger::info("[FileIndex] Using bundled rclone: " + same_dir_path);
            return same_dir_path;
        }
        
        // Dev build: project_root/dist/AppDir/usr/bin/rclone
        // exe is at: project_root/src-native/build/proton-drive OR project_root/dist/AppDir/usr/bin/proton-drive
        fs::path exe_fs(exe_path);
        
        // If running from dist/AppDir/usr/bin, rclone is in same dir
        if (exe_fs.parent_path().filename() == "bin" && 
            exe_fs.parent_path().parent_path().filename() == "usr") {
            // Already checked above with same_dir_path
        }
        
        // If running from src-native/build, go up to project root
        std::string project_root = fs::path(exe_dir).parent_path().parent_path().string();
        std::string dev_path = project_root + "/dist/AppDir/usr/bin/rclone";
        if (fi_safe_exists(dev_path)) {
            Logger::info("[FileIndex] Using dev build rclone: " + dev_path);
            return dev_path;
        }
    }

    const std::vector<std::string> paths = {
        "./dist/AppDir/usr/bin/rclone",
        "/usr/bin/rclone",
        "/usr/local/bin/rclone",
        "/snap/bin/rclone",
    };

    for (const auto& p : paths) {
        if (fi_safe_exists(p)) {
            Logger::info("[FileIndex] Using system rclone: " + p);
            return p;
        }
    }

    Logger::info("[FileIndex] No explicit rclone path found, using PATH lookup");
    return "rclone";
}

// Helper to get current ISO8601 timestamp
static std::string get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time_t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

// Helper to parse ISO8601 timestamp
static std::chrono::system_clock::time_point parse_timestamp(const std::string& ts) {
    std::tm tm = {};
    std::istringstream iss(ts);
    iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

FileIndex& FileIndex::getInstance() {
    static FileIndex instance;
    return instance;
}

FileIndex::FileIndex() {
    // Set database path
    const char* home = getenv("HOME");
    if (home) {
        std::string cache_dir = std::string(home) + "/.cache/proton-drive";
        fs::create_directories(cache_dir);
        db_path_ = cache_dir + "/file_index.db";
    }
}

FileIndex::~FileIndex() {
    // If shutdown() wasn't called, close the DB
    // Note: Database will remain unencrypted on disk if app crashed
    // It will be encrypted on next clean shutdown
    if (!shutdown_complete_ && db_) {
        Logger::warn("[FileIndex] Destructor called without shutdown() - database remains unencrypted");
        sqlite3_close(static_cast<sqlite3*>(db_));
        db_ = nullptr;
    }
}

void FileIndex::shutdown() {
    if (shutdown_complete_) return;
    
    Logger::info("[FileIndex] Graceful shutdown initiated...");
    stop_background_index();
    
    if (db_) {
        // Checkpoint WAL to ensure all data is written to main database file
        Logger::info("[FileIndex] Checkpointing WAL...");
        sqlite3_exec(static_cast<sqlite3*>(db_), "PRAGMA wal_checkpoint(TRUNCATE);", nullptr, nullptr, nullptr);
        
        sqlite3_close(static_cast<sqlite3*>(db_));
        db_ = nullptr;
        Logger::info("[FileIndex] Database closed");
        
        // Encrypt database file if we have a key
        if (!encryption_key_.empty() && fi_safe_exists(db_path_)) {
            Logger::info("[FileIndex] Encrypting database...");
            if (crypto::encrypt_file(db_path_, std::vector<uint8_t>(encryption_key_.begin(), encryption_key_.end()))) {
                Logger::info("[FileIndex] Database encrypted successfully");
            } else {
                Logger::error("[FileIndex] Failed to encrypt database - file remains unencrypted");
            }
        }
    }
    
    shutdown_complete_ = true;
    Logger::info("[FileIndex] Shutdown complete");
}

bool FileIndex::initialize() {
    if (db_) return true;  // Already initialized
    
    Logger::info("[FileIndex] Initializing database at: " + db_path_);
    
    // Ensure cache directory exists
    fs::path db_dir = fs::path(db_path_).parent_path();
    if (!fi_safe_exists(db_dir)) {
        fs::create_directories(db_dir);
    }
    
    // Get or generate encryption key
    std::vector<uint8_t> key = crypto::retrieve_encrypted_key();
    if (key.empty()) {
        // First run or key lost - generate new random key
        Logger::info("[FileIndex] Generating new encryption key...");
        key.resize(crypto::KEY_SIZE);
        if (RAND_bytes(key.data(), crypto::KEY_SIZE) != 1) {
            Logger::error("[FileIndex] Failed to generate random encryption key");
            return false;
        }
        
        // Store encrypted key
        if (!crypto::store_encrypted_key(key)) {
            Logger::error("[FileIndex] Failed to store encryption key");
            return false;
        }
        Logger::info("[FileIndex] Encryption key generated and stored securely");
    } else {
        Logger::info("[FileIndex] Loaded existing encryption key");
    }
    
    // Convert key to string for internal storage
    encryption_key_ = std::string(key.begin(), key.end());
    
    // Check if database file exists and is encrypted
    bool db_exists = fi_safe_exists(db_path_);
    bool is_encrypted = db_exists && crypto::is_encrypted_file(db_path_);
    
    if (is_encrypted) {
        Logger::info("[FileIndex] Database is encrypted, decrypting...");
        if (!crypto::decrypt_file(db_path_, key)) {
            Logger::error("[FileIndex] Failed to decrypt database - key may be incorrect or file corrupted");
            // Try to recover by moving corrupted file and starting fresh
            std::string backup = db_path_ + ".corrupted." + std::to_string(time(nullptr));
            fs::rename(db_path_, backup);
            Logger::warn("[FileIndex] Corrupted database moved to: " + backup);
            Logger::info("[FileIndex] Starting with fresh database");
        } else {
            Logger::info("[FileIndex] Database decrypted successfully");
        }
    } else if (db_exists) {
        Logger::info("[FileIndex] Database exists but is not encrypted (will encrypt on shutdown)");
    } else {
        Logger::info("[FileIndex] No existing database - will create new encrypted database");
    }
    
    int rc = sqlite3_open(db_path_.c_str(), reinterpret_cast<sqlite3**>(&db_));
    if (rc != SQLITE_OK) {
        Logger::error("[FileIndex] Failed to open database: " + 
                      std::string(sqlite3_errmsg(static_cast<sqlite3*>(db_))));
        return false;
    }
    
    Logger::info("[FileIndex] Database opened successfully");
    
    // Set restrictive permissions (owner read/write only)
    fs::permissions(db_path_, 
                    fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace);
    
    // Enable WAL mode for better concurrency
    sqlite3_exec(static_cast<sqlite3*>(db_), "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    
    // Enable foreign keys
    sqlite3_exec(static_cast<sqlite3*>(db_), "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);
    
    bool tables_ok = create_tables();
    if (tables_ok) {
        // Log current stats
        auto stats = get_stats();
        Logger::info("[FileIndex] Loaded existing index: " + std::to_string(stats.total_files) + 
                    " files, " + std::to_string(stats.total_folders) + " folders");
    }
    return tables_ok;
}

bool FileIndex::create_tables() {
    sqlite3* db = static_cast<sqlite3*>(db_);
    char* err = nullptr;
    
    // Main files table
    const char* sql_files = R"(
        CREATE TABLE IF NOT EXISTS files (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            path TEXT NOT NULL UNIQUE,
            parent_path TEXT,
            size INTEGER DEFAULT -1,
            mod_time TEXT,
            is_directory INTEGER DEFAULT 0,
            is_synced INTEGER DEFAULT 0,
            local_path TEXT,
            extension TEXT,
            indexed_at TEXT DEFAULT CURRENT_TIMESTAMP,
            UNIQUE(path)
        );
        
        CREATE INDEX IF NOT EXISTS idx_files_parent ON files(parent_path);
        CREATE INDEX IF NOT EXISTS idx_files_extension ON files(extension);
        CREATE INDEX IF NOT EXISTS idx_files_synced ON files(is_synced);
        CREATE INDEX IF NOT EXISTS idx_files_name ON files(name);
    )";
    
    int rc = sqlite3_exec(db, sql_files, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        Logger::error("[FileIndex] Failed to create files table: " + std::string(err));
        sqlite3_free(err);
        return false;
    }
    
    // Full-text search virtual table (FTS5)
    const char* sql_fts = R"(
        CREATE VIRTUAL TABLE IF NOT EXISTS files_fts USING fts5(
            name, 
            path,
            extension,
            content='files',
            content_rowid='id',
            tokenize='porter unicode61'
        );
        
        -- Triggers to keep FTS in sync
        CREATE TRIGGER IF NOT EXISTS files_ai AFTER INSERT ON files BEGIN
            INSERT INTO files_fts(rowid, name, path, extension) 
            VALUES (new.id, new.name, new.path, new.extension);
        END;
        
        CREATE TRIGGER IF NOT EXISTS files_ad AFTER DELETE ON files BEGIN
            INSERT INTO files_fts(files_fts, rowid, name, path, extension) 
            VALUES ('delete', old.id, old.name, old.path, old.extension);
        END;
        
        CREATE TRIGGER IF NOT EXISTS files_au AFTER UPDATE ON files BEGIN
            INSERT INTO files_fts(files_fts, rowid, name, path, extension) 
            VALUES ('delete', old.id, old.name, old.path, old.extension);
            INSERT INTO files_fts(rowid, name, path, extension) 
            VALUES (new.id, new.name, new.path, new.extension);
        END;
    )";
    
    rc = sqlite3_exec(db, sql_fts, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        // FTS5 might fail on some systems, log but continue
        Logger::warn("[FileIndex] FTS5 setup warning: " + std::string(err));
        sqlite3_free(err);
        // Don't return false - basic search will still work
    }
    
    // Metadata table for tracking index state
    const char* sql_meta = R"(
        CREATE TABLE IF NOT EXISTS index_meta (
            key TEXT PRIMARY KEY,
            value TEXT
        );
        
        INSERT OR IGNORE INTO index_meta (key, value) VALUES ('last_full_index', '');
        INSERT OR IGNORE INTO index_meta (key, value) VALUES ('last_partial_index', '');
        INSERT OR IGNORE INTO index_meta (key, value) VALUES ('total_files', '0');
        INSERT OR IGNORE INTO index_meta (key, value) VALUES ('total_folders', '0');
    )";
    
    rc = sqlite3_exec(db, sql_meta, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        Logger::error("[FileIndex] Failed to create meta table: " + std::string(err));
        sqlite3_free(err);
        return false;
    }
    
    Logger::info("[FileIndex] Database initialized successfully");
    return true;
}

std::string FileIndex::get_extension(const std::string& filename) {
    size_t dot = filename.rfind('.');
    if (dot == std::string::npos || dot == 0) return "";
    std::string ext = filename.substr(dot + 1);
    // Convert to lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

std::vector<IndexedFile> FileIndex::search(const std::string& query, int limit, bool include_folders) {
    std::vector<IndexedFile> results;
    if (!db_ || query.empty()) return results;
    
    sqlite3* db = static_cast<sqlite3*>(db_);
    
    // Try FTS5 first, fall back to LIKE if it fails
    std::string sql;
    
    // Prepare query for FTS5 (escape special characters, add wildcards)
    std::string fts_query = query;
    // Replace * with FTS5 wildcard syntax
    std::replace(fts_query.begin(), fts_query.end(), '*', '*');
    
    // Check if we should use FTS5 or LIKE
    bool use_fts = true;
    
    if (use_fts) {
        sql = R"(
            SELECT f.id, f.name, f.path, f.parent_path, f.size, f.mod_time, 
                   f.is_directory, f.is_synced, f.local_path, f.extension,
                   bm25(files_fts) as relevance
            FROM files_fts 
            JOIN files f ON files_fts.rowid = f.id
            WHERE files_fts MATCH ?
        )";
        
        if (!include_folders) {
            sql += " AND f.is_directory = 0";
        }
        
        sql += " ORDER BY relevance";
        
        if (limit > 0) {
            sql += " LIMIT " + std::to_string(limit);
        }
    }
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        // FTS5 failed, try LIKE-based search
        Logger::debug("[FileIndex] FTS5 unavailable, using LIKE search");
        
        std::string like_pattern = "%" + query + "%";
        sql = R"(
            SELECT id, name, path, parent_path, size, mod_time, 
                   is_directory, is_synced, local_path, extension, 0.0 as relevance
            FROM files
            WHERE name LIKE ? OR path LIKE ?
        )";
        
        if (!include_folders) {
            sql += " AND is_directory = 0";
        }
        
        sql += " ORDER BY name";
        
        if (limit > 0) {
            sql += " LIMIT " + std::to_string(limit);
        }
        
        rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            Logger::error("[FileIndex] Search prepare failed: " + std::string(sqlite3_errmsg(db)));
            return results;
        }
        
        sqlite3_bind_text(stmt, 1, like_pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, like_pattern.c_str(), -1, SQLITE_TRANSIENT);
    } else {
        // FTS5 query - need to format properly
        std::string fts_formatted = "\"" + fts_query + "\"*";  // Prefix match
        sqlite3_bind_text(stmt, 1, fts_formatted.c_str(), -1, SQLITE_TRANSIENT);
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        IndexedFile file;
        file.id = sqlite3_column_int64(stmt, 0);
        file.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        file.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        
        const char* parent = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        file.parent_path = parent ? parent : "";
        
        file.size = sqlite3_column_int64(stmt, 4);
        
        const char* mod_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        file.mod_time = mod_time ? mod_time : "";
        
        file.is_directory = sqlite3_column_int(stmt, 6) != 0;
        file.is_synced = sqlite3_column_int(stmt, 7) != 0;
        
        const char* local = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        file.local_path = local ? local : "";
        
        const char* ext = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        file.extension = ext ? ext : "";
        
        file.relevance_score = sqlite3_column_double(stmt, 10);
        
        results.push_back(file);
    }
    
    sqlite3_finalize(stmt);
    
    Logger::debug("[FileIndex] Search for '" + query + "' returned " + 
                  std::to_string(results.size()) + " results");
    
    return results;
}

std::vector<IndexedFile> FileIndex::search_with_filters(
    const std::string& query,
    const std::string& extension_filter,
    const std::string& path_prefix,
    bool synced_only,
    bool cloud_only,
    int limit
) {
    std::vector<IndexedFile> results;
    if (!db_) return results;
    
    sqlite3* db = static_cast<sqlite3*>(db_);
    
    std::ostringstream sql;
    sql << "SELECT id, name, path, parent_path, size, mod_time, "
        << "is_directory, is_synced, local_path, extension, 0.0 as relevance "
        << "FROM files WHERE 1=1";
    
    std::vector<std::string> params;
    
    if (!query.empty()) {
        sql << " AND (name LIKE ? OR path LIKE ?)";
        std::string pattern = "%" + query + "%";
        params.push_back(pattern);
        params.push_back(pattern);
    }
    
    if (!extension_filter.empty()) {
        // Parse comma-separated extensions
        std::vector<std::string> exts;
        std::istringstream iss(extension_filter);
        std::string ext;
        while (std::getline(iss, ext, ',')) {
            // Trim whitespace
            ext.erase(0, ext.find_first_not_of(" "));
            ext.erase(ext.find_last_not_of(" ") + 1);
            if (!ext.empty()) {
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                exts.push_back(ext);
            }
        }
        
        if (!exts.empty()) {
            sql << " AND extension IN (";
            for (size_t i = 0; i < exts.size(); i++) {
                if (i > 0) sql << ",";
                sql << "?";
                params.push_back(exts[i]);
            }
            sql << ")";
        }
    }
    
    if (!path_prefix.empty()) {
        sql << " AND path LIKE ?";
        params.push_back(path_prefix + "%");
    }
    
    if (synced_only) {
        sql << " AND is_synced = 1";
    }
    
    if (cloud_only) {
        sql << " AND is_synced = 0";
    }
    
    sql << " ORDER BY name";
    
    if (limit > 0) {
        sql << " LIMIT " << limit;
    }
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql.str().c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::error("[FileIndex] Filter search prepare failed");
        return results;
    }
    
    for (size_t i = 0; i < params.size(); i++) {
        sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        IndexedFile file;
        file.id = sqlite3_column_int64(stmt, 0);
        file.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        file.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        
        const char* parent = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        file.parent_path = parent ? parent : "";
        
        file.size = sqlite3_column_int64(stmt, 4);
        
        const char* mod_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        file.mod_time = mod_time ? mod_time : "";
        
        file.is_directory = sqlite3_column_int(stmt, 6) != 0;
        file.is_synced = sqlite3_column_int(stmt, 7) != 0;
        
        const char* local = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        file.local_path = local ? local : "";
        
        const char* ext = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        file.extension = ext ? ext : "";
        
        file.relevance_score = 0.0;
        
        results.push_back(file);
    }
    
    sqlite3_finalize(stmt);
    return results;
}

std::vector<IndexedFile> FileIndex::get_directory_contents(const std::string& path) {
    std::vector<IndexedFile> results;
    if (!db_) return results;
    
    sqlite3* db = static_cast<sqlite3*>(db_);
    
    const char* sql = R"(
        SELECT id, name, path, parent_path, size, mod_time, 
               is_directory, is_synced, local_path, extension
        FROM files
        WHERE parent_path = ?
        ORDER BY is_directory DESC, name ASC
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return results;
    
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        IndexedFile file;
        file.id = sqlite3_column_int64(stmt, 0);
        file.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        file.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        
        const char* parent = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        file.parent_path = parent ? parent : "";
        
        file.size = sqlite3_column_int64(stmt, 4);
        
        const char* mod_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        file.mod_time = mod_time ? mod_time : "";
        
        file.is_directory = sqlite3_column_int(stmt, 6) != 0;
        file.is_synced = sqlite3_column_int(stmt, 7) != 0;
        
        const char* local = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        file.local_path = local ? local : "";
        
        const char* ext = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        file.extension = ext ? ext : "";
        
        file.relevance_score = 0.0;
        
        results.push_back(file);
    }
    
    sqlite3_finalize(stmt);
    return results;
}

std::vector<IndexedFile> FileIndex::get_recent_files(int limit) {
    std::vector<IndexedFile> results;
    if (!db_) return results;
    
    sqlite3* db = static_cast<sqlite3*>(db_);
    
    std::string sql = R"(
        SELECT id, name, path, parent_path, size, mod_time, 
               is_directory, is_synced, local_path, extension
        FROM files
        WHERE is_directory = 0
        ORDER BY mod_time DESC
        LIMIT ?
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return results;
    
    sqlite3_bind_int(stmt, 1, limit);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        IndexedFile file;
        file.id = sqlite3_column_int64(stmt, 0);
        file.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        file.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        
        const char* parent = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        file.parent_path = parent ? parent : "";
        
        file.size = sqlite3_column_int64(stmt, 4);
        
        const char* mod_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        file.mod_time = mod_time ? mod_time : "";
        
        file.is_directory = sqlite3_column_int(stmt, 6) != 0;
        file.is_synced = sqlite3_column_int(stmt, 7) != 0;
        
        const char* local = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        file.local_path = local ? local : "";
        
        const char* ext = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        file.extension = ext ? ext : "";
        
        file.relevance_score = 0.0;
        
        results.push_back(file);
    }
    
    sqlite3_finalize(stmt);
    return results;
}

IndexStats FileIndex::get_stats() {
    IndexStats stats = {};
    if (!db_) return stats;
    
    sqlite3* db = static_cast<sqlite3*>(db_);
    sqlite3_stmt* stmt;
    
    // Get file/folder counts
    const char* sql_counts = R"(
        SELECT 
            SUM(CASE WHEN is_directory = 0 THEN 1 ELSE 0 END) as files,
            SUM(CASE WHEN is_directory = 1 THEN 1 ELSE 0 END) as folders,
            SUM(CASE WHEN is_directory = 0 THEN size ELSE 0 END) as total_size
        FROM files
    )";
    
    if (sqlite3_prepare_v2(db, sql_counts, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            stats.total_files = sqlite3_column_int64(stmt, 0);
            stats.total_folders = sqlite3_column_int64(stmt, 1);
            stats.total_size_bytes = sqlite3_column_int64(stmt, 2);
        }
        sqlite3_finalize(stmt);
    }
    
    // Get last index times
    const char* sql_meta = "SELECT key, value FROM index_meta WHERE key IN ('last_full_index', 'last_partial_index')";
    if (sqlite3_prepare_v2(db, sql_meta, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const char* value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            if (key && value) {
                if (std::string(key) == "last_full_index") {
                    stats.last_full_index = value;
                } else if (std::string(key) == "last_partial_index") {
                    stats.last_partial_index = value;
                }
            }
        }
        sqlite3_finalize(stmt);
    }
    
    stats.is_indexing = is_indexing_.load();
    stats.index_progress_percent = index_progress_.load();
    stats.index_status = stats.is_indexing ? "Indexing..." : "Idle";
    
    return stats;
}

void FileIndex::start_background_index(bool full_reindex) {
    // Use atomic compare-and-exchange to prevent race condition
    bool expected = false;
    if (!is_indexing_.compare_exchange_strong(expected, true)) {
        Logger::warn("[FileIndex] Indexing already in progress - ignoring duplicate request");
        return;
    }
    
    // Wait for any previous thread
    if (index_thread_.joinable()) {
        Logger::debug("[FileIndex] start_background_index: joining previous thread");
        index_thread_.join();
    }
    
    stop_requested_ = false;
    index_progress_ = 0;
    
    Logger::info("[FileIndex] Started background indexing (full=" + 
                 std::string(full_reindex ? "true" : "false") + ")");
    Logger::debug("[FileIndex] Main thread ID: " + std::to_string(pthread_self()));
    
    try {
        index_thread_ = std::thread(&FileIndex::index_worker, this, full_reindex);
    } catch (const std::system_error& e) {
        Logger::error("[FileIndex] Failed to create index thread: " + std::string(e.what()));
        is_indexing_ = false;  // Reset the flag since thread creation failed
    } catch (const std::exception& e) {
        Logger::error("[FileIndex] Unexpected exception creating index thread: " + std::string(e.what()));
        is_indexing_ = false;  // Reset the flag since thread creation failed
    }
}

void FileIndex::stop_background_index() {
    stop_requested_ = true;
    if (index_thread_.joinable()) {
        index_thread_.join();
    }
    is_indexing_ = false;
}

void FileIndex::index_worker(bool full_reindex) {
    Logger::info("[FileIndex] index_worker started, db_=" + std::string(db_ ? "valid" : "null"));
    Logger::debug("[FileIndex] Worker thread ID: " + std::to_string(pthread_self()));
    Logger::debug("[FileIndex] full_reindex=" + std::string(full_reindex ? "true" : "false"));
    
    if (!db_) {
        Logger::error("[FileIndex] Database not initialized in worker thread");
        is_indexing_ = false;
        return;
    }
    
    sqlite3* db = static_cast<sqlite3*>(db_);
    
    report_progress(0, "Starting index...");
    
    if (full_reindex) {
        // Clear existing data
        report_progress(5, "Clearing old index...");
        sqlite3_exec(db, "DELETE FROM files;", nullptr, nullptr, nullptr);
    }
    
    report_progress(10, "Streaming file list from cloud...");
    Logger::info("[FileIndex] Starting rclone lsjson with streaming save (every 500 files)");
    
    // Ensure valid working directory before popen()
    ensure_valid_cwd();
    
    // Use rclone lsjson --recursive with --fast-list for better performance
    std::string cmd = get_rclone_path() + " lsjson --recursive --fast-list \"" + remote_name_ + ":/\" 2>/dev/null";
    
    std::array<char, 8192> buffer;  // Reasonable buffer size
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    
    if (!pipe) {
        Logger::error("[FileIndex] Failed to execute rclone");
        report_progress(100, "Error: rclone failed");
        is_indexing_ = false;
        return;
    }
    
    // Streaming parser state - minimal memory usage
    std::vector<IndexedFile> batch;
    batch.reserve(500);  // Pre-allocate batch size
    int total_saved = 0;
    int brace_depth = 0;
    bool in_string = false;
    bool escape_next = false;
    std::string current_object;
    current_object.reserve(2048);  // Reserve reasonable size for one JSON object
    auto start_time = std::chrono::steady_clock::now();
    
    const size_t BATCH_SIZE = 500;  // Save every 500 files
    
    // Fast manual JSON value extractors (replace regex for ~10x faster parsing)
    auto extract_string = [](const std::string& obj, const char* key, std::string& out) -> bool {
        std::string needle = std::string("\"") + key + "\":";
        size_t pos = obj.find(needle);
        if (pos == std::string::npos) return false;
        pos += needle.size();
        while (pos < obj.size() && (obj[pos] == ' ' || obj[pos] == '\t')) pos++;
        if (pos >= obj.size() || obj[pos] != '"') return false;
        pos++; // skip opening quote
        std::string result;
        result.reserve(128);
        while (pos < obj.size() && obj[pos] != '"') {
            if (obj[pos] == '\\' && pos + 1 < obj.size()) {
                pos++;
                switch (obj[pos]) {
                    case '"': result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/': result += '/'; break;
                    case 'n': result += '\n'; break;
                    case 't': result += '\t'; break;
                    default: result += obj[pos]; break;
                }
            } else {
                result += obj[pos];
            }
            pos++;
        }
        out = std::move(result);
        return true;
    };
    
    auto extract_int64 = [](const std::string& obj, const char* key, int64_t& out) -> bool {
        std::string needle = std::string("\"") + key + "\":";
        size_t pos = obj.find(needle);
        if (pos == std::string::npos) return false;
        pos += needle.size();
        while (pos < obj.size() && (obj[pos] == ' ' || obj[pos] == '\t')) pos++;
        size_t end = pos;
        if (end < obj.size() && obj[end] == '-') end++;
        while (end < obj.size() && std::isdigit(static_cast<unsigned char>(obj[end]))) end++;
        if (end == pos) return false;
        try { out = std::stoll(obj.substr(pos, end - pos)); return true; }
        catch (...) { return false; }
    };
    
    auto extract_bool = [](const std::string& obj, const char* key) -> bool {
        std::string needle = std::string("\"") + key + "\":";
        size_t pos = obj.find(needle);
        if (pos == std::string::npos) return false;
        pos += needle.size();
        while (pos < obj.size() && (obj[pos] == ' ' || obj[pos] == '\t')) pos++;
        return (pos + 3 < obj.size() && obj.substr(pos, 4) == "true");
    };
    
    Logger::info("[FileIndex] Reading rclone output stream...");
    
    // Process character by character directly from buffer - NO accumulation
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        if (stop_requested_) {
            // Save what we have before stopping
            if (!batch.empty()) {
                insert_files_batch(batch);
                total_saved += batch.size();
            }
            Logger::info("[FileIndex] Indexing cancelled, saved " + std::to_string(total_saved) + " files");
            is_indexing_ = false;
            return;
        }
        
        // Process this line directly - do NOT accumulate into json_buffer
        const char* ptr = buffer.data();
        while (*ptr) {
            char c = *ptr++;
            
            if (escape_next) {
                escape_next = false;
                if (brace_depth > 0) current_object += c;
                continue;
            }
            
            if (c == '\\' && in_string) {
                escape_next = true;
                if (brace_depth > 0) current_object += c;
                continue;
            }
            
            if (c == '"' && !escape_next) {
                in_string = !in_string;
            }
            
            if (!in_string) {
                if (c == '{') {
                    if (brace_depth == 0) {
                        current_object.clear();
                    }
                    brace_depth++;
                } else if (c == '}') {
                    brace_depth--;
                    if (brace_depth == 0) {
                        current_object += c;
                        
                        // Parse this complete JSON object using fast extractors
                        IndexedFile file;
                        std::string path_value;
                        
                        if (extract_string(current_object, "Path", path_value)) {
                            file.path = remote_name_ + ":/" + path_value;
                        }
                        extract_string(current_object, "Name", file.name);
                        extract_int64(current_object, "Size", file.size);
                        std::string mod_value;
                        if (extract_string(current_object, "ModTime", mod_value)) {
                            file.mod_time = mod_value.substr(0, 19);
                        }
                        file.is_directory = extract_bool(current_object, "IsDir");
                        
                        // Calculate parent path and extension
                        size_t last_slash = file.path.rfind('/');
                        if (last_slash != std::string::npos && last_slash > file.path.find(':') + 1) {
                            file.parent_path = file.path.substr(0, last_slash);
                        } else {
                            file.parent_path = remote_name_ + ":/";
                        }
                        
                        file.extension = get_extension(file.name);
                        file.is_synced = false;
                        
                        if (!file.name.empty() && !file.path.empty()) {
                            batch.push_back(std::move(file));
                        }
                        
                        // Save batch when it reaches threshold
                        if (batch.size() >= BATCH_SIZE) {
                            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::steady_clock::now() - start_time).count();
                            
                            insert_files_batch(batch);
                            total_saved += batch.size();
                            batch.clear();
                            
                            int progress = std::min(10 + (total_saved / 100), 90);
                            report_progress(progress, "Indexed " + std::to_string(total_saved) + 
                                          " files (" + std::to_string(elapsed) + "s)...");
                            Logger::debug("[FileIndex] Saved batch, total: " + std::to_string(total_saved) + " files");
                        }
                        
                        current_object.clear();
                        continue;
                    }
                }
            }
            
            if (brace_depth > 0) {
                current_object += c;
            }
        }
    }
    
    // Save remaining files
    if (!batch.empty()) {
        insert_files_batch(batch);
        total_saved += batch.size();
        Logger::info("[FileIndex] Saved final batch, total: " + std::to_string(total_saved) + " files");
    }
    
    auto total_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start_time).count();
    
    Logger::info("[FileIndex] Streaming complete: " + std::to_string(total_saved) + 
                " files in " + std::to_string(total_elapsed) + "s");
    
    if (total_saved == 0) {
        Logger::error("[FileIndex] ERROR: No files indexed!");
        report_progress(100, "Error: No files found in cloud");
        is_indexing_ = false;
        return;
    }
    
    report_progress(95, "Updating statistics...");
    update_last_index_time();
    
    report_progress(100, "Indexed " + std::to_string(total_saved) + " items");
    
    Logger::info("[FileIndex] Indexed " + std::to_string(total_saved) + " files/folders");
    
    is_indexing_ = false;
}

bool FileIndex::insert_files_batch(const std::vector<IndexedFile>& files) {
    if (!db_ || files.empty()) {
        Logger::warn("[FileIndex] insert_files_batch: empty files list or no db");
        return false;
    }
    
    sqlite3* db = static_cast<sqlite3*>(db_);
    
    Logger::debug("[FileIndex] Beginning transaction for " + std::to_string(files.size()) + " files");
    
    // Begin transaction
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        Logger::error("[FileIndex] BEGIN TRANSACTION failed: " + std::string(err_msg ? err_msg : "unknown"));
        if (err_msg) sqlite3_free(err_msg);
        return false;
    }
    
    const char* sql = R"(
        INSERT OR REPLACE INTO files 
        (name, path, parent_path, size, mod_time, is_directory, is_synced, local_path, extension)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";
    
    sqlite3_stmt* stmt;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::error("[FileIndex] prepare failed: " + std::string(sqlite3_errmsg(db)));
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    
    int inserted = 0;
    int errors = 0;
    for (const auto& file : files) {
        sqlite3_bind_text(stmt, 1, file.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, file.path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, file.parent_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 4, file.size);
        sqlite3_bind_text(stmt, 5, file.mod_time.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 6, file.is_directory ? 1 : 0);
        sqlite3_bind_int(stmt, 7, file.is_synced ? 1 : 0);
        sqlite3_bind_text(stmt, 8, file.local_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 9, file.extension.c_str(), -1, SQLITE_TRANSIENT);
        
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            errors++;
            if (errors <= 3) {
                Logger::warn("[FileIndex] Insert failed for: " + file.path + " - " + sqlite3_errmsg(db));
            }
        } else {
            inserted++;
        }
        sqlite3_reset(stmt);
    }
    
    sqlite3_finalize(stmt);
    
    rc = sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        Logger::error("[FileIndex] COMMIT failed: " + std::string(err_msg ? err_msg : "unknown"));
        if (err_msg) sqlite3_free(err_msg);
        return false;
    }
    
    Logger::info("[FileIndex] Batch insert complete: " + std::to_string(inserted) + " inserted, " + 
                std::to_string(errors) + " errors");
    
    return true;
}

void FileIndex::update_last_index_time() {
    if (!db_) return;
    
    sqlite3* db = static_cast<sqlite3*>(db_);
    std::string now = get_current_timestamp();
    
    // Use INSERT OR REPLACE to ensure the row exists
    sqlite3_stmt* stmt;
    const char* sql = "INSERT OR REPLACE INTO index_meta (key, value) VALUES ('last_full_index', ?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_TRANSIENT);
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) {
            Logger::info("[FileIndex] Updated last_full_index timestamp: " + now);
        } else {
            Logger::error("[FileIndex] Failed to update timestamp: " + std::string(sqlite3_errmsg(db)));
        }
        sqlite3_finalize(stmt);
    }
}

void FileIndex::update_sync_status(const std::string& remote_path, bool is_synced, const std::string& local_path) {
    if (!db_) return;
    
    sqlite3* db = static_cast<sqlite3*>(db_);
    
    const char* sql = "UPDATE files SET is_synced = ?, local_path = ? WHERE path = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, is_synced ? 1 : 0);
        sqlite3_bind_text(stmt, 2, local_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, remote_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void FileIndex::clear_index() {
    if (!db_) return;
    
    sqlite3* db = static_cast<sqlite3*>(db_);
    sqlite3_exec(db, "DELETE FROM files;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "UPDATE index_meta SET value = '' WHERE key = 'last_full_index';", nullptr, nullptr, nullptr);
    
    Logger::info("[FileIndex] Index cleared");
}

void FileIndex::prune_stale_entries(const std::string& parent_path,
                                     const std::vector<std::string>& paths_seen) {
    if (!db_ || paths_seen.empty()) return;
    
    sqlite3* db = static_cast<sqlite3*>(db_);
    
    // Get existing entries for this parent
    auto existing = get_directory_contents(parent_path);
    
    int removed = 0;
    for (const auto& entry : existing) {
        bool found = false;
        for (const auto& seen : paths_seen) {
            if (entry.path == seen) {
                found = true;
                break;
            }
        }
        if (!found) {
            // This entry is stale - remove it and its children
            const char* del_sql = "DELETE FROM files WHERE path = ? OR path LIKE ?";
            sqlite3_stmt* stmt;
            if (sqlite3_prepare_v2(db, del_sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, entry.path.c_str(), -1, SQLITE_TRANSIENT);
                std::string prefix = entry.path + "/%";
                sqlite3_bind_text(stmt, 2, prefix.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
                removed++;
            }
        }
    }
    
    if (removed > 0) {
        Logger::info("[FileIndex] Pruned " + std::to_string(removed) + 
                     " stale entries from: " + parent_path);
    }
}

// ============================================================================
// Incremental Index Updates - Called when files are synced (avoids full reindex)
// ============================================================================

void FileIndex::add_or_update_file(const std::string& remote_path,
                                    const std::string& name,
                                    int64_t size,
                                    const std::string& mod_time,
                                    bool is_directory,
                                    bool is_synced,
                                    const std::string& local_path) {
    if (!db_) return;
    
    sqlite3* db = static_cast<sqlite3*>(db_);
    
    // Extract parent path
    std::string parent_path = "";
    size_t last_slash = remote_path.rfind('/');
    if (last_slash != std::string::npos && last_slash > 0) {
        parent_path = remote_path.substr(0, last_slash);
    }
    
    std::string extension = is_directory ? "" : get_extension(name);
    
    const char* sql = R"(
        INSERT OR REPLACE INTO files 
        (name, path, parent_path, size, mod_time, is_directory, is_synced, local_path, extension, indexed_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)
    )";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, remote_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, parent_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 4, size);
        sqlite3_bind_text(stmt, 5, mod_time.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 6, is_directory ? 1 : 0);
        sqlite3_bind_int(stmt, 7, is_synced ? 1 : 0);
        sqlite3_bind_text(stmt, 8, local_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 9, extension.c_str(), -1, SQLITE_TRANSIENT);
        
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            Logger::warn("[FileIndex] Failed to add/update file: " + remote_path);
        }
        sqlite3_finalize(stmt);
    }
    
    // Update partial index timestamp
    sqlite3_stmt* meta_stmt;
    const char* meta_sql = "UPDATE index_meta SET value = ? WHERE key = 'last_partial_index'";
    if (sqlite3_prepare_v2(db, meta_sql, -1, &meta_stmt, nullptr) == SQLITE_OK) {
        std::string now = get_current_timestamp();
        sqlite3_bind_text(meta_stmt, 1, now.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(meta_stmt);
        sqlite3_finalize(meta_stmt);
    }
}

void FileIndex::remove_file(const std::string& remote_path) {
    if (!db_) return;
    
    sqlite3* db = static_cast<sqlite3*>(db_);
    
    const char* sql = "DELETE FROM files WHERE path = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, remote_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        Logger::debug("[FileIndex] Removed file from index: " + remote_path);
    }
}

void FileIndex::update_files_from_sync(const std::string& job_id,
                                        const std::string& local_path,
                                        const std::string& remote_path) {
    if (!db_) return;
    
    Logger::info("[FileIndex] Updating index from sync job: " + job_id);
    
    // Ensure valid working directory before popen()
    ensure_valid_cwd();
    
    // Use rclone to get the current state of this remote folder
    // Use shell quoting instead of double quotes for safe path handling
    std::string safe_remote = remote_name_ + ":" + remote_path;
    // Shell-escape using single quotes
    std::string escaped_remote = "'";
    for (char c : safe_remote) {
        if (c == '\'') escaped_remote += "'\"'\"'";
        else escaped_remote += c;
    }
    escaped_remote += "'";
    std::string cmd = "timeout 60 " + get_rclone_path() + " lsjson --fast-list " + escaped_remote + " 2>/dev/null";
    
    std::array<char, 8192> buffer;
    std::string json_output;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    
    if (!pipe) {
        Logger::warn("[FileIndex] Failed to run rclone for incremental update");
        return;
    }
    
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        json_output += buffer.data();
    }
    
    if (json_output.empty() || json_output[0] != '[') {
        Logger::warn("[FileIndex] No JSON output from rclone for: " + remote_path);
        return;
    }
    
    // Parse JSON using fast manual extractors
    std::vector<IndexedFile> files;
    
    // Fast manual JSON value extractors (same as index_worker)
    auto extract_str = [](const std::string& obj, const char* key, std::string& out) -> bool {
        std::string needle = std::string("\"") + key + "\":";
        size_t pos = obj.find(needle);
        if (pos == std::string::npos) return false;
        pos += needle.size();
        while (pos < obj.size() && (obj[pos] == ' ' || obj[pos] == '\t')) pos++;
        if (pos >= obj.size() || obj[pos] != '"') return false;
        pos++;
        std::string result;
        while (pos < obj.size() && obj[pos] != '"') {
            if (obj[pos] == '\\' && pos + 1 < obj.size()) { pos++; result += obj[pos]; }
            else { result += obj[pos]; }
            pos++;
        }
        out = std::move(result);
        return true;
    };
    
    auto extract_i64 = [](const std::string& obj, const char* key, int64_t& out) -> bool {
        std::string needle = std::string("\"") + key + "\":";
        size_t pos = obj.find(needle);
        if (pos == std::string::npos) return false;
        pos += needle.size();
        while (pos < obj.size() && (obj[pos] == ' ' || obj[pos] == '\t')) pos++;
        size_t end = pos;
        if (end < obj.size() && obj[end] == '-') end++;
        while (end < obj.size() && std::isdigit(static_cast<unsigned char>(obj[end]))) end++;
        if (end == pos) return false;
        try { out = std::stoll(obj.substr(pos, end - pos)); return true; }
        catch (...) { return false; }
    };
    
    auto extract_b = [](const std::string& obj, const char* key) -> bool {
        std::string needle = std::string("\"") + key + "\":";
        size_t pos = obj.find(needle);
        if (pos == std::string::npos) return false;
        pos += needle.size();
        while (pos < obj.size() && (obj[pos] == ' ' || obj[pos] == '\t')) pos++;
        return (pos + 3 < obj.size() && obj.substr(pos, 4) == "true");
    };
    
    // Split by },{ to get individual objects
    size_t pos = 0;
    while ((pos = json_output.find('{', pos)) != std::string::npos) {
        size_t end = json_output.find('}', pos);
        if (end == std::string::npos) break;
        
        std::string obj = json_output.substr(pos, end - pos + 1);
        
        IndexedFile file;
        
        extract_str(obj, "Name", file.name);
        std::string rel_path;
        if (extract_str(obj, "Path", rel_path)) {
            file.path = remote_path + "/" + rel_path;
        } else {
            file.path = remote_path + "/" + file.name;
        }
        extract_i64(obj, "Size", file.size);
        file.is_directory = extract_b(obj, "IsDir");
        extract_str(obj, "ModTime", file.mod_time);
        
        // Build parent path
        size_t last_slash = file.path.rfind('/');
        if (last_slash != std::string::npos && last_slash > 0) {
            file.parent_path = file.path.substr(0, last_slash);
        }
        
        file.extension = file.is_directory ? "" : get_extension(file.name);
        
        // This file is synced (it's from a sync job)
        file.is_synced = true;
        
        // Construct local path
        std::string relative = file.path;
        if (relative.find(remote_path) == 0) {
            relative = relative.substr(remote_path.length());
            if (!relative.empty() && relative[0] == '/') {
                relative = relative.substr(1);
            }
        }
        file.local_path = local_path + "/" + relative;
        
        if (!file.name.empty()) {
            files.push_back(file);
        }
        
        pos = end + 1;
    }
    
    // Batch insert
    if (!files.empty()) {
        insert_files_batch(files);
        Logger::info("[FileIndex] Updated " + std::to_string(files.size()) + 
                     " files from sync job: " + job_id);
    }
}

void FileIndex::set_encryption_key(const std::string& key) {
    if (db_) {
        Logger::warn("[FileIndex] Cannot set encryption key after database is opened");
        return;
    }
    encryption_key_ = key;
}

bool FileIndex::needs_refresh(int max_age_hours) const {
    if (!db_) {
        Logger::debug("[FileIndex] needs_refresh: db not initialized, returning true");
        return true;
    }
    
    sqlite3* db = static_cast<sqlite3*>(const_cast<void*>(db_));
    sqlite3_stmt* stmt;
    
    const char* sql = "SELECT value FROM index_meta WHERE key = 'last_full_index'";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        Logger::debug("[FileIndex] needs_refresh: failed to query metadata, returning true");
        return true;
    }
    
    bool needs_refresh = true;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (value && strlen(value) > 0) {
            auto last_index = parse_timestamp(value);
            auto now = std::chrono::system_clock::now();
            auto age = std::chrono::duration_cast<std::chrono::hours>(now - last_index);
            needs_refresh = age.count() >= max_age_hours;
            
            Logger::debug("[FileIndex] needs_refresh: last_index=" + std::string(value) +
                         ", age_hours=" + std::to_string(age.count()) +
                         ", max_age=" + std::to_string(max_age_hours) +
                         ", needs_refresh=" + std::string(needs_refresh ? "yes" : "no"));
        } else {
            Logger::debug("[FileIndex] needs_refresh: no valid timestamp in metadata");
        }
    } else {
        Logger::debug("[FileIndex] needs_refresh: no last_full_index metadata found");
    }
    
    sqlite3_finalize(stmt);
    return needs_refresh;
}

bool FileIndex::path_exists(const std::string& path) const {
    if (!db_) return false;
    
    sqlite3* db = static_cast<sqlite3*>(const_cast<void*>(db_));
    sqlite3_stmt* stmt;
    
    const char* sql = "SELECT 1 FROM files WHERE path = ? LIMIT 1";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    
    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    
    return exists;
}

void FileIndex::report_progress(int percent, const std::string& status) {
    index_progress_ = percent;
    
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (progress_callback_) {
        progress_callback_(percent, status);
    }
}

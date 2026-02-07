# C++ API Reference

Complete reference for the public C++ classes and interfaces in Proton Drive Linux.

---

## Table of Contents

1. [AppWindow](#appwindow) - Main application window
2. [SyncManager](#syncmanager) - Sync job orchestration
3. [FileWatcher](#filewatcher) - Real-time file monitoring
4. [FileIndex](#fileindex) - Cloud file search cache
5. [CloudBrowser](#cloudbrowser) - Cloud file explorer UI
6. [Settings](#settings) - User preferences
7. [Logger](#logger) - Logging system
8. [Enums & Types](#enums--types) - Shared data structures

---

## AppWindow

**File:** `src-native/src/app_window.{hpp,cpp}`

**Purpose:** Main GTK4 application window, orchestrates UI and child components

### Class Definition

```cpp
class AppWindow : public Gtk::ApplicationWindow {
public:
  AppWindow(Glib::RefPtr<Gtk::Application> app);
  ~AppWindow();

  // Initialization
  bool initialize();
  
  // Panel access
  SyncManager* sync_manager();
  CloudBrowser* cloud_browser();
  FileIndex* file_index();
  Settings* settings();
  
  // UI updates from background threads
  void post_message(const std::string& message);
  void show_notification(const std::string& title, 
                         const std::string& body);
  void update_sync_progress(const std::string& job_id, 
                           int percent);
};
```

### Methods

#### `bool initialize()`
Initializes all child components and GTK widgets.

**Returns:** `true` if successful, `false` on error

**Throws:** None (returns false on error)

**Example:**
```cpp
auto app_window = std::make_unique<AppWindow>(app);
if (!app_window->initialize()) {
  Logger::error("Failed to initialize main window");
  return 1;
}
```

#### `SyncManager* sync_manager()`
Returns pointer to sync manager (for job operations).

**Returns:** Non-null `SyncManager*`

**Note:** Pointer is valid for lifetime of AppWindow

**Example:**
```cpp
sync_manager()->create_job(config);
sync_manager()->start_job(job_id);
```

#### `CloudBrowser* cloud_browser()`
Returns pointer to cloud browser (for UI updates).

**Returns:** Non-null `CloudBrowser*`

#### `void show_notification(const std::string& title, const std::string& body)`
Shows desktop notification (from worker thread safe).

**Parameters:**
- `title`: Notification title (e.g., "Sync Complete")
- `body`: Notification body (e.g., "Uploaded 5 files")

**Thread-Safe:** Yes (uses `g_idle_add` internally)

**Example:**
```cpp
app_window->show_notification("Sync Complete", "Uploaded 10 files");
```

---

## SyncManager

**File:** `src-native/src/sync_manager.{hpp,cpp}`

**Purpose:** Create, manage, and orchestrate sync jobs

### Class Definition

```cpp
class SyncManager {
public:
  // Job creation
  std::string create_job(const JobConfig& config);
  
  // Job control
  bool start_job(const std::string& job_id);
  bool stop_job(const std::string& job_id);
  bool delete_job(const std::string& job_id);
  bool pause_job(const std::string& job_id);
  
  // Job queries
  SyncJob* get_job(const std::string& job_id);
  std::vector<SyncJob> list_jobs();
  
  // Sync control
  bool sync_now(const std::string& job_id);
  
  // Progress monitoring
  void monitor_progress(const std::string& job_id);
  
  // Signals (to connect UI handlers)
  sigc::signal<void, const std::string&, int>& 
    signal_progress();  // (job_id, percent)
  sigc::signal<void, const std::string&, const std::string&>& 
    signal_error();     // (job_id, error_message)
  sigc::signal<void, const std::string&>& 
    signal_complete();  // (job_id)
};
```

### Types

```cpp
struct JobConfig {
  std::string local_path;        // e.g., ~/Documents
  std::string remote_path;       // e.g., /Documents
  SyncType type {SyncType::TWO_WAY};
  int interval_minutes {15};
  int max_delete_percent {50};
  ConflictResolution conflict_mode {ConflictResolution::KEEP_BOTH};
};

enum class SyncType {
  TWO_WAY,              // Bidirectional
  DOWNLOAD_ONLY,        // Cloud to local
  UPLOAD_ONLY           // Local to cloud
};

enum class ConflictResolution {
  KEEP_BOTH,            // Rename local, keep remote
  NEWER_WINS,           // Whichever is newer
  LOCAL_WINS,           // Always keep local
  REMOTE_WINS,          // Always keep remote
  ASK_USER              // Show dialog
};

enum class JobStatus {
  RUNNING,
  PAUSED,
  ERROR,
  IDLE,
  SYNCED
};
```

### Methods

#### `std::string create_job(const JobConfig& config)`
Creates a new sync job.

**Parameters:**
- `config`: Job configuration (local/remote paths, interval, etc.)

**Returns:** Generated job ID (UUID format)

**Throws:** None (returns empty string on error)

**Validates:**
- Paths exist and are readable/writable
- Remote path exists in Proton Drive
- No duplicate path combinations

**Side Effects:**
- Creates systemd user service
- Saves job metadata to `~/.config/proton-drive/jobs/`
- Triggers initial sync (rclone bisync --resync)

**Example:**
```cpp
JobConfig config {
  .local_path = "/home/user/Documents",
  .remote_path = "/Documents",
  .type = SyncType::TWO_WAY,
  .interval_minutes = 15
};

std::string job_id = sync_manager->create_job(config);
if (job_id.empty()) {
  Logger::error("Failed to create sync job");
}
```

#### `bool start_job(const std::string& job_id)`
Starts or resumes a sync job.

**Parameters:**
- `job_id`: Job UUID

**Returns:** `true` on success, `false` if job not found/error

**Side Effects:**
- Starts systemd user service
- Begins inotify monitoring
- Triggers initial sync

**Example:**
```cpp
if (!sync_manager->start_job(job_id)) {
  Logger::error("Failed to start job: " + job_id);
}
```

#### `bool stop_job(const std::string& job_id)`
Stops a sync job (can be resumed).

**Parameters:**
- `job_id`: Job UUID

**Returns:** `true` on success

**Side Effects:**
- Stops systemd service
- Stops inotify monitoring
- Saves state

**Note:** Job metadata persists; use `delete_job()` to remove

#### `bool delete_job(const std::string& job_id)`
Permanently deletes a sync job.

**Parameters:**
- `job_id`: Job UUID

**Returns:** `true` on success

**Side Effects:**
- Stops job if running
- Removes systemd service
- Deletes job config file
- Cleans bisync cache
- **Does NOT delete files** (local or cloud)

**Example:**
```cpp
// Delete a job
if (sync_manager->delete_job(job_id)) {
  Logger::info("Deleted job " + job_id);
}
```

#### `SyncJob* get_job(const std::string& job_id)`
Retrieves job metadata and current status.

**Parameters:**
- `job_id`: Job UUID

**Returns:** Pointer to SyncJob if found, `nullptr` otherwise

**Example:**
```cpp
SyncJob* job = sync_manager->get_job(job_id);
if (job) {
  std::cout << "Status: " << static_cast<int>(job->status) << std::endl;
  std::cout << "Local: " << job->local_path << std::endl;
}
```

#### `bool sync_now(const std::string& job_id)`
Immediately trigger sync (don't wait for interval).

**Parameters:**
- `job_id`: Job UUID

**Returns:** `true` on success, `false` if already syncing

**Side Effects:**
- Executes `rclone bisync` immediately
- Emits `signal_progress()` and `signal_complete()` signals

**Example:**
```cpp
// User clicks "Sync Now" button
if (sync_manager->sync_now(job_id)) {
  app_window->show_notification("Syncing", "Starting sync...");
}
```

#### `sigc::signal<void, const std::string&, int>& signal_progress()`
Signal emitted during sync progress.

**Signal Parameters:**
- `job_id` (string): The job being synced
- `percent` (int): Progress 0-100

**Example:**
```cpp
sync_manager->signal_progress().connect(
  [this](const std::string& job_id, int percent) {
    Logger::debug(job_id + " progress: " + std::to_string(percent) + "%");
    update_progress_bar(percent);
  }
);
```

---

## FileWatcher

**File:** `src-native/src/file_watcher.{hpp,cpp}`

**Purpose:** Monitor local filesystem for changes (inotify-based)

### Class Definition

```cpp
class FileWatcher {
public:
  FileWatcher();
  ~FileWatcher();
  
  // Setup
  bool initialize();
  
  // Monitoring
  bool watch_path(const std::string& path);
  bool unwatch_path(const std::string& path);
  
  // Events
  sigc::signal<void, const std::string&>& signal_file_created();
  sigc::signal<void, const std::string&>& signal_file_modified();
  sigc::signal<void, const std::string&>& signal_file_deleted();
};
```

### Methods

#### `bool initialize()`
Sets up inotify file descriptor.

**Returns:** `true` on success

**Prerequisites:** Must call before using watcher

**Example:**
```cpp
FileWatcher watcher;
if (!watcher.initialize()) {
  Logger::error("Failed to initialize file watcher");
  return false;
}
```

#### `bool watch_path(const std::string& path)`
Start watching directory recursively for changes.

**Parameters:**
- `path`: Directory path (must exist)

**Returns:** `true` on success, `false` if path invalid

**Behavior:**
- Watches recursively (includes subdirectories)
- Monitors IN_CREATE, IN_MODIFY, IN_DELETE events
- Debounces rapid changes (30-second window)

**Example:**
```cpp
watcher.watch_path("/home/user/Documents");
watcher.signal_file_created().connect(
  [](const std::string& path) {
    Logger::info("File created: " + path);
  }
);
```

#### `sigc::signal<void, const std::string&>& signal_file_created()`
Emitted when file is created in watched path.

**Signal Parameter:**
- `path` (string): Full path to created file

**Thread:** Worker thread (not UI safe)

**Example:**
```cpp
watcher->signal_file_created().connect(
  [this](const std::string& path) {
    // Schedule sync on main thread
    g_idle_add(+[](gpointer self) -> gboolean {
      reinterpret_cast<MyApp*>(self)->trigger_sync();
      return G_SOURCE_REMOVE;
    }, this);
  }
);
```

---

## FileIndex

**File:** `src-native/src/file_index.{hpp,cpp}`

**Purpose:** Cache cloud file metadata and provide instant search

### Class Definition

```cpp
class FileIndex {
public:
  FileIndex();
  ~FileIndex();
  
  // Setup
  bool initialize(const std::string& cache_dir);
  
  // Search
  std::vector<File> search(const std::string& query);
  std::optional<File> find_file(const std::string& remote_path);
  
  // Updates
  bool add_or_update_file(const File& file);
  bool remove_file(const std::string& remote_path);
  
  // Sync status
  void mark_synced_files(const std::string& local_path,
                         const std::string& remote_path);
  
  // Refresh
  bool refresh_index();
};
```

### Types

```cpp
struct File {
  std::string remote_path;      // /Documents/file.txt
  std::string name;              // file.txt
  size_t size {0};               // bytes
  time_t modified {0};           // UNIX timestamp
  bool is_synced {false};        // In active sync folder?
};
```

### Methods

#### `bool initialize(const std::string& cache_dir)`
Opens or creates encrypted file index database.

**Parameters:**
- `cache_dir`: Cache directory (e.g., `~/.cache/proton-drive`)

**Returns:** `true` on success

**Side Effects:**
- Creates `file_index.db` (SQLite, AES-256 encrypted)
- Creates `file_index.db.keyfile` (encryption key)

**Example:**
```cpp
FileIndex index;
if (!index.initialize(cache_dir)) {
  Logger::error("Failed to initialize file index");
  return false;
}
```

#### `std::vector<File> search(const std::string& query)`
Full-text search on cloud file names.

**Parameters:**
- `query`: Search term (e.g., "report*" for prefix search)

**Returns:** Vector of matching files

**Performance:** <100ms for typical queries (local SQLite)

**Example:**
```cpp
auto results = file_index->search("vacation*");
for (const auto& file : results) {
  Logger::info("Found: " + file.name + " (" + 
               std::to_string(file.size) + " bytes)");
}
```

#### `bool add_or_update_file(const File& file)`
Add or update a file in the index.

**Parameters:**
- `file`: File metadata

**Returns:** `true` on success

**Thread-Safe:** Yes

**Example:**
```cpp
File f {
  .remote_path = "/Documents/report.txt",
  .name = "report.txt",
  .size = 12345,
  .modified = time(nullptr)
};
file_index->add_or_update_file(f);
```

#### `void mark_synced_files(const std::string& local_path, const std::string& remote_path)`
Mark files in a sync folder as actively synced.

**Parameters:**
- `local_path`: Local sync folder (e.g., `~/Documents`)
- `remote_path`: Remote sync folder (e.g., `/Documents`)

**Side Effects:**
- Updates `is_synced` flag for matching files
- CloudBrowser uses this to show visual indicator

**Example:**
```cpp
// When sync job is enabled
file_index->mark_synced_files("/home/user/Documents", "/Documents");
```

#### `bool refresh_index()`
Fetch latest cloud files and update cache.

**Returns:** `true` on success

**Side Effects:**
- Spawns background task: `rclone lsjson --recursive proton:/`
- Updates database
- Thread-safe

**Performance:** ~30 seconds for 10k files

**Example:**
```cpp
if (!file_index->refresh_index()) {
  Logger::error("Failed to refresh index");
}
```

---

## CloudBrowser

**File:** `src-native/src/cloud_browser.{hpp,cpp}`

**Purpose:** GTK4 widget for browsing and managing cloud files

### Class Definition

```cpp
class CloudBrowser : public Gtk::Box {
public:
  CloudBrowser(FileIndex* file_index);
  
  // Navigation
  bool open_folder(const std::string& remote_path);
  void go_back();
  void go_home();
  
  // File operations
  bool download_file(const std::string& remote_path);
  bool delete_file(const std::string& remote_path);
  
  // Signals
  sigc::signal<void, const std::string&>& signal_download_started();
  sigc::signal<void, const std::string&>& signal_delete_started();
};
```

### Methods

#### `bool open_folder(const std::string& remote_path)`
Open a cloud folder in the browser.

**Parameters:**
- `remote_path`: Remote folder path (e.g., `/Documents`)

**Returns:** `true` on success

**Side Effects:**
- Updates tree view with folder contents
- Updates navigation bar

**Example:**
```cpp
cloud_browser->open_folder("/Documents");
cloud_browser->open_folder("/Documents/Archive");
```

#### `bool download_file(const std::string& remote_path)`
Download a file from cloud to local folder.

**Parameters:**
- `remote_path`: File path (e.g., `/Documents/report.txt`)

**Returns:** `true` on success

**Behavior:**
- Downloads to `~/Downloads` (by default)
- Shows progress dialog
- Emits `signal_download_started()`

**Example:**
```cpp
if (cloud_browser->download_file("/Documents/report.txt")) {
  Logger::info("Download started");
}
```

#### `bool delete_file(const std::string& remote_path)`
Delete file from cloud.

**Parameters:**
- `remote_path`: File to delete

**Returns:** `true` on success

**Note:** Subject to rclone delete permission; checks Proton API authorization

**Example:**
```cpp
if (cloud_browser->delete_file("/Documents/oldfile.txt")) {
  Logger::info("File deleted from cloud");
}
```

---

## Settings

**File:** `src-native/src/settings.{hpp,cpp}`

**Purpose:** Global user preferences (singleton pattern)

### Class Definition

```cpp
class Settings {
public:
  // Singleton access
  static Settings& instance();
  
  // Get/set preferences
  std::optional<std::string> get_string(const std::string& key);
  std::optional<int> get_int(const std::string& key);
  std::optional<bool> get_bool(const std::string& key);
  
  void set_string(const std::string& key, const std::string& value);
  void set_int(const std::string& key, int value);
  void set_bool(const std::string& key, bool value);
  
  // Persistence
  bool save();
  bool load();
};
```

### Common Settings Keys

| Key | Type | Default | Purpose |
|-----|------|---------|---------|
| `max_delete_percent` | int | 50 | Max % of files that can be deleted in one sync |
| `conflict_resolution` | string | `keep_both` | How to resolve sync conflicts |
| `sync_interval_default` | int | 15 | Default sync interval (minutes) |
| `show_notifications` | bool | true | Show desktop notifications |
| `dark_mode` | bool | false | Dark theme enabled |
| `start_minimized` | bool | false | Start app in tray |
| `last_cloud_path` | string | `/` | Last browsed cloud folder |

### Methods

#### `std::optional<std::string> get_string(const std::string& key)`
Get string preference.

**Returns:** Value if found, `std::nullopt` otherwise

**Example:**
```cpp
auto conflict_mode = Settings::instance().get_string("conflict_resolution");
if (conflict_mode && conflict_mode.value() == "keep_both") {
  Logger::info("Using keep_both conflict resolution");
}
```

#### `void set_int(const std::string& key, int value)`
Set integer preference.

**Example:**
```cpp
Settings::instance().set_int("sync_interval_default", 5);
Settings::instance().save();
```

---

## Logger

**File:** `src-native/src/logger.{hpp,cpp}`

**Purpose:** Structured logging to file and console

### Class Definition

```cpp
class Logger {
public:
  // Logging (static methods)
  static void info(const std::string& message);
  static void debug(const std::string& message);
  static void warning(const std::string& message);
  static void error(const std::string& message);
  
  // Configuration
  static void initialize(const std::string& log_file);
  static void set_debug_mode(bool enabled);
};
```

### Methods

#### `static void info(const std::string& message)`
Log informational message.

**Example:**
```cpp
Logger::info("Sync started for job: " + job_id);
Logger::info("Downloaded 10 files (5.2 MB)");
```

#### `static void debug(const std::string& message)`
Log debug message (only if `--debug` flag enabled).

**Example:**
```cpp
Logger::debug("File watcher event: " + event_type);
Logger::debug("API response: " + response_json);
```

#### `static void error(const std::string& message)`
Log error message.

**Example:**
```cpp
Logger::error("Sync failed: " + error_description);
Logger::error("File not found: " + path);
```

### Logging Locations

- **Normal logs:** `~/.cache/proton-drive/proton-drive.log`
- **Debug logs:** Same file (with `--debug` flag)
- **Crash dumps:** `~/.cache/proton-drive/crash.log`

---

## Enums & Types

### SyncType

```cpp
enum class SyncType {
  TWO_WAY,              // ↔ Bidirectional sync
  DOWNLOAD_ONLY,        // ← Cloud to local only
  UPLOAD_ONLY           // → Local to cloud only
};
```

### JobStatus

```cpp
enum class JobStatus {
  RUNNING,              // Actively syncing or monitoring
  PAUSED,               // Stopped but metadata exists
  ERROR,                // Last sync failed
  IDLE,                 // Waiting for next interval
  SYNCED                // Last sync completed successfully
};
```

### ConflictResolution

```cpp
enum class ConflictResolution {
  KEEP_BOTH,            // Rename local (conflict_TIMESTAMP.ext), keep remote
  NEWER_WINS,           // Keep whichever version is newer
  LOCAL_WINS,           // Always keep local (discard remote)
  REMOTE_WINS,          // Always keep remote (discard local)
  ASK_USER              // Show dialog to user (interactive only)
};
```

---

## Thread Safety

**Thread-Safe Classes:**
- `FileIndex` - All operations are thread-safe
- `Settings` - Read/write are atomic
- `Logger` - All operations are thread-safe

**Thread-Unsafe Classes:**
- `AppWindow` - Must be called from GTK main thread only
- `CloudBrowser` - GTK widget, main thread only
- `FileWatcher` - Call setup from main thread, signals from worker thread
- `SyncManager` - Calls from main thread safe, signals from any thread OK

**Safe Pattern for Worker Threads:**
```cpp
// In worker thread
some_result result = do_work();

// Post update to main thread
g_idle_add(+[](gpointer data) -> gboolean {
  auto* self = reinterpret_cast<MyClass*>(data);
  self->update_ui_with_result(/* ... */);
  return G_SOURCE_REMOVE;
}, this);
```

---

## Error Handling

**Pattern: Return false/empty on error**
```cpp
// Preferred pattern
bool create_job(const JobConfig& config) {
  if (!validate_config(config)) {
    Logger::error("Invalid configuration");
    return false;
  }
  
  // Success path
  return true;
}

// For methods that return value
std::string create_job(const JobConfig& config) {
  // ...
  if (error) {
    Logger::error("Failed to create job");
    return "";  // Empty string indicates error
  }
  return job_id;  // Successfully created job
}

// For optional return values
std::optional<Job> find_job(const std::string& id) {
  if (found) {
    return job;
  }
  return std::nullopt;  // Not found
}
```

**Caller Pattern:**
```cpp
std::string job_id = sync_manager->create_job(config);
if (job_id.empty()) {
  Logger::error("Failed to create sync job");
  show_error_dialog();
  return;
}
```


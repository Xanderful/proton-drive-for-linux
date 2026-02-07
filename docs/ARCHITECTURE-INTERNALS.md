# Architecture & Internals - Component Deep Dive

Detailed explanation of how Proton Drive Linux components interact.

---

## System Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    GTK4 UI Layer                             │
│  ┌──────────────┐  ┌──────────────┐  ┌───────────────┐    │
│  │  App Window  │  │Cloud Browser │  │  Sync Panel   │    │
│  │ (main_window)│  │(cloud_browser)│  │(sync_manager) │    │
│  └──────────────┘  └──────────────┘  └───────────────┘    │
└─────────────────────────────────────────────────────────────┘
              ↓                    ↓                  ↓
┌─────────────────────────────────────────────────────────────┐
│                  Business Logic Layer                        │
│  ┌──────────────┐  ┌──────────────┐  ┌───────────────┐    │
│  │Sync Manager  │  │File Watcher  │  │ File Index    │    │
│  │(rclone mgmt) │  │(inotify)     │  │(search cache) │    │
│  └──────────────┘  └──────────────┘  └───────────────┘    │
│  ┌──────────────┐  ┌──────────────┐                        │
│  │    Cloud     │  │   Settings   │                        │
│  │   Browser    │  │ (preferences)│                        │
│  └──────────────┘  └──────────────┘                        │
└─────────────────────────────────────────────────────────────┘
              ↓                    ↓                  ↓
┌─────────────────────────────────────────────────────────────┐
│                  Foundation Layer                            │
│  ┌──────────────┐  ┌──────────────┐  ┌───────────────┐    │
│  │ Auth Module  │  │  Key Ring    │  │ Notifications │    │
│  │ (OAuth2)     │  │ (credentials)│  │ (D-Bus)       │    │
│  └──────────────┘  └──────────────┘  └───────────────┘    │
│  ┌──────────────┐  ┌──────────────┐                        │
│  │    Tray      │  │   Logger     │                        │
│  │(StatusNoti.) │  │ (file logs)  │                        │
│  └──────────────┘  └──────────────┘                        │
└─────────────────────────────────────────────────────────────┘
              ↓                    ↓                  ↓
┌─────────────────────────────────────────────────────────────┐
│              External Systems                                │
│  ┌──────────────┐  ┌──────────────┐  ┌───────────────┐    │
│  │  rclone      │  │ Proton API   │  │  SQLite DB    │    │
│  │ (sync tool)  │  │ (REST)       │  │ (file index)  │    │
│  └──────────────┘  └──────────────┘  └───────────────┘    │
│  ┌──────────────┐  ┌──────────────┐                        │
│  │  File System │  │  systemd     │                        │
│  │ (inotify)    │  │  (services)  │                        │
│  └──────────────┘  └──────────────┘                        │
└─────────────────────────────────────────────────────────────┘
```

---

## Core Components

### 1. App Window (`app_window.cpp`) - The Main Window

**Responsibilities:**
- Create and manage GTK4 main window
- Handle UI events (menu clicks, button presses)
- Update UI based on application state
- Coordinate between all sub-panels

**Key Classes:**
```cpp
class AppWindow {
  AppWindowUI ui;              // GTK4 widgets
  SyncManager sync_manager;    // Orchestrate sync jobs
  CloudBrowser cloud_browser;  // Cloud file explorer
  FileIndex file_index;        // Search cache
  Settings settings;           // User preferences
};
```

**Sub-modules:**
- `app_window_ui.cpp` - Widget creation (toolbar, sidebar, panels)
- `app_window_sync.cpp` - Sync job UI updates
- `app_window_cloud.cpp` - Cloud browser panel logic
- `app_window_actions.cpp` - Menu/button actions
- `app_window_monitor.cpp` - Monitor sync progress
- `app_window_polling.cpp` - Periodic UI refresh

---

### 2. Sync Manager (`sync_manager.cpp`) - Orchestration Engine

**Responsibilities:**
- Create, start, stop, delete sync jobs
- Execute rclone commands
- Monitor job progress
- Handle conflict resolution
- Update metadata

**Flow:**

```
User clicks "Add Sync Job"
         ↓
   SyncManager::create_job()
         ↓
   ├─ Validate paths
   ├─ Create job metadata (Job ID, paths, config)
   ├─ Save to ~/.config/proton-drive/jobs/
   ├─ Create systemd user service
   └─ Trigger initial sync
         ↓
   SyncManager::start_job()
         ↓
   Build rclone command: bisync local/ proton:/remote/ ...
         ↓
   Execute: rclone bisync --resync (first run only)
         ↓
   Monitor progress
         ↓
   Update FileIndex with new files
         ↓
   Show UI notifications (success/error)
```

**Key Classes:**
```cpp
class SyncManager {
  std::map<job_id, SyncJob> jobs;
  
  void create_job(local_path, remote_path, type);
  void start_job(job_id);
  void stop_job(job_id);
  void delete_job(job_id);
  
  void monitor_progress(job_id);
  void handle_conflict(job_id, file_path, strategy);
};

class SyncJob {
  std::string job_id;
  std::string local_path;
  std::string remote_path;
  SyncType type;              // Two-Way, Download-Only, Upload-Only
  int interval_minutes;
  JobStatus status;            // Running, Paused, Error, Idle
};
```

**Related Files:**
- `sync_job_metadata.cpp` - Job registry persistence
- `settings.cpp` - User preferences (intervals, safety levels)

---

### 3. File Watcher (`file_watcher.cpp`) - Real-Time Detection

**Responsibilities:**
- Monitor local filesystem for changes (using inotify)
- Detect file creation, modification, deletion
- Trigger sync on local changes
- Update file index for cloud changes

**Flow:**

```
User creates ~/Documents/report.txt
         ↓
   inotify detects IN_CREATE event
         ↓
   FileWatcher::on_file_created()
         ↓
   ├─ Check if path is in a synced folder
   ├─ Queue sync trigger
   └─ Update FileIndex
         ↓
   SyncManager::trigger_sync(job_id) [30-second debounce]
         ↓
   rclone uploads file to cloud
         ↓
   FileIndex.add_file(remote_path)
```

**Key Classes:**
```cpp
class FileWatcher {
  int inotify_fd;
  std::map<int, std::string> watched_paths;
  
  void watch_path(path);
  void on_create(path);
  void on_modify(path);
  void on_delete(path);
  void trigger_sync(job_id);
};
```

**Smart Debouncing:**
- File modifications create multiple inotify events (write, close, etc.)
- Queue is debounced—waits 30 sec after last event before sync
- Prevents 100 syncs from saving the same file 100 times

---

### 4. File Index (`file_index.cpp`) - Search Cache

**Responsibilities:**
- Cache cloud file metadata in local SQLite database
- Provide instant full-text search (FTS5)
- Detect which files are in synced folders
- Stay current with cloud changes

**Flow:**

```
App starts
   ↓
FileIndex::load_or_create()
   ├─ Open ~/.cache/proton-drive/file_index.db (AES-256 encrypted)
   └─ If >24 hours old, refresh index
   ↓
Spawn background task: rclone lsjson --recursive proton:/ > cloud_files.json
   ↓
Parse JSON, insert into FTS5 table
   ↓
User types in search box
   ↓
FileIndex::search("report")
   ├─ SQL: SELECT * FROM files_fts WHERE name MATCH "report*"
   └─ Return results ~instantly (local SQLite)
   ↓
User enters sync folder
   ↓
FileIndex::mark_synced_files(local_path, remote_path)
   ├─ Files inside ~/Documents show as "syncing"/"synced"
   └─ Update CloudBrowser UI
```

**Database Schema:**
```sql
CREATE VIRTUAL TABLE files_fts USING fts5(
  remote_path UNINDEXED,    -- /Documents/report.txt (unindexed for size)
  name,                      -- report.txt (indexed for search)
  size,                      -- file size (for sorting)
  modified,                  -- timestamp (for sorting)
  is_synced UNINDEXED        -- 0=cloud-only, 1=in-sync-folder
);
```

**Key Classes:**
```cpp
class FileIndex {
  sqlite3* db;  // Encrypted database
  
  void refresh_index();
  std::vector<File> search(query);
  void add_or_update_file(path, name, size, mod_time);
  void remove_file(path);
  void mark_synced_files(local_path, remote_path);
};
```

---

### 5. Cloud Browser (`cloud_browser.cpp`) - Explorer UI

**Responsibilities:**
- Display cloud files in tree view
- Handle file operations (download, delete)
- Show upload progress for drag-drop
- Quick navigation and filtering

**Real-Time Updates:**
```
FileWatcher detects local file created
   ↓
SyncManager::trigger_sync()
   ↓
rclone uploads to cloud
   ↓
SyncManager notifies CloudBrowser
   ↓
CloudBrowser::refresh_folder()
   ↓
Fetch updated folder listing from FileIndex
   ↓
Update GTK TreeView (file appears in list)
```

**Key Classes:**
```cpp
class CloudBrowser : public Gtk::Widget {
  Gtk::TreeView tree_view;
  FileIndex file_index;
  
  void on_row_activated(path);
  void on_download(file);
  void on_delete(file);
  void on_drag_data_received(files);  // Drag-drop from local
  void refresh_folder(path);
};
```

---

### 6. Settings (`settings.cpp`) - Preferences

**Responsibilities:**
- Store user preferences (interval, safety levels, notifications)
- Persist to `~/.config/proton-drive-linux/settings.json`
- Provide thread-safe access to settings

**Example Settings:**
```json
{
  "max_delete_percent": 50,
  "conflict_resolution": "keep_both",
  "sync_interval_default": 15,
  "show_notifications": true,
  "show_sync_errors": true,
  "dark_mode": false,
  "last_cloud_path": "/Documents"
}
```

---

### 7. Authentication & Keyring - Secure Credentials

**Flow:**

```
User clicks Login
   ↓
Launch browser → OAuth2 login
   ↓
User approves app
   ↓
Browser redirects to: proton-drive://auth?code=XXXX&state=YYYY
   ↓
App intercepts URL, extracts code
   ↓
Exchange code for access token via Proton API
   ↓
Store token in XDG Secret Service (system keyring)
   ├─ Encryption: system manages (usually user session encryption)
   ├─ Service: org.proton.ProtonDrive
   └─ Attributes: user_email, device_id
   ↓
Token loaded on app startup
   ↓
All API calls include: Authorization: Bearer <token>
```

**Key Methods:**
```cpp
void authenticate() {
  // Open browser for OAuth2
  uri = "https://account.protonmail.com/oauth/authorize?client_id=...";
  open_browser(uri);
  
  // Wait for callback URL
  // Extract code from proton-drive://auth?code=...
  
  // Exchange for token
  token = proton_api.exchange_code_for_token(code);
  
  // Store securely
  keyring.store("org.proton.ProtonDrive", "access_token", token);
}
```

---

### 8. Tray Icon (`tray_gtk4.cpp`) - System Integration

**Responsibilities:**
- Create D-Bus StatusNotifierItem (freedesktop standard)
- Show sync status in system tray
- Quick access to sync controls
- Minimize to tray

**Status Displays:**
- 🟢 Idle (no sync)
- 🔵 Syncing (progress animation)
- 🟡 Paused
- 🔴 Error
- ⚫ Offline

---

## Data Flow Scenarios

### Scenario 1: User Uploads Local File

```
~/Documents/project.pdf created (1 MB)
         ↓
   inotify: IN_CREATE + IN_CLOSE_WRITE
         ↓
   FileWatcher::on_file_created()
         ↓
   [30-second debounce] Last event was final
         ↓
   SyncManager::trigger_sync(job_id)
         ↓
   Build rclone command:
   rclone bisync ~/Documents proton:/Documents \
     --compare size,modtime \
     --max-delete 50 \
     --conflict-resolve newer
         ↓
   Execute, monitor progress
         ↓
   Detect completion
         ↓
   FileIndex::add_file(remote_path="Documents/project.pdf")
         ↓
   CloudBrowser::refresh_folder()
         ↓
   Notification: "Uploaded project.pdf (1.0 MB)"
         ↓
   ✅ Complete
```

**Timing:** ~5 seconds from file creation to upload completion

---

### Scenario 2: User Downloads Cloud File

```
User opens Proton Drive web app, uploads vacation-photos.zip (50 MB)
         ↓
   [Next sync interval arrives - default 15 minutes]
         ↓
   SyncManager::trigger_sync(job_id)
         ↓
   rclone bisync ~/Documents proton:/Documents
         ↓
   rclone detects remote-only file: vacation-photos.zip
         ↓
   Downloads to ~/Documents/vacation-photos.zip
         ↓
   Sync completes
         ↓
   FileWatcher::on_file_created() [detects download]
         ↓
   FileIndex::add_file()
         ↓
   CloudBrowser displays in "Recently Downloaded" section
         ↓
   Notification: "Downloaded vacation-photos.zip (50 MB)"
         ↓
   ✅ Complete

Wait... Why did it take 15 minutes?
   This is the rclone limitation!
   Cloud-to-local is POLLED (checked at intervals).
   Local-to-cloud is TRIGGERED (immediate on file change).
```

---

### Scenario 3: Sync Conflict

```
Both sides modify file:
   Local:  Report.docx modified at 14:30 (Version 1)
   Cloud:  Report.docx modified at 14:32 (Version 2)
         ↓
   Sync runs
         ↓
   rclone detects both versions are newer than last sync
         ↓
   Apply conflict resolution strategy (from settings):
         ↓
   IF strategy == "keep_both":
      Rename local:  Report.docx → Report_conflict-2026-02-07-143000.docx
      Keep remote:   Report.docx (cloud version = newest)
   ↓
   Sync completes
   ↓
   FileWatcher detects new conflict file
   ↓
   Notification: "Conflict in Report.docx - kept remote version"
   ↓
   ✅ Both versions preserved
```

---

## Threading Model

**Main Thread (GTK Event Loop):**
- Handles all UI events
- Updates GTK widgets
- Dispatches work to background threads

**Background Threads:**
```
┌─────────────────────────┐
│  GTK Main Thread        │
│  (UI only)              │
└──────────┬──────────────┘
           │
    ┌──────┴──────┬───────────┬──────────┐
    ↓             ↓           ↓          ↓
┌────────┐  ┌─────────┐ ┌──────────┐ ┌──────────┐
│ Sync   │  │  File   │ │FileIndex │ │  Proton  │
│Manager │  │Watcher  │ │ Refresh  │ │   API    │
│        │  │(inotify)│ │          │ │ Requests │
└────────┘  └─────────┘ └──────────┘ └──────────┘
(rclone)    (real-time) (SQLite ops) (libcurl)
```

**Communication:**
- Main thread queues work for background threads
- Background threads post results back to main thread via `g_idle_add()`
- Files are never modified from multiple threads simultaneously

---

## Storage Locations

```
~/.config/proton-drive-linux/
├── settings.json              # User preferences
├── jobs/                      # Sync job configs
│   ├── <job-id-1>.conf       # Job 1 metadata
│   └── <job-id-2>.conf       # Job 2 metadata
└── sync_jobs.json             # Job registry (dual-system tracking)

~/.cache/proton-drive/
├── proton-drive.log           # Debug logs (if --debug enabled)
├── file_index.db              # Cloud file metadata (AES-256 encrypted)
├── file_index.db.keyfile      # Encryption key (machine-specific)
└── crash.log                  # Crash dumps (if app crashes)

~/.local/share/systemd/user/
└── proton-drive-sync-*.service  # systemd user services for each job

System Keyring (XDG Secret Service):
├── Service: org.proton.ProtonDrive
├── Key: access_token
└── Attributes:
    ├── user_email: user@proton.me
    └── device_id: <unique-device-id>
```

---

## Sync Job Lifecycle

```
CREATE
  ├─ Validate paths
  ├─ Generate job UUID
  ├─ Create ~/.config/proton-drive/jobs/<uuid>.conf
  ├─ Add to sync_jobs.json registry
  ├─ Create systemd user service
  └─ INITIALIZE [first run] → rclone bisync --resync
         ↓
ENABLED
  ├─ systemd service watches ~/ProtonDrive (configured path)
  ├─ FileWatcher monitors local changes
  ├─ Periodic timer (default 15min interval)
  ├─ On trigger: Execute rclone bisync
  ├─ On completion: Update FileIndex, show notifications
  └─ Loop
         ↓
PAUSE
  ├─ Stop systemd service
  ├─ Stop sync triggers
  ├─ Job metadata remains
  └─ Can resume anytime
         ↓
DELETE
  ├─ Stop systemd service
  ├─ Remove ~/.config/proton-drive/jobs/<uuid>.conf
  ├─ Remove from sync_jobs.json
  ├─ Clean bisync cache files
  ├─ Update FileIndex (mark files as cloud-only)
  └─ [Cloud files remain unchanged]
```

---

## Error Handling & Recovery

**Sync Fails (e.g., upload error):**
```
→ Log error message
→ Pause job (prevent spam of failed syncs)
→ Show notification: "Sync error in Documents - check logs"
→ User can view logs, fix issue, manually click "Sync Now"
→ Resume sync
```

**Stale Lock Detection:**
```
rclone lock file exists but process is dead
→ SyncManager detects stale lock (>5 min old)
→ Removes lock file
→ Retries sync
```

**Credential Expiration:**
```
API returns 401 Unauthorized
→ Refresh token from Proton API
→ If refresh fails, prompt user to login again
→ Re-authenticate via OAuth2
```

---

## Performance Considerations

**File Index Size:**
- 20,000+ files → ~5-10 MB database
- FTS5 full-text search: <100ms for typical queries
- Incremental updates: ~1ms per file

**Sync Performance:**
- First sync of 1000 files: ~1-2 minutes (depends on file sizes, network)
- Incremental sync (10 files): ~30 seconds
- Dominated by rclone operations and network latency

**Memory Usage:**
- GTK4 UI: ~50-100 MB
- rclone child process: ~30-50 MB (per active job)
- File index in memory: ~10-20 MB (for 20k files)
- **Total:** ~100-200 MB typical

---

## Future Improvements

1. **Real-time Cloud Notifications** - If/when Proton adds webhooks
2. **Partial Sync** - Selective file sync instead of full folder
3. **Bandwidth Limiting** - Throttle upload/download speeds
4. **Resumable Downloads** - If connection interrupts
5. **LAN Sync** - Sync directly between local devices on LAN


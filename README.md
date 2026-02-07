# Proton Drive Linux

⚠️ **Unofficial Client** - This is an independent, unofficial desktop client for Proton Drive on Linux. It is not affiliated with, maintained by, or endorsed by Proton Technologies.

Native C++/GTK4 desktop application for accessing and synchronizing files with Proton Drive on Linux.

---

## 🚀 Features

### Cloud Browser

- **Full Cloud Explorer** - Browse, search, and preview your Proton Drive files with a native GTK4 interface
- **Full-Text Search** - Instant search across all cloud files using SQLite FTS5
- **File Details** - View file metadata, creation dates, and version history

### Folder Synchronization

- **Two-Way Sync** - Automatic bidirectional synchronization between local folders and cloud
- **Scheduled Sync** - Configurable sync intervals (manual, 15-min, hourly, etc.)
- **Conflict Resolution** - Intelligent handling of files modified on both sides
- **Delete Protection** - Prevents accidental mass deletion with configurable thresholds
- **Drag & Drop Upload** - Drop files directly onto the cloud browser

### Desktop Integration

- **System Tray Icon** - Minimalist D-Bus StatusNotifierItem with sync status
- **Real-Time Notifications** - Alerts for sync errors, completed jobs, and conflicts
- **No Dependencies** - Pure GTK4, no WebView, no Electron—fast and lightweight

### Security

- **Secure Authentication** - OAuth2 through Proton's official APIs
- **Keyring Storage** - Credentials stored in XDG Secret Service (system keyring)
- **Search Privacy** - Cloud filenames indexed locally, never exposed to plaintext

---

## ⚠️ Important Limitations

### Sync Behavior: Polling-Based, Not Real-Time Cloud-to-Local

This app uses **rclone's polling mechanism** for synchronization, which has an important limitation:

**Cloud-to-Local Changes (Limited):**

- ✅ Changes made locally are uploaded to cloud **immediately** when detected
- ❌ Changes made in the web app or other clients are NOT automatically downloaded to your local machine
- ⏱️ Cloud changes are only detected during scheduled sync intervals (default: every 15 minutes)

**Why?** Proton Drive has no real-time push notification API. rclone must poll the server periodically to detect cloud changes. This is a limitation of Proton's API, not this application.

**Recommended Workaround:**

- Set sync intervals to **5-10 minutes** for active folders if you frequently make changes in the web app
- Or manually trigger sync from the app's sync panel when you know cloud changes have been made
- For truly real-time sync of local changes to cloud, use Syncthing or native offerings instead

**Local-to-Cloud Sync is Fully Automatic:**

- Files/folders added locally are uploaded within seconds of creation (inotify-based)
- File modifications are detected and uploaded automatically
- Deletions are synced (subject to delete protection rules)

---

## 📋 System Requirements

### Build Dependencies

**Debian/Ubuntu:**

```bash
sudo apt install build-essential cmake pkg-config \
  libgtk-4-dev libcurl4-openssl-dev libsqlite3-dev libssl-dev
```

**Fedora/RHEL:**

```bash
sudo dnf install gcc g++ cmake gtk4-devel libcurl-devel \
  sqlite-devel openssl-devel
```

**Arch Linux:**

```bash
sudo pacman -S base-devel cmake gtk4 curl sqlite openssl
```

### Runtime Dependencies

- **GTK 4.0+** (libgtk-4-0)
- **libcurl** (libcurl4-openssl)
- **SQLite 3** (libsqlite3-0)
- **OpenSSL** (libssl3)
- **rclone 1.73+** with Proton Drive patch (for sync functionality)

---

## 🔨 Building

### From Source

```bash
git clone https://github.com/Xanderful/proton-drive-linux.git
cd proton-drive-linux

# Build release binary
mkdir -p src-native/build && cd src-native/build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Binary at: src-native/build/proton-drive
./proton-drive
```

### Using Makefile Shortcuts

```bash
make build      # Build release binary
make dev        # Build and run with debug output
make clean      # Clean build artifacts
make help       # Show all available targets
```

---

## 📦 Installation

### From Source (Recommended)

```bash
git clone https://github.com/Xanderful/proton-drive-linux.git
cd proton-drive-linux
make build
sudo make install     # Installs to /usr/local/bin
```

### AppImage (Portable)

```bash
# Build the AppImage
make build-appimage

# Or download from releases and run directly
chmod +x ProtonDrive-*.AppImage
./ProtonDrive-*.AppImage
```

---

## 🚀 Getting Started

### 1. First Run & Authentication

1. Launch the application: `proton-drive`
2. Click **"Login"** in the cloud browser panel
3. Enter your **Proton Account** credentials
4. Authenticate with OAuth2 (opens in browser)
5. Authorization tokens are stored securely in your system keyring

### 2. Set Up a Sync Job

```bash
# Option A: Via GUI (Recommended)
1. Click the sync icon in the sidebar
2. Click "Add Sync Job"
3. Select a local folder: ~/Documents (or any folder)
4. Select a cloud folder: /Backups (or any Proton Drive folder)
5. Choose sync type: Two-Way (recommended)
6. Set interval: 15 minutes (adjust as needed)
7. Enable and start sync

# Option B: Via CLI (Manual Sync)
proton-drive --sync-now ~/Documents /Backups
```

### 3. Monitor Sync Status

- **Sync Panel:** Shows active jobs, progress, and status
- **System Tray:** Click icon for quick status overview
- **Notifications:** Alerts for errors, completion, conflicts
- **Logs:** View detailed logs in Preferences → Logs

---

## 🏗️ Architecture

```
src-native/
├── src/
│   ├── main_native.cpp              # Application entry point
│   ├── app_window.cpp               # Main window (UI core)
│   ├── app_window_*.cpp             # UI features (cloud, sync, actions, etc.)
│   ├── sync_manager.cpp             # Sync orchestration & rclone integration
│   ├── sync_job_metadata.cpp        # Job registry & persistence
│   ├── file_watcher.cpp             # inotify-based local change detection
│   ├── file_index.cpp               # SQLite FTS5 cloud file search index
│   ├── cloud_browser.cpp            # Cloud explorer UI
│   ├── tray_gtk4.cpp                # D-Bus system tray integration
│   ├── notifications.cpp            # Desktop notifications
│   ├── settings.cpp                 # User preferences persistence
│   └── logger.cpp                   # Debug logging to ~/.cache/proton-drive.log
│
├── CMakeLists.txt                   # Build configuration
└── packaging/
    └── proton-drive.desktop         # Desktop integration

scripts/
├── setup-rclone.sh                  # rclone configuration wizard
├── setup-sync-service.sh            # systemd user service setup
├── manage-sync-job.sh               # Sync job lifecycle management
└── build-local-appimage.sh          # AppImage packaging

docs/
├── SYNC_SAFETY_FEATURES.md          # Safety features & conflict resolution
├── SYNC_CONFLICT_FIX.md             # Conflict scenarios & resolution
├── FOLDER_DELETION_HANDLING.md      # Handling deleted sync folders
├── PREVENTING_FALSE_POSITIVES.md    # Cache management & validation
├── FILE_INDEX_SECURITY.md           # File search encryption details
└── CROSS_DISTRO_TESTING.md          # Testing matrix for distributions
```

### Key Components

| Component | Purpose |
|-----------|---------|
| `app_window.cpp` | Main GTK4 window, UI state management, event handling |
| `sync_manager.cpp` | Orchestrates bisync jobs, manages rclone lifecycle |
| `file_watcher.cpp` | Monitors local filesystem changes with inotify |
| `file_index.cpp` | Caches cloud file metadata, provides instant search |
| `cloud_browser.cpp` | GTK4 widget tree for cloud file explorer |
| `tray_gtk4.cpp` | StatusNotifierItem D-Bus tray icon |

---

## 📚 Documentation

| Document | Purpose |
|----------|---------|
| [SYNC_SETUP.md](SYNC_SETUP.md) | Detailed rclone setup, troubleshooting, upload fixes |
| [SYNC_SAFETY_FEATURES.md](docs/SYNC_SAFETY_FEATURES.md) | Delete protection, conflict resolution, versioning |
| [SYNC_CONFLICT_FIX.md](docs/SYNC_CONFLICT_FIX.md) | Understanding & resolving sync conflicts |
| [FOLDER_DELETION_HANDLING.md](docs/FOLDER_DELETION_HANDLING.md) | Recovering from accidental folder deletions |
| [PREVENTING_FALSE_POSITIVES.md](docs/PREVENTING_FALSE_POSITIVES.md) | Cache validation, stale lock cleanup |
| [FILE_INDEX_SECURITY.md](docs/FILE_INDEX_SECURITY.md) | Search privacy & encryption design |
| [CROSS_DISTRO_TESTING.md](docs/CROSS_DISTRO_TESTING.md) | Compatibility matrix & distribution testing |

---

## 🐛 Troubleshooting

### Authentication Issues
**Problem:** Login button stuck or browser doesn't open  
**Solution:** 
```bash
# Clear credentials from keyring
secret-tool delete service proton-drive

# Restart app and try login again
```

### Sync Stuck or Stalled
**Problem:** Sync job not progressing  
**Solution:**
```bash
# Check for stale rclone locks
rm -f ~/.cache/rclone/bisync/*cache*

# View detailed logs
tail -f ~/.cache/proton-drive/proton-drive.log

# Manually restart sync service
systemctl --user restart proton-drive-sync
```

### Upload Failures (422 Error)
**Problem:** Files fail to upload with "422 Invalid Request"  
**Note:** This requires rclone with Proton Drive patch. AppImage includes this; if building from source, see [SYNC_SETUP.md](SYNC_SETUP.md#installing-fixed-rclone-climanual-sync-only)

---

## 🤝 Contributing

Contributions are welcome! Please:

1. **Fork** the repository
2. **Create** a feature branch: `git checkout -b feature/your-feature`
3. **Commit** with clear messages describing changes
4. **Test** on at least one supported distribution
5. **Push** and submit a **Pull Request**

### Development Setup
```bash
# Clone and build with debug symbols
git clone https://github.com/Xanderful/proton-drive-linux.git
make dev          # Builds with --debug, shows detailed output
```

### Reporting Issues
- Include **distribution** (Debian, Fedora, Arch, etc.) and version
- Include logs from `~/.cache/proton-drive/proton-drive.log`
- Include steps to **reproduce** the issue
- Attach relevant **screenshots** or recordings

---

## 📄 License

Licensed under the **Apache License 2.0**. See [LICENSE](LICENSE) for details.

---

## ⚖️ Legal Notice

This project is **not affiliated with, endorsed by, or supported by Proton Technologies AG**. Proton Drive is a trademark of Proton Technologies AG. This is a community-maintained integration for Linux users.

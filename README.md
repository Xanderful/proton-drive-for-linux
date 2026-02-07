# Proton Drive Linux

Native C++/GTK4 desktop client for Proton Drive on Linux. Provides drag-drop file sync, cloud browsing, and real-time folder synchronization.

## Features

- **Native GTK4 Desktop Experience** - No WebView, no Electron, pure native performance
- **Drag & Drop Sync** - Drop local files/folders onto the cloud browser to upload
- **Cloud File Browser** - Browse your Proton Drive files with search and version history
- **Real-Time Folder Sync** - Two-way sync using rclone with conflict resolution
- **System Tray Integration** - StatusNotifierItem D-Bus tray with progress indicators
- **Secure Authentication** - OAuth2 token stored in XDG keyring

## Requirements

### Build Dependencies

**Debian/Ubuntu:**
```bash
sudo apt install build-essential cmake pkg-config libgtk-4-dev libcurl4-openssl-dev libsqlite3-dev libssl-dev
```

**Fedora/RHEL:**
```bash
sudo dnf install gcc g++ cmake gtk4-devel libcurl-devel sqlite-devel openssl-devel
```

**Arch Linux:**
```bash
sudo pacman -S base-devel cmake gtk4 curl sqlite openssl
```

### Runtime Dependencies:
- `rclone` (for sync functionality)
- `fuse3` (for rclone mount)

## Building

```bash
# Clone repository
git clone https://github.com/Xanderful/proton-drive-linux.git
cd proton-drive-linux

# Build
mkdir -p src-native/build
cd src-native/build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Run
./proton-drive
```

Or use make shortcuts:
```bash
make build     # Build release
make dev       # Build and run with --debug flag
```

## Installation

### From Source
```bash
make build
sudo make install
```

### Package Formats
Pre-built packages are available for:
- **DEB** (Debian/Ubuntu)
- **RPM** (Fedora/RHEL)
- **AppImage** (Universal)
- **AUR** (Arch Linux)
- **Flatpak**
- **Snap**

## Folder Sync Setup

The app uses rclone for two-way folder synchronization:

```bash
# Configure rclone with Proton Drive
make setup-sync

# Start/stop sync service
make sync-start
make sync-stop

# Check status
make sync-status
```

Synced folders appear at `~/ProtonDrive`.

## Architecture

```
src-native/src/
├── main_native.cpp      # Entry point
├── app_window.cpp       # Main UI (cloud browser, drag-drop, logs)
├── sync_manager.cpp     # Sync job orchestration
├── file_index.cpp       # SQLite FTS5 cloud file cache
├── tray_gtk4.cpp        # StatusNotifierItem system tray
└── file_watcher.cpp     # inotify real-time sync triggers
```

## Documentation

- [Sync Safety Features](docs/SYNC_SAFETY_FEATURES.md) - Delete protection, conflict resolution, versioning
- [Sync Setup Guide](SYNC_SETUP.md) - Initial rclone configuration
- [File Index Security](docs/FILE_INDEX_SECURITY.md) - Search encryption details

## Contributing

Contributions welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Submit a pull request

## License

AGPL-3.0 - See [LICENSE](LICENSE) for details.

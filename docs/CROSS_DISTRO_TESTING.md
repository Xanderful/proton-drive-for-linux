# Cross-Distribution Testing Guide

Testing instructions for Proton Drive Linux across different distributions. This is a native C++/GTK4 application — no WebView, no Electron.

## System Requirements

### Kernel Features
- **inotify**: File system event monitoring (kernel 2.6.13+, all modern systems)
- **FUSE** (optional): For rclone mount-based sync

### Libraries
| Library | Purpose | Build Package | Runtime Package |
|---------|---------|---------------|-----------------|
| GTK 4.0+ | GUI toolkit | `libgtk-4-dev` | `libgtk-4-1` |
| libcurl | HTTP client | `libcurl4-openssl-dev` | `libcurl4` |
| SQLite 3 | File index cache | `libsqlite3-dev` | `libsqlite3-0` |
| OpenSSL | Encryption | `libssl-dev` | `libssl3` |
| rclone | Sync backend | — | `rclone` |

---

## Testing on Debian/Ubuntu

### Supported Versions
- **Debian**: 12 (Bookworm), Testing, Sid
- **Ubuntu**: 22.04 LTS, 24.04 LTS, and newer
- **Derivatives**: Linux Mint 21+, Pop!_OS 22.04+, elementary OS 7+

### Quick Start
```bash
# Install build dependencies
sudo apt install build-essential cmake pkg-config \
    libgtk-4-dev libcurl4-openssl-dev libsqlite3-dev libssl-dev

# Clone and build
git clone https://github.com/Xanderful/proton-drive-linux.git
cd proton-drive-linux
make build

# Run
./src-native/build/proton-drive
```

---

## Testing on Fedora/RHEL

### Supported Versions
- **Fedora**: 38+
- **RHEL**: 9+

### Quick Start
```bash
sudo dnf install gcc g++ cmake gtk4-devel libcurl-devel \
    sqlite-devel openssl-devel
make build
```

---

## Testing on Arch Linux

### Quick Start
```bash
sudo pacman -S base-devel cmake gtk4 curl sqlite openssl
make build
```

---

## Testing with AppImage

### Building an AppImage
```bash
make build-appimage
```

The AppImage will be created in `dist/ProtonDrive-VERSION-x86_64.AppImage`.

### Running the AppImage
```bash
chmod +x dist/ProtonDrive-*-x86_64.AppImage
./dist/ProtonDrive-*-x86_64.AppImage
```

### AppImage System Requirements
The AppImage bundles rclone but relies on host system libraries:

| Distribution | Required Packages |
|--------------|-------------------|
| Ubuntu 22.04+ | `libgtk-4-1` (pre-installed on 24.04+) |
| Debian 12+ | `libgtk-4-1` |
| Fedora 38+ | `gtk4` |
| Arch Linux | `gtk4` |

### AppImage Features

| Feature | Status | Notes |
|---------|--------|-------|
| File watching (inotify) | ✅ Built-in | Uses kernel inotify API |
| System tray | ✅ Works | D-Bus StatusNotifierItem |
| File share access | ✅ Works | Standard filesystem access |
| Folder sync (rclone) | ✅ Works | Bundled rclone binary |
| Desktop integration | ✅ Works | XDG desktop file included |

---

## inotify Configuration

For watching large directories with many files, increase the inotify limit:

### Temporary (until reboot)
```bash
sudo sysctl -w fs.inotify.max_user_watches=524288
```

### Permanent
```bash
echo 'fs.inotify.max_user_watches=524288' | sudo tee -a /etc/sysctl.conf
sudo sysctl -p
```

---

## Troubleshooting

### Application fails to start
1. Check for missing libraries:
   ```bash
   ldd src-native/build/proton-drive | grep "not found"
   ```

2. Install missing runtime libraries:
   ```bash
   # Ubuntu/Debian
   sudo apt install libgtk-4-1 libcurl4 libsqlite3-0 libssl3
   ```

### System tray icon not showing
- The app uses D-Bus StatusNotifierItem protocol
- Some desktop environments require extensions (e.g., GNOME needs "AppIndicator and KStatusNotifierItem Support")

### inotify limit errors
```bash
# Check if limit is too low
cat /proc/sys/fs/inotify/max_user_watches

# If less than 65536, increase it
sudo sysctl -w fs.inotify.max_user_watches=524288
```

### Sync not detecting file changes
1. Check if inotify is working:
   ```bash
   inotifywait -m ~/ProtonDrive &
   touch ~/ProtonDrive/test-file
   # Should show CREATE and CLOSE_WRITE events
   ```

2. Check application logs:
   ```bash
   cat ~/.cache/proton-drive/proton-drive.log | grep FileWatcher
   ```

---

## Testing Checklist

### Basic Functionality
- [ ] Application launches without errors
- [ ] Cloud browser loads file listing
- [ ] Full-text search finds files
- [ ] File details panel shows metadata

### System Integration
- [ ] System tray icon appears
- [ ] Tray menu is functional
- [ ] Close button minimizes to tray
- [ ] Desktop entry appears in app menu
- [ ] Application icon displays correctly

### Folder Sync
- [ ] rclone setup completes
- [ ] Two-way sync works
- [ ] inotify file watching triggers syncs
- [ ] Sync status shows in UI and tray
- [ ] Conflict resolution works correctly
- [ ] Delete protection prevents mass deletion

### AppImage-Specific
- [ ] AppImage runs without extraction
- [ ] Desktop integration works after first run
- [ ] Icons display correctly
- [ ] Bundled rclone works

---

## Reporting Issues

When reporting issues, include:

1. **Distribution info**:
   ```bash
   cat /etc/os-release
   ```

2. **Library versions**:
   ```bash
   pkg-config --modversion gtk4
   ```

3. **Application logs**:
   ```bash
   cat ~/.cache/proton-drive/proton-drive.log
   ```

4. **Crash logs** (if applicable):
   ```bash
   cat ~/.cache/proton-drive/crash.log
   ```

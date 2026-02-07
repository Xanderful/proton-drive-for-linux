# Cross-Distribution Testing Guide

This document provides instructions for testing Proton Drive Linux on different distributions, focusing on Debian/Ubuntu and AppImage portability.

## System Requirements

### Kernel Features
- **inotify**: File system event monitoring (kernel 2.6.13+, all modern systems)
- **FUSE** (optional): For rclone mount-based sync

### Libraries (Auto-detected at build time)
| Feature | GTK4 Variant | GTK3 Variant |
|---------|--------------|--------------|
| WebView | libwebkitgtk-6.0 | libwebkit2gtk-4.1 |
| Toolkit | GTK4 | GTK3 |
| Tray | AppIndicator3 | AppIndicator3 |

The build system automatically detects and uses what's available.

---

## Testing on Debian/Ubuntu

### Supported Versions
- **Debian**: 11 (Bullseye), 12 (Bookworm), Testing
- **Ubuntu**: 20.04 LTS, 22.04 LTS, 24.04 LTS, and newer
- **Derivatives**: Linux Mint 20+, Pop!_OS, elementary OS, etc.

### Quick Start (Interactive)
```bash
# Clone the repository
git clone --recurse-submodules https://github.com/user/protondrive-linux.git
cd protondrive-linux

# Run the interactive installer
bash scripts/install-debian-ubuntu.sh
```

### Automated Installation
```bash
# Full installation (deps + build + desktop entry)
bash scripts/install-debian-ubuntu.sh --full

# Or step by step:
bash scripts/install-debian-ubuntu.sh --deps-only
bash scripts/install-debian-ubuntu.sh --build-only
```

### Package-Specific Dependencies

#### Debian 12 / Ubuntu 24.04+ (GTK4)
```bash
sudo apt install libwebkitgtk-6.0-dev libgtk-4-dev \
    libcurl4-openssl-dev libsoup-3.0-dev \
    libayatana-appindicator3-dev cmake build-essential
```

#### Debian 11 / Ubuntu 22.04 (GTK3)
```bash
sudo apt install libwebkit2gtk-4.1-dev libgtk-3-dev \
    libcurl4-openssl-dev libsoup-3.0-dev \
    libayatana-appindicator3-dev cmake build-essential
```

---

## Testing with AppImage

### Building an AppImage
```bash
# From project root
make build-appimage

# Or directly
bash scripts/build-local-appimage.sh
```

The AppImage will be created in `dist/ProtonDrive-VERSION-x86_64.AppImage`.

### Running the AppImage
```bash
chmod +x dist/ProtonDrive-*-x86_64.AppImage
./dist/ProtonDrive-*-x86_64.AppImage
```

### AppImage System Requirements
The AppImage does **not** bundle GTK/WebKit (too large and system-dependent). The host system must have:

| Distribution | Required Packages |
|--------------|-------------------|
| Ubuntu 22.04+ | `libwebkit2gtk-4.1-0` (pre-installed) |
| Ubuntu 24.04+ | `libwebkitgtk-6.0-4` or `libwebkit2gtk-4.1-0` |
| Debian 11+ | `libwebkit2gtk-4.1-0` |
| Debian 12+ | `libwebkitgtk-6.0-4` or `libwebkit2gtk-4.1-0` |
| Fedora 38+ | `webkit2gtk4.1` or `webkitgtk6.0` |
| Arch Linux | `webkit2gtk-4.1` or `webkit2gtk` |

### AppImage Features

| Feature | Status | Notes |
|---------|--------|-------|
| File watching (inotify) | ✅ Built-in | Uses kernel inotify API |
| System tray | ✅ Works | Requires AppIndicator on host |
| File share access | ✅ Works | Standard filesystem access |
| Folder sync (rclone) | ✅ Works | Requires rclone + FUSE on host |
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

### Check current limit
```bash
cat /proc/sys/fs/inotify/max_user_watches
```

---

## Troubleshooting

### "WebKitGTK not found" during build
Install the appropriate WebKitGTK development package:
```bash
# Debian 12 / Ubuntu 24.04
sudo apt install libwebkitgtk-6.0-dev

# Debian 11 / Ubuntu 22.04
sudo apt install libwebkit2gtk-4.1-dev
```

### AppImage fails to start
1. Check for missing libraries:
   ```bash
   ./ProtonDrive-*.AppImage --appimage-extract
   ldd squashfs-root/usr/bin/proton-drive | grep "not found"
   ```

2. Install missing runtime libraries:
   ```bash
   # Ubuntu/Debian
   sudo apt install libwebkit2gtk-4.1-0
   ```

### System tray icon not showing
- Ensure AppIndicator is installed:
  ```bash
  sudo apt install libayatana-appindicator3-1
  ```
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
- [ ] Application launches
- [ ] Login/authentication works
- [ ] Web interface loads correctly
- [ ] File browsing works
- [ ] File upload works
- [ ] File download works

### System Integration
- [ ] System tray icon appears
- [ ] Tray menu is functional
- [ ] Close button minimizes to tray
- [ ] Desktop entry appears in app menu
- [ ] Application icon displays correctly

### Folder Sync (if configured)
- [ ] rclone setup completes
- [ ] Two-way sync works
- [ ] inotify file watching triggers syncs
- [ ] Sync status shows in tray

### AppImage-Specific
- [ ] AppImage runs without extraction
- [ ] Desktop integration works after first run
- [ ] Icons display correctly
- [ ] WebClients assets load properly

---

## Reporting Issues

When reporting issues, please include:

1. **Distribution info**:
   ```bash
   cat /etc/os-release
   ```

2. **Package versions**:
   ```bash
   pkg-config --modversion webkit2gtk-4.1 || pkg-config --modversion webkitgtk-6.0
   pkg-config --modversion gtk4 || pkg-config --modversion gtk+-3.0
   ```

3. **Application logs**:
   ```bash
   cat ~/.cache/proton-drive/proton-drive.log
   ```

4. **Crash logs** (if applicable):
   ```bash
   cat ~/.cache/proton-drive/crash.log
   ```

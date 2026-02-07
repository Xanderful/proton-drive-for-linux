# Copilot Instructions for Proton Drive Linux

## Project Overview

Native C++ GTK4 desktop client for Proton Drive sync on Linux. Pure GTK4 implementation with drag-drop sync, cloud browser, and real-time folder synchronization via rclone.

## Architecture

```
src-native/src/           ← C++ GTK4 application
  main_native.cpp         ← Entry point
  app_window.cpp          ← Main GTK4 window, drag-drop, cloud browser UI
  sync_manager.cpp        ← Sync job orchestration, rclone integration
  file_index.cpp          ← SQLite FTS5 cloud file search cache
  tray_gtk4.cpp           ← StatusNotifierItem D-Bus system tray
  file_watcher.cpp        ← inotify real-time file change detection

scripts/                  ← Build orchestration, rclone setup, packaging
```

## Build

```bash
# Build
mkdir -p src-native/build && cd src-native/build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Run
./proton-drive
```

Or use make shortcuts:
```bash
make build          # Build release
make dev            # Build and run with --debug flag
```

## System Requirements

- GTK4 (`libgtk-4-dev` / `gtk4-devel` / `gtk4`)
- libcurl (`libcurl4-openssl-dev` / `libcurl-devel` / `curl`)
- SQLite3 (`libsqlite3-dev` / `sqlite-devel` / `sqlite`)
- OpenSSL (`libssl-dev` / `openssl-devel` / `openssl`)
- rclone (runtime, for sync)

## Key Patterns

### Logging & Debugging
```cpp
Logger::info("message");   // → ~/.cache/proton-drive/proton-drive.log
Logger::debug("message");  // Only when --debug flag
```
Crash dumps: `~/.cache/proton-drive/crash.log`

### SQLite File Index
`file_index.cpp` caches cloud files for instant search:
- Uses `rclone lsjson --recursive proton:/`
- FTS5 full-text search
- Location: `~/.cache/proton-drive/file_index.db`

### rclone Sync Integration
Folder sync via systemd user service:
```bash
make setup-sync     # Configure rclone + create service
make sync-start     # Start sync
```
See `sync_manager.cpp` for job management.

## File Reference

| File | Purpose |
|------|---------|
| `app_window.cpp` | Main UI: drop zone, cloud browser, progress overlay, logs panel |
| `sync_manager.cpp` | Sync job lifecycle, rclone command building |
| `sync_job_metadata.cpp` | Job registry, device identity |
| `file_index.cpp` | Cloud file cache with FTS5 search |
| `file_watcher.cpp` | inotify-based real-time sync triggers |
| `tray_gtk4.cpp` | D-Bus StatusNotifierItem tray icon |

## Do's and Don'ts

✅ **DO:**
- Run `make dev` before pushing changes
- Use `Logger::info/debug` for logging
- Test sync flows manually (create/delete/conflict scenarios)

❌ **DON'T:**
- Hardcode paths—use `getenv("HOME")` and relative paths
- Block the GTK main thread with long operations
- Skip error handling for rclone/file operations

# Sync Safety Features

**Last Updated:** February 1, 2026

This document describes the comprehensive safety and conflict resolution features built into Proton Drive Linux for protecting your data during synchronization operations.

## Overview

The sync safety system provides multiple layers of protection to prevent data loss during cloud synchronization:

1. **Delete Protection** - Prevents accidental mass deletion
2. **Conflict Resolution** - Handles files modified on both sides
3. **File Versioning** - Keeps backups of overwritten files
4. **Pre-Sync Verification** - Validates paths before operations
5. **Graceful Shutdown** - Clean exit during interruptions
6. **Lock Management** - Auto-recovery from stale locks

---

## 1. Delete Protection

### Purpose
Prevents catastrophic data loss when a large number of files would be deleted during sync (e.g., syncing an empty folder to cloud, or accidentally deleting a folder before sync runs).

### How It Works
- **Default**: Aborts sync if more than **50%** of files would be deleted
- **Configurable**: 10-100% via Preferences → Safety tab
- **Implementation**: Uses rclone's `--max-delete` flag

### Example Scenarios

**Protected:**
```
Local folder: 1000 files
Cloud folder: 0 files (accidentally deleted)
Sync attempt: Would delete 1000 files (100%) → ABORTED
```

**Allowed:**
```
Local folder: 1000 files
Changed/deleted: 300 files (30%) → Sync proceeds
```

### Configuration
```json
// ~/.config/proton-drive-linux/settings.json
{
  "max_delete_percent": 50  // Range: 10-100
}
```

---

## 2. Conflict Resolution

### Purpose
When the same file is modified on both local and remote sides between syncs, the system must decide how to handle the conflict.

### Resolution Strategies

| Strategy | Behavior | Use Case |
|----------|----------|----------|
| **Keep Both** (default) | Renames conflicting file with timestamp | Safest - never lose data |
| **Newer Wins** | File with most recent modification time wins | Trust timestamps |
| **Larger Wins** | Larger file wins | Assume larger = more content |
| **Local Wins** | Local file always overwrites remote | Trust local edits |
| **Remote Wins** | Remote file always overwrites local | Trust cloud version |
| **Ask** | Show dialog for user to choose | Maximum control |

### Conflict File Naming
When "Keep Both" is selected, conflicting files are renamed:
```
Original:     document.txt
Conflict:     document_conflict-2026-02-01-143022.txt
```

Pattern: `{filename}_conflict-{YYYY-MM-DD-HHMMSS}.{ext}`

### Conflict Behavior

**Wait (Batch Review)**
- Conflicts are resolved automatically during sync
- User reviews conflict files after sync completes
- Best for unattended syncs

**Immediate (Interactive)**
- Sync pauses on each conflict
- Dialog prompts user to choose which version to keep
- Best for important files

### Configuration
```json
{
  "conflict_resolve": "both",      // both|newer|larger|path1|path2|ask
  "conflict_behavior": "wait"      // wait|immediate
}
```

### Technical Details
```bash
# Flags passed to rclone bisync
--conflict-resolve newer         # Strategy
--conflict-loser num             # Number conflicting files
--conflict-suffix _conflict-{{.Time}}  # Timestamp suffix
```

---

## 3. File Versioning

### Purpose
Keeps local backups of files that are overwritten during sync operations. Note: Proton Drive also maintains cloud-side version history.

### How It Works
- **Backup Location**: `.rclone_backups/` inside each synced folder
- **Structure**: Mirrors the folder structure
- **Retention**: Manual cleanup required (not auto-deleted)

### Example
```
~/Documents/Work/
  ├── report.pdf (current version)
  └── .rclone_backups/
      └── 2026-02-01-143022/
          └── report.pdf (previous version)
```

### When Backups Are Created
- File overwritten during sync
- File modified locally before remote changes applied
- Conflict resolution chooses one version over another

### Configuration
```json
{
  "enable_versioning": false  // true to enable
}
```

### Managing Backups
```bash
# View all backups for a synced folder
ls -la ~/Documents/Work/.rclone_backups/

# Restore a file
cp .rclone_backups/2026-02-01-143022/report.pdf report.pdf

# Clean old backups (older than 30 days)
find .rclone_backups/ -type f -mtime +30 -delete
```

### Important Notes
- Backups are **local only** - not synced to cloud
- `.rclone_backups/` folders are excluded from sync
- Proton Drive maintains cloud-side versioning independently

---

## 4. Pre-Sync Verification

### Purpose
Verifies that both local and remote paths are accessible before starting sync, preventing partial syncs or data loss from missing paths.

### How It Works
1. Creates a `RCLONE_TEST` file in the local sync folder
2. Passes `--check-access` flag to rclone
3. rclone verifies both paths contain the test file
4. Aborts sync if either path is inaccessible

### Configuration
```json
{
  "check_access_enabled": false  // true to enable
}
```

### When to Enable
- Network drives that may disconnect
- External storage that may be unmounted
- Remote servers with intermittent connectivity

### Test File Location
```
~/Documents/Work/RCLONE_TEST
```
Contains: "rclone test file - do not delete"

---

## 5. Graceful Shutdown

### Purpose
Ensures clean exit when sync is interrupted (Ctrl+C, system shutdown, app termination) to prevent corrupted files or stale locks.

### Implementation

**FileWatcher (C++)**
```cpp
// Registers SIGINT/SIGTERM handlers
signal(SIGINT, graceful_shutdown_handler);
signal(SIGTERM, graceful_shutdown_handler);

// Handler calls:
watcher->stop();  // Clean inotify teardown
```

**Bash Script**
```bash
# Trap signals and forward to rclone
trap graceful_shutdown SIGINT SIGTERM SIGHUP

graceful_shutdown() {
    kill -INT $RCLONE_PID  # Send SIGINT to rclone
    wait $RCLONE_PID       # Wait up to 30 seconds
    # Force kill if still running
}
```

### What Happens During Graceful Shutdown
1. Signal received (Ctrl+C, kill, system shutdown)
2. FileWatcher stops monitoring
3. rclone receives SIGINT
4. rclone finishes current file transfer
5. rclone writes state to cache
6. Clean exit (no stale locks)

### Timeout
- **Wait time**: 30 seconds for rclone to finish gracefully
- **Force kill**: After timeout, `SIGKILL` is sent

---

## 6. Lock Management

### Purpose
Prevents multiple bisync processes from running on the same paths simultaneously, and auto-recovers from stale locks left by crashed processes.

### Lock Files
```
~/.cache/rclone/bisync/{path1}..{path2}.lck
```

Contains JSON:
```json
{
  "Session": "unique-session-id",
  "PID": "12345",
  "Start": "2026-02-01T14:30:22Z"
}
```

### Auto-Healing: Stale Lock Removal
**Before every sync:**
```bash
# Check if lock file exists
# Read PID from lock file
# Test if process is still running: kill -0 $PID
# If dead: rm lock_file
```

### Lock Expiry
- **Default**: 2 hours (`--max-lock 2h`)
- **Behavior**: Locks older than 2 hours are automatically ignored/overwritten
- **Prevents**: Permanent deadlock from crashed processes

### Manual Lock Removal
```bash
# Clear all bisync locks (use with caution)
rm -rf ~/.cache/rclone/bisync/*.lck

# Or via Preferences → Advanced → Clear Sync Cache
```

---

## Configuration Reference

### UI: Preferences → Safety Tab

| Setting | Description | Default | Range/Options |
|---------|-------------|---------|---------------|
| **Abort if deleting more than** | Max delete percentage | 50% | 10-100% |
| **Conflict resolution** | How to handle conflicts | Keep Both | 6 options |
| **When to resolve conflicts** | Timing of resolution | After sync | Wait/Immediate |
| **Keep local backups** | Enable versioning | Disabled | On/Off |
| **Verify paths before sync** | Pre-sync check-access | Disabled | On/Off |

### Settings File

Location: `~/.config/proton-drive-linux/settings.json`

```json
{
  "max_delete_percent": 50,
  "conflict_resolve": "both",
  "conflict_behavior": "wait",
  "enable_versioning": false,
  "check_access_enabled": false,
  
  "delta_sync": true,
  "excluded_folders": "node_modules,.git,__pycache__",
  "upload_limit": 0,
  "download_limit": 0,
  "bw_scheduling_enabled": false,
  "work_hour_start": 9,
  "work_hour_end": 17,
  "work_hour_limit_kb": 500
}
```

### Bash Script Flags

Location: `scripts/manage-sync-job.sh` (lines 541-627)

```bash
# Safety flags applied to rclone bisync
--max-delete ${max_delete_percent}    # Percentage value (e.g., 50 = 50%) - no % symbol for bisync
--conflict-resolve ${conflict_resolve}
--conflict-loser num
--conflict-suffix _conflict-{{.Time}}
--max-lock 2h
--backup-dir "${local_path}/.rclone_backups"  # If versioning enabled
--check-access  # If check_access_enabled
```

---

## Best Practices

### Recommended Settings

**For Important Data (Documents, Photos)**
```json
{
  "max_delete_percent": 30,
  "conflict_resolve": "ask",
  "conflict_behavior": "immediate",
  "enable_versioning": true,
  "check_access_enabled": true
}
```

**For Development Projects**
```json
{
  "max_delete_percent": 50,
  "conflict_resolve": "newer",
  "conflict_behavior": "wait",
  "enable_versioning": false,
  "check_access_enabled": false,
  "excluded_folders": "node_modules,.git,dist,build,__pycache__,.venv"
}
```

**For Media Libraries (Read-mostly)**
```json
{
  "max_delete_percent": 70,
  "conflict_resolve": "larger",
  "conflict_behavior": "wait",
  "enable_versioning": false,
  "check_access_enabled": false
}
```

### Safety Checklist

- [ ] Set max_delete_percent below 50% for critical folders
- [ ] Test conflict resolution with dummy files before production use
- [ ] Enable versioning for frequently-edited documents
- [ ] Periodically clean .rclone_backups/ to save space
- [ ] Use check_access for network/external drives
- [ ] Exclude build artifacts and dependencies from sync
- [ ] Monitor `~/.config/proton-drive/sync-manager.log` for errors

---

## Troubleshooting

### Sync Aborted: "Would delete X% of files"
**Cause**: max_delete protection triggered

**Solution**:
1. Verify local/remote folders are correct
2. Check if accidental deletion occurred
3. If intentional, temporarily increase max_delete_percent
4. Or force resync: `bash scripts/manage-sync-job.sh force-resync <job-id>`

### Too Many Conflict Files
**Cause**: Files modified on both sides frequently

**Solutions**:
- Switch to "newer" or "larger" conflict resolution
- Reduce sync interval (more frequent syncs = fewer conflicts)
- Use single-direction sync (sync vs bisync)
- Coordinate edits to avoid simultaneous changes

### Lock File Errors
**Symptoms**:
```
Failed to get lock: lock file is being held by another process
```

**Solutions**:
1. Wait 5 minutes (stale lock removal runs before each sync)
2. Manual removal: `rm ~/.cache/rclone/bisync/*.lck`
3. Check for hung rclone processes: `ps aux | grep rclone`
4. Kill hung process: `pkill -9 rclone`

### Backup Directory Growing Large
**Solutions**:
```bash
# Check size
du -sh ~/Documents/Work/.rclone_backups/

# Delete backups older than 7 days
find ~/Documents/Work/.rclone_backups/ -type f -mtime +7 -delete

# Or disable versioning in preferences
```

### Graceful Shutdown Not Working
**Symptoms**: Stale locks after Ctrl+C

**Debugging**:
1. Check if signal handlers registered: `ps aux | grep proton-drive`
2. Monitor log during shutdown: `tail -f ~/.config/proton-drive/sync-manager.log`
3. Verify bash trap: `bash -x scripts/manage-sync-job.sh run ...`

---

## Architecture Details

### Component Interaction

```
┌─────────────────────────────────────────────────────────────┐
│ User modifies Preferences → Safety tab                       │
│   └─> settings.json updated                                  │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│ FileWatcher detects change                                   │
│   └─> Signal handlers registered (SIGINT/SIGTERM)           │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│ manage-sync-job.sh launched                                  │
│   ├─> Reads settings.json                                    │
│   ├─> Builds rclone flags (max-delete, conflict-*, etc.)    │
│   ├─> Registers graceful_shutdown trap                       │
│   └─> Executes rclone bisync with safety flags              │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│ rclone bisync running                                        │
│   ├─> Check-access verification (if enabled)                │
│   ├─> Max-delete check before applying deletes              │
│   ├─> Conflict resolution on file-by-file basis             │
│   ├─> Backup to .rclone_backups/ (if versioning enabled)    │
│   ├─> Lock file managed (2h expiry)                         │
│   └─> Graceful shutdown on SIGINT                           │
└─────────────────────────────────────────────────────────────┘
```

### File Locations

| Path | Purpose |
|------|---------|
| `~/.config/proton-drive-linux/settings.json` | User preferences |
| `~/.config/proton-drive/sync-manager.log` | Sync operation logs |
| `~/.config/proton-drive/jobs/*.conf` | Sync job configurations |
| `~/.cache/rclone/bisync/` | Bisync state and lock files |
| `{sync_folder}/.rclone_backups/` | Version history backups |
| `{sync_folder}/RCLONE_TEST` | Check-access verification file |

---

## Testing

See `scripts/test-sync-safety.sh` for automated testing of safety features.

### Manual Testing

**1. Test Max-Delete Protection**
```bash
# Create test folder with 100 files
mkdir -p /tmp/test-sync
seq 1 100 | xargs -I {} touch /tmp/test-sync/file{}.txt

# Set max_delete to 30%
# Delete 40 files locally (>30%)
rm /tmp/test-sync/file{1..40}.txt

# Run sync - should abort
# Check log: "Would delete 40% of files"
```

**2. Test Conflict Resolution**
```bash
# Modify same file on both local and remote
# Set conflict_resolve to "both"
# Run sync
# Verify conflict file created: document_conflict-{timestamp}.txt
```

**3. Test Versioning**
```bash
# Enable versioning
# Edit a file
# Run sync (file gets overwritten from remote)
# Check .rclone_backups/ for previous version
```

**4. Test Graceful Shutdown**
```bash
# Start sync
# Press Ctrl+C
# Check no .lck files remain in ~/.cache/rclone/bisync/
```

---

## See Also

- [Sync Setup Guide](../SYNC_SETUP.md) - Initial sync configuration
- [Cross-Distro Testing](CROSS_DISTRO_TESTING.md) - Testing on different Linux distributions
- [File Index Security](FILE_INDEX_SECURITY.md) - File search encryption and security
- [Worker Debugging](../WORKER_DEBUGGING.md) - Debugging Web Workers and IPC

---

## References

- [rclone bisync documentation](https://rclone.org/bisync/)
- [rclone conflict resolution](https://rclone.org/bisync/#conflict-resolution)
- [rclone safety features](https://rclone.org/docs/#max-delete)

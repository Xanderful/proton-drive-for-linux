# File Index Security & Architecture

## Current Status ✅ IMPLEMENTED

The file index at `~/.cache/proton-drive/file_index.db` now has the following security features:

### ✅ What's Implemented
- **AES-256-GCM encryption** of the database at rest
- **Machine-specific key storage** - key can only be decrypted on the same device
- Fast FTS5 full-text search across all cloud files
- SQLite database with ~20,000+ files indexed
- Automatic refresh when stale (>24 hours)
- Background indexing with progress feedback
- **Real-time incremental updates** when files are synced
- **Correct sync status detection** for files inside synced folders

### How Encryption Works

1. **On First Run**: A random 256-bit encryption key is generated
2. **Key Storage**: The key is encrypted using a machine-specific key derived from:
   - `/etc/machine-id` (unique per Linux installation)
   - PBKDF2 with 100,000 iterations for key derivation
3. **Keyfile Location**: `~/.local/share/proton-drive/.keyfile` (mode 600)
4. **On Startup**: Database is decrypted in memory for use
5. **On Shutdown**: Database is re-encrypted with AES-256-GCM

### Security Properties

- **At Rest**: Database is encrypted, unreadable without the keyfile
- **Machine-Bound**: Keyfile only works on the original machine (tied to `/etc/machine-id`)
- **Authenticated Encryption**: GCM mode detects tampering

### Limitations

- If `/etc/machine-id` changes (e.g., reinstall OS), you'll lose access to the encrypted database (a new one will be generated)
- The key is held in memory while the app is running
- For maximum security, also enable full-disk encryption (LUKS)

---

## Previous Issues (RESOLVED)

### ~~1. UNENCRYPTED DATABASE~~ ✅ FIXED
**Was**: Database exposed filenames if laptop was stolen
**Now**: AES-256-GCM encryption protects data at rest

### ~~2. No Real-Time Updates~~ ✅ FIXED
**Was**: Index only refreshed on app startup if >24 hours old
**Now**: FileWatcher integration updates index when sync jobs complete

### ~~3. Sync Status Detection Bug~~ ✅ FIXED
**Was**: Files inside synced folders showed as "Cloud only"
**Now**: Correctly detects files inside synced folders

---

## Implementation Details

### Encryption (crypto.cpp)
```cpp
// Key derivation: PBKDF2-HMAC-SHA256, 100,000 iterations
// Encryption: AES-256-GCM with 12-byte IV, 16-byte tag
// Key storage: Encrypted with machine-derived key
```

### Incremental Index Updates (file_index.cpp)
```cpp
// Called when FileWatcher detects sync completion
void update_files_from_sync(job_id, local_path, remote_path);

// Called for individual file updates
void add_or_update_file(remote_path, name, size, mod_time, ...);
void remove_file(remote_path);
```

### Sync Manager Integration (sync_manager.cpp)
```cpp
// After triggering sync job, also update index
trigger_job_sync(job_id) {
    // ... trigger sync ...
    // Update file index for this folder
    FileIndex::getInstance().update_files_from_sync(...);
}
```

---

## Multi-Device Strategy

### Recommendation: Keep Index Local Per Device

**Reasons:**
1. **Conflict prevention**: No merge conflicts if devices update simultaneously
2. **Security**: Each device has its own encryption key
3. **Performance**: Local index is faster than syncing database
4. **Simplicity**: No cross-device coordination needed

**Trade-off**: Each device builds its own index on first run (~5-10 min for 20,000 files)

---

## Additional Security Recommendations

1. **Enable Full-Disk Encryption (LUKS)** - Provides defense-in-depth
2. **Use a Screen Lock** - Prevent access when away
3. **Regular Backups** - The keyfile is unique to your machine
2. **Performance**: Index rebuilds are fast enough (5-10 min for 20k files)
3. **Privacy**: Even encrypted, syncing the index could expose access patterns to Proton
4. **Storage**: Each device may have different local sync folders, so indexes differ

**Alternative**: Each device maintains its own index and rebuilds on first launch

### 4. Sync Status Detection (FIXED)

#### Previous Bug
```cpp
// Only checked exact path match
if (item.path == job.remote_path) {
    item.is_synced = true;
}
```

#### Fixed Logic
```cpp
// Now checks if item is INSIDE a synced folder
if (item.path.find(job.remote_path + "/") == 0) {
    item.is_synced = true;
    // Construct correct local path
    std::string relative_path = item.path.substr(job.remote_path.length() + 1);
    item.local_path = job.local_path + "/" + relative_path;
}
```

**Result**: Files in `proton:/Documents/Backup/Obsidian/...` now correctly show as synced if `proton:/Documents` is synced

## Implementation Priority

### Phase 1: Critical Fixes (This PR)
- ✅ Fix sync status detection for nested files
- ✅ Add warning logs about unencrypted database
- ✅ Document security considerations

### Phase 2: Real-Time Updates
- Integrate with FileWatcher to update index when files sync
- Add incremental index updates instead of full refresh
- Add "Refresh Index" button in UI for manual updates

### Phase 3: Encryption (Optional)
- Evaluate user demand for encrypted index
- If needed, implement SQLCipher integration
- Alternative: Document disk encryption requirements

## User Recommendations

### For Maximum Security
1. **Enable full-disk encryption (LUKS)**:
   ```bash
   # On Ubuntu/Debian during installation, select "Encrypt disk"
   ```

2. **Use strong Proton password**: The index doesn't contain file contents, but filenames can reveal sensitive info

3. **Manual index refresh**: If you add sensitive files, manually rebuild the index from the UI

### For Multi-Device Users
- Each device will build its own index on first launch
- Indexes don't sync between devices (this is intentional)
- If you add files on Device A, Device B will see them after its next index refresh

## Testing the Fix

### Verify Sync Status Detection
1. Open Cloud Browser
2. Navigate to a folder inside a synced directory (e.g., `Documents/Subfolder`)
3. Files should show "✓ Synced (This PC)" instead of "☁ Cloud only"
4. Double-clicking should open the local file instantly (no download)

### Verify Search Works
1. Use the search bar at the top of Cloud Browser
2. Search for a filename you know exists
3. Click the result - it should navigate or open the file

## Future Enhancements

### Potential Features
- [ ] Encrypt index with user's Proton credentials
- [ ] Real-time index updates via FileWatcher integration
- [ ] Incremental indexing (only scan changed folders)
- [ ] Index statistics in UI (files indexed, last refresh time)
- [ ] Manual index rebuild button with progress indicator
- [ ] Index integrity checking on startup
- [ ] Configurable index refresh interval (currently hardcoded 24 hours)

### Performance Optimizations
- [ ] Use `rclone lsjson --fast-list` (already implemented)
- [ ] Parallel indexing of multiple folders
- [ ] Delta updates instead of full index rebuild
- [ ] Compress database with `PRAGMA auto_vacuum=INCREMENTAL`

## References

- SQLCipher documentation: https://www.zetetic.net/sqlcipher/
- SQLite FTS5: https://www.sqlite.org/fts5.html
- rclone lsjson: https://rclone.org/commands/rclone_lsjson/

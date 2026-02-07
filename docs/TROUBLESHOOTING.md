# Troubleshooting Guide

Complete guide to diagnosing and fixing common Proton Drive Linux issues.

---

## Table of Contents

1. [Authentication Issues](#authentication-issues)
2. [Sync Problems](#sync-problems)
3. [Performance & Resource Usage](#performance--resource-usage)
4. [File Operations](#file-operations)
5. [System Integration](#system-integration)
6. [Advanced Debugging](#advanced-debugging)

---

## Authentication Issues

### Problem: "Login button doesn't work" or "Browser doesn't open"

**Symptoms:**
- Click login → nothing happens
- Browser window doesn't appear
- Stuck at "Logging in..."

**Solutions:**

**Step 1: Check if default browser is set**
```bash
# View what browser would open
xdg-open --version

# Set default browser explicitly
xdg-settings set default-web-browser firefox.desktop
```

**Step 2: Manually open browser and complete login**
```bash
# Log in manually via browser
firefox https://account.protonmail.com/login

# Then copy this URL to get authorization code
https://account.protonmail.com/oauth/authorize?client_id=YOUR_CLIENT_ID

# Restart app and try login again
```

**Step 3: Check error logs**
```bash
# View recent error logs
tail -50 ~/.cache/proton-drive/proton-drive.log | grep -i "auth\|login"

# Look for messages like:
# ERROR: Failed to open browser
# ERROR: OAuth timeout
```

**Step 4: Clear cached credentials and retry**
```bash
# Remove stored credentials
secret-tool delete service proton-drive

# Remove cached token files
rm -f ~/.cache/proton-drive/.auth_cache

# Restart app
```

---

### Problem: "Login successful but says 'Not Logged In'"

**Symptoms:**
- See notification "Login successful"
- But UI still shows "Not Logged In"
- Cloud browser is empty

**Root Cause:** Token stored but not loaded on startup

**Solutions:**

```bash
# Force token reload
killall proton-drive
sleep 1
proton-drive

# If still not working, check keyring
secret-tool search service proton-drive
# Should output one entry with access_token

# If empty, keyring is broken, redo login
secret-tool delete service proton-drive
# (then login again in app)
```

---

### Problem: "401 Unauthorized" error after a while

**Symptoms:**
- Works fine for a while
- Then suddenly "Invalid credentials" error
- Sync fails with authorization errors

**Root Cause:** Access token expired, refresh failed

**Solutions:**

```bash
# Clear credentials entirely
secret-tool delete service proton-drive

# Restart app and re-login
killall proton-drive
proton-drive

# If refresh token is corrupted, this will fix it
```

**Check if multifactor is enabled:**
- Usually not the issue, but if Proton account has 2FA, ensure app was authorized
- Try re-logging in via browser directly to verify credentials work

---

## Sync Problems

### Problem: "Sync stuck or stalled"

**Symptoms:**
- Sync shows "In Progress" but no progress for minutes
- Status frozen at old timestamp
- Sync never completes

**Solutions:**

**Step 1: Check if rclone is actually running**
```bash
ps aux | grep rclone

# Should show rclone processes if sync is active
# If empty, sync may be paused or stuck

# Kill any stale rclone processes
killall -9 rclone
```

**Step 2: Check for stale locks**
```bash
# Stale bisync cache locks prevent new syncs
rm -f ~/.cache/rclone/bisync/*cache*

# Also remove any .lock files
find ~/.cache/rclone -name "*.lock" -delete

# Restart app and try sync again
```

**Step 3: Check logs for errors**
```bash
# Enable debug mode and try sync
proton-drive --debug

# Watch logs in real-time
tail -f ~/.cache/proton-drive/proton-drive.log

# Look for:
# - "Sync failed: ..."
# - "Permission denied"
# - "Connection timeout"
# - "Disk space low"
```

**Step 4: Manual sync with rclone to diagnose**
```bash
# Try manual rclone command with verbose output
rclone bisync ~/Documents proton:/Documents -vv 2>&1 | head -100

# Shows exactly where it's failing
# Common errors:
# - "403 Forbidden" → Proton account permission issue
# - "ENOSPC" → Disk full
# - "ETIMEDOUT" → Network timeout (try again later)
```

---

### Problem: "Files not uploading to cloud"

**Symptoms:**
- Create local file, doesn't appear in cloud
- Sync says "Complete" but file missing
- Upload seemed to work but file vanished

**Solutions:**

**Step 1: Verify file actually exists locally**
```bash
ls -lah ~/Documents/myfile.txt

# If missing, file was deleted before sync ran
# (Check Recycle Bin / trash)
```

**Step 2: Check if file is in a synced folder**
```bash
# In app, click "Sync Jobs" and verify:
# - Local path matches where you created file
# - Sync is enabled (not paused)
# - File isn't inside a conflicted folder

# Example: if syncing ~/Documents but file at ~/Downloads/myfile.txt
# → File won't sync (different folder)
```

**Step 3: Check for upload errors**
```bash
# View sync logs
tail -50 ~/.cache/proton-drive/proton-drive.log | grep -i "upload\|error"

# Common errors:
# - "422 Invalid Request" → rclone upload bug (see SYNC_SETUP.md for fix)
# - "File already exists" → Name conflict
# - "Storage quota exceeded" → Proton Drive is full
```

**Step 4: Force manual sync**
```bash
# Click "Sync Now" button for that job
# Or manual CLI sync:
rclone sync ~/Documents proton:/Documents -v
```

**Step 5: Verify rclone can access Proton**
```bash
# List remote files manually
rclone ls proton:/ | head -20

# If this fails (e.g., "403 Forbidden"):
# - Credentials are wrong (redo login)
# - Proton account permissions issue
# - rclone needs update (see SYNC_SETUP.md)
```

---

### Problem: "Files not downloading from cloud"

**Symptoms:**
- Upload file in Proton Drive web app
- Local folder doesn't get the file
- Checked cloud browser, file is there

**Root Cause:** Cloud-to-local sync uses polling (not real-time)

**Solutions:**

**Step 1: Understand the timing**
```
By design:
- Local → Cloud: Immediate (inotify-based) ✅
- Cloud → Local: Polled (every 15 min default) ⏱️

Your file in cloud was uploaded 2 minutes ago?
→ Wait for next sync interval (default 15 min)
→ Or manually click "Sync Now"
```

**Step 2: Manually trigger sync**
- Click "Sync Now" button in Sync Jobs panel
- Or via CLI: `rclone bisync ~/Documents proton:/Documents`

**Step 3: Reduce sync interval for faster polling**
```
If doing frequent cloud edits:
→ Preferences → Sync → Set interval to 5 minutes
→ Or even 1 minute (uses more CPU/network)
```

**Step 4: Verify download actually tried to happen**
```bash
# Check logs for download activity
tail -100 ~/.cache/proton-drive/proton-drive.log | grep -i "download\|bisync"

# Look for:
# - "Downloading file.txt"
# - "Local: 1 dirs, 5 files"
# - "Remote: 1 dirs, 6 files" (should show sync downloaded 1)
```

**Step 5: Check if disk space is full**
```bash
# Check available space
df -h ~

# If less than 1 GB available:
# - Clean up local files
# - Delete old sync jobs you don't need
# - Increase disk space
```

---

### Problem: "Sync conflict keeps happening"

**Symptoms:**
- Same file keeps conflicting every sync
- Creates `file_conflict-2026-02-07-143000.txt` repeatedly
- Sync slow due to constant conflict resolution

**Root Cause:** File keeps being modified between syncs

**Solutions:**

**Step 1: Stop editing the file mid-sync**
```
Bad timing:
- Cloud version modified at 14:30
- Local version modified at 14:32 (before next sync)
- Sync runs at 14:45 → Conflict!

Solution:
- Wait for sync to complete before editing
- Or increase sync interval to give time for both sides to stabilize
```

**Step 2: Choose a conflict resolution strategy**
```
Preferences → Sync → Conflict Resolution:

- "Keep Both" (default)    → Rename local, keep remote
- "Newer Wins"             → Whatever was modified last
- "Larger Wins"            → File with more content
- "Local Wins"             → Always keep local version
- "Remote Wins"            → Always keep cloud version
- "Ask Me"                 → Dialog appears each time
```

**Step 3: Manually resolve existing conflicts**
```bash
# Find all conflict files
find ~ -name "*_conflict-*" -type f | head -20

# Choose which to keep:
# Option A: Delete conflicts, redo upload
rm ~/Documents/file_conflict-*
# (Then re-upload clean version)

# Option B: Rename conflict back to original
mv ~/Documents/file_conflict-2026-02-07-143000.txt ~/Documents/file.txt
# (Then re-sync to pick one version)
```

---

## Performance & Resource Usage

### Problem: "Sync is very slow"

**Symptoms:**
- First sync of large folder takes forever
- Upload/download speeds are slow
- App seems frozen during sync

**Solutions:**

**Step 1: Understand first sync is slower**
```
First sync: Large number of NEW files → Slow
- 1000 files: 2-5 minutes normal
- 10000 files: 10-30 minutes normal
- Speed depends on file sizes and network

Incremental sync: Just changed files → Fast
- 10 files changed: 30 seconds
```

**Step 2: Check network speed**
```bash
# Test internet upload/download
speedtest-cli

# Slow network (< 5 Mbps):
# - 1 MB file = ~2 seconds
# - 100 MB file = ~3 minutes

# Increase sync interval to reduce CPU usage:
# Preferences → Sync → 30 minutes (instead of 15)
```

**Step 3: Check for CPU/disk bottlenecks**
```bash
# Watch real-time system usage
top -p $(pgrep proton-drive)

# High CPU (> 50%) during sync = normal for large folder
# High CPU (> 50%) while idle = problem

# Check disk I/O
iostat -x 1 10

# If slow disk (< 50 MB/s):
# - Using HDD instead of SSD → expect slower sync
# - Use SSD for better performance
```

**Step 4: Exclude large files/folders**
```bash
# If you have ~1000 files but 100 are huge (100+ MB each):
# - Sync only what you need
# - Create separate sync job for archives (1x per week)

# Example:
# Job 1: ~/Documents (daily, excludes video/)
# Job 2: ~/Documents/Archives (weekly)
```

---

### Problem: "App uses too much memory"

**Symptoms:**
- Proton Drive uses 300+ MB RAM
- System feels sluggish
- App crashes due to out-of-memory

**Typical Memory Usage:**
```
Normal: 100-200 MB (includes rclone process)
Syncing large folder: 200-400 MB
With 50,000+ files: 300-500 MB
```

**If exceeding normal:**

**Step 1: Check if rclone process is runaway**
```bash
ps aux | grep rclone

# If multiple rclone processes, kill extras
killall -9 rclone

# Restart app
```

**Step 2: Reduce file index cache**
```bash
# Clear file index, will rebuild
rm ~/.cache/proton-drive/file_index.db
rm ~/.cache/proton-drive/file_index.db.keyfile

# Restart app (rebuilds index)
```

**Step 3: Check for memory leaks (if bug)**
```bash
# Run with memory tracking (if supported)
proton-drive --valgrind

# Save output and report as issue
```

---

## File Operations

### Problem: "Can't delete file from cloud browser"

**Symptoms:**
- Right-click file → "Delete" grayed out
- Delete button doesn't work
- Error: "Permission denied"

**Solutions:**

**Step 1: Check delete protection**
```
Preferences → Safety → Max Delete %

If set to 0%, no deletes allowed!
→ Change to 10-50% to allow some deletions
```

**Step 2: Check file permissions in Proton**
```bash
# If folder is shared read-only:
# - Can't delete files in Proton web UI either
# - Contact folder owner to grant delete permission
```

**Step 3: Manual deletion via rclone**
```bash
rclone delete proton:/Documents/oldfile.txt -v

# If this fails:
# → Permission issue (not app issue)
```

---

### Problem: "Drag-drop upload doesn't work"

**Symptoms:**
- Drag file onto cloud browser → no upload
- Upload window shows but progress stuck
- "Failed to upload" error

**Solutions:**

**Step 1: Check if sync is paused**
```
Cloud browser drag-drop requires active sync!
→ Ensure job is enabled (green checkmark in Sync Jobs panel)
```

**Step 2: Try manual upload instead**
```
Preferences → Sync Jobs → Target folder
→ Copy file manually to local path
→ Sync will upload automatically
```

**Step 3: Check destination folder exists in cloud**
```bash
# Via cloud browser, verify /folder exists
rclone ls proton:/folder

# If not found:
# → Create folder in Proton web app first
# → Then retry drag-drop
```

---

## System Integration

### Problem: "Tray icon doesn't appear"

**Symptoms:**
- No icon in system tray
- Can't minimize to tray
- No access to quick menu

**Root Cause:** Desktop environment doesn't support StatusNotifierItem protocol

**Solutions:**

**Step 1: Check if tray is installed**
```bash
# For GNOME, install tray extension
sudo apt install gnome-shell-extension-appindicator

# For KDE/Plasma, should work by default
# For XFCE, should work by default
# For i3/sway, needs manual tray setup
```

**Step 2: Fallback: Use app menu instead**
```
If tray doesn't work:
→ Use main application menu
→ Click "Sync Jobs" to see status
→ Set "Start Minimized" disabled
→ Always see main window
```

---

### Problem: "systemd sync service stopped"

**Symptoms:**
- Sync was working, now "Service not running"
- Manual sync works but recurring sync doesn't
- systemd service missing

**Root Cause:** systemd service crashed or was disabled

**Solutions:**

**Step 1: Check service status**
```bash
# List sync services
systemctl --user list-units | grep proton-drive

# Check specific service
systemctl --user status proton-drive-sync-<job-id>

# If inactive/dead:
systemctl --user restart proton-drive-sync-<job-id>
```

**Step 2: Check systemd logs**
```bash
journalctl --user -u proton-drive-sync-<job-id> -n 50

# Look for errors like:
# - "rclone not found"
# - "Permission denied"
# - "Disk full"
```

**Step 3: Recreate service**
```bash
# In app, disable then re-enable the sync job:
→ Sync Jobs panel
→ Click job
→ Toggle off (stops service)
→ Toggle on (recreates service)
```

---

## Advanced Debugging

### Collecting Debug Information

**Enable debug logging:**
```bash
proton-drive --debug
```

**Collect logs for issue report:**
```bash
# All recent logs
cp ~/.cache/proton-drive/proton-drive.log ~/proton-drive-debug.log

# Plus system info
echo "=== System Info ===" >> ~/proton-drive-debug.log
uname -a >> ~/proton-drive-debug.log
lsb_release -a >> ~/proton-drive-debug.log

# Plus rclone version
echo "=== rclone Version ===" >> ~/proton-drive-debug.log
rclone version >> ~/proton-drive-debug.log

# Attach to issue report
```

---

### Manual Testing

**Test Proton API access:**
```bash
# Get current user info (requires valid token)
curl -s -H "Authorization: Bearer $TOKEN" \
  https://api.protonmail.com/api/users \
  | jq .

# If 401 → Token expired/invalid (redo login)
# If 403 → Account issue
# If 200 → API works fine
```

**Test rclone with Proton:**
```bash
# List files
rclone ls proton:/ -v

# Create test file
echo "test" > /tmp/test.txt
rclone copy /tmp/test.txt proton:/test-folder/

# Verify upload
rclone ls proton:/test-folder/

# Delete test
rclone delete proton:/test-folder/test.txt
```

**Simulate sync manually:**
```bash
rclone bisync ~/Documents proton:/Documents \
  --compare size,modtime \
  --conflict-resolve newer \
  --max-delete 50 \
  -vv 2>&1 | tee /tmp/bisync-debug.log

# Outputs detailed sync trace to view
cat /tmp/bisync-debug.log | less
```

---

### Still Stuck?

**Before reporting issue, provide:**
1. **Proton Drive Linux version**
   ```bash
   proton-drive --version
   ```

2. **System info**
   ```bash
   uname -a
   lsb_release -a
   ```

3. **Relevant debug logs**
   ```bash
   tail -100 ~/.cache/proton-drive/proton-drive.log
   ```

4. **Steps to reproduce** (exactly how to trigger the issue)

5. **Screenshots** of error messages or UI state

**Report issue:** https://github.com/Xanderful/proton-drive-linux/issues


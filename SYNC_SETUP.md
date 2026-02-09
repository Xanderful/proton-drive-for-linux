# Proton Drive Sync Setup Guide

## Overview

This project uses rclone for syncing files between your local folders and Proton Drive. Due to known issues with the current rclone release, you may need to install a patched version.

## ⚠️ Proton Sentinel Warning

**Important:** If you have **Proton Sentinel** enabled on your Proton account, it may block this application from syncing.

**Symptoms:**
- Login and authentication work correctly
- Cloud browser may load files
- Sync operations fail, timeout, or get blocked
- API requests return authentication errors after initial login

**Solutions:**

1. **Disable Proton Sentinel** (easiest):
   - Log into [account.proton.me](https://account.proton.me)
   - Go to **Security** settings
   - Turn off **Proton Sentinel**
   - Wait a few minutes for the change to propagate

2. **Keep Sentinel enabled** (requires support ticket):
   - Contact Proton Support at [proton.me/support](https://proton.me/support)
   - Request whitelisting for third-party rclone/API access
   - Mention you're using an unofficial Linux client with rclone backend
   - Wait for support to add your account to the whitelist

**Why?** Proton Sentinel is a security feature that monitors for suspicious account activity. Third-party API access (like rclone) can trigger false positives.

## Known Issues

### rclone Proton Drive Upload Bug (Issue #8870)

**Symptoms:**
- 422 errors when uploading files
- "Operation failed: Please retry" (Code=200501)
- "A file or folder with that name already exists" (Code=2500)
- "File or folder not found" (Code=2501) on revisions endpoint

**Root Cause:** 
The rclone v1.65.x and earlier versions are missing Proton Drive's block verification step. Each encrypted block must include a per-block verification token during upload.

**Status:** Fix available in PR [#9081](https://github.com/rclone/rclone/pull/9081) (pending merge). The AppImage builds rclone from [coderFrankenstain/rclone](https://github.com/coderFrankenstain/rclone/tree/fix-protondrive-upload) with this fix applied.

**Note:** Official rclone releases (v1.68.x, v1.72.x) do NOT include the fix yet. The AppImage includes a patched build from source.

## Installing Fixed rclone (CLI/Manual Sync Only)

### Option 1: Build from source (Recommended)

```bash
# Install Go if not present
# On Arch: sudo pacman -S go
# On Ubuntu: sudo apt install golang-go

# Clone and build fixed version
cd /tmp
git clone --depth 1 -b fix-protondrive-upload https://github.com/coderFrankenstain/rclone.git rclone-fix
cd rclone-fix
go build -o rclone-fixed

# Install to system
sudo cp rclone-fixed /usr/local/bin/
```

### Option 2: Use system package manager (NOT RECOMMENDED - lacks fix)

Official rclone packages do not include the Proton Drive upload fix yet. Use Option 1 instead.

```bash
# These versions have the upload bug:
sudo apt install rclone     # Ubuntu/Debian
sudo pacman -S rclone       # Arch
```

## Verifying Installation

```bash
# Check version (built from source)
/usr/local/bin/rclone-fixed version
# Should show: rclone v1.73.0-DEV (built from coderFrankenstain/rclone:fix-protondrive-upload)

# Test upload
mkdir -p /tmp/sync-test
echo "test" > /tmp/sync-test/test.txt
rclone-fixed mkdir proton:/TestSync
rclone-fixed copy /tmp/sync-test proton:/TestSync -v

# Verify upload
rclone-fixed ls proton:/TestSync/
# Should show: test.txt
```

## Setting Up Sync

### 1. Configure rclone remote

```bash
rclone config
# Choose: n (New remote)
# Name: proton
# Type: protondrive
# Follow prompts for username/password
```

### 2. Create a sync job via the app

1. Open Proton Drive Linux
2. Click the sync icon in the sidebar
3. Click "Add Job"
4. Select local and remote folders
5. Choose sync type (Two-Way recommended)
6. Set interval (15 minutes default)

### 3. Manual sync (CLI)

```bash
# One-way sync (local → remote)
rclone-fixed sync ~/Documents proton:/Documents --verbose

# Two-way sync (bisync)
rclone-fixed bisync ~/Documents proton:/Documents --resync --verbose
# Note: --resync only needed on first run
```

## Troubleshooting

### Ghost Files / Orphaned References

If you see "A file or folder with that name already exists" followed by "File or folder not found":

1. **Empty Proton Drive Trash:**
   - Log into [drive.proton.me](https://drive.proton.me)
   - Go to Trash
   - Empty trash completely
   - Wait 5-10 minutes for API to update

2. **Clear local bisync cache:**
   
   ```bash
   rm -rf ~/.cache/rclone/bisync/*.lck
   rm -rf ~/.cache/rclone/bisync/*.lst*
   ```

3. **Force resync:**
   
   ```bash
   rclone-fixed bisync ~/Documents proton:/Documents --resync --verbose
   ```

### Rate Limiting (429 Errors)

Proton Drive has rate limits. If you see "Too many recent API requests":

1. Wait 1-2 minutes before retrying
2. Reduce sync frequency (30m instead of 15m)
3. Use bandwidth limiting:
   
   ```bash
   rclone-fixed bisync ~/Documents proton:/Documents --bwlimit 1M --transfers 1
   ```

### Port Conflicts

If you see "bind: address already in use":

```bash
# Find and kill any stuck rclone processes
pkill -f "rclone.*rc-addr"
```

## Best Practices

1. **Start small:** Test with a small folder first
2. **Empty trash regularly:** Ghost files accumulate from failed syncs
3. **Use --resync carefully:** Only use on first sync or to reset state
4. **Monitor logs:** Check `~/.config/proton-drive/sync-manager.log`
5. **Disable VPN:** Proton VPN may cause issues with Proton Drive API

## Configuration

The sync manager automatically detects and uses `rclone-fixed` if available:

```bash
# Check which rclone is being used
cat ~/.config/proton-drive/sync-manager.log | grep "Using"
```

## References

- [rclone Proton Drive docs](https://rclone.org/protondrive/)
- [Issue #8870](https://github.com/rclone/rclone/issues/8870) - Upload failures
- [PR #9081](https://github.com/rclone/rclone/pull/9081) - Fix for upload issues
- [Proton Drive SDK](https://proton.me/blog/proton-drive-sdk-preview) - Official SDK preview

# Quick Start Guide - 5 Minutes to Your First Sync

Get Proton Drive Linux running and syncing files in less than 5 minutes.

## Prerequisites

- Linux (Debian, Ubuntu, Fedora, Arch, or any distribution)
- Proton Drive account (free or paid)
- Proton Drive Linux installed ([see installation](../README.md#-installation))

---

## Step 1: Launch the App (30 seconds)

```bash
proton-drive
```

Or find it in your application menu under **Internet** or **Utilities**.

The window opens with a cloud browser panel on the left (currently showing "Not logged in").

---

## Step 2: Log In (1 minute)

1. Click the **"Login"** button in the cloud browser
2. Your browser opens automatically to Proton's OAuth login page
3. Enter your **Proton Account email** and **password**
4. Approve the application request ("Allow this app")
5. Browser redirects and closes—you're logged in! ✅

**Where are my credentials?** Securely stored in your system keyring (XDG Secret Service).

---

## Step 3: Create Your First Sync Job (2 minutes)

### Via GUI (Recommended)

1. Click the **sync icon** in the left sidebar (or "Sync Jobs" menu)
2. Click **"Add Sync Job"** button
3. **Select Local Folder:** Choose `~/Documents` (or any folder)
4. **Select Cloud Folder:** Choose `/Documents` (or create a new folder)
5. **Sync Type:** Select **"Two-Way"** (recommended for most users)
6. **Interval:** Set to **15 minutes** (adjust later if needed)
7. Click **"Create and Enable"** ✅

Sync starts immediately! Files are now syncing between `~/Documents` and Proton Drive.

### Via Command Line (Alternative)

```bash
# Configure rclone (one-time setup)
proton-drive --setup-rclone

# Start a one-time sync
proton-drive --sync-now ~/Documents /Documents

# Or create a recurring sync job
proton-drive --add-sync ~/Documents /Documents --interval 15
```

---

## Step 4: Monitor Sync (1 minute)

### Watch Progress

1. **Sync Panel:** Shows active jobs, progress bars, and status
2. **System Tray:** Click the icon for quick status overview
3. **Notifications:** Desktop alerts when sync completes or errors occur
4. **Logs:** View detailed logs in **Preferences → Logs**

---

## You're Done! 🎉

Your files are now syncing. Here's what happens:

- ✅ **Local files → Cloud:** Uploaded within seconds of creation or modification
- ⏱️ **Cloud files → Local:** Downloaded at your sync interval (default 15 min)
- 🔒 **Safety:** Delete protection prevents accidental mass deletion
- 💬 **Conflicts:** Files modified on both sides are resolved automatically

---

## Next Steps

### Customize Sync Behavior

Open **Preferences** (Ctrl+,) to adjust:
- **Delete Protection:** How many files can be deleted before aborting
- **Conflict Resolution:** Keep both, newer wins, ask me, etc.
- **Sync Intervals:** 5 minutes for active folders, 1 hour for archives
- **Notifications:** Which alerts to show

### Add More Sync Jobs

1. Click **"Add Sync Job"** again
2. Repeat Step 3 for other folders
3. Example setup:
   - `~/Documents` ↔ `/Documents`
   - `~/Projects` ↔ `/Projects`
   - `~/Photos` ↔ `/Photos`

### Explore Cloud Files

1. Click **"Cloud Browser"** at the top
2. Browse your Proton Drive folders
3. Right-click on files to **download**, **view details**, or **delete**
4. Drag & drop local files onto folders to upload quickly

### View Sync History

Click **"Logs"** in the preferences to see:
- All sync operations
- What was uploaded/downloaded when
- Any warnings or errors
- Conflict resolutions

---

## Troubleshooting

### Files Not Syncing?

1. Check **Sync Panel** for pause/error status
2. View **Logs** to see what's happening
3. Verify both folders exist
4. Check disk space is available

See [Troubleshooting Guide](TROUBLESHOOTING.md) for detailed help.

### Sync is Slow?

- Reduce the number of files (large folders take longer on first sync)
- Check your internet speed (especially upload for large files)
- Increase sync interval if you're impatient for cloud→local changes

### Want Real-Time Cloud Changes?

By design, cloud-to-local syncs are **polled** (checked periodically, not pushed instantly). To download latest cloud changes:
- Set interval to **5 minutes** for active folders
- Or manually click **"Sync Now"** when you know cloud changes exist

See [Important Limitations](../README.md#-important-limitations) for more details.

---

## Common Setups

### Backup Documents Folder

```
Local:  ~/Documents
Cloud:  /Backups/Documents
Sync:   Two-Way, 30 minutes
Safety: 50% delete protection
```

### Shared Project Folder

```
Local:  ~/Projects/MyTeamProject
Cloud:  /Team/MyTeamProject
Sync:   Two-Way, 5 minutes (for collaboration)
Safety: Ask me on conflict
```

### Archive/Read-Only Cloud

```
Local:  ~/ArchiveDownloads
Cloud:  /Archives
Sync:   Cloud → Local only (prevent accidental uploads)
Safety: Delete protection disabled (archives are read-only)
```

---

## Need Help?

- **[Full README](../README.md)** - Complete feature documentation
- **[Setup Guide](../SYNC_SETUP.md)** - Detailed rclone configuration
- **[Safety Features](SYNC_SAFETY_FEATURES.md)** - Understanding protection mechanisms
- **[Troubleshooting](TROUBLESHOOTING.md)** - Common issues and solutions


# Folder Deletion Handling Design

## Problem Statement

When a user deletes a local synced folder (either accidentally or intentionally), the application must handle this gracefully without:
1. Automatically deleting the folder from the cloud
2. Leaving the sync job in a broken state
3. Confusing the user about what happened

## Current Behavior

Currently, if a synced folder is deleted locally:
- The file watcher may detect the deletion
- Sync operations will fail (path not found)
- No user notification is shown
- The sync job remains active but broken

## Proposed Solution

### Detection Mechanism

1. **File Watcher Detection**
   - Monitor for `IN_DELETE_SELF` inotify event on synced folders
   - Track folder deletion vs. file deletion separately

2. **Periodic Health Checks**
   - During sync operations, verify local path exists
   - If path missing, trigger folder deletion handler

### User Notification

When folder deletion is detected, show a dialog with:

**Title:** "Synced Folder Missing: [folder name]"

**Message:** 
```
The local folder "[path]" is no longer accessible.

This folder was syncing to "proton:[remote_path]"
Your cloud files are safe and unchanged.

What would you like to do?
```

**Options:**
1. **Restore from Cloud** - Re-download all cloud files to original location
2. **Choose New Location** - Select a different local folder for this sync job
3. **Stop Sync** - Deactivate this sync job (cloud files remain)
4. **Delete Sync Job** - Remove sync job completely (cloud files remain)

### Implementation Details

#### Detection Code (FileWatcher)

```cpp
// In file_watcher.cpp
void FileWatcher::handle_folder_deletion(const std::string& job_id, 
                                         const std::string& folder_path) {
    Logger::info("[FileWatcher] Detected deletion of synced folder: " + folder_path);
    
    // Get sync job details
    auto& registry = SyncJobRegistry::getInstance();
    auto job = registry.getJobById(job_id);
    if (!job.has_value()) return;
    
    // Stop watching this folder
    stop_watching(job_id);
    
    // Trigger main thread notification
    struct FolderDeletedData {
        AppWindow* app_window;
        std::string job_id;
        std::string folder_path;
        std::string remote_path;
    };
    
    auto* data = new FolderDeletedData{
        &AppWindow::getInstance(),
        job_id,
        folder_path,
        job->remote_path
    };
    
    g_idle_add(+[](gpointer user_data) -> gboolean {
        auto* d = static_cast<FolderDeletedData*>(user_data);
        d->app_window->show_folder_deletion_dialog(
            d->job_id, 
            d->folder_path, 
            d->remote_path
        );
        delete d;
        return G_SOURCE_REMOVE;
    }, data);
}
```

#### Dialog UI (AppWindow)

```cpp
// In app_window.cpp
void AppWindow::show_folder_deletion_dialog(const std::string& job_id,
                                             const std::string& local_path,
                                             const std::string& remote_path) {
    GtkWidget* dialog = gtk_message_dialog_new(
        GTK_WINDOW(window_),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING,
        GTK_BUTTONS_NONE,
        "Synced Folder Missing"
    );
    
    std::string folder_name = fs::path(local_path).filename().string();
    std::string message = 
        "The local folder \"" + local_path + "\" is no longer accessible.\n\n" +
        "This folder was syncing to \"proton:" + remote_path + "\"\n" +
        "Your cloud files are safe and unchanged.\n\n" +
        "What would you like to do?";
    
    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(dialog),
        "%s",
        message.c_str()
    );
    
    gtk_dialog_add_button(GTK_DIALOG(dialog), "Restore from Cloud", 1);
    gtk_dialog_add_button(GTK_DIALOG(dialog), "Choose New Location", 2);
    gtk_dialog_add_button(GTK_DIALOG(dialog), "Stop Sync", 3);
    gtk_dialog_add_button(GTK_DIALOG(dialog), "Delete Sync Job", 4);
    
    // Handle response
    g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog* dlg, 
                                                        int response,
                                                        gpointer data) {
        auto* info = static_cast<FolderDeletionInfo*>(data);
        
        switch (response) {
            case 1: // Restore from Cloud
                info->self->restore_folder_from_cloud(info->job_id, info->local_path);
                break;
            case 2: // Choose New Location
                info->self->relocate_sync_folder(info->job_id);
                break;
            case 3: // Stop Sync
                info->self->stop_sync_job(info->job_id);
                break;
            case 4: // Delete Sync Job
                info->self->delete_sync_job(info->job_id);
                break;
        }
        
        gtk_window_destroy(GTK_WINDOW(dlg));
        delete info;
    }), new FolderDeletionInfo{this, job_id, local_path, remote_path});
    
    gtk_widget_show(dialog);
}
```

#### Action Implementations

```cpp
void AppWindow::restore_folder_from_cloud(const std::string& job_id,
                                           const std::string& local_path) {
    // Recreate local folder
    fs::create_directories(local_path);
    
    // Trigger full cloud-to-local sync
    auto& registry = SyncJobRegistry::getInstance();
    auto job = registry.getJobById(job_id);
    if (!job.has_value()) return;
    
    // Use rclone to restore
    std::string cmd = "sync \"proton:" + job->remote_path + "\" \"" + 
                      local_path + "\" --create-empty-src-dirs";
    
    append_log("[Restore] Restoring folder from cloud: " + local_path);
    
    std::thread([this, cmd, local_path]() {
        std::string output = AppWindowHelpers::exec_rclone(cmd);
        
        g_idle_add(+[](gpointer data) -> gboolean {
            auto* self = static_cast<AppWindow*>(data);
            self->append_log("[Restore] Folder restored successfully");
            self->notifications_->show_info("Folder Restored", 
                                           "Files restored from cloud");
            return G_SOURCE_REMOVE;
        }, this);
    }).detach();
}

void AppWindow::relocate_sync_folder(const std::string& job_id) {
    // Show folder chooser
    GtkWidget* chooser = gtk_file_chooser_dialog_new(
        "Choose New Sync Location",
        GTK_WINDOW(window_),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Select", GTK_RESPONSE_ACCEPT,
        NULL
    );
    
    g_signal_connect(chooser, "response", G_CALLBACK(+[](GtkDialog* dlg,
                                                         int response,
                                                         gpointer data) {
        if (response == GTK_RESPONSE_ACCEPT) {
            auto* info = static_cast<JobIdHolder*>(data);
            
            GtkFileChooser* fc = GTK_FILE_CHOOSER(dlg);
            gchar* new_path = gtk_file_chooser_get_filename_compat(fc);
            
            if (new_path) {
                // Update sync job with new path
                auto& registry = SyncJobRegistry::getInstance();
                auto job = registry.getJobById(info->job_id);
                if (job.has_value()) {
                    job->local_path = new_path;
                    registry.updateJob(*job);
                    
                    info->self->append_log("[Relocate] Sync folder moved to: " + 
                                          std::string(new_path));
                }
                g_free(new_path);
            }
        }
        
        gtk_window_destroy(GTK_WINDOW(dlg));
        delete static_cast<JobIdHolder*>(data);
    }), new JobIdHolder{this, job_id});
    
    gtk_widget_show(chooser);
}

void AppWindow::stop_sync_job(const std::string& job_id) {
    // Stop file watcher
    FileWatcher::getInstance().stop_watching(job_id);
    
    // Mark job as inactive (but don't delete)
    auto& registry = SyncJobRegistry::getInstance();
    auto job = registry.getJobById(job_id);
    if (job.has_value()) {
        job->last_sync_status = "stopped";
        registry.updateJob(*job);
    }
    
    append_log("[Sync] Stopped sync job: " + job_id);
    notifications_->show_info("Sync Stopped", "Sync job deactivated");
    refresh_sync_jobs();
}

void AppWindow::delete_sync_job(const std::string& job_id) {
    // Confirm deletion
    GtkWidget* confirm = gtk_message_dialog_new(
        GTK_WINDOW(window_),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "Delete Sync Job?"
    );
    
    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(confirm),
        "This will remove the sync job configuration.\n"
        "Your cloud files will NOT be deleted."
    );
    
    g_signal_connect(confirm, "response", G_CALLBACK(+[](GtkDialog* dlg,
                                                         int response,
                                                         gpointer data) {
        if (response == GTK_RESPONSE_YES) {
            auto* info = static_cast<JobIdHolder*>(data);
            
            // Stop watching
            FileWatcher::getInstance().stop_watching(info->job_id);
            
            // Delete from registry
            auto& registry = SyncJobRegistry::getInstance();
            registry.deleteJob(info->job_id);
            
            info->self->append_log("[Sync] Deleted sync job: " + info->job_id);
            info->self->notifications_->show_info("Sync Job Deleted", 
                                                 "Configuration removed");
            info->self->refresh_sync_jobs();
        }
        
        gtk_window_destroy(GTK_WINDOW(dlg));
        delete static_cast<JobIdHolder*>(data);
    }), new JobIdHolder{this, job_id});
    
    gtk_widget_show(confirm);
}
```

### Safety Guarantees

1. **Never Auto-Delete from Cloud**
   - No automatic `rclone delete` or `rclone purge` on cloud files
   - Cloud files remain intact regardless of local state

2. **Explicit User Action Required**
   - All destructive operations require confirmation dialog
   - Clear messaging about what will be deleted/kept

3. **Reversible Operations**
   - "Restore from Cloud" can recover deleted local files
   - Sync job metadata preserved until explicitly deleted

### Testing Checklist

- [ ] Delete synced folder while app running
- [ ] Delete synced folder while app not running (detect on next start)
- [ ] Move synced folder to different location
- [ ] Rename synced folder
- [ ] Unmount external drive containing synced folder
- [ ] Test "Restore from Cloud" with large folder
- [ ] Test "Choose New Location" with permissions issues
- [ ] Verify cloud files never deleted automatically
- [ ] Test with multiple sync jobs (delete one, others unaffected)

## Future Enhancements

1. **Smart Detection of Moves**
   - Detect if folder was moved vs. deleted
   - Offer to update path automatically

2. **Backup Before Delete**
   - Option to create cloud backup before local deletion
   - Archive old sync jobs instead of hard delete

3. **Recovery Wizard**
   - Guide user through folder recovery process
   - Suggest likely relocation paths

4. **Network Drive Handling**
   - Special handling for network/external drives
   - Pause sync when drive disconnected
   - Resume when drive reconnected

## Related Files

- `src-native/src/file_watcher.cpp` - Folder deletion detection
- `src-native/src/app_window.cpp` - Dialog UI and user actions
- `src-native/src/sync_manager.cpp` - Sync job lifecycle
- `src-native/src/sync_job_metadata.cpp` - Job registry updates
- `scripts/test-sync-comprehensive.sh` - Test script for this feature

## References

- [Dropbox behavior on folder deletion](https://help.dropbox.com/delete-restore)
- [Google Drive sync behavior](https://support.google.com/drive/answer/2375083)
- [Nextcloud sync conflict handling](https://docs.nextcloud.com/server/latest/user_manual/en/files/sync.html)

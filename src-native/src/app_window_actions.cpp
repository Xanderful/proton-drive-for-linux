// app_window_actions.cpp - Button handlers and action dialogs for AppWindow
// Extracted from app_window.cpp to reduce file size

#include "app_window.hpp"
#include "app_window_helpers.hpp"
#include "sync_manager.hpp"
#include "sync_job_metadata.hpp"
#include "device_identity.hpp"
#include "notifications.hpp"
#include "settings.hpp"
#include "logger.hpp"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <thread>
#include <gtk/gtk.h>
#include <glib-object.h>
#include <glib.h>

namespace fs = std::filesystem;
using namespace AppWindowHelpers;

struct CleanupItem {
    std::string path;
    std::string cloud_path;
    int64_t size;
    bool is_dir;
    GtkWidget* checkbox;
};

// Forward declarations for cleanup dialog callbacks
static void on_cleanup_move_clicked(GtkButton*, gpointer data);
static void on_cleanup_cloud_only_clicked(GtkButton*, gpointer data);
static void on_cleanup_delete_everywhere_clicked(GtkButton*, gpointer data);
static void on_cleanup_close_clicked(GtkButton*, gpointer data);
static void on_cleanup_confirm_move(GtkButton*, gpointer data);
static void on_cleanup_confirm_cloud_only(GtkButton*, gpointer data);
static void on_cleanup_confirm_delete_everywhere(GtkButton*, gpointer data);

static void on_cleanup_move_clicked(GtkButton*, gpointer data) {
    auto* params = static_cast<std::tuple<AppWindow*, std::vector<CleanupItem>*>*>(data);
    auto [self, items_ptr] = *params;
    
    size_t selected_count = 0;
    for (const auto& item : *items_ptr) {
        if (gtk_check_button_get_active(GTK_CHECK_BUTTON(item.checkbox))) {
            selected_count++;
        }
    }
    
    if (selected_count == 0) {
        self->append_log("[LocalCleanup] No items selected");
        return;
    }
    
    // Create confirm dialog
    GtkWidget* dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Confirm Move to Cloud");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(self->get_window()));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, -1);
    
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);
    gtk_window_set_child(GTK_WINDOW(dialog), vbox);
    
    GtkWidget* header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* icon = gtk_image_new_from_icon_name("dialog-question-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 48);
    gtk_box_append(GTK_BOX(header_box), icon);
    
    GtkWidget* title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<b>Move to Cloud?</b>");
    gtk_box_append(GTK_BOX(header_box), title);
    gtk_box_append(GTK_BOX(vbox), header_box);
    
    std::string msg = "Move " + std::to_string(selected_count) + " selected items to cloud? This will free up local space.";
    GtkWidget* msg_label = gtk_label_new(msg.c_str());
    gtk_label_set_wrap(GTK_LABEL(msg_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(msg_label), 0);
    gtk_box_append(GTK_BOX(vbox), msg_label);
    
    GtkWidget* action_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(action_box, GTK_ALIGN_END);
    gtk_widget_set_margin_top(action_box, 12);
    
    GtkWidget* cancel_btn = gtk_button_new_with_label("Cancel");
    GtkWidget* confirm_btn = gtk_button_new_with_label("Move to Cloud");
    gtk_widget_add_css_class(confirm_btn, "suggested-action");
    
    gtk_box_append(GTK_BOX(action_box), cancel_btn);
    gtk_box_append(GTK_BOX(action_box), confirm_btn);
    gtk_box_append(GTK_BOX(vbox), action_box);
    
    g_signal_connect_data(cancel_btn, "clicked", G_CALLBACK(on_cleanup_close_clicked), dialog, NULL, (GConnectFlags)0);
    
    g_signal_connect_data(confirm_btn, "clicked", G_CALLBACK(on_cleanup_confirm_move), 
                          new std::tuple<GtkWidget*, AppWindow*, std::vector<CleanupItem>*>(dialog, self, items_ptr), NULL, (GConnectFlags)0);
    
    gtk_window_present(GTK_WINDOW(dialog));
}

static void on_cleanup_cloud_only_clicked(GtkButton*, gpointer data) {
    auto* params = static_cast<std::tuple<AppWindow*, std::vector<CleanupItem>*>*>(data);
    auto [self, items_ptr] = *params;
    
    size_t selected_count = 0;
    for (const auto& item : *items_ptr) {
        if (gtk_check_button_get_active(GTK_CHECK_BUTTON(item.checkbox))) {
            selected_count++;
        }
    }
    
    if (selected_count == 0) {
        self->append_log("[LocalCleanup] No items selected");
        return;
    }
    
    // Create confirm dialog
    GtkWidget* dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Keep Cloud-Only?");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(self->get_window()));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 450, -1);
    
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);
    gtk_window_set_child(GTK_WINDOW(dialog), vbox);
    
    GtkWidget* header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* icon = gtk_image_new_from_icon_name("cloud-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 48);
    gtk_box_append(GTK_BOX(header_box), icon);
    
    GtkWidget* title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<b>Keep Cloud-Only?</b>");
    gtk_box_append(GTK_BOX(header_box), title);
    gtk_box_append(GTK_BOX(vbox), header_box);
    
    std::string msg = "Delete " + std::to_string(selected_count) + " selected items locally and keep them cloud-only? They will not sync back to this device.";
    GtkWidget* msg_label = gtk_label_new(msg.c_str());
    gtk_label_set_wrap(GTK_LABEL(msg_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(msg_label), 0);
    gtk_box_append(GTK_BOX(vbox), msg_label);
    
    GtkWidget* action_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(action_box, GTK_ALIGN_END);
    gtk_widget_set_margin_top(action_box, 12);
    
    GtkWidget* cancel_btn = gtk_button_new_with_label("Cancel");
    GtkWidget* confirm_btn = gtk_button_new_with_label("Keep Cloud-Only");
    gtk_widget_add_css_class(confirm_btn, "suggested-action");
    
    gtk_box_append(GTK_BOX(action_box), cancel_btn);
    gtk_box_append(GTK_BOX(action_box), confirm_btn);
    gtk_box_append(GTK_BOX(vbox), action_box);
    
    g_signal_connect_data(cancel_btn, "clicked", G_CALLBACK(on_cleanup_close_clicked), dialog, NULL, (GConnectFlags)0);
    
    g_signal_connect_data(confirm_btn, "clicked", G_CALLBACK(on_cleanup_confirm_cloud_only), 
                          new std::tuple<GtkWidget*, AppWindow*, std::vector<CleanupItem>*>(dialog, self, items_ptr), NULL, (GConnectFlags)0);
    
    gtk_window_present(GTK_WINDOW(dialog));
}

static void on_cleanup_delete_everywhere_clicked(GtkButton*, gpointer data) {
    auto* params = static_cast<std::tuple<AppWindow*, std::vector<CleanupItem>*>*>(data);
    auto [self, items_ptr] = *params;
    
    size_t selected_count = 0;
    for (const auto& item : *items_ptr) {
        if (gtk_check_button_get_active(GTK_CHECK_BUTTON(item.checkbox))) {
            selected_count++;
        }
    }
    
    if (selected_count == 0) {
        self->append_log("[LocalCleanup] No items selected");
        return;
    }
    
    // Create confirm dialog
    GtkWidget* dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Delete Everywhere?");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(self->get_window()));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 450, -1);
    
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);
    gtk_window_set_child(GTK_WINDOW(dialog), vbox);
    
    GtkWidget* header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* icon = gtk_image_new_from_icon_name("dialog-warning-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 48);
    gtk_widget_add_css_class(icon, "warning");
    gtk_box_append(GTK_BOX(header_box), icon);
    
    GtkWidget* title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<b>Delete Everywhere?</b>");
    gtk_box_append(GTK_BOX(header_box), title);
    gtk_box_append(GTK_BOX(vbox), header_box);
    
    std::string msg = "⚠️ PERMANENTLY DELETE " + std::to_string(selected_count) + " selected items from BOTH local storage AND cloud? This cannot be undone!";
    GtkWidget* msg_label = gtk_label_new(msg.c_str());
    gtk_label_set_wrap(GTK_LABEL(msg_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(msg_label), 0);
    gtk_box_append(GTK_BOX(vbox), msg_label);
    
    GtkWidget* action_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(action_box, GTK_ALIGN_END);
    gtk_widget_set_margin_top(action_box, 12);
    
    GtkWidget* cancel_btn = gtk_button_new_with_label("Cancel");
    GtkWidget* confirm_btn = gtk_button_new_with_label("Delete Everywhere");
    gtk_widget_add_css_class(confirm_btn, "destructive-action");
    
    gtk_box_append(GTK_BOX(action_box), cancel_btn);
    gtk_box_append(GTK_BOX(action_box), confirm_btn);
    gtk_box_append(GTK_BOX(vbox), action_box);
    
    g_signal_connect_data(cancel_btn, "clicked", G_CALLBACK(on_cleanup_close_clicked), dialog, NULL, (GConnectFlags)0);
    
    g_signal_connect_data(confirm_btn, "clicked", G_CALLBACK(on_cleanup_confirm_delete_everywhere), 
                          new std::tuple<GtkWidget*, AppWindow*, std::vector<CleanupItem>*>(dialog, self, items_ptr), NULL, (GConnectFlags)0);
    
    gtk_window_present(GTK_WINDOW(dialog));
}

static void on_cleanup_close_clicked(GtkButton*, gpointer data) {
    gtk_window_destroy(GTK_WINDOW(data));
}

static void on_cleanup_confirm_move(GtkButton*, gpointer data) {
    auto* p = static_cast<std::tuple<GtkWidget*, AppWindow*, std::vector<CleanupItem>*>*>(data);
    auto [dlg, self, items_ptr] = *p;
    gtk_window_destroy(GTK_WINDOW(dlg));
    
    for (const auto& item : *items_ptr) {
        if (gtk_check_button_get_active(GTK_CHECK_BUTTON(item.checkbox))) {
            // Format: rclone move "local_path" "proton:cloud_path"
            std::string cloud_dest = "proton:" + item.cloud_path;
            self->append_log("[LocalCleanup] Moving to cloud: " + item.path + " → " + cloud_dest);
            
            // Use rclone move which deletes source after successful copy
            std::string cmd = "rclone move " + shell_escape(item.path) + " " + shell_escape(cloud_dest) + " --no-check-dest 2>&1 &";
            run_system(cmd);
        }
    }
}

static void on_cleanup_confirm_cloud_only(GtkButton*, gpointer data) {
    auto* p = static_cast<std::tuple<GtkWidget*, AppWindow*, std::vector<CleanupItem>*>*>(data);
    auto [dlg, self, items_ptr] = *p;
    gtk_window_destroy(GTK_WINDOW(dlg));
    
    for (const auto& item : *items_ptr) {
        if (gtk_check_button_get_active(GTK_CHECK_BUTTON(item.checkbox))) {
            try {
                // Delete local file
                fs::remove_all(item.path);
                self->append_log("[LocalCleanup] Deleted locally (cloud-only): " + item.path);
                
                // Add to .rcloneignore to prevent re-download
                std::string job_config_path = std::string(getenv("HOME")) + "/.config/proton-drive/";
                std::string ignore_file = job_config_path + ".rcloneignore";
                std::ofstream ignore(ignore_file, std::ios::app);
                if (ignore.is_open()) {
                    ignore << item.cloud_path << "\n";
                    ignore.close();
                    self->append_log("[LocalCleanup] Added to exclusions: " + item.cloud_path);
                }
            } catch (const std::exception& e) {
                self->append_log("[LocalCleanup] Failed: " + item.path + " - " + e.what());
            }
        }
    }
}

static void on_cleanup_confirm_delete_everywhere(GtkButton*, gpointer data) {
    auto* p = static_cast<std::tuple<GtkWidget*, AppWindow*, std::vector<CleanupItem>*>*>(data);
    auto [dlg, self, items_ptr] = *p;
    gtk_window_destroy(GTK_WINDOW(dlg));
    
    for (const auto& item : *items_ptr) {
        if (gtk_check_button_get_active(GTK_CHECK_BUTTON(item.checkbox))) {
            // Delete from cloud first
            std::string cloud_dest = "proton:" + item.cloud_path;
            self->append_log("[LocalCleanup] Deleting from cloud: " + cloud_dest);
            std::string cloud_cmd = "rclone delete " + shell_escape(cloud_dest) + " 2>&1";
            run_system(cloud_cmd);
            
            // Then delete locally
            try {
                fs::remove_all(item.path);
                self->append_log("[LocalCleanup] Deleted locally: " + item.path);
            } catch (const std::exception& e) {
                self->append_log("[LocalCleanup] Failed to delete locally: " + item.path + " - " + e.what());
            }
        }
    }
}

void AppWindow::on_start_clicked() {
    append_log("[Service] Starting all sync job timers...");
    std::thread([]() {
        // Start all job timers
        run_system("for timer in ~/.config/systemd/user/proton-drive-job-*.timer; do "
                   "systemctl --user start \"$(basename \"$timer\")\" 2>/dev/null; done");
    }).detach();
    
    g_timeout_add(1000, [](gpointer data) -> gboolean {
        static_cast<AppWindow*>(data)->poll_status();
        return G_SOURCE_REMOVE;
    }, this);
}

void AppWindow::on_stop_clicked() {
    append_log("[Service] Stopping all sync job timers...");
    std::thread([]() {
        // Stop all job timers
        run_system("systemctl --user stop 'proton-drive-job-*.timer' 2>/dev/null");
    }).detach();
    
    g_timeout_add(1000, [](gpointer data) -> gboolean {
        static_cast<AppWindow*>(data)->poll_status();
        return G_SOURCE_REMOVE;
    }, this);
}

void AppWindow::on_add_sync_clicked() {
    // Open file chooser for folder selection (GTK 4.6 compatible)
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Select Folder to Sync",
        GTK_WINDOW(window_),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Select", GTK_RESPONSE_ACCEPT,
        nullptr
    );
    G_GNUC_END_IGNORE_DEPRECATIONS
    
    g_object_set_data(G_OBJECT(dialog), "app_window", this);
    
    g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog* dlg, gint response, gpointer) {
        auto* self = static_cast<AppWindow*>(g_object_get_data(G_OBJECT(dlg), "app_window"));
        
        if (response == GTK_RESPONSE_ACCEPT && self) {
            G_GNUC_BEGIN_IGNORE_DEPRECATIONS
            GFile* file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dlg));
            G_GNUC_END_IGNORE_DEPRECATIONS
            if (file) {
                char* path = g_file_get_path(file);
                if (path) {
                    self->append_log("[Sync] Adding folder: " + std::string(path));
                    self->show_sync_from_local_dialog(path);
                    g_free(path);
                }
                g_object_unref(file);
            }
        }
        gtk_window_destroy(GTK_WINDOW(dlg));
    }), nullptr);
    
    gtk_widget_show(dialog);
}

static std::string normalize_remote_path(const std::string& input) {
    std::string path = input;
    if (path.rfind("proton:", 0) == 0) {
        path = path.substr(7);
    }
    if (path.empty()) {
        return "/";
    }
    if (path[0] != '/') {
        path = "/" + path;
    }
    while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
    }
    return path;
}

static void show_message_dialog(GtkWindow* parent,
                                const std::string& title,
                                const std::string& secondary = "") {
    GtkWidget* dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), title.c_str());
    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
        gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    }
    gtk_window_set_default_size(GTK_WINDOW(dialog), 420, -1);
    
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 16);
    gtk_window_set_child(GTK_WINDOW(dialog), box);
    
    GtkWidget* header = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(header), ("<b>" + title + "</b>").c_str());
    gtk_label_set_xalign(GTK_LABEL(header), 0);
    gtk_box_append(GTK_BOX(box), header);
    
    if (!secondary.empty()) {
        GtkWidget* body = gtk_label_new(secondary.c_str());
        gtk_label_set_wrap(GTK_LABEL(body), TRUE);
        gtk_label_set_xalign(GTK_LABEL(body), 0);
        gtk_box_append(GTK_BOX(box), body);
    }
    
    GtkWidget* ok_btn = gtk_button_new_with_label("OK");
    gtk_widget_set_halign(ok_btn, GTK_ALIGN_END);
    g_signal_connect_swapped(ok_btn, "clicked", G_CALLBACK(gtk_window_destroy), dialog);
    gtk_box_append(GTK_BOX(box), ok_btn);
    
    gtk_window_present(GTK_WINDOW(dialog));
}

// Conflict resolution dialog with actionable buttons
struct ConflictDialogData {
    AppWindow* self;
    std::string local_path;
    std::string remote_path;
    std::string sync_type;
    std::string new_remote_path;  // Alternative path with device name
    GtkWidget* dialog;
};

static void show_conflict_resolution_dialog(GtkWindow* parent,
                                             AppWindow* self,
                                             const std::string& title,
                                             const std::string& folder_name,
                                             const std::string& conflicting_device,
                                             const std::string& local_path,
                                             const std::string& remote_path,
                                             const std::string& sync_type,
                                             bool allow_merge,
                                             bool allow_create_new) {
    GtkWidget* dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), title.c_str());
    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
        gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    }
    gtk_window_set_default_size(GTK_WINDOW(dialog), 450, -1);
    
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);
    gtk_window_set_child(GTK_WINDOW(dialog), box);
    
    // Header
    GtkWidget* header = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(header), ("<b>" + title + "</b>").c_str());
    gtk_label_set_xalign(GTK_LABEL(header), 0);
    gtk_box_append(GTK_BOX(box), header);
    
    // Message
    std::string message = "A folder named '" + folder_name + "' already exists in the cloud";
    if (!conflicting_device.empty()) {
        message += " (from device: " + conflicting_device + ")";
    }
    message += ".";
    
    GtkWidget* body = gtk_label_new(message.c_str());
    gtk_label_set_wrap(GTK_LABEL(body), TRUE);
    gtk_label_set_xalign(GTK_LABEL(body), 0);
    gtk_box_append(GTK_BOX(box), body);
    
    GtkWidget* subtitle = gtk_label_new("Choose how to proceed:");
    gtk_widget_add_css_class(subtitle, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0);
    gtk_box_append(GTK_BOX(box), subtitle);
    
    // Create data structure for callbacks
    std::string this_device_name = DeviceIdentity::getInstance().getDeviceName();
    std::string new_remote = fs::path(remote_path).parent_path().string() + "/" + 
                             folder_name + "-" + this_device_name;
    
    auto* data = new ConflictDialogData{self, local_path, remote_path, sync_type, new_remote, dialog};
    g_object_set_data_full(G_OBJECT(dialog), "conflict-data", data,
        +[](gpointer p) { delete static_cast<ConflictDialogData*>(p); });
    
    // Button box
    GtkWidget* btn_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(box), btn_box);
    
    // Merge button
    if (allow_merge) {
        GtkWidget* merge_btn = gtk_button_new_with_label("Merge with existing folder");
        gtk_widget_add_css_class(merge_btn, "suggested-action");
        g_signal_connect(merge_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
            GtkWidget* dlg = GTK_WIDGET(user_data);
            auto* d = static_cast<ConflictDialogData*>(g_object_get_data(G_OBJECT(dlg), "conflict-data"));
            if (d) {
                Logger::info("[Conflict] User chose MERGE for " + d->remote_path);
                d->self->perform_sync_after_conflict_resolution(d->local_path, d->remote_path, d->sync_type, "merge");
            }
            gtk_window_destroy(GTK_WINDOW(dlg));
        }), dialog);
        gtk_box_append(GTK_BOX(btn_box), merge_btn);
        
        GtkWidget* merge_desc = gtk_label_new("Syncs your local folder with the existing cloud folder, combining files.");
        gtk_widget_add_css_class(merge_desc, "caption");
        gtk_widget_add_css_class(merge_desc, "dim-label");
        gtk_label_set_wrap(GTK_LABEL(merge_desc), TRUE);
        gtk_label_set_xalign(GTK_LABEL(merge_desc), 0);
        gtk_widget_set_margin_start(merge_desc, 4);
        gtk_box_append(GTK_BOX(btn_box), merge_desc);
    }
    
    // Create New button
    if (allow_create_new) {
        std::string new_label = "Create new folder: " + folder_name + "-" + this_device_name;
        GtkWidget* create_btn = gtk_button_new_with_label(new_label.c_str());
        g_signal_connect(create_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
            GtkWidget* dlg = GTK_WIDGET(user_data);
            auto* d = static_cast<ConflictDialogData*>(g_object_get_data(G_OBJECT(dlg), "conflict-data"));
            if (d) {
                Logger::info("[Conflict] User chose CREATE NEW: " + d->new_remote_path);
                d->self->perform_sync_after_conflict_resolution(d->local_path, d->new_remote_path, d->sync_type, "create_new");
            }
            gtk_window_destroy(GTK_WINDOW(dlg));
        }), dialog);
        gtk_box_append(GTK_BOX(btn_box), create_btn);
        
        GtkWidget* create_desc = gtk_label_new("Creates a separate folder in the cloud with your device name.");
        gtk_widget_add_css_class(create_desc, "caption");
        gtk_widget_add_css_class(create_desc, "dim-label");
        gtk_label_set_wrap(GTK_LABEL(create_desc), TRUE);
        gtk_label_set_xalign(GTK_LABEL(create_desc), 0);
        gtk_widget_set_margin_start(create_desc, 4);
        gtk_box_append(GTK_BOX(btn_box), create_desc);
    }
    
    // Cancel button
    GtkWidget* cancel_btn = gtk_button_new_with_label("Cancel");
    gtk_widget_set_margin_top(cancel_btn, 8);
    g_signal_connect_swapped(cancel_btn, "clicked", G_CALLBACK(gtk_window_destroy), dialog);
    gtk_box_append(GTK_BOX(btn_box), cancel_btn);
    
    gtk_window_present(GTK_WINDOW(dialog));
}

void AppWindow::show_sync_from_local_dialog(const std::string& local_path) {
    if (local_path.empty()) {
        return;
    }
    
    // Check for existing sync of this folder first
    auto& registry = SyncJobRegistry::getInstance();
    auto existing = registry.findJobByLocalPath(local_path);
    if (existing.has_value()) {
        show_message_dialog(GTK_WINDOW(window_),
                            "Folder Already Synced",
                            "This folder is already configured for sync.\nUse the Sync Jobs page to manage it.");
        return;
    }
    
    GtkWidget* dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Sync Folder to Cloud");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(window_));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 480, -1);
    
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);
    gtk_window_set_child(GTK_WINDOW(dialog), vbox);
    
    // Header with icon
    GtkWidget* header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* header_icon = gtk_image_new_from_icon_name("folder-sync-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(header_icon), 32);
    GtkWidget* header = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(header), "<b><big>Sync Folder to Cloud</big></b>");
    gtk_box_append(GTK_BOX(header_box), header_icon);
    gtk_box_append(GTK_BOX(header_box), header);
    gtk_box_append(GTK_BOX(vbox), header_box);
    
    // Local folder info
    GtkWidget* local_frame = gtk_frame_new("Local Folder");
    GtkWidget* local_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(local_box, 8);
    gtk_widget_set_margin_end(local_box, 8);
    gtk_widget_set_margin_top(local_box, 8);
    gtk_widget_set_margin_bottom(local_box, 8);
    GtkWidget* folder_icon = gtk_image_new_from_icon_name("folder-symbolic");
    GtkWidget* local_label = gtk_label_new(local_path.c_str());
    gtk_label_set_ellipsize(GTK_LABEL(local_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_xalign(GTK_LABEL(local_label), 0);
    gtk_widget_set_hexpand(local_label, TRUE);
    gtk_box_append(GTK_BOX(local_box), folder_icon);
    gtk_box_append(GTK_BOX(local_box), local_label);
    gtk_frame_set_child(GTK_FRAME(local_frame), local_box);
    gtk_box_append(GTK_BOX(vbox), local_frame);
    
    // Cloud destination
    GtkWidget* remote_label = gtk_label_new("Cloud destination folder:");
    gtk_label_set_xalign(GTK_LABEL(remote_label), 0);
    gtk_box_append(GTK_BOX(vbox), remote_label);
    
    GtkWidget* remote_entry = gtk_entry_new();
    std::string default_remote = "/" + fs::path(local_path).filename().string();
    gtk_editable_set_text(GTK_EDITABLE(remote_entry), default_remote.c_str());
    gtk_entry_set_placeholder_text(GTK_ENTRY(remote_entry), "/FolderName");
    gtk_box_append(GTK_BOX(vbox), remote_entry);
    
    // Sync type selection with radio buttons (no GtkDropDown - avoids crash)
    GtkWidget* sync_frame = gtk_frame_new("Sync Type");
    GtkWidget* radio_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(radio_box, 12);
    gtk_widget_set_margin_end(radio_box, 12);
    gtk_widget_set_margin_top(radio_box, 8);
    gtk_widget_set_margin_bottom(radio_box, 8);
    
    GtkWidget* radio_twoway = gtk_check_button_new_with_label("Two-Way Sync (local ↔ cloud)");
    GtkWidget* radio_upload = gtk_check_button_new_with_label("Upload Only (local → cloud)");
    gtk_check_button_set_group(GTK_CHECK_BUTTON(radio_upload), GTK_CHECK_BUTTON(radio_twoway));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(radio_twoway), TRUE);
    
    GtkWidget* twoway_hint = gtk_label_new("Changes sync both directions. Best for active folders.");
    gtk_widget_add_css_class(twoway_hint, "dim-label");
    gtk_widget_add_css_class(twoway_hint, "caption");
    gtk_widget_set_margin_start(twoway_hint, 24);
    
    GtkWidget* upload_hint = gtk_label_new("Only uploads local changes. Cloud changes won't download.");
    gtk_widget_add_css_class(upload_hint, "dim-label");
    gtk_widget_add_css_class(upload_hint, "caption");
    gtk_widget_set_margin_start(upload_hint, 24);
    
    gtk_box_append(GTK_BOX(radio_box), radio_twoway);
    gtk_box_append(GTK_BOX(radio_box), twoway_hint);
    gtk_box_append(GTK_BOX(radio_box), radio_upload);
    gtk_box_append(GTK_BOX(radio_box), upload_hint);
    gtk_frame_set_child(GTK_FRAME(sync_frame), radio_box);
    gtk_box_append(GTK_BOX(vbox), sync_frame);
    
    // Action buttons
    GtkWidget* action_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(action_box, GTK_ALIGN_END);
    gtk_widget_set_margin_top(action_box, 12);
    
    GtkWidget* cancel_btn = gtk_button_new_with_label("Cancel");
    GtkWidget* sync_btn = gtk_button_new_with_label("Start Sync");
    gtk_widget_add_css_class(sync_btn, "suggested-action");
    
    gtk_box_append(GTK_BOX(action_box), cancel_btn);
    gtk_box_append(GTK_BOX(action_box), sync_btn);
    gtk_box_append(GTK_BOX(vbox), action_box);
    
    // Dialog data struct (no GtkDropDown pointer needed)
    struct DialogData {
        AppWindow* self;
        std::string local_path;
        GtkWidget* remote_entry;
        GtkWidget* radio_twoway;  // Use radio buttons instead of dropdown
        GtkWidget* dialog;
    };
    
    auto* data = new DialogData{this, local_path, remote_entry, radio_twoway, dialog};
    g_object_set_data_full(G_OBJECT(dialog), "sync-data", data, +[](gpointer p) {
        delete static_cast<DialogData*>(p);
    });
    
    g_signal_connect_swapped(cancel_btn, "clicked", G_CALLBACK(gtk_window_destroy), dialog);
    g_signal_connect(sync_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        auto* data = static_cast<DialogData*>(g_object_get_data(G_OBJECT(user_data), "sync-data"));
        if (!data) {
            return;
        }
        const char* remote_raw = gtk_editable_get_text(GTK_EDITABLE(data->remote_entry));
        std::string remote_path = normalize_remote_path(remote_raw ? remote_raw : "");
        // Use radio button state instead of dropdown
        bool is_twoway = gtk_check_button_get_active(GTK_CHECK_BUTTON(data->radio_twoway));
        std::string sync_type = is_twoway ? "bisync" : "sync";
        
        if (remote_path == "/") {
            show_message_dialog(GTK_WINDOW(data->dialog),
                                "Invalid Path",
                                "Please enter a valid cloud folder path (e.g., /MyFolder).");
            return;
        }
        data->self->start_local_to_cloud_sync(data->local_path, remote_path, sync_type);
        gtk_window_destroy(GTK_WINDOW(data->dialog));
    }), dialog);
    
    gtk_window_present(GTK_WINDOW(dialog));
}

void AppWindow::start_local_to_cloud_sync(const std::string& local_path,
                                          const std::string& remote_path,
                                          const std::string& sync_type) {
    if (local_path.empty() || !fs::exists(local_path) || !fs::is_directory(local_path)) {
        show_message_dialog(GTK_WINDOW(window_),
                            "Local folder not found or not a directory.");
        return;
    }
    
    std::string normalized_remote = normalize_remote_path(remote_path);
    auto& registry = SyncJobRegistry::getInstance();
    auto conflict = registry.checkForCloudFolderConflicts(local_path, normalized_remote);
    
    if (conflict.type != SyncJobRegistry::ConflictType::NONE) {
        // Show actionable conflict resolution dialog
        std::string folder_name = fs::path(normalized_remote).filename().string();
        bool allow_merge = true;  // Always allow merge
        bool allow_create_new = (conflict.type == SyncJobRegistry::ConflictType::CLOUD_FOLDER_DIFFERENT_DEVICE);
        
        show_conflict_resolution_dialog(
            GTK_WINDOW(window_),
            this,
            "Sync conflict detected",
            folder_name,
            conflict.conflicting_device_name,
            local_path,
            normalized_remote,
            sync_type,
            allow_merge,
            allow_create_new
        );
        return;
    }
    
    // No conflict - proceed directly
    perform_sync_after_conflict_resolution(local_path, normalized_remote, sync_type, "new");
}

void AppWindow::perform_sync_after_conflict_resolution(const std::string& local_path,
                                                        const std::string& remote_path,
                                                        const std::string& sync_type,
                                                        const std::string& resolution) {
    // Show immediate feedback to user
    std::string folder_display = fs::path(remote_path).filename().string();
    show_toast("\U0001f504 Setting up sync: " + folder_display + "...");
    
    // Auto-switch to Sync Jobs page immediately so user sees progress
    if (main_stack_) {
        gtk_stack_set_visible_child_name(GTK_STACK(main_stack_), "sync-jobs");
    }
    
    // Run the rest asynchronously to avoid UI freeze
    struct AsyncData {
        AppWindow* self;
        std::string local_path;
        std::string remote_path;
        std::string sync_type;
        std::string resolution;
        std::string folder_display;
    };
    
    auto* data = new AsyncData{this, local_path, remote_path, sync_type, resolution, folder_display};
    
    std::thread([data]() {
        auto& registry = SyncJobRegistry::getInstance();
        
        std::string job_id = registry.createJob(data->local_path, data->remote_path, data->sync_type);
        
        SyncJobRegistry::CloudFolderMeta meta;
        meta.device_id = DeviceIdentity::getInstance().getDeviceId();
        meta.device_name = DeviceIdentity::getInstance().getDeviceName();
        meta.folder_name = fs::path(data->remote_path).filename().string();
        
        // Write metadata (this can be slow due to rclone operations)
        SyncJobRegistry::writeCloudFolderMetadata("proton:" + data->remote_path, meta);
        
        // Create .conf file using manage-sync-job.sh
        std::string script_path = SyncManager::get_script_path("manage-sync-job.sh");
        std::string cmd = "bash " + shell_escape(script_path) + " create-with-id " + shell_escape(job_id) +
                          " " + shell_escape(data->local_path) +
                          " " + shell_escape("proton:" + data->remote_path) + " " + shell_escape(data->sync_type) + " '15m'";
        [[maybe_unused]] int result = std::system(cmd.c_str());
        
        // Update UI on main thread
        struct UIUpdateData {
            AppWindow* self;
            std::string job_id;
            std::string local_path;
            std::string remote_path;
            std::string sync_type;
            std::string resolution;
            std::string folder_display;
        };
        
        auto* ui_data = new UIUpdateData{
            data->self, job_id, data->local_path, data->remote_path, 
            data->sync_type, data->resolution, data->folder_display
        };
        
        g_idle_add(+[](gpointer user_data) -> gboolean {
            auto* d = static_cast<UIUpdateData*>(user_data);
            
            d->self->append_log("✅ Created sync job: " + d->job_id + " (resolution: " + d->resolution + ")");
            d->self->append_log("   Local: " + d->local_path);
            d->self->append_log("   Remote: " + d->remote_path);
            d->self->append_log("   Type: " + d->sync_type);
            
            // Desktop notification
            proton::NotificationManager::getInstance().notify(
                "Sync Started",
                "Syncing " + d->folder_display + " (" + (d->sync_type == "bisync" ? "Two-Way" : "Upload") + ")",
                proton::NotificationType::INFO);
            
            // Toast
            d->self->show_toast("\U0001f504 Sync job created: " + d->folder_display);
            
            SyncManager::getInstance().load_jobs();
            d->self->refresh_sync_jobs();
            d->self->refresh_cloud_files();
            
            delete d;
            return G_SOURCE_REMOVE;
        }, ui_data);
        
        delete data;
    }).detach();
}

void AppWindow::on_add_profile_clicked() {
    append_log("[Profile] Opening profile setup dialog...");
    
    // Check if proton profile already exists
    if (has_rclone_profile()) {
        append_log("[Profile] Profile 'proton' already exists");
        // Show notification instead of creating duplicate
        GtkWidget* info_dialog = gtk_window_new();
        gtk_window_set_title(GTK_WINDOW(info_dialog), "Profile Exists");
        gtk_window_set_transient_for(GTK_WINDOW(info_dialog), GTK_WINDOW(window_));
        gtk_window_set_modal(GTK_WINDOW(info_dialog), TRUE);
        gtk_window_set_default_size(GTK_WINDOW(info_dialog), 350, 150);
        
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_widget_set_margin_start(box, 20);
        gtk_widget_set_margin_end(box, 20);
        gtk_widget_set_margin_top(box, 20);
        gtk_widget_set_margin_bottom(box, 20);
        gtk_window_set_child(GTK_WINDOW(info_dialog), box);
        
        GtkWidget* label = gtk_label_new("A Proton profile already exists.\nTo add a new one, first delete the existing profile.");
        gtk_label_set_wrap(GTK_LABEL(label), TRUE);
        gtk_box_append(GTK_BOX(box), label);
        
        GtkWidget* ok_btn = gtk_button_new_with_label("OK");
        gtk_widget_add_css_class(ok_btn, "suggested-action");
        g_signal_connect_swapped(ok_btn, "clicked", G_CALLBACK(gtk_window_destroy), info_dialog);
        gtk_box_append(GTK_BOX(box), ok_btn);
        
        gtk_window_present(GTK_WINDOW(info_dialog));
        return;
    }
    
    // Create in-app profile setup dialog
    GtkWidget* dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Add Proton Drive Profile");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(window_));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 400);
    
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);
    gtk_window_set_child(GTK_WINDOW(dialog), box);
    
    // Title
    GtkWidget* title = gtk_label_new("Configure Proton Drive Access");
    gtk_widget_add_css_class(title, "title-2");
    gtk_box_append(GTK_BOX(box), title);
    
    // Instructions
    GtkWidget* instructions = gtk_label_new(
        "Enter your Proton credentials to sync files. This profile will be named 'proton'."
    );
    gtk_label_set_wrap(GTK_LABEL(instructions), TRUE);
    gtk_label_set_xalign(GTK_LABEL(instructions), 0);
    gtk_box_append(GTK_BOX(box), instructions);
    
    // Username field
    GtkWidget* username_label = gtk_label_new("Username (email):");
    gtk_label_set_xalign(GTK_LABEL(username_label), 0);
    gtk_box_append(GTK_BOX(box), username_label);
    
    GtkWidget* username_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(username_entry), "your.email@protonmail.com");
    gtk_box_append(GTK_BOX(box), username_entry);
    
    // Password field
    GtkWidget* password_label = gtk_label_new("Password:");
    gtk_label_set_xalign(GTK_LABEL(password_label), 0);
    gtk_box_append(GTK_BOX(box), password_label);
    
    GtkWidget* password_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(password_entry), FALSE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(password_entry), "Enter password");
    gtk_box_append(GTK_BOX(box), password_entry);
    
    // 2FA code field (optional)
    GtkWidget* twofa_label = gtk_label_new("2FA Code (if enabled):");
    gtk_label_set_xalign(GTK_LABEL(twofa_label), 0);
    gtk_box_append(GTK_BOX(box), twofa_label);
    
    GtkWidget* twofa_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(twofa_entry), "Optional - leave blank if not using 2FA");
    gtk_box_append(GTK_BOX(box), twofa_entry);
    
    // Spacer
    GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(box), spacer);
    
    // Status label (hidden by default)
    GtkWidget* status_label = gtk_label_new("");
    gtk_label_set_wrap(GTK_LABEL(status_label), TRUE);
    gtk_widget_add_css_class(status_label, "error");
    gtk_box_append(GTK_BOX(box), status_label);
    gtk_widget_set_visible(status_label, FALSE);
    
    // Button box
    GtkWidget* button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(button_box, GTK_ALIGN_END);
    
    GtkWidget* cancel_btn = gtk_button_new_with_label("Cancel");
    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        gtk_window_destroy(GTK_WINDOW(data));
    }), dialog);
    gtk_box_append(GTK_BOX(button_box), cancel_btn);
    
    GtkWidget* save_btn = gtk_button_new_with_label("Configure");
    gtk_widget_add_css_class(save_btn, "suggested-action");
    
    // Store widgets in button data for callback
    struct DialogData {
        AppWindow* window;
        GtkWidget* dialog;
        GtkWidget* username_entry;
        GtkWidget* password_entry;
        GtkWidget* twofa_entry;
        GtkWidget* status_label;
    };
    
    DialogData* data = new DialogData{
        this, dialog, username_entry, password_entry, twofa_entry, status_label
    };
    
    g_signal_connect(save_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        DialogData* data = static_cast<DialogData*>(user_data);
        
        const char* username = gtk_editable_get_text(GTK_EDITABLE(data->username_entry));
        const char* password = gtk_editable_get_text(GTK_EDITABLE(data->password_entry));
        const char* twofa = gtk_editable_get_text(GTK_EDITABLE(data->twofa_entry));
        
        if (!username || strlen(username) == 0) {
            gtk_label_set_text(GTK_LABEL(data->status_label), "Please enter your username");
            gtk_widget_set_visible(data->status_label, TRUE);
            return;
        }
        
        if (!password || strlen(password) == 0) {
            gtk_label_set_text(GTK_LABEL(data->status_label), "Please enter your password");
            gtk_widget_set_visible(data->status_label, TRUE);
            return;
        }
        
        data->window->append_log("[Profile] Configuring Proton Drive profile...");
        
        std::string output;
        std::string error_message;
        bool ok = run_rclone_config_create(
            username,
            password,
            (twofa && strlen(twofa) > 0) ? std::string(twofa) : std::string(),
            &output,
            &error_message
        );
        
        if (!ok) {
            std::string msg = !error_message.empty() ? error_message : output;
            if (msg.empty()) msg = "Unknown error";
            gtk_label_set_text(GTK_LABEL(data->status_label),
                ("Configuration failed: " + msg.substr(0, 200)).c_str());
            gtk_widget_set_visible(data->status_label, TRUE);
            data->window->append_log("[Profile] Configuration failed: " + msg);
            return;
        }
        
        data->window->append_log("[Profile] Profile configured successfully");
        data->window->refresh_profiles();
        data->window->refresh_cloud_files();  // Also refresh cloud browser
        gtk_window_destroy(GTK_WINDOW(data->dialog));
        delete data;
    }), data);
    
    g_signal_connect(dialog, "close-request", G_CALLBACK(+[](GtkWindow*, gpointer user_data) -> gboolean {
        delete static_cast<DialogData*>(user_data);
        return FALSE;
    }), data);
    
    gtk_box_append(GTK_BOX(button_box), save_btn);
    gtk_box_append(GTK_BOX(box), button_box);
    
    gtk_window_present(GTK_WINDOW(dialog));
}

void AppWindow::on_settings_clicked() {
    append_log("[Settings] Opening settings...");
    
    // Create settings dialog
    GtkWidget* dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Preferences");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(window_));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 550, 500);
    
    GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(dialog), main_box);
    
    // Scrolled window for settings
    GtkWidget* scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_box_append(GTK_BOX(main_box), scrolled);
    
    GtkWidget* content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_margin_start(content_box, 20);
    gtk_widget_set_margin_end(content_box, 20);
    gtk_widget_set_margin_top(content_box, 20);
    gtk_widget_set_margin_bottom(content_box, 20);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), content_box);
    
    auto& settings = proton::SettingsManager::getInstance();
    
    // === TRASH SETTINGS SECTION ===
    GtkWidget* trash_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget* trash_title = gtk_label_new("Trash & File Recovery");
    gtk_widget_add_css_class(trash_title, "title-4");
    gtk_label_set_xalign(GTK_LABEL(trash_title), 0);
    gtk_box_append(GTK_BOX(trash_section), trash_title);
    
    // Trash retention days
    GtkWidget* retention_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* retention_label = gtk_label_new("Delete files from trash after:");
    gtk_label_set_xalign(GTK_LABEL(retention_label), 0);
    gtk_widget_set_hexpand(retention_label, TRUE);
    gtk_box_append(GTK_BOX(retention_box), retention_label);
    
    GtkAdjustment* retention_adj = gtk_adjustment_new(
        settings.get_int("trash_retention_days", 30), // value
        1,      // minimum
        365,    // maximum
        1,      // step
        7,      // page step
        0       // page size
    );
    GtkWidget* retention_spin = gtk_spin_button_new(retention_adj, 1, 0);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(retention_spin), TRUE);
    gtk_box_append(GTK_BOX(retention_box), retention_spin);
    
    GtkWidget* days_label = gtk_label_new("days");
    gtk_box_append(GTK_BOX(retention_box), days_label);
    
    gtk_box_append(GTK_BOX(trash_section), retention_box);
    
    // Trash info label
    GtkWidget* trash_info = gtk_label_new("Files removed locally are moved to trash and auto-deleted after this period.");
    gtk_label_set_wrap(GTK_LABEL(trash_info), TRUE);
    gtk_label_set_xalign(GTK_LABEL(trash_info), 0);
    gtk_widget_add_css_class(trash_info, "dim-label");
    gtk_box_append(GTK_BOX(trash_section), trash_info);
    
    gtk_box_append(GTK_BOX(content_box), trash_section);
    
    // Separator
    gtk_box_append(GTK_BOX(content_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    
    // === NOTIFICATIONS SECTION ===
    GtkWidget* notif_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget* notif_title = gtk_label_new("Notifications");
    gtk_widget_add_css_class(notif_title, "title-4");
    gtk_label_set_xalign(GTK_LABEL(notif_title), 0);
    gtk_box_append(GTK_BOX(notif_section), notif_title);
    
    GtkWidget* show_notif_check = gtk_check_button_new_with_label("Show desktop notifications");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(show_notif_check),
                                settings.get_show_notifications());
    gtk_box_append(GTK_BOX(notif_section), show_notif_check);
    
    gtk_box_append(GTK_BOX(content_box), notif_section);
    
    // Button box at bottom
    GtkWidget* button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(button_box, GTK_ALIGN_END);
    gtk_widget_set_margin_start(button_box, 20);
    gtk_widget_set_margin_end(button_box, 20);
    gtk_widget_set_margin_bottom(button_box, 20);
    
    GtkWidget* close_btn = gtk_button_new_with_label("Close");
    
    // Store widget pointers for save callback
    struct SettingsData {
        GtkWidget* retention_spin;
        GtkWidget* show_notif_check;
        GtkWidget* dialog;
        AppWindow* window;
    };
    
    SettingsData* data = new SettingsData{
        retention_spin, show_notif_check, dialog, this
    };
    
    g_signal_connect(close_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        SettingsData* data = static_cast<SettingsData*>(user_data);
        auto& settings = proton::SettingsManager::getInstance();
        
        // Save trash retention
        int retention_days = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(data->retention_spin));
        settings.set_int("trash_retention_days", retention_days);
        
        // Save notifications
        bool show_notif = gtk_check_button_get_active(GTK_CHECK_BUTTON(data->show_notif_check));
        settings.set_show_notifications(show_notif);
        
        settings.save();
        data->window->append_log("[Settings] Settings saved");
        
        gtk_window_destroy(GTK_WINDOW(data->dialog));
        delete data;
    }), data);
    
    g_signal_connect(dialog, "close-request", G_CALLBACK(+[](GtkWindow*, gpointer user_data) -> gboolean {
        delete static_cast<SettingsData*>(user_data);
        return FALSE;
    }), data);
    
    gtk_widget_add_css_class(close_btn, "suggested-action");
    gtk_box_append(GTK_BOX(button_box), close_btn);
    gtk_box_append(GTK_BOX(main_box), button_box);
    
    gtk_window_present(GTK_WINDOW(dialog));
}

void AppWindow::on_browse_clicked() {
    append_log("[Browse] Opening cloud browser...");
    // Navigate to cloud browser view in the stack
    if (main_stack_) {
        gtk_stack_set_visible_child_name(GTK_STACK(main_stack_), "browser");
    }
}

void AppWindow::build_hamburger_menu() {
    hamburger_menu_ = gtk_popover_new();
    gtk_widget_set_size_request(hamburger_menu_, 320, -1);
    
    GtkWidget* menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_popover_set_child(GTK_POPOVER(hamburger_menu_), menu_box);
    
    // Service Status Section
    gtk_box_append(GTK_BOX(menu_box), build_service_section());
    
    // Profiles Section
    gtk_box_append(GTK_BOX(menu_box), build_profiles_section());
    
    // Synced Folders Section
    gtk_box_append(GTK_BOX(menu_box), build_jobs_section());
    
    // Settings Section
    gtk_box_append(GTK_BOX(menu_box), build_settings_section());
}

GtkWidget* AppWindow::build_service_section() {
    GtkWidget* section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(section, "menu-section");
    gtk_widget_set_margin_start(section, 10);
    gtk_widget_set_margin_end(section, 10);
    gtk_widget_set_margin_top(section, 10);
    gtk_widget_set_margin_bottom(section, 10);
    
    // Section title
    GtkWidget* title = gtk_label_new("SERVICE STATUS");
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_widget_add_css_class(title, "menu-section-title");
    gtk_box_append(GTK_BOX(section), title);
    
    // Status label
    service_status_label_ = gtk_label_new("Checking...");
    gtk_label_set_xalign(GTK_LABEL(service_status_label_), 0);
    gtk_box_append(GTK_BOX(section), service_status_label_);
    
    // Control buttons
    GtkWidget* btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_set_homogeneous(GTK_BOX(btn_box), TRUE);
    
    start_btn_ = gtk_button_new_with_label("Start");
    g_signal_connect(start_btn_, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<AppWindow*>(data)->on_start_clicked();
    }), this);
    
    stop_btn_ = gtk_button_new_with_label("Stop");
    g_signal_connect(stop_btn_, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<AppWindow*>(data)->on_stop_clicked();
    }), this);
    
    gtk_box_append(GTK_BOX(btn_box), start_btn_);
    gtk_box_append(GTK_BOX(btn_box), stop_btn_);
    gtk_box_append(GTK_BOX(section), btn_box);
    
    return section;
}

GtkWidget* AppWindow::build_profiles_section() {
    GtkWidget* section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(section, "menu-section");
    gtk_widget_set_margin_start(section, 10);
    gtk_widget_set_margin_end(section, 10);
    gtk_widget_set_margin_bottom(section, 10);
    
    // Section title with add button
    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    
    GtkWidget* title = gtk_label_new("PROFILES");
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_widget_add_css_class(title, "menu-section-title");
    gtk_widget_set_hexpand(title, TRUE);
    
    GtkWidget* add_btn = gtk_button_new_from_icon_name("list-add-symbolic");
    gtk_widget_set_tooltip_text(add_btn, "Add new profile");
    gtk_widget_add_css_class(add_btn, "flat-button");
    g_signal_connect(add_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<AppWindow*>(data)->on_add_profile_clicked();
    }), this);
    
    gtk_box_append(GTK_BOX(header), title);
    gtk_box_append(GTK_BOX(header), add_btn);
    gtk_box_append(GTK_BOX(section), header);
    
    // Profiles list
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 80);
    
    profiles_list_ = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(profiles_list_), GTK_SELECTION_NONE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), profiles_list_);
    gtk_box_append(GTK_BOX(section), scroll);
    
    return section;
}

GtkWidget* AppWindow::build_jobs_section() {
    GtkWidget* section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(section, "menu-section");
    gtk_widget_set_margin_start(section, 10);
    gtk_widget_set_margin_end(section, 10);
    gtk_widget_set_margin_bottom(section, 10);
    
    // Section title with add button
    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    
    GtkWidget* title = gtk_label_new("SYNCED FOLDERS");
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_widget_add_css_class(title, "menu-section-title");
    gtk_widget_set_hexpand(title, TRUE);
    
    GtkWidget* add_btn = gtk_button_new_from_icon_name("list-add-symbolic");
    gtk_widget_set_tooltip_text(add_btn, "Add sync folder");
    gtk_widget_add_css_class(add_btn, "flat-button");
    g_signal_connect(add_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<AppWindow*>(data)->on_add_sync_clicked();
    }), this);
    
    gtk_box_append(GTK_BOX(header), title);
    gtk_box_append(GTK_BOX(header), add_btn);
    gtk_box_append(GTK_BOX(section), header);
    
    // Jobs list
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 120);
    
    jobs_list_ = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(jobs_list_), GTK_SELECTION_NONE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), jobs_list_);
    gtk_box_append(GTK_BOX(section), scroll);
    
    return section;
}

GtkWidget* AppWindow::build_settings_section() {
    GtkWidget* section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_start(section, 10);
    gtk_widget_set_margin_end(section, 10);
    gtk_widget_set_margin_bottom(section, 10);
    
    // Settings button
    GtkWidget* settings_btn = gtk_button_new();
    GtkWidget* settings_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* settings_icon = gtk_image_new_from_icon_name("preferences-system-symbolic");
    GtkWidget* settings_label = gtk_label_new("Settings");
    gtk_box_append(GTK_BOX(settings_box), settings_icon);
    gtk_box_append(GTK_BOX(settings_box), settings_label);
    gtk_button_set_child(GTK_BUTTON(settings_btn), settings_box);
    gtk_widget_add_css_class(settings_btn, "flat-button");
    g_signal_connect(settings_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<AppWindow*>(data)->on_settings_clicked();
    }), this);
    gtk_box_append(GTK_BOX(section), settings_btn);
    
    // Open folder button
    GtkWidget* folder_btn = gtk_button_new();
    GtkWidget* folder_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* folder_icon = gtk_image_new_from_icon_name("folder-open-symbolic");
    GtkWidget* folder_label = gtk_label_new("Open Proton Drive Folder");
    gtk_box_append(GTK_BOX(folder_box), folder_icon);
    gtk_box_append(GTK_BOX(folder_box), folder_label);
    gtk_button_set_child(GTK_BUTTON(folder_btn), folder_box);
    gtk_widget_add_css_class(folder_btn, "flat-button");
    g_signal_connect(folder_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer) {
        std::string folder = std::string(getenv("HOME")) + "/ProtonDrive";
        if (fs::exists(folder)) {
            std::string cmd = "xdg-open " + shell_escape(folder);
            run_system(cmd);
        }
    }), nullptr);
    gtk_box_append(GTK_BOX(section), folder_btn);
    
    return section;
}

void AppWindow::show_local_cleanup_dialog() {
    Logger::info("[LocalCleanup] Opening local cleanup dialog");
    
    // Create dialog
    GtkWidget* dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Local Cleanup");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 500);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(get_window()));
    
    // Main box
    GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(main_box, 20);
    gtk_widget_set_margin_end(main_box, 20);
    gtk_widget_set_margin_top(main_box, 20);
    gtk_widget_set_margin_bottom(main_box, 20);
    gtk_window_set_child(GTK_WINDOW(dialog), main_box);
    
    // Header
    GtkWidget* header = gtk_label_new("Analyze synced folders and free up local space");
    gtk_label_set_xalign(GTK_LABEL(header), 0);
    gtk_widget_add_css_class(header, "heading");
    gtk_box_append(GTK_BOX(main_box), header);
    
    // Progress bar for scanning
    GtkWidget* progress_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(progress_bar), TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress_bar), "Scanning...");
    gtk_box_append(GTK_BOX(main_box), progress_bar);
    
    // Scrolled window for file list
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(main_box), scroll);
    
    // List box for files
    GtkWidget* list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_box), GTK_SELECTION_NONE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list_box);
    
    // Button box
    GtkWidget* button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_top(button_box, 10);
    gtk_box_append(GTK_BOX(main_box), button_box);
    
    // Move to Cloud button
    GtkWidget* move_btn = gtk_button_new_with_label("Move to Cloud");
    gtk_widget_add_css_class(move_btn, "suggested-action");
    gtk_widget_set_sensitive(move_btn, FALSE);
    gtk_box_append(GTK_BOX(button_box), move_btn);
    
    // Keep Cloud-Only button
    GtkWidget* cloud_only_btn = gtk_button_new_with_label("Keep Cloud-Only");
    gtk_widget_set_sensitive(cloud_only_btn, FALSE);
    gtk_box_append(GTK_BOX(button_box), cloud_only_btn);
    
    // Delete Everywhere button
    GtkWidget* delete_everywhere_btn = gtk_button_new_with_label("Delete Everywhere");
    gtk_widget_add_css_class(delete_everywhere_btn, "destructive-action");
    gtk_widget_set_sensitive(delete_everywhere_btn, FALSE);
    gtk_box_append(GTK_BOX(button_box), delete_everywhere_btn);
    
    // Close button
    GtkWidget* close_btn = gtk_button_new_with_label("Close");
    gtk_box_append(GTK_BOX(button_box), close_btn);
    
    // Data structures - allocate on heap to persist beyond function scope
    auto* items = new std::vector<CleanupItem>();
    
    // Cancellation flag for background thread (shared_ptr survives dialog destruction)
    auto cancel_flag = std::make_shared<std::atomic<bool>>(false);
    
    // Clean up items when dialog is destroyed and signal thread to stop
    struct CleanupData {
        std::vector<CleanupItem>* items;
        std::shared_ptr<std::atomic<bool>> cancel_flag;
    };
    auto* cleanup_data = new CleanupData{items, cancel_flag};
    g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWindow*, gpointer data) {
        auto* cd = static_cast<CleanupData*>(data);
        cd->cancel_flag->store(true);  // Signal thread to stop
        delete cd->items;
        delete cd;
    }), cleanup_data);
    
    // Get synced jobs
    auto jobs = SyncJobRegistry::getInstance().getAllJobs();
    if (jobs.empty()) {
        GtkWidget* no_sync_label = gtk_label_new("No synced folders found. Add sync folders first.");
        gtk_box_append(GTK_BOX(main_box), no_sync_label);
        gtk_widget_set_sensitive(progress_bar, FALSE);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 0);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress_bar), "");
    } else {
        // Async scan
        std::thread scan_thread([this, cancel_flag, dialog, progress_bar, list_box, move_btn, cloud_only_btn, delete_everywhere_btn, items]() {
            try {
                Logger::info("[LocalCleanup] Starting scan...");
                // Scan all synced folders
                auto jobs = SyncJobRegistry::getInstance().getAllJobs();
                size_t total_jobs = jobs.size();
                size_t current_job = 0;
                
                Logger::info("[LocalCleanup] Found " + std::to_string(total_jobs) + " sync jobs");
                
                for (const auto& job : jobs) {
                    // Check cancellation
                    if (cancel_flag->load()) {
                        Logger::info("[LocalCleanup] Scan cancelled");
                        return;
                    }
                    
                    current_job++;
                    std::string local_path = job.local_path;
                    std::string remote_path = job.remote_path;
                    
                    Logger::info("[LocalCleanup] Scanning job " + std::to_string(current_job) + ": " + local_path);
                    
                    // Update progress on main thread
                    double fraction = static_cast<double>(current_job) / total_jobs;
                    std::string progress_text = "Scanning " + std::to_string(current_job) + "/" + std::to_string(total_jobs);
                    if (!cancel_flag->load()) {
                        g_idle_add(+[](gpointer data) -> gboolean {
                            auto* params = static_cast<std::tuple<GtkWidget*, double, std::string>*>(data);
                            auto [bar, frac, text] = *params;
                            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bar), frac);
                            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(bar), text.c_str());
                            delete params;
                            return FALSE;
                        }, new std::tuple<GtkWidget*, double, std::string>(progress_bar, fraction, progress_text));
                    }
                    
                    if (!fs::exists(local_path)) {
                        Logger::warn("[LocalCleanup] Path does not exist: " + local_path);
                        continue;
                    }
                    
                    // Scan only regular files (skip directories for now to simplify)
                    int file_count = 0;
                    try {
                        for (auto& entry : fs::recursive_directory_iterator(local_path, fs::directory_options::skip_permission_denied)) {
                            try {
                                if (entry.is_regular_file()) {
                                    int64_t size = entry.file_size();
                                    if (size > 0) {
                                        CleanupItem item;
                                        item.path = entry.path().string();
                                        item.size = size;
                                        item.is_dir = false;
                                        
                                        // Compute cloud path
                                        fs::path relative = fs::relative(entry.path(), local_path);
                                        item.cloud_path = remote_path + "/" + relative.string();
                                        
                                        items->push_back(item);
                                        file_count++;
                                    }
                                }
                            } catch (const std::exception& e) {
                                Logger::debug("[LocalCleanup] Skipping file " + entry.path().string() + ": " + e.what());
                            }
                        }
                    } catch (const std::exception& e) {
                        Logger::error("[LocalCleanup] Failed to scan " + local_path + ": " + e.what());
                    }
                    
                    Logger::info("[LocalCleanup] Found " + std::to_string(file_count) + " files in " + local_path);
                }
                
                Logger::info("[LocalCleanup] Total files found: " + std::to_string(items->size()));
                
                // Sort by size descending
                std::sort(items->begin(), items->end(), [](const CleanupItem& a, const CleanupItem& b) {
                    return a.size > b.size;
                });
                
                // Limit to top 100 items
                if (items->size() > 100) {
                    items->resize(100);
                }
                
                Logger::info("[LocalCleanup] Displaying top " + std::to_string(items->size()) + " items");
                
                // Update UI on main thread (only if not cancelled)
                if (!cancel_flag->load()) {
                    g_idle_add(+[](gpointer data) -> gboolean {
                        auto* params = static_cast<std::tuple<GtkWidget*, GtkWidget*, GtkWidget*, GtkWidget*, GtkWidget*, std::vector<CleanupItem>*>*>(data);
                        auto [list_box, progress_bar, move_btn, cloud_only_btn, delete_everywhere_btn, items_ptr] = *params;
                        
                        Logger::info("[LocalCleanup] UI update callback - populating " + std::to_string(items_ptr->size()) + " items");
                        
                        // Hide progress bar
                        gtk_widget_set_visible(progress_bar, FALSE);
                    
                    // Populate list
                    for (auto& item : *items_ptr) {
                        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
                        gtk_widget_set_margin_start(row, 10);
                        gtk_widget_set_margin_end(row, 10);
                        gtk_widget_set_margin_top(row, 5);
                        gtk_widget_set_margin_bottom(row, 5);
                        
                        // Checkbox
                        item.checkbox = gtk_check_button_new();
                        gtk_box_append(GTK_BOX(row), item.checkbox);
                        
                        // Icon
                        GtkWidget* icon = gtk_image_new_from_icon_name(
                            item.is_dir ? "folder-symbolic" : "text-x-generic-symbolic");
                        gtk_box_append(GTK_BOX(row), icon);
                        
                        // Path label
                        std::string display_path = item.path;
                        if (display_path.length() > 50) {
                            display_path = "..." + display_path.substr(display_path.length() - 47);
                        }
                        GtkWidget* path_label = gtk_label_new(display_path.c_str());
                        gtk_label_set_xalign(GTK_LABEL(path_label), 0);
                        gtk_widget_set_hexpand(path_label, TRUE);
                        gtk_box_append(GTK_BOX(row), path_label);
                        
                        // Size label
                        GtkWidget* size_label = gtk_label_new(format_file_size(item.size).c_str());
                        gtk_label_set_xalign(GTK_LABEL(size_label), 1);
                        gtk_box_append(GTK_BOX(row), size_label);
                        
                        gtk_list_box_append(GTK_LIST_BOX(list_box), row);
                    }
                    
                    // Enable buttons
                    gtk_widget_set_sensitive(move_btn, TRUE);
                    gtk_widget_set_sensitive(cloud_only_btn, TRUE);
                    gtk_widget_set_sensitive(delete_everywhere_btn, TRUE);
                    
                    delete params;
                    return FALSE;
                }, new std::tuple<GtkWidget*, GtkWidget*, GtkWidget*, GtkWidget*, GtkWidget*, std::vector<CleanupItem>*>(
                    list_box, progress_bar, move_btn, cloud_only_btn, delete_everywhere_btn, items));
                }
                    
            } catch (const std::exception& e) {
                Logger::error("[LocalCleanup] Scan failed: " + std::string(e.what()));
                if (!cancel_flag->load()) {
                    g_idle_add(+[](gpointer data) -> gboolean {
                    auto* params = static_cast<std::tuple<GtkWidget*, GtkWidget*>*>(data);
                    auto [progress_bar, dialog] = *params;
                    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress_bar), "Scan failed");
                    gtk_widget_set_sensitive(progress_bar, FALSE);
                    delete params;
                    return FALSE;
                }, new std::tuple<GtkWidget*, GtkWidget*>(progress_bar, dialog));
                }
            }
        });
        scan_thread.detach();
    }
    
    // Connect buttons
    g_signal_connect_data(move_btn, "clicked", G_CALLBACK(on_cleanup_move_clicked), 
                          new std::tuple<AppWindow*, std::vector<CleanupItem>*>(this, items), NULL, (GConnectFlags)0);
    
    g_signal_connect_data(cloud_only_btn, "clicked", G_CALLBACK(on_cleanup_cloud_only_clicked), 
                          new std::tuple<AppWindow*, std::vector<CleanupItem>*>(this, items), NULL, (GConnectFlags)0);
    
    g_signal_connect_data(delete_everywhere_btn, "clicked", G_CALLBACK(on_cleanup_delete_everywhere_clicked), 
                          new std::tuple<AppWindow*, std::vector<CleanupItem>*>(this, items), NULL, (GConnectFlags)0);
    
    g_signal_connect_data(close_btn, "clicked", G_CALLBACK(on_cleanup_close_clicked), dialog, NULL, (GConnectFlags)0);
    
    gtk_window_present(GTK_WINDOW(dialog));
}


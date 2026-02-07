// GTK4 compatibility helpers
// These provide shims for GTK3-style functions to help legacy sync code compile
// The application now uses GTK4 exclusively

#pragma once

#include <gtk/gtk.h>

// GTK4 removed GtkContainer entirely - provide a dummy type for legacy code
typedef GtkWidget GtkContainer;

// GTK4 removed gtk_widget_get_toplevel - use gtk_widget_get_root instead
inline GtkWidget* compat_gtk_widget_get_toplevel(GtkWidget* widget) {
    GtkRoot* root = gtk_widget_get_root(widget);
    return root ? GTK_WIDGET(root) : nullptr;
}
#define gtk_widget_get_toplevel(w) compat_gtk_widget_get_toplevel(w)

// GTK4 removed gtk_widget_destroy - use gtk_window_destroy for windows
inline void compat_gtk_widget_destroy(GtkWidget* widget) {
    if (GTK_IS_WINDOW(widget)) {
        gtk_window_destroy(GTK_WINDOW(widget));
    }
}
#define gtk_widget_destroy(w) compat_gtk_widget_destroy(w)

// GTK4 removed gtk_widget_show_all - just use gtk_widget_show
#define gtk_widget_show_all(w) gtk_widget_set_visible(w, TRUE)

// GTK4 removed gtk_container_add - use appropriate child setter
inline void compat_container_add(GtkWidget* parent, GtkWidget* child) {
    if (GTK_IS_BOX(parent)) {
        gtk_box_append(GTK_BOX(parent), child);
    } else if (GTK_IS_WINDOW(parent)) {
        gtk_window_set_child(GTK_WINDOW(parent), child);
    } else if (GTK_IS_SCROLLED_WINDOW(parent)) {
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(parent), child);
    } else if (GTK_IS_FRAME(parent)) {
        gtk_frame_set_child(GTK_FRAME(parent), child);
    } else if (GTK_IS_REVEALER(parent)) {
        gtk_revealer_set_child(GTK_REVEALER(parent), child);
    } else if (GTK_IS_POPOVER(parent)) {
        gtk_popover_set_child(GTK_POPOVER(parent), child);
    } else if (GTK_IS_LIST_BOX(parent)) {
        gtk_list_box_append(GTK_LIST_BOX(parent), child);
    }
}
#define gtk_container_add(c, w) compat_container_add(GTK_WIDGET(c), w)

inline void compat_container_set_border_width(GtkWidget* widget, guint width) {
    gtk_widget_set_margin_start(widget, width);
    gtk_widget_set_margin_end(widget, width);
    gtk_widget_set_margin_top(widget, width);
    gtk_widget_set_margin_bottom(widget, width);
}
#define gtk_container_set_border_width(c, w) compat_container_set_border_width(GTK_WIDGET(c), w)

// GTK4 removed gtk_box_pack_start/end - use gtk_box_append with expand settings
inline void gtk_box_pack_start(GtkBox* box, GtkWidget* child, 
                               gboolean expand, [[maybe_unused]] gboolean fill, guint padding) {
    gtk_widget_set_hexpand(child, expand);
    gtk_widget_set_vexpand(child, expand);
    if (padding > 0) {
        gtk_widget_set_margin_start(child, padding);
        gtk_widget_set_margin_end(child, padding);
    }
    gtk_box_append(box, child);
}

inline void gtk_box_pack_end(GtkBox* box, GtkWidget* child,
                             gboolean expand, [[maybe_unused]] gboolean fill, guint padding) {
    gtk_widget_set_hexpand(child, expand);
    gtk_widget_set_vexpand(child, expand);
    if (padding > 0) {
        gtk_widget_set_margin_start(child, padding);
        gtk_widget_set_margin_end(child, padding);
    }
    gtk_box_append(box, child);
}

// GTK4 scrolled window - simplified constructor
inline GtkWidget* gtk_scrolled_window_new_compat(void) {
    return gtk_scrolled_window_new();
}

// GTK4 file chooser - deprecated API wrappers
// Note: These use deprecated GTK4 APIs - consider using GtkFileDialog instead
inline gchar* gtk_file_chooser_get_filename_compat(GtkFileChooser* chooser) {
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GFile* file = gtk_file_chooser_get_file(chooser);
    G_GNUC_END_IGNORE_DEPRECATIONS
    if (file) {
        gchar* path = g_file_get_path(file);
        g_object_unref(file);
        return path;
    }
    return nullptr;
}

inline gboolean gtk_file_chooser_set_filename_compat(GtkFileChooser* chooser, const gchar* filename) {
    GFile* file = g_file_new_for_path(filename);
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gboolean result = gtk_file_chooser_set_file(chooser, file, nullptr);
    G_GNUC_END_IGNORE_DEPRECATIONS
    g_object_unref(file);
    return result;
}

inline gboolean gtk_file_chooser_set_current_folder_compat(GtkFileChooser* chooser, const gchar* folder) {
    GFile* file = g_file_new_for_path(folder);
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gboolean result = gtk_file_chooser_set_current_folder(chooser, file, nullptr);
    G_GNUC_END_IGNORE_DEPRECATIONS
    g_object_unref(file);
    return result;
}

// GTK4 window state checking
inline gboolean is_window_visible_and_active(GtkWidget* widget) {
    if (!widget || !GTK_IS_WIDGET(widget)) return FALSE;
    if (!gtk_widget_get_realized(widget)) return FALSE;
    if (!gtk_widget_get_mapped(widget)) return FALSE;
    
    GtkNative* native = gtk_widget_get_native(widget);
    if (!native) return TRUE;
    
    GdkSurface* surface = gtk_native_get_surface(native);
    if (!surface) return TRUE;
    
    return gtk_widget_get_mapped(widget);
}

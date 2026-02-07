/**
 * GTK4-compatible System Tray Icon using StatusNotifierItem D-Bus protocol
 * 
 * Since AppIndicator requires GTK3, we implement the StatusNotifierItem
 * protocol directly using GIO/D-Bus which works with any GTK version.
 */

#include "tray.hpp"
#include "logger.hpp"
#include "file_index.hpp"
#include "app_window_helpers.hpp"
#include <gio/gio.h>
#include <iostream>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <atomic>

namespace fs = std::filesystem;

// D-Bus interface for StatusNotifierItem
// Used for D-Bus interface name matching
[[maybe_unused]] static const char* SNI_INTERFACE = "org.kde.StatusNotifierItem";
static const char* SNI_PATH = "/StatusNotifierItem";

// D-Bus interface XML for StatusNotifierItem
static const char* SNI_INTROSPECTION_XML = R"XML(
<node>
  <interface name="org.kde.StatusNotifierItem">
    <property name="Category" type="s" access="read"/>
    <property name="Id" type="s" access="read"/>
    <property name="Title" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="IconName" type="s" access="read"/>
    <property name="IconThemePath" type="s" access="read"/>
    <property name="Menu" type="o" access="read"/>
    <signal name="NewIcon"/>
    <signal name="NewTitle"/>
    <signal name="NewStatus">
      <arg type="s" name="status"/>
    </signal>
    <method name="Activate">
      <arg type="i" name="x" direction="in"/>
      <arg type="i" name="y" direction="in"/>
    </method>
    <method name="SecondaryActivate">
      <arg type="i" name="x" direction="in"/>
      <arg type="i" name="y" direction="in"/>
    </method>
    <method name="ContextMenu">
      <arg type="i" name="x" direction="in"/>
      <arg type="i" name="y" direction="in"/>
    </method>
    <method name="Scroll">
      <arg type="i" name="delta" direction="in"/>
      <arg type="s" name="orientation" direction="in"/>
    </method>
  </interface>
</node>
)XML";

// D-Bus interface XML for DBusMenu
static const char* DBUSMENU_INTROSPECTION_XML = R"XML(
<node>
  <interface name="com.canonical.dbusmenu">
    <property name="Version" type="u" access="read"/>
    <property name="TextDirection" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="IconThemePath" type="as" access="read"/>
    <method name="GetLayout">
      <arg type="i" name="parentId" direction="in"/>
      <arg type="i" name="recursionDepth" direction="in"/>
      <arg type="as" name="propertyNames" direction="in"/>
      <arg type="u" name="revision" direction="out"/>
      <arg type="(ia{sv}av)" name="layout" direction="out"/>
    </method>
    <method name="GetGroupProperties">
      <arg type="ai" name="ids" direction="in"/>
      <arg type="as" name="propertyNames" direction="in"/>
      <arg type="a(ia{sv})" name="properties" direction="out"/>
    </method>
    <method name="GetProperty">
      <arg type="i" name="id" direction="in"/>
      <arg type="s" name="name" direction="in"/>
      <arg type="v" name="value" direction="out"/>
    </method>
    <method name="Event">
      <arg type="i" name="id" direction="in"/>
      <arg type="s" name="eventId" direction="in"/>
      <arg type="v" name="data" direction="in"/>
      <arg type="u" name="timestamp" direction="in"/>
    </method>
    <method name="EventGroup">
      <arg type="a(isvu)" name="events" direction="in"/>
      <arg type="ai" name="idErrors" direction="out"/>
    </method>
    <method name="AboutToShow">
      <arg type="i" name="id" direction="in"/>
      <arg type="b" name="needUpdate" direction="out"/>
    </method>
    <method name="AboutToShowGroup">
      <arg type="ai" name="ids" direction="in"/>
      <arg type="ai" name="updatesNeeded" direction="out"/>
      <arg type="ai" name="idErrors" direction="out"/>
    </method>
    <signal name="ItemsPropertiesUpdated">
      <arg type="a(ia{sv})" name="updatedProps"/>
      <arg type="a(ias)" name="removedProps"/>
    </signal>
    <signal name="LayoutUpdated">
      <arg type="u" name="revision"/>
      <arg type="i" name="parent"/>
    </signal>
    <signal name="ItemActivationRequested">
      <arg type="i" name="id"/>
      <arg type="u" name="timestamp"/>
    </signal>
  </interface>
</node>
)XML";

// Menu item IDs
enum MenuItemId {
    MENU_ROOT = 0,
    MENU_STATUS = 1,
    MENU_SEP1 = 2,
    MENU_SHOW_HIDE = 3,
    MENU_OPEN_FOLDER = 4,
    MENU_SEP2 = 5,
    MENU_PAUSE_SYNC = 6,
    MENU_RESUME_SYNC = 7,
    MENU_STOP_ALL_SYNCS = 8,
    MENU_SETTINGS = 9,
    MENU_SEP3 = 10,
    MENU_QUIT = 11
};

// Helper to run shell commands
static int run_command(const std::string& cmd) {
    return std::system(cmd.c_str());
}

class TrayIconImpl {
public:
    GtkApplication* app_ = nullptr;
    GDBusConnection* connection_ = nullptr;
    guint sni_registration_id_ = 0;
    guint menu_registration_id_ = 0;
    guint watcher_watch_id_ = 0;
    std::string bus_name_;
    std::string icon_theme_path_;
    std::atomic<bool> stop_thread_{false};
    std::atomic<bool> thread_exited_{false};  // Set by thread when it exits
    std::thread status_thread_;
    std::function<void()> toggle_callback_;
    bool sync_enabled_ = false;
    std::string status_text_ = "Sync Status: Checking...";
    guint32 menu_revision_ = 1;
    
    GDBusNodeInfo* sni_node_info_ = nullptr;
    GDBusNodeInfo* menu_node_info_ = nullptr;
    
    void init();
    void cleanup();
    bool is_sync_running();
    void monitor_status();
    void update_status(bool is_running);
    void emit_menu_updated();
    
    // D-Bus handlers
    static void handle_sni_method_call(GDBusConnection* connection,
                                        const gchar* sender,
                                        const gchar* object_path,
                                        const gchar* interface_name,
                                        const gchar* method_name,
                                        GVariant* parameters,
                                        GDBusMethodInvocation* invocation,
                                        gpointer user_data);
    
    static GVariant* handle_sni_get_property(GDBusConnection* connection,
                                              const gchar* sender,
                                              const gchar* object_path,
                                              const gchar* interface_name,
                                              const gchar* property_name,
                                              GError** error,
                                              gpointer user_data);
    
    static void handle_menu_method_call(GDBusConnection* connection,
                                         const gchar* sender,
                                         const gchar* object_path,
                                         const gchar* interface_name,
                                         const gchar* method_name,
                                         GVariant* parameters,
                                         GDBusMethodInvocation* invocation,
                                         gpointer user_data);
    
    static GVariant* handle_menu_get_property(GDBusConnection* connection,
                                               const gchar* sender,
                                               const gchar* object_path,
                                               const gchar* interface_name,
                                               const gchar* property_name,
                                               GError** error,
                                               gpointer user_data);
    
    GVariant* build_menu_layout(int parent_id, int depth);
    void handle_menu_event(int id, const std::string& event_id);
};

static TrayIconImpl* g_impl = nullptr;

// ===== D-Bus Method Handlers =====

void TrayIconImpl::handle_sni_method_call(GDBusConnection* connection,
                                           const gchar* sender,
                                           const gchar* object_path,
                                           const gchar* interface_name,
                                           const gchar* method_name,
                                           GVariant* parameters,
                                           GDBusMethodInvocation* invocation,
                                           gpointer user_data) {
    (void)connection; (void)sender; (void)object_path; (void)interface_name; (void)parameters;
    auto* impl = static_cast<TrayIconImpl*>(user_data);
    
    Logger::info("[Tray/SNI] Method called: " + std::string(method_name));
    
    if (g_strcmp0(method_name, "Activate") == 0) {
        Logger::info("[Tray/SNI] Activate - left click");
        // Left click - show/hide window
        if (impl->toggle_callback_) {
            impl->toggle_callback_();
        }
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "SecondaryActivate") == 0) {
        Logger::info("[Tray/SNI] SecondaryActivate - middle click");
        // Middle click
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "ContextMenu") == 0) {
        Logger::info("[Tray/SNI] ContextMenu - right click (menu handled by host)");
        // Right click - menu is handled by the host
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "Scroll") == 0) {
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else {
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
            G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown method: %s", method_name);
    }
}

GVariant* TrayIconImpl::handle_sni_get_property(GDBusConnection* connection,
                                                 const gchar* sender,
                                                 const gchar* object_path,
                                                 const gchar* interface_name,
                                                 const gchar* property_name,
                                                 GError** error,
                                                 gpointer user_data) {
    (void)connection; (void)sender; (void)object_path; (void)interface_name; (void)error;
    auto* impl = static_cast<TrayIconImpl*>(user_data);
    
    if (g_strcmp0(property_name, "Category") == 0) {
        return g_variant_new_string("ApplicationStatus");
    } else if (g_strcmp0(property_name, "Id") == 0) {
        return g_variant_new_string("proton-drive");
    } else if (g_strcmp0(property_name, "Title") == 0) {
        return g_variant_new_string("Proton Drive");
    } else if (g_strcmp0(property_name, "Status") == 0) {
        return g_variant_new_string("Active");
    } else if (g_strcmp0(property_name, "IconName") == 0) {
        return g_variant_new_string("proton-drive-tray");
    } else if (g_strcmp0(property_name, "IconThemePath") == 0) {
        return g_variant_new_string(impl->icon_theme_path_.c_str());
    } else if (g_strcmp0(property_name, "Menu") == 0) {
        return g_variant_new_object_path("/MenuBar");
    }
    
    return NULL;
}

void TrayIconImpl::handle_menu_method_call(GDBusConnection* connection,
                                            const gchar* sender,
                                            const gchar* object_path,
                                            const gchar* interface_name,
                                            const gchar* method_name,
                                            GVariant* parameters,
                                            GDBusMethodInvocation* invocation,
                                            gpointer user_data) {
    (void)connection; (void)sender; (void)object_path; (void)interface_name;
    auto* impl = static_cast<TrayIconImpl*>(user_data);
    
    Logger::info("[Tray/DBusMenu] Method called: " + std::string(method_name));
    
    if (g_strcmp0(method_name, "GetLayout") == 0) {
        gint32 parent_id;
        gint32 depth;
        g_variant_get(parameters, "(iias)", &parent_id, &depth, NULL);
        
        Logger::info("[Tray/DBusMenu] GetLayout request: parent_id=" + std::to_string(parent_id) + 
                     " depth=" + std::to_string(depth));
        
        GVariant* layout = impl->build_menu_layout(parent_id, depth);
        GVariant* result = g_variant_new("(u@(ia{sv}av))", impl->menu_revision_, layout);
        Logger::info("[Tray/DBusMenu] GetLayout response built, revision=" + std::to_string(impl->menu_revision_));
        g_dbus_method_invocation_return_value(invocation, result);
    } else if (g_strcmp0(method_name, "Event") == 0) {
        gint32 id;
        const gchar* event_id;
        GVariant* data;
        guint32 timestamp;
        g_variant_get(parameters, "(is@vu)", &id, &event_id, &data, &timestamp);
        
        impl->handle_menu_event(id, event_id);
        g_variant_unref(data);
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "AboutToShow") == 0) {
        gint32 id;
        g_variant_get(parameters, "(i)", &id);
        Logger::debug("[Tray/DBusMenu] AboutToShow for id=" + std::to_string(id));
        // Return FALSE - no dynamic update needed, but tell host to refresh if root
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", id == 0 ? TRUE : FALSE));
    } else if (g_strcmp0(method_name, "GetGroupProperties") == 0) {
        // Return properties for requested items
        GVariant* ids_v = nullptr;
        GVariant* props_v = nullptr;
        g_variant_get(parameters, "(@ai@as)", &ids_v, &props_v);
        
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("a(ia{sv})"));
        
        // If the host is asking for specific IDs, return their properties
        if (ids_v) {
            gsize n_ids = g_variant_n_children(ids_v);
            Logger::debug("[Tray/DBusMenu] GetGroupProperties for " + std::to_string(n_ids) + " ids");
            
            for (gsize i = 0; i < n_ids; i++) {
                gint32 id;
                g_variant_get_child(ids_v, i, "i", &id);
                
                GVariantBuilder item_props;
                g_variant_builder_init(&item_props, G_VARIANT_TYPE("a{sv}"));
                
                // Add common properties
                g_variant_builder_add(&item_props, "{sv}", "visible", g_variant_new_boolean(TRUE));
                
                switch (id) {
                    case MENU_ROOT:
                        g_variant_builder_add(&item_props, "{sv}", "children-display", g_variant_new_string("submenu"));
                        break;
                    case MENU_STATUS:
                        g_variant_builder_add(&item_props, "{sv}", "label", g_variant_new_string(impl->status_text_.c_str()));
                        g_variant_builder_add(&item_props, "{sv}", "enabled", g_variant_new_boolean(FALSE));
                        g_variant_builder_add(&item_props, "{sv}", "type", g_variant_new_string("standard"));
                        break;
                    case MENU_SEP1:
                    case MENU_SEP2:
                    case MENU_SEP3:
                        g_variant_builder_add(&item_props, "{sv}", "type", g_variant_new_string("separator"));
                        break;
                    case MENU_SHOW_HIDE:
                        g_variant_builder_add(&item_props, "{sv}", "label", g_variant_new_string("Show/Hide App"));
                        g_variant_builder_add(&item_props, "{sv}", "icon-name", g_variant_new_string("view-reveal-symbolic"));
                        g_variant_builder_add(&item_props, "{sv}", "enabled", g_variant_new_boolean(TRUE));
                        g_variant_builder_add(&item_props, "{sv}", "type", g_variant_new_string("standard"));
                        break;
                    case MENU_OPEN_FOLDER:
                        g_variant_builder_add(&item_props, "{sv}", "label", g_variant_new_string("Open Folder"));
                        g_variant_builder_add(&item_props, "{sv}", "icon-name", g_variant_new_string("folder-open-symbolic"));
                        g_variant_builder_add(&item_props, "{sv}", "enabled", g_variant_new_boolean(TRUE));
                        g_variant_builder_add(&item_props, "{sv}", "type", g_variant_new_string("standard"));
                        break;
                    case MENU_PAUSE_SYNC:
                        g_variant_builder_add(&item_props, "{sv}", "label", g_variant_new_string("Pause Syncing"));
                        g_variant_builder_add(&item_props, "{sv}", "icon-name", g_variant_new_string("media-playback-pause-symbolic"));
                        g_variant_builder_add(&item_props, "{sv}", "enabled", g_variant_new_boolean(impl->sync_enabled_));
                        g_variant_builder_add(&item_props, "{sv}", "type", g_variant_new_string("standard"));
                        break;
                    case MENU_RESUME_SYNC:
                        g_variant_builder_add(&item_props, "{sv}", "label", g_variant_new_string("Resume Syncing"));
                        g_variant_builder_add(&item_props, "{sv}", "icon-name", g_variant_new_string("media-playback-start-symbolic"));
                        g_variant_builder_add(&item_props, "{sv}", "enabled", g_variant_new_boolean(!impl->sync_enabled_));
                        g_variant_builder_add(&item_props, "{sv}", "type", g_variant_new_string("standard"));
                        break;
                    case MENU_STOP_ALL_SYNCS:
                        g_variant_builder_add(&item_props, "{sv}", "label", g_variant_new_string("Stop All Syncs"));
                        g_variant_builder_add(&item_props, "{sv}", "icon-name", g_variant_new_string("process-stop-symbolic"));
                        g_variant_builder_add(&item_props, "{sv}", "enabled", g_variant_new_boolean(TRUE));
                        g_variant_builder_add(&item_props, "{sv}", "type", g_variant_new_string("standard"));
                        break;
                    case MENU_SETTINGS:
                        g_variant_builder_add(&item_props, "{sv}", "label", g_variant_new_string("Sync Settings..."));
                        g_variant_builder_add(&item_props, "{sv}", "icon-name", g_variant_new_string("preferences-system-symbolic"));
                        g_variant_builder_add(&item_props, "{sv}", "enabled", g_variant_new_boolean(TRUE));
                        g_variant_builder_add(&item_props, "{sv}", "type", g_variant_new_string("standard"));
                        break;
                    case MENU_QUIT:
                        g_variant_builder_add(&item_props, "{sv}", "label", g_variant_new_string("Quit"));
                        g_variant_builder_add(&item_props, "{sv}", "icon-name", g_variant_new_string("application-exit-symbolic"));
                        g_variant_builder_add(&item_props, "{sv}", "enabled", g_variant_new_boolean(TRUE));
                        g_variant_builder_add(&item_props, "{sv}", "type", g_variant_new_string("standard"));
                        break;
                }
                
                g_variant_builder_add(&builder, "(i@a{sv})", id, g_variant_builder_end(&item_props));
            }
            g_variant_unref(ids_v);
        }
        if (props_v) g_variant_unref(props_v);
        
        g_dbus_method_invocation_return_value(invocation, 
            g_variant_new("(@a(ia{sv}))", g_variant_builder_end(&builder)));
    } else if (g_strcmp0(method_name, "GetProperty") == 0) {
        gint32 id;
        const gchar* prop_name;
        g_variant_get(parameters, "(is)", &id, &prop_name);
        Logger::debug("[Tray/DBusMenu] GetProperty id=" + std::to_string(id) + " prop=" + std::string(prop_name));
        g_dbus_method_invocation_return_value(invocation, 
            g_variant_new("(v)", g_variant_new_string("")));
    } else if (g_strcmp0(method_name, "AboutToShowGroup") == 0) {
        GVariant* ids_v = nullptr;
        g_variant_get(parameters, "(@ai)", &ids_v);
        
        gsize n_ids = ids_v ? g_variant_n_children(ids_v) : 0;
        Logger::debug("[Tray/DBusMenu] AboutToShowGroup for " + std::to_string(n_ids) + " ids");
        if (ids_v) g_variant_unref(ids_v);
        
        GVariantBuilder updates, errors;
        g_variant_builder_init(&updates, G_VARIANT_TYPE("ai"));
        g_variant_builder_init(&errors, G_VARIANT_TYPE("ai"));
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(@ai@ai)", g_variant_builder_end(&updates), g_variant_builder_end(&errors)));
    } else if (g_strcmp0(method_name, "EventGroup") == 0) {
        // EventGroup receives array of (id, eventId, data, timestamp) tuples
        GVariant* events = nullptr;
        g_variant_get(parameters, "(@a(isvu))", &events);
        
        GVariantBuilder errors;
        g_variant_builder_init(&errors, G_VARIANT_TYPE("ai"));
        
        if (events) {
            gsize n_events = g_variant_n_children(events);
            Logger::info("[Tray/DBusMenu] EventGroup with " + std::to_string(n_events) + " events");
            
            for (gsize i = 0; i < n_events; i++) {
                GVariant* event = g_variant_get_child_value(events, i);
                gint32 id;
                const gchar* event_id;
                GVariant* data;
                guint32 timestamp;
                g_variant_get(event, "(is@vu)", &id, &event_id, &data, &timestamp);
                
                Logger::info("[Tray/DBusMenu] EventGroup item: id=" + std::to_string(id) + 
                             " event_id='" + std::string(event_id) + "'");
                
                impl->handle_menu_event(id, event_id);
                
                g_variant_unref(data);
                g_variant_unref(event);
            }
            g_variant_unref(events);
        }
        
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(@ai)", g_variant_builder_end(&errors)));
    } else {
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
            G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown method: %s", method_name);
    }
}

GVariant* TrayIconImpl::handle_menu_get_property(GDBusConnection* connection,
                                                  const gchar* sender,
                                                  const gchar* object_path,
                                                  const gchar* interface_name,
                                                  const gchar* property_name,
                                                  GError** error,
                                                  gpointer user_data) {
    (void)connection; (void)sender; (void)object_path; (void)interface_name; (void)error;
    auto* impl = static_cast<TrayIconImpl*>(user_data);
    
    if (g_strcmp0(property_name, "Version") == 0) {
        return g_variant_new_uint32(3);
    } else if (g_strcmp0(property_name, "TextDirection") == 0) {
        return g_variant_new_string("ltr");
    } else if (g_strcmp0(property_name, "Status") == 0) {
        return g_variant_new_string("normal");
    } else if (g_strcmp0(property_name, "IconThemePath") == 0) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
        g_variant_builder_add(&builder, "s", impl->icon_theme_path_.c_str());
        return g_variant_builder_end(&builder);
    }
    
    return NULL;
}

// Build menu item properties
static GVariant* build_menu_item_props(const char* label, const char* icon = nullptr, 
                                        bool enabled = true, const char* type = nullptr,
                                        const char* toggle_type = nullptr, int toggle_state = -1) {
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    
    // Always add visible property
    g_variant_builder_add(&props, "{sv}", "visible", g_variant_new_boolean(TRUE));
    
    if (label) {
        g_variant_builder_add(&props, "{sv}", "label", g_variant_new_string(label));
    }
    if (icon) {
        g_variant_builder_add(&props, "{sv}", "icon-name", g_variant_new_string(icon));
    }
    g_variant_builder_add(&props, "{sv}", "enabled", g_variant_new_boolean(enabled ? TRUE : FALSE));
    
    if (type) {
        g_variant_builder_add(&props, "{sv}", "type", g_variant_new_string(type));
    } else {
        // Default type is "standard" for regular items
        g_variant_builder_add(&props, "{sv}", "type", g_variant_new_string("standard"));
    }
    if (toggle_type) {
        g_variant_builder_add(&props, "{sv}", "toggle-type", g_variant_new_string(toggle_type));
        g_variant_builder_add(&props, "{sv}", "toggle-state", g_variant_new_int32(toggle_state));
    }
    
    return g_variant_builder_end(&props);
}

GVariant* TrayIconImpl::build_menu_layout(int parent_id, int depth) {
    (void)depth;
    Logger::debug("[Tray/DBusMenu] Building menu layout for parent_id=" + std::to_string(parent_id));
    
    GVariantBuilder children;
    g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
    
    int item_count = 0;
    
    if (parent_id == MENU_ROOT) {
        // Helper to create menu item with empty children
        auto add_menu_item = [&children, &item_count](int id, GVariant* props) {
            GVariantBuilder item_children;
            g_variant_builder_init(&item_children, G_VARIANT_TYPE("av"));
            GVariant* item_children_v = g_variant_builder_end(&item_children);
            g_variant_builder_add(&children, "v", 
                g_variant_new("(i@a{sv}@av)", id, props, item_children_v));
            item_count++;
        };
        
        // Status item (disabled label)
        add_menu_item(MENU_STATUS, build_menu_item_props(status_text_.c_str(), nullptr, false));
        
        // Separator
        add_menu_item(MENU_SEP1, build_menu_item_props(nullptr, nullptr, true, "separator"));
        
        // Show/Hide App
        add_menu_item(MENU_SHOW_HIDE, build_menu_item_props("Show/Hide App", "view-reveal-symbolic"));
        
        // Open Folder
        add_menu_item(MENU_OPEN_FOLDER, build_menu_item_props("Open Folder", "folder-open-symbolic"));
        
        // Separator
        add_menu_item(MENU_SEP2, build_menu_item_props(nullptr, nullptr, true, "separator"));
        
        // Sync actions
        add_menu_item(MENU_PAUSE_SYNC, build_menu_item_props("Pause Syncing", "media-playback-pause-symbolic",
                                     sync_enabled_));
        add_menu_item(MENU_RESUME_SYNC, build_menu_item_props("Resume Syncing", "media-playback-start-symbolic",
                                      !sync_enabled_));
        add_menu_item(MENU_STOP_ALL_SYNCS, build_menu_item_props("Stop All Syncs", "process-stop-symbolic"));
        
        // Settings
        add_menu_item(MENU_SETTINGS, build_menu_item_props("Sync Settings...", "preferences-system-symbolic"));
        
        // Separator
        add_menu_item(MENU_SEP3, build_menu_item_props(nullptr, nullptr, true, "separator"));
        
        // Quit
        add_menu_item(MENU_QUIT, build_menu_item_props("Quit", "application-exit-symbolic"));
        
        Logger::debug("[Tray/DBusMenu] Added " + std::to_string(item_count) + " menu items");
    }
    
    // Root item
    GVariantBuilder root_props;
    g_variant_builder_init(&root_props, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&root_props, "{sv}", "children-display", g_variant_new_string("submenu"));
    GVariant* root_props_v = g_variant_builder_end(&root_props);
    GVariant* children_v = g_variant_builder_end(&children);
    
    return g_variant_new("(i@a{sv}@av)", parent_id, root_props_v, children_v);
}
void TrayIconImpl::handle_menu_event(int id, const std::string& event_id) {
    Logger::info("[Tray/DBusMenu] Event received: id=" + std::to_string(id) + 
                 " event_id='" + event_id + "'");
    
    // DBusMenu spec: "clicked" is normal activation, "" may also be sent
    // Accept both "clicked" and empty string for compatibility
    if (event_id != "clicked" && !event_id.empty()) {
        Logger::debug("[Tray/DBusMenu] Ignoring non-click event: " + event_id);
        return;
    }
    
    Logger::info("[Tray/DBusMenu] Processing menu action for id=" + std::to_string(id));
    
    switch (id) {
        case MENU_SHOW_HIDE:
            if (toggle_callback_) {
                toggle_callback_();
            }
            break;
            
        case MENU_OPEN_FOLDER: {
            std::string home = std::getenv("HOME");
            std::string path = home + "/ProtonDrive";
            std::string cmd = "xdg-open " + AppWindowHelpers::shell_escape(path) + " &";
            run_command(cmd);
            break;
        }
        
        case MENU_PAUSE_SYNC:
            Logger::info("[Tray] Pausing sync...");
            run_command("systemctl --user stop proton-drive-sync.service");
            sync_enabled_ = false;
            emit_menu_updated();
            break;
        case MENU_RESUME_SYNC:
            Logger::info("[Tray] Resuming sync...");
            run_command("systemctl --user start proton-drive-sync.service");
            sync_enabled_ = true;
            emit_menu_updated();
            break;
        case MENU_STOP_ALL_SYNCS:
            Logger::info("[Tray] Stopping all rclone sync processes...");
            run_command("systemctl --user stop proton-drive-sync.service");
            run_command("pkill -SIGTERM -f 'rclone.*bisync' 2>/dev/null");
            run_command("pkill -SIGTERM -f 'rclone.*sync' 2>/dev/null");
            sync_enabled_ = false;
            emit_menu_updated();
            break;
            
        case MENU_SETTINGS:
            if (toggle_callback_) {
                toggle_callback_();  // Show window for settings
            }
            break;
            
        case MENU_QUIT:
            Logger::info("[Quit] Stopping rclone sync processes...");
            run_command("pkill -SIGTERM -f 'rclone.*bisync' 2>/dev/null");
            run_command("pkill -SIGTERM -f 'rclone.*sync' 2>/dev/null");
            
            Logger::info("[Quit] Shutting down file index...");
            FileIndex::getInstance().shutdown();
            
            Logger::info("[Quit] Exiting application...");
            if (app_) {
                g_application_quit(G_APPLICATION(app_));
            }
            break;
    }
}

void TrayIconImpl::emit_menu_updated() {
    menu_revision_++;
    
    if (connection_) {
        GError* error = nullptr;
        g_dbus_connection_emit_signal(connection_, nullptr, "/MenuBar",
            "com.canonical.dbusmenu", "LayoutUpdated",
            g_variant_new("(ui)", menu_revision_, 0), &error);
        
        if (error) {
            Logger::error("[Tray] Failed to emit LayoutUpdated: " + std::string(error->message));
            g_error_free(error);
        }
    }
}

bool TrayIconImpl::is_sync_running() {
    int result = run_command("systemctl --user is-active --quiet proton-drive-sync.service 2>/dev/null");
    return (result == 0);
}

void TrayIconImpl::monitor_status() {
    try {
        status_thread_ = std::thread([this]() {
            while (!stop_thread_) {
                bool running = is_sync_running();
                
                g_main_context_invoke(nullptr, [](gpointer data) -> gboolean {
                    auto* pair = static_cast<std::pair<TrayIconImpl*, bool>*>(data);
                    pair->first->update_status(pair->second);
                    delete pair;
                    return FALSE;
                }, new std::pair<TrayIconImpl*, bool>(this, running));
                
                // Sleep in shorter intervals to respond to stop_thread_ faster
                for (int i = 0; i < 50 && !stop_thread_; i++) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
            thread_exited_ = true;
        });
    } catch (const std::system_error& e) {
        Logger::error("[TrayIcon] Failed to create status monitor thread: " + std::string(e.what()));
        thread_exited_ = true;  // Mark as exited if we couldn't start
    } catch (const std::exception& e) {
        Logger::error("[TrayIcon] Unexpected exception creating status thread: " + std::string(e.what()));
        thread_exited_ = true;
    }
}

void TrayIconImpl::update_status(bool is_running) {
    bool changed = (sync_enabled_ != is_running);
    sync_enabled_ = is_running;
    status_text_ = is_running ? "Sync Status: Active" : "Sync Status: Stopped";
    
    if (changed) {
        emit_menu_updated();
    }
}

static void on_bus_acquired(GDBusConnection* connection, const gchar* name, gpointer user_data) {
    (void)name;
    auto* impl = static_cast<TrayIconImpl*>(user_data);
    impl->connection_ = connection;
    
    // Register StatusNotifierItem interface
    static const GDBusInterfaceVTable sni_vtable = {
        TrayIconImpl::handle_sni_method_call,
        TrayIconImpl::handle_sni_get_property,
        nullptr,
        {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}  // padding
    };
    
    GError* error = nullptr;
    impl->sni_registration_id_ = g_dbus_connection_register_object(
        connection, SNI_PATH,
        impl->sni_node_info_->interfaces[0],
        &sni_vtable, impl, nullptr, &error);
    
    if (error) {
        Logger::error("[Tray] Failed to register SNI: " + std::string(error->message));
        g_error_free(error);
        return;
    }
    
    // Register DBusMenu interface
    static const GDBusInterfaceVTable menu_vtable = {
        TrayIconImpl::handle_menu_method_call,
        TrayIconImpl::handle_menu_get_property,
        nullptr,
        {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}  // padding
    };
    
    impl->menu_registration_id_ = g_dbus_connection_register_object(
        connection, "/MenuBar",
        impl->menu_node_info_->interfaces[0],
        &menu_vtable, impl, nullptr, &error);
    
    if (error) {
        Logger::error("[Tray] Failed to register Menu: " + std::string(error->message));
        g_error_free(error);
    }
    
    Logger::info("[Tray] D-Bus interfaces registered");
}

static void on_name_acquired(GDBusConnection* connection, const gchar* name, gpointer user_data) {
    (void)user_data;
    Logger::info("[Tray] Acquired bus name: " + std::string(name));
    
    // Register with StatusNotifierWatcher
    GError* error = nullptr;
    GVariant* result = g_dbus_connection_call_sync(
        connection,
        "org.kde.StatusNotifierWatcher",
        "/StatusNotifierWatcher",
        "org.kde.StatusNotifierWatcher",
        "RegisterStatusNotifierItem",
        g_variant_new("(s)", name),
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        -1, nullptr, &error);
    
    if (error) {
        Logger::warn("[Tray] Failed to register with StatusNotifierWatcher: " + std::string(error->message));
        Logger::warn("[Tray] System tray may not be visible. Ensure a system tray is running (e.g., gnome-shell-extension-appindicator)");
        g_error_free(error);
    } else {
        Logger::info("[Tray] Registered with StatusNotifierWatcher");
        if (result) g_variant_unref(result);
    }
}

static void on_name_lost(GDBusConnection* connection, const gchar* name, gpointer user_data) {
    (void)connection; (void)user_data;
    Logger::warn("[Tray] Lost bus name: " + std::string(name));
}

void TrayIconImpl::init() {
    // Find icon path
    const char* icon_env = std::getenv("PROTON_DRIVE_ICON_PATH");
    const char* appdir_env = std::getenv("APPDIR");
    
    if (icon_env && fs::exists(icon_env)) {
        icon_theme_path_ = icon_env;
    } else if (appdir_env) {
        icon_theme_path_ = std::string(appdir_env) + "/usr/share/icons/hicolor/scalable/apps";
    } else {
        std::string local_icons = fs::current_path().string() + "/src-native/resources/icons";
        if (fs::exists(local_icons)) {
            icon_theme_path_ = local_icons;
        } else {
            icon_theme_path_ = "/usr/share/icons/hicolor/scalable/apps";
        }
    }
    
    Logger::info("[Tray] Icon theme path: " + icon_theme_path_);
    
    // Parse introspection data
    GError* error = nullptr;
    sni_node_info_ = g_dbus_node_info_new_for_xml(SNI_INTROSPECTION_XML, &error);
    if (error) {
        Logger::error("[Tray] Failed to parse SNI introspection: " + std::string(error->message));
        g_error_free(error);
        return;
    }
    
    menu_node_info_ = g_dbus_node_info_new_for_xml(DBUSMENU_INTROSPECTION_XML, &error);
    if (error) {
        Logger::error("[Tray] Failed to parse Menu introspection: " + std::string(error->message));
        g_error_free(error);
        return;
    }
    
    // Generate unique bus name
    bus_name_ = "org.kde.StatusNotifierItem-" + std::to_string(getpid()) + "-1";
    
    // Own name on session bus
    g_bus_own_name(G_BUS_TYPE_SESSION, bus_name_.c_str(),
        G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired, on_name_acquired, on_name_lost,
        this, nullptr);
    
    // Start monitoring
    monitor_status();
    
    Logger::info("[Tray] StatusNotifierItem initialized");
}

void TrayIconImpl::cleanup() {
    Logger::debug("[TrayIcon] Cleanup starting, stopping status thread...");
    stop_thread_ = true;
    
    if (status_thread_.joinable()) {
        // Wait up to 2 seconds for thread to notice stop flag (it now checks every 100ms)
        auto start = std::chrono::steady_clock::now();
        while (!thread_exited_ && 
               std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        if (thread_exited_) {
            status_thread_.join();
            Logger::debug("[TrayIcon] Status thread joined");
        } else {
            Logger::warn("[TrayIcon] Status thread did not exit in time, detaching");
            status_thread_.detach();
        }
    }
    
    if (connection_) {
        if (sni_registration_id_ > 0) {
            g_dbus_connection_unregister_object(connection_, sni_registration_id_);
        }
        if (menu_registration_id_ > 0) {
            g_dbus_connection_unregister_object(connection_, menu_registration_id_);
        }
    }
    
    if (sni_node_info_) {
        g_dbus_node_info_unref(sni_node_info_);
    }
    if (menu_node_info_) {
        g_dbus_node_info_unref(menu_node_info_);
    }
    Logger::debug("[TrayIcon] Cleanup complete");
}

// ===== Public TrayIcon Interface =====

TrayIcon::TrayIcon(GtkApplication* app) : app_(app) {
    g_impl = new TrayIconImpl();
    g_impl->app_ = app;
}

TrayIcon::~TrayIcon() {
    if (g_impl) {
        g_impl->cleanup();
        delete g_impl;
        g_impl = nullptr;
    }
}

void TrayIcon::init() {
    if (g_impl) {
        g_impl->init();
    }
}

void TrayIcon::set_toggle_window_callback(std::function<void()> callback) {
    if (g_impl) {
        g_impl->toggle_callback_ = callback;
    }
}

void TrayIcon::stop() {
    if (g_impl) {
        g_impl->cleanup();
        delete g_impl;
        g_impl = nullptr;
    }
}

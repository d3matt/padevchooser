/* $Id$ */

/***
  This file is part of padevchooser.
 
  padevchooser is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2 of the License,
  or (at your option) any later version.
 
  padevchooser is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.
 
  You should have received a copy of the GNU Lesser General Public License
  along with padevchooser; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <gtk/gtk.h>
#include <glade/glade.h>
#include <gconf/gconf-client.h>
#include <libnotify/notify.h>

#include <pulse/pulseaudio.h>
#include <pulse/browser.h>
#include <pulse/glib-mainloop.h>

#include "eggtrayicon.h"
#include "x11prop.h"

#define GCONF_PREFIX "/apps/padevchooser"

struct menu_item_info {
    GtkWidget *menu_item;
    char *name, *server, *device, *description;
    pa_sample_spec sample_spec;
    int sample_spec_valid;
};

static NotifyNotification *notification = NULL;
static gchar *last_events = NULL;

static EggTrayIcon *tray_icon = NULL;
static gchar *current_server = NULL, *current_sink = NULL, *current_source = NULL;
static struct menu_item_info *current_source_menu_item_info = NULL, *current_sink_menu_item_info = NULL, *current_server_menu_item_info = NULL;
static GtkMenu *menu = NULL, *sink_submenu = NULL, *source_submenu = NULL, *server_submenu = NULL;
static GHashTable *server_hash_table = NULL, *sink_hash_table = NULL, *source_hash_table = NULL;
static GtkWidget *no_servers_menu_item = NULL, *no_sinks_menu_item = NULL, *no_sources_menu_item = NULL;
static GtkWidget *default_server_menu_item = NULL, *default_sink_menu_item = NULL, *default_source_menu_item = NULL;
static GtkWidget *other_server_menu_item = NULL, *other_sink_menu_item = NULL, *other_source_menu_item = NULL;
static GtkTooltips *menu_tooltips = NULL;
static int updating = 0;
static time_t startup_time = 0;
static GConfClient *gconf = NULL;
static GladeXML *glade_xml = NULL;
static gboolean notify_on_server_discovery = FALSE, notify_on_sink_discovery = FALSE, notify_on_source_discovery = FALSE, no_notify_on_startup = FALSE;

static void set_sink(const char *server, const char *device);
static void set_source(const char *server, const char *device);
static void set_server(const char *server);

static void set_x11_props(void);

static gboolean find_predicate(const gchar* name, const struct menu_item_info *m, gpointer userdata) {

    return
        strcmp(m->server, current_server) == 0 &&
        (!m->device || strcmp(m->device, userdata) == 0);
}

static void look_for_current_menu_item(
        GHashTable *h,
        const char *device,
        int look_for_device, 
        struct menu_item_info **current_menu_item_info, 
        GtkWidget *default_menu_item,
        GtkWidget *other_menu_item) {

    struct menu_item_info *m; 

    if (!current_server || (look_for_device && !device))
        m = NULL;
    else if (*current_menu_item_info &&
             (strcmp(current_server, (*current_menu_item_info)->server) == 0 &&
              (!look_for_device || strcmp(device, (*current_menu_item_info)->device) == 0)))
        m = *current_menu_item_info;
    else 
        /* Look for the right entry */
        m = g_hash_table_find(h, (GHRFunc) find_predicate, (gpointer) device);

    /* Deactivate the old item */
    if (*current_menu_item_info)
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM((*current_menu_item_info)->menu_item), FALSE);

    /* Update item */
    *current_menu_item_info = m;

    /* Activate the new item */
    if (*current_menu_item_info)
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM((*current_menu_item_info)->menu_item), TRUE);

    /* Enable/Disable the "Default" menu item */
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(default_menu_item), !*current_menu_item_info && (look_for_device ? !device : !current_server));

    /* Enable/Disable the "Other..." menu item and set the tooltip appriately */
    if (!*current_menu_item_info && (look_for_device ? device : current_server)) {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(other_menu_item), TRUE);
        gtk_tooltips_set_tip(GTK_TOOLTIPS(menu_tooltips), other_menu_item, look_for_device ? device : current_server, NULL);
    } else {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(other_menu_item), FALSE);
        gtk_tooltips_set_tip(GTK_TOOLTIPS(menu_tooltips), other_menu_item, NULL, NULL);
    }
}

static void look_for_current_menu_items(void) {
    updating = 1;
    look_for_current_menu_item(server_hash_table, NULL, FALSE, &current_server_menu_item_info, default_server_menu_item, other_server_menu_item);
    look_for_current_menu_item(sink_hash_table, current_sink, TRUE, &current_sink_menu_item_info, default_sink_menu_item, other_sink_menu_item); 
    look_for_current_menu_item(source_hash_table, current_source, TRUE, &current_source_menu_item_info, default_source_menu_item, other_source_menu_item); 
    updating = 0;
}

static void menu_item_info_free(struct menu_item_info *i) {
    if (i->menu_item)
        gtk_widget_destroy(i->menu_item);
    
    g_free(i->name);
    g_free(i->server);
    g_free(i->device);
    g_free(i->description);
    g_free(i);
}

static void notification_closed(void) {
    if (notification) {
        g_object_unref(G_OBJECT(notification));
        notification = NULL;
    }
}

static void notify_event(const char *title, const char*text) {
    char *s;

    if (no_notify_on_startup && time(NULL)-startup_time <= 5)
        return;
    
    if (!notify_is_initted())
        return;
        
    if (!notification) {
        s = g_strdup_printf("<i>%s</i>\n%s", title, text);
        notification = notify_notification_new(title, s, "audio-card", GTK_WIDGET(tray_icon));
        notify_notification_set_category(notification, "device.added");
        notify_notification_set_urgency(notification, NOTIFY_URGENCY_LOW);
        g_signal_connect_swapped(G_OBJECT(notification), "closed", G_CALLBACK(notification_closed), NULL);
    } else {
        s = g_strdup_printf("%s\n\n<i>%s</i>\n%s", last_events, title, text);
        notify_notification_update(notification, title, s, "audio-card");
    }
    
    g_free(last_events);
    last_events = s;

    notify_notification_show(notification, NULL);
}

static GtkWidget *append_radio_menu_item(GtkMenu *menu, const gchar *label, gboolean mnemonic, gboolean prepend) {
    GtkWidget *item;

    if (mnemonic)
        item = gtk_check_menu_item_new_with_mnemonic(label);
    else
        item = gtk_check_menu_item_new_with_label(label);

    gtk_check_menu_item_set_draw_as_radio(GTK_CHECK_MENU_ITEM(item), TRUE); 
    gtk_widget_show_all(item);
    
    if (prepend)
        gtk_menu_shell_prepend(GTK_MENU_SHELL(menu), item);
    else
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    return item;
}

static void sink_change_cb(struct menu_item_info *m) {
    set_sink(m->server, m->device);
}

static void source_change_cb(struct menu_item_info *m) {
    set_source(m->server, m->device);
}

static void server_change_cb(struct menu_item_info *m) {
    set_server(m->server);
}

static struct menu_item_info* add_menu_item_info(GHashTable *h, GtkMenu *menu, const pa_browse_info *i, GCallback callback) {
    struct menu_item_info *m;
    gchar *c;
    const gchar *title;
    gboolean b;
    
    m = g_new(struct menu_item_info, 1);

    m->name = g_strdup(i->name);
    m->server = g_strdup(i->server);
    m->device = g_strdup(i->device);
    m->description = g_strdup(i->description);
    if ((m->sample_spec_valid = !!i->sample_spec))
        m->sample_spec = *i->sample_spec;

    m->menu_item = append_radio_menu_item(menu, m->name, FALSE, TRUE);
    g_signal_connect_swapped(G_OBJECT(m->menu_item), "activate", callback, m);

    if (!m->device)
        c = g_strdup_printf(
                "Name: %s\n"
                "Server: %s",
                i->name,
                i->server);
    else {
        char t[PA_SAMPLE_SPEC_SNPRINT_MAX];
        c = g_strdup_printf(
                "Name: %s\n"
                "Server: %s\n"
                "Device: %s\n"
                "Description: %s\n"
                "Sample Specification: %s",
                i->name,
                i->server,
                i->device,
                i->description ? i->description : "n/a",
                m->sample_spec_valid ? pa_sample_spec_snprint(t, sizeof(t), &m->sample_spec) : "n/a");
    }
    
    gtk_tooltips_set_tip(GTK_TOOLTIPS(menu_tooltips), m->menu_item, c, NULL);

    if (menu == sink_submenu) {
        title = "Networked Audio Sink Discovered";
        b = notify_on_sink_discovery;
    } else if (menu == source_submenu) {
        title = "Networked Audio Source Discovered";
        b = notify_on_source_discovery;
    } else {
        title = "Networked Audio Server Discovered";
        b = notify_on_server_discovery;
    }

    if (b)
        notify_event(title, c);
    
    g_free(c);
    g_hash_table_insert(h, m->name, m);
    
    return m;
}

static void remove_menu_item_info(GHashTable *h, const pa_browse_info *i) {
    struct menu_item_info *m;
    const gchar *title;
    gchar *c;
    gboolean b;

    if (!(m = g_hash_table_lookup(h, i->name)))
        return;
    
    if (h == sink_hash_table) {
        title = "Networked Audio Sink Disappeared";
        b = notify_on_sink_discovery;
    } else if (h == source_hash_table) {
        title = "Networked Audio Source Disappeared";
        b = notify_on_source_discovery;
    } else {
        title = "Networked Audio Server Disappeared";
        b = notify_on_server_discovery;
    }

    c = g_strdup_printf("Name: %s", i->name);

    if (b)
        notify_event(title, c);
    g_free(c);

    g_hash_table_remove(h, i->name);
}

static void update_no_devices_menu_items(void) {
    if (g_hash_table_size(server_hash_table) == 0)
        gtk_widget_show_all(no_servers_menu_item);
    else
        gtk_widget_hide(no_servers_menu_item);

    if (g_hash_table_size(source_hash_table) == 0)
        gtk_widget_show_all(no_sources_menu_item);
    else
        gtk_widget_hide(no_sources_menu_item);

    if (g_hash_table_size(sink_hash_table) == 0)
        gtk_widget_show_all(no_sinks_menu_item);
    else
        gtk_widget_hide(no_sinks_menu_item);
}

static void browse_cb(pa_browser *z, pa_browse_opcode_t c, const pa_browse_info *i, void *userdata) {
    switch (c) {
        case PA_BROWSE_NEW_SERVER:
            add_menu_item_info(server_hash_table, server_submenu, i, (GCallback) server_change_cb);
            break;
            
        case PA_BROWSE_NEW_SINK:
                add_menu_item_info(sink_hash_table, sink_submenu, i, (GCallback) sink_change_cb);
            break;
            
        case PA_BROWSE_NEW_SOURCE:
            add_menu_item_info(source_hash_table, source_submenu, i, (GCallback) source_change_cb);
            break;
            
        case PA_BROWSE_REMOVE_SERVER:
            remove_menu_item_info(server_hash_table, i);
            break;
            
        case PA_BROWSE_REMOVE_SINK:
            remove_menu_item_info(sink_hash_table, i);
            break;
            
        case PA_BROWSE_REMOVE_SOURCE:
            remove_menu_item_info(source_hash_table, i);
            break;
    }

    update_no_devices_menu_items();
    look_for_current_menu_items();
}

static void tray_icon_on_click(GtkWidget *widget, GdkEventButton* event) {
    if (event->type == GDK_BUTTON_PRESS)
        gtk_menu_popup(menu, NULL, NULL, NULL, NULL, event->button, event->time);
}

static void start_manager_cb(void) {
    g_spawn_command_line_async("paman", NULL);
}

static void start_vucontrol_cb(void) {
    g_spawn_command_line_async("pavucontrol", NULL);
}

static void start_vumeter_playback_cb(void) {
    g_spawn_command_line_async("pavumeter", NULL);
}

static void start_vumeter_record_cb(void) {
    g_spawn_command_line_async("pavumeter --record", NULL);
}

static void show_preferences(void) {
    GtkWidget *w, *eb;
    GdkColor white;

    eb = glade_xml_get_widget(glade_xml, "titleEventBox");
    gdk_color_white(gtk_widget_get_colormap(eb), &white);
    gtk_widget_modify_bg(eb, GTK_STATE_NORMAL, &white);
    
    w = glade_xml_get_widget(glade_xml, "preferencesDialog");
    gtk_widget_show_all(w);
    gtk_window_present(GTK_WINDOW(w));
    gtk_dialog_run(GTK_DIALOG(w));
    gtk_widget_hide(w);
}

static void set_props(const char *server, const char *sink, const char *source) {
    if (server != current_server) {
        g_free(current_server);
        current_server = g_strdup(server);
    }

    if (sink != current_sink) {
        g_free(current_sink);
        current_sink = g_strdup(sink);
    }

    if (source != current_source) {
        g_free(current_source);
        current_source = g_strdup(source);
    }

    set_x11_props();
}

static gboolean pstrequal(const char *a, const char *b) {
    if (!a && !b)
        return TRUE;

    if (!a || !b)
        return FALSE;

    return strcmp(a, b) == 0;
}

static void set_sink(const char *server, const char *sink) {
    if (updating)
        return;

    if (server) {
        if (!pstrequal(server, current_server) || !pstrequal(sink, current_sink))
            set_props(server, sink, pstrequal(server, current_server) ? current_source : NULL);
    } else {
        if (!pstrequal(sink, current_sink))
            set_props(current_server, sink, current_source);
    }

    look_for_current_menu_items();
}

static void set_source(const char *server, const char *source) {
    if (updating)
        return;

    if (server) {
        if (!pstrequal(server, current_server) || !pstrequal(source, current_source))
            set_props(server, pstrequal(server, current_server) ? current_sink : NULL, source);
    } else {
        if (!pstrequal(source, current_source))
            set_props(current_server, current_sink, source);
    }

    look_for_current_menu_items();
}

static void set_server(const char *server) {
    if (updating)
        return;
    
    if (!pstrequal(server, current_server))
        set_props(server, NULL, NULL);

    look_for_current_menu_items();
}

static void sink_default_cb(void) {
    set_sink(NULL, NULL);
}

static void source_default_cb(void) {
    set_source(NULL, NULL);
}

static void server_default_cb(void) {
    set_server(NULL);
}

static const gchar *input_dialog(const gchar *title, const gchar *text, const gchar *value) {
    GtkWidget *w, *entry, *label;
    gint response;

    w = glade_xml_get_widget(glade_xml, "inputDialog");

    if (GTK_WIDGET_VISIBLE(w)) {
        gtk_window_present(GTK_WINDOW(w));
        return value;
    }
    
    gtk_window_set_title(GTK_WINDOW(w), title);

    entry = glade_xml_get_widget(glade_xml, "inputEntry");
    gtk_entry_set_text(GTK_ENTRY(entry), value ? value : "");

    label = glade_xml_get_widget(glade_xml, "inputLabel");
    gtk_label_set_markup(GTK_LABEL(label), text);

    gtk_widget_show_all(w);
    gtk_window_present(GTK_WINDOW(w));
    response = gtk_dialog_run(GTK_DIALOG(w));
    gtk_widget_hide(w);

    if (response != GTK_RESPONSE_OK)
        return value;

    return gtk_entry_get_text(GTK_ENTRY(entry));
}

static void sink_other_cb(void) {

    if (updating)
        return;
    
    set_sink(NULL, input_dialog("Other Sink", "Please enter sink name:", current_sink));
}

static void source_other_cb(void) {

    if (updating)
        return;

    set_source(NULL, input_dialog("Other Source", "Please enter source name:", current_source));
}

static void server_other_cb(void) {
    if (updating)
        return;

    set_server(input_dialog("Other Server", "Please enter server name:", current_server));
}

static EggTrayIcon *create_tray_icon(void) {
    GtkTooltips *tips;
    EggTrayIcon *tray_icon;
    GtkWidget *event_box, *icon;

    tray_icon = egg_tray_icon_new("PulseAudio Device Chooser");

    event_box = gtk_event_box_new();
    g_signal_connect_swapped(G_OBJECT(event_box), "button-press-event", G_CALLBACK(tray_icon_on_click), NULL);

    gtk_container_add(GTK_CONTAINER(tray_icon), event_box);
    icon = gtk_image_new_from_icon_name("audio-card", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_container_add(GTK_CONTAINER(event_box), icon);
    
    gtk_widget_show_all(GTK_WIDGET(tray_icon));

    tips = gtk_tooltips_new();
    gtk_tooltips_set_tip(GTK_TOOLTIPS(tips), event_box, "PulseAudio Applet", "I don't know what this is.");

    return tray_icon;
}

static GtkWidget *append_menuitem(GtkMenu *m, const char *text, const char *icon_name) {
    GtkWidget *item;

    item = gtk_image_menu_item_new_with_mnemonic(text);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), item);

    if (icon_name) {
        GtkWidget *i;
        i = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_MENU);
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), i);
    }

    return item;
}

static GtkWidget* append_submenu(GtkMenu *m, const char *text, GtkMenu *sub, const char *icon_name) {
    GtkWidget *item;

    item = append_menuitem(m, text, icon_name);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), GTK_WIDGET(sub));
    return item;
}

static void append_default_device_menu_items(GtkMenu *m, GtkWidget **empty_menu_item, GtkWidget **default_menu_item, GtkWidget**other_menu_item, GCallback default_callback, GCallback other_callback) {

    *empty_menu_item = append_menuitem(m, "No Network Devices Found", NULL);
    gtk_widget_set_sensitive(*empty_menu_item, FALSE);

    gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
    
    *default_menu_item = append_radio_menu_item(m, "_Default", TRUE, FALSE);
    g_signal_connect_swapped(G_OBJECT(*default_menu_item), "activate", default_callback, NULL);
    *other_menu_item = append_radio_menu_item(m, "_Other...", TRUE, FALSE);
    g_signal_connect_swapped(G_OBJECT(*other_menu_item), "activate", other_callback, NULL);
}

static GtkMenu *create_menu(void) {
    GtkWidget *item;
    gchar *c;
    
    menu = GTK_MENU(gtk_menu_new());
    menu_tooltips = gtk_tooltips_new();

    sink_submenu = GTK_MENU(gtk_menu_new());
    source_submenu = GTK_MENU(gtk_menu_new());
    server_submenu = GTK_MENU(gtk_menu_new());

    append_default_device_menu_items(sink_submenu, &no_sinks_menu_item, &default_sink_menu_item, &other_sink_menu_item, sink_default_cb, sink_other_cb);
    append_default_device_menu_items(source_submenu, &no_sources_menu_item, &default_source_menu_item, &other_source_menu_item, source_default_cb, source_other_cb);
    append_default_device_menu_items(server_submenu, &no_servers_menu_item, &default_server_menu_item, &other_server_menu_item, server_default_cb, server_other_cb);

    append_submenu(menu, "Default S_erver", server_submenu, "network-wired");
    append_submenu(menu, "Default S_ink", sink_submenu, "audio-card");
    append_submenu(menu, "Default S_ource", source_submenu, "audio-input-microphone");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    
    item = append_menuitem(menu, "_Manager...", NULL);
    gtk_widget_set_sensitive(item, !!(c = g_find_program_in_path("paman")));
    g_free(c);
    g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(start_manager_cb), NULL);
    
    item = append_menuitem(menu, "_Volume Control...", "multimedia-volume-control");
    gtk_widget_set_sensitive(item, !!(c = g_find_program_in_path("pavucontrol")));
    g_free(c);
    g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(start_vucontrol_cb), NULL);

    item = append_menuitem(menu, "_Volume Meter (Playback)...", NULL);
    gtk_widget_set_sensitive(item, !!(c = g_find_program_in_path("pavumeter")));
    g_free(c);
    g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(start_vumeter_playback_cb), NULL);

    item = append_menuitem(menu, "_Volume Meter (Recording)...", NULL);
    gtk_widget_set_sensitive(item, !!c);
    g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(start_vumeter_record_cb), NULL);
    
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    item = append_menuitem(menu, "_Preferences...", "gtk-preferences");
    g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(show_preferences), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    
    item = gtk_image_menu_item_new_from_stock(GTK_STOCK_QUIT, NULL);
    g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(gtk_main_quit), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
      
    gtk_widget_show_all(GTK_WIDGET(menu));

    return menu;
}

static void get_x11_props(void) {
    char t[256];

    g_free(current_server);
    g_free(current_sink);
    g_free(current_source);

    current_server = g_strdup(x11_get_prop(GDK_DISPLAY(), "PULSE_SERVER", t, sizeof(t)));
    current_sink = g_strdup(x11_get_prop(GDK_DISPLAY(), "PULSE_SINK", t, sizeof(t)));
    current_source = g_strdup(x11_get_prop(GDK_DISPLAY(), "PULSE_SOURCE", t, sizeof(t)));
}

static void set_x11_props(void) {

    if (current_server)
        x11_set_prop(GDK_DISPLAY(), "PULSE_SERVER", current_server);
    else
        x11_del_prop(GDK_DISPLAY(), "PULSE_SERVER");

    if (current_sink)
        x11_set_prop(GDK_DISPLAY(), "PULSE_SINK", current_sink);
    else
        x11_del_prop(GDK_DISPLAY(), "PULSE_SINK");

    if (current_source)
        x11_set_prop(GDK_DISPLAY(), "PULSE_SOURCE", current_source);
    else
        x11_del_prop(GDK_DISPLAY(), "PULSE_SOURCE");

    /* This is used by module-x11-publish to detect whether the
     * properties have been altered. We delete this property here to
     * make sure that the module notices that it is no longer in
     * control */
    x11_del_prop(GDK_DISPLAY(), "PULSE_ID"); 
}

static void start_on_login_cb(GtkCheckButton *w) {
    gchar *c;

    mkdir(g_get_user_config_dir(), 0777);
    c = g_build_filename(g_get_user_config_dir(), "autostart", NULL);
    mkdir(c, 0777);
    g_free(c);
    c = g_build_filename(g_get_user_config_dir(), "autostart", "padevchooser.desktop", NULL);
    
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w))) {
        if (symlink(DESKTOP_FILE, c) < 0 && errno != EEXIST)
            g_warning("symlink() failed: %s", strerror(errno));
    } else {
        if (unlink(c) < 0 && errno != ENOENT)
            g_warning("unlink() failed: %s", strerror(errno));
    }

    g_free(c);
}

static void init_start_on_login_check_button(GtkToggleButton *w) {
    struct stat st;
    gchar *c;

    c = g_build_filename(g_get_user_config_dir(), "autostart", "padevchooser.desktop", NULL);
    gtk_toggle_button_set_active(w, lstat(c, &st) >= 0);
    g_free(c);
}

static void check_button_cb(GtkCheckButton *w, const gchar *key) {
    gboolean b, *ptr;

    ptr = g_object_get_data(G_OBJECT(w), "ptr");
    b = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));
    
    if (*ptr == b)
        return;

    *ptr = b;
    gconf_client_set_bool(gconf, key, b, NULL);

    gtk_widget_set_sensitive(glade_xml_get_widget(glade_xml, "startupCheckButton"), notify_on_server_discovery||notify_on_sink_discovery||notify_on_source_discovery);
}

static void gconf_notify_cb(GConfClient *client, guint cnxn_id, GConfEntry *entry, gpointer userdata) {
    gboolean b;

    b = gconf_value_get_bool(gconf_entry_get_value(entry));
    
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(userdata)) != b)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(userdata), b);

    gtk_widget_set_sensitive(glade_xml_get_widget(glade_xml, "startupCheckButton"), notify_on_server_discovery||notify_on_sink_discovery||notify_on_source_discovery);
}

static void setup_gconf(void) {
    GtkWidget *server_check_button, *sink_check_button, *source_check_button, *startup_check_button, *start_on_login_check_button;

    gconf = gconf_client_get_default();
    g_assert(gconf);

    gconf_client_add_dir(gconf, GCONF_PREFIX, GCONF_CLIENT_PRELOAD_NONE, NULL);

    server_check_button = glade_xml_get_widget(glade_xml, "serverCheckButton");
    sink_check_button = glade_xml_get_widget(glade_xml, "sinkCheckButton");
    source_check_button = glade_xml_get_widget(glade_xml, "sourceCheckButton");
    startup_check_button = glade_xml_get_widget(glade_xml, "startupCheckButton");
    start_on_login_check_button = glade_xml_get_widget(glade_xml, "loginCheckButton");

    g_object_set_data(G_OBJECT(server_check_button), "ptr", &notify_on_server_discovery);
    g_object_set_data(G_OBJECT(sink_check_button), "ptr", &notify_on_sink_discovery);
    g_object_set_data(G_OBJECT(source_check_button), "ptr", &notify_on_source_discovery);
    g_object_set_data(G_OBJECT(startup_check_button), "ptr", &no_notify_on_startup);
    
    notify_on_server_discovery = gconf_client_get_bool(gconf, GCONF_PREFIX"/notify_on_server_discovery", NULL);
    notify_on_sink_discovery = gconf_client_get_bool(gconf, GCONF_PREFIX"/notify_on_sink_discovery", NULL);
    notify_on_source_discovery = gconf_client_get_bool(gconf, GCONF_PREFIX"/notify_on_source_discovery", NULL);
    no_notify_on_startup = gconf_client_get_bool(gconf, GCONF_PREFIX"/no_notify_on_startup", NULL);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(server_check_button), notify_on_server_discovery);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sink_check_button), notify_on_sink_discovery);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(source_check_button), notify_on_source_discovery);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(startup_check_button), no_notify_on_startup);
    init_start_on_login_check_button(GTK_TOGGLE_BUTTON(start_on_login_check_button));

    gtk_widget_set_sensitive(startup_check_button, notify_on_server_discovery||notify_on_sink_discovery||notify_on_source_discovery);
    
    gconf_client_notify_add(gconf, GCONF_PREFIX"/notify_on_server_discovery", gconf_notify_cb, server_check_button, NULL, NULL);
    gconf_client_notify_add(gconf, GCONF_PREFIX"/notify_on_sink_discovery", gconf_notify_cb, sink_check_button, NULL, NULL);
    gconf_client_notify_add(gconf, GCONF_PREFIX"/notify_on_source_discovery", gconf_notify_cb, source_check_button, NULL, NULL);
    gconf_client_notify_add(gconf, GCONF_PREFIX"/no_notify_on_startup", gconf_notify_cb, startup_check_button, NULL, NULL);

    g_signal_connect(G_OBJECT(server_check_button), "toggled", G_CALLBACK(check_button_cb), GCONF_PREFIX"/notify_on_server_discovery");
    g_signal_connect(G_OBJECT(sink_check_button), "toggled", G_CALLBACK(check_button_cb), GCONF_PREFIX"/notify_on_sink_discovery");
    g_signal_connect(G_OBJECT(source_check_button), "toggled", G_CALLBACK(check_button_cb), GCONF_PREFIX"/notify_on_source_discovery");
    g_signal_connect(G_OBJECT(startup_check_button), "toggled", G_CALLBACK(check_button_cb), GCONF_PREFIX"/no_notify_on_startup");
    g_signal_connect(G_OBJECT(start_on_login_check_button), "toggled", G_CALLBACK(start_on_login_cb), NULL);
}

int main(int argc, char *argv[]) {
    pa_browser *b = NULL;
    pa_glib_mainloop *m = NULL;

    startup_time = time(NULL);
    
    gtk_init(&argc, &argv);

    glade_xml = glade_xml_new(GLADE_FILE, NULL, NULL);
    g_assert(glade_xml);

    m = pa_glib_mainloop_new(NULL);
    g_assert(m);

    server_hash_table = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, (GDestroyNotify) menu_item_info_free);
    sink_hash_table = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, (GDestroyNotify) menu_item_info_free);
    source_hash_table = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, (GDestroyNotify) menu_item_info_free);

    create_menu();
    update_no_devices_menu_items();

    setup_gconf();

    notify_init("PulseAudio Applet");

    get_x11_props();
    
    if (!(b = pa_browser_new(pa_glib_mainloop_get_api(m)))) {
        g_warning("pa_browser_new() failed.");
        goto fail;
    }

    pa_browser_set_callback(b, browse_cb, NULL);

    tray_icon = create_tray_icon();
    
    gtk_main();

fail:
    if (b)
        pa_browser_unref(b);

    if (m)
        pa_glib_mainloop_free(m);

    if (server_hash_table)
        g_hash_table_destroy(server_hash_table);
    if (sink_hash_table)
        g_hash_table_destroy(sink_hash_table);
    if (source_hash_table)
        g_hash_table_destroy(source_hash_table);

    if (notification)
        g_object_unref(G_OBJECT(notification));

    g_free(last_events);
    
    return 0;
}

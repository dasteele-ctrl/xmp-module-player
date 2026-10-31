#include "tray_icon.h"

struct XmpTrayIcon {
    GtkStatusIcon *status_icon;
    GtkWidget *menu;
    GtkWidget *track_item;
    GtkWidget *play_item;
    XmpTrayActions actions;
    gchar *last_title;
    gboolean last_playing;
};

static void on_show(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    XmpTrayIcon *tray = user_data;
    if (tray->actions.show)
        tray->actions.show(tray->actions.user_data);
}

static void on_play_pause(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    XmpTrayIcon *tray = user_data;
    if (tray->actions.play_pause)
        tray->actions.play_pause(tray->actions.user_data);
}

static void on_stop(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    XmpTrayIcon *tray = user_data;
    if (tray->actions.stop)
        tray->actions.stop(tray->actions.user_data);
}

static void on_previous(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    XmpTrayIcon *tray = user_data;
    if (tray->actions.previous)
        tray->actions.previous(tray->actions.user_data);
}

static void on_next(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    XmpTrayIcon *tray = user_data;
    if (tray->actions.next)
        tray->actions.next(tray->actions.user_data);
}

static void on_quit(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    XmpTrayIcon *tray = user_data;
    if (tray->actions.quit)
        tray->actions.quit(tray->actions.user_data);
}

static GtkWidget *append_item(XmpTrayIcon *tray, const gchar *label,
                               GCallback callback)
{
    GtkWidget *item = gtk_menu_item_new_with_label(label);
    gtk_menu_shell_append(GTK_MENU_SHELL(tray->menu), item);
    g_signal_connect(item, "activate", callback, tray);
    return item;
}

static void append_separator(XmpTrayIcon *tray)
{
    gtk_menu_shell_append(GTK_MENU_SHELL(tray->menu),
                          gtk_separator_menu_item_new());
}

static void on_icon_activate(GtkStatusIcon *status_icon, gpointer user_data)
{
    (void)status_icon;
    on_show(NULL, user_data);
}

static void on_icon_popup(GtkStatusIcon *status_icon, guint button,
                          guint activate_time, gpointer user_data)
{
    XmpTrayIcon *tray = user_data;
    /* GTK3's tray menu positioning is compatible with the Xfce notification
     * area even when the status icon lives in a vertical panel. */
    gtk_menu_popup(GTK_MENU(tray->menu), NULL, NULL,
                   gtk_status_icon_position_menu, status_icon,
                   button, activate_time);
}

static GdkPixbuf *make_white_tray_icon(GdkPixbuf *source)
{
    if (!source)
        return NULL;

    /* The application PNG contains substantial transparent padding, making
     * the X look tiny in the panel. Crop to the visible alpha bounds (with a
     * small safety margin) before GTK scales it into the tray's icon slot.
     * This does not increase the panel height or change the window icon. */
    gint source_width = gdk_pixbuf_get_width(source);
    gint source_height = gdk_pixbuf_get_height(source);
    gint source_stride = gdk_pixbuf_get_rowstride(source);
    gint source_channels = gdk_pixbuf_get_n_channels(source);
    gint left = source_width;
    gint top = source_height;
    gint right = -1;
    gint bottom = -1;

    if (gdk_pixbuf_get_has_alpha(source)) {
        const guchar *source_pixels = gdk_pixbuf_get_pixels(source);
        for (gint y = 0; y < source_height; ++y) {
            const guchar *row = source_pixels + y * source_stride;
            for (gint x = 0; x < source_width; ++x) {
                const guchar *pixel = row + x * source_channels;
                if (pixel[3] > 16) {
                    left = MIN(left, x);
                    top = MIN(top, y);
                    right = MAX(right, x);
                    bottom = MAX(bottom, y);
                }
            }
        }
    }

    GdkPixbuf *white = NULL;
    if (right >= left && bottom >= top) {
        gint margin = MAX(2, MIN(right - left + 1, bottom - top + 1) / 40);
        left = MAX(0, left - margin);
        top = MAX(0, top - margin);
        right = MIN(source_width - 1, right + margin);
        bottom = MIN(source_height - 1, bottom + margin);
        GdkPixbuf *cropped = gdk_pixbuf_new_subpixbuf(
            source, left, top, right - left + 1, bottom - top + 1);
        white = gdk_pixbuf_copy(cropped);
        g_object_unref(cropped);
    } else {
        white = gdk_pixbuf_copy(source);
    }

    /* Recolor only the tray copy to white. Its alpha is untouched, so only
     * the X appears; the tray's actual background stays fully transparent. */
    guchar *pixels = gdk_pixbuf_get_pixels(white);
    gint width = gdk_pixbuf_get_width(white);
    gint height = gdk_pixbuf_get_height(white);
    gint rowstride = gdk_pixbuf_get_rowstride(white);
    gint channels = gdk_pixbuf_get_n_channels(white);

    for (gint y = 0; y < height; ++y) {
        guchar *row = pixels + y * rowstride;
        for (gint x = 0; x < width; ++x) {
            guchar *pixel = row + x * channels;
            pixel[0] = 255;
            pixel[1] = 255;
            pixel[2] = 255;
        }
    }
    return white;
}

XmpTrayIcon *xmp_tray_icon_new(GdkPixbuf *icon, const XmpTrayActions *actions)
{
    g_return_val_if_fail(actions != NULL, NULL);

    XmpTrayIcon *tray = g_new0(XmpTrayIcon, 1);
    tray->actions = *actions;
    tray->menu = gtk_menu_new();

    tray->track_item = gtk_menu_item_new_with_label("No music loaded");
    gtk_widget_set_sensitive(tray->track_item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(tray->menu), tray->track_item);
    append_separator(tray);
    append_item(tray, "Show XMP Player", G_CALLBACK(on_show));
    append_separator(tray);
    append_item(tray, "Previous", G_CALLBACK(on_previous));
    tray->play_item = append_item(tray, "Play", G_CALLBACK(on_play_pause));
    append_item(tray, "Stop", G_CALLBACK(on_stop));
    append_item(tray, "Next", G_CALLBACK(on_next));
    append_separator(tray);
    append_item(tray, "Quit XMP", G_CALLBACK(on_quit));

    GdkPixbuf *tray_pixbuf = make_white_tray_icon(icon);
    tray->status_icon = tray_pixbuf ? gtk_status_icon_new_from_pixbuf(tray_pixbuf)
                             : gtk_status_icon_new_from_icon_name("multimedia-player");
    if (tray_pixbuf)
        g_object_unref(tray_pixbuf);
    gtk_status_icon_set_title(tray->status_icon, "XMP Module Player");
    gtk_status_icon_set_visible(tray->status_icon, TRUE);
    g_signal_connect(tray->status_icon, "activate",
                     G_CALLBACK(on_icon_activate), tray);
    g_signal_connect(tray->status_icon, "popup-menu",
                     G_CALLBACK(on_icon_popup), tray);
    gtk_widget_show_all(tray->menu);
    xmp_tray_icon_update(tray, FALSE, NULL);
    return tray;
}

gboolean xmp_tray_icon_is_available(XmpTrayIcon *tray)
{
    /* If there is no notification area, closing the main window must quit
     * normally rather than leave an invisible background process. */
    return tray && gtk_status_icon_is_embedded(tray->status_icon);
}

void xmp_tray_icon_update(XmpTrayIcon *tray, gboolean playing,
                          const gchar *track_title)
{
    if (!tray)
        return;

    const gchar *title = (track_title && *track_title) ? track_title : "No music loaded";
    if (tray->last_playing == playing &&
        g_strcmp0(tray->last_title, title) == 0)
        return;

    tray->last_playing = playing;
    g_free(tray->last_title);
    tray->last_title = g_strdup(title);

    gboolean shortened = g_utf8_strlen(title, -1) > 48;
    gchar *short_title = shortened
        ? g_utf8_substring(title, 0, 48) : g_strdup(title);
    gchar *menu_title = g_strdup_printf("%s%s", short_title,
                                        shortened ? "…" : "");
    /* The title comes from the player's track label. Escape it before using
     * Pango markup so filenames containing '&' or '<' render safely. */
    gchar *escaped_title = g_markup_escape_text(title, -1);
    gchar *tooltip = g_strdup_printf(
        "<span foreground=\"#ffffff\" weight=\"bold\" size=\"large\">%s</span>\n"
        "<span foreground=\"#e0e9eb\" weight=\"bold\">XMP Module Player · %s</span>",
        escaped_title, playing ? "Playing" : "Not playing");

    gtk_menu_item_set_label(GTK_MENU_ITEM(tray->track_item), menu_title);
    gtk_menu_item_set_label(GTK_MENU_ITEM(tray->play_item),
                            playing ? "Pause" : "Play");
    gtk_status_icon_set_tooltip_markup(tray->status_icon, tooltip);

    g_free(escaped_title);
    g_free(tooltip);
    g_free(menu_title);
    g_free(short_title);
}

void xmp_tray_icon_free(XmpTrayIcon *tray)
{
    if (!tray)
        return;
    gtk_status_icon_set_visible(tray->status_icon, FALSE);
    g_object_unref(tray->status_icon);
    gtk_widget_destroy(tray->menu);
    g_free(tray->last_title);
    g_free(tray);
}

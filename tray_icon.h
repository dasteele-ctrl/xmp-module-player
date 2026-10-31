#ifndef XMP_TRAY_ICON_H
#define XMP_TRAY_ICON_H

#include <gtk/gtk.h>

typedef struct XmpTrayIcon XmpTrayIcon;

typedef struct {
    void (*show)(gpointer user_data);
    void (*play_pause)(gpointer user_data);
    void (*stop)(gpointer user_data);
    void (*previous)(gpointer user_data);
    void (*next)(gpointer user_data);
    void (*quit)(gpointer user_data);
    gpointer user_data;
} XmpTrayActions;

XmpTrayIcon *xmp_tray_icon_new(GdkPixbuf *icon, const XmpTrayActions *actions);
gboolean xmp_tray_icon_is_available(XmpTrayIcon *tray);
void xmp_tray_icon_update(XmpTrayIcon *tray, gboolean playing,
                          const gchar *track_title);
void xmp_tray_icon_free(XmpTrayIcon *tray);

#endif

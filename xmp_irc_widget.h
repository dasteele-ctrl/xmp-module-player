#ifndef XMP_IRC_WIDGET_H
#define XMP_IRC_WIDGET_H

#include <gtk/gtk.h>

/* Embed this widget in an XMP GtkNotebook page or another GTK3 container.
 * It owns the IRC connection and cleans up when destroyed. */
GtkWidget *xmp_irc_widget_new(void);

/* Convenience API for the XMP player's existing GtkNotebook. Returns the
 * newly added page index. The page and connection live with the notebook. */
gint xmp_irc_add_notebook_page(GtkNotebook *notebook);

#endif

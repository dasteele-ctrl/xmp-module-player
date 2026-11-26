#ifndef MODULEZ_VISUALIZER_H
#define MODULEZ_VISUALIZER_H
#include <gtk/gtk.h>
#include <libsoup/soup.h>
void modulez_visualizer_remote(SoupServerMessage *msg,const char *group,const char *choice);
#include "visualizer_pcm.h"
GtkWidget *modulez_visualizer_page_new(void);
gboolean modulez_visualizer_toggle_fullscreen(void);
gboolean modulez_visualizer_is_fullscreen(void);
void modulez_visualizer_show_track(const char *identity, const char *title, const char *artist);
void modulez_visualizer_set_track_navigation(gboolean (*skip_track)(gint));
void modulez_visualizer_set_toolbar_theme(const char *panel,
                                        const char *selection,
                                        const char *accent);
#endif

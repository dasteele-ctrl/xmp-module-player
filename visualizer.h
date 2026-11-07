#ifndef MODULEZ_VISUALIZER_H
#define MODULEZ_VISUALIZER_H
#include <gtk/gtk.h>
#include "visualizer_pcm.h"
GtkWidget *modulez_visualizer_page_new(void);
gboolean modulez_visualizer_toggle_fullscreen(void);
void modulez_visualizer_set_track_navigation(gboolean (*skip_track)(gint));
#endif

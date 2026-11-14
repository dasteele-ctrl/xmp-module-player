#ifndef MODULEZ_ABOUT_TRIBUTE_H
#define MODULEZ_ABOUT_TRIBUTE_H
#include <gtk/gtk.h>
/* Takes the unparented About image and returns its hidden click target. */
GtkWidget *modulez_about_tribute_wrap(GtkWidget *image);
/* Optional application dialog styling, invoked before the dialog is shown. */
GtkWidget *modulez_about_tribute_wrap_themed(GtkWidget *image,
                                             void (*theme)(GtkWidget *dialog));
#endif

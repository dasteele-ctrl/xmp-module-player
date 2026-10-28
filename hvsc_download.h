#ifndef HVSC_DOWNLOAD_H
#define HVSC_DOWNLOAD_H

#include <gio/gio.h>

#define HVSC_DOWNLOAD_URL \
    "https://hvsc.brona.dk/HVSC/HVSC_85-all-of-them.7z"

typedef void (*HvscProgressCallback)(
    double fraction,
    const char *status,
    gpointer user_data);

void hvsc_download_async(
    const char *parent_directory,
    HvscProgressCallback callback,
    gpointer user_data);

/* Request cancellation of the active installation.
   Safe to call from the GTK main thread. */
void hvsc_download_cancel(void);

/* TRUE while an installation worker is active. */
gboolean hvsc_download_is_running(void);

#endif

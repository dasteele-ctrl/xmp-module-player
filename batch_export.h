#ifndef BATCH_EXPORT_H
#define BATCH_EXPORT_H

#include <gio/gio.h>
#include "export_wav.h"

/* Render and encode one local track, then move the finished file into place.
 * The caller owns the cancellation flag. */
gboolean batch_export_track(ExportWavRequest request, const BatchFormat *format,
                            const char *folder, volatile gint *progress,
                            volatile gint *cancel,
                            GError **error);
#endif

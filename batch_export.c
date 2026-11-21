#define _GNU_SOURCE
#include "batch_export.h"
#include "uade_audio.h"
#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

static gchar *track_stem(const char *path)
{
    g_autofree char *name = g_path_get_basename(path);
    /* Amiga files can put their format first, e.g. mdat.Turrican. */
    for (guint i = 0; uade_name_tags[i]; ++i) {
        gsize n = strlen(uade_name_tags[i]);
        if (!g_ascii_strncasecmp(name, uade_name_tags[i], n) &&
            name[n] == '.' && name[n + 1])
            return g_strdup(name + n + 1);
    }
    char *dot = strrchr(name, '.');
    if (dot && dot != name) *dot = '\0';
    return g_steal_pointer(&name);
}

gboolean batch_export_track(ExportWavRequest request, const BatchFormat *format,
                            const char *folder, volatile gint *progress,
                            volatile gint *cancel,
                            GError **error)
{
    if (!g_file_test(request.source, G_FILE_TEST_IS_REGULAR)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Local audio file unavailable");
        return FALSE;
    }
    g_autofree char *work = g_build_filename(folder, ".modulez-export-XXXXXX", NULL);
    if (!g_mkdtemp(work)) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "Could not create export directory: %s", g_strerror(errno));
        return FALSE;
    }
    g_autofree char *encoded = g_strdup_printf("%s/audio.%s", work, format->extension);
    g_autofree char *stem = track_stem(request.source);
    g_autofree char *fallback_title = g_filename_display_name(stem);
    request.fallback_title = fallback_title;
    request.output = format;
    request.destination = encoded;
    char detail[512];
    gboolean ok = FALSE;
    if (!export_wav_render(&request, progress, cancel, detail, sizeof detail)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, detail);
        goto cleanup;
    }
    /* Atomic no-replace also handles concurrent exports and case-insensitive volumes. */
    for (guint number = 1; !g_atomic_int_get(cancel); ++number) {
        g_autofree char *name = number == 1
            ? g_strdup_printf("%s.%s", stem, format->extension)
            : g_strdup_printf("%s (%u).%s", stem, number, format->extension);
        g_autofree char *dest = g_build_filename(folder, name, NULL);
        if (renameat2(AT_FDCWD, encoded, AT_FDCWD, dest, RENAME_NOREPLACE) == 0) {
            ok = TRUE;
            break;
        }
        if (errno != EEXIST) {
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                        "Could not save %s: %s", name, g_strerror(errno));
            break;
        }
    }
cleanup:
    if (!ok && g_atomic_int_get(cancel) && error && !*error)
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Export cancelled");
    g_unlink(encoded);
    g_rmdir(work);
    return ok;
}

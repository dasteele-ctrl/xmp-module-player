#define _GNU_SOURCE
#include "player_config.h"
#include <fcntl.h>
#include <stdio.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <unistd.h>

gchar *player_config_path(const gchar *name)
{
    return g_build_filename(g_get_user_config_dir(), "modulez-player", name, NULL);
}

static gboolean file_error(const gchar *path, GError **error)
{
    int code = errno;
    g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(code),
                "%s: %s", path, g_strerror(code));
    return FALSE;
}

static gboolean copy_missing(const gchar *src, const gchar *dst, GError **error)
{
    GStatBuf from, to;
    if (g_lstat(src, &from) != 0)
        return file_error(src, error);

    gboolean directory = S_ISDIR(from.st_mode);
    if (!directory && !S_ISREG(from.st_mode))
        return TRUE;

    if (g_lstat(dst, &to) == 0) {
        if (!directory || !S_ISDIR(to.st_mode))
            return TRUE;
    } else if (errno != ENOENT) {
        return file_error(dst, error);
    } else if (directory && g_mkdir(dst, 0700) != 0) {
        return file_error(dst, error);
    }

    if (directory) {
        g_autoptr(GDir) dir = g_dir_open(src, 0, error);
        if (!dir)
            return FALSE;
        const gchar *name;
        while ((name = g_dir_read_name(dir))) {
            g_autofree gchar *child = g_build_filename(src, name, NULL);
            g_autofree gchar *target = g_build_filename(dst, name, NULL);
            if (!copy_missing(child, target, error))
                return FALSE;
        }
        return TRUE;
    }

    /* Publish only a complete copy, even if the player is interrupted. */
    g_autofree gchar *tmp = g_strconcat(dst, ".XXXXXX", NULL);
    int fd = g_mkstemp(tmp);
    if (fd < 0)
        return file_error(tmp, error);
    close(fd);
    g_autoptr(GFile) source = g_file_new_for_path(src);
    g_autoptr(GFile) staged = g_file_new_for_path(tmp);
    gboolean ok = g_file_copy(source, staged, G_FILE_COPY_OVERWRITE,
                              NULL, NULL, NULL, error);
    if (ok && renameat2(AT_FDCWD, tmp, AT_FDCWD, dst, RENAME_NOREPLACE) != 0 && errno != EEXIST)
        ok = file_error(dst, error);
    g_unlink(tmp);
    return ok;
}

gboolean player_config_migrate(GError **error)
{
    g_autofree gchar *marker = player_config_path(".migrated-1.7.2");
    if (g_file_test(marker, G_FILE_TEST_EXISTS))
        return TRUE;

    g_autofree gchar *legacy = g_build_filename(g_get_user_config_dir(),
                                                 "xmp-module-player", NULL);
    if (!g_file_test(legacy, G_FILE_TEST_IS_DIR))
        return TRUE;

    g_autofree gchar *dest = player_config_path(NULL);
    if (g_mkdir_with_parents(dest, 0700) != 0)
        return file_error(dest, error);

    g_autoptr(GDir) dir = g_dir_open(legacy, 0, error);
    if (!dir)
        return FALSE;
    const gchar *name;
    while ((name = g_dir_read_name(dir))) {
        if (g_str_equal(name, ".migrated-1.7.2"))
            continue;
        g_autofree gchar *src = g_build_filename(legacy, name, NULL);
        g_autofree gchar *dst = player_config_path(name);
        if (!copy_missing(src, dst, error))
            return FALSE;
    }
    return g_file_set_contents(marker, "", 0, error);
}

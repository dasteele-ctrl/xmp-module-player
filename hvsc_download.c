#include "hvsc_download.h"
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

/* Cancellation state shared with the GTK thread. */
static GMutex hvsc_lock;
static gboolean hvsc_running = FALSE;
static GCancellable *hvsc_cancellable = NULL;
static GSubprocess *hvsc_process = NULL;

gboolean hvsc_download_is_running(void)
{
    g_mutex_lock(&hvsc_lock);
    gboolean running = hvsc_running;
    g_mutex_unlock(&hvsc_lock);
    return running;
}

void hvsc_download_cancel(void)
{
    g_mutex_lock(&hvsc_lock);

    if (hvsc_running && hvsc_cancellable) {
        g_cancellable_cancel(hvsc_cancellable);

        if (hvsc_process)
            g_subprocess_send_signal(hvsc_process, SIGTERM);
    }

    g_mutex_unlock(&hvsc_lock);
}

typedef struct {
    char *parent;
    HvscProgressCallback callback;
    gpointer user_data;
} HvscJob;

typedef struct {
    HvscProgressCallback callback;
    gpointer user_data;
    double fraction;
    char *status;
} HvscUpdate;

static gboolean deliver_update(gpointer data)
{
    HvscUpdate *update = data;

    update->callback(update->fraction,
                     update->status,
                     update->user_data);

    g_free(update->status);
    g_free(update);

    return G_SOURCE_REMOVE;
}

static void report_progress(HvscJob *job,
                            double fraction,
                            const char *status)
{
    HvscUpdate *update = g_new0(HvscUpdate, 1);

    update->callback = job->callback;
    update->user_data = job->user_data;
    update->fraction = fraction;
    update->status = g_strdup(status);

    g_main_context_invoke(NULL, deliver_update, update);
}

static gpointer download_worker(gpointer data)
{
    HvscJob *job = data;

    char *executable = g_file_read_link(
        "/proc/self/exe", NULL);
    char *directory = executable
        ? g_path_get_dirname(executable) : NULL;

    const char *appdir = g_getenv("APPDIR");
    char *script = appdir
        ? g_build_filename(appdir, "usr", "share",
                           "xmp-module-player",
                           "hvsc-install-safe.sh", NULL)
        : NULL;

    if (!script || !g_file_test(script, G_FILE_TEST_IS_REGULAR)) {
        g_free(script);
        script = directory
            ? g_build_filename(directory,
                               "hvsc-install-safe.sh", NULL)
            : NULL;
    }

    g_free(directory);
    g_free(executable);

    if (!script || !g_file_test(script, G_FILE_TEST_IS_REGULAR)) {
        report_progress(job, -1.0,
                        "ERROR: HVSC installer script not found.");
        goto finish;
    }

    char *argv[] = {
        "/bin/bash",
        script,
        job->parent,
        NULL
    };

    GError *error = NULL;
    GSubprocess *process = g_subprocess_newv(
        (const gchar * const *)argv,
        G_SUBPROCESS_FLAGS_STDOUT_PIPE |
        G_SUBPROCESS_FLAGS_STDERR_MERGE,
        &error);

    if (!process) {
        char *message = g_strdup_printf(
            "ERROR: %s",
            error ? error->message :
                    "Unable to start installer.");

        report_progress(job, -1.0, message);

        g_free(message);
        g_clear_error(&error);
        goto finish;
    }

    g_mutex_lock(&hvsc_lock);
    hvsc_process = g_object_ref(process);

    if (g_cancellable_is_cancelled(hvsc_cancellable))
        g_subprocess_send_signal(process, SIGTERM);

    g_mutex_unlock(&hvsc_lock);

    GDataInputStream *stream = g_data_input_stream_new(
        g_subprocess_get_stdout_pipe(process));

    double fraction = 0.0;
    gboolean read_failed = FALSE;
    char *last_status = g_strdup("Preparing HVSC installation...");

    while (TRUE) {
        gsize length = 0;

        char *line = g_data_input_stream_read_line(
            stream, &length, NULL, &error);

        if (!line)
            break;

        g_strstrip(line);

        if (g_str_has_prefix(line, "PROGRESS: ")) {
            int percent = atoi(line + 10);
            percent = CLAMP(percent, 0, 100);
            fraction = percent / 100.0;

            char *message = g_strdup_printf(
                "%s (%d%%)", last_status, percent);

            report_progress(job, fraction, message);
            g_free(message);
        }
        else if (g_str_has_prefix(line, "STATUS: ")) {
            g_free(last_status);
            last_status = g_strdup(line + 8);

            report_progress(job, fraction, last_status);
        }
        else if (g_str_has_prefix(line, "ERROR: ")) {
            report_progress(job, -1.0, line);
        }
        else if (g_str_has_prefix(line, "SUCCESS: ")) {
            report_progress(job, 1.0, line);
        }

        g_free(line);
    }

    if (error) {
        read_failed = TRUE;
        char *message = g_strdup_printf(
            "ERROR: Reading installer output failed: %s",
            error->message);
        report_progress(job, -1.0, message);
        g_free(message);
        g_clear_error(&error);
    }

    gboolean install_ok =
        g_subprocess_wait_check(process, NULL, &error);

    char *final_error = NULL;

    if (!install_ok) {
        final_error = g_strdup_printf(
            "ERROR: HVSC installation failed: %s",
            error ? error->message : "Unknown error");
        g_clear_error(&error);
    }

    g_mutex_lock(&hvsc_lock);
    g_clear_object(&hvsc_process);
    g_mutex_unlock(&hvsc_lock);

    g_free(last_status);
    g_object_unref(stream);
    g_object_unref(process);

    g_mutex_lock(&hvsc_lock);

    gboolean cancelled = hvsc_cancellable &&
        g_cancellable_is_cancelled(hvsc_cancellable);

    g_clear_object(&hvsc_cancellable);
    hvsc_running = FALSE;

    g_mutex_unlock(&hvsc_lock);

    if (cancelled)
        report_progress(job, -2.0, "Installation cancelled.");
    else if (install_ok && !read_failed)
        report_progress(job, 2.0,
                        "HVSC installation completed successfully.");
    else
        report_progress(job, -2.0,
                        final_error ? final_error :
                        "ERROR: HVSC installation failed.");

    g_free(final_error);

    g_free(script);
    g_free(job->parent);
    g_free(job);
    return NULL;

finish:
    g_mutex_lock(&hvsc_lock);

    gboolean was_cancelled = hvsc_cancellable &&
        g_cancellable_is_cancelled(hvsc_cancellable);

    g_clear_object(&hvsc_process);
    g_clear_object(&hvsc_cancellable);
    hvsc_running = FALSE;

    g_mutex_unlock(&hvsc_lock);

    if (was_cancelled)
        report_progress(job, -2.0, "Installation cancelled.");
    else
        report_progress(job, -2.0,
                        "ERROR: Unable to start HVSC installer.");

    g_free(script);
    g_free(job->parent);
    g_free(job);

    return NULL;
}

void hvsc_download_async(
    const char *parent_directory,
    HvscProgressCallback callback,
    gpointer user_data)
{
    if (!parent_directory || !callback)
        return;

    g_mutex_lock(&hvsc_lock);

    if (hvsc_running) {
        g_mutex_unlock(&hvsc_lock);
        return;
    }

    hvsc_running = TRUE;
    hvsc_cancellable = g_cancellable_new();

    g_mutex_unlock(&hvsc_lock);

    HvscJob *job = g_new0(HvscJob, 1);

    job->parent = g_strdup(parent_directory);
    job->callback = callback;
    job->user_data = user_data;

    GThread *thread = g_thread_new(
        "hvsc-installer",
        download_worker,
        job);

    g_thread_unref(thread);
}

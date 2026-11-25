/* Included by main.c after the local playback and playlist functions. */
#include <archive.h>
#include <archive_entry.h>
#include <stdlib.h>

#define HS_BASE "https://archive.org/details/headspace-rmf-collections"
#define HS_LIMIT (32u * 1024u * 1024u)

static const char *const hs_albums[] = {
    "mellow", "upbeat", "stingers", "ambient", "classical", "sonic-icons",
    "underground", "moods", "atmospheres", "holiday", "holiday-sfx",
    "sonic-icons2", "travel", "electronica", "bestof", "bundle"
};
static const char *const hs_names[] = {
    "Mellow", "Upbeat", "Stingers", "Ambient", "Classical", "Sonic Icons",
    "Underground", "Moods", "Atmospheres", "Holiday", "Holiday Sound Effects",
    "Sonic Icons II", "Travel", "Electronica", "Best Of", "Bundle"
};
static const char hs_style[] =
    "<meta charset=utf-8><style>:root{color-scheme:dark}"
    "body{background:#202124;color:white;font:bold 20px/1.5 sans-serif;"
    "max-width:58rem;margin:1rem auto;padding:0 1rem}"
    "li{margin:.55rem 0}a{color:white}a:focus-visible{outline:3px solid #9bd5ff}"
    ".hs-meter{display:inline-flex;align-items:center;gap:.4em;margin-left:.6em;"
    "font-size:.85em;color:white}.hs-meter progress{width:6em;height:.75em;"
    "accent-color:#79cfff}</style>"
    "<script>"
    "let hsMeter=null;"
    "document.addEventListener('click',e=>{"
      "const a=e.target.closest('a[href^=\"modulez-headspace:\"]');"
      "if(!a||a.getAttribute('href').endsWith(':home')||hsMeter)return;"
      "hsMeter=document.createElement('span');hsMeter.className='hs-meter';"
      "hsMeter.innerHTML='<progress aria-label=\"Album download\"></progress><span></span>';"
      "hsMeter.dataset.save=a.getAttribute('href').startsWith('modulez-headspace:save/');"
      "a.after(hsMeter);window.hsProgress(-1);"
    "});"
    "window.hsProgress=p=>{if(!hsMeter)return;"
      "const bar=hsMeter.querySelector('progress'),label=hsMeter.lastElementChild;"
      "if(p<0){bar.removeAttribute('value');"
      "label.textContent=hsMeter.dataset.save==='true'?'Preparing ZIP…':'Opening…';}"
      "else{const copying=p>=101;const value=copying?p-101:p;"
      "bar.max=100;bar.value=value;label.textContent=(copying?'Saving… ':'')+value+'%';}"
    "};"
    "window.hsProgressDone=saved=>{"
      "const meter=hsMeter;if(!meter)return;hsMeter=null;"
      "if(!saved){meter.remove();return;}"
      "const bar=meter.querySelector('progress');bar.max=100;bar.value=100;"
      "meter.lastElementChild.textContent='Saved to Downloads';"
      "setTimeout(()=>meter.remove(),2000);"
    "};"
    "</script>";

static gchar *hs_cache;
static gboolean hs_busy;
static gint hs_progress_percent;
static guint hs_progress_timer;
static gint hs_last_percent;

/* The download worker only updates the atomic percentage. WebKit calls
 * stay on the GTK main thread. */
static void hs_run_js(WebKitWebView *web, const gchar *js)
{
    webkit_web_view_evaluate_javascript(web, js, -1, NULL, NULL, NULL, NULL, NULL);
}

static gboolean hs_update_progress(gpointer data)
{
    gint percent = g_atomic_int_get(&hs_progress_percent);
    if (percent == hs_last_percent) return G_SOURCE_CONTINUE;
    hs_last_percent = percent;
    g_autofree gchar *js = g_strdup_printf(
        "window.hsProgress && window.hsProgress(%d)", percent);
    hs_run_js(WEBKIT_WEB_VIEW(data), js);
    return G_SOURCE_CONTINUE;
}

static void hs_cleanup(void)
{
    if (!hs_cache) return;
    GDir *dir = g_dir_open(hs_cache, 0, NULL);
    if (dir) {
        const gchar *name;
        while ((name = g_dir_read_name(dir))) {
            g_autofree gchar *path = g_build_filename(hs_cache, name, NULL);
            g_unlink(path);
        }
        g_dir_close(dir);
    }
    g_rmdir(hs_cache);
    g_clear_pointer(&hs_cache, g_free);
}

static void hs_show_albums(WebKitWebView *web)
{
    GString *page = g_string_new(hs_style);
    g_string_append(page, "<h1>Headspace RMF Collections</h1><ul>");
    for (guint i = 0; i < G_N_ELEMENTS(hs_albums); i++)
        g_string_append_printf(page,
            "<li><a href='modulez-headspace:list/%u/0'>%s</a> · "
            "<a href='modulez-headspace:save/%u/0'>Save ZIP to Downloads</a></li>",
            i, hs_names[i], i);
    g_string_append(page, "</ul>");
    webkit_web_view_load_html(web, page->str, HS_BASE);
    g_string_free(page, TRUE);
}

typedef struct {
    guint album, track;
    enum { HS_LIST, HS_PLAY, HS_QUEUE, HS_SAVE } action;
} HsRequest;

static gboolean hs_get_zip(const gchar *zip, const gchar *name, GError **err)
{
    if (g_file_test(zip, G_FILE_TEST_IS_REGULAR)) return TRUE;
    g_autofree gchar *url = g_strconcat(
        "https://archive.org/download/headspace-rmf-collections/", name, NULL);
    g_autoptr(SoupSession) session = soup_session_new_with_options("timeout", 60, NULL);
    g_autoptr(SoupMessage) msg = soup_message_new("GET", url);
    g_autoptr(GInputStream) in = soup_session_send(session, msg, NULL, err);
    if (!in) return FALSE;
    guint status = soup_message_get_status(msg);
    if (status < 200 || status >= 300) {
        g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "Internet Archive returned HTTP %u.", status);
        return FALSE;
    }
    SoupMessageHeaders *headers = soup_message_get_response_headers(msg);
    goffset expected = soup_message_headers_get_content_length(headers);
    if (expected > 0)
        g_atomic_int_set(&hs_progress_percent, 0);
    g_autoptr(GByteArray) bytes = g_byte_array_new();
    guint8 buffer[32768];
    gssize n;
    while ((n = g_input_stream_read(in, buffer, sizeof buffer, NULL, err)) > 0) {
        if ((gsize)n > HS_LIMIT - bytes->len) {
            g_set_error_literal(err, G_IO_ERROR, G_IO_ERROR_FAILED, "Album exceeds 32 MiB.");
            return FALSE;
        }
        g_byte_array_append(bytes, buffer, n);
        if (expected > 0)
            g_atomic_int_set(&hs_progress_percent,
                             (gint)MIN((gint64)99, (gint64)bytes->len * 100 / expected));
    }
    if (n < 0) return FALSE;
    if (bytes->len < 4 || memcmp(bytes->data, "PK\003\004", 4)) {
        g_set_error_literal(err, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "Internet Archive did not return an album ZIP.");
        return FALSE;
    }
    if (!g_file_set_contents(zip, (const gchar *)bytes->data, bytes->len, err))
        return FALSE;
    g_atomic_int_set(&hs_progress_percent, 100);
    return TRUE;
}

/* Values 0–100 describe the download; 101–201 describe the local copy. */
static void hs_copy_progress(goffset copied, goffset total, gpointer data)
{
    (void)data;
    if (total > 0)
        g_atomic_int_set(&hs_progress_percent,
                         101 + (gint)MIN((goffset)100, copied * 100 / total));
}

static gchar *hs_album_action(HsRequest *r, GError **err)
{
    g_autofree gchar *name = g_strdup_printf("rmf-%s.zip", hs_albums[r->album]);
    g_autofree gchar *zip = g_build_filename(hs_cache, name, NULL);
    if (!hs_get_zip(zip, name, err)) return NULL;

    if (r->action == HS_SAVE) {
        const gchar *folder = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
        g_autofree gchar *fallback = g_build_filename(g_get_home_dir(), "Downloads", NULL);
        if (!folder) folder = fallback;
        if (g_mkdir_with_parents(folder, 0755) != 0) {
            g_set_error_literal(err, G_IO_ERROR, G_IO_ERROR_FAILED, "Cannot create Downloads folder.");
            return NULL;
        }
        g_autofree gchar *dest = g_build_filename(folder, name, NULL);
        g_autoptr(GFile) from = g_file_new_for_path(zip);
        g_autoptr(GFile) to = g_file_new_for_path(dest);
        g_atomic_int_set(&hs_progress_percent, 101);
        if (!g_file_copy(from, to, G_FILE_COPY_NONE, NULL, hs_copy_progress, NULL, err))
            return NULL;
        return g_strdup(dest);
    }

    struct archive *a = archive_read_new();
    archive_read_support_format_zip(a);
    GString *page = g_string_new(hs_style);
    g_string_append_printf(page,
        "<p><a href='modulez-headspace:home'>All albums</a></p>"
        "<h1>%s</h1><a href='modulez-headspace:save/%u/0'>Save ZIP to Downloads</a><ul>",
        hs_names[r->album], r->album);

    gchar *result = NULL;
    guint index = 0;
    int rc = archive_read_open_filename(a, zip, 32768);
    struct archive_entry *entry;
    if (rc == ARCHIVE_OK) {
        while ((rc = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
            const char *path = archive_entry_pathname(entry);
            if (!path || archive_entry_filetype(entry) != AE_IFREG) continue;
            const char *ext = strrchr(path, '.');
            if (!ext || g_ascii_strcasecmp(ext, ".rmf")) continue;
            guint current = index++;
            g_autofree gchar *base = g_path_get_basename(path);
            g_autofree gchar *display = g_filename_display_name(base);
            if (r->action == HS_LIST) {
                g_autofree gchar *label = g_markup_escape_text(display, -1);
                g_string_append_printf(page,
                    "<li>%s · <a href='modulez-headspace:play/%u/%u'>Play</a> · "
                    "<a href='modulez-headspace:queue/%u/%u'>Add to playlist</a></li>",
                    label, r->album, current, r->album, current);
                continue;
            }
            if (current != r->track) continue;

            g_autofree gchar *persistent = g_build_filename(g_get_user_data_dir(),
                "modulez-player", "downloads", "Headspace", hs_albums[r->album], NULL);
            const gchar *folder = r->action == HS_QUEUE ? persistent : hs_cache;
            if (g_mkdir_with_parents(folder, 0700) != 0) {
                g_set_error_literal(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                                    "Cannot create the RMF download folder.");
                break;
            }
            g_autofree gchar *leaf = g_strdup_printf("%s-%u-%s", hs_albums[r->album], current, base);
            g_autofree gchar *dest = g_build_filename(folder, leaf, NULL);
            g_autoptr(GByteArray) bytes = g_byte_array_new();
            guint8 buf[32768];
            la_ssize_t n;
            while ((n = archive_read_data(a, buf, sizeof buf)) > 0) {
                if ((gsize)n > HS_LIMIT - bytes->len) {
                    g_set_error_literal(err, G_IO_ERROR, G_IO_ERROR_FAILED, "RMF exceeds 32 MiB.");
                    break;
                }
                g_byte_array_append(bytes, buf, n);
            }
            if (n < 0) rc = ARCHIVE_FATAL;
            else if (!*err && bytes->len &&
                     g_file_set_contents(dest, (const gchar *)bytes->data, bytes->len, err))
                result = g_strdup(dest);
            break;
        }
    }
    if (rc != ARCHIVE_OK && rc != ARCHIVE_EOF) {
        if (!*err) g_set_error(err, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                               "Could not read album: %s", archive_error_string(a));
        g_unlink(zip);
    } else if (r->action == HS_LIST && index) {
        g_string_append(page, "</ul>");
        result = g_strdup(page->str);
    }
    archive_read_free(a);
    g_string_free(page, TRUE);
    if (!result && !*err)
        g_set_error_literal(err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No matching RMF in this album.");
    return result;
}

static void hs_worker(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source; (void)cancel;
    GError *err = NULL;
    gchar *result = hs_album_action(data, &err);
    if (result) g_task_return_pointer(task, result, g_free);
    else g_task_return_error(task, err);
}

static void hs_done(GObject *source, GAsyncResult *res, gpointer data)
{
    (void)data;
    hs_busy = FALSE;
    if (hs_progress_timer) {
        g_source_remove(hs_progress_timer);
        hs_progress_timer = 0;
    }
    HsRequest *r = g_task_get_task_data(G_TASK(res));
    g_autoptr(GError) err = NULL;
    g_autofree gchar *result = g_task_propagate_pointer(G_TASK(res), &err);
    hs_run_js(WEBKIT_WEB_VIEW(source), result && r->action == HS_SAVE
        ? "window.hsProgressDone && window.hsProgressDone(true)"
        : "window.hsProgressDone && window.hsProgressDone(false)");
    if (!result) { set_status(err->message); return; }
    if (r->action == HS_LIST)
        webkit_web_view_load_html(WEBKIT_WEB_VIEW(source), result, HS_BASE);
    else if (r->action == HS_PLAY)
        play_playlist_file(result);
    else if (r->action == HS_QUEUE) {
        append_playlist_path(result);
        set_status("Headspace track added to playlist");
    } else {
        g_autofree gchar *message = g_strdup_printf("Saved %s", result);
        set_status(message);
    }
}

static void hs_activate(WebKitWebView *web, const gchar *command)
{
    if (hs_busy) { set_status("Opening a Headspace album. Please wait."); return; }
    if (g_str_equal(command, "home")) { hs_show_albums(web); return; }
    g_auto(GStrv) parts = g_strsplit(command, "/", -1);
    if (g_strv_length(parts) != 3) return;
    guint64 album, track;
    if (!g_ascii_string_to_unsigned(parts[1], 10, 0, G_N_ELEMENTS(hs_albums)-1, &album, NULL) ||
        !g_ascii_string_to_unsigned(parts[2], 10, 0, G_MAXUINT, &track, NULL)) return;
    HsRequest request = { .album = album, .track = track };
    if (g_str_equal(parts[0], "list")) request.action = HS_LIST;
    else if (g_str_equal(parts[0], "play")) request.action = HS_PLAY;
    else if (g_str_equal(parts[0], "queue")) request.action = HS_QUEUE;
    else if (g_str_equal(parts[0], "save")) request.action = HS_SAVE;
    else return;
    if (!hs_cache) {
        g_autoptr(GError) err = NULL;
        hs_cache = g_dir_make_tmp("modulez-headspace-XXXXXX", &err);
        if (!hs_cache) { set_status(err->message); return; }
        atexit(hs_cleanup);
    }
    HsRequest *job = g_new(HsRequest, 1);
    *job = request;
    GTask *task = g_task_new(web, NULL, hs_done, NULL);
    g_task_set_task_data(task, job, g_free);
    hs_busy = TRUE;
    hs_last_percent = -2;
    g_atomic_int_set(&hs_progress_percent, -1);
    hs_progress_timer = g_timeout_add(120, hs_update_progress, web);
    set_status("Opening Headspace album…");
    g_task_run_in_thread(task, hs_worker);
    g_object_unref(task);
}

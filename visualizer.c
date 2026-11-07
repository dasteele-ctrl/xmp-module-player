#include "visualizer.h"
#include <webkit2/webkit2.h>
#include <libsoup/soup.h>
#include <string.h>
#include <glib/gstdio.h>

typedef struct {
    guint refs, timer;
    GtkWidget *page, *message, *fullscreen, *fullscreen_button;
    WebKitWebView *web;
    SoupServer *server;
    GCancellable *cancel;
    gchar *base;
    gboolean shown, ready, busy, destroyed, importing;
} Visualizer;
static Visualizer *active_visualizer;
static gboolean (*skip_track_callback)(gint);
void modulez_visualizer_set_track_navigation(gboolean (*skip_track)(gint))
{
    skip_track_callback = skip_track;
}
static void toggle_fullscreen(Visualizer *v);
static Visualizer *vis_ref(Visualizer *v) { v->refs++; return v; }
static void vis_unref(Visualizer *v)
{
    if (--v->refs) return;
    g_clear_object(&v->cancel);
    g_free(v->base);
    g_free(v);
}
static void discard_pcm(void)
{
    int16_t unused[MODULEZ_VIS_PCM_FRAMES * 2];
    for (int i = 0; i < 16 && modulez_vis_pcm_pop(unused); i++) {}
}
static void evaluate_done(GObject *object, GAsyncResult *result, gpointer data)
{
    Visualizer *v = data;
    GError *error = NULL;
    JSCValue *value = webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(object), result, &error);
    g_clear_object(&value);
    v->busy = FALSE;
    if (error && !v->destroyed && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        gtk_label_set_text(GTK_LABEL(v->message), error->message);
    g_clear_error(&error);
    vis_unref(v);
}
static void send_visible(Visualizer *v)
{
    if (!v->web || !v->ready || v->destroyed) return;
    const char *js = v->shown ? "window.modulezBridge?.setVisible(true)" :
                               "window.modulezBridge?.setVisible(false)";
    webkit_web_view_evaluate_javascript(v->web, js, -1, NULL, NULL, v->cancel, NULL, NULL);
}
static gboolean pump_pcm(gpointer data)
{
    Visualizer *v = data;
    int16_t chunk[MODULEZ_VIS_PCM_FRAMES * 2], latest[MODULEZ_VIS_PCM_FRAMES * 8];
    size_t count = 0;
    for (int i = 0; i < 16; i++) {
        size_t n = modulez_vis_pcm_pop(chunk);
        if (!n) break;
        if (count + n > MODULEZ_VIS_PCM_FRAMES * 4) {
            size_t drop = count + n - MODULEZ_VIS_PCM_FRAMES * 4;
            memmove(latest, latest + drop * 2, (count - drop) * 2 * sizeof(int16_t));
            count -= drop;
        }
        memcpy(latest + count * 2, chunk, n * 2 * sizeof(int16_t));
        count += n;
    }
    if (!count || !v->shown || !v->ready || v->busy || v->destroyed) return G_SOURCE_CONTINUE;
#if G_BYTE_ORDER == G_BIG_ENDIAN
    for (size_t i = 0; i < count * 2; i++) latest[i] = (int16_t)GUINT16_TO_LE((guint16)latest[i]);
#endif
    gchar *encoded = g_base64_encode((const guchar *)latest, count * 2 * sizeof(int16_t));
    gchar *script = g_strdup_printf("window.modulezBridge?.feed('%s')", encoded);
    v->busy = TRUE;
    webkit_web_view_evaluate_javascript(v->web, script, -1, NULL, NULL,
                                       v->cancel, evaluate_done, vis_ref(v));
    g_free(script);
    g_free(encoded);
    return G_SOURCE_CONTINUE;
}
static const char *resource_type(const char *name)
{
    if (g_str_has_suffix(name, ".html")) return "text/html; charset=utf-8";
    if (g_str_has_suffix(name, ".js")) return "text/javascript; charset=utf-8";
    return "application/wasm";
}
#define MILK_MAX_BYTES (1024 * 1024)
static gchar *preset_directory(void)
{
    return g_build_filename(g_get_user_config_dir(), "xmp-module-player", "milk-presets", NULL);
}
static gboolean preset_id_valid(const char *id)
{
    if (strlen(id) != 64) return FALSE;
    for (int i = 0; i < 64; i++) if (!g_ascii_isxdigit(id[i])) return FALSE;
    return TRUE;
}
static void append_json_string(GString *json, const char *text)
{
    g_string_append_c(json, '"');
    for (const guchar *p = (const guchar *)text; *p; p++) {
        if (*p == '"' || *p == '\\') { g_string_append_c(json, '\\'); g_string_append_c(json, *p); }
        else if (*p < 32) g_string_append_printf(json, "\\u%04x", *p);
        else g_string_append_c(json, *p);
    }
    g_string_append_c(json, '"');
}
static gchar *saved_presets_json(void)
{
    gchar *root = preset_directory();
    GDir *directory = g_dir_open(root, 0, NULL);
    GString *json = g_string_new("[");
    const char *id;
    gboolean first = TRUE;
    while (directory && (id = g_dir_read_name(directory))) {
        if (!preset_id_valid(id)) continue;
        gchar *file = g_build_filename(root, id, "preset.milk", NULL);
        if (!g_file_test(file, G_FILE_TEST_IS_REGULAR)) { g_free(file); continue; }
        gchar *label_file = g_build_filename(root, id, "name.txt", NULL), *label = NULL;
        gsize length = 0;
        if (!g_file_get_contents(label_file, &label, &length, NULL) ||
            length > 4096 || !g_utf8_validate(label, length, NULL) || memchr(label, 0, length)) {
            g_free(label); label = g_strdup(id);
        }
        if (!first) g_string_append_c(json, ',');
        first = FALSE;
        g_string_append(json, "{\"id\":"); append_json_string(json, id);
        g_string_append(json, ",\"name\":"); append_json_string(json, label);
        g_string_append_c(json, '}');
        g_free(label); g_free(label_file); g_free(file);
    }
    if (directory) g_dir_close(directory);
    g_free(root);
    g_string_append_c(json, ']');
    return g_string_free(json, FALSE);
}
/* Return 1 for newly saved, 0 for a duplicate and -1 for a rejected file. */
static int save_milk_preset(const char *source)
{
    GStatBuf st;
    if (g_stat(source, &st) || st.st_size < 1 || st.st_size > MILK_MAX_BYTES) return -1;
    gchar *text = NULL;
    gsize length = 0;
    if (!g_file_get_contents(source, &text, &length, NULL)) return -1;
    if (!length || length > MILK_MAX_BYTES || memchr(text, 0, length) ||
        !g_utf8_validate(text, length, NULL) || !g_strstr_len(text, length, "[preset")) {
        g_free(text); return -1;
    }
    gchar *hash = g_compute_checksum_for_data(G_CHECKSUM_SHA256, (const guchar *)text, length);
    gchar *root = preset_directory(), *directory = g_build_filename(root, hash, NULL);
    gchar *file = g_build_filename(directory, "preset.milk", NULL);
    int result = -1;
    if (g_file_test(file, G_FILE_TEST_IS_REGULAR)) result = 0;
    else if (!g_mkdir_with_parents(directory, 0700)) {
        gchar *label = g_filename_display_basename(source);
        gchar *name = g_build_filename(directory, "name.txt", NULL);
        /* Write the label first; only a complete preset makes an entry visible. */
        if (g_file_set_contents(name, label, -1, NULL) &&
            g_file_set_contents(file, text, length, NULL)) result = 1;
        g_free(name); g_free(label);
    }
    g_free(file); g_free(directory); g_free(root); g_free(hash); g_free(text);
    return result;
}
/* WebKit's default select popup can inherit desktop theme colors. Use the
 * same GtkMenu nodes and application CSS as the player's native menus. */
static void option_activated(GtkMenuItem *item, gpointer data)
{
    guint index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "option-index")) - 1;
    webkit_option_menu_activate_item(WEBKIT_OPTION_MENU(data), index);
}
static void option_closed(GtkWidget *widget, gpointer data)
{
    (void)widget;
    webkit_option_menu_close(WEBKIT_OPTION_MENU(data));
}
static gboolean show_options(WebKitWebView *web, WebKitOptionMenu *options,
                             GdkEvent *event, GdkRectangle *rectangle, gpointer data)
{
    (void)data;
    GtkWidget *menu = gtk_menu_new();
    g_object_ref_sink(menu);
    g_object_set_data_full(G_OBJECT(menu), "webkit-options", g_object_ref(options), g_object_unref);
    GtkWidget *selected = NULL;
    for (guint i = 0; i < webkit_option_menu_get_n_items(options); i++) {
        WebKitOptionMenuItem *option = webkit_option_menu_get_item(options, i);
        GtkWidget *item = gtk_menu_item_new_with_label(webkit_option_menu_item_get_label(option));
        gtk_widget_set_sensitive(item, webkit_option_menu_item_is_enabled(option) &&
                                      !webkit_option_menu_item_is_group_label(option));
        g_object_set_data(G_OBJECT(item), "option-index", GUINT_TO_POINTER(i + 1));
        g_signal_connect(item, "activate", G_CALLBACK(option_activated), options);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        if (webkit_option_menu_item_is_selected(option)) selected = item;
    }
    g_signal_connect(menu, "destroy", G_CALLBACK(option_closed), options);
    g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
    g_signal_connect_object(web, "destroy", G_CALLBACK(gtk_widget_destroy), menu, G_CONNECT_SWAPPED);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_rect(GTK_MENU(menu), gtk_widget_get_window(GTK_WIDGET(web)), rectangle,
                           GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, event);
    if (selected) gtk_menu_shell_select_item(GTK_MENU_SHELL(menu), selected);
    g_signal_connect_swapped(menu, "destroy", G_CALLBACK(g_object_unref), menu);
    return TRUE;
}

static void import_presets(Visualizer *v)
{
    if (v->importing || v->destroyed) return;
    v->importing = TRUE;
    vis_ref(v);
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Import MilkDrop presets", GTK_WINDOW(gtk_widget_get_toplevel(v->page)),
        GTK_FILE_CHOOSER_ACTION_OPEN, "Cancel", GTK_RESPONSE_CANCEL, "Import", GTK_RESPONSE_ACCEPT, NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(dialog), "xmp-dialog");
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    if (content) gtk_style_context_add_class(gtk_widget_get_style_context(content), "xmp-dialog-surface");
    /* The action buttons are outside the filechooser CSS node. */
    GtkWidget *actions = gtk_widget_get_parent(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT));
    if (actions) gtk_style_context_add_class(gtk_widget_get_style_context(actions), "xmp-dialog-surface");
    g_object_ref_sink(dialog);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "MilkDrop presets (*.milk)");
    gtk_file_filter_add_pattern(filter, "*.milk"); gtk_file_filter_add_pattern(filter, "*.MILK");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    int imported = 0, duplicates = 0, rejected = 0;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT && !v->destroyed) {
        GSList *files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));
        for (GSList *item = files; item; item = item->next) {
            int result = save_milk_preset(item->data);
            if (result > 0) imported++; else if (!result) duplicates++; else rejected++;
        }
        g_slist_free_full(files, g_free);
        gchar *message = g_strdup_printf("Presets: %d imported, %d already saved, %d rejected", imported, duplicates, rejected);
        gtk_label_set_text(GTK_LABEL(v->message), message); g_free(message);
        webkit_web_view_evaluate_javascript(v->web, "window.modulezPresetLibrary?.refresh()", -1,
                                            NULL, NULL, v->cancel, NULL, NULL);
    }
    gtk_widget_destroy(dialog); g_object_unref(dialog);
    v->importing = FALSE;
    vis_unref(v);
}
static gboolean serve_saved_presets(SoupServerMessage *message, const char *name)
{
    if (!strcmp(name, "user-presets.json")) {
        gchar *json = saved_presets_json();
        soup_server_message_set_response(message, "application/json; charset=utf-8", SOUP_MEMORY_COPY, json, strlen(json));
        soup_server_message_set_status(message, SOUP_STATUS_OK, NULL); g_free(json); return TRUE;
    }
    if (!g_str_has_prefix(name, "user-presets/")) return FALSE;
    const char *tail = name + strlen("user-presets/");
    gchar *id = g_strndup(tail, 64);
    gboolean valid = preset_id_valid(id) && !strcmp(tail + strlen(id), ".milk");
    gchar *root = preset_directory(), *path = g_build_filename(root, id, "preset.milk", NULL), *text = NULL;
    gsize length = 0;
    GStatBuf st;
    if (valid && !g_stat(path, &st) && st.st_size > 0 && st.st_size <= MILK_MAX_BYTES &&
        g_file_get_contents(path, &text, &length, NULL) && length <= MILK_MAX_BYTES) {
        soup_server_message_set_response(message, "text/plain; charset=utf-8", SOUP_MEMORY_COPY, text, length);
        soup_server_message_set_status(message, SOUP_STATUS_OK, NULL);
    } else soup_server_message_set_status(message, SOUP_STATUS_NOT_FOUND, NULL);
    g_free(text); g_free(path); g_free(root); g_free(id);
    return TRUE;
}

static void serve_asset(SoupServer *server, SoupServerMessage *message,
                        const char *path, GHashTable *query, gpointer data)
{
    (void)server; (void)query;
    Visualizer *v = data;
    const char *token = strstr(v->base, "://");
    token = token ? strchr(token + 3, '/') : NULL;
    if (!token || !g_str_has_prefix(path, token) ||
        strcmp(soup_server_message_get_method(message), "GET")) {
        soup_server_message_set_status(message, SOUP_STATUS_NOT_FOUND, NULL); return;
    }
    const char *name = path + strlen(token);
    soup_message_headers_replace(soup_server_message_get_response_headers(message), "Cache-Control", "no-store");
    if (serve_saved_presets(message, name)) return;
    const char *allowed[] = {"index.html", "viewer.js", "wavescope.js", "presets.js",
        "projectm-adapter.js", "projectm/projectm.js", "projectm/projectm.wasm", NULL};
    gboolean found = FALSE;
    for (int i = 0; allowed[i]; i++) if (!strcmp(name, allowed[i])) { found = TRUE; break; }
    if (!found) { soup_server_message_set_status(message, SOUP_STATUS_NOT_FOUND, NULL); return; }
    gchar *resource = g_strconcat("/org/modulez/visualizer/", name, NULL);
    GBytes *bytes = g_resources_lookup_data(resource, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
    g_free(resource);
    if (!bytes) { soup_server_message_set_status(message, SOUP_STATUS_NOT_FOUND, NULL); return; }
    gsize length;
    const void *body = g_bytes_get_data(bytes, &length);
    soup_server_message_set_response(message, resource_type(name), SOUP_MEMORY_COPY, body, length);
    soup_message_headers_replace(soup_server_message_get_response_headers(message), "Cache-Control", "no-store");
    soup_server_message_set_status(message, SOUP_STATUS_OK, NULL);
    g_bytes_unref(bytes);
}
static gboolean permission_request(WebKitWebView *web, WebKitPermissionRequest *request, gpointer data)
{
    (void)web; (void)data;
    webkit_permission_request_deny(request);
    return TRUE;
}
static gboolean navigation_policy(WebKitWebView *web, WebKitPolicyDecision *decision,
                                   WebKitPolicyDecisionType type, gpointer data)
{
    (void)web;
    Visualizer *v = data;
    if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION || type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
        WebKitNavigationAction *action = webkit_navigation_policy_decision_get_navigation_action(WEBKIT_NAVIGATION_POLICY_DECISION(decision));
        WebKitURIRequest *request = webkit_navigation_action_get_request(action);
        const char *uri = webkit_uri_request_get_uri(request);
        if (!strcmp(uri, "https://github.com/projectM-visualizer/presets-cream-of-the-crop") ||
            !strcmp(uri, "https://github.com/projectM-visualizer/presets-projectm-classic")) {
            /* Open only these two explicit collection links in the user's browser. */
            webkit_policy_decision_ignore(decision);
            GError *error = NULL;
            if (!gtk_show_uri_on_window(GTK_WINDOW(gtk_widget_get_toplevel(v->page)),
                                         uri, GDK_CURRENT_TIME, &error)) {
                gtk_label_set_text(GTK_LABEL(v->message), error->message);
                g_clear_error(&error);
            }
            return TRUE;
        }
        if (!g_str_has_prefix(uri, v->base)) {
            webkit_policy_decision_ignore(decision); return TRUE;
        }
    }
    return FALSE;
}
static void script_message(WebKitUserContentManager *manager, WebKitJavascriptResult *result, gpointer data)
{
    (void)manager;
    Visualizer *v = data;
    gchar *text = jsc_value_to_string(webkit_javascript_result_get_js_value(result));
    if (!strcmp(text, "import-milk-presets")) {
        import_presets(v);
    } else if (!strcmp(text, "toggle-fullscreen")) {
        toggle_fullscreen(v);
    } else if (!strcmp(text, "exit-fullscreen")) {
        if (v->fullscreen) toggle_fullscreen(v);
    } else if (!strcmp(text, "previous-song")) {
        if (skip_track_callback) skip_track_callback(-1);
    } else if (!strcmp(text, "next-song")) {
        if (skip_track_callback) skip_track_callback(1);
    } else if (g_str_has_prefix(text, "Local Wavescope ready")) {
        v->ready = TRUE;
        send_visible(v);
        modulez_vis_pcm_enable(v->shown);
        gtk_label_set_text(GTK_LABEL(v->message), "Wavescope • direct player audio");
    } else if (strstr(text, "failed") || strstr(text, "failure") || strstr(text, "error") || strstr(text, "unavailable")) {
        gtk_label_set_text(GTK_LABEL(v->message), text);
    }
    g_free(text);
}
static void load_changed(WebKitWebView *web, WebKitLoadEvent event, gpointer data)
{
    (void)web;
    Visualizer *v = data;
    if (event == WEBKIT_LOAD_STARTED) { v->ready = FALSE; modulez_vis_pcm_enable(FALSE); }
}
static gboolean load_failed(WebKitWebView *web, WebKitLoadEvent event, const char *uri,
                             GError *error, gpointer data)
{
    (void)web; (void)event; (void)uri;
    Visualizer *v = data;
    v->ready = FALSE;
    modulez_vis_pcm_enable(FALSE);
    gtk_label_set_text(GTK_LABEL(v->message), error->message);
    return FALSE;
}
static void process_terminated(WebKitWebView *web, WebKitWebProcessTerminationReason reason, gpointer data)
{
    (void)web; (void)reason;
    Visualizer *v = data;
    v->ready = FALSE;
    modulez_vis_pcm_enable(FALSE);
    gtk_label_set_text(GTK_LABEL(v->message), "Visualizer stopped. Click Reload Vis to restart it.");
}
static void reload_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    Visualizer *v = data;
    if (v->web) { v->ready = FALSE; modulez_vis_pcm_enable(FALSE); webkit_web_view_reload(v->web); }
}
static gboolean initialize_view(Visualizer *v)
{
    v->server = soup_server_new(NULL, NULL);
    GError *error = NULL;
    if (!soup_server_listen_local(v->server, 0, SOUP_SERVER_LISTEN_IPV4_ONLY, &error)) {
        gtk_label_set_text(GTK_LABEL(v->message), error->message);
        g_clear_error(&error); g_clear_object(&v->server); return FALSE;
    }
    GSList *uris = soup_server_get_uris(v->server);
    int port = g_uri_get_port(uris->data);
    g_slist_free_full(uris, (GDestroyNotify)g_uri_unref);
    gchar *token = g_uuid_string_random();
    v->base = g_strdup_printf("http://127.0.0.1:%d/%s/", port, token);
    g_free(token);
    soup_server_add_handler(v->server, NULL, serve_asset, v, NULL);
    WebKitWebContext *context = webkit_web_context_new_ephemeral();
    v->web = WEBKIT_WEB_VIEW(webkit_web_view_new_with_context(context));
    g_object_ref_sink(v->web);
    g_object_unref(context);
    WebKitSettings *settings = webkit_web_view_get_settings(v->web);
    webkit_settings_set_enable_webgl(settings, TRUE);
    webkit_settings_set_enable_media_stream(settings, FALSE);
    WebKitUserContentManager *manager = webkit_web_view_get_user_content_manager(v->web);
    webkit_user_content_manager_register_script_message_handler(manager, "modulez");
    g_signal_connect(manager, "script-message-received::modulez", G_CALLBACK(script_message), v);
    g_signal_connect(v->web, "show-option-menu", G_CALLBACK(show_options), NULL);
    g_signal_connect(v->web, "permission-request", G_CALLBACK(permission_request), v);
    g_signal_connect(v->web, "decide-policy", G_CALLBACK(navigation_policy), v);
    g_signal_connect(v->web, "load-changed", G_CALLBACK(load_changed), v);
    g_signal_connect(v->web, "load-failed", G_CALLBACK(load_failed), v);
    g_signal_connect(v->web, "web-process-terminated", G_CALLBACK(process_terminated), v);
    GdkRGBA background = {7.0 / 255, 18.0 / 255, 24.0 / 255, 1};
    webkit_web_view_set_background_color(v->web, &background);
    gtk_box_pack_start(GTK_BOX(v->page), GTK_WIDGET(v->web), TRUE, TRUE, 0);
    gtk_widget_show(GTK_WIDGET(v->web));
    gchar *url = g_strconcat(v->base, "index.html", NULL);
    webkit_web_view_load_uri(v->web, url);
    g_free(url);
    v->timer = g_timeout_add(33, pump_pcm, v);
    return TRUE;
}
static void page_mapped(GtkWidget *page, gpointer data)
{
    (void)page;
    Visualizer *v = data;
    v->shown = TRUE;
    discard_pcm();
    if (!v->web) initialize_view(v);
    send_visible(v);
    modulez_vis_pcm_enable(v->ready);
}
static void page_unmapped(GtkWidget *page, gpointer data)
{
    (void)page;
    Visualizer *v = data;
    v->shown = FALSE;
    modulez_vis_pcm_enable(FALSE);
    discard_pcm();
    send_visible(v);
}
static gboolean fullscreen_key(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    (void)widget;
    Visualizer *v = data;
    if (event->keyval == GDK_KEY_Escape || event->keyval == GDK_KEY_F11) {
        if (v->fullscreen) {
            toggle_fullscreen(v);
            return TRUE;
        }
    }
    return FALSE;
}
static gboolean fullscreen_delete(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    (void)widget; (void)event;
    Visualizer *v = data;
    if (v->fullscreen) toggle_fullscreen(v);
    return TRUE;
}
static void toggle_fullscreen(Visualizer *v)
{
    if (!v || v->destroyed || !v->web) return;
    GtkWidget *view = GTK_WIDGET(v->web);
    GtkWidget *old_parent = gtk_widget_get_parent(view);
    if (!old_parent) return;
    /* Retain the same WebKit instance: its JS state and current preset survive. */
    g_object_ref(view);
    gtk_container_remove(GTK_CONTAINER(old_parent), view);
    if (v->fullscreen) {
        GtkWidget *fullscreen = v->fullscreen;
        v->fullscreen = NULL;
        gtk_box_pack_start(GTK_BOX(v->page), view, TRUE, TRUE, 0);
        gtk_widget_show(view);
        gtk_widget_destroy(fullscreen);
        gtk_button_set_label(GTK_BUTTON(v->fullscreen_button), "Fullscreen Vis");
    } else {
        GtkWidget *fullscreen = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        v->fullscreen = fullscreen;
        gtk_window_set_title(GTK_WINDOW(fullscreen), "Modulez Player — Visualizer");
        gtk_window_set_default_size(GTK_WINDOW(fullscreen), 960, 600);
        g_signal_connect(fullscreen, "delete-event", G_CALLBACK(fullscreen_delete), v);
        g_signal_connect(fullscreen, "key-press-event", G_CALLBACK(fullscreen_key), v);
        gtk_container_add(GTK_CONTAINER(fullscreen), view);
        gtk_widget_show_all(fullscreen);
        gtk_window_fullscreen(GTK_WINDOW(fullscreen));
        gtk_button_set_label(GTK_BUTTON(v->fullscreen_button), "Exit Fullscreen");
    }
    g_object_unref(view);
    /* Hide the web toolbar and expose the current mode to context-menu JS. */
    webkit_web_view_evaluate_javascript(v->web,
        v->fullscreen ? "document.querySelector('header').hidden=true;document.body.dataset.fullscreen='true'" :
                        "document.querySelector('header').hidden=false;document.body.dataset.fullscreen='false'",
        -1, NULL, NULL, v->cancel, NULL, NULL);
}
gboolean modulez_visualizer_toggle_fullscreen(void)
{
    Visualizer *v = active_visualizer;
    if (!v || v->destroyed || !v->web) return FALSE;
    if (!v->fullscreen && !gtk_widget_get_mapped(v->page)) return FALSE;
    toggle_fullscreen(v);
    return TRUE;
}
static void fullscreen_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    toggle_fullscreen(data);
}
static void page_destroyed(GtkWidget *page, gpointer data)
{
    Visualizer *v = data;
    if (v->fullscreen) {
        GtkWidget *fullscreen = v->fullscreen;
        v->fullscreen = NULL;
        gtk_widget_destroy(fullscreen);
    }
    if (active_visualizer == v) active_visualizer = NULL;
    g_signal_handlers_disconnect_by_data(page, v);
    v->destroyed = TRUE;
    modulez_vis_pcm_enable(FALSE);
    if (v->timer) g_source_remove(v->timer);
    g_cancellable_cancel(v->cancel);
    if (v->web) {
        g_signal_handlers_disconnect_by_data(webkit_web_view_get_user_content_manager(v->web), v);
        g_signal_handlers_disconnect_by_data(v->web, v);
        g_clear_object(&v->web);
    }
    if (v->server) { soup_server_disconnect(v->server); g_clear_object(&v->server); }
    vis_unref(v);
}
GtkWidget *modulez_visualizer_page_new(void)
{
    Visualizer *v = g_new0(Visualizer, 1);
    active_visualizer = v;
    v->refs = 1;
    v->cancel = g_cancellable_new();
    v->page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    v->message = gtk_label_new("Wavescope • direct player audio");
    gtk_label_set_xalign(GTK_LABEL(v->message), 0);
    gtk_label_set_ellipsize(GTK_LABEL(v->message), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(bar), v->message, TRUE, TRUE, 6);
    GtkWidget *reload = gtk_button_new_with_label("Reload Vis");
    g_signal_connect(reload, "clicked", G_CALLBACK(reload_clicked), v);
    gtk_box_pack_end(GTK_BOX(bar), reload, FALSE, FALSE, 6);
    v->fullscreen_button = gtk_button_new_with_label("Fullscreen Vis");
    g_signal_connect(v->fullscreen_button, "clicked", G_CALLBACK(fullscreen_clicked), v);
    gtk_box_pack_end(GTK_BOX(bar), v->fullscreen_button, FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(v->page), bar, FALSE, FALSE, 4);
    GtkWidget *hint = gtk_label_new("Tip: Right-click the visualization for modes, song controls, and fullscreen (F11).");
    gtk_label_set_xalign(GTK_LABEL(hint), 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(hint), "dim-label");
    gtk_box_pack_start(GTK_BOX(v->page), hint, FALSE, FALSE, 2);
    g_signal_connect(v->page, "map", G_CALLBACK(page_mapped), v);
    g_signal_connect(v->page, "unmap", G_CALLBACK(page_unmapped), v);
    g_signal_connect(v->page, "destroy", G_CALLBACK(page_destroyed), v);
    return v->page;
}

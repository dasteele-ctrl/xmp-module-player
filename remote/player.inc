/* Included after the tray callbacks so local and remote actions share the GTK
 * main loop, audio backends, playlist selection and volume handling. */
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/random.h>

#define REMOTE_PORT 48788
static SoupServer *remote_server;
static GtkWidget *remote_button;
static char remote_secret[7], remote_token[65];
static gint64 pair_deadline;
static guint pair_attempts;
static guint playlist_revision = 1;

static void remote_queue_changed(void)
{
    ++playlist_revision;
}

static gboolean random_hex(char *out, size_t bytes)
{
    const char digits[] = "0123456789abcdef";
    unsigned char data[32];
    if (bytes > sizeof(data) || getrandom(data, bytes, 0) != (ssize_t)bytes)
        return FALSE;
    for (size_t i = 0; i < bytes; ++i) {
        out[2*i] = digits[data[i] >> 4];
        out[2*i+1] = digits[data[i] & 15];
    }
    out[2*bytes] = 0;
    return TRUE;
}

static void remote_json(SoupServerMessage *msg, JsonNode *node)
{
    gchar *body = json_to_string(node, FALSE);
    soup_server_message_set_response(msg, "application/json", SOUP_MEMORY_TAKE, body, strlen(body));
    soup_server_message_set_status(msg, 200, NULL);
    json_node_unref(node);
}

static void remote_request(SoupServer *server, SoupServerMessage *msg,
                           const char *path, GHashTable *query, gpointer data)
{
    (void)server; (void)query; (void)data;
    SoupMessageHeaders *headers = soup_server_message_get_request_headers(msg);
    const char *method = soup_server_message_get_method(msg);
    soup_message_headers_append(soup_server_message_get_response_headers(msg), "Cache-Control", "no-store");
    if (!strcmp(path, "/pair") && !strcmp(method, "POST")) {
        if (!remote_secret[0] || pair_attempts >= 5 || g_get_monotonic_time() > pair_deadline) {
            soup_server_message_set_status(msg, 410, NULL);
            return;
        }
        if (g_strcmp0(soup_message_headers_get_one(headers, "X-Pair-Code"), remote_secret)) {
            ++pair_attempts;
            soup_server_message_set_status(msg, 403, NULL);
            return;
        }
        if (!random_hex(remote_token, 32)) {
            soup_server_message_set_status(msg, 500, NULL);
            return;
        }
        remote_secret[0] = 0;
        gtk_button_set_label(GTK_BUTTON(remote_button), "Remote connected");
        JsonNode *reply = json_node_new(JSON_NODE_OBJECT);
        JsonObject *obj = json_object_new();
        json_object_set_string_member(obj, "token", remote_token);
        json_node_take_object(reply, obj);
        remote_json(msg, reply);
        return;
    }
    if (!remote_token[0] || g_strcmp0(soup_message_headers_get_one(headers, "X-Remote-Token"), remote_token)) {
        soup_server_message_set_status(msg, 401, NULL);
        return;
    }
    if (!strcmp(method, "GET") && !strcmp(path, "/state")) {
        JsonObject *obj = json_object_new();
        json_object_set_string_member(obj, "title", gtk_label_get_text(GTK_LABEL(title_label)));
        json_object_set_string_member(obj, "format", gtk_label_get_text(GTK_LABEL(format_label)));
        json_object_set_boolean_member(obj, "playing", playing);
        json_object_set_boolean_member(obj, "radio", playback_mode == PLAYBACK_RADIO);
        json_object_set_int_member(obj, "volume", master_volume);
        json_object_set_int_member(obj, "revision", playlist_revision);
        JsonNode *reply = json_node_new(JSON_NODE_OBJECT);
        json_node_take_object(reply, obj);
        remote_json(msg, reply);
        return;
    }
    if (!strcmp(method, "GET") && !strcmp(path, "/playlist")) {
        GtkTreeModel *model = GTK_TREE_MODEL(playlist_store);
        GtkTreeIter iter;
        JsonArray *rows = json_array_new();
        for (gboolean more = gtk_tree_model_get_iter_first(model, &iter); more;
             more = gtk_tree_model_iter_next(model, &iter)) {
            gchar *label = NULL;
            gtk_tree_model_get(model, &iter, 0, &label, -1);
            json_array_add_string_element(rows, label ? label : "");
            g_free(label);
        }
        JsonObject *obj = json_object_new();
        json_object_set_array_member(obj, "tracks", rows);
        json_object_set_int_member(obj, "revision", playlist_revision);
        JsonNode *reply = json_node_new(JSON_NODE_OBJECT);
        json_node_take_object(reply, obj);
        remote_json(msg, reply);
        return;
    }
    if (strcmp(method, "POST")) {
        soup_server_message_set_status(msg, 405, NULL);
        return;
    }
    if (!strcmp(path, "/toggle")) on_tray_play_pause(NULL);
    else if (!strcmp(path, "/previous")) on_tray_previous(NULL);
    else if (!strcmp(path, "/next")) on_tray_next(NULL);
    else if (!strcmp(path, "/volume") || !strcmp(path, "/select")) {
        gboolean volume = !strcmp(path, "/volume");
        const char *value = soup_message_headers_get_one(headers, "X-Value");
        guint64 n;
        if (!value || !g_ascii_string_to_unsigned(value, 10, 0, volume ? 100 : G_MAXINT, &n, NULL)) {
            soup_server_message_set_status(msg, 400, NULL);
            return;
        }
        if (volume) gtk_range_set_value(GTK_RANGE(volume_scale), n);
        else {
            gchar revision[16];
            g_snprintf(revision, sizeof(revision), "%u", playlist_revision);
            if (g_strcmp0(revision, soup_message_headers_get_one(headers, "X-Revision")) ||
                !playlist_play_index(n)) {
                soup_server_message_set_status(msg, 409, NULL);
                return;
            }
        }
    } else {
        soup_server_message_set_status(msg, 404, NULL);
        return;
    }
    soup_server_message_set_status(msg, 204, NULL);
}

static void remote_stop(void)
{
    if (remote_server) soup_server_disconnect(remote_server);
    g_clear_object(&remote_server);
    remote_secret[0] = remote_token[0] = 0;
}

static GTlsCertificate *remote_certificate(GError **error)
{
    gchar *dir = g_dir_make_tmp("modulez-remote-XXXXXX", error);
    if (!dir) return NULL;
    gchar *key = g_build_filename(dir, "key.pem", NULL);
    gchar *crt = g_build_filename(dir, "cert.pem", NULL);
    gchar *argv[] = {"/usr/bin/openssl", "req", "-x509", "-newkey", "ec",
        "-pkeyopt", "ec_paramgen_curve:P-256", "-nodes", "-days", "2",
        "-subj", "/CN=Modulez Remote", "-keyout", key, "-out", crt, NULL};
    gint status;
    GTlsCertificate *cert = NULL;
    /* Run Mint's openssl with Mint's libraries outside the AppImage. */
    gchar *env[] = {"PATH=/usr/bin:/bin", NULL};
    if (g_spawn_sync(NULL, argv, env, G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                     NULL, NULL, NULL, NULL, &status, error) && g_spawn_check_wait_status(status, error))
        cert = g_tls_certificate_new_from_files(crt, key, error);
    g_unlink(key); g_unlink(crt); g_rmdir(dir);
    g_free(key); g_free(crt); g_free(dir);
    return cert;
}

static void remote_dialog(GtkButton *button, gpointer data)
{
    (void)data;
    if (remote_server) {
        remote_stop();
        gtk_button_set_label(button, "Pair remote");
        set_status("Remote disconnected");
        return;
    }
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Pair Android remote", GTK_WINDOW(window),
        GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel", GTK_RESPONSE_CANCEL, "Start pairing", GTK_RESPONSE_OK, NULL);
    theme_dialog(dialog);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(box), 14);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Choose the Dell’s home-network address"), FALSE, FALSE, 8);
    GtkWidget *combo = gtk_combo_box_text_new_with_entry();
    struct ifaddrs *all = NULL;
    if (getifaddrs(&all) == 0) {
        for (struct ifaddrs *it = all; it; it = it->ifa_next) {
            if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET ||
                !(it->ifa_flags & IFF_UP) || (it->ifa_flags & IFF_LOOPBACK)) continue;
            char ip[INET_ADDRSTRLEN];
            if (inet_ntop(AF_INET, &((struct sockaddr_in *)it->ifa_addr)->sin_addr, ip, sizeof(ip)))
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), ip);
        }
        freeifaddrs(all);
    }
    gtk_box_pack_start(GTK_BOX(box), combo, FALSE, FALSE, 4);
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    gtk_entry_set_placeholder_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(combo))), "Dell IPv4 address");
    gtk_widget_show_all(dialog);
    int answer = gtk_dialog_run(GTK_DIALOG(dialog));
    gchar *ip = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
    gtk_widget_destroy(dialog);
    if (answer != GTK_RESPONSE_OK || !ip) { g_free(ip); return; }
    g_strstrip(ip);
    struct in_addr parsed;
    if (inet_pton(AF_INET, ip, &parsed) != 1 || parsed.s_addr == INADDR_ANY) {
        set_status("Enter the IPv4 address of the Dell on your home network");
        g_free(ip); return;
    }
    GError *error = NULL;
    GTlsCertificate *cert = remote_certificate(&error);
    if (cert) {
        remote_server = soup_server_new("tls-certificate", cert, NULL);
        GInetAddress *inet = g_inet_address_new_from_string(ip);
        GSocketAddress *address = g_inet_socket_address_new(inet, REMOTE_PORT);
        soup_server_add_handler(remote_server, NULL, remote_request, NULL, NULL);
        soup_server_listen(remote_server, address, SOUP_SERVER_LISTEN_HTTPS, &error);
        g_object_unref(address); g_object_unref(inet);
    }
    if (error || !random_hex(remote_secret, 3)) {
        set_status(error ? error->message : "Could not generate a pairing code");
        g_clear_error(&error);
        remote_stop();
    } else {
        GByteArray *der = NULL;
        g_object_get(cert, "certificate", &der, NULL);
        gchar *hash = g_compute_checksum_for_data(G_CHECKSUM_SHA256, der->data, der->len);
        pair_deadline = g_get_monotonic_time() + 300 * G_USEC_PER_SEC;
        pair_attempts = 0;
        gtk_button_set_label(button, "Stop remote");
        dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "Pair Modulez Remote");
        theme_dialog(dialog);
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
            "On your Android phone, enter\n\nDell address   %s\nPairing code   %.8s-%s\n\nThe code is valid for five minutes and one phone.\nClick the remote button again to disconnect.", ip, hash, remote_secret);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        g_free(hash); g_byte_array_unref(der);
    }
    g_clear_object(&cert); g_free(ip);
}

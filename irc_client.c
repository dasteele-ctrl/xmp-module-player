#include "irc_client.h"
#include "irc_protocol.h"

#include <string.h>

struct IrcClient {
    gint refs;
    guint generation;
    gboolean connected;
    gboolean connecting;
    GSocketClient *socket_client;
    GSocketConnection *socket;
    GDataInputStream *input;
    GCancellable *cancel;
    GQueue *outgoing; /* GBytes*, owned */
    gboolean writing;
    IrcLineCallback on_line;
    IrcStateCallback on_state;
    gpointer user_data;
    gchar *nick;
};

typedef struct {
    IrcClient *client;
    guint generation;
    GBytes *bytes; /* only used by writes */
} AsyncContext;

static IrcClient *irc_client_ref(IrcClient *client)
{
    g_atomic_int_inc(&client->refs);
    return client;
}

void irc_client_unref(IrcClient *client)
{
    if (!client || !g_atomic_int_dec_and_test(&client->refs))
        return;
    g_clear_object(&client->socket);
    g_clear_object(&client->input);
    g_clear_object(&client->socket_client);
    g_clear_object(&client->cancel);
    g_queue_free_full(client->outgoing, (GDestroyNotify)g_bytes_unref);
    g_free(client->nick);
    g_free(client);
}

static AsyncContext *context_new(IrcClient *client)
{
    AsyncContext *ctx = g_new0(AsyncContext, 1);
    ctx->client = irc_client_ref(client);
    ctx->generation = client->generation;
    return ctx;
}

static void context_free(AsyncContext *ctx)
{
    if (ctx->bytes)
        g_bytes_unref(ctx->bytes);
    irc_client_unref(ctx->client);
    g_free(ctx);
}

IrcClient *irc_client_new(IrcLineCallback on_line, IrcStateCallback on_state,
                          gpointer user_data)
{
    IrcClient *client = g_new0(IrcClient, 1);
    client->refs = 1;
    client->outgoing = g_queue_new();
    client->on_line = on_line;
    client->on_state = on_state;
    client->user_data = user_data;
    return client;
}

void irc_client_clear_callbacks(IrcClient *client)
{
    client->on_line = NULL;
    client->on_state = NULL;
    client->user_data = NULL;
}

gboolean irc_client_connected(IrcClient *client)
{
    return client && client->connected;
}

static void drop_connection(IrcClient *client, const char *message)
{
    gboolean had_connection = client->connected || client->connecting;
    ++client->generation;
    if (client->cancel)
        g_cancellable_cancel(client->cancel);
    if (client->socket)
        g_io_stream_close(G_IO_STREAM(client->socket), NULL, NULL);
    g_clear_object(&client->input);
    g_clear_object(&client->socket);
    g_clear_object(&client->socket_client);
    g_clear_object(&client->cancel);
    g_queue_clear_full(client->outgoing, (GDestroyNotify)g_bytes_unref);
    client->writing = FALSE;
    client->connected = FALSE;
    client->connecting = FALSE;
    if (had_connection && client->on_state)
        client->on_state(FALSE, message, client->user_data);
}

void irc_client_disconnect(IrcClient *client)
{
    if (client)
        drop_connection(client, "Disconnected");
}

static void write_next(IrcClient *client);

static void write_finished(GObject *source, GAsyncResult *result, gpointer data)
{
    AsyncContext *ctx = data;
    IrcClient *client = ctx->client;
    gsize written = 0;
    GError *error = NULL;
    gboolean success = g_output_stream_write_all_finish(G_OUTPUT_STREAM(source),
                                                         result, &written, &error);
    if (ctx->generation == client->generation) {
        client->writing = FALSE;
        if (!success) {
            gchar *reason = g_strdup_printf("Send failed: %s",
                                              error ? error->message : "unknown error");
            drop_connection(client, reason);
            g_free(reason);
        } else {
            write_next(client);
        }
    }
    g_clear_error(&error);
    context_free(ctx);
}

static void write_next(IrcClient *client)
{
    if (!client->connected || client->writing || g_queue_is_empty(client->outgoing))
        return;

    AsyncContext *ctx = context_new(client);
    ctx->bytes = g_queue_pop_head(client->outgoing);
    gsize length;
    const gpointer bytes = g_bytes_get_data(ctx->bytes, &length);
    client->writing = TRUE;
    g_output_stream_write_all_async(g_io_stream_get_output_stream(G_IO_STREAM(client->socket)),
                                     bytes, length, G_PRIORITY_DEFAULT, client->cancel,
                                     write_finished, ctx);
}

gboolean irc_client_send(IrcClient *client, const char *line)
{
    if (!client || !client->connected || !irc_valid_outgoing_line(line))
        return FALSE;
    gchar *wire = g_strconcat(line, "\r\n", NULL);
    g_queue_push_tail(client->outgoing, g_bytes_new_take(wire, strlen(wire)));
    write_next(client);
    return TRUE;
}

static void read_next(IrcClient *client);

static void read_finished(GObject *source, GAsyncResult *result, gpointer data)
{
    AsyncContext *ctx = data;
    IrcClient *client = ctx->client;
    gsize len = 0;
    GError *error = NULL;
    gchar *line = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(source),
                                                        result, &len, &error);

    if (ctx->generation == client->generation && client->connected) {
        if (!line) {
            gchar *reason = g_strdup_printf("Connection closed: %s",
                                              error ? error->message : "remote server closed");
            drop_connection(client, reason);
            g_free(reason);
        } else {
            if (g_str_has_prefix(line, "PING ")) {
                gchar *pong = g_strconcat("PONG ", line + 5, NULL);
                irc_client_send(client, pong);
                g_free(pong);
            }
            if (client->on_line)
                client->on_line(line, client->user_data);
            if (ctx->generation == client->generation && client->connected)
                read_next(client);
        }
    }
    g_free(line);
    g_clear_error(&error);
    context_free(ctx);
}

static void read_next(IrcClient *client)
{
    AsyncContext *ctx = context_new(client);
    g_data_input_stream_read_line_async(client->input, G_PRIORITY_DEFAULT,
                                         client->cancel, read_finished, ctx);
}

static void connect_finished(GObject *source, GAsyncResult *result, gpointer data)
{
    AsyncContext *ctx = data;
    IrcClient *client = ctx->client;
    GError *error = NULL;
    GSocketConnection *socket = g_socket_client_connect_to_host_finish(
        G_SOCKET_CLIENT(source), result, &error);

    if (ctx->generation != client->generation) {
        if (socket) {
            g_io_stream_close(G_IO_STREAM(socket), NULL, NULL);
            g_object_unref(socket);
        }
    } else if (!socket) {
        gchar *reason = g_strdup_printf("Connection failed: %s",
                                          error ? error->message : "unknown error");
        drop_connection(client, reason);
        g_free(reason);
    } else {
        client->socket = socket;
        client->input = g_data_input_stream_new(
            g_io_stream_get_input_stream(G_IO_STREAM(socket)));
        g_data_input_stream_set_newline_type(client->input, G_DATA_STREAM_NEWLINE_TYPE_CR_LF);
        client->connected = TRUE;
        client->connecting = FALSE;

        gchar *nickname = g_strdup_printf("NICK %s", client->nick);
        irc_client_send(client, nickname);
        g_free(nickname);
        irc_client_send(client, "USER xmpirc 0 * :XMP IRC Client");
        if (client->on_state)
            client->on_state(TRUE, "Connected; registering nickname...", client->user_data);
        read_next(client);
    }

    g_clear_error(&error);
    context_free(ctx);
}

gboolean irc_client_connect(IrcClient *client, const char *host, guint16 port,
                             gboolean tls, const char *nick)
{
    if (!client || client->connected || client->connecting ||
        !host || !*host || !nick || !*nick || !port)
        return FALSE;
    /* Nick and hostname become IRC wire protocol values; reject control chars. */
    if (strpbrk(nick, " \t\r\n:") || strpbrk(host, " \t\r\n"))
        return FALSE;

    client->connecting = TRUE;
    client->cancel = g_cancellable_new();
    client->socket_client = g_socket_client_new();
    /* IRC sessions can remain quiet for long periods. A socket I/O timeout
     * here also times out the asynchronous read loop, even on a healthy
     * connection. Let IRC PING/PONG keep the session alive instead. */
    g_socket_client_set_tls(client->socket_client, tls);
    g_free(client->nick);
    client->nick = g_strdup(nick);

    AsyncContext *ctx = context_new(client);
    g_socket_client_connect_to_host_async(client->socket_client, host, port,
                                           client->cancel, connect_finished, ctx);
    return TRUE;
}

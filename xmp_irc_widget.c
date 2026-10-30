#include "xmp_irc_widget.h"
#include "irc_client.h"
#include "irc_protocol.h"

#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

/* Match the current XMP 1.3 GTK theme, not the earlier deep-ocean mockup. */
#define IRC_BG        "#071218"
#define IRC_CHAT_BG   "#050D11"
#define IRC_HEADER    "#08181D"
#define IRC_PANEL     "#0D1E25"
#define IRC_BUTTON    "#10262E"
#define IRC_BORDER    "#34616A"
#define IRC_AQUA      "#45D9CB"
#define IRC_TEXT      "#DBEFF0"
#define IRC_NICK      "#80D6AC"
#define IRC_MUTED     "#9DBFC2"

typedef struct {
    gchar *name;
    GtkWidget *page;
    GtkWidget *label;
    GtkWidget *view;
    GtkTextBuffer *log;
    GtkTextBuffer *names;
    GHashTable *users; /* lowercase nick -> original nick (and possible mode) */
    gboolean collecting_names;
} ChatTab;

typedef struct {
    GtkWidget *root;
    GtkWidget *notebook;
    GtkWidget *connect_button;
    GtkWidget *join_button;
    GtkWidget *entry;
    GtkWidget *send_button;
    GtkWidget *connection_label;
    GHashTable *tabs; /* lowercase tab name -> ChatTab */
    IrcClient *client;
    gchar *host;
    gchar *nick;
    gchar *auto_channel;
    guint port;
    gboolean tls;
    gboolean registered;
} XmpIrcUi;

static void append_text(ChatTab *tab, const char *who, const char *message,
                        const char *kind);

static void tab_free(gpointer data)
{
    ChatTab *tab = data;
    g_free(tab->name);
    if (tab->users)
        g_hash_table_unref(tab->users);
    g_free(tab);
}

static void ui_free(gpointer data)
{
    XmpIrcUi *ui = data;
    irc_client_clear_callbacks(ui->client);
    irc_client_disconnect(ui->client);
    irc_client_unref(ui->client);
    g_hash_table_unref(ui->tabs);
    g_free(ui->host);
    g_free(ui->nick);
    g_free(ui->auto_channel);
    g_free(ui);
}

static void widget_destroyed(GtkWidget *widget, gpointer data)
{
    (void)widget;
    XmpIrcUi *ui = data;
    /* The GTK children may be destroyed before the final object unref. */
    irc_client_clear_callbacks(ui->client);
    irc_client_disconnect(ui->client);
}

static gboolean is_channel(const char *name)
{
    return name && strchr("#&+!", name[0]) != NULL && name[0] != '\0';
}

static gchar *nick_key(const char *nick)
{
    while (nick && *nick && strchr("~&@%+", *nick))
        ++nick;
    return g_ascii_strdown(nick ? nick : "", -1);
}

static gint compare_strings(gconstpointer a, gconstpointer b)
{
    return g_ascii_strcasecmp(a, b);
}

static void refresh_names(ChatTab *tab)
{
    if (!tab->names)
        return;
    GList *values = g_hash_table_get_values(tab->users);
    values = g_list_sort(values, compare_strings);
    GString *list = g_string_new("");
    for (GList *p = values; p; p = p->next)
        g_string_append_printf(list, "%s\n", (const gchar *)p->data);
    gtk_text_buffer_set_text(tab->names, list->str, -1);
    g_string_free(list, TRUE);
    g_list_free(values);
}

static void add_user(ChatTab *tab, const char *name)
{
    if (!tab || !tab->users || !name || !*name)
        return;
    g_hash_table_replace(tab->users, nick_key(name), g_strdup(name));
    refresh_names(tab);
}

static gboolean has_user(ChatTab *tab, const char *name)
{
    gchar *key = nick_key(name);
    gboolean present = g_hash_table_contains(tab->users, key);
    g_free(key);
    return present;
}

static void remove_user(ChatTab *tab, const char *name)
{
    if (!tab || !tab->users)
        return;
    gchar *key = nick_key(name);
    g_hash_table_remove(tab->users, key);
    g_free(key);
    refresh_names(tab);
}

static ChatTab *get_tab(XmpIrcUi *ui, const char *name, gboolean create)
{
    gchar *key = g_ascii_strdown(name, -1);
    ChatTab *tab = g_hash_table_lookup(ui->tabs, key);
    if (tab || !create) {
        g_free(key);
        return tab;
    }

    tab = g_new0(ChatTab, 1);
    tab->name = g_strdup(name);
    tab->label = gtk_label_new(name);
    tab->users = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    tab->view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tab->view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(tab->view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tab->view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(tab->view), TRUE);
    gtk_container_add(GTK_CONTAINER(scroller), tab->view);
    tab->log = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tab->view));
    gtk_text_buffer_create_tag(tab->log, "time", "foreground", IRC_MUTED, NULL);
    gtk_text_buffer_create_tag(tab->log, "nick", "foreground", IRC_NICK, NULL);
    gtk_text_buffer_create_tag(tab->log, "own", "foreground", IRC_AQUA, NULL);
    gtk_text_buffer_create_tag(tab->log, "status", "foreground", IRC_TEXT, NULL);
    gtk_text_buffer_create_tag(tab->log, "error", "foreground", "#ff9292", NULL);
    gtk_text_buffer_create_tag(tab->log, "action", "foreground", "#cba8ef", NULL);

    if (is_channel(name)) {
        GtkWidget *pane = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_paned_pack1(GTK_PANED(pane), scroller, TRUE, FALSE);
        GtkWidget *users_scroll = gtk_scrolled_window_new(NULL, NULL);
        gtk_widget_set_size_request(users_scroll, 135, -1);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(users_scroll),
                                        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        GtkWidget *names_view = gtk_text_view_new();
        gtk_text_view_set_editable(GTK_TEXT_VIEW(names_view), FALSE);
        gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(names_view), FALSE);
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(names_view), TRUE);
        gtk_container_add(GTK_CONTAINER(users_scroll), names_view);
        tab->names = gtk_text_view_get_buffer(GTK_TEXT_VIEW(names_view));
        gtk_paned_pack2(GTK_PANED(pane), users_scroll, FALSE, FALSE);
        gtk_paned_set_position(GTK_PANED(pane), 710);
        tab->page = pane;
    } else {
        tab->page = scroller;
    }

    g_object_set_data(G_OBJECT(tab->page), "xmp-irc-tab", tab);
    gtk_notebook_append_page(GTK_NOTEBOOK(ui->notebook), tab->page, tab->label);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(ui->notebook), tab->page, TRUE);
    g_hash_table_insert(ui->tabs, key, tab);
    gtk_widget_show_all(tab->page);
    return tab;
}

static ChatTab *current_tab(XmpIrcUi *ui)
{
    gint index = gtk_notebook_get_current_page(GTK_NOTEBOOK(ui->notebook));
    GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(ui->notebook), index);
    return page ? g_object_get_data(G_OBJECT(page), "xmp-irc-tab") : NULL;
}

static void choose_tab(XmpIrcUi *ui, ChatTab *tab)
{
    gint index = gtk_notebook_page_num(GTK_NOTEBOOK(ui->notebook), tab->page);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(ui->notebook), index);
}

static void append_text(ChatTab *tab, const char *who, const char *message,
                        const char *kind)
{
    GDateTime *now = g_date_time_new_now_local();
    gchar *stamp = g_date_time_format(now, "[%H:%M] ");
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(tab->log, &end);
    gtk_text_buffer_insert_with_tags_by_name(tab->log, &end, stamp, -1, "time", NULL);

    if (who && *who) {
        gchar *prefix = g_strdup_printf("<%s> ", who);
        gtk_text_buffer_insert_with_tags_by_name(tab->log, &end, prefix, -1,
                                                  kind && !strcmp(kind, "own") ? "own" : "nick", NULL);
        g_free(prefix);
    }
    if (kind && (!strcmp(kind, "status") || !strcmp(kind, "error") ||
                 !strcmp(kind, "action")))
        gtk_text_buffer_insert_with_tags_by_name(tab->log, &end, message, -1, kind, NULL);
    else
        gtk_text_buffer_insert(tab->log, &end, message, -1);
    gtk_text_buffer_insert(tab->log, &end, "\n", 1);

    gint lines = gtk_text_buffer_get_line_count(tab->log);
    if (lines > 2001) {
        GtkTextIter start, cutoff;
        gtk_text_buffer_get_start_iter(tab->log, &start);
        gtk_text_buffer_get_iter_at_line(tab->log, &cutoff, lines - 2000);
        gtk_text_buffer_delete(tab->log, &start, &cutoff);
    }
    gtk_text_buffer_get_end_iter(tab->log, &end);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(tab->view), &end, 0.0, FALSE, 0, 0);
    g_free(stamp);
    g_date_time_unref(now);
}

static void status_message(XmpIrcUi *ui, const char *message, gboolean error)
{
    append_text(get_tab(ui, "Status", TRUE), NULL, message, error ? "error" : "status");
}

static gchar *config_filename(void)
{
    return g_build_filename(g_get_user_config_dir(), "xmp-irc", "settings.ini", NULL);
}

static void load_config(XmpIrcUi *ui)
{
    ui->host = g_strdup("irc.esper.net");
    ui->nick = g_strdup_printf("XMPGuest%04u", g_random_int_range(0, 10000));
    ui->auto_channel = g_strdup("#modulez");
    ui->port = 6697;
    ui->tls = TRUE;

    gchar *file = config_filename();
    GKeyFile *key = g_key_file_new();
    if (g_key_file_load_from_file(key, file, G_KEY_FILE_NONE, NULL)) {
        gchar *host = g_key_file_get_string(key, "server", "host", NULL);
        gchar *nick = g_key_file_get_string(key, "server", "nick", NULL);
        gchar *channel = g_key_file_get_string(key, "server", "channel", NULL);
        gint port = g_key_file_get_integer(key, "server", "port", NULL);
        GError *tls_error = NULL;
        gboolean tls = g_key_file_get_boolean(key, "server", "tls", &tls_error);
        if (host && *host) { g_free(ui->host); ui->host = g_strdup(host); }
        if (nick && *nick) { g_free(ui->nick); ui->nick = g_strdup(nick); }
        if (channel) { g_free(ui->auto_channel); ui->auto_channel = g_strdup(channel); }
        if (port > 0 && port <= 65535) ui->port = (guint)port;
        if (!tls_error) ui->tls = tls;
        g_clear_error(&tls_error);
        g_free(host); g_free(nick); g_free(channel);
    }
    g_key_file_unref(key);
    g_free(file);
}

static void save_config(XmpIrcUi *ui)
{
    GKeyFile *key = g_key_file_new();
    g_key_file_set_string(key, "server", "host", ui->host);
    g_key_file_set_string(key, "server", "nick", ui->nick);
    g_key_file_set_string(key, "server", "channel", ui->auto_channel);
    g_key_file_set_integer(key, "server", "port", (gint)ui->port);
    g_key_file_set_boolean(key, "server", "tls", ui->tls);
    gchar *data = g_key_file_to_data(key, NULL, NULL);
    gchar *file = config_filename();
    gchar *directory = g_path_get_dirname(file);
    if (g_mkdir_with_parents(directory, 0700) == 0)
        g_file_set_contents(file, data, -1, NULL);
    g_free(directory); g_free(file); g_free(data);
    g_key_file_unref(key);
}

static void state_changed(gboolean connected, const char *details, gpointer data)
{
    XmpIrcUi *ui = data;
    if (!connected)
        ui->registered = FALSE;
    gtk_button_set_label(GTK_BUTTON(ui->connect_button), connected ? "Disconnect" : "Connect");
    gtk_widget_set_sensitive(ui->connect_button, TRUE);
    gtk_widget_set_sensitive(ui->join_button, connected);
    gtk_widget_set_sensitive(ui->send_button, connected);
    gtk_label_set_text(GTK_LABEL(ui->connection_label),
                        connected ? ui->host : "Offline");
    status_message(ui, details ? details : (connected ? "Connected" : "Disconnected"),
                   !connected && details && g_strcmp0(details, "Disconnected"));
}

static void line_received(const char *line, gpointer data)
{
    XmpIrcUi *ui = data;
    IrcParsedLine p;
    if (!irc_parse_line(line, &p))
        return;

    const char *arg0 = p.n_params > 0 ? p.params[0] : "";
    const char *last = p.n_params ? p.params[p.n_params - 1] : "";
    char from[128];
    irc_prefix_nick(p.prefix, from, sizeof(from));

    if (!strcmp(p.command, "PING") || !strcmp(p.command, "PONG"))
        return;
    if (!strcmp(p.command, "001")) {
        ui->registered = TRUE;
        if (p.n_params && *p.params[0] && g_strcmp0(ui->nick, p.params[0])) {
            g_free(ui->nick);
            ui->nick = g_strdup(p.params[0]);
        }
        status_message(ui, last, FALSE);
        if (ui->auto_channel && *ui->auto_channel) {
            gchar *join = g_strdup_printf("JOIN %s", ui->auto_channel);
            irc_client_send(ui->client, join);
            g_free(join);
        }
        return;
    }
    if (!strcmp(p.command, "PRIVMSG") && p.n_params >= 2) {
        const char *target = is_channel(arg0) ? arg0 : from;
        ChatTab *tab = get_tab(ui, target, TRUE);
        const char *body = p.params[1];
        size_t len = strlen(body);
        if (len > 8 && !strncmp(body, "\001ACTION ", 8) && body[len - 1] == '\001') {
            gchar *action = g_strdup_printf("* %s %.*s", from, (int)(len - 9), body + 8);
            append_text(tab, NULL, action, "action");
            g_free(action);
        } else {
            append_text(tab, from, body, NULL);
        }
        return;
    }
    if (!strcmp(p.command, "NOTICE") && p.n_params >= 2) {
        ChatTab *tab = is_channel(arg0) ? get_tab(ui, arg0, TRUE) : get_tab(ui, "Status", TRUE);
        gchar *notice = g_strdup_printf("-%s- %s", from, p.params[1]);
        append_text(tab, NULL, notice, "status");
        g_free(notice);
        return;
    }
    if (!strcmp(p.command, "JOIN")) {
        ChatTab *tab = get_tab(ui, arg0, TRUE);
        add_user(tab, from);
        gchar *text = g_strdup_printf("→ %s joined %s", from, arg0);
        append_text(tab, NULL, text, "status");
        if (!g_ascii_strcasecmp(from, ui->nick))
            choose_tab(ui, tab);
        g_free(text);
        return;
    }
    if (!strcmp(p.command, "PART") || !strcmp(p.command, "KICK")) {
        ChatTab *tab = get_tab(ui, arg0, FALSE);
        if (tab) {
            const char *nick = !strcmp(p.command, "KICK") && p.n_params > 1 ? p.params[1] : from;
            remove_user(tab, nick);
            gchar *text = g_strdup_printf("← %s left %s", nick, arg0);
            append_text(tab, NULL, text, "status");
            g_free(text);
        }
        return;
    }
    if (!strcmp(p.command, "QUIT") || !strcmp(p.command, "NICK")) {
        GHashTableIter iterator;
        gpointer value;
        g_hash_table_iter_init(&iterator, ui->tabs);
        while (g_hash_table_iter_next(&iterator, NULL, &value)) {
            ChatTab *tab = value;
            if (!has_user(tab, from))
                continue;
            remove_user(tab, from);
            if (!strcmp(p.command, "NICK") && *arg0)
                add_user(tab, arg0);
            gchar *text = !strcmp(p.command, "QUIT")
                ? g_strdup_printf("← %s disconnected", from)
                : g_strdup_printf("%s is now known as %s", from, arg0);
            append_text(tab, NULL, text, "status");
            g_free(text);
        }
        if (!strcmp(p.command, "NICK") && *arg0 &&
            !g_ascii_strcasecmp(from, ui->nick)) {
            g_free(ui->nick);
            ui->nick = g_strdup(arg0);
        }
        return;
    }
    if (!strcmp(p.command, "353") && p.n_params >= 4) {
        ChatTab *tab = get_tab(ui, p.params[2], TRUE);
        if (!tab->collecting_names) {
            g_hash_table_remove_all(tab->users);
            tab->collecting_names = TRUE;
        }
        gchar **names = g_strsplit(p.params[3], " ", -1);
        for (guint i = 0; names[i]; ++i) {
            if (*names[i])
                g_hash_table_replace(tab->users, nick_key(names[i]), g_strdup(names[i]));
        }
        refresh_names(tab);
        g_strfreev(names);
        return;
    }
    if (!strcmp(p.command, "366") && p.n_params >= 2) {
        ChatTab *tab = get_tab(ui, p.params[1], FALSE);
        if (tab) tab->collecting_names = FALSE;
        return;
    }
    if (!strcmp(p.command, "332") && p.n_params >= 3) {
        ChatTab *tab = get_tab(ui, p.params[1], TRUE);
        gchar *topic = g_strdup_printf("Topic: %s", p.params[2]);
        append_text(tab, NULL, topic, "status");
        g_free(topic);
        return;
    }
    if (g_ascii_isdigit(p.command[0])) {
        if (p.command[0] == '4' || p.command[0] == '5') {
            gchar *error = g_strdup_printf("IRC %s: %s", p.command, last);
            status_message(ui, error, TRUE);
            g_free(error);
        } else if (!strcmp(p.command, "375") || !strcmp(p.command, "372") ||
                   !strcmp(p.command, "376")) {
            status_message(ui, last, FALSE);
        }
        return;
    }
    if (!strcmp(p.command, "ERROR"))
        status_message(ui, last, TRUE);
}

static gboolean send_raw(XmpIrcUi *ui, const char *line)
{
    gboolean sent = irc_client_send(ui->client, line);
    if (!sent)
        status_message(ui, "Cannot send: disconnected, invalid command, or IRC 510-byte limit exceeded.", TRUE);
    return sent;
}

static void send_message(XmpIrcUi *ui)
{
    gchar *input = g_strdup(gtk_entry_get_text(GTK_ENTRY(ui->entry)));
    g_strstrip(input);
    if (!*input) { g_free(input); return; }

    ChatTab *tab = current_tab(ui);
    if (!tab || !irc_client_connected(ui->client)) {
        status_message(ui, "Connect to an IRC server first.", TRUE);
        g_free(input);
        return;
    }

    gboolean sent = FALSE;
    if (input[0] == '/') {
        gchar *space = strchr(input, ' ');
        gchar *command = g_ascii_strdown(input + 1,
                                           space ? (gssize)(space - input - 1) : -1);
        gchar *rest = space ? g_strstrip(space + 1) : "";
        if (!strcmp(command, "join") && *rest) {
            gchar *line = g_strdup_printf("JOIN %s", rest);
            sent = send_raw(ui, line);
            g_free(line);
        } else if (!strcmp(command, "part")) {
            const char *channel = *rest ? rest : tab->name;
            if (is_channel(channel)) {
                gchar *line = g_strdup_printf("PART %s", channel);
                sent = send_raw(ui, line);
                g_free(line);
            }
        } else if (!strcmp(command, "nick") && *rest) {
            gchar *line = g_strdup_printf("NICK %s", rest);
            sent = send_raw(ui, line);
            g_free(line);
        } else if (!strcmp(command, "raw") && *rest) {
            sent = send_raw(ui, rest);
        } else if (!strcmp(command, "msg") && *rest) {
            gchar *separator = strchr(rest, ' ');
            if (separator) {
                *separator++ = '\0';
                while (*separator == ' ') ++separator;
                gchar *line = g_strdup_printf("PRIVMSG %s :%s", rest, separator);
                sent = send_raw(ui, line);
                if (sent) append_text(get_tab(ui, rest, TRUE), ui->nick, separator, "own");
                g_free(line);
            }
        } else if (!strcmp(command, "me") && *rest && g_strcmp0(tab->name, "Status")) {
            gchar *line = g_strdup_printf("PRIVMSG %s :\001ACTION %s\001", tab->name, rest);
            sent = send_raw(ui, line);
            if (sent) {
                gchar *action = g_strdup_printf("* %s %s", ui->nick, rest);
                append_text(tab, NULL, action, "action");
                g_free(action);
            }
            g_free(line);
        } else if (!strcmp(command, "quit")) {
            irc_client_disconnect(ui->client);
            sent = TRUE;
        } else if (!strcmp(command, "help")) {
            status_message(ui, "Commands: /join #channel, /part, /nick name, /msg nick text, /me action, /raw COMMAND, /quit, /help", FALSE);
            sent = TRUE;
        } else {
            status_message(ui, "Unknown or incomplete command. Type /help.", TRUE);
        }
        g_free(command);
    } else if (g_strcmp0(tab->name, "Status")) {
        gchar *line = g_strdup_printf("PRIVMSG %s :%s", tab->name, input);
        sent = send_raw(ui, line);
        if (sent) append_text(tab, ui->nick, input, "own");
        g_free(line);
    } else {
        status_message(ui, "Join a channel or use /msg nick message. Type /help for commands.", FALSE);
    }

    if (sent)
        gtk_entry_set_text(GTK_ENTRY(ui->entry), "");
    g_free(input);
}

static void on_send_clicked(GtkWidget *widget, gpointer data)
{
    (void)widget;
    send_message(data);
}

static gboolean show_connection_dialog(XmpIrcUi *ui, gboolean connect_now)
{
    GtkWidget *toplevel = gtk_widget_get_toplevel(ui->root);
    GtkWindow *parent = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        connect_now ? "Connect to IRC" : "IRC Settings", parent, GTK_DIALOG_MODAL,
        "Cancel", GTK_RESPONSE_CANCEL, connect_now ? "Connect" : "Save", GTK_RESPONSE_OK, NULL);
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 9);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 14);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), grid,
                        TRUE, TRUE, 0);

    const gchar *labels[] = { "IRC server", "Port", "Nickname", "Auto-join channel" };
    GtkWidget *host = gtk_entry_new();
    GtkWidget *port = gtk_spin_button_new_with_range(1, 65535, 1);
    GtkWidget *nick = gtk_entry_new();
    GtkWidget *channel = gtk_entry_new();
    GtkWidget *fields[] = { host, port, nick, channel };
    gtk_entry_set_text(GTK_ENTRY(host), ui->host);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(port), ui->port);
    gtk_entry_set_text(GTK_ENTRY(nick), ui->nick);
    gtk_entry_set_text(GTK_ENTRY(channel), ui->auto_channel);
    for (guint i = 0; i < G_N_ELEMENTS(fields); ++i) {
        GtkWidget *label = gtk_label_new(labels[i]);
        gtk_label_set_xalign(GTK_LABEL(label), 0);
        gtk_grid_attach(GTK_GRID(grid), label, 0, (gint)i, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), fields[i], 1, (gint)i, 1, 1);
    }
    GtkWidget *tls = gtk_check_button_new_with_label("Use encrypted TLS connection (recommended)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tls), ui->tls);
    gtk_grid_attach(GTK_GRID(grid), tls, 0, 4, 2, 1);
    gtk_widget_show_all(dialog);

    gboolean accepted = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK;
    if (accepted) {
        gchar *new_host = g_strdup(gtk_entry_get_text(GTK_ENTRY(host)));
        gchar *new_nick = g_strdup(gtk_entry_get_text(GTK_ENTRY(nick)));
        gchar *new_channel = g_strdup(gtk_entry_get_text(GTK_ENTRY(channel)));
        g_strstrip(new_host);
        g_strstrip(new_nick);
        g_strstrip(new_channel);
        if (!*new_host || !*new_nick || strpbrk(new_nick, " \t\r\n:") ||
            strpbrk(new_host, " \t\r\n") ||
            strpbrk(new_channel, " \t\r\n")) {
            status_message(ui, "Invalid server, nickname, or channel in settings.", TRUE);
            accepted = FALSE;
            g_free(new_host); g_free(new_nick); g_free(new_channel);
        } else {
            g_free(ui->host); ui->host = new_host;
            g_free(ui->nick); ui->nick = new_nick;
            g_free(ui->auto_channel); ui->auto_channel = new_channel;
            ui->port = (guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(port));
            ui->tls = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tls));
            save_config(ui);
            if (!connect_now)
                status_message(ui, "Settings saved. Reconnect to apply them.", FALSE);
        }
    }
    gtk_widget_destroy(dialog);
    return accepted;
}

static void on_connect_clicked(GtkWidget *widget, gpointer data)
{
    (void)widget;
    XmpIrcUi *ui = data;
    if (irc_client_connected(ui->client)) {
        irc_client_disconnect(ui->client);
        return;
    }
    if (!show_connection_dialog(ui, TRUE))
        return;
    status_message(ui, "Connecting securely if TLS is selected...", FALSE);
    gtk_button_set_label(GTK_BUTTON(ui->connect_button), "Connecting...");
    gtk_widget_set_sensitive(ui->connect_button, FALSE);
    if (!irc_client_connect(ui->client, ui->host, (guint16)ui->port, ui->tls, ui->nick)) {
        gtk_button_set_label(GTK_BUTTON(ui->connect_button), "Connect");
        gtk_widget_set_sensitive(ui->connect_button, TRUE);
        status_message(ui, "Could not start connection. Check server settings.", TRUE);
    }
}

static void on_settings_clicked(GtkWidget *widget, gpointer data)
{
    (void)widget;
    show_connection_dialog(data, FALSE);
}

static void on_join_clicked(GtkWidget *widget, gpointer data)
{
    (void)widget;
    XmpIrcUi *ui = data;
    GtkWidget *toplevel = gtk_widget_get_toplevel(ui->root);
    GtkWindow *parent = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Join IRC Channel", parent,
        GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "Join", GTK_RESPONSE_OK, NULL);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "#channel");
    gtk_entry_set_text(GTK_ENTRY(entry), ui->auto_channel);
    gtk_container_set_border_width(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), 12);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry,
                        TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        gchar *channel = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
        g_strstrip(channel);
        if (is_channel(channel) && !strpbrk(channel, " \t\r\n,:")) {
            gchar *line = g_strdup_printf("JOIN %s", channel);
            send_raw(ui, line);
            g_free(line);
        } else {
            status_message(ui, "Channel names must start with #, &, +, or ! and contain no spaces.", TRUE);
        }
        g_free(channel);
    }
    gtk_widget_destroy(dialog);
}

GtkWidget *xmp_irc_widget_new(void)
{
    XmpIrcUi *ui = g_new0(XmpIrcUi, 1);
    load_config(ui);
    ui->tabs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, tab_free);
    ui->client = irc_client_new(line_received, state_changed, ui);

    ui->root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_name(ui->root, "xmp-irc-root");
    gtk_container_set_border_width(GTK_CONTAINER(ui->root), 8);
    g_object_set_data_full(G_OBJECT(ui->root), "xmp-irc-state", ui, ui_free);
    g_signal_connect(ui->root, "destroy", G_CALLBACK(widget_destroyed), ui);

    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    ui->connect_button = gtk_button_new_with_label("Connect");
    ui->join_button = gtk_button_new_with_label("Join Channel");
    GtkWidget *settings_button = gtk_button_new_with_label("Settings");
    ui->connection_label = gtk_label_new("Offline");
    gtk_widget_set_sensitive(ui->join_button, FALSE);
    gtk_label_set_xalign(GTK_LABEL(ui->connection_label), 1);
    gtk_box_pack_start(GTK_BOX(toolbar), ui->connect_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), ui->join_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), settings_button, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(toolbar), ui->connection_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ui->root), toolbar, FALSE, FALSE, 0);

    ui->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(ui->notebook), TRUE);
    gtk_box_pack_start(GTK_BOX(ui->root), ui->notebook, TRUE, TRUE, 0);

    GtkWidget *input_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    ui->entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ui->entry), "Message or /command — /help for commands");
    ui->send_button = gtk_button_new_with_label("Send");
    gtk_widget_set_sensitive(ui->send_button, FALSE);
    gtk_box_pack_start(GTK_BOX(input_row), ui->entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(input_row), ui->send_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ui->root), input_row, FALSE, FALSE, 0);

    g_signal_connect(ui->connect_button, "clicked", G_CALLBACK(on_connect_clicked), ui);
    g_signal_connect(ui->join_button, "clicked", G_CALLBACK(on_join_clicked), ui);
    g_signal_connect(settings_button, "clicked", G_CALLBACK(on_settings_clicked), ui);
    g_signal_connect(ui->entry, "activate", G_CALLBACK(on_send_clicked), ui);
    g_signal_connect(ui->send_button, "clicked", G_CALLBACK(on_send_clicked), ui);

    ChatTab *status = get_tab(ui, "Status", TRUE);
    append_text(status, NULL,
        "XMP IRC — classic compact chat with buttons. Connect to begin. /help lists commands.", "status");
    append_text(status, NULL,
        "Default: EsperNet TLS 6697, #modulez preset. Change server/channel in Connect or Settings.", "status");

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "#xmp-irc-root {background:" IRC_BG "; color:" IRC_TEXT ";}"
        "#xmp-irc-root textview, #xmp-irc-root textview text {background:" IRC_CHAT_BG "; color:" IRC_TEXT ";}"
        "#xmp-irc-root entry {background:" IRC_CHAT_BG "; color:" IRC_TEXT "; border-color:" IRC_BORDER ";}"
        "#xmp-irc-root button {background:" IRC_BUTTON "; color:" IRC_TEXT "; border-color:" IRC_BORDER ";}"
        "#xmp-irc-root button:hover {background:#193B44;}"
        "#xmp-irc-root notebook header {background:" IRC_HEADER "; border-color:#25545A;}"
        "#xmp-irc-root notebook tab {background:" IRC_PANEL "; color:#B8D7D8; border:1px solid #28525A;}"
        "#xmp-irc-root notebook tab:checked {background:#14343B; color:" IRC_AQUA "; border-bottom:2px solid " IRC_AQUA ";}"
        "#xmp-irc-root scrolledwindow {border:none; box-shadow:none;}"
        "#xmp-irc-root label {color:" IRC_TEXT ";}", -1, NULL);
    GtkStyleContext *context = gtk_widget_get_style_context(ui->root);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css),
                                    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    /* GTK CSS applies to descendants when installed on the display. */
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                               GTK_STYLE_PROVIDER(css),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    return ui->root;
}

gint xmp_irc_add_notebook_page(GtkNotebook *notebook)
{
    g_return_val_if_fail(GTK_IS_NOTEBOOK(notebook), -1);
    GtkWidget *page = xmp_irc_widget_new();
    gint index = gtk_notebook_append_page(notebook, page, gtk_label_new("IRC"));
    gtk_widget_show_all(page);
    return index;
}

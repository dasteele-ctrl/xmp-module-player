#ifndef XMP_IRC_CLIENT_H
#define XMP_IRC_CLIENT_H

#include <gio/gio.h>

typedef struct IrcClient IrcClient;
typedef void (*IrcLineCallback)(const char *line, gpointer user_data);
typedef void (*IrcStateCallback)(gboolean connected, const char *details,
                                 gpointer user_data);

IrcClient *irc_client_new(IrcLineCallback on_line, IrcStateCallback on_state,
                          gpointer user_data);
void irc_client_clear_callbacks(IrcClient *client);
void irc_client_unref(IrcClient *client);

gboolean irc_client_connect(IrcClient *client, const char *host, guint16 port,
                             gboolean tls, const char *nick);
void irc_client_disconnect(IrcClient *client);
gboolean irc_client_send(IrcClient *client, const char *line);
gboolean irc_client_connected(IrcClient *client);

#endif

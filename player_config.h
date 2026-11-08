#ifndef PLAYER_CONFIG_H
#define PLAYER_CONFIG_H

#include <glib.h>

gchar *player_config_path(const gchar *name);
gboolean player_config_migrate(GError **error);

#endif

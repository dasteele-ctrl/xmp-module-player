#include "irc_protocol.h"

#include <string.h>

bool irc_valid_outgoing_line(const char *line)
{
    size_t count = 0;
    if (!line || !*line)
        return false;
    for (const unsigned char *p = (const unsigned char *)line; *p; ++p) {
        if (*p == '\r' || *p == '\n' || *p == '\0')
            return false;
        if (++count > IRC_MAX_LINE_BYTES)
            return false;
    }
    return true;
}

bool irc_parse_line(const char *line, IrcParsedLine *result)
{
    if (!line || !result)
        return false;

    size_t len = strlen(line);
    if (!len || len >= sizeof(result->storage))
        return false;

    memset(result, 0, sizeof(*result));
    memcpy(result->storage, line, len + 1);

    while (len && (result->storage[len - 1] == '\r' ||
                   result->storage[len - 1] == '\n'))
        result->storage[--len] = '\0';

    char *p = result->storage;
    while (*p == ' ')
        ++p;
    if (*p == ':') {
        result->prefix = ++p;
        char *end = strchr(p, ' ');
        if (!end)
            return false;
        *end = '\0';
        p = end + 1;
    }

    while (*p == ' ')
        ++p;
    if (!*p)
        return false;
    result->command = p;
    while (*p && *p != ' ')
        ++p;
    if (*p)
        *p++ = '\0';

    while (*p && result->n_params < IRC_MAX_PARAMS) {
        while (*p == ' ')
            ++p;
        if (!*p)
            break;
        if (*p == ':') {
            result->params[result->n_params++] = p + 1;
            break;
        }
        result->params[result->n_params++] = p;
        while (*p && *p != ' ')
            ++p;
        if (*p)
            *p++ = '\0';
    }
    return true;
}

void irc_prefix_nick(const char *prefix, char *destination, size_t capacity)
{
    if (!destination || !capacity)
        return;
    if (!prefix) {
        destination[0] = '\0';
        return;
    }
    size_t n = strcspn(prefix, "!@");
    if (n >= capacity)
        n = capacity - 1;
    memcpy(destination, prefix, n);
    destination[n] = '\0';
}

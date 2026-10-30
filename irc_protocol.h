#ifndef XMP_IRC_PROTOCOL_H
#define XMP_IRC_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>

#define IRC_MAX_PARAMS 15
#define IRC_MAX_LINE_BYTES 510

/* All pointers point into storage; keep the structure alive while using them. */
typedef struct {
    char storage[8192];
    const char *prefix;
    const char *command;
    const char *params[IRC_MAX_PARAMS];
    size_t n_params;
} IrcParsedLine;

bool irc_parse_line(const char *line, IrcParsedLine *result);
bool irc_valid_outgoing_line(const char *line);
void irc_prefix_nick(const char *prefix, char *destination, size_t capacity);

#endif

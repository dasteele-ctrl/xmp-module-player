#include "irc_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    IrcParsedLine p;
    assert(irc_parse_line(":nick!user@host PRIVMSG #music :hello there\r\n", &p));
    assert(!strcmp(p.prefix, "nick!user@host"));
    assert(!strcmp(p.command, "PRIVMSG"));
    assert(p.n_params == 2);
    assert(!strcmp(p.params[0], "#music"));
    assert(!strcmp(p.params[1], "hello there"));

    assert(irc_parse_line("PING :irc.example.org", &p));
    assert(!p.prefix && !strcmp(p.command, "PING"));
    assert(p.n_params == 1 && !strcmp(p.params[0], "irc.example.org"));

    assert(irc_parse_line(":irc.example.org 353 nick = #music :@op +voice member", &p));
    assert(p.n_params == 4 && !strcmp(p.params[3], "@op +voice member"));

    assert(!irc_parse_line("", &p));
    assert(!irc_parse_line(":badprefix", &p));
    assert(irc_valid_outgoing_line("PRIVMSG #music :Hello"));
    assert(!irc_valid_outgoing_line("PRIVMSG #music :hello\r\nQUIT :oops"));
    char longline[520];
    memset(longline, 'x', sizeof(longline) - 1);
    longline[sizeof(longline) - 1] = '\0';
    assert(!irc_valid_outgoing_line(longline));

    char nick[16];
    irc_prefix_nick("alice!id@host", nick, sizeof(nick));
    assert(!strcmp(nick, "alice"));
    puts("IRC protocol tests passed");
    return 0;
}

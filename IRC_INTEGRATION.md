# XMP IRC integration — XMP 1.3

This patch adds a self-contained GTK3 IRC tab to XMP Module Player 1.3.
It does not replace or modify XMP's audio decoders, playlist, SoundFont
manager, equalizer, HVSC downloader, or ZX/SID helper processes.

## Applying the patch

From the repository root, while on the current main branch with a clean
working tree:

```bash
git apply --check /path/to/XMP-IRC-v1.3-integration.patch
git apply /path/to/XMP-IRC-v1.3-integration.patch
make check-irc
make -j4
./xmp-linux
```

Use your actual patch location in place of /path/to. Do not run the apply
command if the check reports conflicts. The patch was prepared against the
published 1.3 release and should also match the main branch that was two
README/screenshot commits ahead on October 30, 2026.

## How IRC works

- Open XMP's new IRC notebook tab.
- Click Connect. The default is EsperNet irc.esper.net port 6697 with TLS.
- After server welcome, the client requests #modulez automatically.
- Use Join Channel to enter another channel; tabs remain separate.
- Click Disconnect to leave. Chat transcripts are not written to disk.
- Existing saved IRC server preferences override first-run defaults.

The IRC connection runs asynchronously through GIO on the GTK main loop.
It does not use SDL or an audio thread. No 30-second socket idle timeout
is configured; normal IRC PING/PONG handling keeps the session alive.

## Development check

`make check-irc` performs a GTK/GIO syntax check and runs the pure C IRC
protocol parser tests without linking the entire ZXTune/SDL music engine.
The full XMP build still requires your existing audio development libraries
and local ZXTune libraries as before.

All new source files are compiled into every normal XMP executable target.
This is one IRC server connection with multiple channel/private-message
tabs, not yet a multi-server client.

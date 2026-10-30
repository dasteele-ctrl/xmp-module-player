# XMP Module Player 1.4

## New in 1.4

- An integrated IRC tab with XMP's dark aqua theme and compact controls.
- Secure IRC connections (TLS), with EsperNet and `#modulez` set as defaults.
- Multiple channels, private-message tabs, nick lists, and common IRC commands.
- An idle-connection timeout fix from the earlier standalone IRC prototype.

## Existing player features

Tracker modules, SID, ZX Spectrum, MIDI/SoundFonts, MP3 and other audio formats,
the 10-band equalizer, HVSC integration, playlist management, and WAV export.

## Testing checklist before publishing

- `make check-irc` and the usual player build/tests pass.
- The AppImage launches on the target Linux system and plays sample files.
- ZX Spectrum, SID, tracker, MIDI/SoundFont, FFmpeg, and WAV export features work.
- The AppImage's IRC tab connects to EsperNet over TLS, joins `#modulez`,
  sends messages, joins a second channel, and stays connected while idle.
- Bundled libraries, notices, source availability, and distribution licenses
  are reviewed, including ZXTune and FFmpeg obligations.

IRC connections require an internet connection. Some mobile/ISP IP addresses
may be blocked by network DNS blacklists; this is separate from the player.

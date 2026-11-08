# Modulez Player 1.7.2

Modulez Player now saves preferences, equalizer settings, SoundFont selections,
remembered folders, and imported MilkDrop presets in `~/.config/modulez-player/`.
A custom `XDG_CONFIG_HOME` applies to both the old and new locations.

On upgrade, the player copies missing files once. Existing files in the new
folder take precedence. Earlier releases can still use the original folder.
IRC continues to use its separate `xmp-irc` settings.

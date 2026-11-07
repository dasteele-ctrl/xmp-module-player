# Modulez Player 1.7.0 — Release Notes

Version 1.7.0 focuses on fullscreen visualization and simple, customizable appearance controls.

## Highlights

- **Fullscreen Wavescope and MilkDrop:** F11, double-click, or the Fullscreen Vis button enters fullscreen; Escape exits.
- **Visualizer navigation:** Previous, Next, and Random mode/preset options in the right-click menu, with labels that match the active engine.
- **Playback from the visualizer:** Previous/Next Song options and Ctrl+Left / Ctrl+Right shortcuts work without leaving the visualization.
- **Discoverable controls:** The Vis tab includes a concise right-click hint, and context menus show available shortcuts.
- **Charcoal by default:** A neutral dark GTK theme with rainbow sliders for Background Color and Highlight Color.
- **Brightness control:** Adjust the darkness of the player without reducing custom color saturation. Preferences persist across restarts, and Restore Original Colors resets the controls.
- **Theme-aware main spectrum:** The small spectrum on the Player tab retains a two-color gradient while adapting to custom colors and brightness.
- **Visualizer toolbar styling:** The WebKit controls above Wavescope/MilkDrop follow the selected player colors; the generated visualizations retain their own independent palettes.

## Compatibility and packaging

- Linux x86-64 AppImage intended for testing on Linux Mint 22.3 XFCE before publication.
- The AppImage build is designed to bundle visualizer resources and relevant runtime dependencies.
- Compatibility with older distributions, including Debian 12, has not been verified. The current build may require a newer GLIBC.

## Existing playback features

Modulez Player continues to support tracker modules through libxmp, additional Amiga formats through UADE, C64 SID, ZX Spectrum music, MIDI/SoundFonts, FFmpeg audio formats, the 10-band equalizer, playlist repeat modes, WAV export, HVSC browsing, and integrated IRC chat.

## Credits

Wavescope visualization modes: Signal Ridge Labs (MIT). projectM/MilkDrop rendering: LGPL-2.1. See the bundled notices and project documentation for additional attributions and source references.

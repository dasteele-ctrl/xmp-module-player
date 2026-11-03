# Modulez Player 1.6

Stable release. AppImage packaging verification and desktop testing completed on Linux Mint 22.3 XFCE.

Modulez Player 1.6 adds a dedicated **Vis** tab with Wavescope visualizations
and projectM / MilkDrop rendering. Audio is supplied directly from the player
after EQ and volume processing; microphone or screen capture is not needed.

## New features

- 34 standard Wavescope visualization modes and 13 bundled `.milk` starter presets.
- projectM / MilkDrop preset rendering, with standard modes available as a fallback.
- Multiple-file `.milk` importing and a persistent user preset library outside the AppImage.
- Right-click next, previous, random and import controls, retaining the preset dropdown.
- Links to Cream of the Crop and Classic projectM preset collections.
- Dark and aqua controls matching Modulez Player; compact Samples, SF2/SF3, Prefs and Vis labels.
- Rendering and visualization audio capture pause while Vis is hidden.
- About and window version updated to 1.6, with Wavescope/projectM credits and project links.
- AppImage packaging for WebKit process helpers, injected bundle, sandbox tools and embedded visualization resources.

## Presets and compatibility

User presets are copied into `~/.config/xmp-module-player/milk-presets/` (or the
corresponding XDG configuration directory), keeping them across updates. A
rejected preset retains the previous visual. Image texture importing is not
implemented; texture-dependent presets can render differently or incompletely.
WebGL2 is required for projectM. Its upstream WASM build reserves a 768 MiB heap.

## Credits

Wavescope is by Signal Ridge Labs, licensed under MIT:
https://github.com/jasonulbright/wavescope

projectM provides MilkDrop-compatible rendering, licensed under LGPL-2.1:
https://github.com/projectM-visualizer/projectm

Notices and source/build references accompany the bundled visualizer assets.
Existing libxmp-first playback, UADE fallback, SID, ZXTune, MIDI, FFmpeg, WAV
export, EQ, HVSC, IRC and tray features remain available.

## Testing

Playback, Wavescope, MilkDrop, and final styling were checked on the developer’s desktop. Compatibility on other Linux systems has not yet been verified.

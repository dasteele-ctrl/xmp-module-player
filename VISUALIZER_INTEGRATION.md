# Embedded Wavescope visualization

The `Vis` tab contains Wavescope's 34 standard modes and projectM/MilkDrop,
using the direct stereo audio after Modulez's EQ and master volume. It preserves
the existing notebook and shortens Samples & Notes, SoundFonts and Preferences
to Samples, SF2/SF3 and Prefs, with descriptive tooltips.

## Build and try

On Linux Mint/Ubuntu, add the WebKit development packages to your existing
Modulez build environment:

```sh
sudo apt install libwebkit2gtk-4.1-dev libsoup-3.0-dev
make -j4 modulez-player
./modulez-player
```

The pinned projectM engine is bundled in the source tree. If it is absent,
`make` downloads it once using `visualizer/fetch-engine.py` and verifies its
size, magic and git-blob hash. The HTML, JS, presets and WASM are compiled into
GResource data in the executable. Playback and visualization need no internet,
Python, screen sharing, microphone access or external files at runtime.

Play a track, open **Vis**, then select Standard modes or projectM / MilkDrop.
The page loads on first visit; selecting projectM allocates its engine on
demand. The fixed upstream WASM heap is 768 MiB. projectM renders at a 30 fps
target, capped at 960 pixels wide and 720 high with a 32x24 warp mesh. The
overall surface uses the same dark/aqua palette as the rest of the player.
If WebGL2 or projectM initialization fails, Standard modes remain available.
`Reload Vis` restarts the visualizer after an error without restarting playback.

Switching tabs or hiding the player stops both PCM capture and render work.
Returning resumes with current PCM. projectM is fed fresh samples exactly once,
in chunks within its maximum count; after a pause it receives silence.
The Vis tab does not alter the sound sent to SDL.

## Implementation

`visualizer_pcm.c` is a lock-free single-producer/single-consumer queue. Only
SDL's callback writes; only GTK's timer reads. The queue is bounded to sixteen
1024-frame blocks and drops visual samples rather than wait when full. The UI
retains at most four latest blocks per update. It allocates and encodes data
only on the GTK thread. No GTK, WebKit, network calls or dynamic allocations
run in the audio callback.

`visualizer.c` hosts WebKitGTK inside the notebook. A loopback-only SoupServer
serves a fixed allowlist of GResource assets under a random per-view URL prefix.
It exposes no audio endpoint. Native permissions and external navigation are
blocked; WebKit uses an ephemeral context. The page directly analyzes player
PCM and passes it to the renderer, without MediaStream or Web Audio APIs.

## Validation and packaging status

`make check-vis-pcm` checks exact sample copying, splitting, bounded overflow,
enable/disable behavior and concurrent producer/consumer ordering. The standard
renderers and PCM/projectM adapter have separate Canvas/simulated-engine checks;
the preceding desktop prototypes passed the user's real-hardware tests. Native
GTK/WebKit/libsoup integration and the updated main source pass real-header
syntax checks; the visualizer passes `-Werror`. GResource generation and
resource C compilation pass with the verified WASM. Queue checks also pass
AddressSanitizer/UndefinedBehaviorSanitizer (leak scanning disabled because
the test environment does not expose process metadata). The
embedded notebook build still requires its own desktop smoke test, including
switching/hiding the tab, all playback engines, seeking, and shutdown.

The Vis-aware AppImage builder now includes WebKit helpers, dependencies and
path relocation checks. The player reports version **1.6**, and the final
artifact name is `Modulez-Player-v1.6-Linux-x86_64.AppImage`. Final packaging and
runtime portability checks are pending; no release or tag has been published.
Check the About page in the native player before starting the packaging run.

## Upstream sources

Wavescope renderers and starter presets are from commit
`29d3f8c5c3b577890143c9e6d8abe267bb1628bf` of
https://github.com/jasonulbright/wavescope (MIT; see
`visualizer/upstream/LICENSE`). The unchanged TypeScript renderer sources are
under `visualizer/upstream/`; `wavescope.js` is their combined JavaScript.
The derivative host/PCM glue in this change is contributed under the player
repository's existing license.

The projectM WASM/glue are Wavescope's ABI-2 build, based on projectM commit
`149bfc43963e54e397ccdb42f27019d22b659098`. Its LGPL notices and source/build
references are in `visualizer/projectm/`. The engine remains a separate resource
that can be replaced by a source rebuild. Its pinned git-blob hash is
`84ad815c17b604629f01f21b763546352c37b95f`.

## User MilkDrop library

**Import Presets** opens a native multiple-file chooser for `.milk` files.
Copies are saved under `$XDG_CONFIG_HOME/xmp-module-player/milk-presets`
(normally `~/.config/xmp-module-player/milk-presets`), outside the executable or
AppImage. Original files are never moved or changed. SHA-256 content IDs avoid
importing the same preset twice. Valid UTF-8, nonempty preset text up to 1 MiB
is accepted; the projectM compiler remains the final format check. A rejected
preset retains the previous visual and restores its dropdown selection.

Saved entries are discovered again on each launch/reload. Imported names appear
literally in the existing dropdown alongside the 13 bundled starter presets.
Right-click either canvas for Next Preset, Previous Preset, Random Preset, or
Import Presets. Navigation is enabled while projectM is active; random skips
the current preset when alternatives exist. The popup uses the same dark/aqua
colors and supports arrow keys and Escape. These changes do not alter audio.

This importer handles preset text only. Presets that require external texture
images may also need texture support before they render completely.

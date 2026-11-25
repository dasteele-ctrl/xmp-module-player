# Modulez Player 1.8.7

**The RMF Update** adds direct Beatnik RMF playback with the Beatnik Standard
v1.1.1 Uncompressed bank. Browse WebTV and SoundMusicSys tracks, or open a
Headspace album to play a song, queue it, or save its ZIP to Downloads.

[Download the 1.8.7 Linux AppImage](https://github.com/dasteele-ctrl/xmp-module-player/releases/download/v1.8.7/Modulez-Player-v1.8.7-x86_64.AppImage)


**Formerly XMP Module Player.** Version **1.8.6** improves keyboard control and fullscreen viewing for local video.

Version **1.8.5 — The Video Playback Update** introduces integrated local video playback, a dedicated themed video window, fullscreen viewing, shared playback controls, and mixed music/video playlists.

Version **1.8.4** adds AdLib playback and export through AdPlug. Modulez now recognizes 360 additional filename extensions and aliases. You can open Art & Magic, Nintendo SPC, S98, Atari SAP, and AY Emul tracks directly from Modland.

On the **Radio** tab, play a station and choose **Record WAV…**. **Stop Recording** saves a 44.1 kHz stereo WAV while playback continues. Stopping radio, switching stations, or quitting also finishes the file. Reconnection gaps are omitted.

Modulez Player **1.8.2** adds batch audio export from the playlist. Select tunes with Ctrl-click, Shift-click, or Ctrl+A, choose **Export selected tracks…**, then pick a destination folder and format. Each tune becomes a separate file.

The music archive browser arrived in 1.8.1. Browse Modland, The Mod Archive, AMP, or a site of your own. Click a track download to play it, or drag its link onto the player to queue it.
The built-in AppImage updater, introduced in 1.7.4, lets you update
without interrupting playback.

Version 1.6 introduced Wavescope and projectM / MilkDrop visualization.
Version 1.5 introduced the Modulez Player name and icon.

**In memory of modulez.org (2000-2013)**  
[http://misc.yero.org/modulez/](http://misc.yero.org/modulez/)

A native Linux music and video player built with **GTK3 and SDL2**, with playback powered by **libxmp, UADE, AdPlug, ZXTune, libsidplayfp, FluidSynth, and FFmpeg**.

Modulez Player brings classic tracker music and regular audio playback to the Linux desktop, with a graphical interface, real-time audio visualization, and detailed module and sample information.

![Modulez Player 1.8.5 playing Some Chords with a mixed audio and video playlist](modulez-player-1.8.5.png)

## Local video playback (new in 1.8.5)

Open a local video or add it to a playlist alongside music. The video window uses the active Modulez theme. On X11 it docks to the right of the player when there is room. Drag its title bar away to detach it, or drag its upper-left corner back to the player’s upper-right corner to snap it into place. On Wayland the compositor controls window placement.

The video window shares play/pause, stop, seek, and volume with the main player. Double-click the picture or press F11 to toggle fullscreen. Escape returns to the window. With the video window focused, Space pauses or resumes playback and the left and right arrows seek five seconds backward or forward. In fullscreen, the toolbar fades after five seconds of inactivity and reappears when you move the pointer. Video playback uses libmpv for synchronized sound and picture. Video audio bypasses the Modulez equalizer and visualizer. Source builds on Linux Mint 22 need the additional package `libmpv-dev`.

## Features

Download the [1.8.6 Linux AppImage](https://github.com/dasteele-ctrl/xmp-module-player/releases/tag/v1.8.6) or read the [1.8.6 release notes](RELEASE_NOTES_1.8.6.md).

See the [1.8.2 release notes](RELEASE_NOTES_1.8.2.md) for batch export settings and supported formats.

See the [1.8.1 release notes](RELEASE_NOTES_1.8.1.md) for details about Browse Music.

- **Integrated video player** — local video playback, dockable video window, fullscreen viewing, and shared playback controls
- **Live internet radio** — ten curated retro/demoscene stations, worldwide Radio Browser search, favorites and custom streams
- **Automatic radio reconnection** — increasing retry delays and automatic playback recovery
- **Modern interface** — collapsible navigation sidebar, rounded windows and themed dialogs, and responsive visualizer controls

- 🌐 **Open URL** — download and play audio and tracker files directly from HTTP/HTTPS URLs, including ModArchive and Modland
- 🎵 **PSID and RSID support** — recognizes `.sid`, `.psid`, and `.rsid` filenames
- 📥 **Integrated URL downloader** — cancellation, error handling, decoded filenames, and persistent local storage
- 🎨 **Refined interface** — rounded buttons and tabs, balanced playback controls, and an expandable volume slider

- **Wavescope Vis tab** with 34 standard visualization modes and projectM / MilkDrop
- **MilkDrop preset library** with 13 bundled starters, multi-file import, persistent user storage, and right-click next/previous/random controls
- **Offline visualizations** fed directly from playback after EQ and volume; rendering pauses when hidden
- **Fullscreen visualizers** with F11, double-click, Escape, and right-click controls
- **Song navigation from the visualizer** with Ctrl+Left/Right and context-menu commands
- **Customizable charcoal theme** with rainbow Background/Highlight Color sliders and brightness
- **Theme-aware dual-color spectrum** on the Player tab
- **Theme-aware IRC interface** with colors that follow the selected player theme
- **Theme-aware equalizer graph** with matching backgrounds, curves, and frequency bars
- 🎵 **Tracker module playback** powered by libxmp
- **AdLib playback and export** through AdPlug’s OPL3 emulator, including Reality AdLib Tracker (`.rad`), HSC (`.hsc`), Creative Music File (`.cmf`), EdLib (`.d00`), and AdLib Tracker II (`.a2m` and `.a2t`)
- 🎵 **Additional Amiga music formats** powered by UADE, with libxmp retaining priority for formats supported by both engines
- 🎧 **Regular audio playback** powered by FFmpeg: MP3, WAV, M4A/AAC, Ogg Vorbis, Opus, and FLAC
- 🎵 **Commodore 64 SID playback** powered by libsidplayfp
- 🎹 **MIDI playback** powered by FluidSynth, with user-selectable SF2/SF3 SoundFonts
- 🎼 **SoundFont manager** with multi-file import, folder scanning, live switching, and a startup default
- 🎵 **ZX Spectrum AY/YM playback** through bundled ZXTune, including PT3, AY, VTX, YM, STC, and SQT
- 💾 **Offline WAV export** for all seven playback engines, with cancellation and overwrite confirmation; UADE exports until song end by default, with an optional time limit
- 🎚️ **Playback controls** with seeking and track timing
- 📃 **Playlist management** with add, remove, clear, drag-and-drop support, and recursive music-folder importing
- 📃 **High Voltage SID Collection (HVSC) browsing and integrated downloader**
- 🔁 **Automatic track advancement**
- 📂 **M3U playlist loading and saving**
- 📊 **Real-time audio spectrum visualizer** with peak hold
- 🎚️ **10-band equalizer (introduced in 1.3)** with presets, preamp, and a live frequency-response curve
- 🌊 **Channel scopes** for visualizing audio activity
- 🧩 **Pattern viewer** for tracker data
- 🔊 **Sample browser** with waveform visualization, sample details, loop information, and module comments
- 🖥️ **Native GTK3 interface** with SDL2 audio output
- 💬 **Built-in IRC chat** with EsperNet/#modulez preset, TLS, channel tabs, and private messages

## Internet radio (new in 1.8.0)

The **Radio** tab offers four categories: Demoscene / Retro, World Radio,
Favorites and Custom Stations. Double-click a station to play it through
FFmpeg. Live stream metadata, when supplied by the broadcaster, appears in
the player. Radio uses the same volume, equalizer and visualization pipeline
as local playback; live streams do not support seeking.

The curated directory includes ERICADE, Nectarine, SceneSat, Kohina, CVGM,
SLAY Radio, Rainwave Chiptunes, ModFM, NoLife Radio and SceneSat Compo Backgrounds.
World Radio uses [Radio Browser](https://www.radio-browser.info/) to search by
station name, country code and tag. Favorites and custom stations are saved in
`~/.config/modulez-player/radio-stations.ini` (or the corresponding XDG directory).
Custom stations accept HTTP/HTTPS stream URLs.

When a stream fails, the player retries after 2, 4, 8, 16 and then 30 seconds.
After 15 seconds of successfully decoded audio, the delay resets. Stop,
station switching, local playback and quitting cancel retries. Recovery begins
when the decoder detects a failure; a stalled network may take longer to detect.

Source builds require libsoup 3 and JSON-GLib development headers in addition
to the existing dependencies (Mint/Ubuntu: `sudo apt install libsoup-3.0-dev libjson-glib-dev`).

See the [1.8.0 release notes](RELEASE_NOTES_1.8.0.md).

## IRC community chat

The **IRC** tab is a simple, classic IRC client integrated into Modulez Player's GTK3
interface. It defaults to EsperNet (`irc.esper.net`, TLS port 6697) and
auto-joins `#modulez` after a successful connection. Change the network,
nickname, and auto-join channel with the **Connect** or **Settings** buttons.

The client has channels, private-message tabs, a user list, and IRC commands
such as `/join`, `/part`, `/nick`, and `/me`. It does not connect until you
click **Connect**. Network settings are saved to
`~/.config/xmp-irc/settings.ini`; no IRC passwords are stored.

The IRC interface follows the selected Modulez Player theme, including
backgrounds, buttons, borders, and other controls. Nicknames and message
types retain their distinct colors for readability.

The parser can be checked using `make check-irc`; that command also performs
a syntax check of the GTK/GIO source files.

## Version 1.7.5

About-tab visual enhancements and minor interface refinements.

[1.7.5 release notes](RELEASE_NOTES_1.7.5.md)

## Version 1.7.4

The AppImage can install future releases from the About tab. Choose **Check for
a new version**, then **Install update**. Keep listening or choose **Restart now**
after the download finishes. Your existing launcher continues to use the same path.

## Version 1.7.3

Double-click a sample in the Samples tab to hear it, or open the Pattern tab to play
and loop individual patterns in a larger grid.

[1.7.3 release notes](RELEASE_NOTES_1.7.3.md)

## Version 1.7.2

Player settings now live in `~/.config/modulez-player/`. On upgrade, the player
copies missing settings and imported MilkDrop presets from the old folder.
[1.7.2 release notes](RELEASE_NOTES_1.7.2.md)

## Version 1.7.1

Fixed a GTK theme warning and updated the HVSC waveform when appearance
settings change. [1.7.1 release notes](RELEASE_NOTES_1.7.1.md)

## Version 1.7.0

Version 1.7.0 adds fullscreen Wavescope and MilkDrop, convenient song and preset navigation, and a charcoal default theme with customizable colors and brightness. The Player spectrum retains a colorful two-hue gradient that follows the selected accent.

See the [1.7 release notes](RELEASE_NOTES_1.7.md) for details and compatibility notes.

## Version 1.6.4

Opening a track from the command line, a file manager, or **Open File** now starts playback immediately. The player appends new tracks to the current playlist and reuses the existing entry when you reopen the same path. **Add Files** queues tracks for later.

Version 1.6.3 fixed audio stuttering when **Show pattern & scopes** is enabled.
The pattern view and channel scopes now draw while playback continues.

See the [v1.6.4 release](https://github.com/dasteele-ctrl/xmp-module-player/releases/tag/v1.6.4) or the [v1.6 visualizer release notes](RELEASE_NOTES_1.6.md).

The v1.6.3 Linux x86_64 AppImage passed packaging verification and desktop testing on
Linux Mint 22.3 XFCE. Compatibility on other Linux systems has not yet been verified.
Wavescope is credited to Signal Ridge Labs (MIT), and projectM uses LGPL-2.1;
license notices and source references are included with the visualization assets.

## Download

**[Download the latest Modulez Player for Linux x86_64](https://github.com/dasteele-ctrl/xmp-module-player/releases/latest)**

[Latest release notes and downloads](https://github.com/dasteele-ctrl/xmp-module-player/releases/latest).

For a first install or an upgrade from 1.7.4 or earlier, download the AppImage.
The built-in updater uses the matching `.AppImage.zsync` file automatically for future updates.

The AppImage includes the application launcher icon.
See [all releases](../../releases) for earlier versions.

### Running the AppImage

1. Download the versioned `Modulez-Player-v1.8.6-x86_64.AppImage` file from Release v1.8.6.
2. Open a terminal in the download folder.
3. Make it executable and launch it:

   ```bash
   chmod +x Modulez-Player-v1.8.6-x86_64.AppImage
   ./Modulez-Player-v1.8.6-x86_64.AppImage
   ```

### Running on Windows with WSL

The Linux AppImage has been successfully tested using WSL with Ubuntu 24.04.
No compiling is needed. This remains experimental; audio and visualizer
behavior may vary between computers.

Requires an Intel/AMD 64-bit PC with Windows 11 or Windows 10 build 19044
or newer, using WSL 2 with WSLg.

**1. Install Ubuntu**

Open **PowerShell as Administrator** and run:

```powershell
wsl --install -d Ubuntu-24.04
```

Restart if requested. Open **Ubuntu 24.04** from the Start menu and create
your Linux username and password.

**2. Download Modulez Player**

Download the **AppImage** from the
[releases page](https://github.com/dasteele-ctrl/xmp-module-player/releases/latest).

**3. Copy it into Ubuntu**

In the Ubuntu terminal, run:

```bash
mkdir -p ~/Modulez
cd ~/Modulez
explorer.exe .
```

Copy the downloaded AppImage into the folder that opens.
Keep only one Modulez Player AppImage in this folder.

**4. Run the player**

In the Ubuntu terminal, run:

```bash
cd ~/Modulez
chmod +x Modulez-Player-*.AppImage
SDL_AUDIODRIVER=pulseaudio PULSE_LATENCY_MSEC=200 ./Modulez-Player-*.AppImage --appimage-extract-and-run
```

The extraction option avoids needing FUSE.

**To launch it again**, open Ubuntu and run:

```bash
cd ~/Modulez
SDL_AUDIODRIVER=pulseaudio PULSE_LATENCY_MSEC=200 ./Modulez-Player-*.AppImage --appimage-extract-and-run
```

**Windows music files:** In the player's file picker, browse to
`/mnt/c/Users/`, then your Windows username and music folder.

See [Microsoft's WSL GUI guide](https://learn.microsoft.com/en-us/windows/wsl/tutorials/gui-apps)
for WSL requirements and troubleshooting.

### Add the player to your application menu

For a menu entry with the player icon, use **AppImageLauncher**, an
integration tool described in the official AppImage documentation.

1. Follow the installation instructions in the
   [official AppImageLauncher project](https://github.com/TheAssassin/AppImageLauncher).
   Choose the package appropriate for your Linux distribution and architecture.
2. Download the Modulez Player AppImage from this project's Releases page.
3. Open the downloaded AppImage. When AppImageLauncher offers the choice,
   select **Integrate and run**.
4. Launch **Modulez Player** from your application menu on future runs.

AppImageLauncher uses the desktop entry and icon already inside the AppImage;
there is no need to extract them or find an icon yourself. By default, it moves
the AppImage to `~/Applications`. Keep that integrated file in place so the
menu entry continues to work.

Desktop integration is optional. You can still run the AppImage directly,
use another integration tool, or create a launcher yourself. See the
[AppImage desktop integration guide](https://docs.appimage.org/user-guide/run-appimages.html#integrating-appimages-into-the-desktop)
for more information.

## Requirements

The 1.6 AppImage includes the playback libraries, UADE core and player data, ZXTune helper, and default MIDI SoundFont with its license. A separate UADE installation or SoundFont download is not needed. Compatibility with Linux distributions other than the development system has not yet been fully tested.

### MIDI and SoundFonts

Standard MIDI files (`.mid`, `.midi`, `.kar`) are rendered directly to the
existing SDL stereo output by **FluidSynth**, using its audio sample clock.
The **SoundFonts** tab manages a library of `.sf2` and `.sf3` file paths.
Add one or many files, scan a folder (including subfolders), or double-click
an entry to switch while MIDI plays. **Set / Clear Default** chooses the font
used automatically on subsequent launches. Removal only removes the list entry,
never the original SoundFont file. Existing single-SoundFont settings are
imported automatically when upgrading. The player also detects common system
SoundFonts if you have not selected one.

On Linux Mint / Ubuntu, build MIDI support with the FluidSynth development
library and a General MIDI SoundFont:

```bash
sudo apt install libfluidsynth-dev fluid-soundfont-gm
make -j4
./modulez-player
```

The 1.6 AppImage bundles **MS Basic.sf3** as its default General MIDI
SoundFont, together with its license. You can still import and select your
own SF2/SF3 banks. A previously saved selection may take priority over the
bundled default. Packaging details are in the
[AppImage README](packaging/appimage/README.md).

MIDI playback and the SoundFont Manager were introduced in **version 1.2**.

### AdLib playback (new in 1.8.4)

The 1.8.4 Linux AppImage bundles AdPlug and libbinio. For a source build, install AdPlug 2.4 and the libbinio development headers, then check that `pkg-config --modversion adplug` reports 2.4. AdPlug renders DOS FM
music through an emulated OPL3 chip. Supported examples include RAD, HSC,
CMF, D00, A2M, A2T and DOSBox DRO. Open a file locally or from Modland.
The same decoder supports playlist playback and audio export.

### Amiga playback with UADE (introduced in version 1.4.3)

**UADE** adds Amiga music formats that libxmp cannot play. libxmp retains
priority for shared formats; UADE is used as a fallback. Existing dedicated
SID, MIDI, and ZX Spectrum playback remains available.

Open the music file itself, such as `mdat.song`, and keep any companion
sample files, such as `smpl.song`, in the same folder with their original
names. Folder importing recognizes common Amiga prefix and suffix names.
For formats outside that initial filename list, choose **All files** in
Open or Add Files.

UADE supports seeking, pause/resume, playlist navigation, volume, EQ, and
mixed waveform/spectrum visualization. Seeking uses background emulation,
so larger jumps can take time. When no duration is known, the time display
shows `--:--` and the seek bar uses an expanding navigation range rather
than claiming a fixed song length.

WAV export renders from the beginning using an independent decoder. By
default, it continues until UADE reports song end. For looping songs, enable
**Limit to** in the Save dialog and choose 1–3600 seconds. Exports stop at
that limit or song end, whichever comes first. Natural-end mode does not
guarantee automatic loop detection. Cancellation or a failed export leaves
an existing destination file unchanged.

The AppImage includes UADE's runtime and player data. For source builds,
UADE development headers, `libuade.pc`, the installed core/data, and
libbencodetools are required; development and testing used **UADE 3.05**.
See [UADE integration notes](UADE_INTEGRATION.md) for runtime overrides
and diagnostic commands.

### Beatnik RMF playback

Open a local `.rmf` song, add it to a playlist, or import its folder. NeoBAE
renders stereo audio directly using the bundled Beatnik Standard v1.1.1 Uncompressed bank. To use
your own Beatnik bank, set `MODULEZ_RMF_BANK` to its full path before launching.
The default Linux build uses Git and CMake to build the pinned engine on the
first build. The AppImage includes the decoder and runs offline.

### 10-Band Equalizer (introduced in version 1.3)

The integrated **Equalizer** tab provides ten adjustable bands (31 Hz to
16 kHz), a preamp control, an aqua frequency-response graph with a subtle live
spectrum, on/off bypass, and Flat, Rock, Pop, Classical, Jazz, Bass Boost and
Vocal Clarity presets. Moving a slider creates a Custom preset. Changes are
applied to the shared stereo output for tracker modules, UADE, SID, ZX, MIDI, and
regular audio files and live radio without any additional audio library. The EQ uses smooth
transitions when changing gains or toggling bypass.

EQ settings are stored in
`~/.config/modulez-player/equalizer.ini` (or the corresponding XDG
configuration directory), so they persist between AppImage upgrades.

The EQ is for **live playback**; exported WAV files remain unchanged. Use the
preamp to leave headroom when boosting multiple frequency bands. The 1.3 theme
also brings the same deep ocean-blue and aqua palette to the other tabs.

To run the standalone DSP regression tests during development:

```bash
make check-eq
```

### ZX Spectrum playback

Modulez Player 1.1 includes native ZX Spectrum playback powered
by ZXTune.

The Linux AppImage bundles the ZXTune playback engine and its required
libraries. Users do not need to install ZXTune, compile libraries, or
download additional components.

For compatibility, ZXTune runs in a separate helper process called
`xmp-zx-helper`. Decoded PCM audio is streamed directly to the main
player's SDL2 audio engine without creating temporary WAV files.

Supported formats include `.pt3`, `.pt2`, `.ay`, `.ym`, `.vtx`, `.stc`,
`.sqt`, `.psg`, and other formats recognized by ZXTune.

Volume, seeking, spectrum visualization, waveform display, and playlist
controls are integrated into the player.

Tracker pattern visualization remains available for supported libxmp
formats, but is not currently available for ZX Spectrum files.

#### Building from source

Developers compiling Modulez Player from source must first build
ZXTune's static libraries:

```bash
make platform=linux system.zlib=1 -C ~/zxtune/apps/zxtune123 -j4
```

Then compile Modulez Player:

```bash
make -j4
./modulez-player
```

The optional legacy `zxtune123` WAV-based fallback is not included
in the distributed AppImage.

## Built With

- [GTK 3](https://www.gtk.org/) — graphical interface
- [SDL 2](https://www.libsdl.org/) — audio output
- [libxmp](https://github.com/libxmp/libxmp) — tracker module playback
- [UADE](https://zakalwe.fi/uade/) — additional Amiga music formats
- [libsidplayfp](https://github.com/libsidplayfp/libsidplayfp) — Commodore 64 SID playback
- [AdPlug](https://adplug.github.io/) — AdLib OPL2/OPL3 music playback and rendering for export
- [FFmpeg](https://ffmpeg.org/) — decoding regular audio formats and live radio
- [libsoup](https://libsoup.gnome.org/) and [JSON-GLib](https://gnome.pages.gitlab.gnome.org/json-glib/) — worldwide radio discovery
- [ZXTune](https://github.com/vitamin-caig/zxtune) — native ZX Spectrum and chiptune playback
- [FluidSynth](https://www.fluidsynth.org/) — MIDI synthesis with SF2/SF3 SoundFonts

## Project Status

**Current source version: 1.8.7**

Version 1.6 adds Wavescope/projectM visualizations, saved `.milk` preset importing,
right-click preset navigation, preset-collection links, compact tab labels and
Vis-aware AppImage packaging. The About page includes visualization credits.


Version 1.5 rebrands XMP Module Player as **Modulez Player**, with a new
application icon, About title, tray/menu text, desktop entry, and AppImage
filename. Playback features from 1.4.3 are retained.

Version 1.7.2 stores player settings in `~/.config/modulez-player/` under the
standard XDG configuration directory. On upgrade, the player copies missing
settings and imported MilkDrop presets from `~/.config/xmp-module-player/`.
Existing files in the new folder take precedence. The old folder remains
available for earlier releases. IRC uses `~/.config/xmp-irc/`.

The launcher icon is bundled inside the AppImage and is also available in
the repository as [`icons/modulez-player.png`](icons/modulez-player.png).

Version 1.4.3 adds UADE fallback playback, seeking, and WAV export for
additional Amiga music formats. The AppImage bundles UADE's core, libraries,
player data, and the default MIDI SoundFont with its license. The About tab
now lists all six playback engines and the main features.

The 1.5 AppImage was tested locally for playback, engine switching,
seeking, WAV export, and clean exit. Packaging verification checked the
bundled UADE assets and compared the SoundFont and license with the originals.

Version 1.4.2 fixes playlist navigation, makes Play start the first playlist track, and adds recursive folder loading.

Version 1.4.1 adds a high-contrast system tray icon, playback controls,
optional minimize-to-tray behavior, and a more readable track tooltip.

Version 1.4 integrates an IRC client in the player's existing GTK interface.
It includes TLS connections, multiple channels, private-message tabs, and an
EsperNet `#modulez` preset. IRC connections are initiated manually.

This project is actively being developed. Features and compatibility may change as development continues.

## System tray and preferences

Modulez Player shows a tray icon on desktops with a GTK3-compatible notification area.
Click the icon to bring the player back; right-click for playback controls,
current-track information, and **Quit Modulez Player**. The tray icon uses a bright
white version of the Modulez mark for visibility on dark panels. Its excess
transparent margins are cropped so the logo fills the tray icon slot. Previous and
Next in the tray menu move between playlist files, wrapping at either end;
they do not skip positions within a tracker module. If nothing is loaded,
Play in the tray menu starts the first file in the playlist. The tooltip
shows the current track title in bold white text against a charcoal surface.

The **Preferences** tab includes **Minimize to tray when closing Modulez Player**,
enabled by default. When checked, closing the main window keeps the player
running in the tray. Uncheck it to make the close button quit normally.
This choice is saved in `~/.config/modulez-player/preferences.ini`.
If there is no usable tray, closing the window always exits normally.

## Author

**David (DaSteele) Harryman**

## License

Licensing information for the application source

<details>
<summary><strong>View 10 screenshots</strong></summary>

Click a thumbnail to view the full screenshot.

<p>
  <a href="docs/screenshots/1-player.png"><img src="docs/screenshots/thumbnails/1-player.png" width="160" alt="Player" title="Player"></a>
  <a href="docs/screenshots/2-playlist.png"><img src="docs/screenshots/thumbnails/2-playlist.png" width="160" alt="Playlist" title="Playlist"></a>
  <a href="docs/screenshots/3-samples.png"><img src="docs/screenshots/thumbnails/3-samples.png" width="160" alt="Samples" title="Samples"></a>
  <a href="docs/screenshots/4-soundfonts.png"><img src="docs/screenshots/thumbnails/4-soundfonts.png" width="160" alt="SoundFonts" title="SoundFonts"></a>
  <a href="docs/screenshots/5-eq.png"><img src="docs/screenshots/thumbnails/5-eq.png" width="160" alt="Equalizer" title="Equalizer"></a>
  <a href="docs/screenshots/6-hvsc.png"><img src="docs/screenshots/thumbnails/6-hvsc.png" width="160" alt="HVSC" title="HVSC"></a>
  <a href="docs/screenshots/7-irc.png"><img src="docs/screenshots/thumbnails/7-irc.png" width="160" alt="IRC" title="IRC"></a>
  <a href="docs/screenshots/8-vis.png"><img src="docs/screenshots/thumbnails/8-vis.png" width="160" alt="Visualizer 1" title="Visualizer 1"></a>
  <a href="docs/screenshots/9-vis.png"><img src="docs/screenshots/thumbnails/9-vis.png" width="160" alt="Visualizer 2" title="Visualizer 2"></a>
  <a href="docs/screenshots/10-vis.png"><img src="docs/screenshots/thumbnails/10-vis.png" width="160" alt="Visualizer 3" title="Visualizer 3"></a>
</p>

</details>

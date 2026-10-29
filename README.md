# XMP Module Player

A native Linux music player built with **GTK3, SDL2, libxmp, and FFmpeg**.

XMP Module Player brings classic tracker music and regular audio playback to the Linux desktop, with a graphical interface, real-time audio visualization, and detailed module and sample information.

![XMP Module Player running on Linux](Screenshot_2026-10-26_19-02-39.png)

## Features

- 🎵 **Tracker module playback** powered by libxmp
- 🎧 **Regular audio playback** powered by FFmpeg: MP3, WAV, M4A/AAC, Ogg Vorbis, Opus, and FLAC
- 🎵 **Commodore 64 SID playback** powered by libsidplayfp
- 🎵 **ZX Spectrum AY/YM playback** through bundled ZXTune, including PT3, AY, VTX, YM, STC, and SQT
- 💾 **Offline WAV export** for supported audio formats, with cancellation and overwrite confirmation
- 🎚️ **Playback controls** with seeking and track timing
- 📃 **Playlist management** with add, remove, clear, and drag-and-drop support
- 📃 **High Voltage SID Collection (HVSC) browsing and integrated downloader**
- 🔁 **Automatic track advancement**
- 📂 **M3U playlist loading and saving**
- 📊 **Real-time audio spectrum visualizer** with peak hold
- 🌊 **Channel scopes** for visualizing audio activity
- 🧩 **Pattern viewer** for tracker data
- 🔊 **Sample browser** with waveform visualization, sample details, loop information, and module comments
- 📂 Offline WAV export: Render supported music to WAV without waiting through real-time playback.
- 🖥️ **Native GTK3 interface** with SDL2 audio output

## Download

Download the latest Linux AppImage from the project's **[Releases](../../releases)** page.

### Running the AppImage

1. Download `XMP_Module_Player-x86_64.AppImage`.
2. Open a terminal in the download folder.
3. Make it executable and launch it:

   ```bash
   chmod +x XMP_Module_Player-x86_64.AppImage
   ./XMP_Module_Player-x86_64.AppImage
   ```

## Requirements

The AppImage bundles many of its required libraries. Compatibility with Linux distributions other than the development system has not yet been fully tested.

### ZX Spectrum playback

XMP Module Player 1.1 includes native ZX Spectrum playback powered
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

Developers compiling XMP Module Player from source must first build
ZXTune's static libraries:

```bash
make platform=linux system.zlib=1 -C ~/zxtune/apps/zxtune123 -j4
```

Then compile XMP Module Player:

```bash
make -j4
./xmp-linux
```

The optional legacy `zxtune123` WAV-based fallback is not included
in the distributed AppImage.

## Built With

- [GTK 3](https://www.gtk.org/) — graphical interface
- [SDL 2](https://www.libsdl.org/) — audio output
- [libxmp](https://github.com/libxmp/libxmp) — tracker module playback
- [FFmpeg](https://ffmpeg.org/) — decoding regular audio formats
- [ZXTune](https://github.com/vitamin-caig/zxtune) — native ZX Spectrum and chiptune playback

## Project Status

**Current version: 1.1**

This project is actively being developed. Features and compatibility may change as development continues.

## Author

**David (DaSteele) Harryman**

## License

Licensing information for the application source

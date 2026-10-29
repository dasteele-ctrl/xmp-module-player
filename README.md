# XMP Module Player

A native Linux music player built with **GTK3, SDL2, libxmp, and FFmpeg**.

XMP Module Player brings classic tracker music and regular audio playback to the Linux desktop, with a graphical interface, real-time audio visualization, and detailed module and sample information.

![XMP Module Player running on Linux](Screenshot_2026-10-26_19-02-39.png)

## Features

- 🎵 **Tracker module playback** powered by libxmp
- 🎧 **Regular audio playback** powered by FFmpeg: MP3, WAV, M4A/AAC, Ogg Vorbis, Opus, and FLAC
- 🎵 **ZX Spectrum AY/YM playback** through the optional ZXTune renderer, including PT3, AY, VTX, YM, STC, and SQT
- 🎚️ **Playback controls** with seeking and track timing
- 📃 **Playlist management** with add, remove, clear, and drag-and-drop support
- 🔁 **Automatic track advancement**
- 📂 **M3U playlist loading and saving**
- 📊 **Real-time audio spectrum visualizer** with peak hold
- 🌊 **Channel scopes** for visualizing audio activity
- 🧩 **Pattern viewer** for tracker data
- 🔊 **Sample browser** with waveform visualization, sample details, loop information, and module comments
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

### ZX Spectrum playback (development source builds)

The standard `xmp-linux` build uses ZXTune's native C++ renderer for ZX
Spectrum and related chiptune formats. It sends decoded stereo PCM directly
to the SDL2 audio engine without spawning a separate player or creating a
temporary WAV file. Seeking, volume, spectrum, waveform, and playlist controls
work through the normal player interface.

Build ZXTune's static libraries first (the `zxtune123` target builds them):

```bash
make platform=linux system.zlib=1 -C ~/zxtune/apps/zxtune123 -j4
```

Then build and launch the native player:

```bash
make -j4
./xmp-linux
```

The integration recognizes `.pt3`, `.pt2`, `.ay`, `.ym`, `.vtx`, `.stc`,
`.sqt`, `.psg`, and other supported formats. If native decoding cannot open
a track, the player may fall back to `zxtune123` and a temporary WAV; it
looks for that executable in `PATH` or
`~/zxtune/bin/linux/release/zxtune123`.

To build the older WAV-based executable separately, use
`make -j4 xmp-zx-wav`. Tracker pattern display remains specific to formats
supported by the libxmp pattern viewer; it is not provided for ZX files.
Existing AppImage releases may not include the native ZXTune backend yet.

## Built With

- [GTK 3](https://www.gtk.org/) — graphical interface
- [SDL 2](https://www.libsdl.org/) — audio output
- [libxmp](https://github.com/libxmp/libxmp) — tracker module playback
- [FFmpeg](https://ffmpeg.org/) — decoding regular audio formats
- [ZXTune](https://github.com/vitamin-caig/zxtune) — native ZX Spectrum/chiptune playback (source builds)

## Project Status

**Early development — version 0.2.0**

This project is actively being developed. Features and compatibility may change as development continues.

## Author

**David (DaSteele) Harryman**

## License

Licensing information for the application source

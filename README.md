# XMP Module Player

A native Linux music player built with **GTK3, SDL2, libxmp, and FFmpeg**.

XMP Module Player brings classic tracker music and regular audio playback to the Linux desktop, with a graphical interface, real-time audio visualization, and detailed module and sample information.

![XMP Module Player running on Linux](Screenshot_2026-10-26_19-02-39.png)

## Features

- 🎵 **Tracker module playback** powered by libxmp
- 🎧 **Regular audio playback** powered by FFmpeg: MP3, WAV, M4A/AAC, Ogg Vorbis, Opus, and FLAC
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

## Built With

- [GTK 3](https://www.gtk.org/) — graphical interface
- [SDL 2](https://www.libsdl.org/) — audio output
- [libxmp](https://github.com/libxmp/libxmp) — tracker module playback
- [FFmpeg](https://ffmpeg.org/) — decoding regular audio formats

## Project Status

**Early development — version 0.2.0**

This project is actively being developed. Features and compatibility may change as development continues.

## Author

**David (DaSteele) Harryman**

## License

Licensing information for the application source

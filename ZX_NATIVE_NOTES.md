# Native ZX Spectrum playback

The standard `xmp-linux` executable now uses ZXTune's C++ renderer directly,
generating stereo PCM into the existing SDL2 audio ring without launching
ZXTune or writing temporary files for files it can decode. The older
`zxtune123` -> WAV implementation remains available as a fallback.

## Build

First, build ZXTune's command-line player so that its static libraries exist:

```bash
make platform=linux system.zlib=1 -C ~/zxtune/apps/zxtune123 -j4
```

Then, inside your XMP Module Player source directory:

```bash
make -j4
./xmp-linux
```

If ZXTune lives elsewhere, pass `ZX_ROOT=/path/to/zxtune` to `make`.
`make -j4 xmp-zx-native` also builds the native version under its earlier
name. Use `make -j4 xmp-zx-wav` to build the original WAV-based player
separately. The native executable links the static libraries built by ZXTune,
so it may be larger than the legacy executable.

## Test

Open a known-good `.pt3` file, preferably the same one used in the native
`zxpcm-probe` test. Check Play, Pause, Stop/Play restart, seek, waveform,
playlist advancement, switching to MP3/SID/XMP, and closing the window.

If the native renderer fails to open a ZX tune, the player tries its original
`zxtune123` -> WAV workflow, provided that CLI is available. The XMP
tracker/SID/FFmpeg paths are otherwise unchanged. ZX formats do not display
tracker patterns in the libxmp pattern viewer.

Native ZX playback is working in development source builds, but its static
library dependencies and AppImage packaging still need verification before
a general release.

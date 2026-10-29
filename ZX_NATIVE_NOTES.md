# Experimental native ZX Spectrum playback

The normal `xmp-linux` executable remains unchanged: it uses the established
`zxtune123` to render temporary WAV files. The optional `xmp-zx-native` build
uses ZXTune's C++ renderer directly, generating stereo PCM into the existing
SDL2 audio ring without launching ZXTune or writing temporary files.

## Build

First, build ZXTune's command-line player so that its static libraries exist:

```bash
make platform=linux system.zlib=1 -C ~/zxtune/apps/zxtune123 -j4
```

Then, inside your XMP Module Player source directory:

```bash
make -j4 xmp-zx-native
./xmp-zx-native
```

If ZXTune lives elsewhere, pass `ZX_ROOT=/path/to/zxtune` to `make`.
`xmp-zx-native` is a separate executable; neither the original binary nor its
source has to be deleted or replaced. The native build links against the static
libraries already built by ZXTune, so it may be larger than `xmp-linux`.

## Test

Open a known-good `.pt3` file, preferably the same one used in the native
`zxpcm-probe` test. Check Play, Pause, Stop/Play restart, seek, waveform,
playlist advancement, switching to MP3/SID/XMP, and closing the window.

If the native renderer fails to open a ZX tune, the player tries its original
`zxtune123` -> WAV workflow. The XMP tracker/SID/FFmpeg paths are otherwise
unchanged.

This is a Linux proof of concept, not a packaged AppImage release. The helper
library ABI and AppImage bundling need additional work before general release.

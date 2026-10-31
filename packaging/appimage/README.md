# AppImage packaging (local test)

This recipe packages the already-built `xmp-linux` and `xmp-zx-helper`
executables with `linuxdeploy` and the experimental GTK bundling plugin. It is an initial
local test, not a distribution-ready release.

1. Build the player from the repository root: `make -j4`.
2. Download `linuxdeploy-x86_64.AppImage` from
   <https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage>
   into `~/Downloads/appimage-tools/`.
3. Download `linuxdeploy-plugin-gtk.sh` from
   <https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh>
   into that same folder. Make both tools executable.
4. Run `bash packaging/appimage/build.sh` from the repository root.

The resulting version 1.4.2 AppImage is written under `dist/` with a timestamp in
its filename (`XMP_Module_Player-1.4.2-x86_64-*.AppImage`). This script does not
overwrite an existing AppImage.

GTK plugins and resources can be loaded dynamically, so `ldd` alone does
not capture all runtime dependencies. Test the AppImage with ZX, tracker,
SID, and FFmpeg-supported formats, and verify the file picker and desktop
theme. For version 1.4.2, test the tray icon, minimize-to-tray preference,
playlist controls, and high-contrast tooltip on an Xfce panel. Also test the
IRC tab's TLS connection to EsperNet:
the GTK GIO TLS backend and certificate handling may be dynamically loaded.
ZXTune is linked statically into `xmp-zx-helper`, not the main player.
The two executables communicate directly over a local PCM stream, so the
system libsidplayfp in `xmp-linux` cannot conflict with ZXTune's bundled
libsidplayfp. Both executables and their runtime dependencies are included
in the AppImage: end users do not need ZXTune source or a compiler. The
optional command-line `zxtune123` WAV fallback is not included.

AppImages built on a new distribution may not run on older distributions,
even if most shared libraries are bundled. Build eventual releases on the
oldest supported target distribution and test on a separate system. Audit
licenses and redistribution requirements for all included components before
publishing.

MIDI playback links against FluidSynth. Check that linuxdeploy gathers
`libfluidsynth` and its codecs, and test the **SoundFonts** tab with both SF2
and SF3 before releasing. A SoundFont is not bundled by this recipe yet;
users can select a system-installed or personal SoundFont. Any future bundled
font requires separate license review and an appropriate in-AppDir path.

To include a reviewed SF2 or SF3 without downloading it automatically, set
these environment variables when invoking the build script:

```bash
XMP_BUNDLED_SOUNDFONT="$HOME/Music/SoundFonts/MS Basic.sf3" \
XMP_BUNDLED_SOUNDFONT_LICENSE="$HOME/Music/SoundFonts/MS Basic_License.md" \
bash packaging/appimage/build.sh
```

The script refuses to bundle a font without its supplied license notice.
It stages the selected bank as `default-GM.sf2` or `default-GM.sf3` inside
`usr/share/sounds/sf2/`, where XMP detects it on first launch. The original
file is not moved or modified. Review all applicable license and attribution
terms for the exact SoundFont and version before distributing the AppImage.

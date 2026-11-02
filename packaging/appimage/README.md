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

The resulting version 1.4.3 AppImage is written under `dist/` with a timestamp in
its filename (`XMP_Module_Player-1.4.3-x86_64-*.AppImage`). This script does not
overwrite an existing AppImage.

GTK plugins and resources can be loaded dynamically, so `ldd` alone does
not capture all runtime dependencies. Test the AppImage with ZX, tracker,
SID, and FFmpeg-supported formats, and verify the file picker and desktop
theme. For version 1.4.3, test the tray icon, minimize-to-tray preference,
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

MIDI playback links against FluidSynth. The default SoundFont is required for
this recipe. The default source paths are:

- `~/Music/SoundFonts/MS Basic.sf3`
- `~/Music/SoundFonts/MS Basic_License.md`

The build fails before staging if either is missing. Override them if needed:

```bash
XMP_BUNDLED_SOUNDFONT="$HOME/Music/SoundFonts/MS Basic.sf3" \
XMP_BUNDLED_SOUNDFONT_LICENSE="$HOME/Music/SoundFonts/MS Basic_License.md" \
bash packaging/appimage/build.sh
```

The font and its exact supplied license are copied into
`usr/share/sounds/sf2/default-GM.sf3` (or `.sf2`) and
`usr/share/doc/xmp-module-player/soundfonts/SOUNDFONT-LICENSE.txt`.
No font is downloaded and the original files are not modified.

UADE's core is staged as `usr/bin/uadecore`, with its data in
`usr/share/uade`. The build defaults to `/usr/local/lib/uade/uadecore` and
`/usr/local/share/uade`; `XMP_UADE_CORE` and `XMP_UADE_BASE_DIR` can override
these source paths. All runtime data is copied, including players, score,
uaerc, uade.conf, and eagleplayer.conf. linuxdeploy collects the core's
libraries and the player's linked libuade, libbencodetools, and FluidSynth.

Inside an AppImage, the UADE adapter resolves its core/data relative to
APPDIR, including explicit bundled score/uaerc paths. A source-tree build
continues to use its installed UADE runtime.

After compression, the script extracts the finished AppImage into its
private temporary directory. It verifies executable/data/library presence
and compares the SoundFont and license byte for byte with their originals.
The temporary directory is removed when the build finishes.

Test the final AppImage with UADE music (including paired mdat/smpl files),
seeking, WAV export, MIDI using the bundled font, MOD, SID and ZX playback,
and a clean exit. Verify on a machine or environment without system UADE
and without a system SoundFont before calling the build portable. An
existing saved SoundFont selection may take priority over the bundled bank.

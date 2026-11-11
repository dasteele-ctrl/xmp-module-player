#!/usr/bin/env bash
# Modulez Player v1.7.5 AppImage: WebKit runtime, bundled visualizer assets, UADE and SoundFont.
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "$script_dir/../.." && pwd)"
tool_dir="${XMP_APPIMAGE_TOOLS_DIR:-$HOME/Downloads/appimage-tools}"
linuxdeploy="$tool_dir/linuxdeploy-x86_64.AppImage"
gtk_plugin="$tool_dir/linuxdeploy-plugin-gtk.sh"
dist_dir="$project_dir/dist"

if [[ ! -x "$project_dir/modulez-player" ]]; then
    echo "Missing modulez-player. Build the native player with: make -j4" >&2
    exit 1
fi
if ! grep -aFq 'Modulez Player 1.7.5' "$project_dir/modulez-player"; then
    echo "The compiled player does not report version 1.7.5. Run make -j4 modulez-player first." >&2
    exit 1
fi
if [[ ! -x "$project_dir/xmp-zx-helper" ]]; then
    echo "Missing xmp-zx-helper. Build both executables with: make -j4" >&2
    exit 1
fi
if [[ ! -x "$linuxdeploy" || ! -x "$gtk_plugin" ]]; then
    echo "AppImage packaging tools are missing or not executable in $tool_dir" >&2
    echo "Download linuxdeploy-x86_64.AppImage and linuxdeploy-plugin-gtk.sh," >&2
    echo "then make both executable and rerun this script." >&2
    exit 1
fi

# WebKit needs subprocesses and an injected bundle in addition to its .so.
for command in pkg-config python3 patchelf gresource zsyncmake; do
    command -v "$command" >/dev/null || { echo "Missing build tool: $command" >&2; exit 1; }
done
pipewire_libdir="$(pkg-config --variable=libdir libpipewire-0.3)"
pipewire_library="$pipewire_libdir/libpipewire-0.3.so.0"
if [[ ! -f "$pipewire_library" ]]; then
    echo "Missing PipeWire runtime library: $pipewire_library" >&2
    exit 1
fi
webkit_libdir="$(pkg-config --variable=libdir webkit2gtk-4.1)"
webkit_helper_dir="$webkit_libdir/webkit2gtk-4.1"
if [[ ! -x "$webkit_helper_dir/WebKitWebProcess" ]]; then
    webkit_helper_dir=/usr/libexec/webkit2gtk-4.1
fi
case "$webkit_helper_dir" in
    /usr/*) ;;
    *) echo "Unsupported WebKit helper location: $webkit_helper_dir" >&2; exit 1 ;;
esac
webkit_utilities=()
webkit_process_names=()
for process_path in "$webkit_helper_dir"/WebKit*Process; do
    [[ -x "$process_path" ]] || continue
    webkit_utilities+=(--executable "$process_path")
    webkit_process_names+=("${process_path##*/}")
done
for required in WebKitWebProcess WebKitNetworkProcess; do
    [[ -x "$webkit_helper_dir/$required" ]] || { echo "Missing WebKit helper: $required" >&2; exit 1; }
done
injected_bundle="$webkit_helper_dir/injected-bundle/libwebkit2gtkinjectedbundle.so"
[[ -f "$injected_bundle" ]] || { echo "Missing injected WebKit bundle" >&2; exit 1; }
webkit_utilities+=(--library "$injected_bundle")
for sandbox_tool in bwrap xdg-dbus-proxy; do
    sandbox_path="$(command -v "$sandbox_tool" || true)"
    [[ -x "$sandbox_path" ]] || { echo "Install bubblewrap and xdg-dbus-proxy" >&2; exit 1; }
    webkit_utilities+=(--executable "$sandbox_path")
done
for packaging_file in relocate-webkit.py vis-AppRun; do
    [[ -f "$script_dir/$packaging_file" ]] || { echo "Missing packaging helper: $packaging_file" >&2; exit 1; }
done

# The HVSC downloader uses these executables at runtime. Bundle curl and the
# standalone 7zz (rather than plugin-dependent 7z) with their ELF dependencies.
hvsc_utilities=()
for utility_name in curl 7zz; do
    utility_path="$(command -v "$utility_name" || true)"
    # Ubuntu's /usr/bin/7zz is a shell wrapper; linuxdeploy needs the ELF.
    if [[ "$utility_name" == 7zz && -x /usr/lib/7zip/7zz ]]; then
        utility_path=/usr/lib/7zip/7zz
    fi
    if [[ ! -f "$utility_path" || ! -x "$utility_path" ]]; then
        echo "Missing $utility_name on the build machine. On Mint/Ubuntu: sudo apt install curl 7zip-standalone" >&2
        exit 1
    fi
    hvsc_utilities+=(--executable "$utility_path")
done

# Validate required assets before collecting any libraries. The SoundFont
# defaults match the existing, reviewed MS Basic bank used for earlier builds.
# Locate the bundled SoundFont and its license.
# Explicit environment variables always take priority.
soundfont_dir="$HOME/Projects/XMP Dev Misc Files"

if [[ ! -s "$soundfont_dir/MS Basic.sf3" ||
      ! -s "$soundfont_dir/MS Basic_License.md" ]]; then
    soundfont_dir="$HOME/Music/SoundFonts"
fi

source_font="${XMP_BUNDLED_SOUNDFONT:-$soundfont_dir/MS Basic.sf3}"
source_notice="${XMP_BUNDLED_SOUNDFONT_LICENSE:-$soundfont_dir/MS Basic_License.md}"
if [[ ! -s "$source_font" || ! -s "$source_notice" ]]; then
    echo "The bundled SoundFont and its license are required." >&2
    echo "SoundFont: $source_font" >&2
    echo "License: $source_notice" >&2
    echo "Set XMP_BUNDLED_SOUNDFONT and XMP_BUNDLED_SOUNDFONT_LICENSE if they are elsewhere." >&2
    exit 1
fi
case "${source_font,,}" in
    *.sf2) bundled_font="default-GM.sf2" ;;
    *.sf3) bundled_font="default-GM.sf3" ;;
    *) echo "Bundled SoundFont must have an .sf2 or .sf3 extension." >&2; exit 1 ;;
esac

uade_core="${XMP_UADE_CORE:-/usr/local/lib/uade/uadecore}"
uade_data_dir="${XMP_UADE_BASE_DIR:-/usr/local/share/uade}"
if [[ ! -x "$uade_core" || "${uade_core##*/}" != uadecore ]]; then
    echo "Missing UADE core: $uade_core (set XMP_UADE_CORE if needed)." >&2
    exit 1
fi
for data_file in score uaerc uade.conf eagleplayer.conf; do
    if [[ ! -s "$uade_data_dir/$data_file" ]]; then
        echo "Missing UADE runtime file: $uade_data_dir/$data_file" >&2
        exit 1
    fi
done
if [[ ! -d "$uade_data_dir/players" ]] ||
   [[ -z "$(find "$uade_data_dir/players" -type f -print -quit)" ]]; then
    echo "Missing UADE player data: $uade_data_dir/players" >&2
    exit 1
fi

mkdir -p -- "$dist_dir"
build_root="$(mktemp -d /tmp/xmp-appimage.XXXXXXXX)"
appdir="$build_root/Modulez_Player.AppDir"
output_name="Modulez-Player-v1.7.5-x86_64.AppImage"

cleanup() {
    if [[ -n "${build_root:-}" && -d "$build_root" &&
          "$build_root" == /tmp/xmp-appimage.* ]]; then
        rm -r -- "$build_root"
    fi
}
trap cleanup EXIT

if [[ -e "$dist_dir/$output_name" || -e "$dist_dir/$output_name.zsync" ]]; then
    echo "Output file already exists: $dist_dir/$output_name" >&2
    exit 1
fi

(
    cd -- "$dist_dir"
    LDAI_OUTPUT="$output_name" "$linuxdeploy" \
        --appdir "$appdir" \
        --executable "$project_dir/modulez-player" \
        --executable "$project_dir/xmp-zx-helper" \
        --executable "$uade_core" \
        --library "$pipewire_library" \
        "${hvsc_utilities[@]}" \
        "${webkit_utilities[@]}" \
        --desktop-file "$script_dir/modulez-player.desktop" \
        --icon-file "$project_dir/icons/modulez-player.png" \
        --plugin gtk
)

# hvsc_download.c looks for this helper inside $APPDIR when launched as an
# AppImage. Without it, HVSC installation works only in a source-tree build.
hvsc_installer="$project_dir/hvsc-install-safe.sh"
if [[ ! -f "$hvsc_installer" ]]; then
    echo "Missing HVSC installer script: $hvsc_installer" >&2
    exit 1
fi
install -D -m 0755 "$hvsc_installer" \
    "$appdir/usr/share/xmp-module-player/hvsc-install-safe.sh"

# Include every UADE data file, following symlinks so no runtime asset points
# outside the AppImage. linuxdeploy collects uadecore's ELF dependencies too.
mkdir -p -- "$appdir/usr/share/uade"
cp -aL -- "$uade_data_dir/." "$appdir/usr/share/uade/"

install -D -m 0644 "$source_font" \
    "$appdir/usr/share/sounds/sf2/$bundled_font"
install -D -m 0644 "$source_notice" \
    "$appdir/usr/share/doc/xmp-module-player/soundfonts/SOUNDFONT-LICENSE.txt"
echo "Bundled SoundFont: $source_font ($bundled_font)"

# Keep the collected/patched ELF helpers under WebKit's expected runtime tree.
bundled_webkit_dir="$appdir$webkit_helper_dir"
mkdir -p -- "$bundled_webkit_dir/injected-bundle"
for name in "${webkit_process_names[@]}"; do
    install -m 0755 "$appdir/usr/bin/$name" "$bundled_webkit_dir/$name"
done
install -m 0644 "$appdir/usr/lib/libwebkit2gtkinjectedbundle.so" \
    "$bundled_webkit_dir/injected-bundle/libwebkit2gtkinjectedbundle.so"
# Preserve any non-executable runtime data supplied by this WebKit package.
for entry in "$webkit_helper_dir"/*; do
    [[ -e "$entry" ]] || continue
    [[ "${entry##*/}" == injected-bundle ]] && continue
    if [[ -d "$entry" || ! -x "$entry" ]]; then cp -aL -- "$entry" "$bundled_webkit_dir/"; fi
done
for data_dir in /usr/share/webkitgtk-4.1 /usr/share/webkit2gtk-4.1; do
    if [[ -d "$data_dir" ]]; then mkdir -p -- "$appdir${data_dir%/*}"; cp -aL -- "$data_dir" "$appdir${data_dir%/*}/"; fi
done
python3 "$script_dir/relocate-webkit.py" "$appdir" --source-dir "$webkit_helper_dir"
install -D -m 0644 /dev/null "$appdir/usr/share/modulez-player/webkit-helper-dir"
printf '%s\n' "$webkit_helper_dir" > "$appdir/usr/share/modulez-player/webkit-helper-dir"
[[ -e "$appdir/AppRun" ]] || { echo "linuxdeploy did not create AppRun" >&2; exit 1; }
mv -- "$appdir/AppRun" "$appdir/AppRun.modulez"
install -m 0755 "$script_dir/vis-AppRun" "$appdir/AppRun"
# Preserve visualizer notices alongside the existing dependency notices.
notice_dir="$appdir/usr/share/doc/xmp-module-player/visualizer"
mkdir -p -- "$notice_dir/projectm"
cp -a -- "$project_dir/visualizer/upstream" "$notice_dir/"
for notice in COPYING LICENSE.txt BUILD-SOURCES.txt; do
    install -m 0644 "$project_dir/visualizer/projectm/$notice" "$notice_dir/projectm/$notice"
done

verify_bundle() {
    local bundle="$1"
    local asset library
    for asset in usr/bin/modulez-player usr/bin/xmp-zx-helper usr/bin/uadecore \
        usr/share/applications/modulez-player.desktop \
        usr/share/uade/score usr/share/uade/uaerc usr/share/uade/uade.conf \
        usr/share/uade/eagleplayer.conf; do
        if [[ ! -s "$bundle/$asset" ]]; then
            echo "Bundle verification failed: missing $asset" >&2
            return 1
        fi
    done
    if [[ -z "$(find "$bundle/usr/share/icons" -type f -name modulez-player.png -print -quit)" ]]; then
        echo "Bundle verification failed: missing Modulez Player icon." >&2
        return 1
    fi
    if [[ ! -x "$bundle/usr/bin/uadecore" ]] ||
       [[ -z "$(find "$bundle/usr/share/uade/players" -type f -print -quit)" ]]; then
        echo "Bundle verification failed: UADE core or player data missing." >&2
        return 1
    fi
    for library in libuade libbencodetools libfluidsynth libwebkit2gtk-4.1 libjavascriptcoregtk-4.1 libsoup-3.0 libpipewire-0.3; do
        if [[ -z "$(find "$bundle/usr" -type f -name "$library.so*" -print -quit)" ]]; then
            echo "Bundle verification failed: missing $library." >&2
            return 1
        fi
    done
    python3 "$script_dir/relocate-webkit.py" "$bundle" --source-dir "$webkit_helper_dir" --verify
    grep -q 'MODULEZ_WEBKIT_RELOCATION_V1' "$bundle/AppRun"
    [[ -e "$bundle/AppRun.modulez" ]]
    [[ -x "$bundle/usr/libexec/modulez/install-update" ]]
    "$bundle/usr/libexec/modulez/zsync2.AppImage" --appimage-extract-and-run --version
    local resources
    resources="$(gresource list "$bundle/usr/bin/modulez-player")"
    for asset in index.html viewer.js wavescope.js presets.js projectm-adapter.js projectm/projectm.js projectm/projectm.wasm; do
        grep -Fxq "/org/modulez/visualizer/$asset" <<< "$resources" || { echo "Missing embedded visualizer asset: $asset" >&2; return 1; }
    done
    cmp -- "$source_font" "$bundle/usr/share/sounds/sf2/$bundled_font"
    cmp -- "$source_notice" \
        "$bundle/usr/share/doc/xmp-module-player/soundfonts/SOUNDFONT-LICENSE.txt"
}
# Ship the upstream HTTPS-capable client intact, isolated from player libraries.
updater_name=zsync2-92-fc62ae7-x86_64.AppImage
updater="$tool_dir/$updater_name"
if [[ ! -f "$updater" ]]; then
    updater="$build_root/$updater_name"
    curl --fail --location --retry 2 --output "$updater" \
        "https://github.com/AppImageCommunity/zsync2/releases/download/continuous/$updater_name"
fi
updater_digest=c1d448722ae3552ebc0c80d5ebd05272dd371f0218328bff6142ebc8f9d46381
[[ "$(sha256sum < "$updater")" == "$updater_digest  -" ]] || { echo "Unexpected zsync2 checksum" >&2; exit 1; }
install -Dm755 "$updater" "$appdir/usr/libexec/modulez/zsync2.AppImage"
install -m755 "$script_dir/install-update.sh" "$appdir/usr/libexec/modulez/install-update"
verify_bundle "$appdir"

# Stage the notices after linuxdeploy has collected libraries, but before
# producing the compressed AppImage.
bash "$script_dir/bundle-licenses.sh" "$appdir" "$project_dir"

# Run the output plugin directly: a second linuxdeploy deployment pass
# replaces AppRun and can undo the custom WebKit launcher.
mkdir -p -- "$build_root/packaging-tools"
( cd -- "$build_root/packaging-tools"; "$linuxdeploy" --appimage-extract > extraction.log 2>&1 )
output_plugin="$(find "$build_root/packaging-tools/squashfs-root" -type f -name linuxdeploy-plugin-appimage -perm /111 -print -quit)"
[[ -n "$output_plugin" ]] || { echo "Bundled AppImage output plugin not found." >&2; exit 1; }
update_info='gh-releases-zsync|dasteele-ctrl|xmp-module-player|latest|Modulez-Player-v*-x86_64.AppImage.zsync'
(
    cd -- "$dist_dir"
    LDAI_UPDATE_INFORMATION="$update_info" \
    ARCH=x86_64 LDAI_OUTPUT="$output_name" "$output_plugin" --appdir "$appdir"
)

# Use the classic writer, including the final partial block. Check its whole-file
# digest against the exact release asset before either file is distributed.
(
    cd -- "$dist_dir"
    [[ "$("./$output_name" --appimage-updateinformation)" == "$update_info" ]]
    zsyncmake -u "$output_name" -o "$build_root/release.zsync" "$output_name"
    digest=$(LC_ALL=C sed -n '/^$/q; s/^SHA-1: //p' "$build_root/release.zsync")
    [[ "$digest" == "$(sha1sum "$output_name" | cut -d' ' -f1)" ]]
    mv -- "$build_root/release.zsync" "$output_name.zsync"
)

# Inspect the compressed result, not just its staging directory. This does
# not start the player or require FUSE. The exit trap removes extracted bytes.
mkdir -p -- "$build_root/verification"
(
    cd -- "$build_root/verification"
    "$dist_dir/$output_name" --appimage-extract > "$build_root/extraction.log" 2>&1
)
verify_bundle "$build_root/verification/squashfs-root"
echo "Verified: WebKit helpers/relocation, embedded Vis assets, UADE runtime and exact SoundFont."

install -m 0644 "$project_dir/icons/modulez-player.png" "$dist_dir/modulez-player.png"
echo "Launcher icon: $dist_dir/modulez-player.png"
echo "AppImage built: $dist_dir/$output_name"
echo "Test it locally before distributing it to other machines."

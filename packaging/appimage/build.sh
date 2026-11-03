#!/usr/bin/env bash
# Modulez Player v1.5 AppImage with the UADE runtime and the required default SoundFont.
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
source_font="${XMP_BUNDLED_SOUNDFONT:-$HOME/Music/SoundFonts/MS Basic.sf3}"
source_notice="${XMP_BUNDLED_SOUNDFONT_LICENSE:-$HOME/Music/SoundFonts/MS Basic_License.md}"
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
output_name="Modulez-Player-v1.5-Linux-x86_64-$(date +%Y%m%d-%H%M%S).AppImage"

cleanup() {
    if [[ -n "${build_root:-}" && -d "$build_root" &&
          "$build_root" == /tmp/xmp-appimage.* ]]; then
        rm -r -- "$build_root"
    fi
}
trap cleanup EXIT

if [[ -e "$dist_dir/$output_name" ]]; then
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
        "${hvsc_utilities[@]}" \
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
    for library in libuade libbencodetools libfluidsynth; do
        if [[ -z "$(find "$bundle/usr" -type f -name "$library.so*" -print -quit)" ]]; then
            echo "Bundle verification failed: missing $library." >&2
            return 1
        fi
    done
    cmp -- "$source_font" "$bundle/usr/share/sounds/sf2/$bundled_font"
    cmp -- "$source_notice" \
        "$bundle/usr/share/doc/xmp-module-player/soundfonts/SOUNDFONT-LICENSE.txt"
}
verify_bundle "$appdir"

# Stage the notices after linuxdeploy has collected libraries, but before
# producing the compressed AppImage.
bash "$script_dir/bundle-licenses.sh" "$appdir" "$project_dir"

(
    cd -- "$dist_dir"
    LDAI_OUTPUT="$output_name" "$linuxdeploy" \
        --appdir "$appdir" \
        --output appimage
)

if [[ ! -f "$dist_dir/$output_name" ]]; then
    echo "The packaging tool did not create the expected AppImage." >&2
    exit 1
fi

# Inspect the compressed result, not just its staging directory. This does
# not start the player or require FUSE. The exit trap removes extracted bytes.
mkdir -p -- "$build_root/verification"
(
    cd -- "$build_root/verification"
    "$dist_dir/$output_name" --appimage-extract > "$build_root/extraction.log" 2>&1
)
verify_bundle "$build_root/verification/squashfs-root"
echo "Verified: bundled UADE core/data/libraries and the exact SoundFont and license."

install -m 0644 "$project_dir/icons/modulez-player.png" "$dist_dir/modulez-player.png"
echo "Launcher icon: $dist_dir/modulez-player.png"
echo "AppImage built: $dist_dir/$output_name"
echo "Test it locally before distributing it to other machines."

#!/usr/bin/env bash
# First local AppImage packaging test. Run only after building xmp-linux.
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "$script_dir/../.." && pwd)"
tool_dir="${XMP_APPIMAGE_TOOLS_DIR:-$HOME/Downloads/appimage-tools}"
linuxdeploy="$tool_dir/linuxdeploy-x86_64.AppImage"
gtk_plugin="$tool_dir/linuxdeploy-plugin-gtk.sh"
dist_dir="$project_dir/dist"

if [[ ! -x "$project_dir/xmp-linux" ]]; then
    echo "Missing xmp-linux. Build the native player with: make -j4" >&2
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

mkdir -p -- "$dist_dir"
build_root="$(mktemp -d /tmp/xmp-appimage.XXXXXXXX)"
appdir="$build_root/XMP_Module_Player.AppDir"
output_name="XMP_Module_Player-x86_64-$(date +%Y%m%d-%H%M%S).AppImage"

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
        --executable "$project_dir/xmp-linux" \
        --executable "$project_dir/xmp-zx-helper" \
        --desktop-file "$script_dir/xmp-module-player.desktop" \
        --icon-file "$project_dir/icons/xmp.png" \
        --plugin gtk
)

# Optional bundled General MIDI bank. Keep the exact license for the chosen
# SoundFont with the AppImage; other sources may have different permissions.
# No SoundFont is downloaded or silently redistributed by this script.
if [[ -n "${XMP_BUNDLED_SOUNDFONT:-}" ]]; then
    source_font="$XMP_BUNDLED_SOUNDFONT"
    source_notice="${XMP_BUNDLED_SOUNDFONT_LICENSE:-}"
    if [[ ! -f "$source_font" || ! -f "$source_notice" ]]; then
        echo "Set XMP_BUNDLED_SOUNDFONT and XMP_BUNDLED_SOUNDFONT_LICENSE to existing files." >&2
        exit 1
    fi
    case "${source_font,,}" in
        *.sf2) bundled_font="default-GM.sf2" ;;
        *.sf3) bundled_font="default-GM.sf3" ;;
        *) echo "Bundled SoundFont must have an .sf2 or .sf3 extension." >&2; exit 1 ;;
    esac
    install -D -m 0644 "$source_font" \
        "$appdir/usr/share/sounds/sf2/$bundled_font"
    install -D -m 0644 "$source_notice" \
        "$appdir/usr/share/doc/xmp-module-player/soundfonts/SOUNDFONT-LICENSE.txt"
    echo "Bundled SoundFont: $source_font ($bundled_font)"
fi

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

echo "AppImage built: $dist_dir/$output_name"
echo "Test it locally before distributing it to other machines."

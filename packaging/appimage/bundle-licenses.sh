#!/usr/bin/env bash
# Gather available third-party notices BEFORE the AppDir is packaged.
# An inventory is not a substitute for a legal/source redistribution review.
set -euo pipefail

if (( $# != 2 )); then
    echo "Usage: $0 AppDir project-directory" >&2
    exit 2
fi
appdir="$1"
project_dir="$2"
notice_dir="$appdir/usr/share/doc/xmp-module-player"
package_dir="$notice_dir/debian-packages"
zxtune_dir="${ZXTUNE_SOURCE_DIR:-$HOME/zxtune}"
mkdir -p -- "$package_dir" "$notice_dir/common-licenses" "$notice_dir/zxtune"

if [[ ! -f "$project_dir/LICENSE" ]]; then
    echo "Missing project LICENSE: $project_dir/LICENSE" >&2
    exit 1
fi
install -m 0644 "$project_dir/LICENSE" "$notice_dir/LICENSE"
if [[ -d /usr/share/common-licenses ]]; then
    cp -a /usr/share/common-licenses/. "$notice_dir/common-licenses/"
fi

manifest="$notice_dir/BUNDLED-LIBRARY-INVENTORY.tsv"
missing="$notice_dir/UNMAPPED-LIBRARIES.txt"
printf 'Library filename\tDebian package\tPackage version\n' > "$manifest"
: > "$missing"

# linuxdeploy and its GTK plugin have already populated the AppDir.
# Identify the packages that own those shared-library filenames on the
# same Mint/Ubuntu system where the bundle was built.
count=0
mapped=0
while IFS= read -r lib; do
    ((count+=1))
    found=0
    matches="$(dpkg-query -S "*/$lib" 2>/dev/null || true)"
    while IFS= read -r match; do
        [[ -n "$match" ]] || continue
        pkg="${match%%: *}"
        pkg="${pkg%%:*}"
        copyright="/usr/share/doc/$pkg/copyright"
        if [[ -f "$copyright" ]]; then
            install -D -m 0644 "$copyright" "$package_dir/$pkg/copyright"
            version="$(dpkg-query -W -f='${Version}' "$pkg" 2>/dev/null || true)"
            printf '%s\t%s\t%s\n' "$lib" "$pkg" "$version" >> "$manifest"
            found=1
        fi
    done <<< "$matches"
    if (( found )); then
        ((mapped+=1))
    else
        printf '%s\n' "$lib" >> "$missing"
    fi
done < <(find "$appdir/usr" -type f -name '*.so*' -printf '%f\n' | sort -u)

# ZXTune is incorporated into xmp-zx-helper; it will not appear in the
# shared-library list. Collect its top-level and bundled 3rd-party notices.
if [[ -d "$zxtune_dir" ]]; then
    git -C "$zxtune_dir" rev-parse HEAD > "$notice_dir/ZXTUNE-REVISION.txt" 2>/dev/null || true
    while IFS= read -r -d '' notice; do
        rel="${notice#"$zxtune_dir"/}"
        install -D -m 0644 "$notice" "$notice_dir/zxtune/$rel"
    done < <(find "$zxtune_dir" -type f \
        \( -iname 'LICENSE' -o -iname 'LICENSE.*' \
           -o -iname 'COPYING' -o -iname 'COPYING.*' \
           -o -iname 'NOTICE' -o -iname 'NOTICE.*' \) \
        -not -path '*/.git/*' -print0)
else
    printf '%s\n' "ZXTune source directory not found: $zxtune_dir" > "$notice_dir/ZXTUNE-REVISION.txt"
    echo "WARNING: ZXTune source tree not found; bundled notices incomplete" >&2
fi

if command -v ffmpeg >/dev/null 2>&1; then
    ffmpeg -hide_banner -buildconf > "$notice_dir/FFMPEG-BUILD-CONFIG.txt" 2>&1 || true
fi

cat > "$notice_dir/READ-BEFORE-REDISTRIBUTION.txt" <<'EON'
XMP Module Player: third-party license inventory (not a compliance certificate)

The accompanying library inventory and copyright files were collected from
packages on the build machine. They must be reviewed for completeness and
accuracy against the exact binaries inside this AppImage.

Before public release, provide the corresponding source code for redistributed
GPL/LGPL components as required, including the relevant FFmpeg build and
ZXTune at the recorded revision. Check ZXTune static linking/relinking
requirements under LGPLv3, plus licenses for ZXTune's third-party code.

See: https://ffmpeg.org/legal.html
     https://www.gnu.org/licenses/lgpl-3.0.html

The presence of a notice alone does not establish redistribution compliance.
EON

echo "License inventory: $mapped/$count library filenames mapped."
if [[ -s "$missing" ]]; then
    echo "WARNING: Some libraries were not mapped to installed package notices:" >&2
    cat "$missing" >&2
else
    rm -f "$missing"
fi
echo "Notices added under usr/share/doc/xmp-module-player/"
echo "IMPORTANT: Source availability and license obligations still need review before publication."

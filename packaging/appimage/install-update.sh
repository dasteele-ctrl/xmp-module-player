#!/usr/bin/env bash
# Install the release selected in About, preserving the user's launcher path.
set -euo pipefail
tag=${1:?Missing release tag}
[[ "$tag" =~ ^v[0-9]+\.[0-9]+(\.[0-9]+)?(-r[0-9]+)?$ ]]
image=$(realpath -e -- "${MODULEZ_APPIMAGE_PATH:?}")
exec 9<"$image"
flock -n 9
asset="Modulez-Player-$tag-x86_64.AppImage"
url="https://github.com/dasteele-ctrl/xmp-module-player/releases/download/$tag/$asset"
stage=$(mktemp -d -- "$(dirname -- "$image")/.modulez-update.XXXXXXXX")
trap 'rm -rf -- "$stage"' EXIT
cd -- "$stage"
# Keep the control file on the same authenticated release as the payload.
"$APPDIR/usr/bin/curl" --disable --fail --silent --show-error --location \
    --proto '=https' --proto-redir '=https' --connect-timeout 15 --max-time 90 \
    --output release.zsync "$url.zsync"
digest=$(LC_ALL=C sed -n '/^$/q; s/^SHA-1: \([0-9a-f]\{40\}\)$/\1/p' release.zsync)
[[ "$digest" =~ ^[0-9a-f]{40}$ ]]
env -u LD_LIBRARY_PATH "$APPDIR/usr/libexec/modulez/zsync2.AppImage" \
    --appimage-extract-and-run --force-update -i "$image" -o "$stage/new.AppImage" \
    -u "$url" "$stage/release.zsync"
printf '%s  new.AppImage\n' "$digest" | sha1sum --check --status
chmod --reference="$image" new.AppImage
[[ -x new.AppImage && "$image" -ef /proc/self/fd/9 ]]
mv -fT -- new.AppImage "$image"

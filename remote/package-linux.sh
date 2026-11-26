#!/usr/bin/env bash
# Build ./modulez-player from this branch first. Use the v1.8.8 runtime to
# retain its tested decoder helpers, SoundFont and relocated WebKit resources.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
base=$(realpath "${1:?Supply the v1.8.8 x86_64 AppImage}")
output=$(realpath -m "${2:-Modulez-Player-Remote-Preview-x86_64.AppImage}")
plugin=${GIO_TLS_PLUGIN:-/usr/lib/x86_64-linux-gnu/gio/modules/libgiognutls.so}
notice=${GIO_TLS_NOTICE:-/usr/share/doc/glib-networking/copyright}
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT
cd "$tmp"
"$base" --appimage-extract >/dev/null
app=$tmp/squashfs-root
install -m755 "$repo/modulez-player" "$app/usr/bin/modulez-player"
install -Dm644 "$plugin" "$app/usr/lib/modulez-gio/libgiognutls.so"
install -Dm644 "$notice" "$app/usr/share/doc/modulez-remote-preview/glib-networking-copyright"
sed -i '/^    export APPDIR=/a\    export GIO_EXTRA_MODULES="$appdir/usr/lib/modulez-gio${GIO_EXTRA_MODULES:+:$GIO_EXTRA_MODULES}"' "$app/AppRun"
desktop=modulez-remote-preview.desktop
mv "$app/usr/share/applications/modulez-player.desktop" "$app/usr/share/applications/$desktop"
sed -i -e 's/^Name=.*/Name=Modulez Player Remote Preview/' \
    -e 's/^X-AppImage-Name=.*/X-AppImage-Name=Modulez Player Remote Preview/' \
    -e 's/^X-AppImage-Version=.*/X-AppImage-Version=1.8.8-remote-preview/' "$app/usr/share/applications/$desktop"
rm "$app/modulez-player.desktop"
ln -s "usr/share/applications/$desktop" "$app/$desktop"
head -c "$("$base" --appimage-offset)" "$base" > runtime
# Clear the release update URL and any signatures from the copied ELF header.
python3 - <<'PY'
import re, subprocess
sections = subprocess.check_output(["readelf", "-SW", "runtime"], text=True)
with open("runtime", "r+b") as f:
    for line in sections.splitlines():
        m = re.search(r"\.(?:upd_info|digest_md5|sha256_sig|sig_key)\s+PROGBITS\s+\w+\s+(\w+)\s+(\w+)", line)
        if m:
            f.seek(int(m[1], 16))
            f.write(bytes(int(m[2], 16)))
PY
mksquashfs "$app" payload -comp zstd -Xcompression-level 12 -b 1M -all-root -noappend -no-progress -processors 2
cat runtime payload > "$output"
chmod +x "$output"

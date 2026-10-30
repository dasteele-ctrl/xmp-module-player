#!/usr/bin/env bash
set -Eeuo pipefail

URL="https://hvsc.brona.dk/HVSC/HVSC_85-all-of-them.7z"
PARENT="${1:?Installation parent directory required}"
DESTINATION="$PARENT/HVSC"

if [[ ! -d "$PARENT" || -L "$PARENT" ]]; then
    echo "ERROR: Invalid installation parent directory."
    exit 1
fi

if [[ -e "$DESTINATION" || -L "$DESTINATION" ]]; then
    echo "ERROR: HVSC already exists. Installation refused."
    exit 1
fi

# Prefer the executables packaged in the AppImage to any host installation.
if [[ -n "${APPDIR:-}" && -d "$APPDIR/usr/bin" ]]; then
    export PATH="$APPDIR/usr/bin:$PATH"
fi

missing_commands=()
for command in curl python3; do
    if ! command -v "$command" >/dev/null 2>&1; then
        missing_commands+=("$command")
    fi
done
if command -v 7zz >/dev/null 2>&1; then
    ARCHIVER=7zz
elif command -v 7z >/dev/null 2>&1; then
    ARCHIVER=7z
else
    missing_commands+=("7z/7zz")
fi
if (( ${#missing_commands[@]} > 0 )); then
    echo "ERROR: Missing HVSC requirements: ${missing_commands[*]}. On Linux Mint/Ubuntu: sudo apt install curl 7zip python3"
    exit 1
fi

STAGING=$(mktemp -d "$PARENT/.hvsc-stage-XXXXXXXX")
ARCHIVE="$STAGING/HVSC.7z"
EXTRACTED="$STAGING/extracted"

cleanup() {
    if [[ -n "${STAGING:-}" && -d "$STAGING" ]]; then
        rm -rf -- "$STAGING"
    fi
}
trap cleanup EXIT

mkdir "$EXTRACTED"

echo "STATUS: Downloading HVSC #85"
echo "PROGRESS: 0"

# Verified HVSC #85 archive size in bytes.
EXPECTED_SIZE=84827547

# Download in the background while monitoring actual file size.
curl --fail --location --silent --show-error \
    "$URL" -o "$ARCHIVE" &
CURL_PID=$!

# Stop the downloader if the installer is interrupted.
stop_download() {
    if kill -0 "$CURL_PID" 2>/dev/null; then
        kill "$CURL_PID" 2>/dev/null || true
        wait "$CURL_PID" 2>/dev/null || true
    fi
}
cancel_installation() {
    trap - INT TERM
    stop_download
    echo "ERROR: Installation cancelled."
    exit 130
}
trap cancel_installation INT TERM

while kill -0 "$CURL_PID" 2>/dev/null; do
    if [[ -f "$ARCHIVE" ]]; then
        BYTES=$(stat -c %s -- "$ARCHIVE")
        PERCENT=$(( BYTES * 85 / EXPECTED_SIZE ))
        (( PERCENT > 85 )) && PERCENT=85
        echo "PROGRESS: $PERCENT"
    fi
    sleep 0.5
done

if ! wait "$CURL_PID"; then
    echo "ERROR: HVSC download failed."
    exit 1
fi

ACTUAL_SIZE=$(stat -c %s -- "$ARCHIVE")
if (( ACTUAL_SIZE != EXPECTED_SIZE )); then
    echo "ERROR: Unexpected archive size: $ACTUAL_SIZE bytes."
    exit 1
fi

echo "PROGRESS: 85"
echo "STATUS: Verifying archive"

if ! "$ARCHIVER" t "$ARCHIVE" >/dev/null; then
    echo "ERROR: Downloaded HVSC archive failed its integrity test."
    exit 1
fi

echo "PROGRESS: 88"
echo "STATUS: Checking archive paths"

"$ARCHIVER" l -slt "$ARCHIVE" |
awk '
    /^----------$/ { in_files=1; next }
    in_files && /^Path = / {
        path=substr($0,8)
        if (path !~ /^C64Music\// ||
            path ~ /(^|\/)\.\.(\/|$)/ ||
            path ~ /(^|\/)\.(\/|$)/) {
            print "ERROR: Unsafe archive path: " path > "/dev/stderr"
            bad=1
        }
    }
    END { exit bad }
'

echo "PROGRESS: 90"
echo "STATUS: Extracting HVSC"

if ! "$ARCHIVER" x "$ARCHIVE" "-o$EXTRACTED" -y >/dev/null; then
    echo "ERROR: Could not extract the downloaded HVSC archive."
    exit 1
fi

if [[ ! -f "$EXTRACTED/C64Music/DOCUMENTS/HVSC.txt" ||
      ! -d "$EXTRACTED/C64Music/MUSICIANS" ]]; then
    echo "ERROR: Extracted HVSC structure is invalid."
    exit 1
fi

# Reject links, special files, and unexpected paths before installation.
echo "STATUS: Validating extracted collection"

python3 - "$EXTRACTED" <<'VALIDATE_PY'
import os
import stat
import sys

root = os.path.abspath(sys.argv[1])
collection = os.path.join(root, "C64Music")

def reject(message):
    print("ERROR: " + message, file=sys.stderr)
    sys.exit(1)

if not os.path.isdir(collection) or os.path.islink(collection):
    reject("Invalid C64Music directory.")

for current, dirs, files in os.walk(root, followlinks=False):
    for name in dirs + files:
        path = os.path.join(current, name)
        mode = os.lstat(path).st_mode

        if stat.S_ISLNK(mode):
            reject("Symbolic link in archive: " + path)
        if not (stat.S_ISREG(mode) or stat.S_ISDIR(mode)):
            reject("Unsupported file type: " + path)
        if stat.S_ISREG(mode) and os.stat(path).st_nlink > 1:
            reject("Hard-linked file in archive: " + path)

        relative = os.path.relpath(path, root)
        if relative != "C64Music" and not relative.startswith(
            "C64Music" + os.sep
        ):
            reject("Unexpected archive path: " + relative)

print("STATUS: Extracted collection validated")
VALIDATE_PY

# Remove the archive from the staging directory before installation.
rm -f -- "$ARCHIVE"

echo "PROGRESS: 97"
echo "STATUS: Installing HVSC"

python3 - "$EXTRACTED" "$DESTINATION" <<'PY'
import ctypes
import os
import sys

source, destination = sys.argv[1:3]

libc = ctypes.CDLL(None, use_errno=True)
renameat2 = libc.renameat2

renameat2.argtypes = [
    ctypes.c_int, ctypes.c_char_p,
    ctypes.c_int, ctypes.c_char_p,
    ctypes.c_uint
]
renameat2.restype = ctypes.c_int

result = renameat2(
    -100, os.fsencode(source),
    -100, os.fsencode(destination),
    1
)

if result != 0:
    error = ctypes.get_errno()
    raise OSError(error, os.strerror(error), destination)

print("STATUS: HVSC installed successfully")
PY

echo "PROGRESS: 100"
echo "SUCCESS: $DESTINATION"

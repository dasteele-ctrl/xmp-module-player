#!/usr/bin/env bash
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd)
dest="$root/build/rmf"
src="$dest/NeoBAE"
revision=0f449adfed5a31e40879e732030e2a29e3be3406
mkdir -p "$dest"
if [[ ! -d "$src/.git" ]]; then git init -q "$src"; fi
if ! git -C "$src" cat-file -e "$revision^{commit}" 2>/dev/null; then
    git -C "$src" fetch -q --depth=1 https://github.com/zefie/NeoBAE.git "$revision"
fi
git -C "$src" checkout -q --detach "$revision"
git -C "$src" submodule update --init --depth=1 neobae/src/thirdparty/{minimp3,adpcm-xq,libg722}
options=()
for feature in MP3_ENCODER EMBED_PATCHES EMBED_FONT FLAC_DECODER FLAC_ENCODER VORBIS_DECODER VORBIS_ENCODER OPUS_DECODER OPUS_ENCODER SF2_SUPPORT FLUIDLITE NATIVE_DLS XMF_SUPPORT QOA_SUPPORT ADP_SUPPORT ADX_SUPPORT MIDI_HARDWARE ZMF_SUPPORT BAESCRIPT RMF_EDITOR LIB_HYPHEN; do
    options+=("-DBAE_DISABLE_${feature}=ON")
done
cmake -S "$src" -B "$dest/obj" -DCMAKE_BUILD_TYPE=Release -DBAE_PLATFORM=Dummy \
    -DBUILD_PLAYBAE=OFF -DBUILD_ZEFIDI=OFF -DBUILD_NBEDITOR=OFF -DBUILD_CLITOOLS=OFF \
    -DBUILD_TESTING=OFF -DNEOBAE_SHARED_LIBNEOBAE=OFF "${options[@]}"
cmake --build "$dest/obj" --target neobae -j "${JOBS:-4}"

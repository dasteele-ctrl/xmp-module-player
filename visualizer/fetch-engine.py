#!/usr/bin/env python3
"""Fetch the pinned engine for a source build; no runtime network access."""
from pathlib import Path
import hashlib
from urllib.request import urlopen
ROOT = Path(__file__).resolve().parent

WASM_SHA = '84ad815c17b604629f01f21b763546352c37b95f'
WASM_SIZE = 1057101
WASM_URL = ('https://raw.githubusercontent.com/jasonulbright/wavescope/'
            '29d3f8c5c3b577890143c9e6d8abe267bb1628bf/app/public/projectm/projectm.wasm')


def valid_wasm(data):
    return (len(data) == WASM_SIZE and data[:4] == b'\0asm' and
            hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest() == WASM_SHA)


def ensure_wasm():
    target = ROOT / 'projectm/projectm.wasm'
    if target.is_file() and valid_wasm(target.read_bytes()):
        return
    print('Downloading verified projectM engine (1.06 MB, first launch only)…', flush=True)
    with urlopen(WASM_URL, timeout=30) as response:
        data = response.read(WASM_SIZE + 1)
    if not valid_wasm(data):
        raise RuntimeError('projectM download failed its size/hash check; it was not installed.')
    temporary = target.with_suffix('.wasm.part')
    temporary.write_bytes(data)
    temporary.replace(target)
    print('projectM engine verified.', flush=True)


if __name__ == "__main__":
    ensure_wasm()

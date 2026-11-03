#!/usr/bin/env python3
"""Relocate WebKit's compiled /usr helper paths inside a staged AppDir."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess


def relative_literal(path):
    if not path.startswith('/usr/'):
        raise RuntimeError('Unsupported compiled WebKit path: ' + path)
    tail = path[len('/usr/'):]
    short = './' + tail
    return './' + '/' * (len(path) - len(short)) + tail


def relocate(appdir, source_dir, patchelf, verify=False):
    appdir = Path(appdir).resolve()
    source_dir = str(source_dir).rstrip('/')
    replacements = {source_dir: relative_literal(source_dir),
                    '/usr/bin/bwrap': relative_literal('/usr/bin/bwrap'),
                    '/usr/bin/xdg-dbus-proxy': relative_literal('/usr/bin/xdg-dbus-proxy')}
    libraries = sorted({p.resolve() for p in (appdir / 'usr').rglob('libwebkit2gtk-4.1.so*') if p.is_file()})
    if not libraries:
        raise RuntimeError('Bundled WebKit library is missing')
    for library in libraries:
        if not library.is_relative_to(appdir):
            raise RuntimeError('WebKit library points outside AppDir: ' + str(library))
        data = library.read_bytes()
        for before, after in replacements.items():
            original, replacement = before.encode(), after.encode()
            assert len(original) == len(replacement)
            if verify:
                if original in data:
                    raise RuntimeError('An absolute host helper path remains: ' + before)
            else:
                data = data.replace(original, replacement)
        if not verify:
            if replacements[source_dir].encode() not in data:
                raise RuntimeError('WebKit helper directory was not found; cannot relocate this WebKit build')
            library.write_bytes(data)
        elif replacements[source_dir].encode() not in data:
            raise RuntimeError('Relocated helper directory is not present')
    helper_dir = appdir / source_dir.lstrip('/')
    required = [helper_dir / 'WebKitWebProcess', helper_dir / 'WebKitNetworkProcess',
                helper_dir / 'injected-bundle/libwebkit2gtkinjectedbundle.so',
                appdir / 'usr/bin/bwrap', appdir / 'usr/bin/xdg-dbus-proxy']
    if (helper_dir / 'WebKitGPUProcess').exists() or (Path(source_dir) / 'WebKitGPUProcess').exists():
        required.append(helper_dir / 'WebKitGPUProcess')
    for helper in required:
        if not helper.is_file():
            raise RuntimeError('Missing bundled WebKit helper: ' + str(helper))
        if helper.suffix != '.so' and not os.access(helper, os.X_OK):
            raise RuntimeError('Bundled WebKit helper is not executable: ' + str(helper))
        if verify:
            continue
        rpath = '$ORIGIN/' + os.path.relpath(appdir / 'usr/lib', helper.parent)
        subprocess.run([patchelf, '--set-rpath', rpath, str(helper)], check=True)
    manifest = appdir / 'usr/share/doc/xmp-module-player/WEBKIT-RELOCATION.json'
    if not verify:
        manifest.parent.mkdir(parents=True, exist_ok=True)
        manifest.write_text(json.dumps({'working_directory': '$APPDIR/usr',
            'helper_directory': source_dir, 'path_replacements': replacements}, indent=2) + '\n')
    elif not manifest.is_file():
        raise RuntimeError('WebKit relocation manifest is missing')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('appdir')
    parser.add_argument('--source-dir', required=True)
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    patchelf = shutil.which('patchelf')
    if not patchelf and not args.verify:
        raise RuntimeError('Install patchelf before building the AppImage')
    relocate(args.appdir, args.source_dir, patchelf, args.verify)
    print('WebKit relocation ' + ('verified.' if args.verify else 'complete.'))


if __name__ == '__main__':
    main()

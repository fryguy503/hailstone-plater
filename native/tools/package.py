#!/usr/bin/env python3
"""Package the Windows x64 Crest Native preview with Hailstone's official add-on tool."""

import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[2]
NAME = 'crest-native'
VERSION = '0.1.0-preview.1'
PLATFORM = 'windows-x64'
MIN_CLIENT = '0.7.1-beta'
CAPS = 'read,commands,ui,extend,packets'
AUTHOR = 'Fryguy'
DESCRIPTION = 'Native preview of Crest world nameplates for Hailstone.'
DLL = f'{NAME}.dll'
MANIFEST = 'hailstone-addon.txt'
ARCHIVE = f'{NAME}-{VERSION}-{PLATFORM}.zip'


def validate_windows_dll(data):
    """Check the DOS header, COFF machine and DLL flags, and PE32+ optional header."""
    if len(data) < 64 or data[:2] != b'MZ':
        raise ValueError(f'{DLL} is missing its DOS header')
    pe_offset = struct.unpack_from('<I', data, 0x3C)[0]
    if not 64 <= pe_offset <= len(data) - 26 or data[pe_offset:pe_offset + 4] != b'PE\0\0':
        raise ValueError(f'{DLL} has no valid PE header')
    machine, sections = struct.unpack_from('<HH', data, pe_offset + 4)
    optional_size, flags, magic = struct.unpack_from('<HHH', data, pe_offset + 20)
    if (machine != 0x8664 or not sections or not flags & 0x2000 or magic != 0x20B or
            optional_size < 112 or pe_offset + 24 + optional_size + sections * 40 > len(data)):
        raise ValueError(f'{DLL} must be a Windows x64 PE32+ DLL')


def verify_package(out, expected):
    archive = out / ARCHIVE
    manifest = out / MANIFEST
    zip_bytes = archive.read_bytes()
    digest = hashlib.sha256(zip_bytes).hexdigest()
    fields = {
        'hailstone-addon 1', f'name {NAME}', 'kind plugin', f'version {VERSION}',
        f'min_client {MIN_CLIENT}', f'author {AUTHOR}', f'description {DESCRIPTION}', f'caps {CAPS}',
        f'package {PLATFORM} {len(zip_bytes)} {digest} {ARCHIVE}',
    }
    lines = manifest.read_text(encoding='utf-8').splitlines()
    if len(lines) != len(fields) or set(lines) != fields:
        raise ValueError('Unexpected native add-on manifest or ZIP digest')
    with zipfile.ZipFile(archive) as package:
        if sorted(package.namelist()) != sorted(expected) or package.testzip() is not None:
            raise ValueError('Unexpected native ZIP entries or invalid CRC')
        for name, data in expected.items():
            if package.read(name) != data:
                raise ValueError(f'Native ZIP entry differs from its staged bytes: {name}')
    return digest


def package(client_source, build_dir, out):
    packager = client_source / 'tools' / 'make_addon.py'
    if not packager.is_file():
        raise ValueError('--client-source must contain tools/make_addon.py')
    if out.resolve() == ROOT:
        raise ValueError('--out must be separate from the Lua add-on root manifest')
    libraries = (build_dir / DLL, build_dir / 'Release' / DLL)
    library = next((candidate for candidate in libraries if candidate.is_file()), None)
    if library is None:
        raise ValueError(f'{DLL} was not found in --build-dir or its Release subdirectory')
    dll_bytes = library.read_bytes()
    validate_windows_dll(dll_bytes)
    payload = {DLL: dll_bytes, 'LICENSE': (ROOT / 'LICENSE').read_bytes(),
               'README.md': (ROOT / 'native' / 'README.md').read_bytes()}

    with tempfile.TemporaryDirectory(prefix='crest-native-package-') as staging_directory:
        staging = Path(staging_directory)
        source = staging / 'payload'
        source.mkdir()
        for name, data in payload.items():
            (source / name).write_bytes(data)
        assets = staging / 'assets'
        subprocess.run([sys.executable, str(packager), '--name', NAME, '--kind', 'plugin', '--version', VERSION,
                        '--min-client', MIN_CLIENT, '--caps', CAPS, '--author', AUTHOR, '--description', DESCRIPTION,
                        '--platform', f'{PLATFORM}={source}', '--out', str(assets)], check=True)
        digest = verify_package(assets, payload)
        out.mkdir(parents=True, exist_ok=True)
        for name in (ARCHIVE, MANIFEST):
            (out / name).write_bytes((assets / name).read_bytes())
    return digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client-source', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    try:
        digest = package(args.client_source.resolve(), args.build_dir.resolve(), args.out.resolve())
    except (OSError, ValueError, subprocess.CalledProcessError, zipfile.BadZipFile) as error:
        parser.exit(1, f'Native package failed: {error}\n')
    print(f'{args.out / ARCHIVE}: SHA-256 {digest}')
    print(args.out / MANIFEST)


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Package Crest's native libraries with Hailstone's official add-on tool."""

import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[2]
NAME = 'crest'
VERSION = '0.3.0-preview.7'
MIN_CLIENT = '0.7.1-beta'
CAPS = 'read,commands,ui,extend,packets'
AUTHOR = 'Fryguy'
DESCRIPTION = 'Configurable native world nameplates for Hailstone with themes, profiles and optional server indicators.'
MANIFEST = 'hailstone-addon.txt'


def validate_windows_dll(data):
    """Check the DOS header, COFF machine and DLL flags, and PE32+ optional header."""
    if len(data) < 64 or data[:2] != b'MZ':
        raise ValueError('Missing the DLL DOS header')
    pe_offset = struct.unpack_from('<I', data, 0x3C)[0]
    if not 64 <= pe_offset <= len(data) - 26 or data[pe_offset:pe_offset + 4] != b'PE\0\0':
        raise ValueError('Missing a valid PE header')
    machine, sections = struct.unpack_from('<HH', data, pe_offset + 4)
    optional_size, flags, magic = struct.unpack_from('<HHH', data, pe_offset + 20)
    if (machine != 0x8664 or not sections or not flags & 0x2000 or magic != 0x20B or
            optional_size < 112 or pe_offset + 24 + optional_size + sections * 40 > len(data)):
        raise ValueError('Expected a Windows x64 PE32+ DLL')


def validate_linux_library(data):
    """Check the ELF64 x86-64 shared object header and its program segments."""
    if len(data) < 64 or data[:7] != b'\x7fELF\x02\x01\x01':
        raise ValueError('Missing a little-endian ELF64 header')
    kind, machine, version = struct.unpack_from('<HHI', data, 16)
    program_offset = struct.unpack_from('<Q', data, 32)[0]
    header_size, entry_size, count = struct.unpack_from('<HHH', data, 52)
    if (kind != 3 or machine != 62 or version != 1 or header_size != 64 or entry_size < 56 or
            not count or program_offset < header_size or program_offset + entry_size * count > len(data)):
        raise ValueError('Expected a Linux x64 ET_DYN library')
    segment_types = {struct.unpack_from('<I', data, program_offset + i * entry_size)[0] for i in range(count)}
    if 2 not in segment_types or 3 in segment_types:
        raise ValueError('Expected a dynamic ELF shared library without a program interpreter')


def validate_macos_library(data):
    """Check the thin arm64 Mach-O header and dylib file type."""
    if len(data) < 32 or data[:4] != b'\xcf\xfa\xed\xfe':
        raise ValueError('Missing a 64-bit Mach-O header')
    machine, _, kind, command_count, command_bytes = struct.unpack_from('<IIIII', data, 4)
    if (machine != 0x0100000C or kind != 6 or not command_count or
            command_bytes < command_count * 8 or 32 + command_bytes > len(data)):
        raise ValueError('Expected a macOS ARM64 Mach-O dylib')


PLATFORMS = {
    'windows-x64': (f'{NAME}.dll', validate_windows_dll),
    'linux-x64': (f'{NAME}.so', validate_linux_library),
    'macos-arm64': (f'{NAME}.dylib', validate_macos_library),
}


def archive_name(platform):
    return f'{NAME}-{VERSION}-{platform}.zip'


def verify_packages(out, payloads):
    fields = {
        'hailstone-addon 1', f'name {NAME}', 'kind plugin', f'version {VERSION}',
        f'min_client {MIN_CLIENT}', f'author {AUTHOR}', f'description {DESCRIPTION}', f'caps {CAPS}',
    }
    digests = {}
    for platform, expected in payloads.items():
        archive = out / archive_name(platform)
        zip_bytes = archive.read_bytes()
        digest = hashlib.sha256(zip_bytes).hexdigest()
        digests[platform] = digest
        fields.add(f'package {platform} {len(zip_bytes)} {digest} {archive.name}')
        with zipfile.ZipFile(archive) as package:
            if sorted(package.namelist()) != sorted(expected) or package.testzip() is not None:
                raise ValueError(f'Unexpected ZIP entries or invalid CRC: {archive.name}')
            for name, data in expected.items():
                if package.read(name) != data:
                    raise ValueError(f'ZIP entry differs from its staged bytes: {archive.name}/{name}')
    lines = (out / MANIFEST).read_text(encoding='utf-8').splitlines()
    if len(lines) != len(fields) or set(lines) != fields:
        raise ValueError('Unexpected native add-on manifest or ZIP digest records')
    return digests


def platform_binary(platform, source):
    filename, validate = PLATFORMS[platform]
    candidates = (source,) if source.is_file() else (source / filename, source / 'Release' / filename)
    library = next((candidate for candidate in candidates if candidate.is_file()), None)
    if library is None:
        raise ValueError(f'{platform}: {filename} not found at {source} or its Release subdirectory')
    data = library.read_bytes()
    try:
        validate(data)
    except ValueError as error:
        raise ValueError(f'{platform}: {library}: {error}') from error
    return filename, data


def package(client_source, builds, out):
    packager = client_source / 'tools' / 'make_addon.py'
    if not packager.is_file():
        raise ValueError('--client-source must contain tools/make_addon.py')
    if out.resolve() == ROOT:
        raise ValueError('--out must be separate from the repository root manifest')
    common = {'LICENSE': (ROOT / 'LICENSE').read_bytes(), 'README.md': (ROOT / 'native' / 'README.md').read_bytes()}
    panel = (ROOT / 'native' / 'ui' / 'crest.lua').read_bytes()
    helpers = {file: (ROOT / 'crest' / file).read_bytes() for file in ('config.lua', 'colors.lua', 'panel.lua', 'personal.lua')}
    for pack in ('modern', 'gamepad'):
        common[f'ui/{pack}/crest.lua'] = panel
        for file, content in helpers.items():
            common[f'ui/{pack}/crest/{file}'] = content
    payloads = {}
    for platform, source in sorted(builds.items()):
        filename, data = platform_binary(platform, source)
        payloads[platform] = {filename: data, **common}

    with tempfile.TemporaryDirectory(prefix='crest-native-package-') as staging_directory:
        staging = Path(staging_directory)
        assets = staging / 'assets'
        command = [sys.executable, str(packager), '--name', NAME, '--kind', 'plugin', '--version', VERSION,
                   '--min-client', MIN_CLIENT, '--caps', CAPS, '--author', AUTHOR, '--description', DESCRIPTION,
                   '--out', str(assets)]
        for platform, payload in payloads.items():
            source = staging / platform
            for name, data in payload.items():
                destination = source / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(data)
            command.extend(('--platform', f'{platform}={source}'))
        subprocess.run(command, check=True)
        digests = verify_packages(assets, payloads)
        out.mkdir(parents=True, exist_ok=True)
        for platform in payloads:
            name = archive_name(platform)
            (out / name).write_bytes((assets / name).read_bytes())
        (out / MANIFEST).write_bytes((assets / MANIFEST).read_bytes())
    return digests


def platform_argument(text):
    platform, _, path = text.partition('=')
    if platform not in PLATFORMS or not path:
        raise argparse.ArgumentTypeError(f'expected PLATFORM=BUILD_DIR_OR_BINARY ({", ".join(PLATFORMS)})')
    return platform, Path(path).resolve()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client-source', type=Path, required=True)
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument('--build-dir', type=Path, help='Windows x64 build directory')
    inputs.add_argument('--platform', action='append', type=platform_argument, metavar='PLATFORM=PATH',
                        help='Build directory or downloaded library for a platform; repeat for multiple platforms')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    builds = {'windows-x64': args.build_dir.resolve()} if args.build_dir is not None else dict(args.platform)
    if args.platform and len(builds) != len(args.platform):
        parser.error('supply each platform only once')
    try:
        digests = package(args.client_source.resolve(), builds, args.out.resolve())
    except (OSError, ValueError, subprocess.CalledProcessError, zipfile.BadZipFile) as error:
        parser.exit(1, f'Native package failed: {error}\n')
    for platform, digest in digests.items():
        print(f'{args.out / archive_name(platform)}: SHA-256 {digest}')
    print(args.out / MANIFEST)


if __name__ == '__main__':
    main()

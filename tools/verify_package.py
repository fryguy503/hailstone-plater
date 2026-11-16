#!/usr/bin/env python3
"""Verify Crest's standalone add-on archive, source bytes, public presets and manifest."""
import argparse
import ast
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import zipfile

ROOT = Path(__file__).resolve().parents[1]
RUNTIME_FILES = {
    'init.lua', 'crest/config.lua', 'crest/engine.lua', 'crest/server_indicators.lua',
    'crest/panel.lua', 'crest/personal.lua', 'crest/resources.lua', 'crest/colors.lua',
    'crest/renderer.lua', 'LICENSE', 'README.md', 'THIRD_PARTY.md', 'CHANGELOG.md',
    'docs/VALIDATION.md',
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def package_metadata():
    # Read the authoring tool's declarations without executing its packaging code.
    tree = ast.parse((ROOT / 'tools/package.py').read_text(encoding='utf-8'))
    constants = {}
    minimums = []
    for node in ast.walk(tree):
        if isinstance(node, ast.Assign) and isinstance(node.value, ast.Constant):
            for target in node.targets:
                if isinstance(target, ast.Name) and target.id in ('VERSION', 'CAPS'):
                    constants[target.id] = node.value.value
        if isinstance(node, (ast.List, ast.Tuple)):
            for index, item in enumerate(node.elts[:-1]):
                if isinstance(item, ast.Constant) and item.value == '--min-client':
                    value = node.elts[index + 1]
                    if isinstance(value, ast.Constant):
                        minimums.append(value.value)
    require(set(constants) == {'VERSION', 'CAPS'} and len(minimums) == 1 and
            all(isinstance(value, str) for value in (*constants.values(), *minimums)),
            'Expected literal version, capabilities and minimum client in package.py')
    return constants['VERSION'], constants['CAPS'], minimums[0]


VERSION, CAPS, MIN_CLIENT = package_metadata()


def verify_release(release, source_root=ROOT):
    """Check a release against its source tree without changing either of them."""
    release = Path(release).resolve()
    source_root = Path(source_root).resolve()
    manifest_path = release / 'hailstone-addon.txt'
    manifest = manifest_path.read_text(encoding='utf-8')
    lines = manifest.splitlines()
    expected_fields = {
        'hailstone-addon': '1', 'name': 'crest', 'kind': 'script', 'autoload': '1',
        'version': VERSION, 'caps': CAPS, 'min_client': MIN_CLIENT,
    }
    for key, value in expected_fields.items():
        fields = [line for line in lines if line.split(maxsplit=1)[:1] == [key]]
        require(fields == [key + ' ' + value], 'Invalid add-on manifest field: ' + key)
    packages = [line.split() for line in lines if line.split(maxsplit=1)[:1] == ['package']]
    require(len(packages) == 1, 'Expected exactly one portable add-on package')
    package = packages[0]
    require(len(package) == 5 and package[1] == 'any', 'Invalid portable package declaration')
    name = package[4]
    require(name == 'crest-' + VERSION + '.zip', 'Unexpected add-on archive name: ' + name)
    require(PurePosixPath(name).name == name and '\\' not in name and ':' not in name,
            'Unsafe add-on archive name')
    archive_path = release / name
    digest = hashlib.sha256(archive_path.read_bytes()).hexdigest()
    require(int(package[2]) == archive_path.stat().st_size and package[3] == digest,
            'Add-on archive size or SHA-256 does not match its manifest')
    require((source_root / 'hailstone-addon.txt').read_bytes() == manifest_path.read_bytes(),
            'Root and release manifests differ')
    public = re.search(r'M\.presets\s*=\s*\{([^}]+)\}',
                       (source_root / 'crest/config.lua').read_text(encoding='utf-8'))
    require(public is not None, 'Public preset declaration not found')
    titles = re.findall(r'"([^"]+)"', public.group(1))
    profiles = {'examples/' + value.lower().replace(' ', '-') + '.crest' for value in titles}
    require(len(titles) == 14 and len(profiles) == 14, 'Expected fourteen distinct public profiles')
    expected_files = RUNTIME_FILES | profiles
    with zipfile.ZipFile(archive_path) as archive:
        names = archive.namelist()
        require(len(names) == len(set(names)), 'Duplicate archive paths')
        require(archive.testzip() is None, 'Archive CRC check failed')
        actual_files = set(names)
        require(actual_files == expected_files,
                'Unexpected archive file set; missing: ' + str(sorted(expected_files - actual_files)) +
                '; extra: ' + str(sorted(actual_files - expected_files)))
        for path in names:
            require(not PurePosixPath(path).is_absolute() and '..' not in PurePosixPath(path).parts and
                    '\\' not in path and ':' not in path, 'Unsafe archive path: ' + path)
            require(not path.lower().endswith(('.exe', '.dll', '.so', '.dylib', '.patch', '.s3d', '.eqg')),
                    'Client program, patch or game asset in add-on: ' + path)
            data = archive.read(path)
            require(data == (source_root / path).read_bytes(), 'Stale packaged source: ' + path)
            require(b'bastion' not in data.lower(), 'Server branding in add-on: ' + path)
    return {'status': 'passed', 'standalone_addon': True, 'files': len(names),
            'public_profiles': len(profiles), 'sha256': digest, 'archive': name}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--release-dir', type=Path, default=ROOT / 'release' / VERSION)
    args = parser.parse_args()
    release = args.release_dir.resolve()
    try:
        report = verify_release(release)
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        parser.exit(1, 'Package verification failed: ' + str(error) + '\n')
    artifacts = ROOT / 'artifacts'
    artifacts.mkdir(exist_ok=True)
    (artifacts / 'package-verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    archive = release / report['archive']
    try:
        archive_label = archive.relative_to(ROOT).as_posix()
    except ValueError:
        archive_label = str(archive)
    (artifacts / 'SHA256SUMS.txt').write_text(report['sha256'] + '  ' + archive_label + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()

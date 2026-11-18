#!/usr/bin/env python3
"""Build a local, reproducible preview with Hailstone's official addon packager."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
VERSION = '0.3.0-preview.2'
CAPS = 'read,commands,ui,extend,packets'

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--client-source', type=Path, required=True)
    p.add_argument('--lua', default='lua')
    p.add_argument('--out', type=Path, default=ROOT / 'release' / VERSION)
    args = p.parse_args()
    packager = args.client_source.resolve() / 'tools' / 'make_addon.py'
    if not packager.is_file():
        p.error('client-source must contain tools/make_addon.py')
    out = args.out.resolve()
    # The tool only writes its task-owned staging tree; it never removes a tree.
    stage = ROOT / 'artifacts' / ('package-stage-' + VERSION)
    stage.mkdir(parents=True, exist_ok=True)
    runtime = ['init.lua', 'crest/config.lua', 'crest/engine.lua', 'crest/server_indicators.lua',
               'crest/panel.lua', 'crest/personal.lua', 'crest/resources.lua', 'crest/colors.lua', 'crest/renderer.lua', 'LICENSE', 'README.md', 'THIRD_PARTY.md',
               'CHANGELOG.md', 'docs/VALIDATION.md']
    expected = set(runtime)
    for source in runtime:
        dest = stage / source
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / source, dest)
    example_script = '''local c=require('crest.config')
local dest=assert(arg[1])
for _,name in ipairs(c.presets) do
  local path=dest..'/'..name:lower():gsub(' ','-')..'.crest'
  local f=assert(io.open(path,'wb')); f:write(c.export(c.preset(name))..'\\n'); f:close()
end
'''
    examples = ROOT / 'examples'
    examples.mkdir(exist_ok=True)
    subprocess.run([args.lua, '-', str(examples)], input=example_script, text=True, cwd=ROOT, check=True)
    for source in sorted(examples.glob('*.crest')):
        relative = 'examples/' + source.name
        expected.add(relative)
        (stage / 'examples').mkdir(exist_ok=True)
        shutil.copy2(source, stage / relative)
    actual = {f.relative_to(stage).as_posix() for f in stage.rglob('*') if f.is_file()}
    if actual != expected:
        raise SystemExit('Unexpected staging files; inspect artifacts/package-stage before packaging: ' + str(actual - expected))
    subprocess.run([sys.executable, str(packager), '--name', 'crest', '--kind', 'script',
                    '--version', VERSION, '--caps', CAPS, '--min-client', '0.7.1-beta', '--autoload',
                    '--author', 'Fryguy', '--description', 'Configurable world health, resource and cast plates with optional server status icons.',
                    '--dir', str(stage), '--out', str(out)], check=True)
    # This root manifest describes the local archive; publishing remains an explicit action.
    shutil.copy2(out / 'hailstone-addon.txt', ROOT / 'hailstone-addon.txt')
    print('Local unsigned preview:', out)
    print('Requires Hailstone generic nameplate canvas API 1 and managed Lua add-on support.')

if __name__ == '__main__':
    main()

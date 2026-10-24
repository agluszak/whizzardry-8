#!/usr/bin/env python3
"""Check the architecture's layer boundaries (ROADMAP.md, "Target architecture").

A file belongs to a layer by directory: include/wiz8/<layer>/ and src/<layer>/.
It may include headers of its own layer, of the layers it depends on, the shared
foundation headers, the standard library, glm and fmt; nothing else. Layer files
declare no mutable globals with `extern`. With --clang-tidy, the layers' sources
are also checked for mutable namespace-scope variables.
"""

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LAYERS = {
    'data': set(),
    'rules': {'data'},
    'game': {'data', 'rules'},
}
# Stateless value headers every layer may use.
FOUNDATION = {
    'include/wiz8/math.h',
    'include/wiz8/retail_opaque_records.h',
    'include/wiz8/world/camera_pose.h',
}
ANGLE_LIBRARIES = ('glm/', 'fmt/')
INCLUDE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', re.MULTILINE)
EXTERN = re.compile(r'^\s*extern\s+(?!"C"|const\b|constinit\s+const\b)', re.MULTILINE)


def layer_of(path):
    relative = path.relative_to(ROOT).as_posix()
    if relative in FOUNDATION:
        return 'foundation'
    for layer in LAYERS:
        if relative.startswith((f'include/wiz8/{layer}/', f'src/{layer}/')):
            return layer
    return None


def resolve(including, name):
    for base in (including.parent, ROOT / 'include', ROOT / 'include/wiz8'):
        candidate = (base / name).resolve()
        if candidate.is_file() and ROOT in candidate.parents:
            return candidate
    return None


def layer_files():
    for directory in [ROOT / 'include/wiz8' / layer for layer in LAYERS] + \
                     [ROOT / 'src' / layer for layer in LAYERS]:
        if directory.is_dir():
            yield from sorted(p for p in directory.rglob('*') if p.suffix in ('.h', '.cpp'))
    for relative in sorted(FOUNDATION):
        yield ROOT / relative


def check_includes():
    problems = []
    for path in layer_files():
        layer = layer_of(path)
        allowed = {'foundation'} if layer == 'foundation' else {layer, 'foundation'} | LAYERS[layer]
        text = path.read_text(encoding='utf-8')
        where = path.relative_to(ROOT)
        for match in EXTERN.finditer(text):
            line = text.count('\n', 0, match.start()) + 1
            problems.append(f'{where}:{line}: {layer} declares a mutable global')
        for match in INCLUDE.finditer(text):
            line = text.count('\n', 0, match.start()) + 1
            kind, name = match.groups()
            if name.startswith(ANGLE_LIBRARIES):
                continue
            if kind == '<':
                if '/' in name:
                    problems.append(f'{where}:{line}: {layer} may not include <{name}>')
                continue
            target = resolve(path, name)
            if target is None:
                problems.append(f'{where}:{line}: cannot resolve "{name}"')
                continue
            target_layer = layer_of(target)
            if target_layer not in allowed:
                problems.append(f'{where}:{line}: {layer} may not include "{name}" '
                                f'({target_layer or "unlayered"})')
    # Complete migrated flows cannot reacquire direct screen dependencies.
    for relative in (
        'src/wiz8/game.cpp',
        'src/wiz8/game_advance.cpp',
        'src/wiz8/inventory.cpp',
        'src/wiz8/live_party.cpp',
    ):
        path = ROOT / relative
        for match in INCLUDE.finditer(path.read_text(encoding='utf-8')):
            if any(part in match.group(2) for part in ('local_screens/', 'screen.h', 'pointer.h')):
                problems.append(f'{relative}: gameplay may not include "{match.group(2)}"')
    # Picking interpretation belongs to interaction code, never frame rendering.
    frame = ROOT / 'src/render/frame.cpp'
    if re.search(r'\bacceptPick\s*\(|->picked\b', frame.read_text(encoding='utf-8')):
        problems.append('src/render/frame.cpp: rendering may not commit gameplay selections')
    return problems


def check_globals(clang_tidy, build):
    sources = [p for p in layer_files() if p.suffix == '.cpp']
    known = {Path(entry['file']).resolve()
             for entry in json.loads((build / 'compile_commands.json').read_text())}
    sources = [str(p) for p in sources if p in known]
    if not sources:
        return []
    config = json.dumps({
        'Checks': '-*,cppcoreguidelines-avoid-non-const-global-variables',
        'WarningsAsErrors': '*',
        'HeaderFilterRegex': 'include/wiz8/(' + '|'.join(LAYERS) + ')/',
    })
    result = subprocess.run([clang_tidy, '--quiet', '-p', str(build), f'--config={config}',
                             *sources], capture_output=True, text=True)
    if result.returncode == 0:
        return []
    return [line for line in result.stdout.splitlines() if 'warning:' in line or 'error:' in line] \
        or [result.stdout + result.stderr]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang-tidy', help='clang-tidy binary for the global-variable check')
    parser.add_argument('-p', dest='build', type=Path, help='build directory with compile_commands.json')
    args = parser.parse_args()
    problems = check_includes()
    if args.clang_tidy:
        if not args.build:
            parser.error('--clang-tidy needs -p')
        problems += check_globals(args.clang_tidy, args.build.resolve())
    for problem in problems:
        print(problem)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())

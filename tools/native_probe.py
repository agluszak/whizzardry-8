#!/usr/bin/env python3
"""Syntax-check game and SGP sources with the native lane's flags.

Runs clang in parallel over src/sgp and src/wiz8 (or the given files) and
writes every diagnostic as one JSON object per line. The summary groups
errors so codemods can target whole classes at once.

Usage:
  native_probe.py [--out diagnostics.jsonl] [--jobs N] [--summary identifiers|messages|files] [FILE...]
"""
import argparse
import collections
import concurrent.futures
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Keep in step with cmake/Native.cmake.
FLAGS = [
    '-x', 'c++', '-std=c++17', '-fsyntax-only', '-w', '-ferror-limit=0',
    '-fsigned-char', '-fwrapv', '-fno-strict-aliasing', '-fshort-wchar',
    '-DWIZ8_NATIVE', '-DNDEBUG',
    '-include', 'include/wiz8/compat/compiler.h',
    '-Iinclude', '-Iinclude/wiz8', '-Iinclude/wiz8/engine_code', '-Isrc/sgp',
    '-fno-color-diagnostics', '-fno-caret-diagnostics',
]

DIAGNOSTIC = re.compile(r'^(?P<file>.+?):(?P<line>\d+):(?P<col>\d+): (?P<kind>error|fatal error|note|warning): (?P<message>.*)$')
QUOTED = re.compile(r"'([^']*)'")


def platform_units():
    """Units replaced wholesale by native subsystems; see native_codemod_rules.json."""
    with open(os.path.join(ROOT, 'tools', 'native_codemod_rules.json')) as handle:
        return set(json.load(handle)['platform_units'])


def sources(include_platform=False):
    skip = set() if include_platform else platform_units()
    found = []
    for base in ('src/sgp', 'src/wiz8'):
        for directory, _, names in os.walk(os.path.join(ROOT, base)):
            for name in names:
                path = os.path.relpath(os.path.join(directory, name), ROOT)
                if name.endswith(('.cpp', '.c')) and path not in skip:
                    found.append(path)
    return sorted(found)


def check(path):
    result = subprocess.run(['clang++'] + FLAGS + [path], cwd=ROOT, capture_output=True, text=True,
                            errors='replace')
    diagnostics = []
    for line in result.stderr.splitlines():
        match = DIAGNOSTIC.match(line)
        if match:
            entry = match.groupdict()
            entry['line'] = int(entry['line'])
            entry['col'] = int(entry['col'])
            entry['tu'] = path
            diagnostics.append(entry)
    return diagnostics


def summarize(diagnostics, mode):
    counts = collections.Counter()
    for entry in diagnostics:
        if entry['kind'] not in ('error', 'fatal error'):
            continue
        message = entry['message']
        if mode == 'identifiers':
            if not re.match(r'(use of undeclared identifier|unknown type name|no matching function for call to|no member named)', message):
                continue
            names = QUOTED.findall(message)
            key = f"{message.split(' ' + chr(39))[0]}: {names[0] if names else '?'}"
        elif mode == 'messages':
            key = QUOTED.sub("'X'", message)
        else:
            key = entry['file']
        counts[key] += 1
    for key, count in counts.most_common():
        print(f'{count:6d}  {key}')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('files', nargs='*')
    parser.add_argument('--out', default=os.path.join(ROOT, 'build-native', 'probe.jsonl'))
    parser.add_argument('--jobs', type=int, default=os.cpu_count())
    parser.add_argument('--all', action='store_true', help='include platform units')
    parser.add_argument('--summary', choices=('identifiers', 'messages', 'files'), default='messages')
    args = parser.parse_args()
    files = args.files or sources(args.all)
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        results = list(pool.map(check, files))
    diagnostics = [entry for result in results for entry in result]
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, 'w') as out:
        for entry in diagnostics:
            out.write(json.dumps(entry) + '\n')
    errors = [entry for entry in diagnostics if entry['kind'] in ('error', 'fatal error')]
    clean = sum(1 for result in results if not any(e['kind'] in ('error', 'fatal error') for e in result))
    print(f'{len(files)} files, {clean} clean, {len(errors)} errors -> {os.path.relpath(args.out, ROOT)}',
          file=sys.stderr)
    summarize(diagnostics, args.summary)


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Rewrite quoted #include paths to the on-disk spelling.

Windows resolves include paths case-insensitively; Linux does not. Each quoted
include is resolved against the including file's directory and the project
include roots, ignoring case, and rewritten to the exact file name.

Usage: fix_include_case.py FILE...
"""
import os
import re
import sys

ROOTS = ['include', 'include/wiz8', 'include/wiz8/engine_code', 'src/sgp']
INCLUDE = re.compile(r'^(\s*#\s*include\s*")([^"]+)(")', re.M | re.I)


def resolve(base, relative):
    """Return the exactly-cased relative path under base, or None."""
    current = base
    parts = []
    for part in relative.replace('\\', '/').split('/'):
        if part in ('.', '..'):
            current = os.path.normpath(os.path.join(current, part))
            parts.append(part)
            continue
        try:
            names = os.listdir(current)
        except OSError:
            return None
        match = [name for name in names if name.lower() == part.lower()]
        if not match:
            return None
        exact = part if part in match else match[0]
        parts.append(exact)
        current = os.path.join(current, exact)
    return '/'.join(parts) if os.path.isfile(current) else None


def main(paths):
    changed = 0
    for path in paths:
        with open(path, encoding='latin-1', newline='') as handle:
            text = handle.read()

        def fix(match):
            name = match.group(2)
            for base in [os.path.dirname(path)] + ROOTS:
                exact = resolve(base, name)
                if exact is not None:
                    return match.group(1) + exact + match.group(3)
            return match.group(0)

        result = INCLUDE.sub(fix, text)
        if result != text:
            with open(path, 'w', encoding='latin-1', newline='') as handle:
                handle.write(result)
            changed += 1
    print(f'{changed} of {len(paths)} files rewritten')


if __name__ == '__main__':
    main(sys.argv[1:])

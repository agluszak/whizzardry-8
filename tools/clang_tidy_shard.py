#!/usr/bin/env python3
"""Partition translation units by path, independent of checkout and source edits."""

import argparse
import hashlib
import json
from pathlib import Path
import re

from clang_tidy_support import is_first_party_source, source_path


def owns_source(root, source, index, count):
    if count == 1:
        return True
    digest = hashlib.sha256(source.relative_to(root).as_posix().encode()).digest()
    return int.from_bytes(digest[:8], 'little') % count == index


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--compile-commands', type=Path, required=True)
    parser.add_argument('--pattern', required=True)
    parser.add_argument('--index', type=int, required=True)
    parser.add_argument('--count', type=int, required=True)
    args = parser.parse_args()
    if args.count < 1 or not 0 <= args.index < args.count:
        parser.error('shard count must be positive and index must be within the shard count')
    root = args.root.resolve()
    entries = json.loads(args.compile_commands.read_text())
    sources = {source_path(entry) for entry in entries}
    selected = sorted(source for source in sources if is_first_party_source(root, source)
                      and re.search(args.pattern, str(source))
                      and owns_source(root, source, args.index, args.count))
    print('^(?:' + '|'.join(re.escape(str(source)) for source in selected) + ')$'
          if selected else 'SKIP')


if __name__ == '__main__':
    main()

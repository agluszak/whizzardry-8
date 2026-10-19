#!/usr/bin/env python3
"""Select clang-tidy translation units using compiler-generated dependencies.

Output FULL, SKIP, or a regex of source paths accepted by run-clang-tidy.
Failure to discover dependencies falls back to a full analysis.
"""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from tempfile import TemporaryDirectory

from clang_tidy_support import SOURCES, dependencies, is_first_party_source, source_path

HEADERS = {".h", ".hh", ".hpp", ".hxx", ".inc", ".inl", ".ipp"}
DOCS = {"README.md", "LICENSE", ".gitignore", "AGENTS.md", "CONTRIBUTING.md"}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--compile-commands", type=Path, required=True)
    parser.add_argument("--changed-files", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    names = [os.fsdecode(name) for name in args.changed_files.read_bytes().split(b"\0") if name]

    changed_sources = set()
    changed_headers = set()
    for name in names:
        path = Path(name)
        if name in DOCS or name.startswith(("docs/", ".github/ISSUE_TEMPLATE/")):
            continue
        if path.parts and path.parts[0] in ("src", "tests", "tools") and path.suffix in SOURCES:
            changed_sources.add((root / path).resolve())
        elif path.parts and path.parts[0] in ("src", "tests", "include", "tools") and path.suffix in HEADERS:
            changed_headers.add((root / path).resolve())
        else:
            # Build scripts, lint configuration, or unrecognized input changed.
            print("FULL")
            return

    if changed_headers:
        try:
            entries = json.loads(args.compile_commands.read_text())
            entries = [entry for entry in entries if is_first_party_source(root, source_path(entry))]
            if not entries:
                raise RuntimeError("No first-party translation units in compilation database")
            print(f"Scanning compiler dependencies for {len(entries)} translation units...", file=sys.stderr)
            with TemporaryDirectory() as directory:
                with ThreadPoolExecutor(max_workers=max(1, int(os.environ.get("CLANG_TIDY_SCAN_JOBS", "4")))) as pool:
                    results = list(pool.map(lambda item: dependencies(item[1], item[0], Path(directory)), enumerate(entries)))
            changed_sources.update(path for path, deps in results if deps & changed_headers)
            print(f"Affected translation units: {len(changed_sources)} of {len(entries)}", file=sys.stderr)
        except (OSError, ValueError, KeyError, RuntimeError, subprocess.TimeoutExpired) as exc:
            print(f"Dependency scan failed ({exc}); falling back to full clang-tidy run.", file=sys.stderr)
            print("FULL")
            return

    if changed_sources:
        print("^(?:" + "|".join(sorted(re.escape(str(path)) for path in changed_sources)) + ")$")
    else:
        print("SKIP")


if __name__ == "__main__":
    main()

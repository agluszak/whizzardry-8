#!/usr/bin/env python3
"""Inventory allocation expressions in every tracked C/C++ file.

Includes inactive platform branches, headers, plug-ins, tests and vendored code.
This is a review aid, not a type checker: declarations, macro definitions and
allocator forwarding calls deliberately remain visible. Trace variable byte
counts to their producers; neither the presence nor absence of sizeof proves
that an allocation has the correct size. Ordinary typed new is compiler-sized.
"""

import argparse
import collections
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl"}
IGNORED = re.compile(
    r'R"(?P<delimiter>[^\s()\\]{0,16})\(.*?\)(?P=delimiter)"'
    r'|//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
    re.DOTALL,
)
ALLOCATOR = re.compile(
    r"\b(?:operator\s+new(?:\s*\[\s*\])?|"
    r"(?:[A-Za-z_]\w*_)?(?:malloc|calloc|realloc|aligned_alloc)(?:_dbg)?|"
    r"[A-Z_]*(?:MALLOC|CALLOC|REALLOC)|(?:re)?allocate|Mem(?:Alloc|Realloc)\w*|"
    r"(?:Heap|Local|Global|Virtual)Alloc(?:Ex)?|AIL_mem_alloc_lock|alloc_small|alloc_large)\s*\("
)


def expressions(source):
    # Preserve offsets and newlines so locations refer to the original source.
    code = IGNORED.sub(lambda m: re.sub(r"[^\n]", " ", m.group()), source)
    for match in ALLOCATOR.finditer(code):
        depth = 1
        end = match.end()
        while end < len(code) and depth:
            depth += (code[end] == "(") - (code[end] == ")")
            end += 1
        if depth:
            raise ValueError(
                f"Unbalanced allocation expression at offset {match.start()}"
            )
        yield {
            "line": source.count("\n", 0, match.start()) + 1,
            "allocator": re.sub(
                r"\s+", " ", code[match.start() : match.end() - 1].strip()
            ),
            "expression": re.sub(r"\s+", " ", source[match.start() : end]).strip(),
            "explicit_sizeof": bool(re.search(r"\bsizeof\b", code[match.end() : end])),
        }


def inventory():
    tracked = subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT)
    paths = [Path(p.decode()) for p in tracked.split(b"\0") if p]
    paths = sorted(p for p in paths if p.suffix.lower() in SUFFIXES)
    records = []
    for path in paths:
        source = (ROOT / path).read_text()
        records.extend(
            {"path": path.as_posix(), **record} for record in expressions(source)
        )
    return {
        "files_scanned": len(paths),
        "expressions": records,
        "counts_by_root": dict(
            collections.Counter(r["path"].split("/")[0] for r in records)
        ),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--json", action="store_true", help="emit the complete JSON inventory"
    )
    parser.add_argument(
        "--without-sizeof",
        action="store_true",
        help="show byte-count review candidates",
    )
    args = parser.parse_args()
    result = inventory()
    if args.without_sizeof:
        result["expressions"] = [
            r for r in result["expressions"] if not r["explicit_sizeof"]
        ]
    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print(
            f"{result['files_scanned']} tracked C/C++ files; {len(result['expressions'])} expressions"
        )
        for record in result["expressions"]:
            print(f"{record['path']}:{record['line']}: {record['expression']}")


if __name__ == "__main__":
    main()

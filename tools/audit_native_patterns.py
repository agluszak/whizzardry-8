#!/usr/bin/env python3
"""Inventory native-port review patterns across ALL tracked C/C++ source text.

Includes inactive branches, vendor code, extension modules and headers. These
are lexical leads, not diagnoses: external ABI types, pixel/audio byte strides,
bit-pattern conversions and POD raw storage may be correct. Pair this inventory
with audit_native_layouts.py and producer/consumer evidence before editing.
"""

import argparse
import collections
import json
import re
import subprocess
from pathlib import Path

from audit_allocations import IGNORED, ROOT, SUFFIXES

PATTERNS = {
    "raw_io": r"\b(?:FileRead|FileWrite|W8ReadFile|W8WriteFile|fread|fwrite|Read|Write)\s*\(",
    "ordering": r"\b(?:qsort|bsearch)\s*\(",
    "long": r"\blong\b",
    "typed_cast": r"\b(?:reinterpret_cast|static_cast)\s*<[^;{}\n]*\*[^;{}\n]*>\s*\(|\(\s*(?:unsigned\s+|signed\s+|const\s+)*(?:short|int|long|float|double|w8_ulong|w8_long|u?int\d+_t)\s*\*\s*\)",
    "integer_cast": r"\b(?:reinterpret_cast|static_cast)\s*<\s*(?:unsigned\s+|signed\s+)?(?:int|long|w8_long|w8_ulong|u?intptr_t|u?int32_t)\s*>\s*\(",
    "lifetime": r"\b(?:operator\s+(?:new|delete)|delete|free|MemFree|reallocate|realloc|memcpy|memmove)\b",
    "conversion": r"\b(?:floor|ceil|lrint|lround|isfinite|isnan|ftol)\w*\s*\(|\bstatic_cast\s*<\s*(?:unsigned\s+|signed\s+)?(?:char|short|int|w8_long|w8_ulong)\s*>\s*\(",
    "boundary": r"\b(?:NULL|nullptr)\b|(?:==|!=|<=|>=|=)\s*-1\b|\b(?:end|cursor|current)\s*-\s*\b(?:begin|start|base)\b",
}


def inventory():
    tracked = subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT)
    paths = sorted(Path(p.decode()) for p in tracked.split(b"\0") if p)
    paths = [p for p in paths if p.suffix.lower() in SUFFIXES]
    facts = []
    for path in paths:
        source = (ROOT / path).read_text()
        code = IGNORED.sub(lambda m: re.sub(r"[^\n]", " ", m.group()), source)
        lines = source.splitlines()
        for kind, pattern in PATTERNS.items():
            for match in re.finditer(pattern, code):
                line = source.count("\n", 0, match.start()) + 1
                facts.append(
                    {
                        "kind": kind,
                        "path": str(path),
                        "line": line,
                        "source": lines[line - 1].strip(),
                    }
                )
    facts.sort(key=lambda f: (f["path"], f["line"], f["kind"]))
    return {
        "files_scanned": len(paths),
        "counts": dict(collections.Counter(f["kind"] for f in facts)),
        "facts": facts,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kind", choices=PATTERNS)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    result = inventory()
    if args.kind:
        result["facts"] = [f for f in result["facts"] if f["kind"] == args.kind]
    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print(f"{result['files_scanned']} files; review leads: {result['counts']}")
        for fact in result["facts"]:
            print(f"{fact['path']}:{fact['line']}: {fact['kind']}: {fact['source']}")


if __name__ == "__main__":
    main()

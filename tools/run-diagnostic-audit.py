#!/usr/bin/env python3
"""Replay first-party C++ compilation with experimental diagnostic groups.

Warnings are reported for review; syntax/compilation failures fail the audit.
Use a separate, non-PCH, non-unity compilation database, as for clang-tidy.
"""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--output", type=Path, default=Path("diagnostic-audit.log"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    entries = json.loads((args.build / "compile_commands.json").read_text())
    sources = {}
    for entry in entries:
        source = (Path(entry["directory"]) / entry["file"]).resolve()
        if source.suffix not in (".cc", ".cpp", ".cxx"):
            continue
        if not any(source.is_relative_to(root / part) for part in ("src", "tools")):
            continue
        sources[source] = entry

    if not sources:
        parser.error("Compilation database contains no first-party C++ sources")
    if args.jobs < 1:
        parser.error("--jobs must be positive")

    def audit(item):
        source, entry = item
        command = entry.get("arguments") or shlex.split(entry["command"])
        command = [arg for arg in command if arg != "-c"]
        command = [*command, "-fsyntax-only", "-o", os.devnull, "-Wno-error",
                   "-Wshorten-64-to-32", "-Wswitch-enum", "-Wlifetime-safety-strict"]
        # Concentrate buffer diagnostics at image and serialized-data boundaries.
        if any(word in source.stem.lower() for word in
               ("image", "pixel", "blit", "retail", "chunk", "loadsave", "packed",
                "readmesh", "readlevel", "npc_script", "unicode", "prepare")):
            command.append("-Wunsafe-buffer-usage")
        result = subprocess.run(command, cwd=entry["directory"],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, check=False)
        return source, result

    failures = 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w") as report, ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for source, result in pool.map(audit, sources.items()):
            report.write(f"\n{source.relative_to(root)}\n{result.stdout}")
            failures += result.returncode != 0
    print(f"Audited {len(sources)} sources; {failures} compiler failures. Report: {args.output}")
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())

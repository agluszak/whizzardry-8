#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build=${1:-"$root/build/linux"}
shift "$(( $# > 0 ? 1 : 0 ))"
tidy=${CLANG_TIDY:-clang-tidy}
driver=${RUN_CLANG_TIDY:-run-clang-tidy}
config=${CLANG_TIDY_CONFIG:-"$root/.clang-tidy"}

# In CI, pass a NUL-delimited git diff with --changed-files PATH. Direct source
# edits only affect their translation units. Any changed header, build input or
# clang-tidy configuration requires a full analysis.
changed_files=
if [[ ${1:-} == --changed-files ]]; then
    if [[ $# -lt 2 || ! -f $2 ]]; then
        echo "--changed-files requires an existing NUL-delimited file" >&2
        exit 2
    fi
    changed_files=$2
    shift 2
fi

if [[ ! -f "$build/compile_commands.json" ]]; then
    echo "Missing $build/compile_commands.json; configure with CMAKE_EXPORT_COMPILE_COMMANDS=ON first." >&2
    exit 1
fi
"$tidy" --verify-config --config-file="$config"

# The full database remains the default, including on pushes to main.
pattern=$(python3 -c 'import re, sys; print("^" + re.escape(sys.argv[1]) + "/(src|tests)/")' "$root")
if [[ -n $changed_files ]]; then
    selection=$(python3 - "$root" "$changed_files" <<'PY'
import os
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
changed = [os.fsdecode(p) for p in Path(sys.argv[2]).read_bytes().split(b"\0") if p]
sources = []
full = False
for name in changed:
    path = Path(name)
    if name.startswith(("src/", "tests/")) and path.suffix in (".c", ".cc", ".cpp", ".cxx"):
        sources.append(re.escape(str(root / name)))
    elif name in ("README.md", "LICENSE", ".gitignore", "AGENTS.md", "CONTRIBUTING.md") or name.startswith(("docs/", ".github/ISSUE_TEMPLATE/")):
        continue
    else:
        # Headers, CMake, workflow, lint config, scripts and unknown inputs can
        # affect more than one translation unit.
        full = True
        break

if full:
    print("FULL")
elif sources:
    print("^(?:" + "|".join(sorted(set(sources))) + ")$")
else:
    print("SKIP")
PY
    )
    case "$selection" in
        FULL) ;;
        SKIP)
            echo "No affected first-party translation units; skipping clang-tidy."
            exit 0
            ;;
        *) pattern=$selection ;;
    esac
fi
"$driver" -clang-tidy-binary "$tidy" -config-file "$config" \
    -p "$build" -j "${CLANG_TIDY_JOBS:-4}" "$pattern" "$@"

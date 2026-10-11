#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build=${1:-"$root/build/linux"}
shift "$(( $# > 0 ? 1 : 0 ))"
tidy=${CLANG_TIDY:-clang-tidy}
driver=${RUN_CLANG_TIDY:-run-clang-tidy}
config=${CLANG_TIDY_CONFIG:-"$root/.clang-tidy"}

if [[ ! -f "$build/compile_commands.json" ]]; then
    echo "Missing $build/compile_commands.json; configure with CMAKE_EXPORT_COMPILE_COMMANDS=ON first." >&2
    exit 1
fi
"$tidy" --verify-config --config-file="$config"
# Use the complete first-party database rather than only changed files: header
# changes can affect any translation unit. Downloaded dependencies are excluded.
pattern=$(python3 -c 'import re, sys; print("^" + re.escape(sys.argv[1]) + "/(src|tests)/")' "$root")
"$driver" -clang-tidy-binary "$tidy" -config-file "$config" \
    -p "$build" -j "${CLANG_TIDY_JOBS:-4}" "$pattern" "$@"

#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build=${1:-"$root/build/linux"}
report=${2:-"$build/iwyu-report.txt"}

driver=$(command -v iwyu_tool || command -v iwyu_tool.py || true)
if ! command -v include-what-you-use >/dev/null || [[ -z "$driver" ]]; then
    echo 'Install include-what-you-use and iwyu_tool (Homebrew: brew install include-what-you-use).' >&2
    exit 1
fi
if [[ ! -f "$build/compile_commands.json" ]]; then
    echo "Missing $build/compile_commands.json; configure with CMAKE_EXPORT_COMPILE_COMMANDS=ON first." >&2
    exit 1
fi

mkdir -p -- "$(dirname -- "$report")"
include-what-you-use --version
# Directory selectors omit downloaded dependencies but retain first-party tests.
# Keep the raw IWYU format so the report can be reviewed with fix_includes.py.
status=0
"$driver" -p "$build" -j "${IWYU_JOBS:-4}" "$root/src" "$root/tests" -- \
    -Xiwyu "--mapping_file=$root/tools/iwyu.imp" >"$report" 2>&1 || status=$?
echo "IWYU report: $report"
exit "$status"

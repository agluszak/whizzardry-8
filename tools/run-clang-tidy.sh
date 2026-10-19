#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build=${1:-"$root/build/linux"}
shift "$(( $# > 0 ? 1 : 0 ))"
tidy=${CLANG_TIDY:-clang-tidy}
driver=${RUN_CLANG_TIDY:-run-clang-tidy}
config=${CLANG_TIDY_CONFIG:-"$root/.clang-tidy"}

# In CI, pass a NUL-delimited git diff with --changed-files PATH. Changed
# headers are mapped to their dependent translation units using clang -M;
# unknown build inputs and dependency scan failures require a full analysis.
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

# The full database remains the default, including pushes to main.
pattern=$(python3 -c 'import re, sys; print("^" + re.escape(sys.argv[1]) + "/(src|tests|tools)/")' "$root")
if [[ -n $changed_files ]]; then
    selection=$(python3 "$root/tools/affected-tidy-sources.py" \
        --root "$root" \
        --compile-commands "$build/compile_commands.json" \
        --changed-files "$changed_files")
    case "$selection" in
        FULL) ;;
        SKIP)
            echo "No affected first-party translation units; skipping clang-tidy."
            exit 0
            ;;
        *) pattern=$selection ;;
    esac
fi
header_filter=$(python3 -c 'import re, sys; print("^" + re.escape(sys.argv[1]) + "/(include|src|tests|tools)/")' "$root")
if [[ -n ${CLANG_TIDY_CACHE_DIR:-} ]]; then
    python3 "$root/tools/run-clang-tidy-cached.py" \
        --root "$root" --build "$build" --config "$config" \
        --tidy "$tidy" --driver "$driver" --pattern "$pattern" \
        --header-filter "$header_filter" --jobs "${CLANG_TIDY_JOBS:-4}" \
        --cache "$CLANG_TIDY_CACHE_DIR" -- "$@"
else
    "$driver" -header-filter "$header_filter" -clang-tidy-binary "$tidy" -config-file "$config" \
        -p "$build" -j "${CLANG_TIDY_JOBS:-4}" "$pattern" "$@"
fi

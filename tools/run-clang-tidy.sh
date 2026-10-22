#!/usr/bin/env bash
set -euo pipefail
export PYTHONUNBUFFERED=1

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build=${1:-"$root/build/linux"}
shift "$(( $# > 0 ? 1 : 0 ))"
tidy=${CLANG_TIDY:-clang-tidy}
driver=${RUN_CLANG_TIDY:-run-clang-tidy}
config=${CLANG_TIDY_CONFIG:-"$root/.clang-tidy"}
shard_index=${CLANG_TIDY_SHARD_INDEX:-0}
shard_count=${CLANG_TIDY_SHARD_COUNT:-1}

if [[ ! -f "$build/compile_commands.json" ]]; then
    echo "Missing $build/compile_commands.json; configure with CMAKE_EXPORT_COMPILE_COMMANDS=ON first." >&2
    exit 1
fi
"$tidy" --verify-config --config-file="$config"

# Every first-party translation unit in the shard is a candidate. With
# CLANG_TIDY_CACHE_DIR set, the cache analyzes only those whose source,
# headers, configuration, compiler or command changed since a clean run.
pattern=$(python3 -c 'import re, sys; print("^" + re.escape(sys.argv[1]) + "/(src|tests|tools)/")' "$root")
if [[ $shard_count != 1 || $shard_index != 0 ]]; then
    pattern=$(python3 "$root/tools/clang_tidy_shard.py" \
        --root "$root" --compile-commands "$build/compile_commands.json" \
        --pattern "$pattern" --index "$shard_index" --count "$shard_count")
    if [[ $pattern == SKIP ]]; then
        echo "No translation units in shard $shard_index of $shard_count."
        exit 0
    fi
fi
header_filter=$(python3 -c 'import re, sys; print("^" + re.escape(sys.argv[1]) + "/(include|src|tests|tools)/")' "$root")
if [[ -n ${CLANG_TIDY_CACHE_DIR:-} && ${CLANG_TIDY_DISABLE_CACHE:-false} != true ]]; then
    python3 "$root/tools/run-clang-tidy-cached.py" \
        --root "$root" --build "$build" --config "$config" \
        --tidy "$tidy" --driver "$driver" --pattern "$pattern" \
        --header-filter "$header_filter" --jobs "${CLANG_TIDY_JOBS:-4}" \
        --cache "$CLANG_TIDY_CACHE_DIR" -- "$@"
else
    "$driver" -header-filter "$header_filter" -clang-tidy-binary "$tidy" -config-file "$config" \
        -p "$build" -j "${CLANG_TIDY_JOBS:-4}" "$pattern" "$@"
fi

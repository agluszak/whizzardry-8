#!/usr/bin/env bash
# Compares two clang-cl legacy builds object by object, ignoring addresses,
# string-literal symbol names and anonymous-namespace hashes (both depend on
# the source path). Prints "<changed lines> <object>" for each difference.
#
# Usage: tools/legacy_objdiff.sh BASE_BUILD_DIR NEW_BUILD_DIR [SUBDIR]
set -euo pipefail
norm() {
    llvm-objdump -d -r --no-show-raw-insn --no-leading-addr "$1" 2>/dev/null | tail -n +3 |
        sed -E 's/\?\?_C@[^ ]*/STR/g; s/\?A(0x)?[0-9A-Fa-fN]+@/?ANON@/g; s/^\s*[0-9a-f]+:\s*//; s/0x[0-9a-f]+/N/g'
}
base=$1 new=$2 subdir=${3:-src}
status=0
while IFS= read -r object; do
    case "$object" in */WIZ8_ZLIB_1_0_4.dir/*) continue ;; esac
    if [[ ! -f "$new/$object" ]]; then
        echo "missing $object"; status=1; continue
    fi
    count=$(diff <(norm "$base/$object") <(norm "$new/$object") | grep -c '^[<>]' || true)
    if [[ $count != 0 ]]; then
        echo "$count $object"; status=1
    fi
done < <(cd "$base" && find "$subdir" -name '*.obj' | sort)
exit $status

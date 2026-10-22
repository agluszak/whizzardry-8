#!/usr/bin/env bash
# Claude Code cloud sessions: share the ccache directory between project threads
# through the project's shared files, so a thread's first build reuses objects an
# earlier thread compiled:
#   tools/cloud-ccache.sh restore   # tools/cloud-deps.sh runs this
#   tools/cloud-ccache.sh save      # after a successful build
# WIZ8_CCACHE_ARCHIVE_DIR overrides where the archive lives; without that directory
# both commands do nothing.
set -euo pipefail

archive_dir=${WIZ8_CCACHE_ARCHIVE_DIR:-/mnt/project-files/cache/ccache}
archive=$archive_dir/ccache-linux.tar
cache_dir=$(ccache --get-config cache_dir)

case ${1:-} in
restore)
    mkdir -p "$cache_dir"
    if [[ -f $archive ]]; then
        tar -xf "$archive" -C "$cache_dir"
        echo "Restored ccache from $archive."
    fi
    # Every clone lives at the same path, but PCH and __DATE__ need these to hit.
    ccache --set-config base_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
    ccache --set-config hash_dir=false
    ccache --set-config sloppiness=pch_defines,time_macros
    ccache --set-config max_size=2G
    ccache --zero-stats >/dev/null
    ;;
save)
    [[ -d $(dirname -- "$archive_dir") ]] || exit 0
    mkdir -p "$archive_dir"
    ccache --cleanup >/dev/null
    tar -cf "$archive.$$" -C "$cache_dir" .
    mv "$archive.$$" "$archive"
    echo "Saved ccache to $archive."
    ;;
*)
    echo "usage: $0 restore|save" >&2
    exit 2
    ;;
esac

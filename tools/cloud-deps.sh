#!/usr/bin/env bash
# Claude Code cloud sessions: run tools/setup-linux.sh with the proxy workarounds, and
# share the Conan packages it builds between sessions. Project threads keep an archive
# in the project's shared files; a later thread restores it instead of rebuilding
# FFmpeg, SDL and the rest:
#   tools/cloud-deps.sh
# WIZ8_CONAN_ARCHIVE_DIR overrides where the archives live. Without that directory the
# script only runs tools/setup-linux.sh.
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
archive_dir=${WIZ8_CONAN_ARCHIVE_DIR:-/mnt/project-files/cache/conan}
key=$(cat conanfile.py conan.lock conan/profiles/* $(find conan/recipes -type f | sort) |
    sha256sum | cut -c1-16)
archive=$archive_dir/conan-$key.tgz

if [[ -f $archive ]] && command -v conan >/dev/null; then
    conan cache restore "$archive" >/dev/null
    echo "Restored Conan packages from $archive."
fi

# The session proxy serves git but not GitHub source archives.
WIZ8_SYSTEM_GLSLANG=1 WIZ8_GIT_SOURCES=1 tools/setup-linux.sh

# Compile through the shared ccache; Clang's PCH timestamp would defeat it.
tools/cloud-ccache.sh restore
cmake -B build/linux -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_FLAGS="-Xclang -fno-pch-timestamp" >/dev/null

if [[ ! -f $archive && -d $(dirname -- "$archive_dir") ]]; then
    mkdir -p "$archive_dir"
    conan cache clean '*' >/dev/null
    conan cache save '*/*:*' --file "${archive%.tgz}.$$.tgz" >/dev/null
    mv "${archive%.tgz}.$$.tgz" "$archive"
    echo "Saved Conan packages to $archive."
fi

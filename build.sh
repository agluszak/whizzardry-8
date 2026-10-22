#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$root"

# Local builds and CI execute the same dependency and CMake commands.
if [[ ${WIZ8_BUILD_CONTAINER:-} == 1 ]]; then
    if [[ ${1:-} == -- ]]; then
        shift
        exec "$@"
    fi
    preset=${1:-linux}
    action=${2:-build}
    case $preset in
        linux) build_type=Release ;;
        linux-dev|linux-tidy) build_type=Debug ;;
        linux-asan|linux-integer-audit) build_type=RelWithDebInfo ;;
        *) echo "Unknown Linux preset: $preset" >&2; exit 2 ;;
    esac
    if [[ $action != build && $action != --configure-only ]]; then
        echo "Usage: ./build.sh [preset] [--configure-only]" >&2
        exit 2
    fi

    conan export conan/recipes/libalsa >/dev/null
    CC=clang-23 CXX=clang++-23 conan install . \
        -pr:h conan/profiles/linux-clang23 -pr:b conan/profiles/linux-gcc16 \
        -s:h "build_type=$build_type" \
        -c tools.system.package_manager:mode=check \
        --build=missing -of "build/$preset/conan"

    # shellcheck disable=SC1090
    source "build/$preset/conan/conanbuild.sh"
    cmake --preset "$preset" \
        -DCMAKE_TOOLCHAIN_FILE="$root/build/$preset/conan/conan_toolchain.cmake"
    if [[ $action == build ]]; then
        cmake --build --preset "$preset" --parallel "$(nproc)"
    fi
    exit 0
fi

if [[ $(uname -s) != Linux ]]; then
    echo "The container build currently supports Linux only." >&2
    exit 2
fi

image=${WIZ8_BUILD_IMAGE:-"whizzardry-build:$(sha256sum Dockerfile | cut -c1-12)"}
if ! docker image inspect "$image" >/dev/null 2>&1; then
    docker build -t "$image" .
fi
image_id=$(docker image inspect --format '{{.Id}}' "$image")
key=${image_id#sha256:}
key=${key:0:16}
cache="$root/build/cache/$key"
mkdir -p "$cache/home" "$cache/conan" "$root/build/ccache"

# The compiler and system headers may change when the image changes.
if [[ -f build/.toolchain-id && $(<build/.toolchain-id) != "$image_id" ]]; then
    rm -rf build/linux build/linux-dev build/linux-asan \
        build/linux-integer-audit build/linux-tidy
fi
printf '%s\n' "$image_id" > build/.toolchain-id

args=(--rm --init --user "$(id -u):$(id -g)"
    --volume "$root:$root" --workdir "$root"
    --env WIZ8_BUILD_CONTAINER=1
    --env "HOME=$cache/home" --env "CONAN_HOME=$cache/conan"
    --env "CCACHE_DIR=$root/build/ccache"
    --env "CCACHE_BASEDIR=$root" --env CCACHE_NOHASHDIR=true
    --env CCACHE_MAXSIZE=1G
    --env CCACHE_SLOPPINESS=pch_defines,time_macros)

if [[ -n ${RUNNER_TEMP:-} ]]; then
    args+=(--volume "$RUNNER_TEMP:$RUNNER_TEMP" --env RUNNER_TEMP)
fi
if [[ -n ${WIZ8_ASSET_ROOT:-} ]]; then
    args+=(--volume "$WIZ8_ASSET_ROOT:$WIZ8_ASSET_ROOT:ro" --env WIZ8_ASSET_ROOT)
fi
for var in VK_DRIVER_FILES VK_INSTANCE_LAYERS UBSAN_OPTIONS LD_PRELOAD \
    CLANG_TIDY CLANG_TIDY_CONFIG CLANG_TIDY_JOBS CLANG_TIDY_SHARD_INDEX \
    CLANG_TIDY_SHARD_COUNT CLANG_TIDY_CACHE_DIR CLANG_TIDY_DISABLE_CACHE \
    RUN_CLANG_TIDY PYTHONUNBUFFERED WIZ8_USER_ROOT; do
    if [[ ${!var+x} ]]; then args+=(--env "$var"); fi
done

exec docker run "${args[@]}" "$image" ./build.sh "$@"

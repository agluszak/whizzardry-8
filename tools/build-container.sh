#!/usr/bin/env bash
set -euo pipefail

repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mkdir -p "$HOME/.conan2" "$repo/build/ccache"

args=(
    --rm --user "$(id -u):$(id -g)"
    --volume "$repo:$repo"
    --volume "$HOME/.conan2:$HOME/.conan2"
    --workdir "$repo"
    --env HOME="$HOME"
    --env CONAN_HOME="$HOME/.conan2"
    --env CCACHE_DIR="$repo/build/ccache"
    --env CCACHE_BASEDIR="$repo"
    --env CCACHE_NOHASHDIR="${CCACHE_NOHASHDIR:-true}"
    --env CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-500M}"
    --env CCACHE_SLOPPINESS="${CCACHE_SLOPPINESS:-pch_defines,time_macros}"
)
if [[ -n ${RUNNER_TEMP:-} ]]; then
    args+=(--volume "$RUNNER_TEMP:$RUNNER_TEMP" --env RUNNER_TEMP="$RUNNER_TEMP")
fi
if [[ -n ${WIZ8_ASSET_ROOT:-} ]]; then
    args+=(--volume "$WIZ8_ASSET_ROOT:$WIZ8_ASSET_ROOT:ro"
           --env WIZ8_ASSET_ROOT="$WIZ8_ASSET_ROOT")
fi

exec docker run "${args[@]}" whizzardry-build:latest "$@"

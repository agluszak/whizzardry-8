#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if [[ -f "$repo_dir/.env" ]]; then
    set -a
    source "$repo_dir/.env"
    set +a
fi

build_dir=${WIZ8_BUILD_DIR:-"$repo_dir/build-clang/launch"}
game_dir=${WIZ8_RUN_DIR:-"$repo_dir/build/run-clang"}
runner=${WIZ8_UMU_RUN:-umu-run}
export WINEPREFIX=${WIZ8_WINE_PREFIX:-${WINEPREFIX:-}}
export WINEDLLOVERRIDES="sr=n${WINEDLLOVERRIDES:+;$WINEDLLOVERRIDES}"

for dependency in "$runner" xdotool; do
    if ! command -v "$dependency" >/dev/null; then
        printf 'Required launcher dependency not found: %s\n' "$dependency" >&2
        exit 1
    fi
done
for binary in Wiz8.exe sr.dll; do
    if [[ ! -f "$build_dir/$binary" ]]; then
        printf 'Missing rebuilt binary: %s/%s\n' "$build_dir" "$binary" >&2
        exit 1
    fi
done
if [[ ! -d "$game_dir/Data" || ! -d "$game_dir/Dll" ]]; then
    printf 'Game assets must be staged in %s (Data/ and Dll/).\n' "$game_dir" >&2
    exit 1
fi
if [[ -z ${DISPLAY:-} || -z $WINEPREFIX ]]; then
    printf 'Set DISPLAY and WIZ8_WINE_PREFIX (or WINEPREFIX) before launching.\n' >&2
    exit 1
fi

cp -- "$build_dir/Wiz8.exe" "$build_dir/sr.dll" "$game_dir/"
cd -- "$game_dir"
mkdir -p diagnostics
printf 'Launching Wizardry 8; log: %s/diagnostics/launch.log\n' "$game_dir"
"$runner" "$PWD/Wiz8.exe" /WINDOW "$@" </dev/null >diagnostics/launch.log 2>&1 &
game_pid=$!

# XWayland also needs a desktop activation request: starting Wine from a
# terminal does not reliably transfer keyboard focus to its game window.
# Activate each startup window once; leave subsequent user focus changes alone.
(
    focused_windows=' '
    for ((attempt = 0; attempt < 240; attempt++)); do
        kill -0 "$game_pid" 2>/dev/null || exit 0
        while read -r window; do
            [[ -n $window && $focused_windows != *" $window "* ]] || continue
            if xdotool windowactivate "$window" windowfocus "$window" 2>/dev/null; then
                focused_windows+="$window "
            fi
        done < <(xdotool search --onlyvisible --name '^Wizardry 8$' 2>/dev/null || true)
        sleep 0.25
    done
) &
focus_pid=$!
trap 'kill "$focus_pid" 2>/dev/null || true' EXIT
trap 'kill "$game_pid" 2>/dev/null || true' INT TERM

if wait "$game_pid"; then
    exit 0
else
    status=$?
    printf 'Wizardry 8 exited with status %s. See %s/diagnostics/launch.log\n' \
        "$status" "$game_dir" >&2
    exit "$status"
fi

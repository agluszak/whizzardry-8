#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if [[ -f "$repo_dir/.env" ]]; then
    set -a
    source "$repo_dir/.env"
    set +a
fi

case $(uname -s) in
    Darwin)
        if [[ $(uname -m) == arm64 ]]; then preset=macos-arm64; else preset=macos-x64; fi
        ;;
    MINGW*|MSYS*|CYGWIN*) preset=windows-clangcl ;;
    *) preset=linux ;;
esac
build_dir=${WIZ8_BUILD_DIR:-"$repo_dir/build/$preset"}
executable="$build_dir/Wiz8Native"
if [[ -x "$executable.exe" ]]; then executable="$executable.exe"; fi
asset_root=${WIZ8_ASSET_ROOT:-}
if [[ $(uname -s) == Darwin ]]; then
    user_root=${WIZ8_USER_ROOT:-"$HOME/Library/Application Support/whizzardry8"}
else
    user_root=${WIZ8_USER_ROOT:-"${XDG_DATA_HOME:-$HOME/.local/share}/whizzardry8"}
fi

if [[ ! -x "$executable" ]]; then
    printf 'Missing native executable: %s\nBuild it with: cmake --build %q\n' \
        "$executable" "$build_dir" >&2
    exit 1
fi
if [[ -z "$asset_root" || ( ! -d "$asset_root/Data" && ! -d "$asset_root/data" ) ]]; then
    printf 'Set WIZ8_ASSET_ROOT to your installed Wizardry 8 directory (containing Data/).\n' >&2
    exit 1
fi
build_dir=$(cd -- "$build_dir" && pwd)
executable="$build_dir/$(basename -- "$executable")"
WIZ8_ASSET_ROOT=$(cd -- "$asset_root" && pwd)
export WIZ8_ASSET_ROOT
mkdir -p -- "$user_root"
WIZ8_USER_ROOT=$(cd -- "$user_root" && pwd)
export WIZ8_USER_ROOT
if [[ "$WIZ8_USER_ROOT/" == "$WIZ8_ASSET_ROOT/"* ||
      "$WIZ8_ASSET_ROOT/" == "$WIZ8_USER_ROOT/"* ]]; then
    printf 'WIZ8_USER_ROOT and WIZ8_ASSET_ROOT must be separate directories.\n' >&2
    exit 1
fi

# Initialize the writable overlay once; keep the user's later video choices.
if [[ ! -e "$WIZ8_USER_ROOT/3DVideo.CFG" ]]; then
    printf 'SDLGPU\n640\n480\n16\nminiaudio spatial\n' > "$WIZ8_USER_ROOT/3DVideo.CFG"
fi
mkdir -p -- "$WIZ8_USER_ROOT/diagnostics"
log_file="$WIZ8_USER_ROOT/diagnostics/launch.log"
printf 'Launching Whizzardry 8; log: %s\nClose the game window or press Ctrl-C to quit.\n' "$log_file"
cd -- "$repo_dir"
exec "$executable" /WINDOW "$@" </dev/null > "$log_file" 2>&1

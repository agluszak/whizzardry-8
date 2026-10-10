# Whizzardry 8

Native Wizardry 8 and SurRender for 64-bit Linux, macOS and Windows, built with Clang.
The build uses SDL3 GPU rendering, FFmpeg video decoding,
miniaudio sound and system zlib. CMake downloads a pinned miniaudio revision.
Game data comes from an existing retail installation and is not distributed here.

## Build

Install CMake 3.21+, Git, Ninja and Clang. The build bootstraps pinned vcpkg
and installs SDL3, FFmpeg, zlib and the host shader compiler under the build
tree. Miniaudio is fetched separately by CMake. Linux also needs SDL's system
X11/Wayland development interfaces, build tools (including NASM and pkg-config)
and a working Vulkan driver.

```sh
cmake --preset linux
cmake --build --preset linux
ctest --preset linux
```

Use `macos-arm64`, `macos-x64` or `windows-clangcl` in the same commands.
Windows uses the same LLVM compiler with its MSVC-compatible frontend; run
from a Visual Studio developer shell with LLVM installed. The Windows preset
installs native x64 dependencies, not the removed 32-bit recompilation lane.
The existing POSIX runtime still needs the subsequent platform-API migration
before a passing Windows game build can be claimed.

`VCPKG_ROOT` may point at an existing vcpkg checkout; otherwise its pinned
release is downloaded automatically. Dependencies need no separate manual
installation, but the compiler and platform SDK/system interfaces do.

The native targets include `Wiz8Native` and SurRender (`libsr.so` on Linux).
A Vulkan-capable SDL3 GPU backend is required for graphics checks.

## Run

Point the launcher to your installed game assets:

```sh
WIZ8_ASSET_ROOT=/path/to/Wizardry8 ./run.sh /WINDOW
```

The launcher accepts an optional `.env` file and passes arguments through to
the game. It uses the host's preset build directory unless `WIZ8_BUILD_DIR` is specified.
The launcher leaves display and GPU driver selection to SDL and the host.
`WIZ8_GPU_DEBUG=1` enables SDL GPU debugging and
`WIZ8_SRDD_TRACE=1` traces SurRender draws.

Game assets and writable saves/configuration are separate. The virtual `C:\`
drive maps to `WIZ8_ASSET_ROOT`; file reads prefer `WIZ8_USER_ROOT`, then
fall back to the installed assets. All writes go to the user root.
By default, this is `$XDG_DATA_HOME/whizzardry8` (or
`~/.local/share/whizzardry8` on Linux and
`~/Library/Application Support/whizzardry8` on macOS).
Optional `WIZ8_CD1_ROOT` through `WIZ8_CD3_ROOT` provide read-only virtual
disc drives. The launcher writes diagnostics to the user root.

## Native tests

The CTest suite covers portable file/SLF operations, SDL events/timers,
CRT and pointer semantics, serialization, compression, blitters, JPEG transfer,
audio and movie decoding, and SurRender interfaces. GPU tests need a working
display/Vulkan driver; `native_events` uses SDL's dummy driver.

With installed game assets, run the focused graphics and world harnesses:

```sh
WIZ8_ASSET_ROOT=/path/to/Wizardry8 build/linux/native_game_graphics
WIZ8_ASSET_ROOT=/path/to/Wizardry8 build/linux/native_world_graphics party.CHR \
    "$PWD/build/linux/world.ppm"
```

The world harness accepts additional `save`, `kill` and `angles` modes for
its respective focused checks. The movie fixture can be regenerated with
`tests/native/generate_movie.sh`.

## Source and format conventions

The original game has a 32-bit data model, two-byte strings and packed binary
records. Native storage may use 64-bit pointers, but saved pointer words and
format-defined sizes must preserve their serialized layout. `w8_long` and
`w8_ulong` name historical 32-bit fields; `W8_PTR32` represents disk-size
pointer slots. Text/CRT boundaries use explicit conversion rather than host
wide-string routines that expect four-byte `wchar_t`.

There is no Wine runner, 32-bit target or legacy fallback.
The platform adapters in `src/compat/` are temporary migration boundaries,
not a permanent Windows emulation layer. See [remaining native work](NATIVE_WORK.md).

Third-party source licenses, including the SGP license, remain with
their respective sources.

# Whizzardry 8

Native Wizardry 8 and SurRender for 64-bit Linux, macOS and Windows, built with Clang and C++23.
The build uses SDL3 GPU rendering and CPU surfaces, SDL3_image with JPEG,
SDL3_mixer with MP3, FFmpeg video decoding and zlib.
Game data comes from an existing retail installation and is not distributed here.

## Build

Install CMake 3.21+, Ninja, Clang, pkg-config, glslang, SDL3, SDL3_image,
SDL3_mixer, FFmpeg development libraries and zlib through the host package
manager.

On Ubuntu 26.04, the SDL3_mixer package is not in the release archive yet.
Install the regular dependencies from apt and the prebuilt SDL3_mixer package
from the Ubuntu archive pool:

```sh
sudo apt-get install clang ninja-build cmake curl pkg-config glslang-tools \
    zlib1g-dev libsdl3-dev libsdl3-image-dev \
    libavcodec-dev libavformat-dev libswresample-dev libswscale-dev
curl -fsSLO https://archive.ubuntu.com/ubuntu/pool/universe/libs/libsdl3-mixer/libsdl3-mixer0_3.2.4+ds-1_amd64.deb
curl -fsSLO https://archive.ubuntu.com/ubuntu/pool/universe/libs/libsdl3-mixer/libsdl3-mixer-dev_3.2.4+ds-1_amd64.deb
sudo apt-get install ./libsdl3-mixer0_3.2.4+ds-1_amd64.deb \
    ./libsdl3-mixer-dev_3.2.4+ds-1_amd64.deb
```

Then build normally:

```sh
cmake --preset linux
cmake --build --preset linux
ctest --preset linux
```

Use `macos-arm64`, `macos-x64` or `windows-clangcl` in the same commands once
the matching host packages are installed. Windows uses the same LLVM compiler
with its MSVC-compatible frontend; run from a Visual Studio C++ developer shell
with LLVM and the Windows SDK installed.

The native targets include `Wiz8Native` and statically linked SurRender and
the SDL-backed filesystem and runtime, sharing one SDL state.
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
The default comes from SDL's preference directory for
`Whizzardry/whizzardry8`. Existing populated legacy `whizzardry8` directories
are reused only when the preferred directory is empty. If both contain saves,
the preferred directory wins; set `WIZ8_USER_ROOT` explicitly to use the other.
The launcher queries this same policy rather than inventing another location.
Optional `WIZ8_CD1_ROOT` through `WIZ8_CD3_ROOT` provide read-only virtual
disc drives. The launcher writes diagnostics to the user root.

## Native tests

The CTest suite covers portable file/SLF operations, SDL events/timers,
CRT and pointer semantics, serialization, compression, image decoding/virtual
transfers, CPU surfaces, pixel/blitter differentials, offline/positional audio,
typed ownership/lifetimes, fatal assertions, movie decoding and SurRender interfaces.
GPU tests need a working
display/Vulkan driver; `native_events` uses SDL's dummy driver.
CI builds Linux with native Clang; the first-party host-width wide-string import
check runs only on Unix. macOS, Windows and interactive graphics/gameplay remain
unverified in CI.

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

Use `std::string` for owning text (UTF-8 for Unicode), `std::string_view` for
borrowed read-only text, and `std::filesystem::path` for native host paths.
Pass `.c_str()` at C-string boundaries; serialized buffers and two-byte game
text retain their format-defined representation.

The original game has a 32-bit data model, two-byte strings and packed binary
records. Native storage may use 64-bit pointers, but saved pointer words and
format-defined sizes must preserve their serialized layout. `w8_long` and
`w8_ulong` name historical 32-bit fields; `W8_PTR32` represents disk-size
pointer slots. Text/CRT boundaries use explicit conversion rather than host
wide-string routines that expect four-byte `wchar_t`.

There is no Wine runner, 32-bit target or legacy fallback.
The game/asset filesystem in `src/platform/` owns virtual paths and uses SDL3
streams, native UTF-8 host imports and standard C++ filesystem operations.
Asset names are ASCII-case-insensitive on every host; explicit host imports
keep native filesystem case semantics. The remaining `src/compat/` CRT code
only bridges legacy two-byte strings.

SLF and save timestamps keep their packed low/high FILETIME codec. Save ordering
uses native modification time; the iron-man mask records the metadata snapshot
queried while writing. SDL's POSIX creation field is ctime (metadata-change time),
not true birth time, and can change on close/rewrite. Windows birth time and
POSIX ctime are not interchangeable persistent identities; no loader-side
iron-man timestamp validator currently exists. Cross-host birth-time identity
and a full iron-man save/load roundtrip remain unverified.

Third-party source licenses, including the SGP license, remain with
their respective sources.

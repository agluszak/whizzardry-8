# Whizzardry 8

Portable Wizardry 8 and SurRender reconstruction targeting native Linux and macOS.
The native build uses modern Clang, SDL3 GPU rendering, FFmpeg video decoding,
miniaudio sound and system zlib. CMake downloads a pinned miniaudio revision.
Game data comes from an existing retail installation and is not distributed here.

## Build

On Ubuntu, install the development dependencies:

```sh
sudo apt install clang cmake ninja-build pkg-config libsdl3-dev zlib1g-dev glslang-tools \
    libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev
```

Configure and build:

```sh
cmake -S . -B build-native -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build-native
ctest --test-dir build-native --output-on-failure
```

The native targets include `Wiz8Native` and SurRender (`libsr.so` on Linux).
A Vulkan-capable SDL3 GPU backend is required for graphics checks.

## Run

Point the launcher to your installed game assets:

```sh
WIZ8_ASSET_ROOT=/path/to/Wizardry8 ./run.sh /WINDOW
```

The launcher accepts an optional `.env` file and passes arguments through to
the game. It uses `build-native` unless `WIZ8_BUILD_DIR` is specified.
`SDL_VIDEODRIVER`, `VK_DRIVER_FILES` and `VK_ICD_FILENAMES` override graphics
defaults. `WIZ8_GPU_DEBUG=1` enables SDL GPU debugging and
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
WIZ8_ASSET_ROOT=/path/to/Wizardry8 build-native/native_game_graphics
WIZ8_ASSET_ROOT=/path/to/Wizardry8 build-native/native_world_graphics party.CHR \
    "$PWD/build-native/world.ppm"
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

The native platform implementations reside in `src/compat/`,
`src/sgp/native/`, `src/surrender/native/` and the SDL GPU device. This
repository develops the native implementation; historical Windows
assembly-equivalence belongs to the separate decompilation repository.

Third-party source licenses, including the SGP license, remain with
their respective sources.

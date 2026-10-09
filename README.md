# Whizzardry 8

Wizardry 8 and SurRender. The build produces `Wiz8.exe` and `sr.dll`.
JPEG and UnZip plug-in sources remain in the tree but are excluded from the build.

## Native Linux build (in progress)

Any non-MSVC Clang selects the native lane (`cmake/Native.cmake`): 64-bit
Linux, SDL3 GPU, system zlib and FFmpeg, with miniaudio downloaded at
CMake configuration time from a pinned upstream commit.
It builds `Wiz8Native` and `libsr.so`. The recovered game loop, intro transitions,
SGP input/surfaces and sound manager run through native adapters. FFmpeg decodes
Bink video/audio from loose files or bounded SLF streams; movies present through
SurRender in the same SDL window as the game. Startup, movies, Escape to the
main menu and exit have been exercised with installed retail assets. The world
harness loads a real new game and renders the Monastery beach with terrain,
textures, water, sky, objects and party UI. The executable also reaches that
scene through its new-game screens on X11 with Mesa lavapipe. Physical input,
interactive gameplay, save/load and
shipped-renderer parity still need native runtime validation.

```sh
sudo apt install clang cmake ninja-build pkg-config libsdl3-dev zlib1g-dev glslang-tools \
    libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev
cmake -S . -B build-native -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build-native
(cd build-native && ctest)
```

`srdd_spike` renders through `srGERD` and needs a display with a Vulkan
driver. `WIZ8_SRDD_TRACE=1` logs device draws; `WIZ8_GPU_DEBUG=1` enables
SDL GPU debug mode.

`native_events` uses SDL's dummy video driver and exercises the recovered input
and string editor without a display. `native_imports` checks renderer contracts
from a separate client executable. See [native validation](tests/native/README.md)
for coverage and limitations. With installed assets, run the game or the
focused graphics check:

```sh
WIZ8_ASSET_ROOT=/path/to/Wizardry8 build-native/Wiz8Native /WINDOW
WIZ8_ASSET_ROOT=/path/to/Wizardry8 build-native/native_game_graphics
# Existing character basename in Saves/Characters; private user overlay:
WIZ8_ASSET_ROOT=/path/to/Wizardry8 build-native/native_world_graphics party.CHR
```

The harness uses a temporary user overlay and a private 640x480 configuration.

File I/O uses a virtual `C:\` rooted at `WIZ8_ASSET_ROOT` (default: startup
working directory). Reads check `WIZ8_USER_ROOT` first, then installed assets;
writes go to the user root. Its default is `$XDG_DATA_HOME/whizzardry8`, or
`$HOME/.local/share/whizzardry8` on Linux and
`$HOME/Library/Application Support/whizzardry8` on macOS. Backslashes and ASCII
case differences work throughout Win32 wrappers, CRT opens and SurRender streams.
The roots must be separate. Optional `WIZ8_CD1_ROOT`, `WIZ8_CD2_ROOT` and
`WIZ8_CD3_ROOT` expose read-only `D:\`, `E:\`, `F:\` drives with retail disc labels.
These directories may point at mounted discs or extracted installations.
See [native I/O validation](tests/native/README.md) for tested behavior and limits.

## Clang in Docker

The image pins the Debian base and package snapshot, LLVM 19, the Microsoft
CRT/Windows SDK manifest, and the zlib 1.0.4 archive checksum. It contains all
build dependencies; no host compiler, SDK, Python installation, or extracted
library tree is used.

```sh
docker build --platform linux/amd64 -t whizzardry8-clang:llvm19 docker/clang
mkdir -p build-clang/docker
docker run --rm --network none \
    -v "$PWD:/repo:ro" -v "$PWD/build-clang/docker:/out" \
    whizzardry8-clang:llvm19
```

The container configures CMake and builds 32-bit Windows binaries with clang-cl
and LLD. Set `-e BUILD_JOBS=4` to change build parallelism. Outputs go into
`build-clang/docker`. Both `Wiz8.exe` and `sr.dll` build with Clang. The resulting binaries use the
modern Microsoft C/C++ runtime, including the x86 Visual C++ runtime DLLs.

## MSVC 6

Use a VC6 SP5 command prompt with CMake 3.20 or newer, Python 3, and zlib 1.0.4
sources:

```bat
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DZLIB_SOURCE=C:/deps/zlib-1.0.4
cmake --build build
```

The VC6 Docker toolchain remains in `docker/msvc600`. CMake uses Python only
to generate the existing SurRender assertion import library.

Running the game requires installed Wizardry 8 assets and its other runtime
DLLs. Third-party sources retain their own licenses, including the SGP license
in `src/sgp/SFI Source Code license agreement.txt`.

## Launching on Linux

Run `./run.sh` from any directory. It loads the repository's `.env` and launches
`build-native/Wiz8Native` directly with the installed assets in `build/run-clang`.
The default renderer uses X11 and Mesa lavapipe, matching the verified native
new-game route. The launcher creates a 640x480 native video configuration only
when the user overlay has none; later video choices are preserved.

`WIZ8_BUILD_DIR` selects another native build directory. `WIZ8_ASSET_ROOT` (or
`WIZ8_RUN_DIR`) selects the installed assets, and `WIZ8_USER_ROOT` selects the
separate writable save/config directory. The default user root is
`${XDG_DATA_HOME:-$HOME/.local/share}/whizzardry8`. Set `VK_DRIVER_FILES` or
`VK_ICD_FILENAMES` to choose another Vulkan driver, or `SDL_VIDEODRIVER` to choose
another SDL display backend. Defaults use the already installed lavapipe ICD;
if it is missing, the launcher tells you to install `mesa-vulkan-drivers`.

Arguments are passed to the game, for example `./run.sh /NOSOUND`. Output is
saved to the user root's `diagnostics/launch.log`. The game runs in the
foreground: close its window or press Ctrl-C to quit. Rebuild after source
changes with `cmake --build build-native`.

# Whizzardry 8

Wizardry 8 and SurRender. The build produces `Wiz8.exe` and `sr.dll`.
JPEG and UnZip plug-in sources remain in the tree but are excluded from the build.

## Native Linux build (in progress)

Any non-MSVC Clang selects the native lane (`cmake/Native.cmake`): 64-bit
Linux, system zlib and SDL3, no Windows SDK. It currently builds SurRender
as `libsr.so` with an SDL3 GPU device, the portable SGP core and the recovered
game code as `libWIZ8_GAME_CORE.a`. SDL keyboard, mouse, focus and timer messages
feed the recovered SGP input queue and clock. The game still needs its native
application shell, video surfaces, audio and video decoding before it can link
and run.

```sh
sudo apt install clang cmake ninja-build libsdl3-dev zlib1g-dev glslang-tools
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
for coverage and the remaining link boundary.

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

Run `./run.sh` from any directory. It loads the repository's `.env`, copies
the rebuilt `Wiz8.exe` and `sr.dll` from `build-clang/launch` into the existing
`build/run-clang` asset directory, and launches through UMU/GE-Proton. It activates
the game window so keyboard input reaches the game. Requires `xdotool`, a working
graphical session, and `WIZ8_UMU_RUN` and `WIZ8_WINE_PREFIX` in `.env`.

For Docker outputs, use `WIZ8_BUILD_DIR="$PWD/build-clang/docker" ./run.sh`.
`WIZ8_RUN_DIR` can select another directory containing the installed game assets.
Additional arguments are passed to the EXE, for example `./run.sh /NOSOUND`.
Launcher output is saved to the game directory's `diagnostics/launch.log`.
The script stages existing binaries; rebuild them first after source changes.

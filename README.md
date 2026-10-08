# Whizzardry 8

Wizardry 8 and SurRender. The build produces `Wiz8.exe` and `sr.dll`.
JPEG and UnZip plug-in sources remain in the tree but are excluded from the build.

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

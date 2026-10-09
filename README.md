# Whizzardry 8

Native Linux/macOS port of Wizardry 8 and SurRender, based on the recovered
sources in [wizardry-8-decomp](https://github.com/agluszak/wizardry-8-decomp).
The native build uses Clang, SDL3 GPU rendering, FFmpeg for movies, miniaudio
for sound, and system zlib. CMake downloads miniaudio at a pinned revision.
**The port runs but does not yet reproduce all retail gameplay and graphics.**

This is the modernization repository: it can change runtime implementations,
memory ownership and serialization adapters. The decomp repository remains the
retail-faithful Windows reconstruction (see its [port-preparation PR #980](https://github.com/agluszak/wizardry-8-decomp/pull/980)).
Do not add Windows assembly-matching workarounds here merely to preserve
the old shared commit history.

## Build

On Ubuntu with the development packages available:

```sh
sudo apt install clang cmake ninja-build pkg-config libsdl3-dev zlib1g-dev glslang-tools \
    libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev
cmake -S . -B build-native -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build-native
ctest --test-dir build-native --output-on-failure
```

The native targets include `Wiz8Native` and `SURRENDER` (`libsr.so` on Linux).
CMake also builds focused tests and asset-dependent harnesses. For a normal
launch, install/obtain the retail game assets separately:

```sh
WIZ8_ASSET_ROOT=/path/to/Wizardry8 ./run.sh /WINDOW
```

The launcher reads optional `.env`, uses `build-native` by default, chooses X11
and Mesa lavapipe if no Vulkan driver is configured, and writes
`diagnostics/launch.log` under the user root. Set `WIZ8_BUILD_DIR`,
`SDL_VIDEODRIVER`, `VK_DRIVER_FILES` or `VK_ICD_FILENAMES` to override
these defaults. `WIZ8_GPU_DEBUG=1` enables SDL GPU debugging;
`WIZ8_SRDD_TRACE=1` traces SurRender draws.

File reads consult `WIZ8_USER_ROOT` first, then `WIZ8_ASSET_ROOT`; writes
go only to the user root. Keep these directories separate. By default the user
root is `$XDG_DATA_HOME/whizzardry8` (or `~/.local/share/whizzardry8` on
Linux; `~/Library/Application Support/whizzardry8` on macOS). The compatibility
layer maps a case-insensitive, backslash-separated virtual `C:` drive onto the
asset root and supports optional `WIZ8_CD1_ROOT` through `WIZ8_CD3_ROOT`
for read-only virtual disc drives.

## Validation

`ctest --test-dir build-native --output-on-failure` runs focused checks for
file/SLF handling, SDL input and timers, CRT and pointer semantics, save
records, Huffman/zlib, native audio, FFmpeg movies, JPEG surface transfer,
blitters and SurRender contracts. `native_events` uses SDL's dummy driver.
The tests are **not** proof of interactive game or renderer parity.

With installed assets and a Vulkan-capable display:

```sh
WIZ8_ASSET_ROOT=/path/to/Wizardry8 build-native/native_game_graphics
WIZ8_ASSET_ROOT=/path/to/Wizardry8 build-native/native_world_graphics party.CHR \
    "$PWD/build-native/world.ppm"
# Additional world harness modes: save, kill, angles.
WIZ8_ASSET_ROOT=/path/to/Wizardry8 build-native/native_world_graphics party.CHR \
    "$PWD/build-native/world.ppm" save
```

The world harness uses the actual level, party and asset loaders with a
private writable overlay. Previous Linux/X11/lavapipe checks loaded the
Monastery beach, submitted 38 enabled meshes/3,394 polygons and completed
a save/reload round trip. Counts may vary with animation; this does not
establish retail visual parity. `native_movie_test` also accepts a path to a
retail `.BIK` file; `tests/native/generate_movie.sh` regenerates its
checked-in media fixtures using FFmpeg.

To investigate external SDL/Vulkan shutdown allocations independently of
game code, build and run `native_gpu_lifecycle`. Existing full-game
LeakSanitizer results include allocations in game owners and external
DBus/graphics drivers; they are not leak-clean. GPU/device tests require
a usable Vulkan backend. macOS and physical input/audio have not been
fully validated.

## Porting constraints

- Keep on-disk records and serialized pointer words at their actual widths.
  `W8_PTR32` represents four-byte disk pointer slots; live 64-bit pointers
  require an explicit mapping.
- MSVC `long` is 32-bit, unlike LP64 `long`; `w8_long`/`w8_ulong` express
  game-sized fields. Derive allocation and stride sizes from their types
  without altering format-defined byte counts.
- Game text has two-byte `wchar_t` (`-fshort-wchar`) and needs explicit
  native UTF-16/CRT adapters. Never call host wide-string libc functions
  that assume four-byte `wchar_t` on Linux.
- Packed members, ownership, callback signatures and raw I/O records need
  review at their producer/consumer boundaries; a clean compile is not
  evidence of correct behavior.
- Retain recovered game/SGP logic where useful. Native platform replacement
  belongs in `src/compat/`, `src/sgp/native/`, `src/surrender/native/`
  and the SDL GPU device, not in copied vendor implementations.

## Known problems and next work

- **Rendering:** sky sections disappear at some camera angles; terrain has
  dark/flickering triangles; compass and formation widgets have dark wedges.
  Compare the GPU pipeline and scenes against retail before claiming parity.
- **Saves:** the real save/load routines round-trip in an isolated harness,
  but the interactive options menu has reported an overwrite/write-protected
  error. Test actual menu actions.
- **Combat:** an observed crash follows killing a crab. Earlier sanitizer
  evidence reached an unaligned experience reference in
  `AwardPartyExperience`; neither the cause nor fix is established.
- **Native behavior:** exercise real keyboard/mouse/focus, audible playback,
  Wayland, macOS, full shutdown and additional gameplay/save paths.
  Verify real JPEG decoding and native unzip integration, not only mocked
  transfer/fixture paths.
- **Source cleanup:** review remaining narrow `printf` calls with two-byte
  strings (`%S`/`%ls`), packed pointer escapes, callbacks, raw record fields,
  narrowing conversions, object lifetimes and owning copies. Investigate
  the temporary string leak in `AppendToLastTextLine`, sentinel/null cursor
  arithmetic and cleanup/error paths. Do not widen serialized pointer slots
  or change intentional byte pitches/alignment padding.

## Legacy Windows builds

The temporary legacy lane still builds `Wiz8.exe` and `sr.dll` with VC6 or
clang-cl, using zlib 1.0.4. For VC6 SP5 (with Python 3 and CMake 3.20+):

```bat
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DZLIB_SOURCE=C:/deps/zlib-1.0.4
cmake --build build
```

The isolated clang-cl Docker workflow is available in `docker/clang`;
`docker/msvc600` contains the VC6 toolchain. The legacy blitter capture
harness remains optional (`WIZ8_BUILD_LEGACY_BLITTER_TEST`). Exact Windows
assembly equivalence is tracked in the decomp project, not here.

Retail assets and proprietary runtime libraries are not distributed in
this repository. Third-party source licenses, including the SGP license,
remain with their respective sources.

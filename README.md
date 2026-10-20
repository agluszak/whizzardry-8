# Whizzardry 8

Whizzardry 8 is a work-in-progress port of [Wizardry 8](https://en.wikipedia.org/wiki/Wizardry_8) to modern, 64-bit Linux, macOS and Windows.

It builds on the recovered source code from [Wizardry 8 Decomp](https://github.com/agluszak/wizardry-8-decomp), but has a different goal: **keep the game, replace the outdated machinery**. Instead of reproducing the original executable, we're simplifying the code and using C++26, SDL3 and other cross-platform libraries.

Linux is the only platform currently tested in CI. macOS and Windows builds are not yet verified, and the game is still under development.

This repository does **not** include game assets. You'll need your own retail copy of Wizardry 8.

## Build on Linux

You'll need Clang 23, GCC 16 (including libstdc++ development headers), CMake 4.4.4 or newer, Ninja, Python 3.11 or newer, [Conan 2](https://conan.io/), Vulkan development headers and loader (`libvulkan-dev` on Debian/Ubuntu), and a working Vulkan driver. Conan handles the other C/C++ library dependencies; CMake fetches the pinned VulkanSceneGraph source.

From the repository root:

```sh
python3 -m pip install 'conan==2.33.0' 'cmake==4.4.4'
conan export conan/recipes/libalsa

CC=clang-23 CXX=clang++-23 conan install . \
    -pr:h conan/profiles/linux-clang23 -pr:b conan/profiles/linux-gcc16 \
    -s:h build_type=Release --build=missing -of build/linux/conan

. build/linux/conan/conanbuild.sh
cmake --preset linux \
    -DCMAKE_TOOLCHAIN_FILE="$PWD/build/linux/conan/conan_toolchain.cmake" \
    -DCMAKE_C_COMPILER=clang-23 -DCMAKE_CXX_COMPILER=clang++-23
cmake --build --preset linux
. build/linux/conan/conanrun.sh
```

The first build may take a while because Conan needs to build some dependencies.

The renderer uses Vulkan on all platforms. Windows builds require the Vulkan SDK;
macOS builds require the Vulkan SDK with MoltenVK. Set `VULKAN_SDK` as instructed
by the SDK installer. SDL continues to provide windows, input, audio and CPU
surfaces. Linux is the platform covered by CI.

## Play

First, prepare the data from your retail installation:

```sh
build/linux/wiz8_prepare_assets /path/to/prepared-assets /path/to/retail-install
```

Choose a **new directory** for `prepared-assets`, outside the retail installation. The preparation tool extracts the game data and applies the installed patches.

Then launch the game:

```sh
WIZ8_ASSET_ROOT=/path/to/prepared-assets ./run.sh
```

Saves, settings and logs go into the system's per-user application directory, separate from the game assets. Set `WIZ8_USER_ROOT` if you'd rather choose that directory yourself.

## Contributing

This is a modernization project, not an exercise in preserving the original engine's structure. We aim to remove obsolete code and unnecessary abstractions while keeping Wizardry 8's gameplay, content and file compatibility.

For the current technical direction, see [AGENTS.md](AGENTS.md), [the architecture](ARCHITECTURE.md) and [the roadmap](ROADMAP.md).

After building, run the tests with:

```sh
ctest --preset linux
```

On a headless Linux host, run CTest under `xvfb-run -a`; the graphics scenarios use
SDL's X11 Vulkan surface. CI also enables the Khronos validation layer and fails
graphics tests on validation errors.

Some runtime tests also need prepared retail assets:

```sh
WIZ8_ASSET_ROOT=/path/to/prepared-assets ctest --preset linux -L runtime
```

C and C++ changes should use the repository's `.clang-format` configuration (clang-format 23).

## Diagnostics

Linux uses Clang 23 with GCC 16's libstdc++ in C++26 mode. CMake checks for
`std::function_ref` and `std::inplace_vector` during configuration; upgrading
only the language flag is insufficient. macOS and Windows also need a standard
library implementing both facilities and remain unverified.

`linux-asan` combines ASan and UBSan with fatal undefined-behavior diagnostics.
Debug and RelWithDebInfo enable `_GLIBCXX_ASSERTIONS`; ordinary assertions follow
the build configuration. Signed overflow and strict aliasing follow the
language rules (no `-fwrapv` or `-fno-strict-aliasing`), so UBSan's
signed-overflow check guards gameplay arithmetic; retail wraparound is written
as explicit unsigned arithmetic. Plain `char` holds only text; small signed
numbers use `signed char` or `std::int8_t`, so no build sets `-fsigned-char`.

The normal tidy allowlist includes assignment-in-condition, string-view C API
misuse, allocation ownership, dangling string buffers and last-use moves.
Configure an analysis build with `WIZ8_PRECOMPILE_HEADERS=OFF`, then run:

```sh
CLANG_TIDY=clang-tidy-23 RUN_CLANG_TIDY=run-clang-tidy-23 \
    tools/run-clang-tidy.sh build/linux-tidy
CLANG_TIDY_CONFIG="$PWD/.clang-tidy-audit" \
    CLANG_TIDY=clang-tidy-23 RUN_CLANG_TIDY=run-clang-tidy-23 \
    tools/run-clang-tidy.sh build/linux-tidy
python3 tools/run-diagnostic-audit.py build/linux-tidy
```

The separate audit checks unchecked optional dereferences (intentional `.value()`
exceptions are allowed), moved-from ownership, strict lifetime safety, exhaustive
enum handling and buffer use at parser/image boundaries. Experimental compiler
warnings are retained in `diagnostic-audit.log` for review. The optional-access
and moved-from checks fail on findings. Weekly and manually dispatched CI runs
include these audits and `linux-integer-audit`, which adds implicit truncation
instrumentation to file/image boundaries. The ordinary ASan/UBSan jobs run on
every PR. `-Wconversion` and `-Wsign-conversion` remain outside the global warning
baseline, and `-fstrict-bool` is not enabled.

`function_ref` is used only for borrowed synchronous callbacks; stored callbacks
retain ownership. Fixed-capacity storage requires a proven bound. SIMD and hive
need workload evidence before adoption; reflection and contracts remain limited
by compiler support.

CI reuses successful tidy results only after Clang rediscovers the current
include graph and the contents of every dependency match, including system
headers. Tool versions, compile commands and tidy configuration also invalidate
the cache. Failed scans and analyses are always retried; clean files remain
cached when another file fails. To use this locally:

```sh
CLANG_TIDY_CACHE_DIR="$PWD/build/tidy-cache" \
    CLANG_TIDY=clang-tidy-23 RUN_CLANG_TIDY=run-clang-tidy-23 \
    tools/run-clang-tidy.sh build/linux-tidy
```

Unset `CLANG_TIDY_CACHE_DIR` for an uncached run. CI prints hit/miss counts and
elapsed times, skips native jobs for documentation-only changes, and regenerates
offline shaders when their sources, tools, dependencies or build settings change.
Weekly/manual runs retain the full source scope; their integer-sanitizer job
includes ASan and UBSan, replacing the duplicate ordinary sanitizer run.

Analysis runs in four stable partitions so cold repository-wide checks also run
concurrently. Header dependency discovery stays within each partition, and the
required `clang-tidy` check aggregates all four results. The workflow's manual
`full_scan` option bypasses the tidy cache for both regular and ownership checks;
manual runs also include the compiler audit and integer-sanitizer build.

# Compilation performance

For a fast, non-instrumented Linux development build (Clang, `-O0 -g1`,
game only, PCH enabled):

```sh
cmake --preset linux-dev
cmake --build --preset linux-dev --target Wiz8Native --parallel 8
```

The normal `linux` preset remains a Release build. Stable PCHs are enabled
by default; CI additionally enables unity builds for clean-runner performance.
CI's uninstrumented build stays Debug; its ASan build uses `RelWithDebInfo`
with `-O1 -g1`.
The latter runs optimized sanitizer code without paying the full `-O2`
compile cost.

## Measure before enabling more

The default build uses PCH but not unity builds. Both are separately
configurable:

- `-DWIZ8_PRECOMPILE_HEADERS=ON` (default) precompiles stable C++ headers for
  `WIZ8_GAME_CORE` and `SURRENDER`.
- `-DWIZ8_UNITY_BUILD=ON -DWIZ8_UNITY_BATCH_SIZE=4` combines batches of
  source files. Larger batches can cause C++ name collisions, increase peak
  memory, and make incremental rebuilds slower. A conflicting source file
  can be excluded using `SKIP_UNITY_BUILD_INCLUSION`.

For a baseline with compiler timings:

```sh
cmake --preset linux-profile
cmake --build --preset linux-profile --target Wiz8Native --parallel 8
ClangBuildAnalyzer --all build/linux-profile build-timings.bin
ClangBuildAnalyzer --analyze build-timings.bin
```

The profile preset uses `-ftime-trace` without PCH or unity. For comparisons,
use **separate clean build directories**, with the same compiler, optimization
level, number of jobs, and target. Build `Wiz8Native` and compare these
options: neither feature, PCH only, unity only, and both features.
Also measure single-file rebuild time and peak memory; the fastest clean
build may be worse for everyday development.

Before enabling unity as a default, first compile all game and renderer
sources and resolve real name/macro collisions at source level rather than
adding broad compiler-specific workarounds.

## Clean-build benchmark (2026-10-11)

GitHub Actions Ubuntu 26.04, native Clang, `-O0 -g1`, `Wiz8Native`
only, 4 parallel build processes, compiler cache disabled. These are single
clean-build samples (configuration time excluded), not averaged results.
Unity builds exclude `OctPreTree.cpp` because its translation-unit-local
constant collides with the same-named constant in `Octree.cpp`.

| Build | Wall time | Relative to baseline | Max subprocess RSS |
| --- | ---: | ---: | ---: |
| Baseline (no PCH/unity) | 269.46s | 1.00× | 358000 KiB |
| PCH only | 150.14s | 1.79× | 331604 KiB |
| Unity only (batch size 4) | 130.14s | 2.07× | 412100 KiB |
| PCH + unity (batch size 4) | 98.24s | 2.74× | 386064 KiB |

The maximum RSS is for one subprocess, not the sum of concurrently running
compilers. Unity builds are kept optional for local development because one
source edit recompiles the entire unity batch. The workflow
`.github/workflows/compile-benchmarks.yml` supports manual benchmarking
of all four variants without a compiler cache. Do not interpret the
single-sample timing as a stable statistical estimate.

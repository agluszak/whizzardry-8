# Whizzardry 8 native port: handoff

Oct 9, 2026 · @Mietek Pierdzibąk

## Continuation — filtered decomp upstream patch, Oct 9, 2026

The agreed project boundary supersedes the historical dual-build instructions
below. Decomp preserves retail behavior, including established bugs and UB;
Whizzardry will become native-only and may rewrite runtime structures and
serialization boundaries. Do not add more native conditionals merely to retain
Windows x86/assembly matching in Whizzardry.

The historical shared prefix was filtered rather than cherry-picked wholesale.
`../whizzardry-decomp-upstream.patch` is committed above decomp `main`
`6c7de144cf94fd7bc62e0fc42e1449f2be37c704` as
`81c8606d5e12215e62038147cc5697569e6b4be4` on
`port-prep/historical-types-and-sizes`. The upstream PR is
[decomp #980](https://github.com/agluszak/wizardry-8-decomp/pull/980). It contains 380 source/header
changes: ordinary historical long/pointer-role aliases, ABI/disk contract
markers, typed sizes/strides and text/allocation declarations. Native branches,
implementations, handles, packed-access workaround aliases and gameplay fixes
are excluded. The newer decomp fixes were retained. The branch is pushed;
the PR has not been merged.

Both Clang-cl and actual VC6 SP5 compile all 311 first-party units. Clang's
non-debug COFF sections/relocations/symbols match in all 311 objects. VC6 differs
in 24 objects after compiler-local name normalization; a focused experiment
with only the three long typedefs already reproduces 15 differences. Exact VC6
or retail matching is not claimed. Nine independent source gates, changed-file
formatting, unchanged annotation inventories and patch round-trip checks pass.
The Docker-backed compiler-index/full project check remains outstanding.

See `../whizzardry-decomp-upstream-review.md` for scope, checks and the VC6
emission differences. Whizzardry publication includes the native port history,
typed model bounds, save-name conversion and bounded ambient-name disk writes,
plus installed-asset regression harness modes. The unfinished packing and
experience-reference experiments remain local and uncommitted. Sky, terrain
and combat regressions remain open in `native-runtime-issues.md`. The native-only
cutover is the next separate task.

Publication checks: the native build and all **14/14 CTest tests pass**. The
imports/model-bounds test also passes ASan/UBSan with leak detection. The real
game loader renders **38 enabled meshes / 3,394 submitted polygons**, then
successfully saves and reloads `native-test` in a private writable directory.
This does not establish visual parity or interactive save-menu behavior. The
retired decomp `wiz8 pr-check` command is unavailable in Whizzardry; native
CMake/CTest checks are used instead. macOS was not tested.

## Continuation — shared generalization audit, Oct 9, 2026

The persistent queue is `upstream-generalization-todo.md`; confirmed fixes,
reviewed exceptions and remaining work are in `upstream-generalization-audit.md`.
The new source inventory covers **763 tracked C/C++ files**, including inactive
branches, headers, extensions, tests and vendor code. Compiler inventories cover
**329 native and 315 Windows units**, with zero errors. All **70 protected raw-IO
records/prefixes agree** in extent and field offsets/widths. These are evidence
inventories and a reviewed batch, not an exhausted semantic audit.

Shared commit **`a706709c8c89d62b8e2b6b43c90e2b2d5b94233a`** fixes saved NPCT/NSF
pointer-presence words (including collisions with runtime handles), packed scalar
pointer alignment, JPEG four-byte pixel access and extension long widths, three
remaining ordering strides, reviewed raw allocation/release pairs and signed
32-bit float conversion boundaries. It adds reusable source/layout/comparison
tools and preserves Windows branches, disk extents and recovered ownership.
All 89 remaining long tokens were classified as compatibility, host/external
ABI, formatting or intentional long long cases. Other narrowing remains queued.

Native commit **`664fc7438ec5a81734954b8af1130b1c05e34e1f`** adds the UTF16-to-narrow
CRT implementation, actual NPCT/NSF loader fixtures, packed AI/vector checks,
production JPEG row-transfer checks and rounding-mode FISTP comparisons.
The NSF test caught libc narrow `%S` interpreting the two-byte text as four-byte
wchar; the loader now uses the native conversion. Save-path/diagnostic narrow
formatting elsewhere remains a known follow-up. The recovered merged text
allocation leak and broader lifetime, packed callback, serialization, sentinel
and shutdown reviews remain explicit in the todo.

Validation: native build and **14/14 tests pass**, syntax probe **246/246 clean**,
and four focused tests pass **ASan/UBSan with leak detection**. Six old-code
negative controls fail on the repaired defects. Legacy clang-cl builds, and all
**299 main non-zlib objects remain exact** against 228fa4c with
SOURCE_DATE_EPOCH=1791503644. Five extension objects agree after normalizing only
the anonymous-namespace filename hash caused by the baseline mirror path.
The fresh world harness renders **38 enabled meshes / 3,394 submitted polygons**,
160/35 draw calls, 1,525/64 triangles and 131,747 changed / 130,374 restored viewport
pixels, then exits normally. It uses Xvfb/lavapipe; physical input, VC6/macOS,
real JPEG decoding, native unzip and every affected gameplay path remain untested.
Full-world lifetime leaks are not claimed fixed. No packages were installed.

Concurrent Windows libclang parsing stalled on this host; serial parsing of the
same units completed. The layout tool now serializes that lane and keeps native
parsing parallel. Evidence uses `generalization-*`, `layout-final-*` and
`raw-layout-comparison.json` in the ignored build directories.

The restack preserved the exact tree and restored the validated native files.
With this final documentation commit the stack is **43 commits over main:
22 upstreamable commits first**, ending at `upstream-tip`
(`a706709c8c89d62b8e2b6b43c90e2b2d5b94233a`), followed by 21 native/documentation
commits. Code, tools, audit, regressions and synchronized handoff are published
to the Whizzardry fork; no decomp upstream PR or push was performed.
`build-native/upstream-generalization.patch` contains the new shared commit.
Older hashes and validation snapshots below are historical.

## Continuation — systematic allocation-size audit, Oct 9, 2026

The complete allocation-width review is recorded in `allocation-size-audit.md`.
The inventory covers **760 tracked C/C++ files**, including headers, inactive
platform branches, debug code, extension plug-ins, tests and miniaudio. It
records 729 allocation references (including definitions/declarations), traces
variable byte counts and distinguishes element widths from file/pixel bytes,
capacity budgets and alignment padding. `tools/audit_allocations.py` reproduces
the inventory, including complete multiline expressions.

Shared commit **`0207bc7e89d8fd463b939633ac32a08bd49f84a5`** replaces **99 allocation
expressions in 37 production files** and associated typed copies/clears. It
fixes four missed native-width defects: the allocator header (32 -> 48 bytes),
monster spell icon (8 -> 16), level-file trigger (6 -> 10), and monster combat
state (339 -> 343). The allocator's name placement and alignment now follow
its header size. Spare slots and multiplicities remain intact, including four
UV-pool entries per polygon and 40 ushort slots per leaf. Serialized byte
widths remain unchanged. Reviewed byte-buffer/format/alignment exceptions are
listed in the report; this does not establish general ownership, overflow or
indexing safety.

Validation: native build and **12/12 tests pass**, the game/SGP syntax probe is
**246/246 clean**, and the allocator/client and UTF16 CRT tests pass ASan/UBSan
with leak detection. The new allocator regression failed against the old code
before the fix and passes now; native regression commit is **`ab513f1`**. Legacy
clang-cl builds and all **299 non-zlib objects remain exact** in instructions,
relocations and `.rdata` against the existing `228fa4c` baseline with
`SOURCE_DATE_EPOCH=1791503644`. Evidence logs use the `allocation-*` prefix in
`build-native`, `build-native-asan` and `build-clang`. VC6/macOS, every debug
configuration and the three affected gameplay paths were not run separately.
No packages were installed.

The shared change was restacked ahead of the complete native suffix; comparison
with the pre-restack tree was empty. With this documentation commit the stack
is **40 commits over main: 21 upstreamable commits first**, ending at
`upstream-tip` (`0207bc7e89d8fd463b939633ac32a08bd49f84a5`), followed by 19 native/
documentation commits. The code, audit/tool, native regression and synchronized
handoff are published to the Whizzardry fork; no decomp upstream PR or push
was performed. `build-native/upstream-allocation-sizes.patch` is the generated
patch for the new shared commit. Older hashes below are historical snapshots.

## Continuation — native run.sh launcher, Oct 9, 2026

`run.sh` now launches `Wiz8Native` directly at `7dc073d4b5cc9fcb4ae9729bbd82040fc16c8958`, using the native build,
installed assets and a separate writable user directory. Default video is X11
with the installed Mesa lavapipe ICD. An existing `VK_DRIVER_FILES` or
`VK_ICD_FILENAMES` selection is honored. The launcher initializes the SDLGPU
640x480/miniaudio video config only when absent, preserves later video choices,
and writes output to the user root's `diagnostics/launch.log`. It uses `exec`
in the foreground, passes extra arguments and preserves the game exit status.
Build/asset/user paths and SDL/Vulkan selections can be overridden; `.env`
loading remains available. README documents the current native launcher.

Validation: `bash -n` and `git diff --check` pass. Starting `run.sh /NOSOUND`
from `/tmp` with a fresh private user overlay displays the Sir-Tech movie,
creates the expected config/log directory, and exits with status 0 on standard
window close. Screenshot: `build-native/native-launch.png`. No packages were
installed and the controlled game/display processes were stopped. This does
not add physical desktop focus validation.

With this final documentation commit, the stack is **37 commits over main:
20 upstreamable commits first**, with unchanged `upstream-tip`
`cc4bba6f8352ccd4cfee0c2312cfc53556343ef4`, followed by 17 native/documentation
commits. The tracked handoff and parent copy are synchronized.

## Continuation — native world rendering, Oct 9, 2026

Native world rendering is implemented at `bd26f26a9bbeb9bbcbe0f9cc6260cf164105d8b5`. With this final handoff
commit, `native-port` has **35 commits over main: 20 upstreamable commits
first**, ending at `upstream-tip` (`cc4bba6f8352ccd4cfee0c2312cfc53556343ef4`), followed by 15 native/documentation
commits. The shared world fixes were moved below the entire native suffix;
`git diff` against the pre-restack tree was empty before restoring native work.
Earlier sections record historical hashes and validation boundaries. No
publication to the decomp upstream repository was performed.

- **Visible world:** the real new-game loader reaches the Monastery beach with
  textured sand and cliffs, water, sky, trees, wreckage, a chest and the party
  interface. `native_world_graphics` links production game/SGP/renderer code,
  loads an existing installed CHR, initializes the party/NPC opening state and
  enters the ordinary loading screen and game loop. Only the opening movie is
  bypassed. No mock level, world, actor or graphics implementations were added.
  The harness creates and removes a private user overlay. Retail assets and
  character data are supplied locally and are not committed.
- **World contribution:** 38 enabled meshes and 3,394 submitted level polygons.
  At the same camera/UI, world on/off records **160/35 GPU draw calls** and
  **1,525/64 input triangles**. Disabling the world changes **130,597 viewport
  pixels**, and re-enabling it restores **129,048 pixels** relative to the
  control. Animation can vary these counts. Output: `build-native/world.png`,
  `world.ppm`, and `world.ppm.control.ppm`; log `world-final-run.log`.
- **Ordinary executable:** synthetic Escape, party selection with the existing
  character, confirmation/options, opening movie and actual level loading also
  reach the beach in `Wiz8Native` on isolated X11 with the already installed
  Mesa lavapipe Vulkan driver. The party opening speech appears in
  `build-native/world-ui-beach.png`. Standard WM_DELETE_WINDOW exits with status
  0. This verifies the full new-game screen route separately from the harness;
  physical input and audible output remain unverified. The COSMIC/XWayland
  desktop window stays hidden without keyboard focus. NVIDIA on Xvfb shows
  stale presentation and rapid resource growth; lavapipe avoids those symptoms.
  Desktop focus and NVIDIA/Xvfb behavior require separate investigation.
- **Native renderer fixes:** fog alpha is opacity, so zero must preserve object
  color. The shader had inverted that value, producing a blue world. All three
  fog modes and the absent-array default now follow the recovered pipeline.
  Partial texture uploads take exclusive right/bottom bounds from GERD, not
  width/height; confusing them overwrote untouched UI texels and read beyond
  temporary staging storage. Magnification comes from packed bits 4..5 rather
  than the perspective-correction bits. Nine fog readbacks and 8,192 texture
  texel comparisons cover those contracts.
- **Shared fixes first:** preserve the 0xad-byte serialized `W8WorldItem` with
  four-byte pointer slots, interpret saved links as chain markers, and stage
  native item pointers across output APIs. Huffman symbol trees and the vertex
  processor bank allocate by element size. Packed dice/vector/UTF16/condition/
  allied-group/cache accesses retain their retail byte offsets while avoiding
  native alignment UB. Raw bit/triangulator allocations use matching unsized
  deletion on native builds. Native guards also handle unused null trace
  cursors, unmatched monster-cycle table rows, non-finite normal compression,
  streamed audio cache sentinels and fatal shutdown before handle-table teardown.
  Windows control flow and code generation remain intact.
- **Light ownership evidence:** the native copied `srLight` constructs a fresh
  registered base instead of sharing scene links/registry ownership. The retail
  `MonsterLight` copy at **0x0049D660** calls the zero-parent illuminator
  constructor at **0x0049D682**, registers the new class, then invokes light
  assignment at **0x0049D6BF** before copying its members. The existing
  `srClassSupport(const Derived&)` implements that ownership sequence. A copied
  light lifecycle test and real recovered world shutdown exercise the fix.
- **Validation:** **12/12 native tests pass** (`world-final-tests.log`). Packed
  UTF16, branching Huffman and copied-light client tests pass ASan/UBSan with
  leak detection (`build-native-asan/world-focused-tests.log`). The world harness
  reaches rendering and shutdown without invalid accesses or UB reports
  (`build-native-asan/world-run.log`). GPU fog/partial-texture assertions pass
  under sanitizers too (`world-gpu-tests.log`). Legacy clang-cl builds; all
  **299 non-zlib objects remain exact** in instructions, relocations and `.rdata`
  against `228fa4c` with `SOURCE_DATE_EPOCH=1791503644`
  (`build-clang/world-final-objdiff.log`). VC6/macOS remain untested.
- **Sanitizer boundary:** full-world LeakSanitizer still reports **1,275,925
  bytes in 205 allocations**, including game status/vector/level/material
  owners and external driver/DBus allocations. GPU-only validation reports
  **55,148 bytes in 22 allocations**, including runtime-class name ownership
  and external modules. These runs exit nonzero because of leaks; they are not
  leak-clean results. No suppressions were introduced.
- **Dependencies/publication:** no packages were installed. Keep the user's
  preference: provide installation commands if new dependencies are required.
  Code is followed by this final documentation commit, and the parent handoff
  copy is synchronized. All controlled game/display processes were stopped.

Reproduce with installed assets and an existing character basename:

```sh
WIZ8_ASSET_ROOT=/path/to/Wizardry8 SDL_VIDEODRIVER=x11 \
  build-native/native_world_graphics party.CHR "$PWD/build-native/world.ppm"
WIZ8_ASSET_ROOT=/path/to/Wizardry8 SDL_VIDEODRIVER=x11 \
  build-native/Wiz8Native /WINDOW
```

Next: validate desktop focus and physical movement/look input, then save/load
round trips and ordinary gameplay. Compare world output against the shipped
renderer and audit the observed game-owned lifetime leaks. Recovered Targa
textures already serve this world; native JPEG integration remains open.

## Continuation — FFmpeg movies and native game startup, Oct 9, 2026

Native movie playback and the actual game executable are implemented at
`822ce38d47e15eff1dc094b9bada4b8820456b61`. This final handoff commit brings
`native-port` to **32 commits over main: 19 upstreamable commits first**, ending
at `upstream-tip` (`f37bb567327d2278c320d13f8cb5725d60cd646c`), followed by
13 native/documentation commits. The shared commit was restacked below the
entire native suffix, with an exact tree comparison before restoring native
work. Earlier sections contain historical hashes from prior stacks; consult
`git log` for the current suffix. No decomp-repo upstreaming was done.

- **Full link and startup:** `Wiz8Native` links the real SDL entry point,
  recovered game loop, 211 recovered game units plus two native movie units,
  and the 33-unit SGP archive. The five remaining Bink contracts are now real
  implementations. A whole-archive link also succeeds without unresolved
  contracts (`build-native/movie-link-audit.log`). No fake media routines or
  harness-only substitutions were added to the executable.
- **Movies:** `src/native/movie.cpp` owns bounded platform handles for loose
  files or recovered SLF entries, FFmpeg custom AVIO, codecs, resampler and
  RGB555 conversion. Decoding remains on the main thread, with a quarter-second
  of frame lookahead and bounded PCM buffering. Container timestamps pace
  frames; EOF drains codecs/PCM and retains the final frame for its duration.
  Movie-owned PCM voices share the existing miniaudio engine. Reopening stops
  the preceding movie/voice. Only the used zero Open flags are supported;
  movie output requires RGB555 and dimensions no larger than 640x480. When
  soundman has no output engine, video can run silently.
- **Black window fixed:** recovered `stSurface2D::updateRectangle` ignores its
  pixel-pointer argument and uploads through each texture's source surface.
  Passing movie pixels while retaining the game's empty source produced black.
  The native movie now owns a separate SurRender surface/tile set. It presents
  in the existing SDL window without changing the game primary. The graphics
  check matches all 307,200 movie/background pixels, then checks restoration
  of the game primary and persistent window. Retail UI/cursor checks remain
  58,548 matching UI pixels and 556 cursor pixels.
- **Visible game behavior:** with installed assets, X11 and a private user
  overlay, the full executable displays the Sir-Tech movie. Synthetic Escape
  transitions to the rendered main menu, with retail artwork and cursor.
  A standard WM_DELETE_WINDOW request reaches the native window procedure and
  exits normally (status 0). The menu's recovered Exit screen waits for an
  additional key/button press. Screenshots: `build-native/movie-fixed.png`
  and `movie-menu.png`. Physical input and audible output were not independently
  verified; these are startup/menu checks, not gameplay or world-mesh parity.
- **Shared fixes first:** the opaque native movie handle and primary-surface
  declaration are guarded without changing Windows layout. Native movie
  presentation brackets ordinary frame rendering. SGP exit releases screen-owned
  movie voices before the audio engine, and native main invokes the guarded exit
  handler before SDL shutdown. Startup ASan exposed the item-category array's
  four-byte allocation; it and the remaining explicit pointer banks in prepath,
  submesh lighting, octree particles/props, build scratch and GameData triggers/
  environments now allocate by pointee size, preserving spare element counts.
  Serialized four-byte scalar arrays were left intact.
- **Validation:** 12/12 native tests pass. Six focused I/O/input/import/surface/
  audio/movie tests pass ASan/UBSan with leak detection; the installed movie also
  passes the movie sanitizer test. Authored FFV1/PCM fixtures check timed frames
  byte-for-byte against the FFmpeg CLI, stereo PCM count/energy, loose/SLF reads,
  a truncated SLF entry, last-frame duration, reopen, release and invalid flags.
  The installed Sir-Tech movie decodes **486 frames and 716,160 PCM frames**;
  RGB555 FNV64 **a8ac71db8aa752cc** matches the CLI over every decoded byte.
  The syntax probe remains 246/246 clean; the two new native movie units compile
  through CMake separately. Legacy clang-cl builds and all **299 non-zlib objects
  remain exact** in instructions, relocations and `.rdata` against `228fa4c`,
  using `SOURCE_DATE_EPOCH=1791503644`. VC6/macOS remain untested.
- **Sanitizer boundary:** the full game reaches movie/menu/exit without an
  invalid-access report. LeakSanitizer is **not clean**: it reports existing
  `ResetGameStatus` character/status buffers and `gXStatus` vector allocations
  overwritten by initialization, plus the already observed DBus/unloaded-driver
  allocations. Closing during an active movie also reaches shutdown without
  invalid accesses (`build-native-asan/movie-active-close.log`). See
  `build-native-asan/movie-runtime-check.log` for the menu/exit leak report. Do not conflate
  the leak-clean focused movie test with the full executable. No suppressions
  were introduced and no retail cleanup behavior was invented to hide leaks.
- **Dependencies:** system FFmpeg development libraries were already installed.
  No packages were installed by the agent. Keep the user's preference: give
  installation commands for missing dependencies and let the user install them.
  README now lists the required FFmpeg/pkg-config packages.

Next work should start from the actual executable rather than the old link
inventory: validate a new-game/load path and visible world mesh/draw calls,
then resolve failures using runtime evidence. Native JPEG/Targa integration,
shipped-renderer world parity, save/load and the observed game-owned lifetime
leaks remain open. Exercise focus/resume and real input too; X11 synthetic
menu input does not establish Wayland or physical-input correctness.

Publication uses exact leases on the previously verified branch
`34ffe0f82eb5b7ae67116661d228369a0ad62295` and tag
`e7e5647517a1f2ad45be71388c19ecbb0b72713a`. The tracked handoff and parent-directory
copy are synchronized. Evidence logs/artifacts live in ignored build directories.

## Continuation — native audio and pipeline ownership, Oct 9, 2026

The native audio implementation is committed at
`47f084c0908d126a05b0d4c901026973d9d8c70b`. This final handoff commit brings
`native-port` to 29 commits over `main`: 18 upstreamable commits first, ending
at `upstream-tip` (`e7e5647517a1f2ad45be71388c19ecbb0b72713a`), followed by 11
native/documentation commits. Publication uses exact leases on the previously
verified branch `0e9c2080d1528e57a1ac7d789a5be73a333917ab` and tag
`719095bfde14840cad2b772f7d49b47f501ee960`. No decomp-repo upstreaming was done.

- **Pipeline ownership fixed:** game methods and the game vtable bind locally
  on native clients; the DLL-owned `srTriMeshPipeline::pipe` remains imported.
  This preserves the original EXE/DLL ownership rather than allowing ELF
  interposition to merge the two implementations. The graphics test checks
  distinct game/DLL `Get` functions returning the same singleton and exercises
  allocation, reset, flush and destruction through `srExit`. Strict ASan no
  longer reports the vtable collision. Windows declarations/codegen are intact.
- **Audio implemented:** retain recovered `soundman.cpp` for cache, IDs, channel
  selection, priorities, random scheduling, fades, flags and EOS callbacks.
  `src/sgp/native/audio.cpp` implements its used Miles calls through pinned,
  unmodified miniaudio 0.11.25 (`third_party/miniaudio`, original license retained).
  Memory decoders own encoded bytes; clearing cache while music plays stays safe.
  Streams use bounded platform handles at the recovered SLF entry's offset and
  length, without encoding full-width native handles in filenames. Decoder
  reads/seeks/cursor queries share a mutex; finite/infinite loops use an atomic
  counter. Native shutdown releases voices before their driver and dispatches
  no EOS callbacks. The all-ones callback sentinel is pointer-width safe.
- **Native adaptations:** legacy provider selections use miniaudio spatialization;
  EAX effects are unavailable and are not advertised. The unused Miles raw-buffer
  callback interface fails explicitly if requested. Tests use an offline engine;
  production starts its real native output device. No system packages were
  installed. The user's package preference is explicit: provide install commands
  for missing dependencies; never install them yourself. FFmpeg development
  libraries and the CLI are already present on this machine.
- **Validation:** 11/11 native tests pass. Audio tests cover generated WAV and
  MP3 samples/streams, WAV-to-MP3 fallback, loose/SLF streaming, duration/cursor,
  pan/volume, finite/infinite loops, fades, priority groups, 32 occupied channels,
  random scheduling, spatialization, callbacks/sentinels and repeated lifecycle.
  `native_audio_test --device` also opened the native output device and observed
  playback completion; audible output was not independently checked. Audio,
  including miniaudio C code, passes full ASan/UBSan with leak detection. The
  real UI/cursor graphics check still matches 58,548 UI and 556 cursor pixels.
- **Graphics leak boundary:** strict ASan reaches correct pixels but still
  reports shutdown allocations in DBus and unloaded external modules. The new
  `native_gpu_lifecycle` target reproduces these allocation sites using only
  SDL/Vulkan, without game or renderer code. This is an independently reproduced
  host-stack limitation; graphics remains **not leak-clean**. No sanitizer
  suppressions were added. The standalone test enables SDL's DBus shutdown
  diagnostic hint; production does not enable it. See
  `build-native-asan/audio-graphics.log` and `gpu-lifecycle.log`.
- **Build/link frontier:** 211 recovered game units, 33 SGP/native units and the
  separately built miniaudio implementation. Probe: 246/246 clean, with four
  whole Windows/media units excluded. Whole-archive link including actual main
  now has **five unresolved contracts**, all `W8BinkVideo` constructor,
  destructor, Open, SetTarget and UpdateFrame. Audio, graphics and shell resolve.
  The full game still does not link/run until movie playback is implemented.
- **Legacy:** clang-cl builds; exact instruction bytes, relocations and `.rdata`
  match all 299 non-zlib objects against `228fa4c`, using the established
  `SOURCE_DATE_EPOCH=1791503644`. VC6 and macOS remain untested.

Shared commit: `e7e5647`. Native implementation: `47f084c`. Earlier native
commits were restacked to `d0e451f`, `9350e50`, `dff2987`, `30fb609`, `6576126`,
`cb3363a`, `52281a4`, `5e0bf5b`, `229c0fc`; the combined tree was preserved.
README.md and tests/native/README.md contain build/test commands and limits.

**Next:** implement FFmpeg-backed Bink playback against the recovered
`W8BinkVideo` contract, including pacing, target/primary RGB555 output and audio.
Finish the real game link; then validate startup, resize/focus/input, shutdown,
saves and gameplay interactively. JPEG/Targa importer integration and shipped
renderer parity remain in the revised plan. Keep further shared commits before
native commits; refresh remote IDs before any restacked branch/tag push.

Earlier continuation sections below are historical and superseded here.

## Continuation — CPU surfaces, recovered Video2 and SDL shell, Oct 9, 2026

Work stopped at the user's request after publishing the implementation at
`9543f0cb313bc4a93ffa4dd29977921442bbf5a9`. This handoff is included in the final
documentation commit on `native-port`, bringing the branch to 26 commits over
`main`: all 17 upstreamable commits first, ending at `upstream-tip`
(`719095bfde14840cad2b772f7d49b47f501ee960`), followed by nine native/documentation
commits. The implementation branch and tag were pushed atomically with exact
leases and their remote IDs verified; the handoff follows as a normal branch
push. No decomp-repo cherry-pick or PR was performed.

- **Shared prefix:** native window/surface declarations and guards let the
  recovered `vsurface.cpp`, `sgp.cpp` and `Video2.cpp` build without Windows SDK
  or COM. Their Windows paths remain intact. Native clip-list construction uses
  byte offsets and header size; wrapped surface storage initializes its ownership
  fields. Renderer window/pick tokens retain full-width native pointers.
- **Native implementation:** CPU 8/16/32-bit surfaces provide pitch, locking,
  fills, keys, stretching, self-overlap, clip unions and palette/clipper lifetime.
  SDL windows use the existing message/input bridge; warps update the cached
  position immediately. Video2 retains its scene/tile/cursor helpers and uses
  SDL GPU for presentation. Native main and window procedure compile, including
  focus suspension and the recovered game loop. **The full shell does not yet
  link or run**; audio and movies remain absent. Movie-target allocation is
  available, but actual decoding/presentation belongs to the next media batch.
- **Build:** 211 recovered game units and 31 SGP/native units build. The probe
  reports 243/243 clean units, with five whole Windows/media units excluded.
  A whole-archive audit including the actual main has 38 unresolved contracts,
  all sound-manager/Bink ownership, including `gfEnableStartup`; no graphics,
  entry-point or SurRender contract is unresolved.
- **Validation:** 10/10 native tests pass. The new surface test matches all 90
  Wine DirectDraw captures; these are distinct from the 270 legacy-assembly
  blitter captures. Installed retail SLF/STI assets render through the recovered
  CPU surface, tile renderer and cursor scene: all 58,548 colored UI pixels match
  CPU RGB555 within eight levels per channel, plus 556 cursor pixels. This is
  focused rendering, not interactive gameplay; the harness uses input-only
  dispatch, a test-only sound-provider recorder and startup flag. Its temporary
  overlay/private 640x480 config preserves installed assets and user settings.
- **Sanitizers:** surface, file, event and renderer-client tests pass full
  ASan/UBSan with leak detection. **Graphics sanitizers are not clean**: default
  ASan reports duplicate `srTriMeshPipeline` vtables between game and renderer.
  A diagnostic with `detect_odr_violation=1` reaches correct pixels but reports
  shutdown leaks in external/unknown modules. Logs are
  `build-native-asan/graphics-sanitizer-strict.log` and
  `build-native-asan/graphics-sanitizer-diagnostic.log`. A separate zero-length
  upload memcpy with a null source was fixed in the SDL GPU implementation.
  Keep the ownership/leak findings open; do not describe graphics as sanitizer clean.
- **Windows:** clang-cl rebuild passes; exact instruction bytes, relocations
  and `.rdata` match across all 299 non-zlib objects versus `228fa4c`, with the
  established `SOURCE_DATE_EPOCH=1791503644`. VC6 and macOS remain untested.

New shared commit: `719095b`. New native commit: `9543f0c`. Earlier native commits
were restacked to `10307dc`, `5074850`, `04abd0a`, `51c19be`, `1a245e2`,
`0bf610f`, `ce96256`, in that order. Stacking preserved the prior combined tree.
Reproduction and limitations are in `tests/native/README.md`.

**Next work:** resolve native game/renderer pipeline ownership, then implement
miniaudio sound and FFmpeg/Bink playback against their recovered contracts.
Finish the real game link and validate initialization, input, resize/focus,
shutdown, saves and gameplay interactively. Keep every further shared change
before the native suffix; publishing a restacked branch/tag requires exact
leases on freshly verified remote branch/tag IDs; the final documentation commit
advances the branch beyond the implementation hash above. The user requested
stopping now; do not continue implementation until asked.

Earlier continuation sections below are historical; their IDs, status and
remaining-unit counts are superseded by this section.

## Continuation — native game core and SDL input, Oct 9, 2026

`native-port` is clean at `6d66b32`, 23 commits over `main`. All 16
upstreamable commits come first, ending at `upstream-tip` (`9d4f52c`); all
seven Whizzardry-only commits follow. Both the branch and tag are pushed to
`https://github.com/agluszak/whizzardry-8.git`, and their remote IDs were verified.
The restacked push used exact leases on the previously published branch/tag.

- **Game build:** `libWIZ8_GAME_CORE.a` now compiles 210 recovered game units;
  `WIZ8_SGP` compiles 26 units, including the recovered `input.cpp`, clock,
  file/archive readers, UI controls, images and blitters. Eight platform units
  remain excluded: Miles imports, Bink, Video2, SGP shell, surfaces, sound and
  the two DirectDraw/DirectX units. This is not a runnable game executable.
- **SDL bridge:** `src/compat/platform_events.cpp` implements the declared
  message pump and callback timers, plus client mouse position, confinement
  and minimization. Register an SDL window with `w8_native::attach_window`,
  and detach it before destruction. SDL keys, mouse, focus and wheel messages
  feed `NativeInputWindowProcedure`, which calls the recovered queue/key
  routines. The original Windows hooks remain in their Windows branches.
  Timers dispatch on SDL's main thread, preserve full-width IDs and coalesce
  queued ticks. Fractional scroll accumulates into the recovered 120-unit
  wheel protocol; held input clears on focus loss. No SDL IME/text-input port.
- **Shared contracts:** native renderer clients now use generated special
  members like the provider for camera, clip plane, fog, material interface
  and Huffman sampling. The native assertion declaration matches the variadic
  provider; Windows import spellings remain intact. Sanitizers exposed a
  four-byte-aligned `srQuadWord` read through an eight-byte pointer; the native
  conversion now combines its two words without an unaligned load.
- **Probe correction:** C units are checked as GNU C17, not C++. This exposed
  the native assertion macro's C-invalid `true`, now spelled `1`. All 236
  current non-platform units pass, including the native input adapter.
- **Validation:** all 9 native tests pass, including SDL GPU readback. The
  recovered-input/clock integration, file integration and renderer-client
  tests pass full ASan/UBSan with leak detection. SDL input tests use the dummy
  video driver and pushed events, not interactive gameplay. The clang-cl build
  passes; exact instruction bytes, relocations and `.rdata` remain identical
  across all 299 non-zlib objects versus the preserved `228fa4c` baseline, with
  `SOURCE_DATE_EPOCH=1791503644` for the renderer build string. VC6 and macOS
  remain untested.
- **Link frontier:** a whole-archive native link reports 163 distinct unresolved
  symbols, owned by the excluded platform units and entry point, with no
  unresolved SurRender APIs. Saved logs: `build-native/game-link-audit.log`
  and `build-native/game-unresolved.txt`; reproduction is in tests/native/README.md.
  No production placeholder implementations were added to hide that boundary.

New shared commit: `9d4f52c`. New native implementation/test commit: `6d66b32`.
The six earlier native commits were restacked to `af645d3`, `65b1ef1`, `a37be9c`,
`33d0b9b`, `03b1a97`, `7d1d7f2`, in that order.

**Next frontier:** SDL application entry point and CPU video surfaces, then
port Video2 while retaining its recovered renderer/game helpers. Wire the new
message/input bridge into that shell; retain `input.cpp` rather than replacing
its queue/string behavior. Audio and video decoding follow the revised plan.
The game still does not link or run natively.

Earlier continuation sections below are historical; their branch/local IDs,
push status and remaining-unit counts are superseded by this section.

## Continuation — native filesystem, Oct 9, 2026

`native-port` is clean at `6fc3fbf`, 21 commits over `main`. The first
15 commits are upstreamable and end at `upstream-tip` (`73e6abb`); all six
Whizzardry-only commits follow. Nothing was pushed.

- **Implemented:** all 34 non-shell `W8*` APIs in `compat/platform.h`, plus
  path-aware CRT file opens, rename/remove, access/chmod and virtual CWD.
  The native implementation is in `src/compat/platform_*.cpp` and is shared
  between the executable and SurRender through `libwiz8_compat`.
- **Paths:** `WIZ8_ASSET_ROOT` is the virtual C: installation root;
  `WIZ8_USER_ROOT` supplies a writable overlay. Existing asset updates copy up;
  installed files are preserved. Backslashes and ASCII case lookup work across
  Win32 wrappers, CRT opens and renderer streams. Optional `WIZ8_CD1_ROOT` through
  `WIZ8_CD3_ROOT` expose read-only D:/E:/F: discs with WIZ8_1/2/3 labels.
  Defaults, overlay deletion behavior and platform limits are in README.md and
  tests/native/README.md.
- **SGP integration:** FileMan.cpp, LibraryDataBase.cpp and WizLibs.cpp now build
  natively. The integration test creates retail-layout SLF records and runs the
  recovered reader through mapped and streamed reads, loose-file precedence,
  archive seeks, timestamps and save round trips. It also exercises renderer
  input/output streams and directory searches. These are generated fixtures,
  not captures from installed retail SLF archives or gameplay.
- **Validation:** all 7 native tests pass; all 234 non-platform probe units
  remain clean. The full file integration test, including SurRender, passes
  ASan/UBSan with leak detection. Native bsearch comparator declarations needed
  their required const-void-pointer signatures; Windows signatures are retained.
  Native renderer clients now use the same generated stream special members
  as the library, avoiding a missing base-destructor symbol.
- **Windows:** clang-cl rebuild passes; exact instruction bytes, relocations
  and .rdata match across all 299 non-zlib objects versus 228fa4c. SurRender's
  core.cpp build string was pinned with SOURCE_DATE_EPOCH=1791503644 to match
  the preserved baseline. No instruction/immediate/symbol normalization was
  used. VC6 and macOS remain untested.

New shared commit: `73e6abb`. New native implementation/test commit: `6fc3fbf`.
The existing five local commits were restacked to `04a3b4e`, `bfec1c5`, `0568f26`,
`9022ce9` and `de5f001`, in that order.

**Next frontier:** build/link the remaining SGP and game units and replace the
nine whole-platform units. SDL shell/message/timer/input/video surfaces are
next; audio and video decoding follow the revised plan. The game still does
not link or run natively. File sharing covers in-process W8 handles only;
unsupported overlapped/named/writable mapping operations fail explicitly.
Linux creation timestamps currently use ctime; details are documented.

The earlier continuation below is preserved as history; its branch and local
commit IDs are superseded by this section.

## Continuation — Oct 9, 2026

The first two next steps are complete. `native-port` is clean at `f3c1941`, now
19 commits over `main`. `upstream-tip` is `4da729c`; the six new shared-source
commits were restacked below the native-only commits. Nothing was pushed.

- **Probe:** all 234 non-platform translation units pass, with zero errors.
  The nine whole-platform replacements remain excluded by the existing rules.
  The probe now applies `NOMINMAX` and `WIN32_LEAN_AND_MEAN` to game sources.
- **Native lane:** all 20 SGP assembly blitters have C++ branches and are built
  into `WIZ8_SGP`. All 6 native tests pass, including the SDL GPU readback spike.
- **Pixel oracle:** 270 cases captured from the legacy assembly pass against
  native output. They cover clipping, font masks, transparent runs, mirroring,
  row padding and overlapping copies. The same cases pass ASan and UBSan.
  Provenance and regeneration commands are in `tests/native/README.md`.
- **Legacy lane:** clang-cl rebuild passes. Both the handoff's normalized
  object comparison and a stricter comparison of exact instruction bytes,
  relocations and `.rdata` report zero differences across 299 non-zlib objects
  versus a preserved build of `228fa4c`. VC6 has not been run.
- **Fixes:** Windows shell/header isolation; complete wide-text parameter and
  cast rewrites; additional platform declarations and call renames; full-width
  button pointers through the existing pointer table; monster out-pointer
  writeback; reviewed low-bit pointer casts; native-safe varargs context;
  timer/video presentation contracts. Native `min`/`max` also needed a fix:
  their conditional-expression return types were references to local arguments.

New shared commits, in stack order: `d1e4405`, `996e590`, `a6d6cd9`, `d6f5c5c`,
`f970b9f`, `4da729c`. New native-only test commits: `5c0ed2b`, `f3c1941`.

**Next frontier:** implement the declared `W8*` platform functions, then add
the game/full-SGP native targets. Timer/message-pump/video declarations compile
but still need the SDL shell's implementations. The game does not link or run
natively yet. Audio, video decoding and GPU driver parity remain as below.

The original handoff below is preserved as historical context; its error
counts and rewritten native-only commit IDs are superseded by this section.

## State at handoff

All work is committed on branch `native-port` (12 commits over `main`, tree clean). The native lane builds SurRender with an SDL3 GPU device; 4 of 4 native tests pass. Game and SGP code do not link natively yet: the native probe reports 176 errors in 123 of 234 translation units, down from 4,922.

- **Native lane:** `libsr.so`, SGP compression core, CRT compat library. Tests: zlib round trip, Microsoft wide-string semantics, no glibc wide-string imports, `srdd_spike` (textured triangle through `srGERD` on Vulkan, checked by readback).
- **Legacy lane:** the clang-cl build still produces `Wiz8.exe` and `sr.dll`. Every upstream commit was checked object by object to leave that build's code unchanged; the VC6 build has not been run.
- **Plan:** the revised roadmap and the original findings are in the plan doc, [Whizzardry 8 native port: revised plan](https://claude.ai/code/artifact/64a1e46d-1250-443e-ab1b-d42155e1c45e). Its open question on upstreaming `w8_long` is answered: everything in the upstream commits goes back to the decomp repo.

## Branch and commit stack

Upstream commits sit directly on `main` and end at tag `upstream-tip`; whizzardry-only commits follow. Upstream commits change only files the decomp repo shares and leave the Windows build's code unchanged.

| Commit | Kind | Content |
| --- | --- | --- |
| `228fa4c` | local | `tools/legacy_objdiff.sh`, `tools/stack_upstream.sh` |
| `a477d5d` | local | `wiz8_compat`: Microsoft CRT and two-byte wide strings; `-fno-builtin-wcslen`; tests |
| `dfefa5d` | local | Native CMake lane, `src/surrender/native/`, SDL3 GPU device and shaders, zlib header move, spike and zlib tests, README |
| `5d658d8` | upstream, `upstream-tip` | wide-vars codemod handles assignments |
| `7ade950` | upstream | `compat/platform.h`: Win32 file calls renamed to `W8*`; key codes, `min`/`max` |
| `2744e46` | upstream | `compat/native.h` maps CRT and wide-string names |
| `c9f3d29` | upstream | `tools/native_probe.py`, `tools/native_codemod.py` + rules; includes and wide-param passes |
| `a80f5d9` | upstream | `W8_PTR32` slots in 13 raw-I/O records; 443 checks to `W8_ABI_ASSERT` |
| `2c6c44c` | upstream | Shared headers stop leaking `windows.h`, DirectDraw and Miles |
| `12dbd30` | upstream | `w8_long` in game code |
| `39455f0` | upstream | `w8_long` in SurRender/SGP, `W8_ABI_ASSERT`, native branches, include case, tools |

To upstream: cherry-pick `main..upstream-tip` onto the decomp repo. Nothing has been pushed.

## How to work

Every change to shared sources goes through one loop: probe, rewrite by tool or table, prove the Windows build unchanged, commit, restack. Hand edits are for genuine one-offs.

1. **Probe.** `python3 tools/native_probe.py --summary identifiers` (or `messages`, `files`) compiles game and SGP code with the native flags and writes `build-native/probe.jsonl`. Platform units listed in `tools/native_codemod_rules.json` are skipped; `--all` includes them.
2. **Rewrite.** Pick the largest class and extend a table or a pass, never individual files:
   - `tools/native_codemod_rules.json`: include rules, renames to `W8*` wrappers, include drops, raw-I/O record list.
   - `tools/native_codemod.py includes | wide-params | wide-vars | rename | abi-asserts`. Diagnostic-driven passes read the latest probe; rerun probe and pass until no edits.
   - Names with exact native equivalents go in `compat/native.h` (CRT) or `compat/kernel32.h` (Win32 data types, constants) instead of touching call sites.
3. **Verify legacy.** Build the clang-cl lane and diff against a baseline build of the parent commit: `WINSDK_ROOT=$PWD/build-clang/xwin cmake --build build-clang/legacy-check`, then `tools/legacy_objdiff.sh <baseline> build-clang/legacy-check` must print nothing. Make the baseline once with `git worktree add` and the same cmake line ([README](https://github.com/agluszak/whizzardry-8) commands; zlib sources are in `.wiz8-work/sources/unpacked/zlib-1.0.4/zlib-1.0.4`).
4. **Commit.** Upstreamable changes alone in one commit, then run `tools/stack_upstream.sh` to move it below the local commits and advance `upstream-tip`. Native-only code (CMake, `src/compat`, `src/surrender/native`, devices, tests) is committed as local.
5. **Native build.** `cmake --build build-native && (cd build-native && ctest)`; needs SDL3, zlib and `glslang-tools` from apt.

## Key design decisions

Each mechanism keeps the Windows lanes token-identical and gives the native lane the retail semantics.

| Mechanism | Windows lanes | Native lane | Why |
| --- | --- | --- | --- |
| `w8_long` / `w8_ulong` | `long` | 32-bit int | MSVC `long` is 32-bit; LP64 makes it 64. Applied by `tools/native_long_rewrite.py`, rerunnable on ported files |
| `w8_ulong_ptr` | `unsigned long` | `uintptr_t` | Pick keys, window handles and pointer hashes that carried addresses in a `long` |
| `W8_ABI_ASSERT` | `static_assert` | no-op | Layout checks of in-memory objects with pointers or vtables. 410 game checks and every raw-I/O record stay `static_assert` |
| `W8_PTR32(T)` | `T*` | 4-byte handle into a pointer table | Pointer slots inside records read or written as raw bytes (NPC database, scripts, `W8GDSurface`, save-game status, party, NPC and monster records). Keeps retail data files and saves loadable |
| `CHAR16` for text | = `wchar_t` = `unsigned short` | 2-byte builtin `wchar_t` (`-fshort-wchar`) | libstdc++ rejects `wchar_t` as a typedef; text parameters and buffers spelled `UINT16` become `CHAR16` |
| `compat/native.h` | not included | Maps MSVC CRT and wide-string names to `w8_*` with Microsoft semantics | `swprintf` without a count, `%s` wide; glibc's wide functions assume 4 bytes |
| `compat/kernel32.h` | `#include <windows.h>` | Win32 data types, `VK_*`, `GetTickCount`, `Sleep`, `min`/`max` | Types and constants only; no APIs, COM or DirectDraw |
| `compat/platform.h` | `#define W8X X` (Win32) | POSIX implementations (not written yet) | One place for paths, case-insensitive names and CD-drive emulation |
| `srDD_SDLGPU` | not built | `srGERD` device on SDL3 GPU | Replaces the `srDD_*.dll` drivers without changing `srGERD` |

The raw-I/O record list was produced by the compiler, not by grep: probe headers turned `FileRead`/`FileWrite`/`fread`/`fwrite` and `W8Chunk::Read`/`Write` into deprecated templates so clang named each buffer's type, and `-fdump-record-layouts` compared each type's native and i686 MSVC field offsets.

## Remaining native errors

The last probe reports 176 errors; 113 of them are one line of `sgp.h` repeated per translation unit.

| Errors | Where | Cause | Next step |
| --- | --- | --- | --- |
| 113 | `src/sgp/sgp.h:48` | `WindowProcedure` declared `FAR PASCAL` with Win32 types | Keep the shell declarations out of the native lane (PR 6 owns `sgp.cpp`) |
| 20 | `vobject_blitters.cpp` | x86 `__asm` blitters | Port to C++ with identical pixel output; check for existing C paths first |
| \~14 | `Font.cpp`, `PortraitQuote.cpp`, `ReviewCharacterScreen.cpp`, `mousesystem.cpp`, `Button System.cpp` | Leftover `UINT16`/`CHAR16` mismatches the wide passes do not match (const, nested casts, `vswprintf` with a `UINT16` buffer) | Widen the wide-vars patterns; rerun to fixpoint |
| 8 | `ButtonUserData.h`, `DialogFactoryDialogs.cpp`, `ReadMesh.cpp`, `MasterFunctionList.cpp` | Pointers cast to 32-bit ints | Real 64-bit bug: SGP button user data is an `INT32` slot holding pointers; widen or use `W8_PTR32` per site |
| 7 | `Levels.cpp`, `OctPath.cpp`, `NPC Scripting.cpp`, `LibraryDataBase.cpp` | `GetLogicalDriveStrings`, `SetErrorMode`, message pump, `GetDateFormat`, `FormatMessage` | Add to `compat/platform.h` and the rename table |
| 5 | `FileMan.cpp`, `Octree.cpp`, `materials.cpp`, `LoadSaveGame.cpp`, `RegInst.h` | `<io.h>`, `<direct.h>`, `<tchar.h>` | Guard these includes in the native lane (new codemod table) |
| 3 | `timer.cpp` | `SetTimer`/`KillTimer` callback timer | PR 6: SDL timer or thread |
| 2 | `MonsterManager.cpp` | `&monster_info->p3D` on a `W8_PTR32` field | Read through a local and store back |
| 1 | `IntroScreen.cpp` | `BeginVideoPresentation` (DirectDraw) | PR 5/6 video presentation |

After the probe is clean, the native CMake lane needs the game target itself (define `NOMINMAX` and `WIN32_LEAN_AND_MEAN` for game code as the Windows lane does; not for SGP) and native implementations of `compat/platform.h` with tests.

## Pitfalls already hit

- **Inline wrappers are not codegen-neutral.** An inline forwarder for `FindFirstFile` changed register allocation and EH funclet numbering in `LoadSaveGame.cpp`. Windows-lane wrappers must be macros naming the Win32 function.
- **Signedness changes hide in pointer math.** `ptrdiff_t` vs `unsigned long` turned `idiv` into `div` (`memory_pool.cpp`) and `shr` into `sar` (`triangle_culler.cpp`). Keep the original arithmetic type in the Windows lane.
- **LLVM calls glibc behind your back.** It rewrote a wide strlen loop into `wcslen`, which reads four-byte characters; `-fno-builtin-wcslen` and the import-check ctest guard against it.
- **`min`/`max` as macros break libstdc++.** Use templates in C++; keep macros only for C.
- **Hard-coded retail sizes.** `srZeroMemory(&x, 0x13c8)`-style clears and `memset(texture, 0, 0xa4)` must be expressed by member boundaries natively; search for literal byte counts near `memset`/`memcpy`.
- **`__declspec(novtable)` has no Itanium equivalent.** `srVP`'s 166 never-defined base virtuals are pure natively (`SR_VP_ABSTRACT`); the -O0 link fails otherwise.
- **SurRender API conventions.** `srMatrix4T` stores rows; `srGERD::perspective` takes radians; `srGERD` never calls `texImage`, so the device uploads on `bindTexture`; picking is done on the CPU; call `srInit()` before creating an `srGERD`.
- **Tool edges.** Include insertion must stay outside `#if` blocks; vendor headers (`ddraw.h`, `Mss.h`) and platform units are excluded by the rules file; file names with spaces break naive `file:line` parsing.
- **Process.** Never install packages without asking; give the apt command instead.

## Next steps

- [x] Finish the probe: the passes and table entries listed under Remaining native errors, each as one upstream commit.
- [x] Port the SGP blitters' asm to C++ and add a pixel test against blits captured from the legacy build.
- [x] Implement `compat/platform.h` natively (POSIX, backslash paths, case-insensitive lookup, read-only assets vs writable saves) with tests; this is PR 3.
- [x] Add portable game and SGP core targets to the native lane and audit the remaining platform link contracts.
- [x] SDL message/timer bridge and input into the recovered queue, with integration tests.
- [x] CPU surfaces and recovered Video2 integration; SDL main/window procedure compiled.
- [x] Preserve native game/renderer pipeline method ownership and shared singleton; strict ODR check passes.
- [ ] Graphics shutdown leak reports: independently reproduced in standalone SDL/Vulkan; keep the host-stack limitation documented.
- [x] PR 4 miniaudio adapter with recovered sound manager, WAV/MP3/SLF tests and native-device check.
- [ ] PR 5 FFmpeg playback; complete the real shell/game link and validate it interactively.
- [ ] Bring `srDD_SDLGPU` to parity: scissored clears, stencil, `TexParms` decoding, detail combiners, fog and alpha reference, then compare scenes with the shipped `srDD_Software`/`srDD_OpenGL` output.
- [ ] Upstream: cherry-pick `main..upstream-tip` into the decomp repo and run its VC6 matching check.

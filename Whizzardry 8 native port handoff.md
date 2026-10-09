# Whizzardry 8 native port: handoff

Oct 9, 2026 · @Mietek Pierdzibąk

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
- [ ] Resolve native game/renderer pipeline ownership and graphics sanitizer shutdown findings.
- [ ] PR 4 (miniaudio) and PR 5 (FFmpeg); complete the real shell/game link and validate it interactively.
- [ ] Bring `srDD_SDLGPU` to parity: scissored clears, stencil, `TexParms` decoding, detail combiners, fog and alpha reference, then compare scenes with the shipped `srDD_Software`/`srDD_OpenGL` output.
- [ ] Upstream: cherry-pick `main..upstream-tip` into the decomp repo and run its VC6 matching check.

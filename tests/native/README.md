# Native tests

Run the configured native lane with `cmake --build build-native` and
`ctest --test-dir build-native --output-on-failure`.

`native_events` sends SDL events through the native message bridge into the
recovered SGP `input.cpp` and `timer.cpp`. It checks keys/modifiers, repeated
keys, extended/numpad keys, string editing, mouse scaling and immediate warp queries, button repeats and
double clicks, fractional/flipped wheel input, focus-loss release, message
filters, queue wrap/full behavior, main-thread callbacks and the game clock.
Print-screen/video-capture entry points are test recorders, not video ports.
CTest selects SDL's dummy video driver; this checks event translation and
dispatch, not interactive window behavior.

The native message APIs run on SDL's main thread after a window is registered
with `w8_native::attach_window`; detach it before destroying the SDL window.
Timers use full-width IDs and callbacks execute only during message dispatch.
Already queued timer callbacks remain dispatchable after cancellation, per
[KillTimer](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-killtimer).
The bridge distinguishes old peeked messages from new arrivals for
[WaitMessage](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-waitmessage)
and follows [SetTimer](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-settimer)
interval bounds. Text goes through the recovered key table/string editor;
SDL text-input/IME support is not implemented.

`native_imports` compiles a separate renderer client and exercises generated
copy/destructor contracts for cameras, clip planes, fog, materials and Huffman
sampling, plus the provider's variadic assertion signature. It also checks a
four-byte-aligned `srQuadWord` conversion. Native clients use the same generated
members as the library; Windows import declarations remain intact.

`WIZ8_GAME_CORE` compiles 211 recovered game units, including Video2, except
Miles imports and Bink. The 33-unit SGP archive includes its recovered shell and
surface manager, sound manager and CPU/SDL/miniaudio adapters. The two Windows
DirectDraw units remain excluded. `WIZ8_NATIVE_SHELL` compiles the actual
SDL entry point. The full game link still requires movie playback;
building these archives and entry point does not prove gameplay.
To inspect the remaining contracts on Linux:

```sh
clang++ -o build-native/game-link-audit \
    build-native/CMakeFiles/WIZ8_NATIVE_SHELL.dir/src/sgp/native/main.cpp.o \
    -Wl,--whole-archive \
    build-native/libWIZ8_GAME_CORE.a build-native/libWIZ8_SGP.a \
    -Wl,--no-whole-archive -Lbuild-native -lsr -lwiz8_compat -lwiz8_miniaudio -lz -pthread -ldl -lm -lSDL3 \
    > build-native/game-link-audit.log 2>&1
```

The latest audit has five unresolved contracts, all `W8BinkVideo` playback
ownership. Audio, including `gfEnableStartup`, now resolves. Graphics, entry-point and
SurRender contracts resolve. This is a saved link inventory, not gameplay.

`native_surface_oracle` compares 90 cases at 8/16/32 bpp with Wine DirectDraw:
fills, source/destination keys, nearest-neighbour stretching, self-overlap and
canonical clip unions. Pitch and every destination byte, including padding,
are captured in `surfaces_legacy.txt`. This is a Wine DirectDraw reference,
separate from the legacy-assembly blitter captures and shipped-renderer parity.
Regenerate with the same harness:

```sh
WINSDK_ROOT="$PWD/build-clang/xwin" cmake -S . -B build-clang/legacy-check \
    -DWIZ8_BUILD_LEGACY_SURFACE_TEST=ON
WINSDK_ROOT="$PWD/build-clang/xwin" cmake --build build-clang/legacy-check \
    --target legacy_surface_test
WINEDEBUG=-all wine build-clang/legacy-check/legacy_surface_test.exe --capture \
    > build-clang/surfaces_capture.txt
tr -d '\r' < build-clang/surfaces_capture.txt > tests/native/surfaces_legacy.txt
```

The native CPU adapter retains row pitch, color-key ranges, palette/clipper
references and lock state. Host memory survives focus changes. Unsupported
operations throw explicitly. Native descriptions contain only fields used by
the recovered callers; they are not serialized COM layouts.
[Color-key ranges](https://learn.microsoft.com/en-us/windows/win32/api/ddraw/nf-ddraw-idirectdrawsurface7-setcolorkey)
follow `DDCKEY_COLORSPACE`; clipped stretching was checked against the captures
and [Wine's implementation](https://github.com/wine-mirror/wine/blob/master/dlls/ddraw/surface.c).

`native_game_graphics` requires installed retail assets and a display/Vulkan
GPU. It runs the actual SLF/STI readers, recovered surface/object managers,
Video2 renderer setup, `stSurface2D::DrawTiles` and cursor scene. It uses its own
temporary overlay and 640x480 video config. The default bottom-interface fixture
has 58,548 colored pixels; all match CPU RGB555 within eight levels per channel,
and the cursor contributes 556 GPU pixels in the sampled region.

```sh
WIZ8_ASSET_ROOT=/path/to/Wizardry8 build-native/native_game_graphics
# Optional asset and absolute PPM output path:
WIZ8_ASSET_ROOT=/path/to/Wizardry8 build-native/native_game_graphics \
    'Data\MAIN INTERFACE\BOTTOM.STI' "$PWD/build-native/game-ui.ppm"
```

The graphics harness uses `NativeInputWindowProcedure` and the real sound-manager
contracts. Its configuration disables audio startup. The full `WindowProcedure`
and main compile but need
the media implementations to link their game lifecycle. This checks real asset
rendering, not an interactive game or full-shell runtime. The 640x480 client-space
adapter scales SDL input/warps against actual window dimensions; native Video2
keeps its logical cursor coordinate calculations.

`native_audio` retains the recovered manager's cache, channel selection, random
scheduling, fade steps, callback dispatch and sound IDs. The native Miles-call
adapter uses vendored [miniaudio 0.11.25](../../third_party/miniaudio/README.md).
Memory decoders own encoded bytes so clearing cache while music plays retains
valid backing storage. Streams own bounded platform handles, retaining an SLF
entry's offset/length without encoding native pointers in filenames. Decoder
reads/seeks/cursor queries share a mutex; loop counters are atomic. The native
shutdown releases voices before their driver and dispatches no EOS callbacks,
matching retail shutdown's lack of callbacks. Native provider names select
miniaudio spatialization; EAX environment effects and the unused Miles raw-buffer
callback interface are unsupported. The latter fails explicitly if requested.

The generated WAV and 100 ms sine-wave `tone.mp3` fixture exercise sample and
stream decoding, WAV-to-MP3 filename fallback, duration/cursor, pan, volume,
finite/infinite loops, fades, music/cache lifetime, priority groups, 32 occupied
channels, random scheduling, spatial sounds, callbacks, all-ones LP64 callback
sentinels and repeated shutdown/reinitialization. The default test mixes through
miniaudio's offline engine, so it needs no sound device. A manual device check is:

```sh
build-native/native_audio_test --device
```

That command also opens the native output device and observes playback completion;
it does not independently verify audible output. The MP3 fixture was generated
with `ffmpeg -i tone-source.wav -c:a libmp3lame -b:a 96k -map_metadata -1 tone.mp3`;
the test constructs the equivalent source WAV programmatically.

Audio, surface, file, event and renderer-client tests pass ASan/UBSan with leak
detection. Compile both C and C++ with sanitizers to include miniaudio. The game
pipeline's methods/vtable bind locally on native clients while the DLL-owned
`pipe` remains imported. The graphics test checks distinct game/DLL `Get`
functions sharing one singleton and exercises its lifecycle through `srExit`.
Strict ASan no longer reports the prior vtable collision. Graphics still reports
shutdown leaks in DBus and unloaded external modules; a standalone SDL/Vulkan
lifecycle without game/renderer code reproduces these allocation sites
(`native_gpu_lifecycle`; saved log `build-native-asan/gpu-lifecycle.log`).
Build/run that target with the same sanitizer configuration to reproduce.
This remains a host-stack limitation,
not a clean graphics leak result. The standalone test enables SDL's documented
DBus shutdown diagnostic hint; production code does not enable that hint.
Linux focused-graphics links use `-z start-stop-gc`
so unused ASan global-registration sections can be discarded with unused game
functions while media remains absent.

`native_pointer` checks an address above 4 GiB through the signed 32-bit button
userdata adapter and a four-byte raw-record pointer slot, including null and
repeated-pointer identity.

`native_blitters` compares all destination bytes using FNV-1a 64-bit hashes,
return values and rectangle mutations against `blitters_legacy.txt`. The same
harness runs the original clang-cl assembly paths to produce that fixture.
Its 270 cases cover all 20 assembly routines plus the hatch wrapper: opaque and
transparent RLE runs, font zero/one masks, one/two/four/127-byte runs, every
clipped edge, complete rejection, null clip rectangles, offsets, mirror blits,
odd widths, row padding, transparency keys and forward overlapping copies.

The fixture was captured on Linux through Wine using Clang 23.1.2, the existing
xwin SDK and the legacy assembly from the `228fa4c` handoff. After porting,
all 299 non-zlib legacy objects were compared against that baseline, including
exact instruction bytes, relocations and `.rdata`, with no differences.
This is a clang-cl oracle; VC6 and shipped-binary pixel captures remain separate
validation work.

To regenerate using the configured legacy build:

```sh
WINSDK_ROOT="$PWD/build-clang/xwin" cmake -S . -B build-clang/legacy-check \
    -DWIZ8_BUILD_LEGACY_BLITTER_TEST=ON
WINSDK_ROOT="$PWD/build-clang/xwin" cmake --build build-clang/legacy-check \
    --target legacy_blitter_test
WINEDEBUG=-all wine build-clang/legacy-check/legacy_blitter_test.exe --capture \
    > build-clang/blitters_legacy.txt
```

Use an existing compatible `WINEPREFIX` if the default prefix is unsuitable.
Normalize Windows CRLF when copying the capture into the checked-in fixture.
Regenerate from the legacy executable, which selects the assembly branches.

Two inherited behaviors are intentional. The routines named “8BPP Shadow”
write 16-bit pixels but retain their byte-addressed origins and row skips.
The clipped version reads words at destination-value byte offsets in `pShade8`;
the harness supplies 65,537 bytes of backing storage so it checks every offset
without depending on out-of-bounds legacy reads. Pattern blits also use
`Pattern[0][0]` for the first column of each row. The clipped RLE tail scans for
a zero byte rather than decoding complete runs, exactly as the assembly does.

The blitter harness additionally passes AddressSanitizer and
UndefinedBehaviorSanitizer when compiled with the native flags at `-O1`.

`native_files` runs against temporary asset, user and disc roots, with a small
SLF fixture written using the recovered 532-byte header and 280-byte directory
records. It exercises the actual SGP library reader in both mapped and streamed
modes, loose-file precedence, seek/read/write saves, renderer file streams and
file searches. The recovered mapped-library timestamp-query failure is retained.
This checks the reader and native I/O together; it is not gameplay validation or
a capture from shipped SLF files.

The same test covers environment-driven roots, backslashes, ASCII case lookup,
merged enumeration (including `*.*` matching names without extensions), copy-up
updates without asset mutation, read-only files, missing parents, creation
modes, sharing failures, delete-on-close, invalid/stale handles, 64-bit sparse
seeks and mapping lifetime after closing handles. It also checks CRT rename
failure when the destination exists, disc labels, environment buffers, thread
local errors, UTC/local file times and the NPC report's date format.

`wiz8_compat` is shared so the executable and renderer own one namespace and
handle registry, including on macOS with its two-level symbol namespace.
Existing assets are copied into the user root for preserving updates; deleting
an asset-only file fails. Removing an overlay file reveals an underlying asset
again. Discs are exposed only when explicitly configured. External absolute
POSIX paths are supported, but changing the virtual current directory to an
external directory is unsupported. Case collisions select exact spelling first,
then the lexically first ASCII case match.

The wrappers implement the synchronous, regular-file calls used by the game.
Named/writable mappings, overlapped I/O, custom security descriptors and other
unsupported flags fail explicitly. Sharing is tracked between `W8*` handles
within this process; CRT `FILE*` handles and other processes are not tracked.
Read/write and directory attributes come from POSIX modes; unsupported Win32
attribute mutations fail. Linux creation times currently use POSIX `ctime`
(metadata-change time), while macOS uses birth time. CRT text output uses host
newlines. Date/error formatting supports the call patterns present in this tree.
Linux is validated; macOS and VC6 have not been run.

API details were checked against Microsoft documentation for
[creation modes and sharing](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilea),
[mapping offsets and lifetime](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-mapviewoffile),
[CRT rename](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/rename-wrename),
and [file-time conversion](https://learn.microsoft.com/en-us/windows/win32/sysinfo/file-times).
The local conversion retains Win32's use of the current timezone/DST bias when
converting historical file times.

The native file, event and renderer-client tests, including SurRender, can be
built with sanitizers:

```sh
cmake -S . -B build-native-asan -G Ninja \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_FLAGS='-fsanitize=address,undefined -shared-libasan -fno-omit-frame-pointer' \
    -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -shared-libasan -fno-omit-frame-pointer'
cmake --build build-native-asan --target native_files_test native_events_test native_imports_test native_surface_oracle native_audio_test
native_sanitizer_runtime=$(dirname "$(clang++ --print-file-name=libclang_rt.asan-x86_64.so)")
LD_LIBRARY_PATH="$native_sanitizer_runtime${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=1 \
    ctest --test-dir build-native-asan --output-on-failure \
    -R '^native_(files|events|imports|surface_oracle|audio)$'
```

`-shared-libasan` supplies the runtime to the shared libraries while retaining
`--no-undefined` at link time. This command is for the validated Linux lane.
The native SLF comparator declarations use `bsearch`'s required function type;
the Windows declarations and comparison bodies retain their behavior.

The latest exact comparison checks instruction bytes, relocations and `.rdata`
of all 299 non-zlib legacy objects against `228fa4c`. SurRender embeds `__DATE__`
and `__TIME__`; rebuilding its `core.cpp` with `SOURCE_DATE_EPOCH=1791503644`
reproduces the preserved baseline's `Oct  8 2026 23:54:04` build string. Other
bytes and symbols are compared without normalization.

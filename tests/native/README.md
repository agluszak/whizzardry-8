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
copy/destructor contracts for cameras, clip planes, fog, materials and lights,
plus branching Huffman compression/decompression and sampling, plus the provider's variadic assertion signature. It also checks a
four-byte-aligned `srQuadWord` conversion. Native clients use the same generated
members as the library; Windows import declarations remain intact.

`WIZ8_GAME_CORE` contains 211 recovered game units and the native movie decoder
and Bink owner replacement. The 33-unit SGP archive includes the recovered shell,
surface/input/sound managers and CPU/SDL/miniaudio adapters. `Wiz8Native` links
these with the actual SDL entry point. Windows Miles/Bink imports and the two
DirectDraw units are replaced rather than linked on native builds.

`native_movies` checks five timed FFV1 RGB555 frames byte-for-byte against the
FFmpeg CLI, 22,050 decoded PCM frames, actual miniaudio output energy, loose and
SLF sources, truncated-entry bounds, final-frame duration, repeated Open, owner
release and unsupported parameters. `movie.mkv` and `movie.rgb555` are authored
lavfi fixtures; reproduce them with `bash tests/native/generate_movie.sh`.
The CLI is needed only to regenerate fixtures, not to build or run tests.

The decoder uses FFmpeg's
[custom AVIO](https://ffmpeg.org/doxygen/trunk/avio_read_callback_8c-example.html)
and [packet/frame API](https://ffmpeg.org/doxygen/trunk/demux_decode_8c-example.html).
Video and PCM lookahead are bounded, decoding stays on the main thread, and a
movie-owned PCM voice uses the same miniaudio engine as recovered soundman.
Frames follow container timestamps; EOF drains codecs/audio and holds the final
frame for its duration. Only the used zero Open flags are supported. If the
sound manager has no output engine, movies can play silently. Movie surfaces
must support RGB555 and contain the decoded dimensions (at most 640x480).

The installed Sir-Tech Bink decodes 486 frames and 716,160 stereo PCM frames.
All decoded RGB555 bytes have FNV64 `a8ac71db8aa752cc`, matching the FFmpeg CLI
with `-sws_flags bilinear+bitexact -pix_fmt rgb555le`. To run that check using
assets supplied locally:

```sh
build-native/native_movie_test /absolute/path/to/Wizardry8/Data/Flics/Intro/sirtech.BIK
```

The actual game displays the Sir-Tech movie and the retail main menu after
Escape, with cursor and menu input. A standard window-close request reaches the
native window procedure and exits normally (status 0). The recovered menu Exit
screen waits for a further key or button press. Checks used a private overlay
and X11 on this Linux host.
Physical input, audible output, Wayland/macOS and interactive gameplay
have not been validated. Full-game ASan/UBSan reaches the menu and exit without
invalid accesses, but LeakSanitizer reports existing game status buffers and
vectors plus external DBus/unloaded-driver allocations. This is not a
leak-clean full-game result; no suppressions were added. The focused movie and
audio tests pass with leak detection.

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
and the cursor contributes 556 GPU pixels in the sampled region. It also checks
all 307,200 pixels of the movie output (authored frame plus black background),
the persistent window and unchanged game primary across movie presentation.
Movie tiles own their source: recovered `updateRectangle` gets pixels through
`stTexture2D::surface`, ignoring the ABI pixel argument. The native adapter must
not upload the game primary when presenting a separate movie surface.

```sh
WIZ8_ASSET_ROOT=/path/to/Wizardry8 build-native/native_game_graphics
# Optional asset and absolute PPM output path:
WIZ8_ASSET_ROOT=/path/to/Wizardry8 build-native/native_game_graphics \
    'Data\MAIN INTERFACE\BOTTOM.STI' "$PWD/build-native/game-ui.ppm"
```

The graphics harness uses `NativeInputWindowProcedure` and the real sound-manager
contracts. Its configuration disables audio startup. The full executable separately links the actual
`WindowProcedure`, main and media implementations. This focused check exercises
asset rendering without the full game lifecycle. The 640x480 client-space
adapter scales SDL input/warps against actual window dimensions; native Video2
keeps its logical cursor coordinate calculations.

`native_world_graphics` requires installed retail assets, an existing character
basename in `Saves/Characters`, and a display/Vulkan device. It initializes the
real game, adds that character to the party, runs new-game setup and enters the
ordinary loading screen and game loop. Only the opening movie is bypassed;
level/monster/item databases, SLF/Targa textures, octree meshes, actors, sky and
party UI use the production implementations. It owns a temporary user overlay
and writes a full-frame PPM and a `.control.ppm` with the world pass disabled.

```sh
WIZ8_ASSET_ROOT=/path/to/Wizardry8 SDL_VIDEODRIVER=x11 \
    build-native/native_world_graphics party.CHR "$PWD/build-native/world.ppm"
```

The Monastery beach check observed 38 enabled meshes and 3,394 submitted level
polygons. At the same camera, world on/off produced 160/35 GPU draw calls and
1,525/64 input triangles; disabling the world changed 130,597 viewport pixels,
and re-enabling it restored 129,048 pixels relative to the control. Counts can
vary with monster animation. Screenshots show textured sand, cliffs, water, sky,
wreckage and a chest. This proves world contribution separately from UI/sky
rendering; it does not establish shipped-renderer parity or interactive play.
`srdd_spike` additionally checks nine GPU readbacks for fog opacity 0, 0.5 and 1
under each of the three fog modes. Zero opacity preserves object color. Its
partial-texture regression checks all 4,096 texels before and after an 8x8
update at (40,48), proving that pixels outside the rectangle stay intact and
that native sampling follows the packed magnification field.

The world harness reaches rendering and recovered shutdown under ASan/UBSan
without invalid accesses or undefined-behavior reports. LeakSanitizer still
reports 1,275,925 bytes in 205 allocations from game status/vector/level/material
owners and external DBus/driver code. No suppressions were added. Focused packed
UTF16, branching Huffman and copied-light ownership tests pass with leak
detection. The ordinary executable also reaches the beach through the actual party
selection, options, intro and level-loading screens on an isolated X11 display
with Mesa lavapipe. Synthetic keys/buttons select the existing character, and
WM_DELETE_WINDOW exits with status 0. The desktop COSMIC/XWayland window remains
hidden without keyboard focus. NVIDIA on Xvfb shows stale presentation and
rapid resource growth; lavapipe avoids those symptoms. Physical input, audible
output and shipped-renderer parity remain unverified. GPU-only sanitizer
readbacks also pass their assertions without invalid accesses, but
LeakSanitizer reports 55,148 bytes in 22 allocations, including external
driver/DBus allocations and existing runtime-class name ownership.

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
cmake --build build-native-asan --target native_files_test native_events_test native_imports_test native_surface_oracle native_audio_test native_movie_test
native_sanitizer_runtime=$(dirname "$(clang++ --print-file-name=libclang_rt.asan-x86_64.so)")
LD_LIBRARY_PATH="$native_sanitizer_runtime${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=1 \
    ctest --test-dir build-native-asan --output-on-failure \
    -R '^native_(files|events|imports|surface_oracle|audio|movies)$'
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

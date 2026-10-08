# Native tests

Run the configured native lane with `cmake --build build-native` and
`ctest --test-dir build-native --output-on-failure`.

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

The full native file test, including SurRender, can be built with sanitizers:

```sh
cmake -S . -B build-native-asan -G Ninja \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -shared-libasan -fno-omit-frame-pointer'
cmake --build build-native-asan --target native_files_test
native_sanitizer_runtime=$(dirname "$(clang++ --print-file-name=libclang_rt.asan-x86_64.so)")
LD_LIBRARY_PATH="$native_sanitizer_runtime${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=1 \
    build-native-asan/native_files_test
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

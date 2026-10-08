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

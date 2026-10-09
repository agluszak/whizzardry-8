# Shared native-port generalization audit — October 9, 2026

This batch generalizes the confirmed serialization, alignment, width, lifetime
and conversion defects beyond the earlier allocation-size pass. The persistent
work queue is `upstream-generalization-todo.md`. This is a reviewed batch with
whole-tree inventories, not an exhausted semantic or memory-safety audit.

## Reusable evidence

`tools/audit_native_patterns.py` scans all **763 tracked C/C++ files**, including
inactive branches, headers, excluded extensions, tests and vendor code. The
current inventory contains 1,682 raw-IO leads, 1,870 typed pointer casts, 1,651
lifetime/copy leads, 1,525 integer casts, 2,653 conversion leads, 7,088 boundary
leads, 89 remaining `long` occurrences and 11 ordering calls. These categories
overlap and include definitions, safe uses and external APIs. Counts are not
defect counts or proof that every producer/consumer has been reviewed.

`tools/audit_native_layouts.py` uses the installed LLVM Python bindings and the
actual native/Windows compilation databases. It records compiler sizes,
field offsets, raw-IO argument types and packed-member address/array candidates.
Failed translation units are explicit and cause failure. Its packed candidates
include direct indexed access that Clang already handles correctly; they do not
by themselves prove an unsafe pointer escape. Nested packing and casts still
require source review. Compiled facts cover the selected configuration only.

The fresh inventories cover **329 native and 315 Windows translation units**
with zero errors. Native facts include 1,276 records, 1,674 IO/ordering calls
and 2,183 packed candidates; Windows facts include 1,287 records, 1,630 calls
and 2,210 packed candidates. Concurrent Windows-driver parsing stalled inside
libclang on this host; the tool now uses serial indexes for that lane, which
completed successfully. Native parsing remains parallel.

`tools/compare_native_layouts.py` compares the protected raw-IO list in
`tools/native_codemod_rules.json`: **70 records/prefixes agree** in extent and
field offsets/widths between both lanes, with no missing or conflicting layouts.
It compares multiple compiled variants rather than silently choosing one.
This proves the protected list, not that every serialized record is on it.

Reproduce the compiler inventory with existing tools; no installation is needed:

```sh
ninja -C build-native -t compdb > /tmp/wiz8-native-compdb.json
ninja -C build-clang/legacy-check -t compdb > /tmp/wiz8-windows-compdb.json
python3 tools/audit_native_patterns.py --json > build-native/native-pattern-inventory.json
python3 tools/audit_native_layouts.py /tmp/wiz8-native-compdb.json \
  --llvm /home/linuxbrew/.linuxbrew/opt/llvm --jobs 4 > build-native/layout-final-native.json
python3 tools/audit_native_layouts.py /tmp/wiz8-windows-compdb.json \
  --llvm /home/linuxbrew/.linuxbrew/opt/llvm --jobs 1 > build-native/layout-final-windows.json
python3 tools/compare_native_layouts.py build-native/layout-final-native.json \
  build-native/layout-final-windows.json > build-native/raw-layout-comparison.json
```

## Confirmed fixes

### Serialized pointer words

A saved nonzero word is a payload-presence marker, not a live handle. Previously
an unmapped retail address appeared null through W8Ptr32, so NPCT character or
NSF string payloads were skipped and subsequent reads lost alignment. Conversely,
a disk word can equal an existing runtime handle and resolve to an unrelated
object. W8SerializedPointerPresent inspects the four stored bytes without
consulting the handle table. The corrected comment documents this distinction.

LoadItem uses the helper for its next-item marker. LoadNpcStates captures the
character marker and clears the slot before constructing a replacement. NSF
loading does the same for file names, quote string tables and sub-entry text;
unused entries/sub-entry slots and empty table strings are reset to null.
Original disk extents and the Windows branches remain unchanged.

The new fixtures exercise NPCT versions 2 and 3, absent/retail-address/live-handle
markers, following-stream alignment, database rebinding, NSF names and strings,
zero-count arrays carrying stale words, and empty strings. They use real loaders
with private binary files and release fixture allocations explicitly. The NSF
fixture also exposed native narrow sprintf("%S") calling libc with two-byte
text. That loader now uses the native bounded UTF16-to-multibyte conversion.
ASCII, odd-address input, counting, truncation and invalid-character handling
are tested; a complete Windows code-page emulation is not claimed.

### Packed scalar access

Taking an address or decaying a packed array into an ordinary scalar pointer
loses the record alignment information. `compat/unaligned.h` introduces scalar
aliases whose native Clang access alignment is one byte; Windows retains the
original scalar types and signatures. They preserve live aliases and do not
change record offsets or stage copies that could lose callback mutations.

Reviewed uses now cover character condition/status arrays in magic, AI and
combat; hate and NPC wanted-item arrays; text-box line counts; triangle/vector,
bounds, octree, mesh and quaternion components; and imported character header
shorts. The native tests exercise odd-address AI conditions, packed triangle
points and the actual generic vertex processor min/max implementation.
Bit-pattern mesh casts retain the existing no-strict-aliasing build contract.
Remaining packed candidates still need classification.

### Extension widths and ordering strides

Authored JPEG and unzip logical MSVC `long`/`unsigned long` storage, counters
and signatures now use w8_long/w8_ulong. Windows aliases preserve the old types.
The JPEG four-component loop previously used native unsigned long, loading and
storing eight bytes for four-byte pixels. It now copies exactly one four-byte
word through memcpy, preserving the recovered byte permutation without an
alignment assumption. The test links the production importer/transfer loop and
checks one-, three- and four-component rows with controlled codec adapters.
It is not an end-to-end JPEG decoder test.

Runtime plugin size assertions now use W8_ABI_ASSERT; pointer-bearing runtime
classes may grow natively. Four JPEG units compile with the pinned IJG headers
in both lanes; the Windows unzip plugin compiles too. Native unzip decoding is
not implemented or validated by this batch.

All 11 qsort/bsearch calls were reviewed. Three remaining literal widths in
character generation and realm-skill sorting now derive from the array element.
Other sites already used the correct typed size. No comparator or ordering
semantics were rewritten.

All 89 remaining long tokens were classified: compatibility typedefs, intentional
long long, POSIX time/sysconf types, printf argument casts, external Info-ZIP
callbacks, zlib types and miniaudio OS/library/intrinsic declarations. Repeated
long long tokens inflate the count. They retain the type owned by their host
or external ABI; this does not validate every vendor implementation or narrowing
cast in the separate integer-cast queue.

### Allocation/deallocation and conversion

Texture-file names and two octree candidate buffers allocated with raw operator
new now use operator delete natively. Level-up queued text and its integer
payload now use typed new[]/new, matching their existing delete[]/delete
consumers. Windows allocation/deallocation behavior remains unchanged.
Explicit raw operator-new families were traced through allocation and release;
this does not exhaust malloc-backed object lifetime or arbitrary owning copies.

Native srFloatToInt now applies the current rounding mode, checks the signed
32-bit result range and produces integer-indefinite with FE_INVALID for NaN,
infinities and overflow. LP64 lrint previously produced a 64-bit result that
could narrow to zero. Tests compare against actual x87 FISTP for all four
rounding modes, finite boundaries, 4-GiB overflow and non-finite values. Other
floating exception flags, trap modes and all cast sites remain separate work.

## Exceptions retained deliberately

| Family | Reason |
| --- | --- |
| W8Ptr32 and fixed raw records | Wire pointer slots remain four bytes. Live native pointers require explicit rebinding; widening disk fields would corrupt saves/databases. |
| W8LevelFileMonster | Only the 0x22-byte prefix is serialized. The MonPath pointer tail is runtime-only and may grow from four to eight bytes; its offset is asserted. |
| NSF sub-entry reads of eight bytes | The recovered wire record is eight bytes; this is a format extent, not an allocation stride. |
| Direct packed field/index access | Clang retains packing at the expression. An ordinary escaped scalar pointer is the separate issue. |
| Packed UTF16 through native CRT | Existing helpers read/write via memcpy. Other direct consumers/callbacks still need review. |
| Ordinary srVector4 and srVector3i pointers | These types have ordinary four-byte alignment; srVector3T is a distinct packed type. They were not converted merely because another vector is packed. |
| Info-ZIP callbacks and windll_subset.c | External library declarations/producers own that ABI; unsigned long callback types were retained. Native adaptation needs its own backend evidence. |
| Remaining host-long printf casts, system/vendor declarations and long long | These describe host variadic/API types or intentional 64-bit storage, not recovered MSVC dword fields. Pointer/integer narrowing leads remain queued. |
| Raw POD storage and allocator forwarding | Matching operator new/delete or malloc/free is valid for reviewed families; a spelling alone does not establish an owner defect. |
| SDK owning copies/registry behavior | No blanket copy rewrite without recovered callers. Existing explicit game clones and reviewed light/allocator tests remain the evidence boundary. |
| Byte pitches, compressed extents and alignment padding | Format/allocator contracts remain bytes; see allocation-size-audit.md for the earlier complete allocation-width classification. |

## Validation and remaining work

- Native build and **14/14 tests pass**; game/SGP probe **246/246 clean**.
- Imports, UTF16 CRT, save-record and JPEG-transfer tests pass **ASan/UBSan with
  leak detection**, four tests total. Six old-implementation negative controls
  fail on the defects: float conversion, NPC markers, NSF markers, AI condition
  alignment, vector alignment and JPEG word width. Early-exit negative runs
  disable leak detection; the passing tests enable it.
- Legacy clang-cl build passes. **299 main non-zlib objects have zero differences**
  in instructions, relocations and .rdata against the existing 228fa4c baseline
  with SOURCE_DATE_EPOCH=1791503644. Five extension objects also agree after
  normalizing only the anonymous-namespace filename hash introduced by compiling
  the baseline codec adapter from its mirrored path.
- The native world harness exits successfully: **38 enabled meshes and 3,394
  submitted polygons**, 160/35 draw calls, 1,525/64 triangles and 131,747 changed /
  130,374 restored viewport pixels. This is controlled Xvfb/lavapipe rendering,
  not evidence of physical input or macOS behavior. Counts can vary by frame.

Remaining work is explicit in the todo. Known next issues include native narrow
printf-family wide arguments in save paths/diagnostics, the recovered merged
text temporary leak, full raw-record identification and error cleanup, remaining
packed callbacks, owning copies, malloc object lifetime, sentinel/null indexing
and complete shutdown. Existing world lifetime leaks are not suppressed or
claimed fixed. VC6/macOS, all debug configurations, real JPEG decoding, native
unzip and every affected gameplay path were not separately exercised.

Evidence is in ignored build directories under the generalization-* and
layout-final-* names. Generated inventories, binaries and retail assets are
not committed. Shared fixes/tools and this record belong before upstream-tip;
native CRT implementation, regression harnesses and CMake targets follow it.

# Allocation element-size audit — October 9, 2026

The review covers all **760 tracked C/C++ files**, including headers, inactive
platform branches, SGP debug code, both extension plug-ins, tests and the
vendored miniaudio header. It replaces **99 allocation expressions in 37
production files** with sizes derived from the allocated type, and updates
associated header offsets, copies and clears. Existing element counts, spare
slots, growth budgets and serialized byte widths are preserved.

## Method and coverage

Run the inventory from any directory:

```sh
python3 tools/audit_allocations.py --json > allocation-inventory.json
python3 tools/audit_allocations.py --without-sizeof > allocation-review.txt
```

The inventory strips comments and string literals while preserving locations,
then balances parentheses to include complete multiline expressions. It finds
**729 allocation references** in this tree: 35 in headers, 553 in production
sources, two in the new native regression, and 139 in miniaudio. These counts
include declarations, definitions and forwarding macros; they are not counts
of independent allocations. Both `sizeof` and non-`sizeof` expressions were
reviewed. Searches also covered allocator aliases, ordinary typed `new`, byte
array `new`, placement construction and stack allocation; no `alloca` sites
were found in first-party sources.

Reviewed forms include `malloc`, `calloc`, `realloc`, debug CRT variants,
`MemAlloc`/`MemRealloc`, `srHeap.allocate`, allocator/recycler forwarding,
explicit `operator new`, and miniaudio allocation wrappers. The JPEG adapter's
`alloc_sarray` delegates element sizing to IJG. Ordinary typed `new` already
uses the compiler's element size; placement construction uses typed backing
storage. Variable byte counts were traced to their producers instead of being
treated as safe merely because the call contains no literal multiplier.

The script is a review inventory, not an AST type checker or a proof of memory
safety. It deliberately retains declarations and generic forwarding calls.
This audit concerns allocation element/header widths; allocation failure,
integer overflow, ownership and arbitrary indexing require separate review.

## Four remaining native-width defects

| Allocation | Previous assumption | Native size | Correction |
| --- | ---: | ---: | --- |
| `srMemoryAllocator::Block` | 32 bytes | 48 bytes | Size reservation, aligned payload placement and name offset use `sizeof(Block)`. |
| `W8MonsterSpellIcon` | 8 bytes | 16 bytes | Allocate the complete icon/pointer record. |
| `W8LevelFileTrigger` in `ReadPropsFile` | 6 bytes | 10 bytes | Allocate the packed record including its native `pData` pointer. |
| `W8MonsterCombatState` | 339 bytes | 343 bytes | Allocate and clear the complete record including its native action-list pointer. |

The three game-record sizes were measured with the native compiler and its
actual short-wchar/packing flags. `W8MonsterAction`, `W8CombatSlot` and
`W8EffectSlot` remain 48, 32 and 17 bytes respectively; their fixed-width
serialized pointer slots must not be widened.

The allocator regression failed on the old code's 32-byte payload alignment
before the fix. It now checks count/size overloads, several payload lengths,
alignment, payload/name separation, reported sizes and unlinking a non-head
block before freeing the head. The early-return failure's leak report was a
consequence of stopping before cleanup; the corrected regression is leak-free.

## Preserved byte counts and multiplicities

| Retained form or allocation family | Reason |
| --- | --- |
| `char`/`unsigned char` strings, flags, names and compressed buffers | Element size is one byte; lengths and capacity constants are byte counts. |
| `materials.cpp` name banks using `count << 9` | Each material/texture name owns a 512-byte slot. |
| `Strings.cpp` `malloc(byte_count)` | The file supplies a UTF16 byte length, consumed unchanged by `FileRead`; native wchar remains two bytes. |
| `BitArray::Load` `operator new(packed_size)` | File-supplied compressed bytes; decoded words now use their element size. |
| `npc_script_file.cpp` `malloc(block_size)` | Producer already multiplies the entry count by `sizeof(*record->entries)`. |
| `GDFileIO.cpp` environment bank `malloc(size)` | Producer already uses `(m_iNumEnvirons + 10) * sizeof(*m_ppEnvirons)`. |
| `LibraryDataBase.cpp` `MemAlloc(size)` / `MemRealloc(..., uiSize)` | Producers already use the corresponding library/open-file record size. |
| `Container.cpp` variable-size allocations and reallocations | Header `sizeof` plus caller-supplied element bytes; growth preserves that accounting. |
| `srArray`, pooled entries and texture chunks | Typed `sizeof` backing allocations or ordinary compiler-sized `new`; forwarding calls take counts or bytes. |
| `srColorSurface` / GERD pixel buffers and recycler allocations | Byte pitch, dimensions, mip format or pixel-size producers determine the extent. |
| STCI compressed pixels, palette bytes and application data | Stored byte counts and format widths are file contracts. The ETRLE record allocation now uses its element size while reads retain `STCI_SUBIMAGE_SIZE`. |
| PCX/TGA, JPEG decoded rows, FFmpeg IO and audio sample buffers | Format-defined pixels, sample bytes or explicit byte capacities. |
| Debug allocation wrappers and audio allocation callbacks | Forward caller-supplied byte counts; debug strings are byte buffers. |
| Native `srHeap`'s 16-byte prefix | Alignment reservation containing a `size_t`, not a 16-byte typed element. |
| Legacy pooled heap's alignment/tag padding | Retail byte offsets and 32-byte alignment remain in the Windows-only implementation; the actual block-header allocation now uses `sizeof(Block)`. |
| Vendored miniaudio | Typed objects/banks use `sizeof`, including the ALSA device-ID and custom decoder-vtable banks whose sizes come through variables; other buffers use format/API-supplied byte counts. No vendor edits. |

The UV dedup pool keeps **four entries per polygon** rather than mistaking its
48-byte budget for one 12-byte entry. Mesh sorting keeps three scalar slots per
polygon, region streams keep 40 ushort slots per leaf, and shadow geometry
keeps two index triples and six positions. Extra one/two/five/ten-element
reservations remain present. Associated typed copies/clears use the same
element size; raw file reads/writes retain their original widths.

After this pass, no unreviewed hardcoded first-party typed allocation width
remains in the inventory. Literal alignment, byte-buffer and format budgets
remain intentionally; a blanket rule requiring `sizeof` on every allocation
would conflate those with element widths.

## Validation boundary

- Native CMake build and **12/12 native tests** pass.
- Allocator/client and UTF16 CRT tests pass **ASan/UBSan with leak detection**.
- Native syntax probe covers all 246 selected game/SGP translation units with
  zero errors; four legacy platform units are replaced by native backends.
- Legacy clang-cl build passes. **299 non-zlib objects have zero differences**
  in instructions, relocations and `.rdata` against the existing `228fa4c`
  baseline with `SOURCE_DATE_EPOCH=1791503644`.
- VC6, macOS, every debug configuration and the three affected gameplay paths
  were not exercised separately. The inventory includes those source branches;
  source coverage does not imply runtime coverage.

Local build evidence is in `build-native/allocation-*`,
`build-native-asan/allocation-*`, and `build-clang/allocation-*`; these generated
artifacts and installed retail resources are not committed.

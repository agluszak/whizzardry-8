# Upstream generalization work

The shared changes precede `upstream-tip`; native implementations and tests
follow it. Preserve retail layouts, serialization widths, ownership and control
flow. Use compiler/source evidence to classify candidates; inventory counts do
not establish correctness. Keep reviewed exceptions with their reasons.

- [x] Typed allocation element/header widths: 99 expressions corrected;
  see `allocation-size-audit.md`.
- [x] Build reusable inventories of serialization calls, compiler record layouts,
  packed-field pointer escapes, allocation/deallocation families, scalar/pointer
  widths, copying and sentinel/conversion boundaries.
- [ ] Serialization: review raw IO records and their nested fields; preserve
  wire sizes, classify saved pointer slots and chain markers, and check every
  producer/consumer of changed records.
- [ ] Packed access: review escaped member/array pointers and typed byte-buffer
  loads/stores in game, SGP, SurRender and extensions; preserve retail offsets.
- [ ] Lifetime: review raw storage construction/destruction and allocator pairs;
  inspect copying of registry/scene/resource owners against recovered behavior.
- [ ] Widths: audit remaining `long`, pointer narrowing, callback signatures and
  logical element strides such as `qsort`/`bsearch`, including excluded modules.
- [ ] Boundaries: review null cursor arithmetic, sentinel-backed indexes,
  non-finite/out-of-range conversions and shutdown sequencing by source family.
- [x] Validate this batch with native builds, meaningful focused sanitizer
  checks and the existing Windows object oracle. Record untested paths.
- [ ] Restack shared fixes first, synchronize both handoffs, and push the code
  and completed audit record.

## Current work

Completed fixes and reviewed exceptions are recorded in
`upstream-generalization-audit.md`. The broad category checkboxes above remain
open: a source inventory is not an exhausted semantic audit.

- [x] Compare the 70 protected raw-IO records/prefixes in both compiler lanes.
- [x] Correct NPCT character and NSF name/subquote/text presence markers,
  including saved words that collide with live handle IDs; clear unused slots.
- [x] Preserve packed scalar access alignment for reviewed condition, combat,
  vector, bounds, mesh, quaternion, text-count and import-header pointers.
- [x] Repair the JPEG four-byte pixel loop and reviewed extension long widths.
- [x] Classify all 89 remaining long tokens: compatibility aliases, host APIs,
  external library/intrinsic declarations, printf arguments and long long.
- [x] Review all 11 qsort/bsearch sites; replace three literal element widths.
- [x] Repair reviewed raw operator-new release pairs and level-up payload
  construction; retain existing registry ownership behavior.
- [x] Match signed-dword FISTP results/invalid status for all four rounding modes.
- [x] Use an alignment-safe UTF16 conversion for NSF subquote strings.
- [ ] Review narrow printf-family `%S` / `%ls` users, especially save paths in
  OptionsScreen.cpp and diagnostic FormatString/FormatDebugMessage callers.
  They still pass two-byte wchar text to host libc. The wide printf family
  already uses the native CRT implementation.
- [ ] Review remaining packed scalar/wide-string escapes through callbacks and
  local consumers; compiler candidates include safe direct indexed accesses.
- [ ] Identify additional serialized types outside the protected list, then
  follow every nested pointer producer/consumer and successful/error cleanup.
- [ ] Review malloc-backed object lifetimes and owning copy operations against
  recovered callers. Do not blanket-rewrite public SDK copy behavior.
- [ ] Resolve the recovered AppendToLastTextLine temporary allocation leak:
  ShowNotice copies the merged string, but the temporary is never released.
- [ ] Review sentinel/null cursor/index families and full shutdown sequencing.
- [ ] Exercise native unzip decoding, real JPEG decoding, additional saves,
  VC6/macOS and relevant debug paths before claiming coverage of those paths.

No packages were installed. Ask the user to install any future missing package.

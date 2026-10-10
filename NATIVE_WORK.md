# Remaining native work

Only serialized game formats are compatibility contracts; runtime classes
do not need the original compiler layout.

- Replace the file/event/timer facades in `include/wiz8/compat/` and
  `src/compat/` with native filesystem operations, SDL events and a game loop.
  Preserve case-insensitive asset lookup, SLF access and the writable overlay.
- Separate save/asset records from runtime objects, then replace `W8_PTR32`
  and its process-wide pointer table with ordinary pointers and containers.
  Keep explicit fixed-width disk fields and round-trip fixtures.
- Replace two-byte `wchar_t`/CRT substitution with explicit asset text decoding
  and native strings. Remove `-fshort-wchar` only after migrating callers and
  testing format boundaries, including existing `fgetws`/`swscanf` game calls.
- Remove `-fwrapv` and `-fno-strict-aliasing` after correcting the arithmetic
  and aliasing assumptions they currently protect.
- Modernize remaining SurRender/SGP allocator, string, array and threading
  abstractions; remove unused layout assertions, packing and ABI annotations
  once runtime and serialized records are separate.
- Make JPEG import an explicit native codec boundary. Its retained plug-in
  source is not currently linked into the game; `native_jpeg_transfer` exercises
  pixel transfer with controlled decoded rows, not codec loading.

The CTest fixtures remain independent of installed assets. The separate
graphics/world harnesses still require a display, a usable GPU driver and
an installed game. Passing CTest alone does not establish complete gameplay.

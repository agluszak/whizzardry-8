# Remaining native work

Only serialized game formats are compatibility contracts; runtime classes
do not need the original compiler layout.

- Replace the file/event/timer facades in `include/wiz8/compat/` and
  `src/compat/` with native filesystem operations, SDL events and a game loop.
  Preserve case-insensitive asset lookup, SLF access and the writable overlay.
- Separate save/asset records from runtime objects, then replace `W8_PTR32`
  and its process-wide pointer table with ordinary pointers and containers.
  Keep explicit fixed-width disk fields and round-trip fixtures.
- Replace the remaining recovered UTF-8 C buffers with owning `std::string`
  and borrowed `std::string_view` as runtime records are further separated.
  UTF-16LE text decoding and record encoding are already explicit; the
  short-wchar build mode and wide CRT substitutions have been removed.
- Remove `-fwrapv` and `-fno-strict-aliasing` after correcting the arithmetic
  and aliasing assumptions they currently protect.
- Modernize remaining SurRender/SGP allocator, string, array and threading
  abstractions; remove unused layout assertions, packing and ABI annotations
  once runtime and serialized records are separate.

The CTest fixtures remain independent of installed assets. The separate
graphics/world harnesses still require a display, a usable GPU driver and
an installed game. Passing CTest alone does not establish complete gameplay.

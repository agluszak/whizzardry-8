# Remaining native work

Only serialized game formats are compatibility contracts; runtime classes
do not need the original compiler layout.

- Replace the file/event/timer facades in `include/wiz8/compat/` and
  `src/compat/` with native filesystem operations, SDL events and a game loop.
  Preserve case-insensitive asset lookup, SLF access and the writable overlay.
- Separate save/asset records from runtime objects, then replace `W8_PTR32`
  and its process-wide pointer table with ordinary pointers and containers.
  Keep explicit fixed-width disk fields and round-trip fixtures.
- Continue separating the remaining numeric save records and engine resource
  identifiers from their runtime representations. Game text and editable
  controls now own UTF-8 strings; retail UTF-16LE records have independent
  layouts and explicit codecs. Native text input and grapheme traversal are
  supported, while font coverage still depends on the installed retail fonts.
- Remove `-fwrapv` and `-fno-strict-aliasing` after correcting the arithmetic
  and aliasing assumptions they currently protect.
- Modernize remaining SurRender/SGP allocator, string, array and threading
  abstractions; remove unused layout assertions, packing and ABI annotations
  once runtime and serialized records are separate.

CTest includes both asset-independent fixtures and licensed runtime scenarios.
The runtime label covers video, character creation, game start, world movement,
quick-save, combat, save-fixture creation and save loading. These scenarios need
WIZ8_ASSET_ROOT and a usable SDL GPU backend; CI provisions the licensed assets.
The separate graphics/world harnesses additionally require an interactive display.

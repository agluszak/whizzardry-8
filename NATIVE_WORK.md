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

The CTest fixtures remain independent of installed assets. The separate
graphics/world harnesses still require a display, a usable GPU driver and
an installed game. Passing CTest alone does not establish complete gameplay.

# CPU graphics classification

SDL surfaces are the common CPU pixel representation. `CpuSurface` owns SGP
frame buffers; `HVOBJECT::sprites` is a single vector of `SpriteFrame`
decoded once at `CreateVideoObject` — each owns its INDEX8 `SDL_Surface`,
dimensions, offsets, and (only for frames whose opaque runs contain a
literal index 0) an explicit per-pixel opacity mask, so transparency is
never ambiguous. The compressed bytes are never retained; decoding is
bounded strictly by each frame's recorded `uiDataOffset`/`uiDataLength`.

Every sprite blitter is the same mask-aware `NativeBltSprite` walker with
an operation-specific opaque/transparent callback: 8/16-bit transparent
and shadow draws, mono-shadow, clipped variants, and `TransMirror` (a
negative source step, no mirrored copy). The old `NativeBltETRLE`/
`NativeBltETRLEClip` streaming walkers, the mirror cache and the
`SrcPtr`/`uiDataOffset` plumbing are gone — one representation, one
implementation per operation.

Simple byte operations stayed simple: `srColorSurface` fills and
horizontal/vertical line runs are element loops, and compatible `blit`/
`copyNoScaling` copies are row-wise `std::memcpy` (self-blits keep the
direction-aware overlap paths). The `himage` 8-to-16 copy paths are plain
`dst = palette[src]` lookups and now write every scanline — the inherited
last-row skip was an off-by-one, not a feature.

Genuinely Wizardry-specific and retained: `ShadeTable`/`IntensityTable`
destination darkening, `pShade8` index remapping, mono-shadow writes,
pixelation/hatch/shadow rectangles, mirrored and forward-copy overlap
semantics in `Blt16BPPTo16BPP`/`Blt8BPPTo8BPP`/`Blt16BPPTo16BPPTrans`/
`Blt16BPPTo16BPPMirror`, exact RGB555/intensity/YUV/indexed conversion in
`pixel_convert.cpp` (rounded LUTs differ from SDL bit replication),
nearest-scaling in `scaleFast` (retail floors `i * ratio`; SDL nearest
samples pixel centers), and the generic `srColorSurfaceIFace` filters,
clamp modes and channel operations.

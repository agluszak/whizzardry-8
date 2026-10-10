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
frame buffers; `HVOBJECT::sprites` holds each ETRLE frame decoded to an
INDEX8 `SDL_Surface` at load, with transparent runs as the index-0 color
key. Ordinary sprite draws (`Transparent`, `TransparentClip`, `TransMirror`,
`Blt8BPPDataSubTo16BPPBuffer`) go through `SDL_BlitSurface`: the shade LUT is
decomposed through an ARGB1555 view so every 16-bit word round-trips
exactly, including bit 15 via palette alpha.

Standard SDL now performs: sprite color-key draws, flat indexed-to-16-bit
LUT blits, `srColorSurface` fills, horizontal/vertical line runs, and
ordinary compatible-surface copies (`blit`, `copyNoScaling`). The
indexed-to-16-bit LUT blit is the shared `BlitIndexedTo16BPP` in
`compat/surfaces.h`, also used by the `himage` 8-to-16 copy paths
(`Copy8BPPImageTo16BPPBuffer`, `Copy8BPPCompressedImageTo16BPPBuffer`,
which decompresses scanlines into an indexed block first).

Genuinely Wizardry-specific and retained: `ShadeTable`/`IntensityTable`
destination darkening, `pShade8` index remapping, mono-shadow writes,
pixelation/hatch/shadow rectangles, mirrored and forward-copy overlap
semantics in `Blt16BPPTo16BPP`/`Blt8BPPTo8BPP`/`Blt16BPPTo16BPPTrans`/
`Blt16BPPTo16BPPMirror`, ETRLE streaming fallbacks for frames with literal
index 0 in opaque runs, the retail last-scanline-skip quirk in the `himage`
copy blitters, exact RGB555/intensity/YUV/indexed conversion in
`pixel_convert.cpp` (rounded LUTs differ from SDL bit replication),
nearest-scaling in `scaleFast` (retail floors `i * ratio`; SDL nearest
samples pixel centers), and the generic `srColorSurfaceIFace` filters,
clamp modes and channel operations.

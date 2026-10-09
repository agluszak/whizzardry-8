# Native gameplay regressions — October 9, 2026

Reported against the working Windows executable. Screenshot evidence:
`/home/agluszak/Pictures/Screenshots/Screenshot_2026-10-09_16-{35-54,36-08,36-19,36-43,36-49}.png`.
Windows parity must be verified where needed; native rendering and startup alone
were insufficient acceptance checks.

- [ ] Sky: missing angular sections when looking up/turning.
- [ ] Terrain: dark triangles and flickering during camera/combat movement.
- [ ] UI: rotating compass and formation widgets show dark wedges. Model bounds
  now traverse actual triangle/vertex records instead of hardcoded float strides;
  a six-vertex, three-axis regression test passes. Visual parity remains unchecked.
- [ ] Saving: options reports "Overwrite failed. File is write-protected."
  Save/delete/overwrite paths now explicitly convert two-byte game text before
  narrow formatting. Ambient names serialize into a padded 128-byte disk record
  instead of reading beyond their variable-length runtime allocation. The actual
  SaveGame/LoadGame routines complete a round trip in a private test directory;
  physical save-menu interaction still needs verification.
- [ ] Combat: crash immediately after killing an enemy (crabs in the screenshots).
  The harness can apply lethal damage to an actual active monster and run combat
  cleanup. Prior sanitizer evidence reaches an unaligned experience reference in
  AwardPartyExperience; the reported crash has not been conclusively reproduced
  or fixed. The unfinished packing/reference experiments are not published.

Reproduce with private user roots and actual game loaders, save/kill routines and
GPU readbacks. `native_world_graphics` accepts a character filename, an absolute
output PPM path, and optional `save`, `kill` or `angles` mode. Fresh publication
checks pass the native build, all 14 CTest tests, and the imports/model-bounds test
under ASan/UBSan with leak detection. The asset harness renders 38 enabled meshes
and 3,394 submitted polygons and successfully saves/reloads `native-test`.

Decomp preserves established retail bugs and the historical Windows ABI.
The filtered preparation patch is [decomp #980](https://github.com/agluszak/wizardry-8-decomp/pull/980);
Whizzardry owns runtime/serialization repairs and the upcoming native-only
cutover. Do not upstream native workarounds just because they were in the old
shared prefix. No host packages are to be installed by the agent.

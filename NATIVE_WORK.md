# Remaining native work

Only serialized game formats are compatibility contracts; runtime classes
do not need the original compiler layout.

- Replace the file/event/timer facades in `include/wiz8/compat/` and
  `src/compat/` with native filesystem operations, SDL events and a game loop.
  Use prepared lowercase assets and explicit asset/user roots; runtime I/O uses ordinary files.
- Save and game-data records that hold live objects now have fixed-width
  disk images and `wiz8::retail::Record` codecs (`retail_save_records.h`,
  `retail_geometry_records.h`); runtime types use ordinary pointers. Next:
  give those runtime types owning members (characters,
  monster group member lists, world item chains) instead of raw pointers
  managed by hand.
- Continue separating the remaining numeric save records and engine resource
  identifiers from their runtime representations. Game text and editable
  controls now own UTF-8 strings; retail UTF-16LE records have independent
  layouts and explicit codecs. Native text input and grapheme traversal are
  supported, while font coverage still depends on the installed retail fonts.
- Remove `-fwrapv` and `-fno-strict-aliasing` after correcting the arithmetic
  and aliasing assumptions they currently protect.
- Rendering now runs on the main thread with renderer-owned batches, mesh
  scratch, fallback resources and GPU texture records. Indexed TGA decoding
  owns its palette; no SurRender core initialization or CPU texture-cache
  budget remains.
- Modernize remaining SurRender/SGP allocator, string, array and threading
  abstractions; remove unused layout assertions, packing and ABI annotations
  once runtime and serialized records are separate.

CTest includes both asset-independent fixtures and licensed runtime scenarios.
The runtime label covers video, character creation, game start, world movement,
quick-save, combat, save-fixture creation and save loading. These scenarios need
WIZ8_ASSET_ROOT and a usable SDL GPU backend; CI provisions the licensed assets.
The separate graphics/world harnesses additionally require an interactive display.

## Runtime layout audit

Runtime engine classes no longer carry explicit alignment fields. The audit
checked named uses, constructors/copies, raw I/O and retail codecs:

| Fields | Classification and disposition |
| --- | --- |
| Monster-group `unknown_2b`, `unknown_d4`; spell `unknown_040`, `unknown_055`, `unknown_11b`, `unknown_125`, `unknown_145` | Serialization only. Retained in retail opaque storage, with explicit field codecs; new groups still write 3 at 0x2b. |
| Environment `pad_06`; global-status `padding_242f`, `padding_49be` | Serialization only. Retained in retail opaque storage, including nonzero values from loaded records. |
| Model, mesh, light, camera, animation, navigator, monster, missile, prop, ambient-sound, emitter, octree/pathing/build-workspace padding | Unused by gameplay and serialization. Deleted from native types. This includes `stModelInstance::padding_1a2` and every `W8Octree::m_padding_*`. |
| World padding and `m_unknown_07c`, `m_unknown_0d4`; level-motion `unknown_bc`; spell/missile representation `unknown_0b4` | No consumers; some had initialization-only writes. Deleted along with those writes. |
| Octree `m_unknown_13c`, `m_unknown_27c`, `m_unknown_299`, `m_location_region_matched`; build-tree `unknown_138`, `unknown_13c` and their destination fields; pathing `m_unknown_014`, `m_removed_edge_count`, `path_clearance`; light `m_unknown_238` | Initialization, copying or stores only, with no gameplay or serialization consumer. Deleted, including the unused pathing argument. The octree's own clearance remains used by path building. |
| Spell duration, radius, targeting, power, missile, book and usage fields; octree sun count, region cell/masks and geometry streams | Read by gameplay/building or emitted as identified file data. Retained. |
| OCT header `pad_aa`, `pad_c9`; material-record `padding_105`; screenshot `padding_05` | Actual disk representations, not native object alignment. Retained with their file layouts. Other unknown save/database fields remain intact. |
| Eight `W8LevelInfo` C buffers | Replaced by two owned UTF-8 asset paths. World loading derives bitmap and sibling file paths using filesystem components/extensions and never mutates the caller's filename. |

The opaque storage types live in `retail_opaque_records.h`; their runtime owners
carry them solely to preserve file data, independent of native member offsets.
Regression fixtures cover nonzero opaque bytes, copies/edits, truncated group
records, and level/sky path selection without installed assets.

## Unicode and compiled-level boundary

utf8proc now supplies UTF-8 decoding/encoding as well as categories and grapheme
boundaries; utf8cpp is no longer fetched or linked. Retail UTF-16LE field codecs
remain explicit, including malformed-input replacement and fixed-field limits.

The native application only reads compiled OCT/WPT/PVL geometry. The unreachable
level compiler, mesh and waypoint writers, waypoint editor/visualization and
interactive build-log UI are removed. Shared spatial operations live with the
runtime octree. `W8OctBuildTree` remains the gameplay collision index built from
loaded game data; its queries are exercised independently of the removed editor.
Reader fixtures cover both retail waypoint edge layouts and truncated-input
rollback without invoking a production writer.

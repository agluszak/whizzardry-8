# Remaining native work

Only serialized game formats are compatibility contracts; runtime classes
do not need the original compiler layout.

- Replace the file/event/timer facades in `include/wiz8/compat/` and
  `src/compat/` with native filesystem operations, SDL events and a game loop.
  Use prepared lowercase assets and explicit asset/user roots; runtime I/O uses ordinary files.
- Save and game-data records that hold live objects now have fixed-width
  disk images and `wiz8::retail::Record` codecs (`retail_save_records.h`,
  `retail_geometry_records.h`); runtime types use normal C++ ownership. Monster groups,
  species caches, visibility maps, and world item records own their storage;
  gameplay and spatial registries borrow pointers or IDs. Continue auditing
  character state and remaining transient object lifetimes.
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
boundaries, leaving one Unicode dependency. Retail UTF-16LE field codecs
remain explicit, including malformed-input replacement and fixed-field limits.

The native application only reads compiled OCT/WPT/PVL geometry. The unreachable
level compiler, mesh and waypoint writers, waypoint editor/visualization and
interactive build-log UI are removed. Shared spatial operations live with the
runtime octree. `W8OctBuildTree` remains the gameplay collision index built from
loaded game data; its queries are exercised independently of the removed editor.
Reader fixtures cover both retail waypoint edge layouts and truncated-input
rollback without invoking a production writer.

## Native runtime contracts

All game randomness uses the explicitly seeded MSVC 32-bit sequence with
15-bit draws; C library RNG state and platform-dependent RAND_MAX are gone.
Loot and NPC theft retain their shared-stream reseeding and draw order.
The software UI framebuffer is fixed ARGB1555. Asset/video inputs can still use
other SDL formats, including RGB565, and palette/TGA/STI decoding stays explicit.

SDL_image reads ordinary images from the existing file stream. The duplicate
JPEG parser and whole-file copy are removed; decoded dimensions remain bounded.
The save-space threshold, inert capture/CD modes, octree trace visualization,
inverted-depth branches, debug allocator-pointer checks and unused inverse
civil-time conversion are removed. PrintScreen and the video inspector remain.
A legacy monster-leader sentinel is normalized by its retail record decoder.

SDL GPU can select SPIR-V, Metal or Direct3D 12 shaders. MSL and DXIL are produced
offline with pinned SDL_shadercross tools and embedded at build time. Linux CI checks offline regeneration. Native Metal/D3D12 shader compilation and
gameplay still need verification on those platforms; platform CI is not enabled.

### Offline GPU shaders

SDL GPU selects Vulkan, Metal or Direct3D 12 using embedded SPIR-V, MSL or DXIL.
Normal builds compile GLSL to SPIR-V and embed the checked-in Metal/Direct3D
outputs without linking a shader compiler into the game. Source digests make
CMake reject stale offline outputs after a GLSL edit.

Regenerate on Linux x86-64 using the same Conan build environment as the game
(match the build type to that Conan configuration; this example uses Release):

```sh
cmake -S tools/shadercross -B build/shadercross -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/build/linux/conan/conan_toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang-23 -DCMAKE_CXX_COMPILER=clang++-23
cmake --build build/shadercross --target shadercross --parallel 2
python3 tools/shadercross/generate.py \
  --shadercross build/shadercross/_deps/shadercross-build/shadercross
```

The standalone tool pins [SDL_shadercross](https://github.com/libsdl-org/SDL_shadercross),
SPIRV-Cross and DXC. Linux CI regenerates both formats and compares the outputs.
Linux runtime scenarios exercise Vulkan; native Metal/D3D12 shader compilation
and game rendering still need platform verification.

## Gameplay ownership and runtime assets

Spell records are decoded into temporary owned storage and published only after
validated header and record reads. NPC active/retired registries and searchable
registries own their records; dialogue callbacks retain borrowed pointers, and
replaced NPC states remain alive until the registry is reset. Cycle particle
attachments and monster spell-icon entries are value collections; particle
retirement and world icon removal retain their existing world lifetimes.
World cursor state has a normal C++ owner and no retail-layout padding.
Cursor-node scratch bytes now live in vectors; saved scratch and fixed asset
names are validated at the file boundary. Trigger text, payloads, and events,
prop representations/timers/animations, and ambient schedules have explicit
owners. Monster animations, marker items, lights, and floating models likewise
use owned members; retiring particles cancel borrowed monster callbacks.

World destruction stops activity and drops borrowed indexes before releasing
entities, then geometry and the spatial index, and finally the scene. Loaded
monsters have separate owning storage and an active borrowed update list, so
hiding the cursor or loading an inactive monster does not transfer ownership.
Typed runtime loaders return unique owners, including on partial load failure.
Missile-table records and octree lookup maps are values; gameplay sorting uses
typed standard algorithms. The raw-pointer deletion helpers are gone.


Recompilation-era addressable numeric constants have been replaced with literals
and semantic constexpr parameters. Runtime tuning values remain mutable. The
sprite font uses a fixed compile-time translation table. ASCII asset loaders use
strings and line streams; UTF-16LE credit/input text decoding remains explicit.

The MIPE editor and its generator marker UI are removed. Its activation flag was
only enabled inside the editor; production input only dispatched to it while
already active, and the sole external toggle closed it on screen exit. Gameplay
encounter generation, script-driven table selection, compiled geometry readers,
and serialized generator/cube fields remain supported.

## Authoritative loaded worlds

The primary and sky slots own their worlds. Queries borrow those owners; there
is no separate update registry or published raw slot to synchronize. World
identity and resource paths are immutable. Live level queries use the primary
world; `resume_level` is decoded save data and is populated in save snapshots,
not maintained as a second active-level field. Sky reuse follows resource identity.

Primary replacement stops callbacks before ownership changes. Teardown still
destroys entities before geometry and scenes. Loading failures use the same
ordered retirement without rendering partial worlds. Status-loading source is
an argument, and view invalidation belongs to the world rather than a global latch.

## Gameplay operation boundaries

Equipment replacement writes its completed arrangement before rebuilding hands,
effects and action validity. Spell pickers own an action draft; cancellation
discards it. Committed and remembered choices also own action values; acceptance
copies the completed draft, while a repeat-cast power override changes only that
cast. Target
assessment consumes action contents, while controllers choose the storage and
present failures. Preview and execution share target geometry, with explicit
sight rules and without sharing random power or damage results.

Trade rows retain a source location and expected item contents. Inventory
compaction or changed contents invalidates the selection. Identical stacks are
fungible; no global item identity is added. Purchasing prepares stack merges,
capacity and overflow placement before committing stock and payment.

Selection tints and target membership are replaceable outputs shared by the
scene, radar and combat list. Layout preferences persist while interactions
constrain the effective mode. Advancement policy reads the actual interaction
state and explicit user pause, applying environment and navigator edges in one
place. UI, combat, sight observation and nearby-item clocks retain their separate
cadences. Last-seen positions and remembered actions remain gameplay history.

A world owns its monster records, groups and world items. Individual removal
owns combat, group, visibility, spatial and NPC relationship cleanup before the
roster can erase a record. Member count projects membership; active and observed
counts describe different sets and retain their existing observation cadence.
Navigators use their owning world's indexes, and global movement commands visit
only the active world and persistent navigation. Failed preparation cannot
unregister a matching monster identity from the active world's octree.

Level reconstruction assembles records, representations, NPC bindings and group
links against the unpublished replacement. Entry then starts encounter clocks,
restored death activity, music, dialogue and first-visit effects. This distinction
does not promise allocation-free entry or a universal atomic transition.

The old level snapshot precedes departure. Departure explicitly ends transient
combat, records the session sight stamp and captures script state into session
location variables before releasing runtime resources. Per-level snapshots do
not persist combat scheduling. Whole-game saves capture script/location,
message-countdown and status images without advancing scripts or modifying live
records; successful saving alone resets autosave pacing. Resource destruction
performs no departure gameplay bookkeeping.

## Derived selection and population state

A spell picker is open exactly while its view is owned. Layout, input, targeting,
and advancement consult that owner; there is no separate spell-mode flag to
reset on screen changes. Close releases the owner before recomputing policy.
The draft identifies the spell, so its power class and affordable levels are
queries over the spell database and caster. Availability queries take the spell
explicitly; no temporary selection override is installed. Each list row holds
its spell and availability color together, and the collection supplies its size.
Item targeting likewise constructs an action from the selected item rather than
maintaining another copy of that pointer in a global action-detail block.

Monster-group active counts are queries over membership in the owning population.
Condition changes, control changes, birth, removal and level preparation no longer
maintain a count. Both loaded and newly changed lured monsters are excluded by
the same rule. The retail count is retained as decoded disk data for round trips;
level saves replace it in the output image with the derived value without editing
the live group. Visibility counts remain observations of a visibility pass.

## Encounter ownership and group relationships

Each world owns one encounter state: generators, budget and saved settings move
into the replacement together. Entry arms that world's generators; it no longer
copies settings into session globals. The population limit is derived from the
budget and level rules at generation time.

An active encounter is a participating group with no leader. Traversal takes a
short-lived snapshot from the owning group collections. There is no persistent
active-encounter registry, registration API, or load-time reconstruction pass.
Culling walks a snapshot so removal cannot skip the next candidate. Combat budget
loss is explicit at retirement or leader destruction, and repeated retirement
cannot spend again. Companion linking and successor election do not spend budget.

Formation linking reserves a destination before changing either relationship;
a full formation leaves the source attached. Leader election selects from the
intact formation, including the first ally when every leadership rank is zero,
then publishes the new links before navigation/hostility refresh. It no longer
turns every ally into an independent group and repairs their registrations.

Group IDs resolve directly in the owning population. Runtime traversal and removal
use the owning collections; the synthetic 10,000-biased index domain and its
conversion/checking APIs are removed from combat, targeting, AI and level scripts.
Only displayed combat groups can be selected by a combat-list row; out-of-range
rows return no group.

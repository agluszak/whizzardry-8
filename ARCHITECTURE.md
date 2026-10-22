# Architecture

This file describes the structure as it is now and the contracts code relies on.
Principles are in [AGENTS.md](AGENTS.md); remaining work is in
[ROADMAP.md](ROADMAP.md).

## Ownership

```
Application
├── GameData             loaded once at startup, read-only
└── GameSession          the game in progress, if any
    └── Game             one play-through
        ├── party, status, story, events, rng, combat, activity, ...
        └── Level        declared last, destroyed first
            ├── primary  World
            └── sky      World

ScreenRouter (file-scope in screen_router.cpp)
├── current screen + suspended stack
│   MainGameScreen (its HUD), camp, character screen, ...
└── Pointer
```

- **`Application`** (`application.h`) is the composition root: it owns the loaded
  `GameData` and the `GameSession`, and runs the main loop. The screen router is
  still a file-scope object, not owned by the Application.
- **`GameSession`** (`game.h`) holds the game in progress, the random stream the
  next game continues, and the event presenter. A game begins in party creation
  (`BeginNewGame`) or when a save is loaded (`LoadGame`); either replaces any game in
  progress. It ends when the main menu is entered and at shutdown. Nothing is reset
  field by field: a new game is a new `Game`.
- **`Game`** owns one play-through: the party and status record, the random stream,
  the story (facts, factions, journal, NPC states, location variables), the message
  log, combat, the activity and advancement, the speech queue, the NPC
  conversation, spell cooldowns, the event log, stored characters, the byte images
  of levels the party has left, and the `Level`. Destroying a game does no
  departure bookkeeping; the main game leaves the level before its game ends.
- **`Level`** owns the primary and sky `World`s. Queries borrow those owners, so no
  world outlives its game; replacing a slot hands back the previous owner.
- **`World`** (`world/world.h`) owns a loaded level: the VSG scene, environment,
  spatial index, audio, entities, monster records and groups, world items, the
  encounter state, the level script and the cursor. Its member order is its
  teardown order. `WorldItems` (`world/world_items.h`) owns the item records and
  places them on the ground, on the prop beneath them and in the scene of its own
  world, so a world being loaded never touches the active one.
- **Screens** are built by the router when first requested, with the session or
  game for the screens that need one, and destroyed when discarded.

### Transitional global access

The decompiled code reaches state through global lookups. They are the main
migration target (AGENTS.md, "What we are doing") and must not gain new callers:

| Lookup | Reads |
| :--- | :--- |
| `CurrentGame()`, `GameInProgress()` | the live `GameSession`'s game (throws with none) |
| `GetWorld()`, `GetSecondaryWorld()`, `GetWorldSpatialIndex()`, `GetWorldCollision()`, `GetWorldPathing()` | the current game's level slots and their parts (null with no game) |
| `Data()` | the installed `GameData` |
| `MainGame()`, `MainGameHud()`, `FindScreen<T>()` | a screen on the global router's stack |
| `MakeScriptContext(CurrentGame())` | a `ScriptContext` built from the global game |
| `PresentationRng()` | the presentation random stream |

Tests own their own instances (`ScopedGame`, `ScopedGameData`).

## Layers

Two layers are enforced:

- **`data`** (`include/wiz8/data/`, `src/data/`): content ids, database records,
  the constant profession, race, skill and portrait tables, and `GameData` (string
  table, item, spell, NPC, fact, level, monster and missile databases, encounter
  tables, dialogue keyword lists). `LoadGameData` reads it once; a malformed
  database throws.
- **`rules`** (`include/wiz8/rules/`, `src/rules/`): the character and party model,
  party storage (`W8Party`, formation), item rules, inventory storage rules,
  character derivation, conditions and enchantments (`party_conditions.h`), the
  event log and the `Activity`. Functions that only read databases take
  `const GameData&`; those that change the party, draw random numbers or report
  events take a `RuleContext` (`rule_context.h`), which `Game::rules()` builds.

A layer file includes only its own layer, the layers below it, the standard
library, glm, fmt and three stateless headers (`math.h`, `world/camera_pose.h`,
`retail_opaque_records.h`), and declares no mutable globals. `tools/check_layers.py`
enforces this (the `layer_boundaries` test, and CI's clang-tidy job), and
`wiz8_rules` links without the platform library, SDL or the engine.

## Modules

| Module | Owns |
| :--- | :--- |
| `platform/` | Window, SDL input events, filesystem roots, clocks and timers, the presentation random stream. |
| `canvas/` | CPU drawing on `SDL_Surface&`: images, STI sprite sheets, fonts, shading tables. No GPU knowledge. |
| `audio/` | The sound service, positional `Sound3D`, ambient audio, lip sync. |
| `render/` | VSG scene graph, shared meshes and instance poses, materials, views, Vulkan resources and presentation; publication of the CPU UI canvas. |
| `spatial/` | `LevelSpatialIndex` (compiled octree, collision), `SurfaceIndex`, the navigation mesh and `Navigator`. Queries take explicit segments and filters and return optional typed hits. |
| `world/` | `World`, party camera and motion, environment, encounters, level loading. |
| `entities/` | `Monster`, `Missile`, `SpellVisual`, `Prop`, `WorldItemEntity`, triggers and locks. Each kind is loaded once into a definition that live entities copy. Entities report hits, landings and release frames to their world's `SimulationSink` rather than resolving them. |
| `native/` | Application loop, game events, key bindings, FFmpeg movie playback. |
| `wiz8/` | Gameplay not yet in `rules`, the game and session, saves, screens, dialogs and level scripts. `wiz8/combat/` is the fight. `local_code/`, `local_screens/`, `dialog_code/` and `level_specific_code/` hold the decompiled game code still being migrated. |

## Compatibility boundary

Only serialized formats are compatibility contracts: retail saves must load, and
saves we write must load in retail.

- Save and game-data records are fixed-width disk images with
  `wiz8::retail::Record` codecs (`retail_save_records.h`,
  `retail_geometry_records.h`, `retail_text_records.h`, `save_codec.cpp`). A codec
  copies a field verbatim only when the runtime field has exactly the disk width,
  and throws otherwise. Unknown and padding bytes live in `retail_opaque_records.h`
  and round-trip unchanged.
- Runtime types don't mirror retail layout, though types still embedded in disk
  records keep it until they get records of their own. Wizardry 7 import records
  are local decoded values; only alignment and two story choices remain on
  `Game` until opening fact seeding consumes them.
- Retail UTF-16LE text is decoded to and encoded from UTF-8 explicitly.
- Four-byte words that were process addresses in retail are never trusted as
  addresses: each is a presence word, a framing word, or a dead constant.
- The game reads compiled OCT/WPT/PVL geometry and `.rlk` region links; there is no
  level compiler or editor.
- Retail address annotations (`// FUNCTION: WIZ8 0x…`) sit directly above the
  definition they name.

### Saves

`save_codec.h` holds the save sections as pure functions between bytes and values
(GSTA, TEXT, TVAR, NPCI, NPCT, NPCF, FATA, JRNL, HYPN, CHAR, and a level group's
STAT header). A load decodes every section before it begins the replacement game,
so a malformed section fails the load and leaves the game in progress untouched.
Level groups decode into a `LevelState` without a world, and the replacement
world is built from it. The game keeps the groups of levels the party has left as
in-memory RIFF images (`Game::left_levels`); no working file sits beside the saves.
The configuration file's CNFG chunk has its own codec (`EncodeGameSettings`).

## The game

- **Randomness.** All gameplay draws come from `Game::rng`, retail's MSVC
  `rand()` sequence (`rules/random.h`), in retail order. A session draws one
  sequence from one seed: each game continues the stream of the one before it
  (`GameSession::seed` fixes it for tests). Moving, adding or dropping a draw
  changes gameplay; `tests/native/random_test.cpp` traces the expected sequence.
  Presentation (sounds, particles, animated textures, camera shake, portrait
  flourishes) draws from a separate stream, so frame timing cannot move game
  draws.
- **Activity.** Rules branch on `Game::activity` (place, conversation, scripted
  scene), never on a screen id or UI flag. Combat is `Game::combat`, a
  `std::optional<Combat>` engaged exactly while the party fights.
- **Advancement and stepping.** `Game::advance(elapsed, hold)` (`game_advance.cpp`)
  is the game's one step: it applies the interface's hold, runs speech, and in the
  world runs the simulation in retail order. `Game::time` is the step time that
  deadlines below the step use. Only the main game and the camp step the game.
- **Events.** Rules push typed events onto `Game::events` (notices, display
  changes, combat progress, speech) and never ask for repaints. The session's
  presenter (`PresentGameEvent`) maps each to the active screen. A game without an
  observer keeps events pending, which is how tests inspect them.
- **Combat.** `Combat::turn()` is idle or one actor's turn, stepped once per frame
  once nothing is waited on (camera, animation, sound, missile, script). Actors are
  held by slot or location id and looked up when used. Combat is not saved.
- **Membership.** `W8Party::members` holds an optional member per slot, so nothing
  tells members apart by flags or addresses.
- **Inventory.** Inventory rules (`rules/inventory.h`) address members by slot and items by
  location or as owned values; the game's commands (`wiz8/inventory.h`) apply a
  rule and then its consequences, settling the party once per command.
- **Character derivation.** Every statistic is derived in one place,
  `DeriveCharacterStats`, from evaluated inputs; any change a statistic depends on
  settles the party (`SettleActiveParty`).
- **Conditions.** Inflicting, lifting and aging conditions and death are game
  operations in `live_party.h`; the pure calculations they need are in
  `rules/party_conditions.h`.

## Worlds and levels

- Level reconstruction builds a replacement world completely before it is
  published; entry then starts clocks, music, dialogue and first-visit effects.
- Departure ends combat and captures script state into location variables before
  resources are released. Destruction does no departure bookkeeping.
- Monster and world-item records link to their scene entity through an
  `EntityLink`. An inactive record may have none. Anything that keeps an actor
  past the current call keeps its id and looks it up again when used.
- Group counts, active-monster presence and combat membership are queries over
  ownership, not maintained counters.
- The party's pose is the primary world's `party_camera`. Render cameras derive
  from it each frame; rendering writes no gameplay state.

## Scripts

NPC scripting and level scripts act on a `ScriptContext` (`script_context.h`): the
game's parts a script may touch. Each loaded level owns its `LevelScript`
(`World::script`), so script state never outlives its level; a level with specific
code has a subclass whose members are that state. Trigger callbacks are
`std::function`s bound to the script.

## Rendering

- `WorldScene::root` is the VSG graph. An instance owns its transform and
  drawable subgraph; its model owns shared topology and materials. Poses are
  sampled on the CPU once per frame and shared by every view, picking and
  attachments. The automap is a separate view of the same scene that changes no
  gameplay state.
- Wizardry lighting, fog and material operations run in GLSL shaders compiled to
  SPIR-V. VSG uses Vulkan (MoltenVK on macOS).
- `endFrame` waits for submitted work: graph mutations and object destruction
  happen on the game thread between completed frames.
- The UI is drawn on a fixed ARGB1555 CPU canvas and published to a dynamic VSG
  image.
- Particles and other effects advance in the game step, never in render
  traversal. World animation uses `Game::visual_time`, which stops while the world
  is held; UI animation uses presentation time.

## UI

- **Screens.** `screen.h` declares `Screen` (enter, resume, frame, leave) and the
  router, which owns the current screen and a stack of suspended screens. Requests
  suspend, discard or return; the camp, character and options screens are rebuilt
  on every visit.
- **Input and ownership.** Screens own `UiLayer`s of `HitTarget`s; one dispatcher borrows the
  live layers and owns hover, capture, modal scopes and tooltips. Coordinates are in
  640×480 design space. A `UiTransaction` spans one input step; objects retired
  during it are destroyed when the outermost transaction ends
  (`DestroyAfterUiDispatch`), so nothing is freed while its own callback runs.
- **Controls and painting.** Controls paint into an explicit `SDL_Surface&`.
  `Draw` never commits; `Refresh` commits a dirty rectangle. Screens record which
  parts the next draw repaints. One `Pointer`, owned by the router, holds the cursor
  and the targeting request. The notebook view derives its rows from
  `Game::story.keywords`; information dialogs own separate append-only paragraphs.

## Tests

CTest runs asset-independent tests and licensed runtime scenarios (label
`runtime`), which need `WIZ8_ASSET_ROOT` and a usable Vulkan backend; CI provides
both. Scenarios click controls through `GameHarness::click`, which first checks a
player could: the control is shown, enabled and reachable under the pointer.

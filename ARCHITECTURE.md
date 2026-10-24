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
  `GameData` and the `GameSession`, and runs the main loop. It loads immutable data
  before constructing the session; the session and its games borrow that data.
  The screen router is still a file-scope object, not owned by the Application.
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
  departure bookkeeping; explicit session and level transitions leave the level before
  releasing it.
- **`Level`** owns the primary and sky `World`s. Queries borrow those owners, so no
  world outlives its game; replacing a slot hands back the previous owner.
- **`World`** (`world/world.h`) owns a loaded level: the VSG scene, environment,
  spatial index, audio, entities, monster records and groups, world items, the
  encounter state, the level script and the cursor. Its member order is its
  teardown order.
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

## Design contracts

The ownership tree describes the current code; the contracts here are the direction
for migrations. They are not permission to rewrite retail behavior. Check the decomp
when a lifetime, ordering or apparent duplication is unclear. Migrate complete
operations and delete their old path, rather than adding a parallel architecture.

### Authority and lifetime

| Authority | State and responsibility |
| :--- | :--- |
| `Application` / `GameSession` | Devices, immutable content and screens; installing and ending a game; the RNG stream that continues across games. |
| `Game` | One play-through, its clocks and gameplay RNG; explicit operations that coordinate party, story, world and combat. Not a forwarding API for every rule. |
| `Party` / characters / inventory | Active characters, their conditions and equipment, shared storage and held item, formation and accepted action choices. Transfers preserve any unplaced remainder. |
| `Story` / NPC state | Facts, factions, journal, persistent NPC identities, stock, history and retained character snapshots. |
| `Level` / `World` | Primary and independently reusable sky worlds; loaded monster records and runtime, groups, encounters, world items, triggers, navigation, scripts and their local lifetimes. |
| `Combat` | The **party's** fight: rounds, turn order, current action execution and waits. Monsters may have engagement state even with no party combat. |
| Ongoing sequences | Conversation/script continuation, voice holds, travelling missiles and continuing casts; each lasts as long as the activity or level that sustains it, not merely the screen showing it. |
| Screens / panels | UI controls, hover, selection drafts, modal and suspension state, painting and playback. They request gameplay operations and present results; they do not own live inventory items or monsters. |
| Codecs | Conversion between live values and retail disk records. Not a second live game model. |

Some current fields do not yet live at these boundaries (for example,
layout-specific dialogue requests and current-game dependencies in level preparation). Moving one must also
move its actual lifecycle and consumers; a new container or `Game&` wrapper alone
does not satisfy this contract.

- **One authority per current fact, not one object per concept.** A recruited NPC's
  active party character and the NPC's retained snapshot are distinct; the snapshot
  has defined copy-back/save behavior. A UI draft, accepted action and executing
  action are likewise different stages, not redundant mirrors. Never keep parallel
  mutable representations that require routine synchronization.
- **Ownership is not membership.** A world owns its monster records, and each record
  exclusively owns its optional loaded runtime. The world cursor owns its own
  animation body; it is not a gameplay monster. Groups, combat,
  indexes and VSG scene attachments reference them. A monster record may exist
  without a body, a loaded body may be inactive, and death may leave a fading body.
  The existing navigator index borrows loaded bodies in construction order; there
  is no second owning body collection or attach/detach protocol.
  World items similarly own their optional loaded appearance: its presence is
  activation, with no separate active flag, body owner or active-item counter.
  Item animation updates attached instances instead of detaching and rebuilding
  scene membership each tick.
  Continuing casts belong to their gameplay world, alongside their borrowed
  missiles and visuals. Missile completion and destruction use that original
  world, even after another world or session has become current. Saved casts
  remain decoded values until installation into the destination; normal entry
  clears entry-created casts at its established point, while restore rebuilds
  the saved lure's visuals without moving its semantic anchor. Combat exit does
  not end these world-owned casts.
  Neither scene attachment nor combat participation decides destruction. Deferred
  work identifies its original actor/session, never whichever one is current later.
- **Borrow according to lifetime.** A live actor can retain its `World&`; a party
  character, saved item or persistent NPC cannot, because it crosses level changes.
  Constructors retain dependencies that define a continuing object's identity or
  lifetime. Changing inputs go on the operation; no general `Game&` service locator
  as a substitute for a real boundary.

### Operations and observation

- **Local operation:** the state owner performs a mutation and restores its
  invariant before returning. Do not expose an update protocol that callers must
  finish (repair equipment, reindex entities, recalculate derived stats, etc.).
- **Cross-owner operation:** one readable game-level coordinator calls the
  necessary owners in a deliberate order (death, recruitment, combat entry/exit,
  travel, save preparation). `Game` coordinates; it does not reimplement their
  internal rules. Do not add a trivial forwarding method for an already complete
  local operation.
- **Calculations and queries:** stateless formulas can be ordinary free functions
  over explicit inputs; not everything needs a class. Queries do not mutate game
  state, consume gameplay RNG or issue presentation work. A class earns its
  existence by owning state, a resource, an invariant or a continuing operation.
- **Commands return what actually happened.** Some retail operations succeed
  partially (stack merging) or have effects even when final placement fails
  (identification). Do not impose transactional/all-or-nothing semantics unless
  gameplay requires them. Preserve remainders and distinguish refusal from
  completion.
- **Required consequences are explicit.** Damage/death, item acquisition, NPC
  dismissal and combat changes must not depend on presentation subscribers being
  installed. Observers report an outcome after the operation's relevant
  invariants are settled; they must not re-enter half-mutated gameplay.
- **Preserve simulation milestones and waits.** Missile hits, animation release
  frames and script completions can affect the same simulation step and its RNG
  order. Keep their synchronous/ordered effects where required; do not blindly
  defer all events to the end of a frame. Combat owns its decision to wait, while
  playback owns actual audio/animation progress and reports completion to the
  originating operation. Polling a read-only readiness query is acceptable when
  it is simpler than subscriptions or cached busy counts. Monster update traversal
  retains removed records until its script callbacks return. Removal immediately
  retires targeting, navigation and scene relationships; it cannot reclaim the
  actor whose update is on the stack. Newly spawned actors enter the next pass,
  and removing another actor cannot skip surviving actors in the current pass.
- **Presentation is an output, not an authority over game state.** Render and
  paint do not mutate gameplay or draw gameplay RNG. Picking returns a semantic
  selection for the interaction owner to interpret. UI notifications may still
  be delivered synchronously in unmigrated flows; change their delivery only
  with the complete flow that depends on them. No universal event bus, ECS
  mirror or generic command-replay framework is needed.

### Transitions and saves

- **Name the transition:** suspend/resume a screen, cancel an interaction,
  finish a cast, leave a level, end a game, and replace a session have different
  semantics. Screen destruction releases presentation; gameplay departure and
  pending-interaction cancellation run through explicit operations before release.
  A suspended lock/trap attempt must not be mistaken for an abandoned one.
- **Prepare, then publish:** decode/validate saved values and prepare a candidate
  before installing it where possible. Entry, script callbacks and first-visit
  consequences happen at the correct activation point, in retail RNG order.
  Loading failure must not leave a half-published world. Do not add general
  rollback machinery merely to compensate for constructing into the live world.
- **Prepare, capture, encode:** save preparation may have gameplay consequences
  (the decomp settles pending monster removals); make these explicit. Capture
  reads the authoritative state; codecs encode values without invoking gameplay.
  World-local continuing effects are cleared on ordinary entry or reconstructed
  from a save according to their actual lifetime. Combat exit alone does not
  necessarily end a cast.

The transition contracts below constrain later ownership moves:

| Transition | Behavior to preserve |
| --- | --- |
| A monster engages before party combat | World-owned engagement/readiness exists without a party `Combat`. |
| The last group member dies | Group participation can end while the monster's death, script and fade work remains alive. |
| Recruit, edit, save, dismiss | The active party character, retained NPC snapshot and world embodiment are distinct; save and copy-back retain their respective semantics and identities. |
| Lure, combat exit, travel or restore | Combat exit does not clear the continuing cast. Normal entry clears it at its established point; saved entry reconstructs its runtime. |
| Lock attempt, nested picker, return | The same attempt resumes after a picker. Genuine departure applies explicit cancellation rather than inferring it from widget destruction. |
| Partly filled stack in a full inventory | Preserve transferred units and the held remainder. Identification and practice may occur even when placement cannot finish. |
| Save during scripted fade | Decide settlement timing explicitly, then capture values and encode them. Encoding itself cannot run removal consequences or scripts. |

A refactor is finished when the new authority and transition are clear, the
original call path and redundant state are removed, and required behavior
(including timing, RNG, save representation and UI sequencing) remains intact.
Prefer fewer owners and obligations over more classes or stronger-looking
encapsulation.

## Layers

Two layers are enforced:

- **`data`** (`include/wiz8/data/`, `src/data/`): content ids, database records,
  the constant profession, race, skill and portrait tables, and `GameData` (string
  table, item, spell, NPC, fact, level, monster and missile databases, encounter
  tables, dialogue keyword lists). `LoadGameData` reads it once; a malformed
  database throws.
- **`rules`** (`include/wiz8/rules/`, `src/rules/`): the character and party model,
  party storage (`W8Party`, formation), item rules over values and characters
  (`rules/inventory.h`), character derivation, conditions and enchantments
  (`party_conditions.h`), the event log and the `Activity`. Functions take what they
  read and change explicitly: `const GameData&`, the character, party or item values,
  `Rng&`, `Events&`.

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
| `entities/` | `Monster`, `Missile`, `SpellVisual`, `Prop`, `WorldItemEntity`, triggers and locks. Each kind is loaded once into a resource-only definition (animations and lights outside any scene, events, emitters and authored parameters) from which each live entity is constructed with its own copies, scene attachments and navigator. Entities report hits, landings and release frames to their world's `SimulationSink` rather than resolving them. |
| `native/` | Application loop, game events, key bindings, FFmpeg movie playback. |
| `wiz8/` | Gameplay not yet in `rules`, the game and session, saves, screens, dialogs and level scripts. `wiz8/combat/` is the fight. `local_code/`, `local_screens/`, `dialog_code/` and `level_specific_code/` hold the decompiled game code still being migrated. |

## Compatibility boundary

Only serialized formats require **binary** compatibility: retail saves must load,
and saves we write must load in retail. Gameplay timing, content and visuals must
still match retail behavior.

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

- **Session ownership.** `GameSession` keeps the current game in a `unique_ptr`.
  Beginning a game constructs its basic owners before ending the outgoing game;
  installation preserves the new object's address. Saved-level resource preparation
  is not yet independent of the current game (see "Worlds and levels").
- **Level transitions.** `Game::travelToLevel` prepares the destination, captures
  the outgoing level, ends its level-bound activity, publishes the replacement and
  enters it. `Game::leaveLevel` performs the same departure and releases the loaded
  world without ending the game. The session uses these operations when a game ends
  or a save is installed; screens request travel and present loading, but neither
  their teardown nor their destructors load or unload levels. Entry and resource
  preparation still reach current-game helpers (monster manager, NPC bindings, item
  lists and sky/camera release); moving those internals is remaining work.
- **Clock identity.** Timers retain a narrow view of their originating game's step,
  world and calendar clocks and world hold. They cannot discover the installed game;
  restoring progress and changing timer mode retain the same owner. Disconnected
  geometry worlds sample zero simulation time, while presentation timers explicitly
  sample monotonic time. Resource decoding does not pause/resume simulation: synchronous
  decoding cannot advance step time and must not release a caller's hold. First camera
  motion resumes its clock directly, without a second pending-resume flag or repair step.
  The legacy motion-delta accumulator is still process-scoped, but is a plain scaled
  delta calculation rather than a clock-sampling timer; its ownership remains migration work.
- **Camera sequences.** The game owns its shake system and computed frame offset.
  Primary and sky worlds borrow that same system, and keyed actor/trigger effects stop
  when their owner is released. Unkeyed sequences can continue across a level change,
  including the ending callback, but are destroyed with their game. Rendering only
  reads the prepared offset. Timer and shake implementations cannot include game/screen
  headers or call current-game clock helpers; the layer check enforces that boundary.
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
- **Dialogue continuation.** `W8NpcDialogue` owns the active utterance: its originating
  conversation, NPC/script/quote snapshot, serviced-voice wait and timed hold. The panel
  owns playback, mouth and bubble resources, not another busy flag or hold clock.
  Natural voice completion records the original quote and retains its 700 ms hold;
  a scoped utterance identity prevents late completion from affecting a replacement.
  Holds are serviced at the existing speech-queue update point before portrait-idle
  and dialogue advancement, not by rendering. Replacement completes the old utterance
  and its notice before starting the next; suspension retains it and explicit discard
  abandons it without completing scripts. Headless presentation creates no wait, and
  another game's panel cannot satisfy a presentation request.
- **Advancement and stepping.** `Game::advance(elapsed, hold)` (`game_advance.cpp`)
  is the game's one step: it applies the interface's hold, runs speech, and in the
  world runs the simulation in retail order. `Game::time` is the step time that
  deadlines below the step use. Only the main game and the camp step the game.
- **Events.** Rules push typed events onto `Game::events` (notices, display
  changes, combat progress, speech) and never ask for repaints. The session's
  presenter (`PresentGameEvent`) maps each to the active screen. A game without an
  observer keeps events pending, which is how tests inspect them. Party damage,
  condition infliction/lifting, revival and settlement hold delivery with
  `Events::Operation`: nested operations complete before the outer boundary presents
  their ordered outcomes. Damage narration and splats use that same stream; mandatory
  combat, story and speech consequences remain explicit calls. Unmigrated commands
  continue to deliver immediately. Failed operations discard their unpublished outcomes,
  without implying gameplay rollback.
- **Combat.** `Combat::turn()` is idle or one actor's turn, stepped once per frame
  once nothing is waited on (camera, animation, sound, missile, script). Actors are
  held by slot or location id and looked up when used. Combat is not saved.
- **Membership.** `W8Party::members` holds an optional member per slot, so nothing
  tells members apart by flags or addresses.
- **Inventory.** Characters own their equipment and backpack slots; `Game::inventory`
  (`wiz8/inventory.h`) owns the shared pool (a packed vector of at most 500 entries)
  and the hand, and is the only thing that moves items between any of them. Each
  operation is complete when it returns: restrictions, the move, acquisition and
  settling the party. A transfer's `item` argument is what remains with the giver, so
  a refused or partial transfer never loses units. Pool deposits explicitly distinguish
  refusal, partial placement and completion; their outcomes are delivered after both
  pool and hand settle. `MergeStack` is the one stacking
  rule and changes nothing but the two values. A `W8ItemLocation` addresses a slot
  for one call (an empty hand or free pool row reads as an empty item); anything kept
  longer (a queued use, a trade selection) is checked against its expected item when
  used.
- **Character derivation.** Every statistic is derived in one place,
  `DeriveCharacterStats`, from evaluated inputs; any change a statistic depends on
  settles the party (`SettleActiveParty`).
- **Conditions.** Inflicting, lifting and aging conditions and death are game
  operations in `live_party.h`; the pure calculations they need are in
  `rules/party_conditions.h`.

## Worlds and levels

- Level reconstruction builds a replacement world completely before it is
  published; entry then starts clocks, music, dialogue and first-visit effects.
  `Game::travelToLevel` owns that sequence and leaves the outgoing level only after
  the destination and its saved status have been prepared. A failed destination
  leaves the current world loaded, but this is not yet an isolation guarantee:
  embedded monster construction, trigger seeds and prop variables still touch
  the installed game while parsing. Item creation uses the supplied game for its
  data, RNG and continuing runtime IDs; its candidate world owns ground/sector
  queries, the item record, scene attachment and lighting. Loading does not defer
  item construction past later props: trigger/item/yaw random draws and legacy
  APST identity allocation keep their established order. The world-item operation
  implementation cannot include the game or screens or find the installed world;
  the layer check enforces this boundary. Malformed compiled-level sections return a
  loading failure rather than asking presentation to terminate the application.
- Departure ends combat and captures script state into location variables before
  resources are released. Destruction does no departure bookkeeping. `Game::leaveLevel`
  is the departure operation; the session calls it when ending a game, and a screen
  requests it rather than implementing it in teardown.
- Loading a save decodes every section, each level group included, before the game
  in progress or its screens are released, into a plain `SavedGame` (`ReadSavedGame`)
  that `GameSession::load` installs at an explicit presentation-release boundary.
  A rejected decode resumes the outgoing game. The session ends combat while its
  controls exist, begins the replacement game, has `SavedGame::install(Game&)` fill
  it, and constructs and enters its level through `Game::enterSavedLevel`. The saved
  value cannot begin or end a game itself. If construction fails, the session ends the
  replacement and the caller presents the error. Resource preparation still uses the
  installed game; retaining the outgoing game on construction failure is a target, not
  a guarantee of this implementation.
- Monster and world-item records own their optional loaded runtime directly.
  Monster activation is separate from runtime presence; a world item's activation
  is its appearance's presence. Borrowed indexes and rendering relationships never
  decide their lifetime. Work that outlives an actor uses its scoped identity.
- Group counts, active-monster presence and combat membership are queries over
  ownership, not maintained counters.
- Each world has a `WorldParty` (`World::party`): the party's pose and its
  navigator, registered with the world's other movers. Placing or restoring the
  party moves both. Actors are constructed with their `World&` and use only that
  world. Render cameras derive from the pose each frame; rendering writes no
  gameplay state.

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
  gameplay state. Its cells, notes and last view are the level's `World::automap`;
  `AutomapScreen` is rebuilt with that world on every visit.
- Wizardry lighting, fog and material operations run in GLSL shaders compiled to
  SPIR-V. VSG uses Vulkan (MoltenVK on macOS).
- `RenderFrame` returns the raw intersection (or a miss) without changing the world's
  accepted selection. The main screen applies it using the current combat context.
  Rendering auxiliary views cannot replace the interaction's accepted pick. Frame
  submission receives a `FrameView` of explicit scenes, cameras, frame values and
  background, plus an optional normalized pick request. An empty view never falls
  back to the installed game. `world_view.cpp` composes these inputs from the game
  supplied by presentation; the main screen owns whether it requests picking.
  Screenshots use a steady view and do not advance clocks. The automap borrows its
  game and level together, so its world, clock and lighting inputs agree. The layer
  checker prevents frame submission from reacquiring gameplay headers or lookups.
- `endFrame` waits for submitted work: graph mutations and object destruction
  happen on the game thread between completed frames.
- The UI is drawn on a fixed ARGB1555 CPU canvas and published to a dynamic VSG
  image.
- `Game::motion_clock` owns the scaled, clamped motion delta and accumulated
  motion timestamp. Worlds, navigators and their camera borrow it through their
  originating clock view; replacing the installed session cannot redirect them.
  Level departure resets this value in place at the existing transition boundary,
  after ending camera sway. Scale changes preserve elapsed motion; timer holds
  and the camera-sway suspension latch remain separate inputs. World item falling
  and proximity updates use the owning world, including its collision geometry,
  clock and item appearances, with gravity and activation inputs supplied explicitly.
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
  The main game's dialogue panel owns NPC voice playback and the active quote's
  input; explicit screen discard abandons its logical utterance and cancels pending
  voice completion. Destructors release playback resources only.
  In the main game, `MainGameScreen::refreshLayout` decides one `MainGameLayout`
  (shown mode, panels, world viewport) from the open interaction and the player's
  preferences and changes only the panels that differ; an interaction overrides
  the layout, never the preference.
  Spell and item pickers own private controls and drafts; the screen commits a copied
  choice inside the same retirement boundary. Explicit completion returns to the opener;
  replacement and screen/combat teardown abandon it. Dismissal also retires the picker's
  modal so an old confirmation or cursor-restoration callback cannot affect a replacement.
  Camp suspension keeps the dormant parent interaction; replacement, combat and screen
  discard release it. Item rows and their detail selection store inventory locations,
  not duplicate item pointers.
- **Controls and painting.** Controls paint into an explicit `SDL_Surface&`.
  `Draw` never commits; `Refresh` commits a dirty rectangle. Screens record which
  parts the next draw repaints; the main game's `MainGameRepaint` record is resolved
  by `MainGameScreen::updatePresentation` before `MainGameScreen::draw` paints. One
  `Pointer`, owned by the router, holds the cursor and the targeting request. The
  notebook view derives its rows from `Game::story.keywords`; information dialogs
  own separate append-only paragraphs.

## Tests

CTest runs asset-independent tests and licensed runtime scenarios (label
`runtime`), which need `WIZ8_ASSET_ROOT` and a usable Vulkan backend; CI provides
both. Scenarios click controls through `GameHarness::click`, which first checks a
player could: the control is shown, enabled and reachable under the pointer.

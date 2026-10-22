# Architecture

This file describes the contracts the code relies on now. Remaining work is in
[ROADMAP.md](ROADMAP.md); principles are in [AGENTS.md](AGENTS.md).

## Compatibility boundary

Only serialized formats are compatibility contracts: retail saves must load, and
saves we write must load in retail. Save and game-data records have fixed-width
disk images and `wiz8::retail::Record` codecs (`retail_save_records.h`,
`retail_geometry_records.h`, `retail_text_records.h`, `save_codec.cpp`). Unknown and
padding bytes that belong to a file live in `retail_opaque_records.h` and round-trip
unchanged. A codec copies a field verbatim only when the runtime field is exactly as wide
as its disk field, and throws otherwise; items, formations and NPC stock entries have
codecs of their own, so their runtime types need not match a retail size. Character
attributes, resistances, enchantments, modifier blocks and HYPN effect definitions
use explicit field offsets in byte images; the character codec is the sole declaration
of its retail field layout.
Runtime types are not meant to mirror retail memory layout, but types embedded in
disk records still keep it until they get records of their own (ROADMAP.md,
"Runtime types with semantic shape"). Runtime strings are UTF-8;
retail UTF-16LE fields are decoded and encoded explicitly by utf8proc-based
codecs, including malformed-input replacement and fixed field limits.

Retail records also hold four-byte words that were process addresses when retail wrote
them. No reader trusts one as an address. Each is one of these:
- a presence word (`pointer_slot`, `presence_slot`): the writer stores whether the live
  object exists, and a reader that needs the answer reads it from the disk image (an
  NPC's character, a monster group's members, an NPC record's stock rules);
- a framing word set by the section's writer: the world item marker, and its `next`
  word, which says whether another record follows;
- a dead address, written as a constant and ignored on read: the combat target's item
  marker, a party slot's item address and the message list word are written as zero,
  and the status block's two buffer words as one.

The game only reads compiled OCT/WPT/PVL geometry; there is no level compiler or
editor. Shipped `.rlk` region links remain readable.

A retail address annotation (`// FUNCTION: WIZ8 0x…`, `// GLOBAL: WIZ8 0x…`) sits
directly above the definition it names. One that names no definition is deleted.

## Layers

The target layering is described in [ROADMAP.md](ROADMAP.md). The layers that exist
are enforced:

- **`data`** (`include/wiz8/data/`, `src/data/`): content ids, database record types,
  the constant profession, race, skill and portrait tables, and `GameData`. `GameData` is
  the string table, the item, item-table, spell, NPC, fact, level, monster and missile
  databases, the encounter tables and the dialogue keyword lists. `LoadGameData` reads
  all of it once at startup, and a malformed database throws; missing or malformed
  keyword lists leave keywords untranslated. The Application owns the loaded data and
  installs it; unlayered code reads it through `Data()`. A test owns and installs a small
  `GameData` it builds for itself with `ScopedGameData`.
- **`rules`** (`include/wiz8/rules/`, `src/rules/`): the character and party model,
  party storage (`W8Party`, formation), item rules (`item_rules.h`: names, equip slots,
  requirements, item making), the inventory's storage rules (`inventory.h`), character
  derivation (modifier blocks, effective attributes and skills, statistics and the
  party's shared load), members' conditions, enchantments, death and the party's
  settlement (`party_conditions.h`), the event log (`events.h`) and the party's
  `Activity`. Functions
  that read the databases take `const GameData&`; those that change the party, draw
  random numbers or report events take a `RuleContext` (`rule_context.h`: the data, the
  party, the game's `Rng` and `Events`, and whether the game has started), which
  `Game::rules()` builds.

A layer file includes only its own layer, the layers below it, the standard library,
glm, fmt and three stateless foundation headers (`math.h`, `world/camera_pose.h`,
`retail_opaque_records.h`). It declares no mutable globals.

- `tools/check_layers.py` checks includes and `extern` declarations, and runs as the
  `layer_boundaries` test.
- The CI tidy job also runs it with clang-tidy's non-const-global check over the layers'
  sources.
- `wiz8_rules` builds without the platform library, SDL or the engine, and
  `native_party_storage_test` links only `wiz8_rules`, so an engine dependency fails to
  link.

Unlayered code may use the layers.

## Modules

| Module | Owns |
| :--- | :--- |
| `platform/` | Window, SDL input events, filesystem roots, the monotonic clock (`timer.h`), timers and game clocks, the presentation random stream. |
| `canvas/` | CPU drawing on `SDL_Surface&`: images, STI sprite sheets, fonts, shading tables. Knows nothing about the GPU or the framebuffer. |
| `audio/` | The sound service (`Playback` values), positional `Sound3D`, `AmbientAudioSystem`, sound events and mouth-gap lip sync. |
| `render/` | VSG scene topology, shared mesh assets and instance poses, material state, views, Vulkan resources and presentation; SDL CPU canvas publication and overlay composition. SDL owns the platform window. |
| `spatial/` | `LevelSpatialIndex` (compiled octree and collision banks), `SurfaceIndex`, collision, the navigation mesh and `Navigator`. Traces, sight lines and ground probes take explicit segments and filters, own per-query scratch and return optional typed hits. |
| `world/` | `World` (scene, environment, spatial index, audio, population, entities), `PartyCamera` and `PartyMotion`, `EnvironmentState`, encounters, `level_loader` and `level_session`. |
| `entities/` | Concrete `Monster`, `Missile`, `SpellVisual`, `Prop`, `WorldItemEntity`, triggers and locks, composed from `CyclePlayer`, `Navigator` and `KeyframeTrack` values. A monster species, missile kind or spell visual is loaded once into a never-live definition (`World::*_definitions`), which every live entity is copied from. Animations hold shared frame meshes (`AnimationMesh`) and never enter a scene; a `CyclePlayer` or `Prop` owns the model instance for each part it draws and shows the current frame on it. An actor's appearance (skin and damage stage, light scale, glow, model scale) is on its `CyclePlayer`, which applies it to that instance; a damage stage is the skin table named for the species and stage on whichever mesh is drawn. Entities report missile hits, missile landings and release frames to their world's `SimulationSink` instead of resolving them, and monster scripts and triggers act on the fight through `Game` commands (`setMonsterGroupDisposition`, `startGolemAttack`, `castTriggerSpell`). |
| `native/` | Application loop, game events, key bindings, FFmpeg movie playback. |
| `wiz8/` | Gameplay rules not yet in `rules`, screens, dialogs and level scripts. `wiz8/combat/` is the fight: starting, ending and pacing it (`fight`), attack resolution (`attack`), hostility, range, the party's move in a round, group attacks and combat sounds; it resolves what the world's entities report (`CombatEntitySignals`), installed by the game's level into its primary world, and defines the commands scripts and triggers issue. |

## Runtime contracts

- **Randomness.** Randomness uses the MSVC 32-bit sequence with 15-bit draws
  (`Rng`, `rules/random.h`) in two streams. Moving, adding or dropping a draw on the
  game's stream changes gameplay ("The game", "Its random stream").
- **Pixels.** The software UI framebuffer is fixed ARGB1555. Assets and video
  may use other SDL formats; palette, TGA and STI decoding is explicit.
- **Render ownership.** `WorldScene::root` is the actual VSG graph. A visual
  instance owns its transform and drawable subgraph; its model owns shared render
  topology, UVs, material batches and baked vertex colours. Only a topology
  change rebuilds that asset; skins, bounds, pass-wide materials, textures and
  shaders, and the lighting bake update it in place. The polygons a view leaves
  visible are an input to the instance's pose, not state on the shared mesh.
  Ordinary VSG subgraphs can join the root without a Wizardry material or
  submission adapter. Typed scene collections
  remain where game activation, ownership and world queries need them; they do
  not define render parentage. Adopting or detaching a subtree updates those
  collections by traversing its graph. The spatial index retains collision, authored
  regions and activation timing.
- **Poses and views.** Each instance samples its current pose on the CPU into
  ordinary VSG position and normal arrays. Positions interpolate; normals use
  the current integer frame. The frame stamp prevents additional views from
  sampling the pose again. Picking, attachments and translucent ordering use
  the same pose. Scene brightness, environment offset and time are shared frame
  inputs. Each camera has its own depth cue and derived light, fog and clipping
  data. Materials resolve animated textures while updating, rather than freezing
  their initial frame.
- **Map view.** The automap is a separate view of the world scene, not a change
  to it. `WorldScene::map` (`MapView`) owns the map's orthographic camera, the
  `MapFilters.txt` texture exclusions and the explored vertices. Its graph holds
  only static geometry: draws that share each static mesh's arrays, materials and
  textures, with map-owned index and colour arrays. It has no actors, effects,
  lights, fog or ambient light. The layer is the map camera's near clip. Opening
  the map changes no gameplay camera, lighting, picking or mesh state, so
  gameplay needs nothing restored afterwards.
- **Materials.** Wizardry lighting, fog, material operations and generated UVs
  run in shaders. The descriptor layout is scoped to Wizardry geometry.
  Animated retail faces retain their authored integer-frame polygon normals
  for face selection. Translucent triangles retain the game's global ordering
  rule. Resource counts
  follow the content rather than the evaluation prototype's fixed limits.
- **Effects.** Particle integration and completion callbacks run in the game
  update, preserving visibility and activation gates. Render traversal consumes
  the resulting geometry and never advances gameplay.
- **Presentation and lifetime.** SDL creates the Vulkan-capable window; `Renderer`
  supplies its surface to VSG. VSG owns the device, views, command graphs and
  swapchain. Before recording, one persistent VSG compile traversal visits the
  frame: compiled objects return immediately, so only new geometry, descriptor
  sets, pipelines and views do work, and anything attached to the graph is
  compiled before it is drawn. A view's resources are collected once, when it is
  first drawn; descriptor pools are reserved for the whole frame only when they
  had to grow. Both targets share one transfer task for dynamic data.
  `endFrame` waits for submitted work before returning. That is the
  single resource-retirement boundary: game-thread graph mutations and object
  destruction occur between completed frames. Readback is requested before
  presentation and copies the completed image into an SDL surface. Window and
  offscreen targets follow physical pixel dimensions, including swapchain changes
  during acquisition or present-mode updates. Offscreen capture renders the same
  prepared graph; presentation surfaces without transfer-source support use it
  for screenshots as well.
- **UI publication.** Refresh converts the requested rectangle from the ARGB1555
  CPU canvas directly into the committed dynamic VSG pixel array. `Draw` without
  `Refresh` stays private. Widgets own retained VSG geometry, image bindings and
  explicit blend/alpha-test state. Layer groups preserve composition order; the
  cursor uses rectangular artwork and an explicit hotspot.
- **Native content.** Picking returns VSG intersections, including node paths,
  coordinates and primitive data. Existing-game callers resolve `MeshInstance`
  only when needed. `loadGameContent` reads static indexed VSG position/normal/UV
  geometry and adapts it to game lighting, ambient and fog without a retail mesh.
  Ordinary subgraphs can continue using standard VSG materials. `World::picked`
  carries the intersection into gameplay; world-owned `NativeObject`s resolve their
  graph identity there and issue normal game events when activated. Placement and
  reach use the authoritative party pose, independently of render-view offsets. `/NATIVEOBJECT`
  imports optional session content at level entry, with a shipped prism example;
  ordinary world hover/click gates apply. These cosmetic sample objects are rebuilt
  on entry rather than changing retail save records.
- **Image alignment.** Overlay and poster UVs span the complete image. Vulkan samples
  texel centers, with clamp-to-edge for posters and UI; no half-texel offset is applied.
  An omitted poster dimension follows the decoded image's aspect ratio. The retired
  blurred-text QLTY byte remains reserved in the retail settings record.
- **Visual clocks.** `Game::visual_time` stops with world and shared-timer holds.
  World texture playback, UV scrolling and light animation use it. Texture lookup
  and extra views never advance animation or draw random values. UI texture
  playback and glow use presentation time. Particles retain their game-step clock
  and activity gates; keyframed lights follow their actor's sampled cycle time.
  Inherited timers choose `TimerClock::World`, `Game`, `Presentation`, or `Calendar`.
  World gates, monster script/light/fade delays, triggers, camera holds, shake,
  ambient fades/replay, keyframes, environment transitions and item radar deadlines
  use held world time. Spell practice attempts explicitly restart cooldowns using game step
  time, including failed attempts; target eligibility queries never restart them. UI delays and portrait
  idle poses use presentation time. Calendar-trigger delays use the saved game-day
  clock. Timer saves still store retail progress/scale, rebased on load even at zero;
  pause controls hold simulation clocks without a wall-clock offset accumulator.
- **Window events.** Focus controls pause and input release. Minimize/restore
  controls presentation. Focus restoration resets the current screen's simulation
  lap, so time spent away never reaches game or world time. Pixel-size changes follow
  the high-density drawable;
  reopening presentation never reapplies startup window dimensions.
- **Threads.** Scene updates, traversal and rendering run on the game thread;
  audio completion callbacks are dispatched there by the sound service.
- **Shaders and platforms.** GLSL is compiled to SPIR-V in the normal build.
  VSG uses Vulkan on Linux and Windows and MoltenVK on macOS. There are no
  SDL_GPU pipelines or separate MSL/DXIL shader outputs. The Vulkan SDK is a
  build dependency; macOS packaging also supplies MoltenVK.

## Worlds and levels

- The game's `Level` (`Game::level`) holds the primary and sky slots, which own
  their worlds; queries borrow those owners, so no world outlives its game.
  Replacing a slot hands back the previous owner, and a world is never reachable
  from its slot while it is destroyed. `GetWorld()` and `GetSecondaryWorld()`
  are transitional forwarders to the slots, null with no game in progress. World
  identity and resource paths are immutable, and live level queries use the
  primary world. `resume_level` is decoded save data, filled in save snapshots.
- `World` member order is its teardown order. Destruction stops activity and drops
  borrowed indexes, then releases entities, geometry, the spatial index and
  finally the scene. Replacing the primary world stops callbacks before ownership
  changes; a failed load retires the same way without rendering a partial world.
- Level reconstruction assembles records, representations, NPC bindings and group
  links against the unpublished replacement. Entry then starts encounter clocks,
  restored death activity, music, dialogue and first-visit effects.
- Departure ends transient combat, records the session sight stamp and captures
  script state into location variables before releasing resources. Resource
  destruction performs no departure bookkeeping. Whole-game saves capture state
  without advancing scripts or modifying live records; only a successful save
  resets autosave pacing.
- A world owns its monster records, groups, world items and one encounter state.
  Removing a monster performs its combat, group, visibility, spatial and NPC
  cleanup before the roster erases it. Group counts, active-monster presence and
  combat membership are queries over ownership, not maintained counters; retail
  count fields are decoded data, re-derived when saving. Formation linking
  reserves a destination before changing either relationship.
- A monster or world item record links to its scene entity (`entity`, an `EntityLink`).
  Activation attaches it, and removal or deactivation detaches it before the entity is
  destroyed. An active record always has an entity, but an inactive one may not, so a
  caller says which it needs: `entity.find()` may find none, and `entity->` and
  `*entity` require one (asserted). An operation that needs the entity takes
  `Monster&` or `WorldItemEntity&`, so its caller resolves the link once; one meaningful
  for an inactive record takes the record and handles absence itself. Anything that keeps an actor
  past the current call keeps its id: a monster's location id or a world item's runtime
  id. It looks the actor up again when it is used and does nothing when the actor is
  gone. Navigators target and follow each other by address, so a destroyed navigator
  clears every link to itself. A placed item's level trigger belongs to the item's
  record, because the item's entity is rebuilt each time the item comes back into range.
- A world owns its cursor while it is shown (`World::cursor`) and what a new cursor
  starts from (`World::cursor_recall`: the last cursor's distance from the camera, reset
  when a fight ends, and its monster group), so no cursor state outlives its world.
- A level's script is part of the level ("Scripts"). Its masters return completion
  directly; masters armed during a dispatch wait for the next frame.
- The party's pose is the primary world's `party_camera`: the eye position and the
  orientation (`partyPose`). Movement writes it (`updatePartyMovement`); level entry,
  portals, reloads and recall place it through `placeParty` and `restorePartyPose`,
  which also stand the party navigator `g_default_world_height` below the eye
  (`navigationPosition`). Gameplay queries, saves and the automap read it. The render
  cameras derive from it each frame (`placeWorldCameras`): the primary camera and its
  light at the eye plus the frame's shake, and the sky camera turning and swaying with
  it. Rendering writes no gameplay state, so extra renders and views leave the party
  where it is.

## Scripts

- **The context.** NPC scripting and the level scripts act on a `ScriptContext`
  (`script_context.h`): the party, the story, the status record, the activity, combat,
  the event log, the loaded world (null between levels) and the game, for the commands
  that still take it. Whoever runs a script passes it in; code that does not receive
  the game yet builds it with `MakeScriptContext(CurrentGame())`.
- **Level scripts.** Each loaded level owns a `LevelScript` (`World::script`), made by
  `MakeLevelScript` when the level is entered and destroyed with the world, so script
  state never outlives its level or game. A level with level-specific code has a
  subclass whose members are that state; `enter()` binds its trigger callbacks and
  restores its saved state from the location variables. The script runs its masters
  each world update, records their persistent state when the level is left or saved
  (`captureState`), and dispatches the world-cursor nodes. Trigger activation
  callbacks are `std::function`s bound to the script.
- **NPC scripting.** The dialogue (`W8NpcDialogue::Advance`, `Speak`, `Answer`), quote
  presentation, story actions and fact consequences take the context. Closing a
  dialogue tells the level script, whose watches wait for it.

## The game

- **What `Game` is.** `Game` (`game.h`) is one play-through. It owns:
  - the party and the status record;
  - the random stream, `rng`;
  - the story (`story.h`): facts, faction dispositions and relations, the journal, the
    learned keyword notebook, NPC states and level-script variables;
  - the text boxes' message log;
  - combat and the spell effects still in progress;
  - the activity and the world's advancement;
  - the camping surprise sequence, the search sweep's deadline and the autosave notice;
  - the speech queue;
  - the NPC conversation: its progress (`npc_dialogue`), who speaks with whom, the
    offered item and the staged script notice (`npc_interaction`; the dialogue's
    interface itself is the main game's `W8NpcDialoguePanel`), the quote or portrait
    message on screen and the NPC voice (`npc_presentation`, `npc_speech`) and the
    Al-Sedexus scene;
  - the shared spell cooldowns;
  - the gameplay timer;
  - the monster-manager entries;
  - the gameplay flags;
  - the event log, `events`;
  - the level, `level`: the primary and sky worlds ("Worlds and levels"). It is
    declared last, so its worlds are destroyed first, while combat and speech still
    exist.
- **Its lifetime.** The Application owns a `GameSession`: the game in progress, if any,
  the random stream the next game continues and the event presenter. The router hands the
  session to the screens it builds; the ones that begin and end games keep it. A game
  begins in party creation (`BeginNewGame`: party selection's entry, reset and import) or
  when a save is loaded (`LoadGame`); either replaces any game in progress. It ends when
  the main menu is entered and at shutdown. Nothing is reset field by field: a new game
  starts every member afresh, and the destructor stops the NPC voice and the Al-Sedexus
  ambience. Destroying a game does no departure bookkeeping, so the level is left by the
  main game, which belongs to one game: it is discarded before its game ends (quitting
  returns to the menu) or is replaced (a load first discards the suspended screens above
  the menu, `DiscardSuspendedScreensAbove`), and a fresh one is built for the next.
- **A new game's story.** Factions start from their starting dispositions and relations;
  the other story parts start empty. The opening sequence creates the starting NPC states
  (`CreateNewGameNpcStates`), and the new-game loading screen seeds the starting facts
  before the first level and the rest after it. A load fills each part from its save
  section instead.
- **Before the opening sequence.** Party selection, import and character creation edit
  the new game's party. `GameStarted()` tells play apart from that party creation.
- **Its random stream.** Every gameplay draw comes from `Game::rng`: party creation
  (import conversion, voices, starting hit points), combat, monster AI, spells, traps,
  encounters, loot, NPC trade and theft, skill practice and level scripts all share
  that one stream, as they shared retail's `rand()`. Rules that receive the game draw
  from `game.rng`; unmigrated code reaches it through `CurrentGame()`.
  - A session draws one sequence from one seed, as retail did. The stream starts from
    the clock, and a game continues the stream of the game it replaces or that ended
    before it, so party selection, which begins a new game each time it is entered,
    keeps retail's draw order into play. `GameSession::seed` fixes the seed (the runtime
    scenarios use Catch's seed).
  - Loot and NPC theft reseed the stream from saved values: a trigger's item group from
    its persistent seed, a theft from the party's experience spun by the NPC's naming
    style and suspicion. Their outcomes do not depend on earlier draws, but every later
    draw follows from the reseed.
  - Presentation draws from its own stream, `PresentationRng()` (`platform/random.h`),
    seeded from the clock: sound variants, pitch and volume, ambient sound and music,
    particles, animated textures and lights, prop animation, the world-item pulse, camera
    shake, damage splats and portrait and spell-page flourishes. In retail they shared the game's
    stream, so frame timing and the sound settings moved game draws; now they cannot.
    The game's sequence is retail's with those draws removed.
  - `tests/native/random_test.cpp` traces a seeded game's draws (`Rng::trace` records
    each value and the function that drew it) through party creation, loot, skill
    practice and combat rules and compares them with the expected sequence.
- **The activity.** Rules branch on `Game::activity`, never on a screen id or a UI flag.
  It is a plain value with independent parts, because the game lets them overlap:
  - `place`: the world or the camp. The camp commands set it: opening the camp from
    the world or from party creation's review sets the camp, and leaving it sets the
    world. Game time, monster sight, queued spell effects and combat-end effects follow
    it. A screen covering the world is not a place: a covered world is not stepped, and
    the interface reports it as covered (see "Advancement").
  - `conversation`: the NPC dialogue opens and ends it (`BeginNpcDialogue`,
    `EndNpcDialogueSession`).
  - `conversation_aside`: raised when the party steps out of a conversation to camp, the
    journal or an NPC's spell or item service; the next dialogue panel clears it.
  - `scripted_scene`: from `BeginNpcScriptedScene` until `EndScriptedPortraitPick`.

  Combat is not part of it (`Game::combat`). Rules that only decide what to redraw or
  notify still check the screen.
- **Advancement.** `Game::advancement` says whether the world moves on. `paused` is the
  player's pause; `held` is the pause or the interface's hold, as last applied, and
  missiles, encounters, combat turns and the search sweep wait while it is set;
  `time_passes` runs the environment clock, which also stops for a fight. The interface's
  hold (`WorldHold`) is read once per step: each step applies the hold its screen passes
  in, and the main game screen covers the world when another screen replaces it
  (`WorldHold::covered`; a new game's world starts covered). Monsters do not look about
  while the world is covered, so a level change's loading leaves sight alone.
  `ApplyWorldAdvancement` acts only on edges (the move timer, environment time, the
  navigators, the party's motion) and pushes `WorldAdvancementChanged`, which the screen
  presents. Starting or ending a fight and camping apply the last hold again. The game clock (`UpdateGameClock`: turns, aging, recharges, fatigue) runs only in
  the world, fed by the environment clock and by the rounds of a fight there; aging also
  waits while `aging_held`, which the screens set while the camp's Use hands an item to
  the use-item picker (`MainGameScreen::item_drag`).
- **The planned formation.** `Game::planned_formation` is the marching order being
  planned: the formation panel stages its edits there, and in a fight the party takes it
  when the round ends. Joins, departures and conditions keep it in step with the party.
- **Stepping.** `Game::advance(elapsed, hold)` (`game_advance.cpp`) is the game's one
  step. It applies the interface's hold, runs speech (the deferred character events and
  the party's voices, `AdvancePartyVoices`) unless the portraits are hidden, and in the
  world runs the simulation in retail order: the surprise sequence, autosave pacing, the
  worlds' props, the monster manager, movement fatigue and the search sweep (or the
  combat turn in a fight), combat starts, monster sight and the nearby world items on
  their own countdowns, monster groups, and the worlds. The elapsed time drives those
  countdowns, the frame delta motion reads (`GameTimeAccumulator`) and the environment
  clock (`PassEnvironmentTime`), and it accumulates into `Game::time`, the game's step
  time. Code below the step stamps its deadlines on that: the surprise fade, the
  recent-combat window, speech's dispatch delays, recent-event window and mouth frames,
  and, through `GameClockMilliseconds`, the world's animation clock (cycles, monsters,
  missiles, particles). Props, monster generators (`GeneratorInterval`) and the autosave
  countdown take the elapsed time itself, and stand still while the shared timers are
  held (`SharedGameTimersHeld`), as their retail timers did. Inherited world timers
  use the held world clock; interface timers explicitly use presentation time. Two
  screens step the game and measure the time between their steps
  (`LapTimer`): the main game, which each frame reads input and moves the camera, steps
  unless a screen transition is pending (passing its hold and whether a picker holds the
  combat turn), animates the portraits (`AnimatePartyPortraits`), then commits pickers,
  services dialogue and draws; and the camp, which steps with the world covered. Time
  under other screens, and while a transition or the review transition holds the world,
  never reaches the step. A navigator that moved during a fight is reported as
  `CombatantsMoved`, and the screen recounts its monster target panel.
- **A fight.** `Game::combat` is a `std::optional<Combat>` (`combat/combat.h`): engaged
  exactly while the party fights, so "in combat" means the optional holds a value.
  `Game::beginCombat` creates it after the entry checks and bookkeeping; `Game::endCombat`
  settles the fight, destroys it, and then restores the world around it. Nothing of a
  fight survives into the next one.
- **The combat turn.** `Combat::turn()` is a `CombatTurn` value: idle, or a
  `CombatActor` (a `PartyActor` slot or a `MonsterActor` location id, looked up when
  used) that is scheduled, resolving or finishing. `Combat::step` (`combat/turn.cpp`)
  steps it once per frame after `Combat::waitingOn` reports nothing to wait on (camera,
  monster animation, hit sound, retiring spell effect, missile, queued NPC script, in
  that order). The held hit sound is released by its completion callback; the camera,
  monsters and missiles are read as they are, the monsters through
  `Monster::HoldsCombatTurn`, so their cycles and frame thresholds stay in the entity. The scheduler and the actions it runs are
  `Combat` members that take the game (`combat/actions.cpp`); a party member's action
  is `ExecuteCharacterAction(game, slot)`, because members also act outside a fight.
  Outside code changes the turn only through `endTurnOf` and `setStepOf`, which leave
  a turn the actor no longer holds alone, so an actor that dies or leaves while its
  action runs is not given its turn back. `action_delay` paces the scheduled actor and
  outlives the turn. It and the fight's other countdowns (continuous combat's start
  delay, a continuous-combat move's pacing) run down by the step's elapsed time
  (`Combat::passTime`), not by the clock. Combat is not saved.
- **Transitional access.** Code that does not yet receive the game reaches it through
  `CurrentGame()`, which reads the one live `GameSession` (it registers itself) and
  throws when no game is in progress. Menus check `GameInProgress()`. Tests own a session
  of their own (`ScopedGame`).
- **Events.** `Events` is the game's ordered log of what happened: notices
  (`TextNotice`, a member's `CharacterNotice`, a `NoticeDialog`), text-box mode changes,
  display changes (`PartySlotChanged`, `PartyLoadChanged`, `CombatPanelChanged`,
  `CharacterActionChanged`, `EquipmentSettled`, `PartyPanelChanged`,
  `PartyEffectsChanged`, `PartyMembershipChanged`, `MonstersChanged`,
  `WorldItemRemoved`), a fight's course (`CombatBegan`, `CombatEnded`,
  `CombatProgressChanged`, `CombatStanceChanged`, `CombatEffectsChanged`) and skill
  changes (`SkillAvailabilityChanged`, `SkillImproved`) and speech (`CharacterSpeech`).
  Rules push them; they never ask for repaints. An installed observer receives each event as it is pushed, so
  presentation keeps its place among the screen calls that unmigrated code still
  makes; a game without one keeps them pending, which is how tests inspect them.
  `GameSession::observe` (called once by `InitializeGame`) gives every game the session
  begins the UI's presenter, `PresentGameEvent` in `Screens.cpp`, which maps each
  event to the active screen's notice, repaint or character-screen call (the main game's
  repaints only while it is shown) and decides there whether a skill increase is shown
  at once.
- **Voices.** A voice that finishes after its game has ended is dropped.
- **Saves.** `save_codec.h` holds the save sections that are pure functions between bytes
  and values: `wiz8::save::GameState`, the game's `GameStatus` and party that the GSTA
  section stores; the message boxes (TEXT), location variables (TVAR), dialogue
  transcript (NPCI), NPC states (NPCT), facts (NPCF), factions (FATA), journal (JRNL), the
  monster-control effect (HYPN) and stored characters (CHAR); and `LevelStatusHeader`, a
  level group's STAT record. A save captures each value from the game (the GSTA copy is
  stamped with the camera, difficulty and Iron Man file time, shown messages carry their
  remaining countdowns, and location variables fold in the level script's state) and
  writes its payload. A load decodes every section before it begins the replacement
  game, so a malformed section fails the load and leaves the game in progress
  untouched; the decoded sections are then installed in file order. A malformed CHAR
  section is skipped. The retail records of these sections exist only in `save_codec.cpp`, and
  `GameStatus::retail` carries the serialized bytes that are not game state. A started
  game keeps character files in `Game::stored_characters`. The level groups are still
  written from live worlds by `LoadSaveGame.cpp`; reading one decodes the whole group
  into a `LevelState` without the level's world (`saved_world_sections.h` for the trigger,
  prop, lock and action sections; the sections say themselves how many records follow),
  then builds the replacement world's monsters from it and applies the sections in the
  order they were read. The game keeps the groups of levels the party has left as bytes
  in `Game::left_levels` (each a RIFF image of its own, in memory): leaving a level
  stores its group, entering one decodes it, a save writes them beside the current
  level's, and a load brings every saved group in. No working file sits beside the saves.
- **Configuration.** The configuration file's CNFG chunk is a fixed 0xa4-byte record with
  its own codec (`EncodeGameSettings`); `W8GameSettings` has an ordinary layout and keeps
  the bytes of its unnamed slots.

## Gameplay operations

- **Actions.** Spell and item pickers own an action draft while open; cancelling
  discards it, and committing copies the completed draft. The party keeps a row
  (`W8PartySlotRow`) for every slot, occupied or not, with three independent parts
  (`action_choices.h`): the member's `remembered` choices (basic
  action, last spell, item use, breath target, the repeat key's action), the action
  `committed` for this turn, and the action `executing`. A berserk choice yields to
  attack while the member cannot berserk and returns when they can
  (`ReconcileBerserkChoice`, a rule); in combat a weapon-hand change also re-aims an
  attack and flips the alternate hand (`ReconcileCombatHandChange`). Target preview and execution share target geometry
  and sight rules but never share random power or damage results. An item use
  (`W8ItemUse`) names its item's location and the item id it expects there; a staged or
  committed use whose location no longer holds that item is abandoned.
- **Membership.** `W8Party::members` holds an optional `W8PartyMember` per slot: the
  member's place in the marching order, its NPC state and binding, walking fatigue not
  yet charged and whether its level-up was announced. An empty slot has none, so
  nothing tells members apart by flags or addresses; `npcIndex` and `orderIndexOf`
  answer for empty slots too. The save's party record keeps these fields beside the
  slot's action state, and an empty slot is written with a cleared member.
- **Trade.** Trade rows keep a source location and the expected item contents;
  compaction or changed contents invalidates the selection. Identical stacks are
  fungible, and there is no global item identity. Purchasing prepares stack merges,
  capacity and overflow placement before committing stock and payment.
- **Inventory.** Two headers share the inventory. `rules/inventory.h` holds the
  storage rules over a `RuleContext`: placement (`PlaceWithCharacter`, `PlaceInPool`,
  `PlaceInEmptyHand`) returns where an item went, and removal, splitting, exchange,
  stack merging, pool sorting, weapon-set swaps and attack consumption return an
  `InventoryEdit` naming the locations they replaced. Rules address members by party
  slot and items by `W8ItemLocation` (`PartyItem`) or as owned values (`OwnedItem`),
  never by scanning storage for an address. `wiz8/inventory.h` holds the game's
  commands, which apply a rule and then what it owes the game: acquisition effects
  (story facts, exceptional-item reactions), world drops, and one settlement per
  command (unusable equipment comes off in slot order and is stored like a received
  item, statistics and shared load are recalculated, an exhausted member dies, combat
  choices follow a weapon-hand change, committed item uses whose item is gone are
  dropped, and a changed hand is reported). A change of quantity is not a replacement.
  Notices and display changes are events; a player command turned down returns an
  `InventoryRefusal` the calling screen shows (`ShowRefusal`). Queries take
  `const W8Party&`. `W8Party` owns storage primitives with no gameplay;
  `party_stats.h` derives values from explicit rules; `live_party.h` supplies the
  game's own rules and applies what follows. `PC Item.cpp` keeps per-item effects.
- **Character derivation.** Every statistic is derived in one place,
  `DeriveCharacterStats` (`character_stats.h`): pool ceilings, regeneration, damage
  reduction, resistances, capacity, weight and load, initiative, attacks and armor
  class, from the evaluated inputs (`EvaluateCharacterInputs`: modifier blocks,
  effective attributes, skill levels, hand kinds), equipment, learned spells,
  conditions, fatigue band, the assigned share of the party's load and
  `CharacterStatRules` (combat action, damaged android). It leaves current pools
  alone. `party_stats.h` evaluates the members, assigns the shared load, derives, and
  then applies the resource transitions: hit points, stamina and spell points follow
  changed ceilings and the fatigue band follows stamina; an exhausted hit-point pool
  and a crossed carrying capacity are reported, and settlement turns them into death
  and the overload notice ("Conditions"). An operation captures one `CharacterBaseline`; the transitions
  compare against it and swings read its damage bonuses (retail scores swings with
  the bonus its previous calculation left), so a second pass, as after a fatigue-band
  change, cannot change the result. Every change a statistic depends on settles the
  party (`SettleActiveParty`). The creation editor, which edits effective
  attributes and skill levels itself, recalculates without re-evaluating them
  (`RecalculateEditedCharacterStats`), and learning a spell rebuilds the realm
  ceilings for every profession, as retail does, and lets the pools follow.
- **Conditions.** `live_party.h` holds a member's conditions and enchantments, death
  and the party's settlement as game operations on an explicit `Game&`
  (`InflictCondition(game, …)`, `LiftCondition`, `SettleActiveParty`, …). Each reads
  as one sequence: it changes the party, draws from the game's stream, pushes notices,
  display changes and `CharacterDied`, and applies its consequences for inventory,
  combat bookkeeping, formation seats and the portrait pose, sight, targeting, the
  NPC's state and the members' spoken reactions in place, so the order of
  consequences and draws is kept. The self-contained calculations they need live in
  `rules/party_conditions.h` over explicit inputs: durations, trait resistance, what
  holds a hex or insanity, the highest condition, poison aging, cheat-death turns and
  hit points, enchantment strength and the condition block. Inflicting, lifting, aging
  and enchanting rebuild the member's condition block and settle the party once;
  settling kills a member whose pool ran out, and death's own bookkeeping happens only
  when the death condition takes hold. The presenter plays the death sound. Monster
  conditions stay with the world's monster records (`ConditionsAndEnchantments.cpp`).
- **Character events.** Quote resolution returns text directly; voice timing and
  subtitles use the same value. Ambient exchanges hold one next-speaker schedule,
  and cancelling it prevents late completions from restarting idle dialogue.

## UI

The UI has one input dispatcher, screen-owned hit targets, ordinary controls and
explicit SDL painting. Screen controllers own layout and draw order; there is no
widget tree.

### Screens

- `screen.h` declares `Screen` (enter, resume, frame, leave) and the router.
  The router owns the current screen and a stack of suspended screens, each
  kept with the request it resumes with. A request suspends the current screen
  under the destination unless it asks to discard it; a return discards the
  current screen and resumes the top of the stack; returning with nothing
  suspended ends the program. `CreateScreen` builds a screen when it is first
  requested, with the game for the screens that belong to one (the main game, the camp
  and the character screen), and the router destroys it when it is discarded, so a suspended
  main game keeps its HUD (`MainGameScreen::hud`; its draw path receives it, and the
  main game's other free functions reach it through `MainGameHud()`). The camp,
  character and options screens are rebuilt on every visit: their `rebuildRequest`
  makes the router destroy them when they are suspended and enter a new one with that
  request in place of resuming them (a rebuilt camp reviews the character it showed).
  They hold their per-visit state as members and exist only while current.
- Discarding suspended screens, on a load or at shutdown, has each leave while it is
  still on the stack, so its teardown finds it through `FindScreen`.
- Other modules reach a screen through `FindScreen<T>()` (or `MainGame()`),
  never through a global slot: `PresentGameEvent` hands an event to the screen
  that shows it, and UI code calls a screen's own methods. The intro film,
  loading mode, options page and character-screen mode travel in the request's
  mode variant.

### Input and ownership

- `UiLayer` owns stable `HitTarget`s (a rectangle or circle, a priority, a pointer
  callback and an optional tooltip provider). The dispatcher borrows live layers
  and owns hover, capture, modal scopes and the tooltip timer. `PumpUiInput`
  consumes normalized SDL events and applies the screen's input policy.
- Runtime events, held keys and command bindings use SDL keycodes. Retail
  virtual-key numbers exist only at the saved-binding and `MGSKeyboard.ini`
  boundaries. `PollInputEvent` returns an optional event by value.
- Coordinates are in 640×480 design space (`DesignRect`, converted explicitly to
  `SDL_Rect`). SDL rectangles are half-open; retail inclusive bounds are converted
  when a target is constructed.
- Precedence: `UiLayerOrder` declaration order, then registration order; within a
  layer, higher target priority, then earlier registration. Text fields use the
  cursor tip and win over their backing artwork.
- A press captures whatever its handler returns, because gameplay controls that
  `Pass` a press still expect its release. Hover-only targets call
  `HitTarget::setPassive()` and never capture. A release outside still reaches the
  source; conventional buttons require a matching press and an inside release.
  `UiResult::Pass` delegates to screen policy, not to lower targets.
- Disabling or deleting a target clears its hover, capture and displayed help,
  including references saved by a suspended modal layer. Modal layers block the
  targets beneath and restore valid capture on dismissal. Controls receive their
  owner's layer explicitly.
- Screens own their layers and keep them while suspended, so the game loop
  disables all input before entering a screen and the entering screen enables its
  own. In the main game, a mode change does the same and `SyncMainGameModeInput`
  re-enables the layers that mode uses.
- A `UiTransaction` spans one input step: a pointer dispatch, a `PumpUiInput`
  drain, or a modal's `ProcessInput()`. Transactions nest. When the outermost ends,
  actions queued with `AfterUiDispatch` run, then objects retired with
  `DestroyAfterUiDispatch` are destroyed. This is the only deferral mechanism, so
  no object is freed while its own callback is on the stack. The pump yields once
  an action is queued, leaving the remaining events for the next owner.
- `ProcessModalOwner()` runs a modal's input step as one transaction; a modal
  opened from inside the current one retires it after `ProcessInput()` returns.
  Dialog owners extract typed completion values, disable input, retire the closing
  object and then deliver the callback. Destructors release resources and never
  deliver results.
- The dispatcher plays hover, press and release sounds; buttons only silence them
  or play the repeat release. Tooltip providers read current state when shown.

### Controls and painting

- `ButtonInteraction` holds press, selection, arming and repeat state;
  `ButtonInputRule` names the matching-press and pressed-state contracts.
  `W8ButtonWidget` is an ordinary button on its owner's layer. A `ButtonSetup`
  (callbacks, policy, tooltip, priority, artwork mode) is replaced whole by a
  successful `Configure`; a failed image load leaves the previous setup intact.
- `W8ButtonWidget::Draw` paints into the supplied surface even when clean and never
  commits an update. `Refresh` commits a dirty or forced framebuffer update; owners
  restore backing artwork before a forced repaint. Scrollbars follow the same rule.
- Buttons, text buffers, editors, scrollbars and dialog controls paint into an
  explicit `SDL_Surface&`. Scroll callbacks request a redraw instead of painting.
  A closing overlay calls `EraseRegion` on the area it painted.
- `TextFieldGroup` supplies UTF-8 editing, selection, capacity, tab order and nested
  focus restoration; numeric split fields use it with digit restriction and share
  their dialog's input layer.
- `W8RangeControl` registers its three targets on its owner's layer and unregisters
  them on destruction.
- `MainGameScreen` owns its HUD buttons, input layer and active submenu as
  values. Suspension disables their input; full game exit destroys them.
- The camp screen owns routing, the header, portraits, the page buttons,
  `CampDialogs` (its one modal dialog) and the character-info panel. Its items,
  stats, skills and spells pages own their state and controls behind a small
  lifecycle and hold no reference to the camp: `Activate` and
  `ReviewedCharacterChanged` give them the reviewed character, their
  constructors give them `CampDialogs` and the character-info panel, and the
  items page selects a character or leaves the camp through two callbacks. The
  items page owns the item-action buttons and repaints cells from what they
  last showed.
- Camp invalidation is per part: the header and each page record what the next
  draw repaints, down to single item cells on the items page, and
  `CampScreen::InvalidateAll` repaints everything. The items page asks for a
  full or portrait repaint through `TakeCampRepaint`, which the camp reads
  before drawing. Each draw receives whether it is a full repaint, and
  everything recorded is cleared after the draw.
- The main game's invalidation is per part too: its HUD's `MainGameRepaint` is the one
  record of what the next draw repaints. It holds each portrait and HUD part, the mode
  panels (spell, use-item, NPC dialogue), the portrait plate, the keyboard, formation and
  movement panels, and `everything` for a full repaint (which entering the main game asks
  for). Panels own no repaint flags of their own; widgets keep only their draw cache
  (`m_dirty` on buttons and text areas). Each frame first updates what it shows
  (`UpdateMainGamePresentation`: the party's facing, the targeting presentation, the
  radar blips, button and gauge state, the portrait plate's layout, and the mode panels'
  state), then `DrawMainGameScreen` paints the recorded parts (`ApplyMainGameRepaint`,
  where painting a portrait band also repaints what lies on it), invalidates the
  rectangles of the parts it was asked for, and clears the record. Painting changes no
  game or panel state. Screen code marks the parts it changed; game events mark the
  shown main game's parts through the presenter.
- One `Pointer` (`pointer.h`), owned by the screen router, holds what the pointer shows
  and what the player is asked to pick a target for. The main game and the camp borrow it
  from their construction, so a camp opened over a main game, or from party creation,
  aims with the same pointer. It shows what a screen last asked for (`showSlot`, a
  cursor-table slot; `showHeldItem`, the held item under the camp's move overlays;
  `showCustom`, an image the screen drew) together with the hand it asked with. A
  request lapses when the hand changes, and without one the main game and the camp show
  the held item and other screens the arrow. `present` applies this every frame and
  after every request, so commands that change the hand never touch the cursor; screens
  drop their request on entry with `reset`, and dialogs restore the request they opened
  over. Its target need (`setTargeting`) shows that need's cursor; `SetTargetingMode`
  also refreshes the party panel and the aiming cone, and game code asks for it with a
  `TargetingRequested` event.

## Tests

CTest contains asset-independent fixtures and licensed runtime scenarios (label
`runtime`: video, character creation, game start, movement, quick-save, combat,
save-fixture creation and loading). Runtime scenarios need `WIZ8_ASSET_ROOT` and a
usable Vulkan backend; CI provisions the licensed assets. A skipped licensed test
does not establish behavioral parity.

Scenarios click controls as objects (`GameHarness::click`), which first checks that a
player could: the control is shown and enabled, the pointer over it reaches it, and
the presented frame shows what the UI painted there. On every frame the harness also
requires a newly active modal input scope to be shown within four frames, so input
can never be captured by a prompt the player cannot see.

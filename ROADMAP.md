# Roadmap

The goal is a clean architecture: gameplay rules that read like the game, owned
state with obvious lifetimes, presentation that consumes outcomes, and retail
formats confined to codecs. [AGENTS.md](AGENTS.md) states the principles;
[ARCHITECTURE.md](ARCHITECTURE.md) describes what exists now. This file is the
single list of remaining work. Delete an item when it is done, rather than
marking it.

The target architecture and its migration come first; the steps are ordered so
that no code is rewritten twice. Contained follow-ups and research questions
follow.

## Target architecture

Today the architecture is the decompiled program's:

- **Shared mutable state.** 341 `extern` globals. The largest are `g_status` (about 2,250
  uses), `g_combat` (1,050), `gXStatus` (630), `g_level_block` (950, 93% of them in
  screens) and `g_localized_strings` (1,080).
- **The simulation runs inside a screen.** `MainGameScreen::frame` ticks the simulation,
  and the screen router sets `Activity::place`, so game time passes only while the main
  game screen is current.
- **Rules drive presentation directly.** Rules call it about 500 times (notices,
  redraws, cursor, screen ids), and 32 of the 57 `local_code` files include screen
  headers.
- **Games are reset by hand.** New game and load clear globals one by one
  (`ResetForNewGame`, `LeaveGameSession`, `ResetGameplayStatusBlock`,
  `DestroyGameplayObjects`, `ResetMainGameScreenState`).
- **Combat is driven by animation.** Combat polls animation state, and missile and
  monster entities resolve attacks themselves.

The target is five layers. Each may depend only on the layers listed above it.

| Layer | Owns | May not |
|---|---|---|
| `data` | `GameData`: the string table; item, spell, monster, NPC, fact and level databases; missile and encounter tables; profession, race and skill tables. Immutable after loading. Retail record codecs. | change after loading |
| `rules` | Functions over explicit state: inventory, character derivation and settlement, conditions, spell and attack resolution, facts, factions, trade, NPC script evaluation. They take a `RuleContext` (`const GameData&`, the state they change, `Rng&`, `Events&`), return values, and report refusals as `std::expected`. | include SDL, render, audio, world or screen headers; touch a mutable namespace-scope variable |
| `game` | `Game`, one play-through. It owns the saved `GameState` value (party, story, clock, message log, level progress), `Rng`, the `Level` (primary and sky worlds, monsters, triggers, encounters), `std::optional<Combat>`, the `Activity`, the speech scheduler and the event log. Commands are its member functions. `Game::advance(elapsed)` is the only simulation step. | include screen headers or call presentation |
| `ui` | The router and screens. A screen borrows `const GameData&` and `Game*`. It draws from state, issues commands and presents events (notices, voices, portrait flashes, camera cues). It owns all presentation state: the main game's HUD (today's `g_level_block`), text boxes, cursor, targeting mode, the edited formation and the item drag. | change game state except through commands |
| `app` | The platform (window, GPU, audio device, input), `GameData`, `std::optional<Game>` and the UI. | |

The decisions behind it:

1. **A game is an object.**
   - A new game is `Game(data, PartyDraft)`, a load is `Game(data, SaveFile)`, and
     quitting to the menu destroys it.
   - The reset functions disappear, along with every `Clear*`/`Release*` they call.
   - Party selection, import and character creation edit the party of a game still in
     party creation, so menus outside a game never touch live state.
2. **Rules emit events; the UI presents them.**
   - Rules append, in order:
     - domain facts (`ItemReceived`, `ConditionChanged`, `CharacterDied`,
       `RoundStarted`, `GoldChanged`);
     - narration (`Message{text, speaker}`);
     - speech requests.
   - The UI drains the log after each command and each step, and presents it in that
     order.
   - `RequestRedraw` and redraw bitmasks disappear: screens repaint what an event
     invalidated.
   - The retail order of messages and voices and every random draw are preserved.
3. **An activity replaces UI modes.**
   - `Game::activity` says where the party is (the world or camp) and whether it is
     talking, has set a conversation aside or is in a scripted scene. Combat stays
     `Game::combat`, because a fight goes on in any of them.
   - Rules branch on the activity: Identify works only while camping, and time passes
     while the party is in the world.
   - Rules never branch on a screen id or on `fCampMode`/`fNpcDialogueMode`-style
     flags. Screens follow the activity.
4. **The simulation steps in one place.**
   - `Game::advance` runs, in a fixed order, what `MainGameScreen::frame` runs today:
     speech, surprise, autosave pacing, monsters, movement fatigue, search, combat,
     sight, groups, worlds, the environment clock and NPC events.
   - The screen decides only whether to step (pause, an open modal).
5. **Combat advances on explicit signals.**
   - `Combat` owns its participants' choices and its pacing, and steps when the
     presentation reports its actors idle.
   - Animation frames and missile impacts post simulation events (`SpellReleased`,
     `MissileHit`) to combat. Entities no longer resolve attacks themselves.
6. **Saves are codecs over values.**
   - A save is `GameState` plus one `LevelState` per visited level.
   - `W8GlobalStatus` and the other retail records exist only inside the codec.
   - A save snapshot is a copy of `GameState`.
7. **The boundaries are enforced.**
   - `data` and `rules` build as a library without the engine or SDL, and `game` does
     not link `ui`.
   - A checker limits each layer's includes. CI applies
     `cppcoreguidelines-avoid-non-const-global-variables` to the layers.

## Migration

Each step lands one subsystem in its final shape and moves its code behind the enforced
boundary (ARCHITECTURE.md, "Layers"), so a step cannot regress. Code not yet migrated
reaches the game through `CurrentGame()`. That accessor returns null while no game
exists, `rules` and `game` may not call it, and it is deleted with its last caller. Run
the licensed runtime scenarios after every step.

1. **Finish the game object.** `Game` exists (ARCHITECTURE.md, "The game").
   - Its slot is a static in `game.cpp`. The Application should own it once screens
     receive the game instead of calling `CurrentGame()`.
   - Move the random stream into the game. Party creation draws from the same stream
     today, so settle the random-stream research item first.
   - Fold in the reset functions that still clear per-game state:
     `LeaveGameSession` and `ResetMainScreenStateBlock`. Each goes when its state
     moves into the game.
2. **Finish `GameData`.** The databases and the string table are one immutable value
   (ARCHITECTURE.md, "Layers").
   - The Application should own it once screens receive it instead of calling `Data()`.
   - Tests should build small `GameData` fixtures instead of swapping vectors with
     `ScopedRecords`.
   - Give message ids names.
   - Still outside it: the monster database (read per record), encounter tables (reloaded
     per level), NPC keyword lists and the generic item name cache.
3. **Inventory and party in `rules`.** They take the `Game` and report through its
   events (ARCHITECTURE.md, "The game"); what remains is moving them behind the boundary.
   - Replace `Game&` with a `RuleContext` once the random stream is in the game, and
     take the calls they still make into unmigrated code (conditions, combat targeting,
     NPC states, facts, world drops, sounds) as explicit inputs or events.
   - Presentation is synchronous: the observer shows each event as it is pushed, to keep
     its order among the direct screen calls of unmigrated code. When those are gone,
     the UI drains the log after each command and step instead.
   - This absorbs "characters by slot, items by location" below.
4. **One step.** `Game::advance` runs the world's simulation (ARCHITECTURE.md,
   "Stepping").
   - Speech and the portrait voice state machine (`ProcessDeferredCharacterEvents`,
     `UpdateCharacterEventState`) still run from the frame, mixed with portrait
     presentation; split them and move the speech part into the step.
   - The step reads the clock itself (`w8_get_ticks`, the level's elapsed-time
     accumulators); pass the elapsed time in (`advance(elapsed)`) so that tests can step
     a game deterministically.
   - Game time follows the activity, not the screen: camp commands set `Activity::place`
     instead of the screen router, and `Place::Menu` goes because a covered screen does
     not step.
   - With one step, the world's hold can be read once per step instead of being reported
     from every call site of `SyncWorldAdvancement` (ARCHITECTURE.md, "Advancement").
5. **Presentation owned by screens.**
   - `g_level_block`, the cursor, the targeting mode, `edited_formation` and `item_drag`
     become screen members. `gXStatus` is deleted. `Game::aging_held` mirrors a pending
     `item_drag`; the screen that owns the drag sets it.
   - The HUD panels receive explicit inputs.
   - The main game's redraw bitmask gives way to invalidation by events, as the camp's
     per-page invalidation already does.
   - Camp pages take the reviewed character and the camp's dialogs as explicit inputs
     instead of reaching back through `screen.`; the items page's cell bitmask becomes
     per-cell invalidation.
   - The held-item cursor is derived every frame from the hand and the interaction
     mode, replacing `UpdateHeldItemCursor`/`SetItemCursor` calls after commands.
6. **Conditions, magic and combat in `rules`.**
   - Conditions, spells and attack resolution return results and emit events.
   - `Combat` becomes an owned turn system, signalled by animation and missile events.
     The turn and its waits are values (ARCHITECTURE.md, "The game"); `CombatWaitFor`
     still polls animation, sound and missiles each frame, and actions still resolve
     inside `local_code`.
7. **Story state.** `Game::story` and `Game::message_log` hold it (ARCHITECTURE.md,
   "The game").
   - NPC scripting and level scripts take a context (the world, the party, story state,
     events) instead of reaching globals, and level-script statics become per-level
     script objects.
8. **Saves over values.** Save and load become `encode(GameState, LevelState…)` and
   `decode(...)`. Then delete `W8GlobalStatus` as a runtime type.

## Within the layers

These items keep their own scope, and are easiest after the migration step named in
each.

- **One derivation path for character state.** (After step 3.) Settlement
  (`SettleActiveParty`) coexists with partial recalculations
  (`RecalculateArmorClass`, `RecalculateHitPoints`,
  `RedistributePartyEncumbrance`, `RebuildConditionsAndDerivedStats`,
  `ApplyPartyModifiers`, direct `CalcInitiative` and `CalcAttacks` calls), so
  different callers see different subsets updated.
  - Make derived values (effective attributes, skills, ceilings, armor, attacks,
    load) a pure function of explicit inputs, recomputed in one place.
  - Apply resource transitions (current HP/stamina/spell points following a new
    ceiling, death) as named transitions of the operation that caused them; today
    they happen inside the statistics calculation.
  - Make the retail previous-pass damage bonus used by swings an explicit history
    input, so that a different number of passes cannot change combat results.
- **The party and actions model.** (After step 1; choices move with `Combat` in
  step 6.) A `W8PartySlotRow` groups a member's action state into named parts
  (`remembered` choices, the `committed` action, the action `executing`), but it still
  mixes them with persistent membership, NPC binding and marching order, UI flags
  (`item_action_pending`, `portrait_advance`) and accumulators (`movement_fatigue`).
  Split these by owner:
  - membership in the party, and the action parts in a turn/combat model;
  - presentation in the screens.

  Replace `fOccupied` arrays with real optional members, so that no code asks "is
  this character an occupied member" by address (`slotOf`).
- **Characters by slot, items by location or value.** (Part of step 3.) Inventory
  commands still take `W8Character*` and `W8ItemInstance*` and resolve sources by
  scanning party storage (`locateItem`). Callers should pass party slots,
  `W8ItemLocation` sources and owned values. Split pure placement (onto `W8Party`,
  returning where items went) from acquisition effects (notices, story facts,
  exceptional-item reactions), which the commands that owe them apply explicitly.
  Committed and pending item uses should record the expected item contents, as
  trade rows do, and abandon on mismatch, because pool compaction can silently
  retarget a location.
- **Runtime types with semantic shape; retail layout only in codecs.** (Finished
  by step 8.) `W8Character`, `W8PartySlotRow`, `W8ItemInstance` and formation
  state still carry retail field names and sizes: about 1,230 uses of Hungarian
  names such as `iItemNo`, `fOccupied`, `uiHPMax` and `uiCondition`, plus size
  `static_assert`s. Twenty-eight `wiz8::retail::Record` codecs already exist.
  Finish the move:
  - semantic names, enums, `std::array`/`std::optional`/`inplace_vector` members,
    and no runtime size asserts;
  - codecs own every byte, including unknown fields;
  - the remaining numeric save records and engine resource identifiers get the
    same treatment.
- **Actor references.** About 500
  uses of raw `p3D` actor pointers survive. The location-ID review fixed some
  unloaded-actor cases. Replace borrowed pointers with world-owned handles or
  explicit lookups resolved at use, including trigger-driven reactivation, and add
  regressions for each bug the audit finds.

## Remaining follow-ups

Substantial but contained work.

- **Explicit renderer inputs.** Matrix-mode/load operations and mutable current
  draw state remain in `GpuRenderer`. Replace the implicit protocol with per-draw
  inputs and migrate callers, including `compositor.cpp`.
- **Spatial query value APIs.** `traceSurfaces`/`traceGeometry` return typed
  hits, but `W8OctreeTrace`, line-of-sight and ground-probe callers still use
  mutable output records and surface-zero sentinels. Convert them end to end to
  explicit inputs and optional typed results, preserving world/prop filtering.
- **Explicit scene-pass inputs.** Draws and mesh submissions take explicit
  transforms and draw state, but `GpuRenderer` still carries scene-pass state
  that `WorldScene::render`, `MeshInstance` and `GroundShadow` set, read back and
  restore through scope guards: ambient light, the light and fog spans, the depth
  cue range, the exclusion mask and the pick key and stack. Pass them as a scene
  pass value (or a `MeshState` member) instead, and delete the save/restore
  guards.
- **Delete the compatibility layer.** `include/wiz8/compat/` (`compiler.h` is
  force-included into every game source, plus `native.h`, `unaligned.h` and
  `video.h`) and `src/compat/native_helpers.cpp` should go. Replace them with
  native filesystem, SDL event and loop facilities, and explicit byte decoding at
  the file boundaries.
- **Remove `-fwrapv` and `-fno-strict-aliasing`.** Fix the arithmetic and aliasing
  assumptions these flags protect, using the integer-sanitizer audit job, then drop
  them from `WIZ8_GAME_OPTIONS`.
- **Named strings.** About 770 `g_localized_strings[0x…]` lookups use bare
  indices. Give message ids names (an enum generated from or checked against the
  string table) so rules and screens say which text they show.
- **Errors instead of decomp assertions.** About 720 `Fail(...)` calls keep retail
  assertion strings. Classify each one: an invariant (assert), a recoverable
  data error (an error value or exception at the boundary that read the data), or
  dead. Remove the retail message text.
- **Modules named for what they do.** `local_code/` and `local_screens/` keep
  retail translation-unit names, including fourteen files with spaces (`PC Item.cpp`,
  `Combat Attack.cpp`, …). Move the remaining code into named modules, as
  `inventory`, `party`, `party_stats` and `live_party` already are. `PC Item.cpp`
  should split into item rules, identification/binding and display.
- **Value initialization instead of `memset`/`memcpy`.** About 100 calls remain on
  runtime objects; use value initialization and assignment.
## Easiest follow-ups

Small, local and low-risk.

- `CreateItemIntoHandOrPool` always replaces the hand; rename it to say so.
- `MergeItems`, `DropItemInHand` and `GiveHeldItemToCharacterOrParty` report refusals
  as `NoticeDialog` events; return the refusal to the screen instead.
- Add regression tests with a light combat fixture: a weapon-set swap toggles
  `alternate_hand` once, and spending one charge keeps a committed item use.
- Decide the policy for decomp address annotations (`// FUNCTION: WIZ8 …`): keep
  them only directly above the definition they name, and remove the rest.
- Full storage can still lose an item: an unusable item with a full backpack, a
  full 500-entry pool, no drop position and an occupied hand. Pick a deterministic
  outcome and test it.

## Needs research

Questions to settle, usually against the retail executable, traces or saves,
before the related refactoring changes behavior.

- **Retail semantics to confirm.**
  - Whether swings really use the previous pass's damage bonus in observable play,
    and how many stat passes retail runs per operation.
  - Whether `GiveHeldItemToCharacterOrParty`'s party-first refusal discards the
    held item.
  - Whether `CountCharacterItem`'s backpack count (which reads the equipment slot
    with the same index) affects any quest.
  - Whether conjuring an item into an occupied hand destroys the held item.
  - What retail does with unusable equipment when every store is full.
  - How the alternate-hand toggle behaves when both hand pairs swap.
- **Random-stream ordering.** Loot, NPC theft and combat share one seeded stream.
  Moving notices, events or recalculation can reorder draws. Build a determinism
  harness (seeded scripted runs comparing draw logs) before the outcome-log and
  derivation refactorings.
- **A retail save corpus.** `tests/runtime/fixtures/vi.CHR` was written by the
  port and is not an independent reference. Collect retail saves (start, mid-game,
  late game, NPC-heavy, combat-interrupted) and add round-trip and
  load-then-continue checks.
- **Remaining runtime dependence on retail sizes.** Find what still relies on
  `sizeof(W8ItemInstance) == 0x0c`, the formation state size and similar asserts
  (raw copies, codecs, save-chunk sizing) before giving those types semantic
  members.
- **Platform verification.** macOS (Metal) and Windows (Direct3D 12) builds,
  shaders and gameplay are unverified, and platform CI is not enabled. Establish
  what is needed to run them in CI.

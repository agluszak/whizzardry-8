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

The decompiled program's architecture is partly migrated (see "Migration"). What remains
of it:

- **Shared mutable state.** About 250 mutable `extern` globals remain, and unlayered code
  reaches the game through `CurrentGame()` (about 3,500 uses). The game's state has moved
  into `Game`; the main game's HUD is `MainGameScreen::hud`, which the main game's own
  free functions reach through `MainGameHud()`, and `g_localized_strings` (about 1,080
  uses) is now `Data().strings`.
- **The step still reads the clock.** `Game::advance(elapsed)` is the one step, but the
  code it calls still keeps its own deadlines on the monotonic clock.
- **Rules drive presentation directly.** Inventory and party report events, but other
  rules still call presentation (notices, targeting, the combat panel, screen ids), and
  33 of the 56 `local_code` files include screen headers.
- **Some resets remain.** A game is an object, but leaving one still clears session
  state it does not own yet (`LeaveGameSession`, `ResetForNewGame`,
  `ResetMainGameScreenState`).
- **Combat waits on presentation.** Each step, combat reads the camera, monster
  animations and missiles in flight to decide whether its turn may advance.

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
   - `Combat` owns its pacing and its participants' per-fight state, and steps when
     the presentation reports its actors idle. Action choices stay with the party.
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
   - Fold in the reset functions that still clear per-game state:
     `LeaveGameSession` and `ResetMainScreenStateBlock`. Each goes when its state
     moves into the game.
2. **Finish `GameData`.** The databases and the string table are one immutable value
   (ARCHITECTURE.md, "Layers").
   - The Application should own it and hand `const GameData&` to screens and rules,
     deleting the slot behind `Data()`. What blocks it: about 1,640 `Data()` calls in 90
     unlayered files (37 in `local_code`, 25 in `local_screens`, 11 in `dialog_code`)
     reach the data from code that receives neither the data nor the game. They go as
     screens receive it from the router and rules receive a `RuleContext` (steps 3 and
     6); until then `InstallGameData` fills the one slot.
   - Give message ids names.
3. **Inventory and party in `rules`.** They take the `Game` and report through its
   events (ARCHITECTURE.md, "The game"); what remains is moving them behind the boundary.
   - Replace `Game&` with a `RuleContext`, which carries the game's random stream
     (`Game::rng`), and take the calls they still make into unmigrated code (conditions, combat targeting,
     NPC states, facts, world drops, sounds) as explicit inputs or events.
   - Presentation is synchronous: the observer shows each event as it is pushed, to keep
     its order among the direct screen calls of unmigrated code. When those are gone,
     the UI drains the log after each command and step instead.
   - This absorbs "characters by slot, items by location" below.
4. **One step.** `Game::advance(elapsed)` is the game's step (ARCHITECTURE.md,
   "Stepping"); its own countdowns, the frame delta and the environment clock take the
   elapsed time it is given. What it calls still reads the monotonic clock: the surprise
   fade, autosave pacing, the combat turn's `action_clock` and waits, the monster
   manager, props and monster generators (`W8GameTimer`/`W8IntervalGate` on the shared
   clock), missiles, and speech's dispatch delays, recent-event window and mouth frames.
   Move each onto the elapsed time the step passes down so that a test can step a game
   deterministically; then add a test that steps a small game with explicit times.
5. **Presentation owned by screens.** The main game repaints what its HUD's record
   (`MainGameRepaint`) holds, game code reports changes as events, the draw path
   receives the HUD, and the pointer derives the held-item cursor every frame
   (ARCHITECTURE.md, "Controls and painting" and "Input and ownership"). What remains:
   - The main game's own free functions in `local_screens` (17 files, about 850 uses,
     most in `MainGameScreen.cpp` and `MGSTextBox.cpp`) reach the HUD through
     `MainGameHud()`, including to mark repaint parts; they should receive it from the
     screen, as its members or as parameters, and `MainGameHud()` then goes. Outside
     the screens three readers remain: `Targeting.cpp` reads the submenu draft
     (`selection_kind`, `selection_settled`) and whether a main game exists, because
     `SelectCombatAction` reads the spell, item and submenu drafts the screens own;
     `QuoteManager.cpp`'s portrait bubbles read the review transition and pending
     portrait refreshes; and `PartyCamera::ComputeTrackingOrientation` reads the
     viewport mode, which the screen should hand the camera when it changes (after the
     VulkanSceneGraph renderer migration, which rewrites the world code).
   - The pointer (the shown cursor and its request, in `Screens.cpp`) and the targeting
     mode (`TargetingMode()`, in `MainGameScreen.cpp`) are module state. The camp shares
     both with a main game suspended under it, and a camp opened from party creation
     has no main game, so they become members of one pointer owner that both screens
     borrow, not of either screen.
   - The HUD panels below `ApplyMainGameRepaint` (portraits and their bars, the text box,
     submenu, formation board, monster list, radar) still read the HUD, the game and
     module state themselves; give each the values it draws.
   - Game code still calls presentation for targeting (`SetTargetingMode`) and the
     combat panel (`RequestRefreshPartyState` in `Combat Hostility.cpp`,
     `MonsterManager.cpp`, `world.cpp` and `entities/cursor.cpp`, which the existing
     `CombatPanelChanged` event already presents).
6. **Conditions, magic and combat in `rules`.**
   - Conditions, spells and attack resolution return results and emit events.
   - `Combat` is owned by the game (`std::optional<Combat>`, `beginCombat`/`endCombat`)
     and its turn is a value (ARCHITECTURE.md, "The game"). Entities report missile
     hits, landings and release frames through the world's `SimulationSink`, monster
     scripts and triggers issue game commands, and the hit sound releases its wait when
     it finishes. What remains:
     - `CombatWaitFor` still reads presentation every step: the camera's transition,
       each combat monster's cycle, frame and facing (`AnyCombatMonsterBusy`), and the
       missiles in flight (`BlocksEndingCombat`, which also releases the engaged
       missile). For the step to read only values, the presentation has to report each
       transition (a camera transition ending, a monster settling, a missile's impact
       cycle starting or its flight ending), including those the monster manager
       causes earlier in the same step, without a maintained count of busy actors.
     - `UpdateCombat`, the scheduler and the action executors reach the game through
       `CurrentGame()` and resolve actions inside `local_code`; they become operations
       of `Combat` that take the game and the rules' context.
7. **Story state.** `Game::story` and `Game::message_log` hold it, and NPC scripting and
   the level scripts act on a `ScriptContext` (ARCHITECTURE.md, "Scripts").
   - The fact API (`GetFact`/`SetFact`, about 210 calls) still reaches the game;
     `SetFact` builds the context for the fact's consequences. Facts should be read
     from the story and set through the context.
   - `NPC Manager.cpp` (NPC states, bindings, notices; about 160 `CurrentGame()` uses)
     and the helpers scripts call (trigger, entity and NPC lookups, location
     variables, `SpawnMonsters`) still reach the world and story through globals.
   - The fade and monster-cycle completions are plain function pointers, so the NPC
     story callbacks they run read `CurrentGame()`.
   - Dialogue state is still module state: `g_npc_dialogue`, the Al-Sedexus scene and
     the quote presentation (`npc_dialogue_view.cpp`). The game should own the first
     two and the dialogue screen the last.
   - `IsPartySlotEligible` (about 50 callers) is a party query that reads the game; it
     belongs in `rules`, taking the party.
8. **Saves over values.** The GSTA section and the STAT record are codecs over values
   (`save_codec.h`; ARCHITECTURE.md, "Saves"), and `W8GlobalStatus` is gone: the game's
   status is a `GameStatus` value that carries its unknown bytes. What remains:
   - The other game sections (TEXT, TVAR, NPCI, NPCT, NPCF, FATA, JRNL, HYPN) are still
     written from and read into live state through `CurrentGame()`. Give them `encode` and
     `decode` over `Story`, the message log and the monster-control effect, so that a save
     is `encode(GameState, LevelState…)`.
   - A level's LVLS group (MONS, ITEM, AUTO, TRIG, APST, CUBS, MGNS, LCKS, AMBS, PART,
     LGHT) is captured from and applied to its world. Decode it into a `LevelState` value
     (`W8LoadedLevelStatus::Data` is the start) and build the world from that value.
   - A save copies the CHAR chunks and the other levels' groups from `CurrentGame.SAV` as
     bytes. Once level states are values, the game can own them instead.
   - `GameStatus` still has retail-era names (`uiTurnsElapsed`, `status_ints`) and holds
     interface state (`text_line_cursor`, `selected_party_member`, `world_cursor_gate`).
     Split it by owner.

## Within the layers

These items keep their own scope, and are easiest after the migration step named in
each.

- **The party and actions model.** (After step 1.) A `W8PartySlotRow` groups a
  member's action state into named parts (`remembered` choices, the `committed`
  action, the action `executing`). They belong to the party, not to `Combat`: the
  retail party record saves all three, and members choose and carry out actions
  outside fights too (item uses and spells in the world and in camp). What a fight
  keeps per member is `Combat::characters`. The row still mixes the action parts with
  persistent membership, NPC binding and marching order, UI flags
  (`item_action_pending`, `portrait_advance`) and accumulators (`movement_fatigue`).
  Split these by owner:
  - membership and the action parts in the party;
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
- **Runtime types with semantic shape; retail layout only in codecs.** (After step 8.)
  `W8Character`, `W8PartySlotRow` and `W8ItemInstance` still carry retail field names:
  about 1,230 uses of Hungarian names such as `iItemNo`, `fOccupied`, `uiHPMax` and
  `uiCondition`. Verbatim codec copies check field widths, and items, formations and
  NPC stock entries have their own disk records, so those types may change shape. Retail
  disk records still embed other runtime types, which therefore keep their retail
  layout: `W8Enchantment`, `W8CharacterAttribute` and `W8CharacterResistance` (asserted
  in `retail_records.h`), `W8GameplayModifierBlock`, `W8PartyThreatRecord`, `W8Dice`,
  `ItemProperties` and `ItemEffects`, and the runtime enums used as disk field types.
  `W8GameSettings` is read and written raw as the configuration file's CNFG chunk.
  Finish the move:
  - semantic names, enums, `std::array`/`std::optional`/`inplace_vector` members, and
    no runtime size asserts;
  - a disk record and codec for each embedded runtime type;
  - the remaining numeric save records and engine resource identifiers get the
    same treatment.
- **Actor references.** About 500
  uses of raw `p3D` actor pointers survive. The location-ID review fixed some
  unloaded-actor cases. Replace borrowed pointers with world-owned handles or
  explicit lookups resolved at use, including trigger-driven reactivation, and add
  regressions for each bug the audit finds.

## Remaining follow-ups

Substantial but contained work.

- **Remove `-fsigned-char`.** About 160 locals, parameters and casts plus some
  record fields (`restore_level`, NPC dispositions, theft scores, level bands,
  lock pins) keep small signed numbers in plain `char`. Give them `signed char` or
  `std::int8_t` (text stays `char`), then drop the flag from `wiz8_native_settings`.
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
## Easiest follow-ups

Small, local and low-risk.

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
- **A retail save corpus.** `tests/runtime/fixtures/vi.CHR` was written by the
  port and is not an independent reference. Collect retail saves (start, mid-game,
  late game, NPC-heavy, combat-interrupted) and add round-trip and
  load-then-continue checks.
- **Platform verification.** macOS (Metal) and Windows (Direct3D 12) builds,
  shaders and gameplay are unverified, and platform CI is not enabled. Establish
  what is needed to run them in CI.

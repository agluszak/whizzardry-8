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
  reaches the game through `CurrentGame()` (about 4,500 uses) and the data through
  `Data()` (about 1,650). The Application owns both; the game's state has moved into
  `Game`; the main game's HUD is `MainGameScreen::hud`, reached through `MainGameHud()`,
  and `g_localized_strings` is now `Data().strings`.
- **The step still reads the clock in places.** `Game::advance(elapsed)` is the one step
  and most of what it runs keeps time on the step's time, but some world entities still keep deadlines on the monotonic clock or the shared timers.
- **Rules drive presentation directly.** Inventory and party report events, but other
  rules still call presentation (notices, targeting, the combat panel, screen ids), and 33 of the 57
  `local_code` files include screen headers.
- **The level's module state outlives the game.** Destroying a game does no departure
  bookkeeping: the main game, discarded before its game ends or is replaced, leaves the
  level (`LeaveLevel`), which releases the monster manager, item lists, world cursor, sky,
  camera sway and game-time accumulator that live outside `Game`.
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
   - The Application owns the session's game. A new game begins in party creation with
     the starting party, a load begins one from the save, and quitting to the menu
     destroys it. Nothing is reset field by field: what belongs to a play-through is a
     member of `Game`.
   - Party selection, import and character creation edit the party of a game still in
     party creation, so menus outside a game never touch live state.
   - A load decodes the save into values (`GameState`, step 8) and constructs the game
     from them, instead of filling a begun game section by section.
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
reaches the game through `CurrentGame()`. That accessor throws `std::logic_error` while no game
exists, `rules` and `game` may not call it, and it is deleted with its last caller. Run
the licensed runtime scenarios after every step.

1. **Finish the game object.** The Application owns the game through a `GameSession`, and
   no reset function remains (ARCHITECTURE.md, "The game"). What remains:
   - `CurrentGame()` reads the one live session, which registers itself. It has about
     4,520 uses in 101 files: 2,370 in 40 `local_code` files, 1,890 in 26
     `local_screens` files, and the rest in `src/wiz8` (77), `world` (68), `entities`
     (62), `dialog_code` (18), `spatial` (13), `combat` (13), `level_specific_code`,
     `platform` and `audio`. The router hands the session to every screen it builds, but
     only the main menu, party selection and the Please Wait screen keep it; the other
     screens should take the game from it, and the code below them the game or a
     `RuleContext` (steps 3, 6 and 7).
   - `Game` holds interface state of the main game screen, because the screen outlives a
     load: `npc_interaction` (the dialogue's layouts, trade tables, quote bubble and
     `NpcDialogueUi`; its transcript and staged notice are game state),
     `npc_presentation` and `npc_speech`, and the portrait animation in
     `monster_manager_entries`. Move them to the screen with step 5.
   - The main game's first entry starts the world running, restarts the gameplay timer,
     clears the aging hold and readies the portrait slots (`ResetTargetingState`, whose
     idle timers draw from `Game::rng`). Those belong to the game's own start once the
     portraits are the screen's.
2. **Finish `GameData`.** The databases and the string table are one immutable value
   (ARCHITECTURE.md, "Layers"), which the Application owns.
   - `Data()` reads the data the Application installs (`InstallGameData`). It has about
     1,650 uses in 92 unlayered files: 758 in 37 `local_code` files, 535 in 25
     `local_screens` files, 198 in 11 `dialog_code` files, 122 in `src/wiz8`, and the rest
     in `level_specific_code`, `entities`, `world` and `combat`. They reach the data from
     code that receives neither the data nor the game, and go as screens receive it
     from the router and rules receive a `RuleContext` (steps 3 and 6).
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
4. **One step.** `Game::advance(elapsed)` is the game's step and `Game::time` its
   accumulated time (ARCHITECTURE.md, "Stepping"); a test steps an empty level with
   explicit times (`game_timer_test.cpp`). What the step calls still keeps its own time
   in these places:
   - World entities on the shared timers (`W8GameTimer`/`W8IntervalGate`, real time less
     pauses): monsters' script delay, light and fade timers, triggers and their
     countdowns, the level scripts' gates, the party camera's manual-input hold, ambient
     sound fades and camera shake. Their saves store progress, so each can move to
     step-time countdowns as `GeneratorInterval` did; then `GameClockTicks` serves only
     presentation.
   - World code on the monotonic clock: world items' settle countdown
     (`world_item.cpp`), keyframe tracks, the environment's lighting transition, random
     ambient sounds' next play, trigger item-group seeds and random subcycles, spell
     cooldowns (`Magic.cpp`), and the portrait idle clock the rest sequence arms
     (`GameplayTime.cpp`). The renderer's light animations and texture playback stay on
     the monotonic clock until the VulkanSceneGraph migration settles their owner.
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
   - `Combat` is owned by the game (`std::optional<Combat>`, `beginCombat`/`endCombat`)
     and its turn is a value stepped by `Combat::step`, with the scheduler and the
     actions it runs as `Combat` members in `combat/` (ARCHITECTURE.md, "The game").
     Its countdowns run on the step's elapsed time. Entities report missile hits,
     landings and release frames through the world's `SimulationSink`, monster scripts
     and triggers issue game commands, the hit sound releases its wait when it
     finishes, and the fight, attack resolution, conditions and spells report their
     notices as events (`TextNotice`, `CharacterNotice`, `TextBoxModeChange`).
     What remains:
     - Presentation calls left in those rules: redraw bitmasks (`RequestRedraw`,
       `RequestPartySlotRedraw`, `RequestRedrawParty`, about 40 in `combat/`, conditions
       and spells), sounds (`wiz8::sound::play`, `combat/sounds.cpp`), the camera
       (`PointCameraAtMonster`, `PointCameraAtCombatTarget`), the text box's queries and
       edits (`GetTextBoxMode`, `AppendToLastTextLine`, `HighlightTextBoxRange`,
       `RefreshTextBoxMode`, `SelectTextBox`), the character screen the scheduler opens
       for an equip round (`OpenCharacterScreenForPartySlot`) and the selection
       auto-advance (`AutoAdvanceSelectedCharacter`). `Health Stamina Mana.cpp` still
       posts its notices directly.
     - Attack resolution (`StartCharacterAttack`, `ResolveCharacterAttack`,
       `StartMonsterAttack`, `ContinueMonsterAttack`), conditions and spells are free
       functions that reach the game through `CurrentGame()`; so do the fight's other
       operations in `combat/fight.cpp` (rounds, surprise, the end check, action
       choices). They should take the game, then a `RuleContext`, and return what
       happened instead of reporting it.
     - `Combat::waitingOn` still polls presentation each step: the camera's
       transition, each combat monster's cycle, frame and facing, and the missiles in
       flight (`BlocksEndingCombat`, which `Combat::step` also polls to release the
       engaged missile). These cannot be signalled exactly yet: a monster is busy
       until its attack cycle passes half its frames or its yaw comes within an
       epsilon of its target, and a missile until its impact cycle starts, so each
       would need an event from inside the entity's animation and navigation update,
       including the transitions the monster manager causes earlier in the same step.
       Do it with the entity rework that follows the scene-graph renderer migration,
       without a maintained count of busy actors.
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
8. **Saves over values.** Every game section (GSTA, TEXT, TVAR, NPCI, NPCT, NPCF, FATA,
   JRNL, HYPN, CHAR) and the STAT record are codecs over values (`save_codec.h`;
   ARCHITECTURE.md, "Saves"), and the game owns its stored characters. What remains:
   - A level's LVLS group (MONS, ITEM, AUTO, TRIG, APST, CUBS, MGNS, LCKS, AMBS, PART,
     LGHT) is still captured from and applied to its world: `SaveStatusHeader` writes the
     sections from live objects, and `ReadLevelStatus` decodes MONS, ITEM, CUBE, MONG,
     AUTO, AMBS, PART and LGHT into `W8LoadedLevelStatus::Data` but reads TRIG, APST,
     LCKS/LOCK and TRES straight into the replacement world. Give those four sections
     value records, turn `Data` into a `LevelState` value with `encode` and `decode`, and
     build the world from it.
   - `CurrentGame.SAV` is still the store for levels the party has left: departure
     rewrites the departing level's group into it, arrival reads the group back, and a
     save copies the other levels' groups from it as bytes. Once level states are values,
     the game owns them (keeping the groups of levels it never decoded intact) and a save
     encodes them, so `CurrentGame.SAV` goes away.
   - `GameStatus` still has retail-era names (`uiTurnsElapsed`, `status_ints`) and holds
     interface state (`text_line_cursor`, `selected_party_member`, `world_cursor_gate`).
     Split it by owner. The dialogue transcript the NPCI section saves still lives in the
     NPC dialogue's interface state (`W8NpcInteractionState`); it belongs to `Story`.

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
  `W8SpellEffectDefinition` (asserted in `save_codec.cpp`), `ItemProperties` and
  `ItemEffects`, and the runtime enums used as disk field types.
  Finish the move:
  - semantic names, enums, `std::array`/`std::optional`/`inplace_vector` members, and
    no runtime size asserts;
  - a disk record and codec for each embedded runtime type;
  - the remaining numeric save records and engine resource identifiers get the
    same treatment.
- **Actor entity links.** A monster record reaches its scene entity through
  `W8MonsterInfo::p3D` (about 500 uses) and a world item through `W8WorldItem::p3D`.
  These links stay within one world and are kept in step with the entities ("Worlds and
  levels"), but only an active record is sure to have one, and most callers dereference
  `p3D` without checking. Give the records a checked accessor for their entity (or let
  the record own it) and migrate the callers, starting outside the combat files.

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
  retail translation-unit names, including nine files with spaces (`PC Item.cpp`,
  `Magic Effects.cpp`, …). Move the remaining code into named modules, as
  `inventory`, `party`, `party_stats`, `live_party` and `combat/` already are. `PC Item.cpp`
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

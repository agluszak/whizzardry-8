# Roadmap

The goal is a clean architecture: gameplay rules that read like the game, owned
state with obvious lifetimes, presentation that consumes outcomes, and retail
formats confined to codecs. [AGENTS.md](AGENTS.md) states the principles;
[ARCHITECTURE.md](ARCHITECTURE.md) describes what exists now. This file is the
single list of remaining work. Delete an item when it is done, rather than
marking it.

Items are grouped by difficulty, with the ones that first need research kept
separately. Most of the hardest items overlap; doing them in the listed order
avoids rewriting the same code twice.

## Hardest follow-ups

These change ownership or data flow across many modules. Each one needs a
design agreed up front and should land in reviewable slices, with CI (including
the licensed runtime scenarios) green after each slice.

- **A game session instead of process globals.** `g_status` (about 1,950 uses;
  a 100-field `W8GlobalStatus` holding the party, clocks, story progress and
  decoded save fields) and `gXStatus` (about 670 uses; a 50-field grab bag of UI
  modes, targeting, timers and flags) are the de facto architecture. Further
  globals include `g_combat`, `g_level_block` (860 uses in 30 files), screen states
  and `g_settings`; headers declare about 340 `extern` globals in all. Introduce a `GameSession` that owns the party, story state
  (facts, quests, level progress), the clock, the world session and combat; UI
  state belongs to its screens. Live commands take the session, or the part they
  need, explicitly. The retail GSTA status block becomes a codec over session
  parts. Done when `gXStatus` is gone and `W8GlobalStatus` exists only as a
  decoded record.
- **Gameplay produces outcomes; presentation consumes them.** Rules call
  `ShowNotice`/`PostCharacterNotice` (about 360 sites), `RequestRedraw` (about
  180), cursor updates and screen state directly, so gameplay modules include
  screen headers. Commands should return outcome values, or append typed events
  to an ordered outcome log, which screens present in order. Keep the
  retail-visible order of notices and voices, and never move a random draw.
  Done when `local_code` gameplay and `inventory`/`party` modules no longer
  include `local_screens/` headers.
- **One derivation path for character state.** Settlement
  (`SettleActiveParty`) coexists with partial recalculations
  (`RecalculateArmorClass`, `RecalculateHitPoints`, `RedistributePartyEncumbrance`,
  `RebuildConditionsAndDerivedStats`, `ApplyPartyModifiers`, direct `CalcInitiative`
  and `CalcAttacks` calls), so different callers see different subsets updated.
  - Make derived values (effective attributes, skills, ceilings, armor, attacks,
    load) a pure function of explicit inputs, recomputed in one place.
  - Apply resource transitions (current HP/stamina/spell points following a new
    ceiling, death) as named transitions of the operation that caused them; today
    they happen inside the statistics calculation.
  - Make the retail previous-pass damage bonus used by swings an explicit history
    input, so that a different number of passes cannot change combat results.
- **The party and actions model.** `W8PartySlotRow` mixes several unrelated
  kinds of state:
  - persistent membership, NPC binding and marching order;
  - combat choices (pending, committed, remembered and spell actions, targets);
  - UI flags (`item_action_pending`, `portrait_advance`) and accumulators
    (`movement_fatigue`).

  Split these by owner:
  - membership in the party, and choices in a turn/combat model;
  - presentation in the screens.

  Replace `fOccupied` arrays with real optional members. Give character-creation
  and import drafts their own owner instead of unoccupied party slots, so that no
  code asks "is this character an occupied member" by address (`slotOf`).
- **Characters by slot, items by location or value.** Inventory commands still
  take `W8Character*` and `W8ItemInstance*` and resolve sources by scanning party
  storage (`locateItem`). Callers should pass party slots, `W8ItemLocation`
  sources and owned values. Split pure placement (onto `W8Party`, returning where
  items went) from acquisition effects (notices, story facts, exceptional-item
  reactions), which the commands that owe them apply explicitly. Committed and
  pending item uses should record the expected item contents, as trade rows do,
  and abandon on mismatch, because pool compaction can silently retarget a
  location.
- **Runtime types with semantic shape; retail layout only in codecs.**
  `W8Character`, `W8PartySlotRow`, `W8ItemInstance` and formation state still
  carry retail field names and sizes: about 1,230 uses of Hungarian names such as
  `iItemNo`, `fOccupied`, `uiHPMax` and `uiCondition`, plus size `static_assert`s.
  Twenty-eight `wiz8::retail::Record` codecs already exist. Finish the move:
  - semantic names, enums, `std::array`/`std::optional`/`inplace_vector` members,
    and no runtime size asserts;
  - codecs own every byte, including unknown fields;
  - the remaining numeric save records and engine resource identifiers get the
    same treatment.
- **Screens as objects with lifetimes.** Routing dispatches through a
  function-pointer table (`g_screen_handlers` enter/frame/leave/finalize), and
  screen state lives in globals (`g_camp_screen`, `g_main_game_state`, `use_item`,
  …) that other modules reach into. The router should own `Screen` objects, and
  suspension and return should be values. In the camp:
  - pages reach back through `screen.` for redraw bits, the reviewed character
    and dialogs;
  - the item-action buttons belong to the items page;
  - redraw state is still retail bitmasks (`0x7ff`, `0x10000`).

  Replace these with per-page invalidation and explicit page inputs. Derive the
  held-item cursor every frame from the hand and the interaction mode instead of
  calling `UpdateHeldItemCursor`/`SetItemCursor` after commands.
- **Combat as an owned turn system.** `Combat.cpp` and `Combat Attack.cpp`
  (about 6,000 lines) operate on global party rows and monster records:
  - fatigue costs travel through a `-1` sentinel;
  - equipment changes reach combat through `ReconcileHandChange`, a repair of
    berserk, targets and the alternate-hand toggle.

  Combat should own its participants' choices and respond to an equipment
  change as a combat-side rule; costs and outcomes are values.
- **Actor references.** About 500 uses of raw `p3D` actor pointers survive. The
  location-ID review fixed some unloaded-actor cases. Replace borrowed pointers
  with world-owned handles or explicit lookups resolved at use, including
  trigger-driven reactivation, and add regressions for each bug the audit finds.

## Remaining follow-ups

Substantial but contained work.

- **Spatial query value APIs.** `traceSurfaces`/`traceGeometry` return typed
  hits, but `W8OctreeTrace`, line-of-sight and ground-probe callers still use
  mutable output records and surface-zero sentinels. Convert them end to end to
  explicit inputs and optional typed results, preserving world/prop filtering.
- **Explicit renderer inputs.** Matrix-mode/load operations and mutable current
  draw state remain in `GpuRenderer`. Replace the implicit protocol with per-draw
  inputs and migrate callers, including `compositor.cpp`.
- **World and session ownership.** Review the remaining borrowed camera and
  static-mesh aliases against their owners, and move persistent cursor
  selection/projection state into the owning world or session.
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
- **Level scripts.** `level_specific_code/` reads and writes the level block,
  story facts and the party through globals. Once the session exists, give scripts
  an explicit context (the world, the party, story state and outcomes).

## Easiest follow-ups

Small, local and low-risk.

- `CreateItemIntoHandOrPool` always replaces the hand; rename it to say so.
- `MergeItems` and a few other inventory commands still show camp notices
  themselves; return the refusal to the screen.
- Merge the overlapping screen-routing queries (`RequestedScreen`,
  `GetPendingScreenState`, `IsScreenTransitionPending`); `Screens.h` also
  redeclares the latter two next to `Gameloop.h`.
- `ActivateGameStatus` is a one-line wrapper around assignment; inline it.
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

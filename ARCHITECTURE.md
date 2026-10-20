# Architecture

This file describes the contracts the code relies on now. Remaining work is in
[ROADMAP.md](ROADMAP.md); principles are in [AGENTS.md](AGENTS.md).

## Compatibility boundary

Only serialized formats are compatibility contracts: retail saves must load, and
saves we write must load in retail. Save and game-data records have fixed-width
disk images and `wiz8::retail::Record` codecs (`retail_save_records.h`,
`retail_geometry_records.h`, `retail_text_records.h`). Unknown and padding bytes
that belong to a file live in `retail_opaque_records.h` and round-trip unchanged.
Runtime types do not mirror retail memory layout. Runtime strings are UTF-8;
retail UTF-16LE fields are decoded and encoded explicitly by utf8proc-based
codecs, including malformed-input replacement and fixed field limits.

The game only reads compiled OCT/WPT/PVL geometry; there is no level compiler or
editor. Shipped `.rlk` region links remain readable.

## Modules

| Module | Owns |
| :--- | :--- |
| `platform/` | Window, SDL input events, filesystem roots, timers and game clocks, the seeded RNG, assertions. |
| `canvas/` | CPU drawing on `SDL_Surface&`: images, STI sprite sheets, fonts, shading tables. Knows nothing about the GPU or the framebuffer. |
| `audio/` | The sound service (`Playback` values), positional `Sound3D`, `AmbientAudioSystem`, sound events and mouth-gap lip sync. |
| `render/` | `GpuRenderer` (SDL GPU pipelines and submissions), `WorldScene` typed draw lists, concrete `Mesh`/`MeshInstance`/`Texture`/`Material`, variant-animated `Light`, particles, ground shadows, fog, picking, and the compositor: one 640×480 UI canvas texture plus flat `OverlayQuad` values. |
| `spatial/` | `LevelSpatialIndex` (compiled octree and collision banks), `SurfaceIndex`, collision and traces with per-query scratch, the navigation mesh and `Navigator`. |
| `world/` | `World` (scene, environment, spatial index, audio, population, entities), `PartyCamera` and `PartyMotion`, `EnvironmentState`, encounters, `level_loader` and `level_session`. |
| `entities/` | Concrete `Monster`, `Missile`, `SpellVisual`, `Prop`, `WorldItemEntity`, triggers and locks, composed from `CyclePlayer`, `Navigator` and `KeyframeTrack` values. |
| `native/` | Application loop, game events, key bindings, Bink/FFmpeg video. |
| `wiz8/` | Gameplay rules, screens, dialogs and level scripts. |

## Runtime contracts

- **Randomness.** All game randomness uses the explicitly seeded MSVC 32-bit
  sequence with 15-bit draws. Loot and NPC theft keep their shared-stream
  reseeding and draw order, so moving a draw changes gameplay.
- **Pixels.** The software UI framebuffer is fixed ARGB1555. Assets and video
  may use other SDL formats; palette, TGA and STI decoding is explicit.
- **Threads.** Scene updates, traversal and rendering run on the game thread;
  audio completion callbacks are dispatched there by the sound service.
- **Shaders.** SDL GPU selects Vulkan, Metal or Direct3D 12 using embedded SPIR-V,
  MSL or DXIL. Normal builds compile GLSL to SPIR-V and embed checked-in Metal and
  Direct3D outputs; source digests make CMake reject stale outputs after a GLSL
  edit. Regenerate them on Linux x86-64 with the pinned SDL_shadercross tool,
  using the same Conan environment and build type as the game:

  ```sh
  cmake -S tools/shadercross -B build/shadercross -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$PWD/build/linux/conan/conan_toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang-23 -DCMAKE_CXX_COMPILER=clang++-23
  cmake --build build/shadercross --target shadercross --parallel 2
  python3 tools/shadercross/generate.py \
    --shadercross build/shadercross/_deps/shadercross-build/shadercross
  ```

  Linux CI regenerates both formats and compares the outputs.

## Worlds and levels

- The primary and sky slots own their worlds; queries borrow those owners. World
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
- Level master callbacks return completion directly. Callbacks appended during a
  dispatch wait for the next frame.

## Gameplay operations

- **Actions.** Spell and item pickers own an action draft while open; cancelling
  discards it, and committing copies the completed draft. Committed and remembered
  choices own action values. Target preview and execution share target geometry
  and sight rules but never share random power or damage results.
- **Trade.** Trade rows keep a source location and the expected item contents;
  compaction or changed contents invalidates the selection. Identical stacks are
  fungible, and there is no global item identity. Purchasing prepares stack merges,
  capacity and overflow placement before committing stock and payment.
- **Inventory.** `inventory.h` is the active party's inventory model. Each command
  states the locations it replaced or emptied and finishes once: settlement
  (unusable equipment comes off in slot order and is stored like a received item,
  statistics and shared load are recalculated, an exhausted member dies), combat
  choices follow a weapon-hand change, a committed item use loses a replaced item,
  and the cursor shows the hand. A change of quantity is not a replacement.
  `W8Party` owns storage primitives with no gameplay; `party_stats.h` derives
  values from explicit rules; `live_party.h` supplies the active session's rules
  and applies what follows. `PC Item.cpp` keeps per-item rules. Swings read the
  damage bonus left by the previous calculation, as retail does.
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
  requested and the router destroys it when it is discarded, so a suspended
  main game keeps its level block and HUD while the camp, character and options
  screens rebuild their per-visit state on every entry. Other modules reach a
  screen through `FindScreen<T>()` or the named accessors (`MainGame()`,
  `CampScreenState()`, …), never through a global slot. The intro film,
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
- `MainGameScreenState` owns its HUD buttons, input layer and active submenu as
  values. Suspension disables their input; full game exit destroys them.
- The camp screen owns routing, the header, portraits and dialogs. Its items,
  stats, skills and spells pages own their state and controls behind a small
  lifecycle, and the items page repaints cells from what they last showed.

## Tests

CTest contains asset-independent fixtures and licensed runtime scenarios (label
`runtime`: video, character creation, game start, movement, quick-save, combat,
save-fixture creation and loading). Runtime scenarios need `WIZ8_ASSET_ROOT` and a
usable SDL GPU backend; CI provisions the licensed assets. A skipped licensed test
does not establish behavioral parity. `tests/native/gpu_lifecycle.md` documents a
known NVIDIA driver leak.

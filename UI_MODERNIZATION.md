# UI modernization: scoped input, screen-owned painting

The destination is the smallest UI infrastructure that removes SGP and the
recovered control framework while preserving Wizardry 8 behavior and artwork.
A universal visual widget tree is not a requirement. Drawable objects need not
be interactive, and gameplay hit areas need not be widget subclasses.

## Target architecture

- An application-owned `UiRouter` eventually owns hit testing, hover, pointer
  capture, keyboard focus, tooltip timing and modal priority.
- Each screen, dialog or temporary gameplay mode owns a `UiLayer`. Its persistent
  targets describe input geometry and callbacks without owning any artwork.
- Small ordinary controls own button, slider and text-editing behavior. Screens
  own those controls and their resources by value or exclusive ownership.
- Screens paint their original sprites and text to explicit `SDL_Surface&`
  targets and own their draw order and invalidation.

There must be one dispatcher throughout migration. `PumpUiInput` continues to
consume the normalized SDL input stream. Do not introduce a second event queue,
hover manager, capture state or focus manager beside RegionManager.

## Implemented foundation

`include/wiz8/ui_layer.h` provides `UiLayer`, `HitTarget` and typed pointer events.
The implementation in `local_code/UiLayer.cpp` is an explicitly temporary adapter
over RegionManager. Numeric region/set identities stay inside the adapter;
screen handlers receive geometry, the changed button, held/armed button masks,
wheel motion and SDL click counts. Keyboard/text events pass through to the
existing screen and focused-editor handling.

A layer owns its targets. `add` returns a borrowed target reference that remains
stable until removal or layer destruction. Targets support geometry updates,
enable/disable, dynamic tooltip text and explicit capture release for gameplay
that sends a drop to a different target. An empty handler consumes pointer
input, allowing a nonvisual blocker. A handler returning `Pass` leaves the event
for the screen's unhandled policy; it does not search lower targets.

- SDL rectangles are half-open. Retail inclusive rectangles must be converted
  with `width = right - left + 1` and `height = bottom - top + 1`.
- Circles include their circumference; their local coordinates are relative to
  the center. Rectangle local coordinates are relative to the top-left corner.
- Lower layer orders win, then earlier layer creation. Higher target priorities
  win within a layer, then earlier registration. The default order is the
  backend's existing dynamic-set order.
- Down captures in the existing dispatcher. Up outside still reaches the source.
  `captured_buttons` lets ordinary buttons reject an unarmed release; gameplay
  drop handlers may deliberately accept one after the source releases capture.
- Disabling a target/layer clears its backend hover/capture and displayed help.
  Removing targets or destroying the layer releases registrations and slots.
- Tooltip providers run when the existing delayed tooltip is displayed. They
  should only read state and return localized text, not mutate UI ownership.

Callbacks use `AfterUiDispatch` for target removal, owner destruction and screen
transitions. It runs actions after the outermost pointer/hover transaction,
including nested dispatch and the screen event policy, or immediately when outside
dispatch. The input pump yields after committing these changes so a departed
screen does not receive subsequent queued events. Captured owners
must remain alive until that point. It is a deferred mutation boundary, not an
input queue. Direct self-destruction from a handler is not supported.

The adapter intentionally retains the backend's modal scopes, text-field
precedence exception, cursor-hotspot handling and sound behavior. It is not a
completed `UiRouter` or control consolidation. Those semantics
must be migrated with their real callers, not duplicated in the adapter.

## Main-menu prototype

The main menu now owns one `UiLayer` and six callbacks registered from its
`SDL_Rect` bounds table. `MainMenuItemWidget` and the screen's `Controls` panel
are deleted. Drawing and keyboard shortcuts remain screen-specific, and pointer
activation requests run after dispatch. The disabled Load item still occupies
its hit area, draws disabled artwork and cannot activate. A release without a
matching press or outside the item cannot activate it.

The screen still owns its existing message dialog. This is the simple prototype,
not evidence that text-entry dialogs have completed migration.

## Formation prototype

Formation now owns a layer for its fifteen cells, three actions and background.
The legacy region template table and three region callback entry points are
removed, including their creation, re-enable and destruction callers. Existing
`W8TextControl` objects retain painting and sprite state but have no regions.
The gameplay controller receives typed pointer events directly.

The 250 ms hold threshold, leave-to-drag gesture, cursor composition, cell
selection, movement/swap legality and reset/accept/cancel rules remain in
`MGSFormation.cpp`. Drag start explicitly releases capture so the destination
receives Up. The backend no longer resurrects held-button flags after a leave
callback releases capture. Accept and close defer panel destruction until the
button handler has returned; teardown removes input before its visual controls.

This is an input migration, not the button consolidation or final painting
migration. NPC keyword replies now exercise modal text entry, as described below.

## Modal input and text editing

`UiLayerMode::Modal` and `setModal` use the backend's existing exclusive scope.
Dialogs own their layer directly. The transitional dialog frame and buttons
still share that layer through their current registration path; no second modal
stack is introduced.

`TextFieldGroup` owns ordinary fields and layer targets, not `W8Widget` instances.
Groups can attach explicitly to a modal layer. Existing callers inherit their
current dialog/control scope inside the adapter. UTF-8 editing, selection, tab
order, capacity limits and SDL text-input activation retain the existing editor.
The cursor-tip precedence exception remains internal to text-field targets.

NPC reply dialogs own their editor and suspend the previous group for their
lifetime. The global popup pointer and separate global popup field group are
removed. Keyword Enter submits; Escape cancels. Closing restores the previous
editor before reporting the result, and the result callback runs while the
reply's strings are alive. Options close through `AfterUiDispatch`.

The NPC popup no longer inherits `W8DialogBase`: it owns its geometry, backdrop,
layer, editor, buttons and explicit-surface painter. The main screen owns active
and suspended modal content through `unique_ptr`; a small `ModalContent` interface
only supplies input, painting and redraw entry points. Other dialogs implement
that host contract through their existing base while their callers migrate.
Reply delivery happens on explicit close, before destruction; discarding the
popup during screen teardown does not submit an unfinished reply.

Text fields paint through `RenderAll(SDL_Surface&)`. Focus changes and pointer
handlers no longer paint into the global framebuffer. Existing screens repaint
their fields during composition, preserving caret and selection rendering.
The reusable popup buttons still use `W8ButtonWidget`; merging its behavior with
`W8TextControl` and removing their widget infrastructure remain outstanding.

## Migration sequence and gates

1. **Baseline:** keep the useful parent-independent construction, SDL geometry,
   sprite-state and explicit painting work. Obtain green native, ASan, include
   hygiene and relevant licensed runtime checks. Treat recursive widget/panel
   lifecycle machinery as transitional.
2. **Input-layer API:** use the temporary RegionManager backend and lock down
   precedence, geometry, capture, modal blocking and callback lifetime behavior.
   The foundation and main-menu prototype above are implemented.
3. **Architectural prototype:** migrate formation's cells/actions/background as
   one controller-owned interaction group, preserving the 250 ms drag threshold,
   cursor behavior and legal/invalid drops. Convert one modal text-entry dialog
   with existing editing logic, nested focus restoration and deferred close.
   Require runtime parity and a measurable complexity reduction before expanding
   to the remaining screens. Formation input is implemented; its runtime parity
   gate is covered by licensed scenarios. Modal/editor ownership is migrated;
   the NPC popup owns its painter independently of the legacy dialog base.
   Shared button consolidation is the next gate.
4. **Controls:** audit and merge `W8TextControl`/`W8ButtonWidget` button semantics,
   including toggles, repeat, right clicks and sounds. Keep `ButtonAppearance`
   as resource/painting functionality. Reuse existing text editing and layout.
5. **Screens and gameplay:** migrate coherent options, party, character, journal,
   camp, spell, portrait, inventory and NPC interaction groups. Do not translate
   every callback into another widget subclass.
6. **Backend removal:** once callers use layers, replace RegionManager with the
   small router and delete region registries, template adapters, numeric handles,
   `OwnedRegion`, `ScreenRegionSet`, `RegionSetView` and `ExclusiveRegionSet`.
   Delete `W8Widget`, `Controls` and the dialog hierarchy as their callers leave.
7. **Painting:** retain explicit surfaces, give each screen its draw order and
   dirty areas, and invalidate both old/new geometry when moving artwork.

## Validation

The Catch2 cases tagged `[ui-layer]` in `tests/native/owner_test.cpp` use the real
RegionManager backend without licensed assets. They cover layer/target ordering,
equal-priority ties, circle/rectangle boundaries, target updates/removal, release
outside, capture release during mouse-leave for a drop, legacy nested modal blocking, rejection of disabled/removed suspended capture,
and safe deferred owner destruction from pointer and hover callbacks. The pump
case verifies the current policy finishes before close and leaves later events
queued for the next owner. Existing
ownership and text-input cases remain the regression coverage for the backend.

```sh
cmake --build --preset linux
ctest --preset linux -R 'native_owners/'
WIZ8_ASSET_ROOT=/path/to/Wizardry8 ctest --preset linux -L runtime
```

The licensed `runtime_main-menu-ui` scenario remains the main-menu behavior gate:
disabled Load, release outside, pointer-dismissed Credits and clean re-entry.
`runtime_formation-ui` adds a real-game scenario for the hold threshold,
destination drops, reset, cancellation on the panel edge, accept and re-entry.
It requires retail assets. The main-menu and formation scenarios passed locally
on the rebased implementation, together with all eleven licensed scenarios.
New migration slices must repeat their relevant checks.
Spell hover, NPC dialogue, save/load and portrait behavior also need licensed
runtime checks as those interaction groups migrate. A skipped runtime
scenario is not evidence of parity.

Completion means one dispatcher, one button state machine, one text editor,
scoped modal/focus ownership, explicit painting and no required production
`W8Region`, `W8Widget`, `Controls` or `ExclusiveRegionSet`. The completed series
must reduce infrastructure, state and lifetime protocols; this foundation adds
temporary adapter code so subsequent screen conversions can remove the old APIs.

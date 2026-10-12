# UI modernization

The goal is less infrastructure: one input dispatcher, screen-owned hit targets,
ordinary controls, and explicit SDL painting. Do not build a visual widget tree
or a general-purpose UI framework. The completed migration must delete more code,
state and lifetime protocols than it introduces.

## Current implementation

`UiLayer` owns stable `HitTarget` registrations. Screens provide rectangular or
circular bounds, priority, pointer callbacks and optional dynamic tooltip text.
It temporarily uses RegionManager; there is only one dispatcher, hover state,
capture owner and tooltip timer. `PumpUiInput` consumes the normalized SDL stream.

- SDL rectangles are half-open; convert retail inclusive edges explicitly.
- Lower layer order wins, then earlier creation. Within a layer, higher target
  priority wins, then earlier registration. Default order matches the old backend.
- Down captures. Up outside still reaches the source; conventional buttons require
  a matching captured press and an inside release. Gameplay can release capture
  explicitly to send a drop to its destination.
- Disabling or removing targets clears their hover, capture and displayed help.
- `UiResult::Pass` delegates to the screen policy, not to lower targets.
- Tooltip providers read current state when the delayed tooltip is displayed.
- `AfterUiDispatch` commits target removal, owner destruction and screen transitions
  after the outermost dispatch transaction. The pump then yields, preserving queued
  events for the next owner. Do not destroy a handler directly from its callback.
- Modal layers reuse the backend's existing exclusive scope. Inherited legacy input
  scopes and the text-cursor precedence exception remain private adapter details.

Main menu owns six targets and its painter; its widget subclass and panel are gone.
Formation owns cell interaction state, three ordinary buttons and direct sprite
painting. Its cell/overlay widgets, region templates and exported widget arrays are
removed. The gameplay controller retains the 250 ms hold, leave-to-drag, destination
capture release, legal moves, cancellation and accept/reset behavior.

NPC replies own their layer, editor, buttons, backdrop and explicit-surface painter,
without `W8DialogBase`. A small `ModalContent` contract lets the main screen own active
and suspended modal content. Explicit close restores editor focus before delivering
the reply; teardown does not submit unfinished input. Other dialogs retain their
base temporarily, but their background no longer requires a widget subclass.

`TextFieldGroup` owns ordinary fields and paints through `RenderAll(SDL_Surface&)`.
Pointer/focus changes no longer paint the global framebuffer. UTF-8 editing,
selection, capacity, tab order and nested focus restoration reuse the existing editor.

`ButtonInteraction` supplies the shared press, selection, arming and repeat state.
Policies retain text-style release toggles/latches and dialog-style press toggles,
per-button capture, repeats and controller activation. `W8ButtonWidget` is now an
ordinary layer-backed control; `W8TextControl` still has the old visual ownership
protocol. Journal owns its layer, buttons, page text and draw order without a panel.
The options menu owns eight buttons and its extra artwork hotspots directly; its
row subclasses and selection-listener loop are removed. Navigation and radio
selection use owner callbacks. Options panels own ordinary sliders with direct
painting and callbacks; the horizontal thumb widget and listener are deleted.
Track clicks and drags update bound values before notifying their owners.
Options pages now own buttons, key-binding targets, save rows and explicit draw
order without Controls or W8Widget. Their key-capture, save-row and editor
listener interfaces are deleted. Journal, formation actions and options share
the existing button painter, including missing frames and hover insets.

## Remaining deletion work

1. Finish merging button painting and migrate the camp scroll controls. Remove obsolete
   wrappers rather than adding another control adapter.
2. Migrate party/character screens, camp and the remaining gameplay groups
   as coherent owners. Keep gameplay behavior in their existing controllers.
3. Replace RegionManager only after its registrations migrate. Delete numeric region
   APIs, templates, slot recycling, binding generations, `OwnedRegion`,
   `ScreenRegionSet`, `RegionSetView`, `ExclusiveRegionSet` and `W8RegionHandler`.
4. Delete `W8Widget`, `Controls` and the remaining dialog hierarchy as callers leave.
   Screens must own draw order and recomposition of old/new areas when artwork moves.

The adapter is temporary. Migration is not complete while production needs those
legacy types or two control implementations. Check the cumulative diff, not only
individual slices, and revise the approach if conversions keep adding machinery.

## Validation

Use existing Catch2 ownership/input cases for precedence, geometry, capture,
modal blocking, editing and deferred teardown. Avoid tests of trivial accessors.
Licensed runtime scenarios cover menu re-entry, formation hold/drop/reset/cancel,
NPC text submission/cancellation, journal navigation, spell hover, portraits and
save/load. Skipped licensed tests are not evidence of behavioral parity.

```sh
cmake --build --preset linux
ctest --preset linux -R 'native_owners/'
WIZ8_ASSET_ROOT=/path/to/Wizardry8 ctest --preset linux -L runtime
```

Require green native, ASan and include-hygiene checks before merging. Report code
and state reductions alongside behavior checks; do not count an unfinished adapter
as the final simplification.

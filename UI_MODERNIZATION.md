# UI modernization

The UI uses one input dispatcher, screen-owned hit targets, ordinary controls,
and explicit SDL painting. Screen controllers own layout and draw order; there
is no visual widget tree or second region backend.

## Input and ownership

`UiLayer` owns stable `HitTarget` objects. A target contains a rectangle or circle,
priority, pointer callback and optional tooltip provider. The dispatcher borrows
live layers and maintains hover, capture, modal scopes and the tooltip timer.
`PumpUiInput` consumes normalized SDL events and applies the screen's input policy.

- Coordinates are in 640x480 game space. SDL rectangles are half-open; retail
  inclusive bounds are converted when constructing the target.
- Lower layer order wins, then earlier creation. Within a layer, higher target
  priority wins, then earlier registration. Text fields use the cursor tip and
  take precedence over their backing artwork.
- Down captures. Up outside still reaches the source; conventional buttons require
  a matching press and an inside release. Gameplay can release capture explicitly
  when a drag destination must receive the drop.
- Disabling or deleting a target clears its hover, capture and displayed help,
  including references saved by a suspended modal layer.
- Modal layers block underlying targets and restore valid capture on dismissal.
  Controls receive their owner's layer explicitly; there is no global dialog stack
  used to infer ownership.
- `UiResult::Pass` delegates to screen policy, not to lower targets.
- `AfterUiDispatch` commits target removal, owner destruction and screen transitions
  after the outermost dispatch transaction. The pump then yields, preserving queued
  events for the next owner. Callbacks must defer destruction of their own target.
- Tooltip providers read current state when delayed help is displayed.

The numeric region registry, templates, IDs, slot recycling, binding generations,
`OwnedRegion`, `ScreenRegionSet`, `RegionSetView`, `ExclusiveRegionSet`, and region
handler adapters are deleted. Gameplay pause/mouselook policy remains in gameplay
controllers, separate from modal UI ownership.

## Controls and painting

`ButtonInteraction` supplies press, selection, arming and repeat state.
`W8ButtonWidget` is an ordinary layer-backed button with the shared artwork painter;
its historical name does not imply widget inheritance. Policies preserve press and
release toggles, latches and controller activation. Screen-specific animated art
and readouts are painted by their owners.

`TextFieldGroup` supplies UTF-8 editing, selection, capacity, tab order and nested
focus restoration. Numeric split fields use this editor with digit restriction and
value validation; their small dialog-specific painter retains right alignment.
There is no separate numeric editing algorithm or global numeric scratch buffer.

Buttons, text buffers, editors, scrollbars and dialog text controls paint into an
explicit `SDL_Surface&`. Screens choose draw order and recomposition, including
restoring backing artwork before repainting changed controls. Ordinary dialog
controllers retain shared backdrop and sizing behavior; they are not a generic
visual parent/child system. NPC replies demonstrate modal content without
`W8DialogBase` inheritance.

Main menu, formation, journal, options, party selection, character creation, camp,
NPC dialogue, combat actions, locks, traps, spell selection and gameplay overlays
own their input and painting directly. `W8Widget`, `Controls`, text widgets, panel
and selection trees, listener interfaces, implicit dialog ownership, unused dialog
factory kinds and notification subclasses are deleted.

## Validation

Use CI for builds, native ownership/input checks, ASan, include hygiene and licensed
runtime scenarios. Existing cases cover precedence, capture, modal teardown,
editing, menu re-entry, formation, NPC dialogue, journal, spell selection, portraits,
options and save/load. Skipped licensed tests do not establish behavioral parity.
Require green checks before merging. Measure the cumulative diff against the base;
this migration must reduce code and lifetime protocols, not add a UI framework.

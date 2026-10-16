# UI modernization

The UI uses one input dispatcher, screen-owned hit targets, ordinary controls,
and explicit SDL painting. Screen controllers own layout and draw order; there
is no visual widget tree or second region backend.

## Input and ownership

`UiLayer` owns stable `HitTarget` objects. A target contains a rectangle or circle,
priority, pointer callback and optional tooltip provider. The dispatcher borrows
live layers and maintains hover, capture, modal scopes and the tooltip timer.
`PumpUiInput` consumes normalized SDL events and applies the screen's input policy.
Runtime events, held keys and live command bindings use SDL keycodes. Retail virtual-key
numbers are decoded/encoded only at the saved-binding and `MGSKeyboard.ini` boundaries.
`PollInputEvent` returns an optional event by value; pending events belong to `InputState`
and have no fixed event-count limit. Attaching or detaching the input window resets them.

Ordinary button down/up events carry SDL's click count; double-click activation stays
on button down. Focused editors request native text input: key events edit or navigate,
and committed text events insert characters. Retail key-name mapping is confined to
binding conversion. MIPE text rows and generator names follow the same text contract.

The character-summary dialog owns its voice playback and portrait animation. Quote
and sample lookup use the supplied character, and the animation state is shared with
party portraits as a concrete resource. Opening or closing a preview does not install
a party member, reset gameplay state, or finish the party's queued events.

- Coordinates are in 640x480 game space. SDL rectangles are half-open; retail
  inclusive bounds are converted when constructing the target.
- Lower layer order wins, then earlier creation. Within a layer, higher target
  priority wins, then earlier registration. Text fields use the cursor tip and
  take precedence over their backing artwork.
- Down captures, whatever the handler returns: gameplay controls that `Pass` a
  press still expect its release. Hover-only targets (tooltips, previews) call
  `HitTarget::setPassive()` and never capture. Up outside still reaches the source; conventional buttons require
  a matching press and an inside release. Gameplay can release capture explicitly
  when a drag destination must receive the drop.
- Disabling or deleting a target clears its hover, capture and displayed help,
  including references saved by a suspended modal layer.
- Modal layers block underlying targets and restore valid capture on dismissal.
  Controls receive their owner's layer explicitly; there is no global dialog stack
  used to infer ownership.
- Screens own their layers. A suspended screen keeps its layers, so the game loop
  disables all input before entering any screen and the entering screen enables
  its own. Within the main game, a UI mode change does the same and
  `SyncMainGameModeRegions` re-enables the layers that mode uses, including the
  fixed interface buttons in `MainGameButtons`.
- `UiResult::Pass` delegates to screen policy, not to lower targets.
- A `UiTransaction` spans one input step: a pointer dispatch, a `PumpUiInput`
  drain with its screen policy, or a modal's `ProcessInput()`. Transactions nest.
  When the outermost one ends, the actions queued with `AfterUiDispatch` run, then
  the objects retired with `DestroyAfterUiDispatch` are destroyed. This is the
  only deferral mechanism: owner destruction and screen transitions are actions,
  and removed hit targets and replaced modals are retired, so no object is freed
  while its own callback is on the stack. The pump yields once an action is
  queued, preserving the remaining events for the next owner. A button asserts
  that no callback destroys it directly.
- The dispatcher owns hover, press and release sounds. Buttons only silence them
  (`PushButtonSoundScheme(0, true)`) or play the repeat release, which has no
  dispatcher event.
- `ProcessModalOwner()` runs the modal's input step as one transaction, so
  `OpenModal` from inside the current modal retires it until `ProcessInput()`
  returns; only the modal that ran is closed, never its replacement. Dialog owners
  extract typed completion values, disable input, retire the closing object and
  then deliver the callback. Destructors release resources; they do not deliver results.
- Tooltip providers read current state when delayed help is displayed.

The numeric region registry, templates, IDs, slot recycling, binding generations,
`OwnedRegion`, `ScreenRegionSet`, `RegionSetView`, `ExclusiveRegionSet`, and region
handler adapters and the paired global input-sink protocol are deleted.
Gameplay pause/mouselook policy remains in gameplay
controllers, separate from modal UI ownership.

## Controls and painting

`ButtonInputRule` names the matching-press and primary-pressed-state contracts;
menu artwork is independent of these interaction rules.

`ButtonInteraction` supplies press, selection, arming and repeat state.
`W8ButtonWidget` is an ordinary button on its owner's layer with the shared artwork painter;
its historical name does not imply widget inheritance. Policies preserve press and
release toggles, latches and controller activation. Screen-specific animated art
and readouts are painted by their owners.

`TextFieldGroup` supplies UTF-8 editing, selection, capacity, tab order and nested
focus restoration. Numeric split fields use this editor with digit restriction and
value validation; their small dialog-specific painter retains right alignment.
There is no separate numeric editing algorithm or global numeric scratch buffer.

`W8ButtonWidget::Draw` paints every visible configured button into its supplied
canvas, even when the button is clean. It does not invalidate the main framebuffer
or clear screen dirtiness. Owners restore backing artwork, paint in order, then
commit button updates through `DrawAndInvalidate`. That helper checks dirtiness,
paints, invalidates and clears the dirty flag only for the main framebuffer. A
forced call repaints after backing artwork has been restored. Auxiliary targets
always receive a pure paint and retain pending screen updates. Scrollbars follow
the same rule and recompose the track before painting the thumb. All button groups borrow an explicitly owned input
layer, including the HUD banks and automap chrome; there is no per-button layer.
`MainGameScreenState` owns its HUD button bank and input layer as value members.
Suspension disables input while retaining that state; full game exit destroys the
buttons and layer together. Entry and mode changes explicitly enable the HUD layer.
The active submenu is also owned by this state, with its layer and rows destroyed
as one panel. Available entries are local construction data; button count and panel
bounds replace global row bookkeeping, and an optional deadline owns auto-close.

Configuration uses existing image/appearance values plus a named `ButtonSetup`
containing callbacks, interaction policy, tooltip, priority and menu artwork.
Successful `Configure` replaces the entire setup, including secondary, movement,
double-click and hover callbacks, and resets visibility, enabled and pressed state.
Failed image configuration leaves the previous setup and input target untouched.
Callbacks capture the actual owner or action without a generic dialog-owner or
numeric payload route. The stored setup is the single source of interaction policy
and artwork mode; runtime policy changes use `Policy()`.


Buttons, text buffers, editors, scrollbars and dialog text controls paint into an
explicit `SDL_Surface&`, including the complete dialog painting path. Scroll
callbacks request redraw instead of painting the framebuffer directly. Screens choose draw order and recomposition, including
restoring backing artwork before repainting changed controls. Dialog constructors configure their controls; message content is configured before
presentation. Missing required artwork fails configuration and releases owned controls.
Closing owners erase the dialog explicitly; destructors release resources. Ordinary dialog
controllers retain shared backdrop and sizing behavior; they are not a generic
visual parent/child system. NPC replies demonstrate modal content without
`W8DialogBase` inheritance. A closing overlay calls `EraseRegion` on the area it
painted, which clears that area and schedules its upload.

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

Numeric split-field editors share their owning dialog's input layer for pointer
caret placement and modal focus rather than creating a disabled private layer.

Video scene roots have a separate resource owner, `VideoOverlayScenes`. The two
prerender pages are rendered before the world, and the frame pages retain their
existing retirement rule. User and fullscreen scenes retain their ordering
switch; the square scene retains its own camera, and the cursor renders last.
These passes borrow nodes whose lifetime belongs to ordinary C++ owners.
World and video roots own their explicitly adopted subtrees with `unique_ptr`;
widgets, animation templates and temporary effects keep their own owners.
`setParent(nullptr, ...)` detaches from traversal without moving ownership or
attaching to a global root. Parent destruction detaches borrowed children.
Scene updates, traversal and rendering run on the game thread; audio completion
callbacks are dispatched there by the sound service. There is no graph worker
or graph-wide lock; renderer resource locks retain their separate responsibility. Design-space bounds use `DesignRect`, converted explicitly to an
`SDL_Rect` for the target surface.

## Layer precedence and scrollbar ownership

Screens and overlays use an ordered `UiLayerOrder` enum rather than recovered
region-set numbers. The enum's declaration order preserves established hit-test
precedence, including eight consecutive portrait slots; equal-ranked layers
still use registration order. No legacy rank values remain.

A reusable `W8RangeControl` registers its three targets with its owning screen's
`UiLayer`. It unregisters them on destruction and activates only its own targets.
The camp screen shares one foreground scroll layer among its item, spell and stats
scrollbars, so switching a page cannot disable input owned by other controls.
`W8ButtonWidget`'s explicit layer ownership and separate visual state are handled
in the preceding button-contract PR.

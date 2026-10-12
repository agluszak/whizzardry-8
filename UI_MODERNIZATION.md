# Consolidating the native UI

Whizzardry should have one game UI, not separate SGP and Wizardry 8 control systems.
SDL3 owns the platform event loop, window, input devices, drawing primitives and
textures. It is **not** a widget toolkit; our small game-specific widget hierarchy
will still need to render the game's original graphics and text.

## Current layers

- **ButtonAppearance**: shared sprite resources and drawing styles. The SGP
  Button System, its input registry and the static region catalog are gone.
- **W8Region / RegionManager**: the sole mouse hit-test and event dispatcher.
  Every region belongs to an `OwnedRegionSet`, `ScreenRegionSet`, `OwnedRegion`
  or widget that creates it on screen entry and releases it on exit; released
  slots are recycled, so region and set indices are handles, not positions.
  Screen sets pass the retail set index they replaced as their hit-test order,
  so overlapping regions keep the original precedence regardless of creation
  order. The eight party-portrait event regions are process-lifetime because
  the main game and camp both drive them while the other is suspended.
- **Controls / W8Widget / W8TextControl**: already owns child controls through
  unique_ptr through `AddControl` / `EmplaceControl` and routes region events
  into virtual widget methods. Constructors take no parent and leave widgets
  unattached; anything derived from the panel origin (hit region, text
  layout, embedded scroll bars) is resolved in `OnGeometryChanged`, which runs
  on adoption, on `W8Widget::SetBounds` and when `Controls::SetBounds` moves
  the panel. `TakeControl` transfers ownership and `RemoveControl` destroys a
  child.
  Widget destructors release regions without mutating the parent. It backs
  game panels and many screens. A panel that needs hit testing creates its own
  region set (`Controls::CreateRegionSet`) and destroys it with the panel; the
  retail per-panel global set slots are gone, and a widget's region moves with
  it when it is reparented.
- **W8DialogBase / W8ButtonWidget**: dialog buttons are `W8Widget`s
  (`local_code/ButtonWidget.h`); a button's region is an `OwnedRegion` member
  and modal dispatch reaches it through the ordinary widget hooks. The
  separate `W8DialogButton` hierarchy is gone. Camp screen state, its ranges,
  its active dialog and every `Controls` panel are `unique_ptr`-owned.
- **Screen and panel ownership (#192–#197)**: `Controls::AcquireRegionSet` and the
  process-lifetime per-panel set slots have been deleted. Options, character
  creation, main-game, party-selection and other screen objects are
  `unique_ptr`-owned and reset on leave; their child panels and text buffers
  are owned by their containing screens. Automap screen state and the cell
  and marker metadata collections are also directly owned.
- **Local screens**: screen-specific state, draw code, global region callbacks
  and input queues. These should remain screen-specific *views*, not each
  maintain a distinct input framework.

## Target

- A screen owns its widgets. A modal dialog is a screen-owned widget container
  (or overlays one), not a separate global input mode.
- One owner-aware hit test, hover, button capture, event dispatch, keyboard
  focus and tooltip implementation. No global numeric region or button IDs.
- SDL events are normalized exactly once. Widgets receive mouse, keyboard,
  wheel and text input through the same dispatch. Rendering is separate.
- Asset decoding and sprite/text rendering stay usable as ordinary resources.
- Preserve the original 640x480 game-space coordinates and visual appearance.
  Avoid bolting on a new adapter or adding another parallel control class.

## POC: main-menu ownership

The main menu previously used six catalog regions with six free callbacks.
Now a Controls panel owns six W8Widget instances with their own rectangles,
creates an ordinary dynamic region set, and shares activation between keyboard
shortcuts and pointer clicks. The old callback functions, declarations and
hard-coded catalog region IDs are no longer used. Sprite states are table
driven, both full-redraw paths reuse one loop, and the modal dialog is owned
by the screen rather than by a raw global pointer.

W8Region still provides hit testing as an intermediate step.

## Passive screens do not need widgets

Intro and Credits do not need hit testing, hover or focus. They now consume
their own skip/dismiss events instead of retaining full-screen region callbacks.
Do not invent widget subclasses where simple screen input suffices.

## Remaining work

The region-set migration and first screen/panel ownership passes are complete.
Next, finish screen-local resource lifetimes, remove recovery-only initialization
and manual teardown, and consolidate event routing, focus and redraw. Renderer
objects (`g_fade_overlay`, surprise effects and automap 2D markers) still use
`ReleaseRendererObject` and belong to a separate renderer ownership audit.

## Migration sequence

1. **Screen-owned UI**: convert screens with interactive hit targets to
   Controls-owned widgets. Delete redundant full-screen regions for passive
   screens. Remove per-screen global callbacks and global region tables
   as screens migrate.
2. **Dialogs**: migrate a small message dialog first, then text entry,
   scrolling and complex dialogs. Dialog buttons are normal widgets
   (`W8ButtonWidget`); do not add another parallel control class.
3. **Single input routing**: route each SDL input event to the current
   screen/modal tree, with a single capture/focus policy. RegionManager now
   handles screen regions, dialog buttons and text fields; continue removing
   global region IDs and the SGP input-event queue.
4. **Drawing and invalidation**: consolidate the dialog button painter,
   Controls' dirty rectangles, per-screen refresh flags and SDL texture
   uploads. Keep an explicit game-space UI canvas and minimize full-frame
   redraws.
5. **Remove dead infrastructure**: delete obsolete registries, factory
   helpers, manual teardown and fallback branches, rather than retaining
   compatibility with the recovered Windows code.

## POC review checklist

- Intro, New Game, Load Game (enabled/disabled), Credits, Options and Exit
  respond to keyboard shortcuts and pointer presses.
- Hover, press, release outside a hit rectangle, mouse leave and re-entry
  have the expected sprite states and do not activate an unpressed item.
- Opening and closing a modal dialog does not route clicks to the menu.
- Leaving and re-entering the main menu leaves no widget or region callbacks
  pointing into a destroyed screen.
- Run CTest `runtime_main-menu-ui` to check disabled Load, release outside a
  button, mouse-dismissed Credits, and clean re-entry.
- Native build and existing runtime screen tests pass.

The SGP Mouse System and SGP Button System are gone. ButtonAppearance remains
as a resource/painter module while dialog and screen composition moves onto
Controls.

# Consolidating the native UI

Whizzardry should have one game UI, not separate SGP and Wizardry 8 control systems.
SDL3 owns the platform event loop, window, input devices, drawing primitives and
textures. It is **not** a widget toolkit; our small game-specific widget hierarchy
will still need to render the game's original graphics and text.

## Current layers

- **SGP Button System / Mouse System**: GUI_BUTTON, MOUSE_REGION, sprite
  resources, click state, independent hit-testing, mouse capture and fast help.
  Dialogs and message boxes use it.
- **W8Region / RegionManager**: another mouse hit-test and event dispatcher,
  with a global catalog of 313 predeclared regions and 51 sets, plus dynamic
  registrations and a second tooltip path. It backs most local screens.
- **Controls / W8Widget / W8TextControl**: already owns child controls through
  unique_ptr and routes region events into virtual widget methods. It backs
  game panels and many screens.
- **W8DialogBase / W8DialogButton**: a second control composition and input
  loop, implemented on top of SGP instead of Controls.
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
- Asset decoding and sprite/text rendering stay usable as ordinary resources;
  SGP's button manager and region manager should eventually disappear.
- Preserve the original 640x480 game-space coordinates and visual appearance.
  Avoid bolting on a new adapter or adding another parallel control class.

## POC: main-menu ownership

The main menu's six catalog regions used six free callbacks to handle hover,
press and activation. The POC binds those same rectangles to six panel-owned
W8Widget instances, uses DispatchControlRegionEvent, and shares activation
between keyboard shortcuts and pointer clicks. The old callback functions and
their declarations are deleted. Its sprite-state mapping is table driven, and
both full menu-redraw paths reuse one loop rather than six calls apiece.

This deliberately still uses W8Region for hit testing: changing the event
dispatcher, control ownership and region storage simultaneously would hide
regressions. The global catalog keeps its first six rectangle records, now
without permanent callback pointers. Removing their numeric IDs is follow-up
work, not an excuse to keep the adapter forever.

## Passive screens do not need widgets

Intro and Credits do not need hit testing, hover or focus. They now consume
their own skip/dismiss events instead of retaining full-screen region callbacks.
Do not invent widget subclasses where simple screen input suffices.

## Migration sequence

1. **Screen-owned UI**: convert screens with interactive hit targets to
   Controls-owned widgets. Delete redundant full-screen regions for passive
   screens. Remove per-screen global callbacks and static catalog records
   as screens migrate.
2. **Dialogs**: migrate a small message dialog first, then text entry,
   scrolling and complex dialogs. Make dialog buttons normal widgets with
   sprite-backed rendering; remove W8DialogButton and its SGP GUI_BUTTON
   intermediary rather than wrapping that intermediary once more.
3. **Single input routing**: route each SDL input event to the current
   screen/modal tree, with a single capture/focus policy. Eliminate the
   duplicate MOUSE_REGION and W8Region dispatchers, global region IDs,
   separate tooltip machinery and SGP's input-event queue.
4. **Drawing and invalidation**: consolidate the SGP button painter,
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

The POC intentionally does not introduce a new UI framework or alter SGP
buttons. Those are separate removal steps once the Controls dispatch path
has demonstrated the behavior.

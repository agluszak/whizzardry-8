# RmlUi experiment

This is an **opt-in native UI experiment**, not a replacement for the production
menu or dialog infrastructure. It lives in the real game's main menu, not in a
second application/window.

Build with:

```sh
cmake --preset linux -DWIZ8_RMLUI_POC=ON
cmake --build --preset linux
```

Press **F9** on the main menu to open an RmlUi modal with working mouse hover,
click handling, keyboard focus, Enter, Tab, Escape and cancellation. It uses
RmlUi's element hierarchy, CSS layout, event listeners and FreeType fonts. The
modal maps native SDL window coordinates to 640×480, and the renderer targets
the *existing* 640×480 CPU framebuffer before the SurRender SDL GPU upload.
There is no second window, GPU device or independent event pump.

A system TrueType font is needed for this experiment. On Linux the DejaVu Sans
or Liberation Sans locations are tried; on macOS and Windows, common Arial
locations are tried. Set `WIZ8_RMLUI_FONT=/path/to/font.ttf` to override.

Dependencies are only fetched when `WIZ8_RMLUI_POC` is enabled. The
RmlUi version is pinned to its 6.3 release commit. The upstream SDL renderer
is compiled from that same version; it is not copied into the repository.

## What this proves

- A real modal can own its controls and click events without SGP `GUI_BUTTON`,
  mouse regions, W8Region IDs or a parallel window.
- RmlUi can be composited into Whizzardry's original coordinate system using
  SDL3 and the existing frame presentation.
- Closing the modal returns control to the original main menu with its
  previous screen pixels restored.

## What this deliberately does not prove

- Visual fidelity to retail: the demo uses placeholder CSS colors, not the
  original STI graphics and bitmap fonts.
- GPU efficiency: the current renderer uses SDL's **software renderer** on
  the existing game framebuffer, taking and restoring a snapshot of the small
  dialog rectangle. It must eventually be replaced with an SDL GPU pass
  integrated with SurRender before adopting RmlUi for frequent UI redraws.
- Complete input coverage: this POC maps only the keys needed for the modal.
  A full migration should route normalized SDL3 input to RmlUi centrally.
- Replacement of the full dialog taxonomy, especially scroll areas, numeric
  input, keyboard composition, sprite animation and screen-specific state.

## Decision criteria

Compare the deletion possible with RmlUi against the `Controls` / `W8Widget`
consolidation in PR #124. Do not add a lasting adapter that retains SGP
button IDs underneath RmlUi. Either adopt a single RmlUi-owned widget tree
and delete old systems, or discard this experiment.

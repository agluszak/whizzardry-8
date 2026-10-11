# Wizardry 8 artwork in the RmlUi experiment

Build the opt-in proof of concept:

```sh
cmake --preset linux -DWIZ8_RMLUI_POC=ON
cmake --build --preset linux
```

Run the native game with the installed retail assets, then press **F9** in the
main menu. Confirm, Cancel, Enter, Tab, Escape, hover and pressed button
states are handled by RmlUi. Closing returns to the underlying menu.

## Actual game artwork, not placeholder CSS

The demo loads these resources **directly from the installed Wizardry 8 game
archives**, using the existing `CreateVideoObject` STI decoder:

- `Data\Dialogs\DialogBorder.STI` — nine-slice outer border
- `Data\Dialogs\DialogEdge.STI` — nine-slice inset frame
- `Data\Dialogs\DialogBackground.sti` — dialog interior
- `Data\Dialogs\DialogConfirmation.STI` — original normal,
  hover and pressed confirmation/cancel button sprite frames (0-2 and 4-6)

RmlUi's built-in `tiled-box` and `image` decorators consume these STI
sprites through a subclass of the upstream SDL render interface. No extracted
art, generated PNGs, recolored CSS surfaces, or bundled font files are added
to the repository. The static style rules only specify geometry, asset
selection and input states. Rendering still takes place on the original
640×480 SDL CPU framebuffer presented by SurRender.

The dialog text is rendered with the game's initialized bitmap dialog font
(`g_dialog_interface_font`) at coordinates provided by RmlUi layout. There
is **no fallback to a system TrueType font**.

## Purpose and limitations

This tests RmlUi widget layout, keyboard focus, modal input routing, asset
loading and visual state decorators in the actual game, against PR #124's
`Controls` / `W8Widget` path. It deliberately does not create a second
window, event pump or production UI framework.

The POC currently uses the upstream SDL software renderer and restores a
small CPU framebuffer rectangle between frames. Its decorators stretch the
middle of the tiled-box sprites, while the old button system tiled its
background and edges. If RmlUi is adopted, validate that difference against
retail and move the UI draw pass onto the existing SDL GPU pipeline.

The bitmap text renderer is used for the message as a POC; a comprehensive
migration should implement a RmlUi font engine backed by Wizardry's glyphs
rather than manually drawing arbitrary labels. In particular this experiment
does **not** imply keeping the SGP button registry as a rendering backend.

Missing retail STI artwork or the initialized Wizardry font is a failure,
not a reason to display a made-up substitute.

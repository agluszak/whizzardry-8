#include <SDL3/SDL_pixels.h>
#include "wiz8/ui_input.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/cursor.h"
#include "input.h"
#include "wiz8/engine_code/Video2.h"
#include "himage.h"
#include "wiz8/application.h"
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>

/* Lifecycle record 12 is the exit screen selected by Main Menu's Exit row and
   its Escape/E/X shortcuts. Live query: 0x00591780 sits in the gap between
   Local Screens\PleaseWaitScreen.cpp (upper 0x00590FA0) and
   Local Screens\MGSKeyboard.cpp (lower 0x00591960). */

// GLOBAL: WIZ8 0x0064c1c8
int g_selected_party_slot = -1;

// FUNCTION: WIZ8 0x00591780
void RequestExitScreen(void)
{
    SetPendingScreenState(W8_SCREEN_EXIT);
}

// FUNCTION: WIZ8 0x00593320
int GetSelectedPartySlot(void)
{
    return g_selected_party_slot;
}

/* Lifecycle record 12's entry handler. It paints the whole 640x480 frame in the
   near-black 0x010101 and puts one video-object frame over it, which is the
   shape record 1's much larger main-menu entry starts with too. */
// FUNCTION: WIZ8 0x00591790
unsigned char ExitScreenEnter(void)
{
    unsigned short colour;

    ResetVideoFrameState();
    UpdateHeldItemCursor();
    colour = Get16BPPColor(SDL_Color{1, 1, 1, SDL_ALPHA_OPAQUE});
    const SDL_Rect bounds{0, 0, 0x280, 0x1e0};
    SDL_FillSurfaceRect(&GetFrameBufferSurface(), &bounds, colour);
    CatalogArtwork::Load(0x1e4, 0).draw(GetFrameBufferSurface(), 0, 0, 0);
    ResetTransientRenderScenes();
    return 1;
}

/* Lifecycle record 12's frame close-out. It drains the input queue through the
   region manager and lets a key press that the regions did not consume clear
   0x006F0628; the screen then tears down unless that flag is still set and
   neither mouse-button latch is. The two trailing repeats of 0x00426790 are
   the original's own. */
// FUNCTION: WIZ8 0x005917e0
void ExitScreenFrame(void)
{
    RenderFrame();
    PumpUiInput({
        .unhandled = [](const InputEvent& input) {
            if (input.type == InputEventType::KeyDown) gfProgramIsRunning = 0;
            return InputRoute::Handled;
        },
        .track_hover = false,
    });
    if (!IsLeftButtonHeld() && !IsRightButtonHeld()) {
        if (gfProgramIsRunning != 0) {
            return;
        }
    } else {
        gfProgramIsRunning = 0;
    }
    DisableCursorScene();
    ClearPrimarySurface();
    ResetTransientRenderScenes();
    RenderFrame();
    RenderFrame();
}

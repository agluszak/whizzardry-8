#include <memory>

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>

#include "wiz8/application.h"
#include "wiz8/canvas/himage.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/platform/input.h"
#include "wiz8/render/frame.h"
#include "wiz8/screen.h"
#include "wiz8/ui_input.h"

void RequestExitScreen(void)
{
    RequestScreen({.id = W8_SCREEN_EXIT});
}

namespace {
/* The farewell picture shown by the main menu's Exit row and its Escape/E/X
   shortcuts. Any key or a released button ends the program. */
class ExitScreen final : public Screen {
public:
    bool enter(const ScreenRequest&) override
    {
        ResetVideoFrameState();
        ResetPointerCursor();
        const unsigned short colour = Get16BPPColor(SDL_Color{1, 1, 1, SDL_ALPHA_OPAQUE});
        const SDL_Rect bounds{0, 0, 0x280, 0x1e0};
        SDL_FillSurfaceRect(&GetFrameBufferSurface(), &bounds, colour);
        CatalogArtwork::Load(0x1e4, 0).draw(GetFrameBufferSurface(), 0, 0, 0);
        ResetTransientRenderScenes();
        return true;
    }

    void frame() override
    {
        RenderFrame();
        PumpUiInput({
            .unhandled =
                [](const InputEvent& input) {
                    if (input.type == InputEventType::KeyDown) gfProgramIsRunning = false;
                    return InputRoute::Handled;
                },
            .track_hover = false,
        });
        if (IsLeftButtonHeld() || IsRightButtonHeld()) {
            gfProgramIsRunning = false;
        } else if (gfProgramIsRunning) {
            return;
        }
        DisableCursorScene();
        ClearPrimarySurface();
        ResetTransientRenderScenes();
        RenderFrame();
        RenderFrame();
    }

    bool leave(ScreenDeparture) override
    {
        ReleaseCatalogArtwork();
        return true;
    }
};
} // namespace

std::unique_ptr<Screen> CreateExitScreen()
{
    return std::make_unique<ExitScreen>();
}

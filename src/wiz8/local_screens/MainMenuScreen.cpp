#include "wiz8/local_screens/MainMenuScreen.h"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>

#include "Font.h"
#include "himage.h"
#include "image.h"
#include "input.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/cursor.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/dialog_code/MessageDialogBase.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/fonts.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_screens/IntroScreen.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/music_playlist.h"
#include "wiz8/sr_api.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"
#include "wiz8/utility.h"
#include "wiz8/version.h"
#include "wiz8/xstatus.h"

/* The screen's own state. */
// GLOBAL: WIZ8 0x0069c4ba
bool g_main_menu_has_save_games;
// GLOBAL: WIZ8 0x0069c4b6
bool g_main_menu_redraw;
// GLOBAL: WIZ8 0x0069c4b4
static short g_main_menu_selected_item;
// GLOBAL: WIZ8 0x0069c4c4
bool g_main_menu_warning_shown;
// GLOBAL: WIZ8 0x0069c4bc
std::string g_pending_main_menu_message;
// The modal dialog belongs to this screen, not a global registry.
static std::unique_ptr<W8MessageDialogBase> main_menu_dialog;

namespace {
// Retail right/bottom edges are inclusive; SDL extents include that last pixel.
constexpr std::array<SDL_Rect, 6> main_menu_item_bounds = {{
    {174, 138, 294, 45},
    {140, 187, 362, 45},
    {204, 235, 233, 45},
    {239, 284, 165, 45},
    {234, 335, 175, 45},
    {279, 423, 86, 45},
}};
constexpr std::array<int, 4> main_menu_sprite_objects = {0xea, 0xec, 0xeb, 0xed};
}

// The original six item rectangles are now screen-owned data, not global
// region registrations. The load-game item uses the disabled sprite when
// the party has no saves.
unsigned char DrawMainMenuItem(short item, short state)
{
    if (item < 0 || static_cast<std::size_t>(item) >= main_menu_item_bounds.size()) return 0;
    if (item == 2 && !g_main_menu_has_save_games) state = 3;

    const auto& bounds = main_menu_item_bounds[item];
    if (state >= 0 && static_cast<std::size_t>(state) < main_menu_sprite_objects.size())
        CatalogArtwork::Load(main_menu_sprite_objects[state], 0)
            .draw(GetFrameBufferSurface(), item, 0x98, bounds.y);
    InvalidateRegion(0x98, bounds.y, 0x1f2, bounds.y + bounds.h - 1);
    return 1;
}

static void DrawMainMenuItems()
{
    for (short item = 0; item < 6; ++item) DrawMainMenuItem(item, 0);
    DrawMainMenuItem(g_main_menu_selected_item, 1);
}

static void SelectMainMenuItem(short item)
{
    DrawMainMenuItem(g_main_menu_selected_item, 0);
    g_main_menu_selected_item = item;
    DrawMainMenuItem(item, 1);
}

// The screen owns its input registrations; artwork stays in this file.
namespace {
void ActivateMainMenuItem(short item)
{
    switch (item) {
    case 0:
        RequestScreenTransition();
        g_settings.intro_seen = false;
        SetIntroVideoIndex(0);
        SetPendingScreenState(W8_SCREEN_INTRO);
        break;
    case 1:
        SetPendingScreenState(W8_SCREEN_PARTY_SELECTION);
        break;
    case 2:
        if (g_main_menu_has_save_games) {
            g_pending_screen_state.mode = 1;
            SetPendingScreenState(W8_SCREEN_OPTIONS);
        }
        break;
    case 3:
        SetPendingScreenState(W8_SCREEN_CREDITS);
        break;
    case 4:
        ClearScreenWait();
        break;
    case 5:
        RequestExitScreen();
        break;
    }
}

UiResult HandleMainMenuItem(short item, const UiPointerEvent& event)
{
    const bool available = item != 2 || g_main_menu_has_save_games;
    switch (event.action) {
    case UiPointerAction::Enter:
        if (available) SelectMainMenuItem(item);
        break;
    case UiPointerAction::Leave:
        DrawMainMenuItem(g_main_menu_selected_item, 0);
        g_main_menu_selected_item = -1;
        break;
    case UiPointerAction::Down:
        if (available && event.button == UiPointerButton::Left)
            DrawMainMenuItem(g_main_menu_selected_item, 2);
        break;
    case UiPointerAction::Up:
        if (!available || event.button != UiPointerButton::Left ||
            !(event.captured_buttons & SDL_BUTTON_LMASK))
            break;
        DrawMainMenuItem(g_main_menu_selected_item, event.inside ? 1 : 0);
        if (event.inside) {
            AfterUiDispatch([item] { ActivateMainMenuItem(item); });
        } else if (g_main_menu_selected_item == item) {
            g_main_menu_selected_item = -1;
        }
        break;
    default:
        break;
    }
    return UiResult::Handled;
}

std::unique_ptr<UiLayer> main_menu_input;

void CreateMainMenuInput()
{
    main_menu_input = std::make_unique<UiLayer>();
    for (short item = 0; item < static_cast<short>(main_menu_item_bounds.size()); ++item) {
        main_menu_input->add(main_menu_item_bounds[item], [item](const UiPointerEvent& event) {
            return HandleMainMenuItem(item, event);
        });
    }
}
} // namespace

unsigned char MainMenuScreenEnter(void)
{
    std::string text;
    std::string wide;
    unsigned short colour;
    W8MessageDialogBase* dialog;
    std::string_view pending;
    short measured;

    ResetVideoFrameState();
    g_status.game_started = false;
    g_main_menu_has_save_games = SaveGameExists();
    g_main_menu_redraw = true;
    ClearPrimarySurface();
    colour = Get16BPPColor(SDL_Color{1, 1, 1, SDL_ALPHA_OPAQUE});
    const SDL_Rect bounds{0, 0, 0x280, 0x1e0};
    SDL_FillSurfaceRect(&GetFrameBufferSurface(), &bounds, colour);
    SetViewport({0, 0, 0x281, 0x1e1});
    g_main_menu_selected_item = 0;
    CatalogArtwork::Load(0xe8, 0).draw(GetFrameBufferSurface(), 0, 0, 0);

    DrawMainMenuItems();

    text = FormatVersionBanner(false, false, false);
    wide = text;
    g_wiz_text_font_secondary.setPalette(g_font_state_palettes[W8_FONT_PALETTE_WHITE]);
    measured = g_wiz_text_font_secondary.textWidth(wide);
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x27b - measured, 5}, wide);
    g_wiz_text_font_secondary.resetPalette();
    main_menu_input.reset();
    CreateMainMenuInput();

    if (gXStatus.uiMonstersInDatabase > 1000) {
        srAssertFail("gXStatus.uiMonstersInDatabase <= MAX_MONSTERS_IN_DATABASE", __FILE__, 0x87,
                     0);
    }
    if (g_previous_screen_id != W8_SCREEN_OPTIONS) {
        StartMusicResource("MainMenu.MPL", 0, 1);
    }
    UpdateHeldItemCursor();

    pending = g_pending_main_menu_message;
    if (!pending.empty()) {
        main_menu_dialog.reset(CreateMessageDialog());
        dialog = main_menu_dialog.get();
        dialog->SetClientExtent(0xfa, 200);
        dialog->SetMessage(pending, 1, 0x32, true, false, true, true, 0, 0x15e);
        dialog->m_destroy_callback = 0;
        g_pending_main_menu_message.clear();
        return 1;
    }
    if (!HasEnoughFreeDiskSpace() && !g_main_menu_warning_shown) {
        main_menu_dialog.reset(CreateMessageDialog());
        dialog = main_menu_dialog.get();
        dialog->SetClientExtent(0xfa, 200);
        dialog->SetMessage(g_localized_strings[0x7ee], 1, 0x32, true, false, true, true, 0, 0x15e);
        dialog->m_destroy_callback = 0;
        g_main_menu_warning_shown = true;
    }
    return 1;
}

/* The main menu's developer-mode key hook. Retail never consumes the key: its
   linker folded this body into IgnoreSpellCastingInput, which compiles to the
   same bytes. */
static bool HandleDeveloperModeKey(const InputEvent* input)
{
    return false;
}

/* The canonical state-1 frame services a modal dialog first.  Without one it
   dispatches queued region and keyboard input, including the direct New Game,
   Load, Options and exit shortcuts, then completes the shared 2D redraw and
   renderer transaction. */
void MainMenuScreenFrame()
{

    if (g_dev_mode) {
        RequestExitScreen();
    }
    if (main_menu_dialog != nullptr) {
        main_menu_dialog->Draw(GetFrameBufferSurface());
        if (!main_menu_dialog->ProcessInput()) {
            main_menu_dialog.reset();
            g_main_menu_redraw = true;
            CatalogArtwork::Load(0xe8, 0).draw(GetFrameBufferSurface(), 0, 0, 0);
            DrawMainMenuItems();
        }
    } else {
        PumpUiInput({.unhandled = [&](const InputEvent& input) {
            if (input.type == InputEventType::KeyDown) {
                if (HandleDeveloperModeKey(&input)) {
                    if (g_dev_mode) {
                        g_wiz_text_font_secondary.resetPalette();
                        DrawTextAndInvalidate(GetFrameBufferSurface(), g_wiz_text_font_secondary,
                                              {5, 5}, "Developer mode enabled.");
                    }
                } else {
                    switch (input.key) {
                    case SDLK_RETURN:
                        DrawMainMenuItem(g_main_menu_selected_item, 2);
                        ActivateMainMenuItem(static_cast<short>(g_main_menu_selected_item));
                        break;
                    case SDLK_ESCAPE:
                    case SDLK_E:
                    case SDLK_X:
                        RequestExitScreen();
                        break;
                    case SDLK_PAGEUP:
                        SelectMainMenuItem(0);
                        break;
                    case SDLK_PAGEDOWN:
                        SelectMainMenuItem(5);
                        break;
                    case SDLK_UP:
                        DrawMainMenuItem(g_main_menu_selected_item, 0);
                        if (g_main_menu_selected_item > 0) {
                            --g_main_menu_selected_item;
                        } else {
                            g_main_menu_selected_item = 5;
                        }
                        DrawMainMenuItem(g_main_menu_selected_item, 1);
                        break;
                    case SDLK_DOWN:
                        DrawMainMenuItem(g_main_menu_selected_item, 0);
                        if (g_main_menu_selected_item < 5) {
                            ++g_main_menu_selected_item;
                        } else {
                            g_main_menu_selected_item = 0;
                        }
                        DrawMainMenuItem(g_main_menu_selected_item, 1);
                        break;
                    case SDLK_L:
                        ActivateMainMenuItem(2);
                        break;
                    case SDLK_O:
                        ActivateMainMenuItem(4);
                        break;
                    case SDLK_S:
                        ActivateMainMenuItem(1);
                        break;
                    }
                }
            }
            return InputRoute::Handled;
        }});
    }

    if (g_main_menu_redraw || main_menu_dialog != nullptr) {
        if (main_menu_dialog != nullptr) {
            main_menu_dialog->Draw(GetFrameBufferSurface());
        }
        ResetTransientRenderScenes();
        g_main_menu_redraw = false;
    }
    RenderFrame();
}

unsigned char MainMenuScreenLeave(int)
{
    main_menu_dialog.reset();
    main_menu_input.reset();
    ReleaseCatalogArtwork();
    return 1;
}

void SetMainMenuMessage(std::string_view message)
{
    g_pending_main_menu_message = message;
}

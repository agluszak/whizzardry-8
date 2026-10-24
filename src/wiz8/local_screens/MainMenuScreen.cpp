#include "wiz8/local_screens/MainMenuScreen.h"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>

#include "wiz8/canvas/Font.h"
#include "wiz8/canvas/himage.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/dialog_code/MessageDialogBase.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/music_playlist.h"
#include "wiz8/platform/input.h"
#include "wiz8/pointer.h"
#include "wiz8/render/frame.h"
#include "wiz8/screen.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"
#include "wiz8/version.h"
#include "wiz8/world_view.h"

std::string g_pending_main_menu_message;

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
constexpr short main_menu_item_count = static_cast<short>(main_menu_item_bounds.size());

} // namespace

MainMenuScreen::MainMenuScreen(GameSession& games) : games_(games) {}
MainMenuScreen::~MainMenuScreen() = default;

// The load-game item uses the disabled sprite when the party has no saves.
void MainMenuScreen::drawItem(short item, short state)
{
    if (item < 0 || item >= main_menu_item_count) return;
    if (item == 2 && !has_save_games) state = 3;

    const auto& bounds = main_menu_item_bounds[item];
    if (state >= 0 && static_cast<std::size_t>(state) < main_menu_sprite_objects.size())
        CatalogArtwork::Load(main_menu_sprite_objects[state], 0)
            .draw(GetFrameBufferSurface(), item, 0x98, bounds.y);
    InvalidateRegion(0x98, bounds.y, 0x1f2, bounds.y + bounds.h - 1);
}

void MainMenuScreen::drawItems()
{
    for (short item = 0; item < main_menu_item_count; ++item) drawItem(item, 0);
    drawItem(selected_item_, 1);
}

void MainMenuScreen::selectItem(short item)
{
    drawItem(selected_item_, 0);
    selected_item_ = item;
    drawItem(item, 1);
}

void MainMenuScreen::activateItem(short item)
{
    switch (item) {
    case 0:
        g_settings.intro_seen = false;
        RequestScreen({.id = W8_SCREEN_INTRO, .mode = W8_INTRO_VIDEO_WIZARDRY},
                      ScreenDeparture::Discard);
        break;
    case 1:
        RequestScreen({.id = W8_SCREEN_PARTY_SELECTION});
        break;
    case 2:
        if (has_save_games) {
            RequestScreen({.id = W8_SCREEN_OPTIONS, .mode = W8_OPTIONS_LOAD});
        }
        break;
    case 3:
        RequestScreen({.id = W8_SCREEN_CREDITS});
        break;
    case 4:
        ClearScreenWait();
        break;
    case 5:
        RequestExitScreen();
        break;
    }
}

UiResult MainMenuScreen::handleItem(short item, const UiPointerEvent& event)
{
    const bool available = item != 2 || has_save_games;
    switch (event.action) {
    case UiPointerAction::Enter:
        if (available) selectItem(item);
        break;
    case UiPointerAction::Leave:
        drawItem(selected_item_, 0);
        selected_item_ = -1;
        break;
    case UiPointerAction::Down:
        if (available && event.button == UiPointerButton::Left) drawItem(selected_item_, 2);
        break;
    case UiPointerAction::Up:
        if (!available || event.button != UiPointerButton::Left ||
            !(event.captured_buttons & SDL_BUTTON_LMASK))
            break;
        drawItem(selected_item_, event.inside ? 1 : 0);
        if (event.inside) {
            AfterUiDispatch([this, item] { activateItem(item); });
        } else if (selected_item_ == item) {
            selected_item_ = -1;
        }
        break;
    default:
        break;
    }
    return UiResult::Handled;
}

bool MainMenuScreen::enter(const ScreenRequest&)
{
    ResetVideoFrameState();
    games_.end();
    has_save_games = SaveGameExists();
    redraw_ = true;
    ClearPrimarySurface();
    const unsigned short colour = Get16BPPColor(SDL_Color{1, 1, 1, SDL_ALPHA_OPAQUE});
    const SDL_Rect bounds{0, 0, 0x280, 0x1e0};
    SDL_FillSurfaceRect(&GetFrameBufferSurface(), &bounds, colour);
    SetWorldViewport(games_.game(), {0, 0, 0x281, 0x1e1});
    selected_item_ = 0;
    CatalogArtwork::Load(0xe8, 0).draw(GetFrameBufferSurface(), 0, 0, 0);

    drawItems();

    const std::string banner = FormatVersionBanner(false, false, false);
    g_wiz_text_font_secondary.setPalette(g_font_state_palettes[W8_FONT_PALETTE_WHITE]);
    const short measured = g_wiz_text_font_secondary.textWidth(banner);
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x27b - measured, 5}, banner);
    g_wiz_text_font_secondary.resetPalette();

    input_ = std::make_unique<UiLayer>();
    for (short item = 0; item < main_menu_item_count; ++item) {
        input_->add(main_menu_item_bounds[item],
                    [this, item](const UiPointerEvent& event) { return handleItem(item, event); });
    }

    if (PreviousScreen() != W8_SCREEN_OPTIONS) {
        StartMusicResource("MainMenu.MPL", 0, 1);
    }
    ScreenPointer().reset();

    if (!g_pending_main_menu_message.empty()) {
        dialog_ = CreateMessageDialog();
        dialog_->SetClientExtent(0xfa, 200);
        dialog_->SetMessage(g_pending_main_menu_message, 1, 0x32, true, false, true, true, 0,
                            0x15e);
        g_pending_main_menu_message.clear();
    }
    return true;
}

/* A modal dialog is serviced first. Without one, queued pointer and keyboard
   input is dispatched, including the direct New Game, Load, Options and exit
   shortcuts, before the shared 2D redraw and renderer transaction. */
void MainMenuScreen::frame()
{
    if (dialog_) {
        dialog_->RefreshModal();
        if (!dialog_->ProcessInput()) {
            RetireModal(std::move(dialog_), false);
            redraw_ = true;
            CatalogArtwork::Load(0xe8, 0).draw(GetFrameBufferSurface(), 0, 0, 0);
            drawItems();
        }
    } else {
        PumpUiInput({.unhandled = [&](const InputEvent& input) {
            if (input.type != InputEventType::KeyDown) return InputRoute::Handled;
            switch (input.key) {
            case SDLK_RETURN:
                drawItem(selected_item_, 2);
                activateItem(selected_item_);
                break;
            case SDLK_ESCAPE:
            case SDLK_E:
            case SDLK_X:
                RequestExitScreen();
                break;
            case SDLK_PAGEUP:
                selectItem(0);
                break;
            case SDLK_PAGEDOWN:
                selectItem(main_menu_item_count - 1);
                break;
            case SDLK_UP:
                drawItem(selected_item_, 0);
                selected_item_ = selected_item_ > 0 ? selected_item_ - 1 : main_menu_item_count - 1;
                drawItem(selected_item_, 1);
                break;
            case SDLK_DOWN:
                drawItem(selected_item_, 0);
                selected_item_ = selected_item_ < main_menu_item_count - 1 ? selected_item_ + 1 : 0;
                drawItem(selected_item_, 1);
                break;
            case SDLK_L:
                activateItem(2);
                break;
            case SDLK_O:
                activateItem(4);
                break;
            case SDLK_S:
                activateItem(1);
                break;
            }
            return InputRoute::Handled;
        }});
    }

    if (redraw_ || dialog_) {
        if (dialog_) dialog_->RefreshModal();
        ResetTransientRenderScenes();
        redraw_ = false;
    }
    PresentGameFrame(games_.game());
}

bool MainMenuScreen::leave(ScreenDeparture)
{
    RetireModal(std::move(dialog_), false);
    input_.reset();
    ReleaseCatalogArtwork();
    return true;
}

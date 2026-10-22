#include "wiz8/local_screens/MGSPartyMovement.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include <SDL3/SDL_rect.h>

#include "layouts/main_game_screen.h"
#include "wiz8/button_interaction.h"
#include "wiz8/canvas/image.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/fight.h"
#include "wiz8/combat/party_movement.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/platform/timer.h"
#include "wiz8/render/frame.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/screen.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"

namespace {
struct PartyMovementPanel {
    UiLayer input{UiLayerOrder::PartyMovement};
    std::array<std::unique_ptr<W8ButtonWidget>, 2> buttons;
    std::unique_ptr<W8TextBuffer> caption;
    uint64_t animation_clock = 0;
    unsigned int animation_frame = 0;
    int visible_button = 1;
};
std::unique_ptr<PartyMovementPanel> movement;
}

bool CreatePartyMovementPanel(void)
{
    movement = std::make_unique<PartyMovementPanel>();
    InvalidatePartyMovementPanel();
    const W8ControlsRect bounds{0xc3, 0x13f, 0x1b9, 0x153};
    movement->caption =
        std::make_unique<W8TextBuffer>(&bounds, std::string_view{}, g_wiz_text_font_secondary,
                                       g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter, 4);
    const auto art = CatalogArtwork::Load(0x94, 0);
    for (int index = 0; index < 2; ++index) {
        auto& button = movement->buttons[index];
        button = std::make_unique<W8ButtonWidget>(movement->input);
        button->Configure(ButtonGraphics::Load(art.spriteSheet(), 7, 4, 5, 6, 6),
                          {.on_click =
                               [index](auto*) {
                                   AfterUiDispatch(index ? CancelPartyMovement
                                                         : BeginFreeTurnPhase);
                               },
                           .policy = {.input_rule = ButtonInputRule::PrimaryPressedState},
                           .tooltip = 37 + index,
                           .priority = UI_PRIORITY_LOWEST,
                           .menu_artwork = true});

        button->SetPosition(0x1bb, 0x13f);
        button->Resize(20, 20);
        button->SetVisible(index == 1);
    }
    MainGame()->party_movement_panel = true;
    CurrentGame().combat->party_move_percent = 100;
    MainGameHud()->move_percent_shown = 100;
    return true;
}

void ShowPartyMovementGauge(bool force)
{
    const int percent = CurrentGame().combat->party_move_percent;
    if (!force && percent == MainGameHud()->move_percent_shown) return;
    InvalidatePartyMovementPanel();
    MainGameHud()->move_percent_shown = percent;
}

void ReleasePartyMovement(void)
{
    if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME) {
        EraseRegion(0xb1, 0x13f, 0x1cf, 0x153);
    }
    if (auto* hud = MainGameHud()) hud->repaint.portrait_panel = true;
    movement.reset();
    if (auto* main_game = MainGame()) main_game->party_movement_panel = false;
}

// Show the free-turn button while the party can move, the cancel button otherwise.
static void SyncPartyMovementButtons()
{
    const int visible = CanPartyMove() ? 1 : 0;
    if (visible != movement->visible_button) InvalidatePartyMovementPanel();
    movement->visible_button = visible;
    for (int index = 0; index < 2; ++index) movement->buttons[index]->SetVisible(index == visible);
}

void UpdatePartyMovementPanel(void)
{
    movement->input.setEnabled(true);
    SyncPartyMovementButtons();
}

void DisablePartyMovementRegions(void)
{
    if (movement) movement->input.setEnabled(false);
}

/* The gauge image: 0 while the party can move, 1 while it runs out, 2 once the
   level stopped its movement. */
static int PartyMovementGaugeImage()
{
    if (CanPartyMove()) return 0;
    return IsLevelMovementStopped() ? 2 : 1;
}

// Whether the gauge shows the action under way rather than the one planned next.
static bool PartyMovementShowsCurrentAction()
{
    return !CanPartyMove() ||
           (CurrentGame().combat->execution_active != 0 && IsPartyEngaged() == 0);
}

void UpdatePartyMovementGauge(void)
{
    SyncPartyMovementButtons();
    if (movement->visible_button == 1)
        movement->buttons[1]->SetEnabled(CurrentGame().combat->round_active);
    const bool running = PartyMovementGaugeImage() == 1;
    if (running && TimeUntilDeadline(movement->animation_clock) != 0) return;
    if (running) {
        movement->animation_frame = (movement->animation_frame + 1) % 12;
        InvalidatePartyMovementPanel();
    }
    const W8PartyAction action = PartyMovementShowsCurrentAction()
                                     ? CurrentGame().combat->uiCurrentPartyAction
                                     : CurrentGame().combat->uiNextPartyAction;
    movement->animation_clock =
        MonotonicMilliseconds() + (action == W8_PARTY_ACTION_RUN ? 0x32 : 0x5a);
}

/* The panel, its live button, the gauge bar clipped to the party's remaining
   move with its animated end, the caption, and the portrait plate it covers. */
void DrawPartyMovementPanel(bool requested)
{
    assert(CurrentGame().combat.has_value());
    assert((CurrentGame().combat->uiCurrentPartyAction != W8_PARTY_ACTION_NONE &&
            CurrentGame().combat->uiCurrentPartyActionStatus != W8_PARTY_ACTION_FINISHED) ||
           CurrentGame().combat->uiNextPartyAction != W8_PARTY_ACTION_NONE);
    const int visible = movement->visible_button;
    if (!requested && !movement->buttons[visible]->m_dirty) return;
    auto& target = GetFrameBufferSurface();
    CatalogArtwork::Load(0x93, 0).drawAndInvalidate(target, 0, 0xb1, 0x13f);
    movement->buttons[visible]->Refresh(true);

    const int percent = CurrentGame().combat->party_move_percent;
    const short right = 0x1b9 - percent * 0xf6 / 100;
    const int image = PartyMovementGaugeImage();
    int caption;
    if (image == 1) {
        caption = ((percent != 100) - 1 & 0x77f) - 1;
    } else if (image == 2) {
        caption = 0x77d;
    } else if (CurrentGame().combat->execution_active != 0 && IsPartyEngaged() == 0) {
        caption = (CurrentGame().combat->uiCurrentPartyAction != W8_PARTY_ACTION_RUN) + 0x77b;
    } else {
        caption = (CurrentGame().combat->uiNextPartyAction != W8_PARTY_ACTION_RUN) + 0x77b;
    }
    {
        ScopedClipRect clipped(target, SDL_Rect{right, 0, 0x1b9 - right, 0x1e0});
        CatalogArtwork::Load(0x96, 0).drawAndInvalidate(target, image, 0xc3, 0x144);
    }

    const W8PartyAction action = PartyMovementShowsCurrentAction()
                                     ? CurrentGame().combat->uiCurrentPartyAction
                                     : CurrentGame().combat->uiNextPartyAction;
    const int frame_base = action == W8_PARTY_ACTION_RUN ? 0 : 0x24;
    const int frame = image == 0 ? 0x18 : image == 2 ? 0xc : 0;
    CatalogArtwork::Load(0x95, 0).drawAndInvalidate(
        target, movement->animation_frame + frame_base + frame, right - 0xf, 0x142);
    InvalidateRegion(right - 0xf, 0x142, right - 1, 0x150);
    if (caption != -1) {
        movement->caption->RenderString(Data().strings[caption], g_wiz_text_font_secondary, false,
                                        target);
    }
    PaintPortraitPlate(*MainGameHud());
}

void InvalidatePartyMovementPanel(void)
{
    if (auto* hud = MainGameHud()) hud->repaint.movement_panel = true;
}

void DisableFreeTurnButton(void)
{
    movement->buttons[0]->SetEnabled(false);
}

void EnableFreeTurnButton(void)
{
    movement->buttons[0]->SetEnabled(true);
}

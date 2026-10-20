#include "wiz8/local_screens/MGSPartyMovement.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <SDL3/SDL_rect.h>

#include "layouts/main_game_screen.h"
#include "wiz8/button_interaction.h"
#include "wiz8/canvas/image.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/combat/combat.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/CombatPartyMovement.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/platform/assertion.h"
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
    bool dirty = true;
    int visible_button = 1;
};
std::unique_ptr<PartyMovementPanel> movement;
}

void DrawPartyMovementGauge(short right, short image, bool panel_live, int caption);

unsigned char CreatePartyMovementPanel(void)
{
    movement = std::make_unique<PartyMovementPanel>();
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
    return 1;
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
    RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_PANEL);
    movement.reset();
    if (auto* main_game = MainGame()) main_game->party_movement_panel = false;
}

void UpdatePartyMovementPanel(void)
{
    movement->input.setEnabled(true);
    const int visible = CanPartyMove() ? 1 : 0;
    movement->dirty |= visible != movement->visible_button;
    movement->visible_button = visible;
    for (int index = 0; index < 2; ++index) movement->buttons[index]->SetVisible(index == visible);
}

void DisablePartyMovementRegions(void)
{
    if (movement) movement->input.setEnabled(false);
}

void DrawPartyMovementPanel(void)
{
    SDL_Rect clip;
    bool panel_live;
    int caption;
    short right;
    int image;

    if (!CurrentGame().combat.has_value()) {
        Fail("CurrentGame().combat.has_value()");
    }
    if ((CurrentGame().combat->uiCurrentPartyAction == W8_PARTY_ACTION_NONE ||
         CurrentGame().combat->uiCurrentPartyActionStatus == W8_PARTY_ACTION_FINISHED) &&
        CurrentGame().combat->uiNextPartyAction == W8_PARTY_ACTION_NONE) {
        Fail("((gpCombat->uiCurrentPartyAction != PARTY_ACTION_NONE) && "
             "(gpCombat->uiCurrentPartyActionStatus != ACTION_STATUS_FINISHED)) || "
             "(gpCombat->uiNextPartyAction != PARTY_ACTION_NONE)");
    }
    const int visible = CanPartyMove() ? 1 : 0;
    movement->dirty |= visible != movement->visible_button;
    movement->visible_button = visible;
    for (int index = 0; index < 2; ++index) movement->buttons[index]->SetVisible(index == visible);
    if (visible == 1) movement->buttons[1]->SetEnabled(CurrentGame().combat->round_active);
    panel_live = movement->dirty || movement->buttons[visible]->m_dirty;
    if (panel_live) {
        CatalogArtwork::Load(0x93, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0xb1, 0x13f);
        movement->buttons[visible]->Refresh(true);
        movement->dirty = false;
    }
    const int percent = CurrentGame().combat->party_move_percent;
    right = 0x1b9 - percent * 0xf6 / 100;
    if (!CanPartyMove()) {
        if (!IsLevelMovementStopped()) {
            image = 1;
            caption = ((percent != 100) - 1 & 0x77f) - 1;
        } else {
            image = 2;
            caption = 0x77d;
        }
    } else {
        image = 0;
        if (CurrentGame().combat->execution_active != 0 && IsPartyEngaged() == 0) {
            caption = (CurrentGame().combat->uiCurrentPartyAction != W8_PARTY_ACTION_RUN) + 0x77b;
        } else {
            caption = (CurrentGame().combat->uiNextPartyAction != W8_PARTY_ACTION_RUN) + 0x77b;
        }
    }
    if (panel_live) {
        clip = {right, 0, 0x1b9 - right, 0x1e0};
        ScopedClipRect clipped(GetFrameBufferSurface(), clip);
        CatalogArtwork::Load(0x96, 0).drawAndInvalidate(GetFrameBufferSurface(), image, 0xc3,
                                                        0x144);
    }
    DrawPartyMovementGauge(right, image, panel_live, caption);
    if (panel_live) {
        RefreshTrackedPortraitOverlay();
    }
}

/* The movement gauge: a static end frame for the empty/full states and the
   twelve-frame animated bar otherwise, rearming its own countdown each tick. */
void DrawPartyMovementGauge(short right, short image, bool panel_live, int caption)
{
    bool rearm;
    bool advanced;
    int frame;
    int timer_length;
    int frame_base;

    frame_base = 0;
    rearm = false;
    advanced = false;
    if (image == 0) {
        rearm = true;
        frame = 0x18;
    } else if (image == 2) {
        rearm = true;
        frame = 0xc;
    } else {
        if (TimeUntilDeadline(movement->animation_clock) == 0) {
            ++movement->animation_frame;
            if (movement->animation_frame > 0xb) {
                movement->animation_frame = 0;
            }
            rearm = true;
            advanced = true;
        }
        frame = 0;
    }
    W8PartyAction action;
    if (!CanPartyMove() || (CurrentGame().combat->execution_active != 0 && IsPartyEngaged() == 0)) {
        action = CurrentGame().combat->uiCurrentPartyAction;
    } else {
        action = CurrentGame().combat->uiNextPartyAction;
    }
    if (action == W8_PARTY_ACTION_RUN) {
        timer_length = 0x32;
    } else {
        frame_base = 0x24;
        timer_length = 0x5a;
    }
    if (rearm) {
        movement->animation_clock = MonotonicMilliseconds() + (timer_length);
    }
    if (!panel_live) {
        if (advanced) {
            movement->dirty = true;
        }
        return;
    }
    CatalogArtwork::Load(0x95, 0).drawAndInvalidate(GetFrameBufferSurface(),
                                                    movement->animation_frame + frame_base + frame,
                                                    right - 0xf, 0x142);
    InvalidateRegion(right - 0xf, 0x142, right - 1, 0x150);
    if (caption != -1) {
        movement->caption->RenderString(Data().strings[caption], g_wiz_text_font_secondary, false,
                                        GetFrameBufferSurface());
    }
}

void InvalidatePartyMovementPanel(void)
{
    movement->dirty = true;
}

void DisableFreeTurnButton(void)
{
    movement->buttons[0]->SetEnabled(false);
}

void EnableFreeTurnButton(void)
{
    movement->buttons[0]->SetEnabled(true);
}

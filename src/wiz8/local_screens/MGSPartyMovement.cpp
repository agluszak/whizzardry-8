#include "wiz8/local_screens/MGSPartyMovement.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <SDL3/SDL_rect.h>

#include "compat/native.h"
#include "conditions.h"
#include "image.h"
#include "layouts/character.h"
#include "layouts/main_game_screen.h"
#include "layouts/targeting.h"
#include "surrender/srDebug.h"
#include "timer.h"
#include "wiz8/button_interaction.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/fonts.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/CombatPartyMovement.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"
#include "wiz8/xstatus.h"

constexpr float kMovementFatigueTickScale = 0.0004f;

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
    gXStatus.fPartyMovementUi = true;
    g_level_block->move_percent = g_level_block->move_percent_shown = 100;
    return 1;
}

void ReleasePartyMovement(void)
{
    if (g_current_screen_state.id == W8_SCREEN_MAIN_GAME) {
        EraseRegion(0xb1, 0x13f, 0x1cf, 0x153);
    }
    RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_PANEL);
    movement.reset();
    gXStatus.fPartyMovementUi = false;
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

    if (!gXStatus.fCombatMode) {
        Fail("gXStatus.fCombatMode");
    }
    if ((g_combat_state->uiCurrentPartyAction == W8_PARTY_ACTION_NONE ||
         g_combat_state->uiCurrentPartyActionStatus == W8_PARTY_ACTION_FINISHED) &&
        g_combat_state->uiNextPartyAction == W8_PARTY_ACTION_NONE) {
        Fail("((gpCombat->uiCurrentPartyAction != PARTY_ACTION_NONE) && "
             "(gpCombat->uiCurrentPartyActionStatus != ACTION_STATUS_FINISHED)) || "
             "(gpCombat->uiNextPartyAction != PARTY_ACTION_NONE)");
    }
    const int visible = CanPartyMove() ? 1 : 0;
    movement->dirty |= visible != movement->visible_button;
    movement->visible_button = visible;
    for (int index = 0; index < 2; ++index) movement->buttons[index]->SetVisible(index == visible);
    if (visible == 1) movement->buttons[1]->SetEnabled(g_combat_state->round_active);
    panel_live = movement->dirty || movement->buttons[visible]->m_dirty;
    if (panel_live) {
        CatalogArtwork::Load(0x93, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0xb1, 0x13f);
        movement->buttons[visible]->Refresh(true);
        movement->dirty = false;
    }
    right = 0x1b9 - g_level_block->move_percent * 0xf6 / 100;
    if (!CanPartyMove()) {
        if (!IsLevelMovementStopped()) {
            image = 1;
            caption = ((g_level_block->move_percent != 100) - 1 & 0x77f) - 1;
        } else {
            image = 2;
            caption = 0x77d;
        }
    } else {
        image = 0;
        if (g_combat_state->execution_active != 0 && IsPartyEngaged() == 0) {
            caption = (g_combat_state->uiCurrentPartyAction != W8_PARTY_ACTION_RUN) + 0x77b;
        } else {
            caption = (g_combat_state->uiNextPartyAction != W8_PARTY_ACTION_RUN) + 0x77b;
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
    unsigned int action;
    if (!CanPartyMove() || (g_combat_state->execution_active != 0 && IsPartyEngaged() == 0)) {
        action = g_combat_state->uiCurrentPartyAction;
    } else {
        action = g_combat_state->uiNextPartyAction;
    }
    if (action == 2) {
        timer_length = 0x32;
    } else {
        frame_base = 0x24;
        timer_length = 0x5a;
    }
    if (rearm) {
        movement->animation_clock = w8_get_ticks() + (timer_length);
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
        movement->caption->RenderString(g_localized_strings[caption], g_wiz_text_font_secondary,
                                        false, GetFrameBufferSurface());
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

/* Per-frame movement/fatigue processing: each occupied party slot with stamina
   and a bearable load accumulates distance scaled by load, doubled in combat
   and raised again while the second condition runs, until the accumulator
   crosses 2500 and converts into real fatigue. */
unsigned char HandlePartyMovement(float* real_elapsed, float* frame_elapsed)
{
    int party_slot;
    float amount;
    float multiplier;
    unsigned int ticks;

    if (ConsumeLevelElapsedTime(real_elapsed, frame_elapsed) == 0) {
        return 0;
    }
    for (party_slot = 0; party_slot < W8_PARTY_SLOT_COUNT; ++party_slot) {
        W8PartySlotRow* row = &g_status.buffers.XChar[party_slot];
        W8Character* character = &g_status.buffers.Char[party_slot];
        if (!row->fOccupied || character->stamina <= 0 ||
            character->highest_condition >= W8_CONDITION_ASLEEP) {
            continue;
        }
        amount = *frame_elapsed;
        if (g_status.search_mode != 0 || gXStatus.fCombatMode) {
            amount = *real_elapsed * 0.5f + amount;
        }
        switch (character->load_category) {
        case 0:
            multiplier = 1.0f;
            break;
        case 1:
            multiplier = 1.25f;
            break;
        case 2:
            multiplier = 1.5f;
            break;
        case 3:
            multiplier = 2.0f;
            break;
        case 4:
            multiplier = 3.0f;
            break;
        default:
            Fail("FALSE", "HandlePartyMovement: ERROR - Invalid load category");
        }
        if (gXStatus.fCombatMode) {
            multiplier += multiplier;
        }
        if (character->uiCondition[W8_CONDITION_DISEASED] != 0) {
            multiplier *= 1.5f;
        }
        row->movement_fatigue = multiplier * amount + row->movement_fatigue;
        if (row->movement_fatigue > g_position_height_epsilon) {
            ticks = static_cast<unsigned int>(row->movement_fatigue * kMovementFatigueTickScale);
            FatigueCharacter(party_slot, ticks, false, 0);
            row->movement_fatigue = row->movement_fatigue - (ticks * 0x9c4);
        }
    }
    return 1;
}

#include "wiz8/local_screens/MGSPartyMovement.h"

#include <memory>
#include <SDL3/SDL_rect.h>
#include <array>
#include <cstdint>
#include <vector>

#include "image.h"
#include "input.h"
#include "timer.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/float_constants.h"
#include "wiz8/fonts.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/CombatPartyMovement.h"
#include "wiz8/local_code/Controls.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_code/TextControl.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/regions.h"
#include "wiz8/sr_api.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/xstatus.h"
#include "compat/native.h"
#include "conditions.h"
#include "layouts/character.h"
#include "layouts/main_game_screen.h"
#include "layouts/targeting.h"

#define PARTY_MOVEMENT_CPP __FILE__

/* Retail callback IDs 0 and 1 index this base, and ReleasePartyMovement walks
   the two-pointer span. */
// GLOBAL: WIZ8 0x0069BF40
static W8TextControl* g_party_movement_buttons[2];
// GLOBAL: WIZ8 0x0069BF48
uint64_t g_party_movement_animation_clock;
// GLOBAL: WIZ8 0x0069BF4C
std::unique_ptr<Controls> g_party_movement_panel;
// GLOBAL: WIZ8 0x0069BF50
std::unique_ptr<W8TextBuffer> g_party_movement_caption;
// GLOBAL: WIZ8 0x0069BF54
unsigned int g_party_movement_animation_frame;

// Retail set 0x1c, regions 200-201.
static constexpr std::array<RegionTemplate, 2> g_party_movement_region_templates{{
    {0x00000001, 0, 0, 0, 0, FreeTurnButtonRegionEvent, 0, true, 37},
    {0x00000001, 0, 0, 0, 0, FreeTurnButtonRegionEvent, 1, true, 38},
}};
ScreenRegionSet g_party_movement_regions{g_party_movement_region_templates, 0x1c};

// GLOBAL: WIZ8 0x005EECD0
const float g_movement_fatigue_tick_scale = 0.0004f;

void DrawPartyMovementGauge(short right, short image, bool panel_live, int caption);

unsigned char CreatePartyMovementPanel(void)
{
    W8ControlsRect bounds;

    g_party_movement_animation_frame = 0;
    g_party_movement_panel.reset();
    g_party_movement_caption.reset();
    g_party_movement_buttons[0] = 0;
    g_party_movement_buttons[1] = 0;

    g_party_movement_panel = std::make_unique<Controls>(0xb1, 0x13f, 0x1cf, 0x153, CatalogArtwork::Load(0x93, 0), 0);

    bounds.left = 0xc3;
    bounds.right = 0x1b9;
    bounds.top = 0x13f;
    bounds.bottom = 0x153;
    g_party_movement_caption =
        std::make_unique<W8TextBuffer>(&bounds, std::string_view{}, g_wiz_text_font_secondary,
                                       g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter, 4);

    g_party_movement_buttons[0] =
        g_party_movement_panel->EmplaceControl<W8TextControl>(g_party_movement_regions->index(0), 0x10a, 0, 0x11e,
                          0x14, CatalogArtwork::Load(0x94, 0), 4, 6, 5, 6, 7);
    g_party_movement_buttons[1] =
        g_party_movement_panel->EmplaceControl<W8TextControl>(g_party_movement_regions->index(1), 0x10a, 0, 0x11e,
                          0x14, CatalogArtwork::Load(0x94, 0), 4, 6, 5, 6, 7);

    if (g_party_movement_panel && g_party_movement_buttons[0] != 0 &&
        g_party_movement_buttons[1] != 0) {
        g_party_movement_buttons[0]->m_primaryActivationCallback = BeginFreeTurnPhase;
        g_party_movement_buttons[1]->m_primaryActivationCallback = CancelPartyMovement;
        g_party_movement_regions.Enable();
        g_party_movement_panel->SetEnabled(true);
        g_party_movement_buttons[0]->SetActive(false);
        gXStatus.fPartyMovementUi = true;
        g_level_block->move_percent = 100;
        g_level_block->move_percent_shown = 100;
        g_party_movement_panel->Invalidate(0);
        return 1;
    }
    ReleasePartyMovement();
    return 0;
}

void ReleasePartyMovement(void)
{
    if (g_current_screen_state.id == W8_SCREEN_MAIN_GAME) {
        ClearSurfaceRect(0xb1, 0x13f, 0x1cf, 0x153);
        InvalidateRegion(0xb1, 0x13f, 0x1cf, 0x153);
    }
    RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_PANEL);
    g_party_movement_regions.Disable();
    g_party_movement_panel.reset();
    for (auto& button : g_party_movement_buttons) button = nullptr;
    g_party_movement_caption.reset();
    gXStatus.fPartyMovementUi = false;
}

void UpdatePartyMovementPanel(void)
{
    g_party_movement_regions.Enable();
    if (CanPartyMove()) {
        g_party_movement_buttons[1]->SetActive(true);
        g_party_movement_buttons[0]->SetActive(false);
        return;
    }
    g_party_movement_buttons[0]->SetActive(true);
    g_party_movement_buttons[1]->SetActive(false);
}

void DisablePartyMovementRegions(void)
{
    g_party_movement_regions.Disable();
}

void DrawPartyMovementPanel(void)
{
    SDL_Rect clip;
    bool panel_live;
    int caption;
    short right;
    int image;

    if (!gXStatus.fCombatMode) {
        srAssertFail("gXStatus.fCombatMode", PARTY_MOVEMENT_CPP, 0xf6, 0);
    }
    if ((g_combat_state->uiCurrentPartyAction == W8_PARTY_ACTION_NONE ||
         g_combat_state->uiCurrentPartyActionStatus == W8_PARTY_ACTION_FINISHED) &&
        g_combat_state->uiNextPartyAction == W8_PARTY_ACTION_NONE) {
        srAssertFail("((gpCombat->uiCurrentPartyAction != PARTY_ACTION_NONE) && "
                     "(gpCombat->uiCurrentPartyActionStatus != ACTION_STATUS_FINISHED)) || "
                     "(gpCombat->uiNextPartyAction != PARTY_ACTION_NONE)",
                     PARTY_MOVEMENT_CPP, 0xfb, 0);
    }
    panel_live = g_party_movement_panel->m_fEnabled &&
                 (g_party_movement_panel->m_fDirty || g_party_movement_panel->m_fLayoutDirty);
    if (!CanPartyMove()) {
        if (!g_party_movement_buttons[0]->m_active) {
            g_party_movement_buttons[0]->SetActive(true);
            g_party_movement_buttons[0]->Invalidate(false);
        }
        if (g_party_movement_buttons[1]->m_active) {
            g_party_movement_buttons[1]->SetActive(false);
        }
    } else {
        if (!g_party_movement_buttons[1]->m_active) {
            g_party_movement_buttons[1]->SetActive(true);
            g_party_movement_buttons[1]->Invalidate(false);
        }
        if (g_party_movement_buttons[0]->m_active) {
            g_party_movement_buttons[0]->SetActive(false);
        }
        if (!g_combat_state->round_active) {
            g_party_movement_buttons[1]->SetEnabled(false);
        } else {
            g_party_movement_buttons[1]->SetEnabled(true);
        }
    }
    g_party_movement_panel->Redraw();
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
        CatalogArtwork::Load(0x96, 0).drawAndInvalidate(GetFrameBufferSurface(), image, 0xc3, 0x144);
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
        if (TimeUntilDeadline(g_party_movement_animation_clock) == 0) {
            ++g_party_movement_animation_frame;
            if (g_party_movement_animation_frame > 0xb) {
                g_party_movement_animation_frame = 0;
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
        g_party_movement_animation_clock = w8_get_ticks() + (timer_length);
    }
    if (!panel_live) {
        if (advanced) {
            g_party_movement_panel->Invalidate(0);
        }
        return;
    }
    CatalogArtwork::Load(0x95, 0).drawAndInvalidate(GetFrameBufferSurface(), g_party_movement_animation_frame + frame_base + frame, right - 0xf, 0x142);
    InvalidateRegion(right - 0xf, 0x142, right - 1, 0x150);
    if (caption != -1) {
        g_party_movement_caption->RenderString(g_localized_strings[caption], g_wiz_text_font_secondary,
                                               false, GetFrameBufferSurface());
    }
}

void InvalidatePartyMovementPanel(void)
{
    g_party_movement_panel->Invalidate(0);
}

unsigned char FreeTurnButtonRegionEvent(const InputEvent* event, W8Region* region, unsigned short id)
{
    if (!gXStatus.fPartyMovementUi) {
        return 0;
    }
    return DispatchButtonRegionEvent(event, region, g_party_movement_buttons[id]);
}

void DisableFreeTurnButton(void)
{
    g_party_movement_buttons[0]->SetEnabled(false);
}

void EnableFreeTurnButton(void)
{
    g_party_movement_buttons[0]->SetEnabled(true);
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
            amount = *real_elapsed * g_float_half + amount;
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
            srAssertFail("FALSE", PARTY_MOVEMENT_CPP, 0x2a9,
                         "HandlePartyMovement: ERROR - Invalid load category");
        }
        if (gXStatus.fCombatMode) {
            multiplier += multiplier;
        }
        if (character->uiCondition[W8_CONDITION_DISEASED] != 0) {
            multiplier *= g_float_one_and_a_half;
        }
        row->movement_fatigue = multiplier * amount + row->movement_fatigue;
        if (row->movement_fatigue > g_position_height_epsilon) {
            ticks =
                static_cast<unsigned int>(row->movement_fatigue * g_movement_fatigue_tick_scale);
            FatigueCharacter(party_slot, ticks, false, 0);
            row->movement_fatigue = row->movement_fatigue - (ticks * 0x9c4);
        }
    }
    return 1;
}

#include "himage.h"
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>
#include <fmt/printf.h>
#include <string_view>
#include <string>
#include <array>
#include <vector>

#include "image.h"
#include "input.h"
#include "sprite.h"
#include "wiz8/spell_ids.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/integer_constants.h"
#include "wiz8/local_screens/RCSCommon.h"
#include "wiz8/local_screens/MGSFormation.h"
#include "wiz8/local_screens/MGSPortraits.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/local_screens/RCSItemsPage.h"
#include "wiz8/local_screens/RCSStatsPage.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_code/TextControl.h"
#include "wiz8/local_code/Controls.h"
#include "wiz8/layouts/character.h"
#include "wiz8/character_skills.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/targeting.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/dialog_code/MessageDialogBase.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/fonts.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/video_object_catalog.h"
#include "wiz8/cursor.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/regions.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/sr_api.h"
#include "wiz8/xstatus.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/item_video_object_vector.h"
#include "wiz8/learned_spells.h"
#include "Font.h"
#include "conditions.h"
#include "dialog_code/DialogBase.h"
#include "equipment_slots.h"
#include "layouts/gameplay_databases.h"
#include "layouts/item_instance.h"
#include "layouts/main_game_screen.h"

// GLOBAL: WIZ8 0x0069c3c4
Controls* g_level_up_panel;
// GLOBAL: WIZ8 0x0069c3c8
Controls* g_dismiss_panel;

/* The eight item-page action buttons, indexed by the mode SetCampItemActionMode
   arms: one control per actionable entry-mode, cleared before each reselection. */
// GLOBAL: WIZ8 0x0069c3cc
W8TextControl* g_item_action_controls[W8_CAMP_ACTION_BUTTON_COUNT];
/* Five page buttons (help 2364-2368) packed immediately before the dismiss
   button; CreateCampButtonPanel allocates them on the shared bottom Controls panel. */
// GLOBAL: WIZ8 0x0069c3ec
W8TextControl* g_camp_page_buttons[5];
// GLOBAL: WIZ8 0x0069c3c0
W8TextControl* g_level_up_button;
// GLOBAL: WIZ8 0x0069c400
W8TextControl* g_dismiss_button;

/* The shared bottom panel CreateCampButtonPanel parents the five page
   buttons and the eight item-action controls to. */
// GLOBAL: WIZ8 0x0069c404
Controls* g_item_actions_panel;

/* The level-line format the header draws - name, level and the
   profession's level-band title. */

/* The page and item-action button callbacks CreateCampButtonPanel
   wires into m_primaryActivationCallback. */
static void OnCampPageButton0(void);
static void OnCampPageButton1(void);
static void OnCampPageButton2(void);
static void OnCampPageButton3(void);
static void CampPageDismissAction(void);
static void OnCampIdentifyItem(void);
static void OnCampMoveItem(void);
static void OnCampSplitStack(void);
static void OnCampUseItem(void);
static void OnCampDropItem(void);
static void OnCampCastSpell(void);
static void OnCampUseItemOnItem(void);
static void OnCampUseItemOnCharacter(void);

static void ShowDismissCharacterDialog(void);
static void OnDismissCharacterDialogClosed(W8DialogBase* dialog);

/* Swap the reviewed party member: rebuild the item list or learned-spell
   scratch for the new character and repaint whichever page is showing. */
// FUNCTION: WIZ8 0x005B6B30
void SelectCampCharacter(int slot)
{
    giReviewCharSlot = slot;
    g_review_character = &g_status.buffers.Char[slot];
    SyncReviewCharInputRegion();
    switch (g_camp_screen->page) {
    case W8_CAMP_PAGE_ITEMS:
        EnableCampActionButtons();
        if (g_camp_screen->item_filters[W8_CAMP_ITEM_FILTER_USABLE] != 0) {
            g_camp_screen->item_scroll = 0;
            RebuildCampItemList();
        }
        if (g_camp_screen->item_action != W8_CAMP_ITEM_ACTION_IDENTIFY &&
            g_camp_screen->item_action != W8_CAMP_ITEM_ACTION_IDENTIFY_SPELL &&
            g_camp_screen->item_action != W8_CAMP_ITEM_ACTION_USE_ON_ITEM) {
            SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
        }
        break;
    case W8_CAMP_PAGE_STATS:
        g_camp_screen->effect_selection = 0;
        RebuildCampEffectList();
        g_camp_screen->character_info->Invalidate(0);
        break;
    case W8_CAMP_PAGE_SPELLS:
        BuildLearnedSpellState(&g_camp_screen->learned_spells, g_review_character);
        RefreshCampSpellRanges();
        break;
    default:
        break;
    }
    g_camp_screen->redraw_flags |= W8_CAMP_REDRAW_ALL;
}

/* Give the held item to a camp portrait on right-click: refuse dead/insane/
   stoned with a notice, check eligibility and combat action allowance, then
   AddItemToCharacter. */
// FUNCTION: WIZ8 0x005B6C10
void TryGiveHeldItemToCampPortrait(int slot)
{
    W8Character* character;

    if (!g_status.buffers.XChar[slot].fOccupied) {
        return;
    }
    character = &g_status.buffers.Char[slot];
    if (character->uiCondition[W8_CONDITION_MISSING] != 0) {
        ShowCampNoticeLine(g_localized_strings[0x907], 0, true, false);
        return;
    }
    if (character->uiCondition[W8_CONDITION_WEBBED] != 0) {
        ShowCampNoticeLine(g_localized_strings[0x908], 0, true, false);
        return;
    }
    if (character->uiCondition[W8_CONDITION_TURNCOAT] != 0) {
        ShowCampNoticeLine(g_localized_strings[0x909], 0, true, false);
        return;
    }
    if (!IsPartySlotEligible(slot)) {
        ShowCampNoticeLine(g_localized_strings[0x901], 0, true, false);
        return;
    }
    if (!IsCampActionAllowed(slot)) {
        return;
    }
    AddItemToCharacter(character, &g_status.item_in_hand, false, false, false);
}

// FUNCTION: WIZ8 0x005b6d20
bool CanSelectRcsPartySlot(int ui_slot)
{
    if (!g_status.buffers.XChar[ui_slot].fOccupied) {
        srAssertFail("gStatus.XChar[uiSlot].fOccupied",
                     "C:\\Projects\\Wizardry 8\\Local Screens\\RCSCommon.cpp", 0x9ac, 0);
    }

    W8Character* character = &g_status.buffers.Char[ui_slot];
    if (character->uiCondition[W8_CONDITION_MISSING] != 0) {
        return false;
    }
    if (character->uiCondition[W8_CONDITION_WEBBED] != 0) {
        return false;
    }
    if (character->uiCondition[W8_CONDITION_INSANE] != 0) {
        return false;
    }
    if (character->uiCondition[W8_CONDITION_TURNCOAT] != 0) {
        return false;
    }
    if (gXStatus.fCombatMode) {
        if (!g_combat_state->equip_phase) {
            return false;
        }
        if (g_status.buffers.XChar[ui_slot].pending_action != W8_ACTION_EQUIP) {
            return false;
        }
    }
    return true;
}

// FUNCTION: WIZ8 0x005b6df0
void DrawRcsText(std::string_view text, int left, int top, int width, unsigned int layout_mode)
{
    W8ControlsRect bounds = {left, top, left + width, top + 12};
    W8TextBuffer buffer(&bounds, text, g_wiz_text_font_secondary, layout_mode, 4);
    buffer.RenderToTarget(0, false, GetFrameBufferSurface());
}

// FUNCTION: WIZ8 0x005b6e90
void DrawRcsBoldText(std::string_view text, int left, int top, int width, unsigned int layout_mode)
{
    W8ControlsRect bounds = {left, top, left + width, top + 12};
    W8TextBuffer buffer(&bounds, text, g_wiz_text_bold_font, layout_mode, 4);
    buffer.RenderToTarget(0, false, GetFrameBufferSurface());
}

// FUNCTION: WIZ8 0x005b6f30
void DrawTallRcsText(std::string_view text, int left, int top, int width, unsigned int layout_mode)
{
    W8ControlsRect bounds = {left, top, left + width, top + 18};
    W8TextBuffer buffer(&bounds, text, g_wiz_text_font_secondary, layout_mode, 4);
    buffer.RenderToTarget(0, false, GetFrameBufferSurface());
}

/* Draws text honoring the same layout mask pairs as the buffered variants
   above, through gprintfDirty with the current font: the centered and right
   masks shift the start by the measured string length and the baseline is
   centered on the caller-provided height. */
// FUNCTION: WIZ8 0x005b6fd0
void DrawRcsTextJustified(std::string_view text, int left, int top, int width, int height,
                          unsigned int layout_mode)
{
    if (layout_mode == (g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter)) {
        left +=
            (width - g_wiz_text_font_secondary.textWidth(text)) / 2;
    } else if (layout_mode == (g_W8TextBufferAlignRight | g_W8TextBufferAlignMiddle)) {
        left += width - g_wiz_text_font_secondary.textWidth(text);
    }
    top += (height - g_wiz_text_font_secondary.height()) / 2;
    SetFont(g_wiz_text_font_secondary);
    DrawFontTextDirty(left, top, text);
}

// FUNCTION: WIZ8 0x005b6630
static void OpenLevelUpCharacterScreen(void)
{
    if (!g_status.buffers.XChar[giReviewCharSlot].fOccupied) {
        srAssertFail("fCHAR_OCCUPIED(giReviewCharSlot)",
                     "C:\\Projects\\Wizardry 8\\Local Screens\\RCSCommon.cpp", 0x888, 0);
    }
    g_current_screen_state.parameter_2 = giReviewCharSlot;
    g_current_screen_state.parameter_3 = g_review_character;
    g_pending_screen_state.parameter_3 = &g_status.buffers.Char[giReviewCharSlot];
    g_pending_screen_state.mode = 2;
    SetPendingScreenState(W8_SCREEN_CHARACTER);
}

/* Dismiss-confirm portrait/name hitbox (help 2368): click dismisses the
   reviewed party member; mouse enter/leave toggles portrait_hovered[0]. */
// FUNCTION: WIZ8 0x005B5E90
unsigned char CampDismissPortraitRegionEvent(const InputEvent* event, W8Region* region)
{
    const InputEventType us_event = event->type;

    if (us_event == InputEventType::LeftButtonDown) {
        region->flags |= W8_REGION_LEFT_BUTTON_HELD;
        return 1;
    }
    if (us_event != InputEventType::LeftButtonUp) {
        if (us_event != InputEventType::MouseMove) {
            return 0;
        }
        if ((region->flags & W8_REGION_MOUSE_LEAVE) == 0) {
            if ((region->flags & W8_REGION_MOUSE_ENTER) == 0) {
                return 0;
            }
            g_camp_screen->portrait_hovered[0] = 1;
        } else {
            g_camp_screen->portrait_hovered[0] = 0;
        }
        g_camp_screen->redraw_flags |= W8_CAMP_REDRAW_PORTRAIT;
        return 0;
    }
    if ((region->flags & W8_REGION_LEFT_BUTTON_HELD) == 0) {
        return 1;
    }
    DismissSelectedPartyCharacter();
    return 1;
}

/* Eight party portrait slots on the camp screen. Targeting modes 1 and 7 aim
   spells/items; otherwise a click selects the reviewed character. */
// FUNCTION: WIZ8 0x005B5F10
unsigned char CampPortraitSlotRegionEvent(const InputEvent* event, W8Region* region)
{
    const InputEventType us_event = event->type;
    int target_slot = region->callback_id;

    if (us_event <= InputEventType::RightButtonDown) {
        if (region->CaptureButtonDown(us_event)) {
            return 1;
        }
        if (us_event != InputEventType::LeftButtonUp) {
            return 0;
        }
        if ((region->flags & W8_REGION_LEFT_BUTTON_HELD) != 0 &&
            g_status.buffers.XChar[target_slot].fOccupied) {
            if (gXStatus.iTargetingMode == W8_TARGET_NEED_ALLY) {
                if (g_camp_screen->page != W8_CAMP_PAGE_ITEMS ||
                    (g_camp_screen->item_action != W8_CAMP_ITEM_ACTION_CAST_SPELL &&
                     g_camp_screen->item_action != W8_CAMP_ITEM_ACTION_USE_ON_CHARACTER)) {
                    if (!CanPartySlotParticipate(target_slot)) {
                        QueueCharacterEvent(&g_status.buffers.Char[giReviewCharSlot],
                                            g_character_event_kind2, 0, g_character_event_no_flags,
                                            g_character_event_full_volume);
                        return 1;
                    }
                    AimAtCharacter(giReviewCharSlot, target_slot, W8_TARGETING_CONTEXT_CURRENT);
                    StartBreathCycle(giReviewCharSlot, false);
                    return 1;
                }
                if (g_status.buffers.Char[target_slot].uiCondition[W8_CONDITION_MISSING] == 0) {
                    if (g_camp_screen->item_action != W8_CAMP_ITEM_ACTION_CAST_SPELL) {
                        TargetCharacterWithHeldItem(target_slot);
                        return 1;
                    }
                    ReportCastResult(target_slot);
                    return 1;
                }
            } else {
                if (gXStatus.iTargetingMode == W8_TARGET_NEED_CHARACTER_INDIRECT) {
                    if (!IsDeadCharacterTargetable(target_slot)) {
                        QueueCharacterEvent(&g_status.buffers.Char[giReviewCharSlot],
                                            g_character_event_kind2, 0, g_character_event_no_flags,
                                            g_character_event_full_volume);
                        return 1;
                    }
                    AimAtCharacterIndirect(giReviewCharSlot, target_slot,
                                           W8_TARGETING_CONTEXT_CURRENT);
                    StartBreathCycle(giReviewCharSlot, false);
                    return 1;
                }
                if (giReviewCharSlot != target_slot) {
                    SelectCampCharacter(target_slot);
                    return 1;
                }
            }
        }
    } else {
        if (us_event != InputEventType::RightButtonUp) {
            if (us_event != InputEventType::MouseMove) {
                return 0;
            }
            if ((region->flags & W8_REGION_MOUSE_TRANSITION_MASK) != 0) {
                g_camp_screen->redraw_flags |= 1u << (region->callback_id & 0x1f);
                if ((g_camp_screen->item_action == W8_CAMP_ITEM_ACTION_CAST_SPELL ||
                     g_camp_screen->item_action == W8_CAMP_ITEM_ACTION_USE_ON_CHARACTER) &&
                    g_status.buffers.Char[target_slot].uiCondition[W8_CONDITION_MISSING] == 0) {
                    if ((region->flags & W8_REGION_MOUSE_ENTER) != 0) {
                        UpdateItemCursorForState(1, 0, target_slot);
                        return 0;
                    }
                    UpdateItemCursorForState(0, 0, target_slot);
                }
            }
            return 0;
        }
        if ((region->flags & W8_REGION_RIGHT_BUTTON_HELD) != 0 && g_status.item_in_cursor) {
            TryGiveHeldItemToCampPortrait(target_slot);
        }
    }
    return 1;
}

/* Opens the character editor (W8_SCREEN_CHARACTER, mode 1) for the reviewed
   party member. Help 2362. */
// FUNCTION: WIZ8 0x005B61A0
unsigned char CampOpenCharacterScreenRegionEvent(const InputEvent* event, W8Region* region)
{
    const InputEventType us_event = event->type;

    if (us_event == InputEventType::LeftButtonDown) {
        region->flags |= W8_REGION_LEFT_BUTTON_HELD;
    } else {
        if (us_event != InputEventType::LeftButtonUp) {
            if (us_event != InputEventType::MouseMove) {
                return 0;
            }
            if ((region->flags & W8_REGION_MOUSE_TRANSITION_MASK) != 0) {
                g_camp_screen->redraw_flags |= 0x200;
            }
            return 0;
        }
        if ((region->flags & W8_REGION_LEFT_BUTTON_HELD) != 0) {
            g_current_screen_state.parameter_3 = g_review_character;
            g_current_screen_state.parameter_2 = giReviewCharSlot;
            g_pending_screen_state.mode = 1;
            g_pending_screen_state.parameter_3 = g_review_character;
            SetPendingScreenState(W8_SCREEN_CHARACTER);
            return 1;
        }
    }
    return 1;
}

/* Holding the header region (help 2363) shows profession history; releasing
   it restores the summary and clears the active hover region. */
// FUNCTION: WIZ8 0x005B6220
unsigned char CampProfessionHistoryRegionEvent(const InputEvent* event, W8Region* region)
{
    const InputEventType us_event = event->type;

    if (us_event == InputEventType::LeftButtonDown) {
        region->flags |= W8_REGION_LEFT_BUTTON_HELD;
        SetCampHeaderMode(W8_CAMP_HEADER_PROFESSION_HISTORY);
        ActivateDialogRegion(g_camp_screen->hover_region);
    } else {
        if (us_event != InputEventType::LeftButtonUp) {
            if (us_event == InputEventType::MouseMove) {
                if ((region->flags & W8_REGION_MOUSE_TRANSITION_MASK) != 0) {
                    g_camp_screen->redraw_flags |= 0x200;
                }
                return 0;
            }
            return 0;
        }
        if ((region->flags & W8_REGION_LEFT_BUTTON_HELD) == 0) {
            return 1;
        }
        SetCampHeaderMode(W8_CAMP_HEADER_SUMMARY);
        ClearActiveRegionIfMatches(g_camp_screen->hover_region);
    }
    g_camp_screen->redraw_flags |= 0x7ff;
    return 1;
}

/* Shared handler for the five bottom page buttons in g_camp_page_buttons. */
// FUNCTION: WIZ8 0x005B62C0
unsigned char CampPageButtonRegionEvent(const InputEvent* event, W8Region* region)
{
    return DispatchButtonRegionEvent(event, region, g_camp_page_buttons[region->callback_id]);
}

/* Shared handler for the eight item-action buttons. */
// FUNCTION: WIZ8 0x005B6360
unsigned char CampItemActionRegionEvent(const InputEvent* event, W8Region* region)
{
    return DispatchButtonRegionEvent(event, region, g_item_action_controls[region->callback_id]);
}

// FUNCTION: WIZ8 0x005b6400
void CreateRcsLevelUpPanel(void)
{
    g_level_up_panel = 0;
    g_level_up_button = 0;

    g_level_up_panel = new Controls(0xe9, 0x3f, 0xfb, 0x51, -1, 0, -1);
    if (g_level_up_panel == 0) {
        srAssertFail("gpLevelUpPanel", "C:\\Projects\\Wizardry 8\\Local Screens\\RCSCommon.cpp",
                     0x84e, 0);
    }

    g_level_up_button =
        new W8TextControl(g_level_up_panel, g_camp_main_regions->index(0), 0, 0, 0x12, 0x12, 0xa7, 0, 0, 2, 1, 4, 3);
    if (g_level_up_button == 0) {
        srAssertFail("gpLevelUpButton", "C:\\Projects\\Wizardry 8\\Local Screens\\RCSCommon.cpp",
                     0x852, 0);
    }
    g_level_up_button->m_primaryActivationCallback = OpenLevelUpCharacterScreen;
    g_level_up_panel->SetEnabled(true);
    g_level_up_button->SetActive(false);
}
/* Ask the first panel to redraw all of itself. A null rectangle is how
   Controls::Invalidate spells "the whole area", so these are not a separate
   one-argument redraw slot - they are the panel class Local Code\Controls.cpp
   models, reached through its second vtable slot. */
// FUNCTION: WIZ8 0x005b6590
void RedrawRcsLevelUpPanel(void)
{
    g_level_up_panel->Invalidate(0);
}

/* Redraw the second. */
// FUNCTION: WIZ8 0x005b68d0
void RedrawRcsDismissPanel(void)
{
    g_dismiss_panel->Invalidate(0);
}

/* Tear the first panel down, then destroy its separate widget. */
// FUNCTION: WIZ8 0x005b6540
void DestroyRcsLevelUpPanel(void)
{
    DestroyControlPanel(g_level_up_panel);
    g_level_up_button = nullptr;
}

// FUNCTION: WIZ8 0x005b65a0
void UpdateRcsLevelUpPanel(void)
{
    bool enabled = IsCharacterReadyToAdvance(giReviewCharSlot);
    if (!enabled || gXStatus.fCombatMode ||
        (!g_status.buffers.XChar[giReviewCharSlot].portrait_advance && g_status.game_started) ||
        gXStatus.fCampMode) {
        if (g_level_up_button->m_active) {
            g_level_up_button->SetActive(false);
        }
    } else if (!g_level_up_button->m_active) {
        g_level_up_button->SetActive(true);
        RedrawRcsLevelUpPanel();
    }
    g_level_up_panel->Redraw();
}

/* Single level-up button (help 1984). Assembly writes region flags through the
   second parameter; Ghidra wrongly folded them into the event pointer. */
// FUNCTION: WIZ8 0x005B66B0
unsigned char CampLevelUpButtonRegionEvent(const InputEvent* event, W8Region* region)
{
    return DispatchButtonRegionEvent(event, region, g_level_up_button);
}

// FUNCTION: WIZ8 0x005b6740
void CreateRcsDismissPanel(void)
{
    g_dismiss_panel = 0;
    g_dismiss_button = 0;

    g_dismiss_panel = new Controls(0xa7, 0x41, 0xbe, 0x51, -1, 0, -1);
    if (g_dismiss_panel == 0) {
        srAssertFail("gpDismissPanel", "C:\\Projects\\Wizardry 8\\Local Screens\\RCSCommon.cpp",
                     0x8cf, 0);
    }

    g_dismiss_button =
        new W8TextControl(g_dismiss_panel, g_camp_main_regions->index(1), 0, 0, 0x17, 0x10, g_camp_item_pool_regions->index(3), 0, 0, 2, 1, 2, 3);
    if (g_dismiss_button == 0) {
        srAssertFail("gpDismissButton", "C:\\Projects\\Wizardry 8\\Local Screens\\RCSCommon.cpp",
                     0x8d3, 0);
    }
    g_dismiss_button->m_primaryActivationCallback = ShowDismissCharacterDialog;
    g_dismiss_panel->SetEnabled(true);
    g_dismiss_button->SetActive(false);
}

// FUNCTION: WIZ8 0x005b6950
static void ShowDismissCharacterDialog(void)
{
    if (!g_status.buffers.XChar[giReviewCharSlot].fOccupied) {
        srAssertFail("fCHAR_OCCUPIED(giReviewCharSlot)",
                     "C:\\Projects\\Wizardry 8\\Local Screens\\RCSCommon.cpp", 0x90a, 0);
    }

    W8MessageDialogBase* dialog =
        static_cast<W8MessageDialogBase*>(CreateDialogByKind(W8_DIALOG_MESSAGE));
    dialog->SetClientExtent(0xfa, 200);

    W8Character* character = &g_status.buffers.Char[giReviewCharSlot];
    std::string format;
    if (character->uiCondition[W8_CONDITION_MISSING] == 0) {
        if (character->uiCondition[W8_CONDITION_DEAD] == 0) {
            format = g_localized_strings[0x92d];
        } else {
            format = g_localized_strings[0x92e];
        }
    } else {
        format = g_localized_strings[0x92f];
    }
    dialog->SetMessage(fmt::sprintf(format, character->name), 1, 0x32, true, true, true, true, 0,
                       0x15e);
    SetDialogDestroyCallback(dialog, OnDismissCharacterDialogClosed);
    DisplayCampDialog(dialog);
}

// FUNCTION: WIZ8 0x005b6a60
static void OnDismissCharacterDialogClosed(W8DialogBase* base)
{
    if (GetDialogResult(base) && g_status.buffers.XChar[giReviewCharSlot].npc_index != -1) {
        gXStatus.review_character_slot = static_cast<unsigned short>(giReviewCharSlot);
        DismissSelectedPartyCharacter();
    }
}

/* Single dismiss button (help 2387). Same region-flag pattern as the level-up
   button. */
// FUNCTION: WIZ8 0x005B6AA0
unsigned char CampDismissButtonRegionEvent(const InputEvent* event, W8Region* region)
{
    return DispatchButtonRegionEvent(event, region, g_dismiss_button);
}

/* The same for the second panel and its widget. */
// FUNCTION: WIZ8 0x005b6880
void DestroyRcsDismissPanel(void)
{
    DestroyControlPanel(g_dismiss_panel);
    g_dismiss_button = nullptr;
}

/* Bring the second panel up to date. Its widget is available only for the first
   two party slots, out of combat and out of camp; enabling it also invalidates the
   panel, disabling it does not. Either way the panel is then updated. */
// FUNCTION: WIZ8 0x005b68e0
void UpdateRcsDismissPanel(void)
{
    if ((giReviewCharSlot == 0 || giReviewCharSlot == 1) && !gXStatus.fCombatMode &&
        !gXStatus.fCampMode) {
        if (!g_dismiss_button->m_active) {
            g_dismiss_button->SetActive(true);
            RedrawRcsDismissPanel();
        }
    } else if (g_dismiss_button->m_active) {
        g_dismiss_button->SetActive(false);
    }
    g_dismiss_panel->Redraw();
}

/* Formation storage. Physical retail TU attribution remains unresolved;
   the lifecycle interface is owned by MGSFormation.h. */

// GLOBAL: WIZ8 0x0069c2ec
Controls* g_formation_panel;
// GLOBAL: WIZ8 0x0069c344
W8TextControl* g_formation_cell_controls[15];
// GLOBAL: WIZ8 0x0069c384
W8TextControl* g_formation_cell_overlays[15];
// GLOBAL: WIZ8 0x0069c2f8
W8TextControl* g_formation_action_buttons[3];

/* Release the formation board, compass and overlay sprites. */
// FUNCTION: WIZ8 0x005B1C00
void ReleaseFormationBoard(void)
{
    if (g_level_block->formation_board_sprite != 0) {
        ReleaseObject(g_level_block->formation_board_sprite);
        g_level_block->formation_board_sprite = 0;
    }
    if (g_level_block->formation_compass_sprite != 0) {
        ReleaseObject(g_level_block->formation_compass_sprite);
        g_level_block->formation_compass_sprite = 0;
    }
    if (g_level_block->formation_overlay_sprite != 0) {
        ReleaseObject(g_level_block->formation_overlay_sprite);
        g_level_block->formation_overlay_sprite = 0;
    }
}

// FUNCTION: WIZ8 0x005B2580
void DestroyFormationPanel(void)
{
    DestroyControlPanel(g_formation_panel);
    for (auto& control : g_formation_cell_controls) control = nullptr;
    for (auto& overlay : g_formation_cell_overlays) overlay = nullptr;
    for (auto& button : g_formation_action_buttons) button = nullptr;
}

/* The upper-left character block. While an input mode is up it lists the
   reviewed character's professions and their levels instead of the portrait;
   otherwise it repaints the portrait and vitals, the name/gender/profession
   caption, and the eight-slot party strip. */
// FUNCTION: WIZ8 0x005b4000
void DrawCampHeader(void)
{
    W8CampScreenState* state = g_camp_screen;
    W8Character* character = g_review_character;
    unsigned int row;
    unsigned int slot;
    int profession;
    int name_x;
    int level_x;
    int y;
    int left;
    int top;
    int band_x;
    int band_y;
    int image;
    int sub_image;

    if (state->header_mode != W8_CAMP_HEADER_SUMMARY) {
        if ((state->redraw_flags & 0x7ff) != 0) {
            InvalidateRegion(0, 0, 0x136, 0xa5, 0);
            const SDL_Rect bounds{0, 0, 0x136, 0xa5};
            SDL_FillSurfaceRect(&GetFrameBufferSurface(), &bounds, 0x8000);
            DrawCatalogImage(GetFrameBufferSurface(), g_camp_page_regions->index(4), 0, 0, 0, 0);
            DrawRcsText(g_localized_strings[0x8c2], 0xb, 0xd, 0x11e,
                        g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
            row = 0;
            for (profession = 0; profession < W8_PROFESSION_COUNT; ++profession) {
                if (character->profession_levels[profession] != 0 ||
                    character->iProfession == profession) {
                    if ((row & ~7u) == 0) {
                        name_x = 0xf;
                        level_x = 0x78;
                    } else {
                        name_x = 0xa1;
                        level_x = 0x10a;
                    }
                    y = (row & 7) * 0xe + 0x24;
                    DrawRcsText(g_localized_strings[g_profession_name_message_ids[profession]], name_x, y,
                                0x68, g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
                    state->text_buffer =
                        fmt::format("{:d}", character->profession_levels[profession]);
                    DrawRcsText(state->text_buffer, level_x, y, 0x20,
                                g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
                    ++row;
                }
            }
        }
        return;
    }

    if ((state->redraw_flags & 0x400) != 0) {
        InvalidateRegion(0, 0, 0x136, 0xa5, 0);
        const SDL_Rect bounds{0, 0, 0x136, 0xa5};
        SDL_FillSurfaceRect(&GetFrameBufferSurface(), &bounds, 0x8000);
        DrawCatalogImage(GetFrameBufferSurface(), 0x10f, 0, 0, 0, 0);
    }
    if ((state->redraw_flags & W8_CAMP_REDRAW_PORTRAIT) != 0) {
        if (character->hp_current == 0) {
            DrawCatalogImage(GetFrameBufferSurface(), g_dead_portrait_catalog_ids[character->iRace][1], 0, 0,
                             0xa4, 0xc);
        } else {
            RenderPartyPortrait(character->portrait_index, 0xa4, 0xc, {}, 1, giReviewCharSlot);
        }
        DrawCatalogImage(GetFrameBufferSurface(), 0x10f, 0, 9, 0xa4, 0xc);
        if (state->portrait_hovered[0] != 0) {
            DrawCatalogImage(GetFrameBufferSurface(), g_camp_item_pool_regions->index(6), 0, 0, 0xa4, 0xc);
        }
        if (!gXStatus.fCombatMode) {
            RedrawRcsLevelUpPanel();
            RedrawRcsDismissPanel();
        }
        if (character->highest_condition == W8_CONDITION_NONE) {
            DrawCatalogImage(GetFrameBufferSurface(), 0x2f, 0, 0, 0xa5, 0xe);
        } else {
            DrawCatalogImage(GetFrameBufferSurface(), character->highest_condition + 0xb6, 0, 0, 0xa5, 0xe);
            DrawCatalogImage(GetFrameBufferSurface(), 0x61, 0, 0, 0xa4, 0xd);
        }
        if (character->enchantment_top == W8_ENCHANTMENT_NONE) {
            DrawCatalogImage(GetFrameBufferSurface(), 0x30, 0, 0, 0xee, 0xe);
        } else {
            DrawCatalogImage(GetFrameBufferSurface(), character->enchantment_top + 0xc9, 0, 0, 0xee, 0xe);
            DrawCatalogImage(GetFrameBufferSurface(), 0x61, 0, 0, 0xed, 0xd);
        }
        InvalidateRegion(0xa4, 0xc, 0xfe, 0x54, 1);
        DrawCampVitals();
        DrawCampHands();
    }
    if ((state->redraw_flags & 0x200) != 0) {
        SetFont(g_wiz_text_bold_font);
        g_wiz_text_bold_font.setShade(Font::kNormalShade);
        if (state->hover_region == g_camp_main_regions->index(11)) {
            g_wiz_text_bold_font.setPalette(g_font_state_palettes[W8_FONT_PALETTE_GREEN]);
        }
        state->text_buffer = character->name;
        DrawFontTextDirty((0xba - g_wiz_text_bold_font.textWidth(state->text_buffer)) / 2 +
                              0x74,
                          0x60, state->text_buffer);
        g_wiz_text_bold_font.resetPalette();
        SetFont(g_wiz_text_font_secondary);
        g_wiz_text_font_secondary.setShade(Font::kNormalShade);
        state->text_buffer = g_localized_strings[g_gender_name_message_rows[character->gender][0]];
        state->text_buffer += " ";
        state->text_buffer += g_localized_strings[g_race_name_message_ids[character->iRace]];
        DrawFontTextDirty(
            (0xba - g_wiz_text_font_secondary.textWidth(state->text_buffer)) / 2 + 0x74,
            0x6f, state->text_buffer);
        if (state->hover_region == g_camp_main_regions->index(12)) {
            g_wiz_text_font_secondary.setPalette(g_font_state_palettes[W8_FONT_PALETTE_GREEN]);
        }
        state->text_buffer = g_localized_strings[g_profession_name_message_ids[character->iProfession]];
        DrawFontTextDirty(
            (0xba - g_wiz_text_font_secondary.textWidth(state->text_buffer)) / 2 + 0x74,
            0x7c, state->text_buffer);
        g_wiz_text_font_secondary.resetPalette();
        SetFont(g_wiz_text_font_secondary);
        g_wiz_text_font_secondary.setShade(Font::kNormalShade);
        state->text_buffer = fmt::format(
            "{} {:d} ({})", g_localized_strings[0x91a], character->uiExpLevel,
            g_localized_strings[g_profession_level_name_message_ids[character->iProfession]
                                                                   [character->level_band]]);
        DrawFontTextDirty(
            (0xba - g_wiz_text_font_secondary.textWidth(state->text_buffer)) / 2 + 0x74,
            0x89, state->text_buffer);
    }
    for (slot = 0; slot < 8; ++slot) {
        if ((state->redraw_flags & (1 << (slot & 0x1f))) != 0) {
            band_y = (slot >> 1) * 0x27;
            band_x = (slot & 1) * 0x30;
            left = band_x + 6;
            top = band_y + 5;
            if (giReviewCharSlot == -1 || !g_status.buffers.XChar[slot].fOccupied) {
                DrawCatalogImage(GetFrameBufferSurface(), 0x10f, 0, slot + 1, band_x + 5, band_y + 4);
            } else {
                W8Character* member = &g_status.buffers.Char[slot];
                if (member->hp_current == 0) {
                    image = g_dead_portrait_catalog_ids[member->iRace][0];
                    sub_image = 0;
                } else {
                    sub_image = member->portrait_index;
                    image = 0x13;
                }
                DrawCatalogImage(GetFrameBufferSurface(), image, sub_image, 0, left, top);
                if (!CanSelectRcsPartySlot(slot)) {
                    const SDL_Rect bounds{left, top, band_x + 0x32 - left + 1, band_y + 0x28 - top + 1};
                    ShadowRect(GetFrameBufferSurface(), bounds);
                    ShadowRect(GetFrameBufferSurface(), bounds);
                }
                if (state->hover_region == g_camp_main_regions->index(3 + slot)) {
                    DrawCatalogImage(GetFrameBufferSurface(), g_camp_item_pool_regions->index(7), 0, 0, left, top);
                } else if (static_cast<int>(slot) == giReviewCharSlot) {
                    DrawCatalogImage(GetFrameBufferSurface(), g_camp_item_pool_regions->index(7), 0, 1, left, top);
                }
            }
            InvalidateRegion(left, top, band_x + 0x33, band_y + 0x29, 0);
        }
    }
}

/* The health/stamina/spell bars beside the portrait: the three frame images,
   the shaded depletion on each, the current-profession label and, under the
   numeric-hit-points option, the raw hit-point value. */
// FUNCTION: WIZ8 0x005b4790
void DrawCampVitals(void)
{
    int image;
    int numeric;
    int frame_column;
    int hp_row;
    int stamina_row;
    int spell_column;
    int hp_frame;
    int stamina_frame;
    int spell_frame;
    int bar_top;
    unsigned int hp_fill;
    unsigned int stamina_fill;
    unsigned int spell_fill;
    int spell_left;
    unsigned int gap;
    W8ControlsRect bounds;
    W8TextBuffer* text;
    std::string formatted;

    if (g_settings.numeric_hit_points == 0) {
        image = 1;
    } else {
        image = 3;
    }
    DrawCatalogImage(GetFrameBufferSurface(), 0x80, 0, image, 0x10c, 0xb);
    if (g_review_character->hp_current == 0) {
        return;
    }
    if (g_settings.numeric_hit_points == 0) {
        frame_column = 0x17;
        numeric = 0;
        hp_row = 6;
        stamina_row = 0xa;
        spell_column = 0xe;
        hp_frame = 0x4f;
        stamina_frame = 0x50;
        spell_frame = 0x51;
    } else {
        frame_column = 0x11;
        numeric = 1;
        hp_row = 3;
        stamina_row = 9;
        spell_column = 0xf;
        hp_frame = 0x52;
        stamina_frame = 0x53;
        spell_frame = 0x54;
    }
    if (g_review_character->uiHPMax == 0) {
        srAssertFail("gpReviewPC->uiHPMax",
                     "C:\\Projects\\Wizardry 8\\Local Screens\\RCSCommon.cpp", 0x373, 0);
    }
    hp_fill = g_review_character->hp_current * 0x2d / g_review_character->uiHPMax;
    if (g_review_character->uiStaminaMax == 0) {
        srAssertFail("gpReviewPC->uiStaminaMax",
                     "C:\\Projects\\Wizardry 8\\Local Screens\\RCSCommon.cpp", 0x377, 0);
    }
    stamina_fill = (g_review_character->stamina < 0 ? 0 : g_review_character->stamina) * 0x2d /
                   g_review_character->uiStaminaMax;
    if (SumCharacterSpellPoints(g_review_character) == 0) {
        spell_fill = 0;
    } else {
        spell_left = SumCharacterSpellPointsLeft(g_review_character);
        if (spell_left < 0) {
            spell_left = 0;
        } else {
            spell_left = SumCharacterSpellPointsLeft(g_review_character);
        }
        spell_fill = spell_left * 0x2d / SumCharacterSpellPoints(g_review_character);
    }
    bar_top = frame_column + 0xb;
    DrawCatalogImage(GetFrameBufferSurface(), hp_frame, 0, 0, hp_row + 0x10c, bar_top);
    gap = 0x2d - hp_fill;
    if (gap != 0) {
        ShadeStatusBarGap(gap, hp_row + 0x10c, frame_column - numeric + 0xb);
    }
    DrawCatalogImage(GetFrameBufferSurface(), stamina_frame, 0, 0, stamina_row + 0x10c, bar_top);
    gap = 0x2d - stamina_fill;
    if (gap != 0) {
        ShadeStatusBarGap(gap, stamina_row + 0x10c, frame_column - numeric + 0xb);
    }
    if (SumCharacterSpellPoints(g_review_character) != 0) {
        DrawCatalogImage(GetFrameBufferSurface(), spell_frame, 0, 0, spell_column + 0x10c, bar_top);
        gap = 0x2d - spell_fill;
        if (gap != 0) {
            ShadeStatusBarGap(gap, spell_column + 0x10c, frame_column - numeric + 0xb);
        }
    }
    bounds.left = 0x10e;
    bounds.right = g_camp_page_regions->index(2);
    bounds.top = 0xd;
    bounds.bottom = 0x17;
    text =
        new W8TextBuffer(&bounds, {}, g_smfnt_font, g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter, 4);
    if (!g_status.game_started) {
        text->SetFontStateIndex(8);
    } else {
        text->SetFontStateIndex(g_status.buffers.XChar[giReviewCharSlot].party_order_index);
    }
    text->RenderString(
        g_localized_strings[g_profession_name_message_ids[g_review_character->iProfession + 0x10]],
        g_smfnt_font, false, GetFrameBufferSurface());
    if (g_settings.numeric_hit_points != 0) {
        bounds.left = 0x10e;
        bounds.right = g_camp_page_regions->index(1);
        bounds.top = 0x49;
        bounds.bottom = 0x51;
        text->SetLayoutBounds(&bounds, true, false);
        text->SetFontStateIndex(-1);
        formatted = fmt::format("{:d}", g_review_character->hp_current);
        text->RenderString(formatted, g_smfnt_font, false, GetFrameBufferSurface());
    }
    InvalidateRegion(0x10c, bar_top, g_camp_item_action_regions->index(0), frame_column + 0x38, 0);
    delete text;
}

/* Each hand uses the same equipment icon and stack-count rendering. The
   caller supplies the two layouts and decides whether the off-hand is visible. */
static void DrawCampHand(W8EquipSlot slot, int hand_index, int top, int count_center)
{
    int item_id = g_review_character->EquippedItem[slot].iItemNo;
    if (item_id == -1) {
        DrawCatalogImage(
            GetFrameBufferSurface(),
            g_empty_hand_catalog_ids[g_review_character->iRace * W8_RACE_DWARF + hand_index], 0, 0,
            0x85, top);
    } else {
        DrawSprite(GetFrameBufferSurface(), g_item_video_objects.Sprites(item_id), 2, 0x84, top);
        unsigned char count = g_review_character->EquippedItem[slot].stack_count;
        if (count != 0) {
            std::string text;
            text = fmt::format("{:d}", count);
            SetFont(g_smfnt_font);
            g_smfnt_font.resetPalette();
            DrawFontText(count_center - (g_smfnt_font.textWidth(text) + 1) / 2, top + 0xf,
                         text);
        }
    }
}

/* The two hands under the vitals: each draws its equipment icon (or the
   race's empty-hand image), the stack count when the item has one, and the
   armor-class line under the left hand in the load-category palette. The
   off-hand is skipped while a two-handed item fills the primary slot. */
// FUNCTION: WIZ8 0x005b4bd0
void DrawCampHands(void)
{
    bool two_handed;
    int item_id;
    int hand_image;
    std::string text;
    Palette16 palette;

    item_id = g_review_character->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo;
    if (item_id == -1 || (g_item_records[item_id].flags & W8_ITEM_FLAG_TWO_HANDED) == 0) {
        two_handed = false;
        hand_image = 0;
    } else {
        two_handed = true;
        hand_image = 2;
    }
    DrawCatalogImage(GetFrameBufferSurface(), 0x80, 0, hand_image, 0x81, 0xb);
    DrawCampHand(W8_EQUIP_SLOT_PRIMARY_WEAPON, 0, 0x22, 0x89);
    if (!two_handed) {
        DrawCampHand(W8_EQUIP_SLOT_SECONDARY_WEAPON, 1, 0x3a, 0x8c);
    }
    SetFont(g_smfnt_font);
    text = fmt::format("{:d}", g_review_character->armor_class_average);
    palette = g_smfnt_font.glyphs->colors;
    if (g_review_character->load_category != 0) {
        palette =
            g_font_state_palettes[g_load_category_palettes[g_review_character->load_category]];
    }
    g_smfnt_font.setPalette(palette);
    DrawFontText((0xd - g_smfnt_font.textWidth(text)) / 2 + 0x84, 0xe, text);
    InvalidateRegion(0x81, 0xb, 0x95, 0x53, 0);
}

/* The shared bottom panel the five page buttons and the eight item-action
   controls live on; it is created with the camp screen and torn down by
   DestroyCampButtonPanel. Returns 0 when any allocation fails. */
// FUNCTION: WIZ8 0x005b4eb0
int CreateCampButtonPanel(void)
{
    int index;

    g_item_actions_panel = 0;
    for (index = 0; index < 5; ++index) {
        if (g_camp_page_buttons[index] != 0) {
            g_camp_page_buttons[index] = 0;
        }
    }
    for (index = 0; index < 8; ++index) {
        if (g_item_action_controls[index] != 0) {
            g_item_action_controls[index] = 0;
        }
    }
    g_camp_page_regions.Enable();
    g_camp_item_action_regions.Enable();
    g_item_actions_panel = new Controls(0, 0x1c2, 0x280, 0x1e0, g_camp_item_pool_regions->index(0), 0, 0);
    if (g_item_actions_panel != 0) {
        g_camp_page_buttons[0] = new W8TextControl(g_item_actions_panel, g_camp_page_regions->index(0), 0x1a4, 0, 0x1d0,
                                                   0x1e, g_camp_item_pool_regions->index(1), 0, 0, 4, 1, 2, 3);
        g_camp_page_buttons[1] = new W8TextControl(g_item_actions_panel, g_camp_page_regions->index(1), 0x1d0, 0, 0x1fc,
                                                   0x1e, g_camp_item_pool_regions->index(1), 0, 10, 0xe, 0xb, 0xc, 0xd);
        g_camp_page_buttons[2] = new W8TextControl(g_item_actions_panel, g_camp_page_regions->index(2), 0x1fc, 0, 0x228,
                                                   0x1e, g_camp_item_pool_regions->index(1), 0, 0xf, 0x13, 0x10, 0x11, 0x12);
        g_camp_page_buttons[3] = new W8TextControl(g_item_actions_panel, g_camp_page_regions->index(3), 0x228, 0, 0x254,
                                                   0x1e, g_camp_item_pool_regions->index(1), 0, 5, 9, 6, 7, 8);
        g_camp_page_buttons[4] = new W8TextControl(g_item_actions_panel, g_camp_page_regions->index(4), 0x254, 0, 0x280,
                                                   0x1e, 0x106, 0, 0x10, -1, 0x11, 0x12, 0x13);
        for (index = 0; index < 5; ++index) {
            if (g_camp_page_buttons[index] == 0) {
                return 0;
            }
        }
        g_camp_page_buttons[0]->AddLayoutFlags(g_W8TextControlLayoutToggle);
        g_camp_page_buttons[1]->AddLayoutFlags(g_W8TextControlLayoutToggle);
        g_camp_page_buttons[2]->AddLayoutFlags(g_W8TextControlLayoutToggle);
        g_camp_page_buttons[3]->AddLayoutFlags(g_W8TextControlLayoutToggle);
        g_camp_page_buttons[0]->m_primaryActivationCallback = OnCampPageButton0;
        g_camp_page_buttons[1]->m_primaryActivationCallback = OnCampPageButton1;
        g_camp_page_buttons[2]->m_primaryActivationCallback = OnCampPageButton2;
        g_camp_page_buttons[3]->m_primaryActivationCallback = OnCampPageButton3;
        g_camp_page_buttons[4]->m_primaryActivationCallback = CampPageDismissAction;
        g_item_action_controls[W8_CAMP_ACTION_BUTTON_IDENTIFY] = new W8TextControl(
            g_item_actions_panel, g_camp_item_action_regions->index(0), 0, 0, 0x2c, 0x1e, g_camp_item_pool_regions->index(2), 0, 0, 2, 1, 4, 3);
        g_item_action_controls[W8_CAMP_ACTION_BUTTON_MOVE] = new W8TextControl(
            g_item_actions_panel, g_camp_item_action_regions->index(1), 0x2c, 0, 0x58, 0x1e, g_camp_item_pool_regions->index(2), 0, 10, 0xc, 0xb, 0xe, 0xd);
        g_item_action_controls[W8_CAMP_ACTION_BUTTON_SPLIT_STACK] = new W8TextControl(
            g_item_actions_panel, g_camp_item_action_regions->index(2), 0x58, 0, 0x84, 0x1e, g_camp_item_pool_regions->index(2), 0, 5, 7, 6, 9, 8);
        g_item_action_controls[W8_CAMP_ACTION_BUTTON_USE] =
            new W8TextControl(g_item_actions_panel, g_camp_item_action_regions->index(3), 0x84, 0, 0xb0, 0x1e, g_camp_item_pool_regions->index(2), 0, 0xf, 0x11,
                              0x10, 0x13, 0x12);
        g_item_action_controls[W8_CAMP_ACTION_BUTTON_DROP] =
            new W8TextControl(g_item_actions_panel, g_camp_item_action_regions->index(4), 0xb0, 0, 0xdc, 0x1e, g_camp_item_pool_regions->index(2), 0, 0x14,
                              0x16, 0x15, 0x18, 0x17);
        g_item_action_controls[W8_CAMP_ACTION_BUTTON_CAST_SPELL] =
            new W8TextControl(g_item_actions_panel, g_camp_item_action_regions->index(5), 0x134, 0, 0x160, 0x1e, g_camp_item_pool_regions->index(2), 0, 0x19,
                              0x1b, 0x1a, 0x1d, 0x1c);
        g_item_action_controls[W8_CAMP_ACTION_BUTTON_USE_ON_ITEM] =
            new W8TextControl(g_item_actions_panel, g_camp_item_action_regions->index(6), 0xdc, 0, 0x108, 0x1e, g_camp_item_pool_regions->index(2), 0, 0x1e,
                              0x20, 0x1f, 0x22, 0x21);
        g_item_action_controls[W8_CAMP_ACTION_BUTTON_USE_ON_CHARACTER] =
            new W8TextControl(g_item_actions_panel, g_camp_item_action_regions->index(7), 0x108, 0, 0x134, 0x1e, g_camp_item_pool_regions->index(2), 0, 0x23,
                              0x25, 0x24, 0x27, 0x26);
        index = 0;
        while (g_item_action_controls[index] != 0) {
            ++index;
            if (index > 7) {
                g_item_action_controls[W8_CAMP_ACTION_BUTTON_IDENTIFY]->AddLayoutFlags(
                    g_W8TextControlLayoutToggle);
                g_item_action_controls[W8_CAMP_ACTION_BUTTON_MOVE]->AddLayoutFlags(
                    g_W8TextControlLayoutToggle);
                g_item_action_controls[W8_CAMP_ACTION_BUTTON_SPLIT_STACK]->AddLayoutFlags(
                    g_W8TextControlLayoutToggle);
                g_item_action_controls[W8_CAMP_ACTION_BUTTON_USE]->AddLayoutFlags(
                    g_W8TextControlLayoutToggle);
                g_item_action_controls[W8_CAMP_ACTION_BUTTON_DROP]->AddLayoutFlags(
                    g_W8TextControlLayoutToggle);
                g_item_action_controls[W8_CAMP_ACTION_BUTTON_CAST_SPELL]->AddLayoutFlags(
                    g_W8TextControlLayoutToggle);
                g_item_action_controls[W8_CAMP_ACTION_BUTTON_USE_ON_ITEM]->AddLayoutFlags(
                    g_W8TextControlLayoutToggle);
                g_item_action_controls[W8_CAMP_ACTION_BUTTON_USE_ON_CHARACTER]->AddLayoutFlags(
                    g_W8TextControlLayoutToggle);
                g_item_action_controls[W8_CAMP_ACTION_BUTTON_IDENTIFY]
                    ->m_primaryActivationCallback = OnCampIdentifyItem;
                g_item_action_controls[W8_CAMP_ACTION_BUTTON_MOVE]->m_primaryActivationCallback =
                    OnCampMoveItem;
                g_item_action_controls[W8_CAMP_ACTION_BUTTON_SPLIT_STACK]
                    ->m_primaryActivationCallback = OnCampSplitStack;
                g_item_action_controls[W8_CAMP_ACTION_BUTTON_USE]->m_primaryActivationCallback =
                    OnCampUseItem;
                g_item_action_controls[W8_CAMP_ACTION_BUTTON_DROP]->m_primaryActivationCallback =
                    OnCampDropItem;
                g_item_action_controls[W8_CAMP_ACTION_BUTTON_CAST_SPELL]
                    ->m_primaryActivationCallback = OnCampCastSpell;
                g_item_action_controls[W8_CAMP_ACTION_BUTTON_USE_ON_ITEM]
                    ->m_primaryActivationCallback = OnCampUseItemOnItem;
                g_item_action_controls[W8_CAMP_ACTION_BUTTON_USE_ON_CHARACTER]
                    ->m_primaryActivationCallback = OnCampUseItemOnCharacter;
                g_item_actions_panel->SetEnabled(true);
                g_camp_page_buttons[0]->EnableSecondaryState(false);
                return 1;
            }
        }
    }
    return 0;
}

/* Tear down the shared button panel: delete the Controls parent and each page
   and item-action control, then disable both region sets. */
// FUNCTION: WIZ8 0x005b55f0
void DestroyCampButtonPanel(void)
{
    DestroyControlPanel(g_item_actions_panel);
    for (auto& button : g_camp_page_buttons) button = nullptr;
    for (auto& button : g_item_action_controls) button = nullptr;
    g_camp_item_action_regions.Disable();
    g_camp_page_regions.Disable();
}

/* Refresh the enabled state of the eight item-action controls. Every button
   is enabled only while the items page is up and the party exists; the
   per-button cases then gate on the held item, combat, the reviewed slot's
   eligibility and the relevant item/spell tests. When invalidate is set the
   whole panel is additionally marked dirty before it is redrawn. */
// FUNCTION: WIZ8 0x005b5670
void RefreshCampItemActions(bool invalidate)
{
    int index;
    W8SpellRuntimeRecord* spell;
    W8ItemInstance* held = &g_status.item_in_hand;

    for (index = 0; index < W8_CAMP_ACTION_BUTTON_COUNT; ++index) {
        W8TextControl* control = g_item_action_controls[index];
        if (g_camp_screen->page == W8_CAMP_PAGE_ITEMS && g_status.game_started) {
            switch (index) {
            case W8_CAMP_ACTION_BUTTON_IDENTIFY:
                control->SetEnabled(IsPartySlotEligible(giReviewCharSlot));
                continue;
            case W8_CAMP_ACTION_BUTTON_MOVE:
                control->SetEnabled(!gXStatus.fCombatMode && IsPartySlotEligible(giReviewCharSlot));
                continue;
            case W8_CAMP_ACTION_BUTTON_SPLIT_STACK:
                if (!g_status.item_in_cursor) {
                    control->SetEnabled(true);
                    continue;
                }
                if (!CanSplitItemStack(held)) {
                    control->SetEnabled(false);
                    continue;
                }
                if (!gXStatus.fCombatMode ||
                    (g_combat_state->equip_phase != 0 &&
                     g_status.buffers.XChar[giReviewCharSlot].pending_action == W8_ACTION_EQUIP)) {
                    control->SetEnabled(true);
                    continue;
                }
                if (IsEquippableItemClass(held) && gXStatus.held_item_source == giReviewCharSlot) {
                    control->SetEnabled(true);
                    continue;
                }
                break;
            case W8_CAMP_ACTION_BUTTON_USE:
                if (gXStatus.fCombatMode) {
                    control->SetEnabled(false);
                    continue;
                }
                if (!IsPartySlotEligible(giReviewCharSlot)) {
                    control->SetEnabled(false);
                    continue;
                }
                if (gXStatus.fCampMode || gXStatus.fLockInteract || gXStatus.fTrapInteract) {
                    control->SetEnabled(false);
                    continue;
                }
                if (!g_status.item_in_cursor) {
                    control->SetEnabled(true);
                    continue;
                }
                control->SetEnabled(CanCharacterUseItemEntry(g_review_character, held) != 0);
                continue;
            case W8_CAMP_ACTION_BUTTON_DROP:
                control->SetEnabled(true);
                continue;
            case W8_CAMP_ACTION_BUTTON_CAST_SPELL:
                control->SetEnabled(
                    IsPartySlotEligible(giReviewCharSlot) &&
                    CharacterHasTrait(g_review_character, W8_TRAIT_REMOVE_CURSED_ITEMS));
                continue;
            case W8_CAMP_ACTION_BUTTON_USE_ON_ITEM:
                if (g_review_character->spell_learned[0x17] != 1) {
                    break;
                }
                if (!IsPartySlotEligible(giReviewCharSlot) || gXStatus.fCombatMode) {
                    break;
                }
                spell = &g_spell_records[W8_SPELL_IDENTIFY_ITEM];
                if (spell->spell_point_cost > g_review_character->iSPLeft[spell->realm]) {
                    break;
                }
                if (!SpellUsableNow(W8_SPELL_IDENTIFY_ITEM, false)) {
                    break;
                }
                if (!g_status.item_in_cursor) {
                    control->SetEnabled(true);
                    continue;
                }
                control->SetEnabled(CanItemLeaveItsSlot(held));
                continue;
            case W8_CAMP_ACTION_BUTTON_USE_ON_CHARACTER:
                if (g_review_character->spell_learned[0x3a] != 1) {
                    break;
                }
                if (!IsPartySlotEligible(giReviewCharSlot) || gXStatus.fCombatMode) {
                    break;
                }
                spell = &g_spell_records[W8_SPELL_REMOVE_CURSE];
                if (spell->spell_point_cost > g_review_character->iSPLeft[spell->realm]) {
                    break;
                }
                control->SetEnabled(SpellUsableNow(W8_SPELL_REMOVE_CURSE, false));
                continue;
            }
            control->SetEnabled(false);
        } else {
            control->SetEnabled(false);
        }
    }
    if (invalidate) {
        g_item_actions_panel->Invalidate(0);
    }
    g_item_actions_panel->Redraw();
}

/* Switch the items-page action mode: every action button's secondary state is
   cleared, the mode maps onto the targeting mode and the button it highlights,
   and mode zero also drops a held item cursor back into the pool. */
// FUNCTION: WIZ8 0x005b59b0
void SetCampItemActionMode(char mode)
{
    int index;
    short selected = W8_CAMP_ACTION_BUTTON_NONE;
    W8TargetNeed targeting;
    W8TextControl** control;

    for (control = g_item_action_controls, index = W8_CAMP_ACTION_BUTTON_COUNT; index != 0;
         ++control, --index) {
        if (static_cast<unsigned char>((*control)->m_stateFlags & g_W8TextControlStateSecondary) !=
            0) {
            (*control)->DisableSecondaryState(false);
        }
    }
    g_camp_screen->item_action = mode;
    switch (mode) {
    case W8_CAMP_ITEM_ACTION_IDENTIFY_SPELL:
    case W8_CAMP_ITEM_ACTION_USE_ON_ITEM:
        targeting = W8_TARGET_NEED_ITEM;
        selected = W8_CAMP_ACTION_BUTTON_USE_ON_ITEM;
        break;
    case W8_CAMP_ITEM_ACTION_IDENTIFY:
        targeting = W8_TARGET_NEED_ITEM;
        selected = W8_CAMP_ACTION_BUTTON_IDENTIFY;
        break;
    case W8_CAMP_ITEM_ACTION_MOVE:
        targeting = W8_TARGET_NEED_ITEM;
        selected = W8_CAMP_ACTION_BUTTON_MOVE;
        break;
    case W8_CAMP_ITEM_ACTION_SPLIT_STACK:
        targeting = W8_TARGET_NEED_ITEM;
        selected = W8_CAMP_ACTION_BUTTON_SPLIT_STACK;
        break;
    case W8_CAMP_ITEM_ACTION_USE:
        targeting = W8_TARGET_NEED_ITEM;
        selected = W8_CAMP_ACTION_BUTTON_USE;
        break;
    case W8_CAMP_ITEM_ACTION_DROP:
        targeting = W8_TARGET_NEED_ITEM;
        selected = W8_CAMP_ACTION_BUTTON_DROP;
        break;
    case W8_CAMP_ITEM_ACTION_CAST_SPELL:
        targeting = W8_TARGET_NEED_ALLY;
        selected = W8_CAMP_ACTION_BUTTON_CAST_SPELL;
        break;
    case W8_CAMP_ITEM_ACTION_USE_ON_CHARACTER:
        targeting = W8_TARGET_NEED_ALLY;
        selected = W8_CAMP_ACTION_BUTTON_USE_ON_CHARACTER;
        break;
    default:
        targeting = W8_TARGET_NEED_NONE;
        break;
    }
    SetTargetingMode(targeting);
    if (mode == W8_CAMP_ITEM_ACTION_NONE && g_status.item_in_cursor) {
        SetItemCursor(0);
    }
    if (selected != W8_CAMP_ACTION_BUTTON_NONE &&
        static_cast<unsigned char>(g_item_action_controls[selected]->m_stateFlags &
                                   g_W8TextControlStateSecondary) == 0) {
        g_item_action_controls[selected]->EnableSecondaryState(false);
    }
    g_camp_screen->redraw_flags |= W8_CAMP_REDRAW_ITEM_ACTIONS;
}

/* Descriptive name for the page-button operation expanded in the callbacks. */
static void SelectCampPageButton(int button, W8CampPage page)
{
    if (static_cast<unsigned char>(g_camp_page_buttons[button]->m_stateFlags &
                                   g_W8TextControlStateSecondary) != 0) {
        for (int index = 0; index < 5; ++index) {
            if (static_cast<unsigned char>(g_camp_page_buttons[index]->m_stateFlags &
                                           g_W8TextControlStateSecondary) != 0) {
                g_camp_page_buttons[index]->DisableSecondaryState(false);
            }
        }
        SwitchCampPage(page);
    }
    g_camp_page_buttons[button]->EnableSecondaryState(false);
}

/* The five page-button callbacks share one shape: while this button's
   secondary state is up, clear it on every page button and switch to the
   button's page; then raise the pressed button's secondary state. */
// FUNCTION: WIZ8 0x005b5ae0
static void OnCampPageButton0(void)
{
    SelectCampPageButton(0, W8_CAMP_PAGE_ITEMS);
}

// FUNCTION: WIZ8 0x005b5b30
static void OnCampPageButton1(void)
{
    SelectCampPageButton(1, W8_CAMP_PAGE_SPELLS);
}

// FUNCTION: WIZ8 0x005b5b80
static void OnCampPageButton2(void)
{
    SelectCampPageButton(2, W8_CAMP_PAGE_SKILLS);
}

// FUNCTION: WIZ8 0x005b5bd0
static void OnCampPageButton3(void)
{
    SelectCampPageButton(3, W8_CAMP_PAGE_STATS);
}

// FUNCTION: WIZ8 0x005b5c20
static void CampPageDismissAction(void)
{
    DismissSelectedPartyCharacter();
}

/* Item-action button callbacks: while the button's secondary state is up the
   click performs the held-item action or arms the button's targeting mode;
   while it is down the click falls back to clearing the action mode. */
// FUNCTION: WIZ8 0x005b5c30
static void OnCampIdentifyItem(void)
{
    if (static_cast<unsigned char>(
            g_item_action_controls[W8_CAMP_ACTION_BUTTON_IDENTIFY]->m_stateFlags &
            g_W8TextControlStateSecondary) != 0) {
        g_camp_identifying_character = g_review_character;
        if (g_status.item_in_cursor) {
            IdentifyAndOpenItemInfo(&g_status.item_in_hand);
            return;
        }
        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_IDENTIFY);
        return;
    }
    SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
}

// FUNCTION: WIZ8 0x005b5c80
static void OnCampMoveItem(void)
{
    if (static_cast<unsigned char>(
            g_item_action_controls[W8_CAMP_ACTION_BUTTON_MOVE]->m_stateFlags &
            g_W8TextControlStateSecondary) != 0) {
        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_MOVE);
        if (g_status.item_in_cursor) {
            SetHandCursors(0);
            g_camp_screen->item_redraw_flags |= W8_CAMP_ITEM_REDRAW_EQUIPMENT;
        }
        return;
    }
    SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
}

// FUNCTION: WIZ8 0x005b5cd0
static void OnCampSplitStack(void)
{
    if (static_cast<unsigned char>(
            g_item_action_controls[W8_CAMP_ACTION_BUTTON_SPLIT_STACK]->m_stateFlags &
            g_W8TextControlStateSecondary) != 0) {
        if (g_status.item_in_cursor) {
            OpenSplitStackDialog(&g_status.item_in_hand);
            return;
        }
        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_SPLIT_STACK);
        return;
    }
    SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
}

// FUNCTION: WIZ8 0x005b5d10
static void OnCampUseItem(void)
{
    if (static_cast<unsigned char>(g_item_action_controls[W8_CAMP_ACTION_BUTTON_USE]->m_stateFlags &
                                   g_W8TextControlStateSecondary) != 0) {
        if (g_status.item_in_cursor) {
            UseCampItem(&g_status.item_in_hand);
            return;
        }
        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_USE);
        return;
    }
    SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
}

// FUNCTION: WIZ8 0x005b5d50
static void OnCampDropItem(void)
{
    if (static_cast<unsigned char>(
            g_item_action_controls[W8_CAMP_ACTION_BUTTON_DROP]->m_stateFlags &
            g_W8TextControlStateSecondary) != 0) {
        if (g_status.item_in_cursor) {
            if (ResolvePendingCampCharacter(true)) {
                DropHeldCampItem();
            }
            return;
        }
        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_DROP);
        return;
    }
    SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
}

// FUNCTION: WIZ8 0x005b5d90
static void OnCampCastSpell(void)
{
    bool eligible;
    unsigned char active;

    eligible = IsCampActionAllowed(giReviewCharSlot);
    active = static_cast<unsigned char>(
        g_item_action_controls[W8_CAMP_ACTION_BUTTON_CAST_SPELL]->m_stateFlags &
        g_W8TextControlStateSecondary);
    if (!eligible) {
        if (active != 0) {
            g_item_action_controls[W8_CAMP_ACTION_BUTTON_CAST_SPELL]->DisableSecondaryState(false);
            g_item_action_controls[W8_CAMP_ACTION_BUTTON_CAST_SPELL]->Invalidate(false);
        }
        return;
    }
    if (active != 0) {
        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_CAST_SPELL);
        return;
    }
    SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
}

// FUNCTION: WIZ8 0x005b5df0
static void OnCampUseItemOnItem(void)
{
    if (static_cast<unsigned char>(
            g_item_action_controls[W8_CAMP_ACTION_BUTTON_USE_ON_ITEM]->m_stateFlags &
            g_W8TextControlStateSecondary) != 0) {
        giCasterCharSlot = giReviewCharSlot;
        if (g_status.item_in_cursor) {
            UseHeldItemOnItem(&g_status.item_in_hand);
            g_item_action_controls[W8_CAMP_ACTION_BUTTON_USE_ON_ITEM]->DisableSecondaryState(false);
            g_item_action_controls[W8_CAMP_ACTION_BUTTON_USE_ON_ITEM]->Invalidate(false);
            return;
        }
        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_USE_ON_ITEM);
        return;
    }
    SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
}

// FUNCTION: WIZ8 0x005b5e60
static void OnCampUseItemOnCharacter(void)
{
    if (static_cast<unsigned char>(
            g_item_action_controls[W8_CAMP_ACTION_BUTTON_USE_ON_CHARACTER]->m_stateFlags &
            g_W8TextControlStateSecondary) != 0) {
        giCasterCharSlot = giReviewCharSlot;
        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_USE_ON_CHARACTER);
        return;
    }
    SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
}

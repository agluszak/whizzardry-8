#include "wiz8/local_screens/RCSItemsPage.h"

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>
#include <fmt/printf.h>

#include "Font.h"
#include "conditions.h"
#include "dialog_code/DialogBase.h"
#include "equipment_slots.h"
#include "input.h"
#include "layouts/item_instance.h"
#include "layouts/targeting.h"
#include "local_code/ControlsRect.h"
#include "local_code/RangeControl.h"
#include "local_code/TextBuffer.h"
#include "soundman.h"
#include "ui_input.h"
#include "wiz8/button_interaction.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/character_skills.h"
#include "wiz8/cursor.h"
#include "wiz8/dialog_code/AssayDialog.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/dialog_code/DialogFactoryDialogs.h"
#include "wiz8/dialog_code/StatInfoDialogs.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/fonts.h"
#include "wiz8/integer_constants.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/GameplayMods.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_code/party_encumbrance.h"
#include "wiz8/local_screens/RCSCommon.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/spell_ids.h"
#include "wiz8/sr_api.h"
#include "wiz8/ui_layer.h"
#include "wiz8/xstatus.h"

/* Retail Local Screens\RCSItemsPage.cpp - the items page of the camp
   review screen: page-mode toggling, the item-info/split/stat dialogs,
   use-item and targeting entry points, and the five region callbacks the
   camp region table registers for the backpack, equipment, item-pool,
   realm-tab and panel-tab areas. */

namespace {
struct CampItemControls {
    UiLayer input{UiLayerOrder::CampPool};
    UiLayer secondary;
    std::array<std::unique_ptr<W8ButtonWidget>, 2> actions;
    std::array<std::unique_ptr<W8ButtonWidget>, 7> filters;
    std::array<std::unique_ptr<W8ButtonWidget>, 2> tabs;
    std::array<HitTarget*, 7> stats;
    std::array<HitTarget*, 4> info;
    HitTarget* help;
    std::string help_text;
    bool dirty = true;
};
std::unique_ptr<CampItemControls> item_controls;
}

void SetCampItemHelp(std::string text)
{
    item_controls->help_text = std::move(text);
}

static void DrawCampItemLabel(W8ItemInstance* item, int left, int top, char flag);

// GLOBAL: WIZ8 0x0069C0EC
int giCasterCharSlot;

// GLOBAL: WIZ8 0x0069C424
W8ItemInstance* g_split_item_source;

/* The twelve equip-slot label message ids indexed by region
   region id. */
// GLOBAL: WIZ8 0x0061E7C4
unsigned short g_equip_slot_label_ids[12] = {
    0x433, 0x434, 0x435, 0x436, 0x437, 0x438, 0x439, 0x43a, 0x43b, 0x43c, 0x43d, 0x43e,
};

/* The two page-tab primary callbacks: the Items and Character info buttons
   created by CreateCampSecondaryPanel. */
// FUNCTION: WIZ8 0x005B9FB0
void ShowCampItemsPage(void)
{
    SetCampInfoPageMode(W8_CAMP_INFO_PAGE_ITEMS);
}

// FUNCTION: WIZ8 0x005B9FC0
void ShowCampCharacterPage(void)
{
    SetCampInfoPageMode(W8_CAMP_INFO_PAGE_CHARACTER);
}

/* The items page swaps two control panels in and out: mode zero shows the
   item page, mode one the character page. */
// FUNCTION: WIZ8 0x005B9FD0
void SetCampInfoPageMode(char mode)
{
    int index;

    switch (mode) {
    default:
        srAssertFail("FALSE", "C:\\Projects\\Wizardry 8\\Local Screens\\RCSItemsPage.cpp", 0x544,
                     "Info toggle error");
        break;
    case W8_CAMP_INFO_PAGE_CHARACTER:
        item_controls->tabs[0]->SetPressed(false);
        item_controls->tabs[1]->SetPressed(true);
        item_controls->help->setEnabled(false);
        g_camp_screen->character_info->SetActive(true);
        for (index = 0; index < 7; ++index) {
            item_controls->stats[index]->setEnabled(false);
        }
        for (index = 0; index < 4; ++index) {
            item_controls->info[index]->setEnabled(false);
        }
        break;
    case W8_CAMP_INFO_PAGE_ITEMS:
        item_controls->tabs[0]->SetPressed(true);
        item_controls->tabs[1]->SetPressed(false);
        item_controls->help->setEnabled(true);
        g_camp_screen->character_info->SetActive(false);
        for (index = 0; index < 7; ++index) {
            item_controls->stats[index]->setEnabled(true);
        }
        for (index = 0; index < 4; ++index) {
            item_controls->info[index]->setEnabled(true);
        }
        break;
    }
    g_camp_screen->info_page = mode;
    g_camp_screen->redraw_flags |= W8_CAMP_REDRAW_CHARACTER_INFO;
}

// FUNCTION: WIZ8 0x005BA110
void OpenItemInfoDialog(W8ItemInstance* item)
{
    W8DialogBase* dialog;

    if (!IsItemWornByCharacter(g_review_character, item) &&
        !IsItemCarriedByCharacter(g_review_character, item)) {
        dialog = new W8AssayDialog(item, 0, {g_info_dialog_x, g_info_dialog_y});
    } else {
        dialog = new W8AssayDialog(item, g_review_character, {g_info_dialog_x, g_info_dialog_y});
    }
    dialog->SetText("");

    DisplayCampDialog(std::unique_ptr<W8DialogBase>(dialog));
}

/* The secondary-activation callbacks the seven attribute labels and four
   secondary labels on the camp secondary panel carry: each opens the stat
   info dialog at its own index. Note retail wires both
   item_controls->info[2] and [3] to the index-3 thunk. */
// FUNCTION: WIZ8 0x005BA200
void OpenStrengthInfoDialog(void)
{
    OpenAttributeInfoDialog(W8_ATTRIBUTE_STRENGTH);
}

// FUNCTION: WIZ8 0x005BA210
void OpenIntelligenceInfoDialog(void)
{
    OpenAttributeInfoDialog(W8_ATTRIBUTE_INTELLIGENCE);
}

// FUNCTION: WIZ8 0x005BA220
void OpenPietyInfoDialog(void)
{
    OpenAttributeInfoDialog(W8_ATTRIBUTE_PIETY);
}

// FUNCTION: WIZ8 0x005BA230
void OpenVitalityInfoDialog(void)
{
    OpenAttributeInfoDialog(W8_ATTRIBUTE_VITALITY);
}

// FUNCTION: WIZ8 0x005BA240
void OpenDexterityInfoDialog(void)
{
    OpenAttributeInfoDialog(W8_ATTRIBUTE_DEXTERITY);
}

// FUNCTION: WIZ8 0x005BA250
void OpenSpeedInfoDialog(void)
{
    OpenAttributeInfoDialog(W8_ATTRIBUTE_SPEED);
}

// FUNCTION: WIZ8 0x005BA260
void OpenSensesInfoDialog(void)
{
    OpenAttributeInfoDialog(W8_ATTRIBUTE_SENSES);
}

// FUNCTION: WIZ8 0x005BA270
void OpenSecondaryAttributeInfoDialog0(void)
{
    OpenSecondaryAttributeInfoDialog(0);
}

// FUNCTION: WIZ8 0x005BA280
void OpenSecondaryAttributeInfoDialog1(void)
{
    OpenSecondaryAttributeInfoDialog(1);
}

// FUNCTION: WIZ8 0x005BA290
void OpenSecondaryAttributeInfoDialog3(void)
{
    OpenSecondaryAttributeInfoDialog(3);
}

// FUNCTION: WIZ8 0x005BA2A0
void OpenSecondaryAttributeInfoDialog4(void)
{
    OpenSecondaryAttributeInfoDialog(4);
}

// FUNCTION: WIZ8 0x005BA2B0
void OpenAttributeInfoDialog(W8Attribute attribute)
{
    DisplayCampDialog(std::make_unique<W8AttributeInfoDialog>(attribute));
}

// FUNCTION: WIZ8 0x005BA310
void OpenSecondaryAttributeInfoDialog(unsigned int uiIndex)
{
    DisplayCampDialog(std::make_unique<W8SecondaryAttributeInfoDialog>(uiIndex));
}

// FUNCTION: WIZ8 0x005BA370
void IdentifyAndOpenItemInfo(W8ItemInstance* item)
{
    if (CanItemLeaveItsSlot(item)) {
        if (PartyAttemptsToIdentifyItem(item, 0) &&
            g_camp_screen->item_filters[W8_CAMP_ITEM_FILTER_UNIDENTIFIED] != 0) {
            RebuildCampItemList();
            g_camp_screen->redraw_flags |= W8_CAMP_REDRAW_ALL;
        }
    }
    OpenItemInfoDialog(item);
    SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
}

// FUNCTION: WIZ8 0x005BA3D0
void DropHeldCampItem(void)
{
    if (ResolvePendingCampCharacter(true)) {
        if (DropItemInHand(0)) {
            SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
        }
    }
}

// FUNCTION: WIZ8 0x005BA400
void OpenSplitStackDialog(W8ItemInstance* item)
{
    W8SplitItemDialog* dialog;

    g_split_item_source = 0;
    if (item->iItemNo != -1 && item->stack_count > 1 &&
        (g_item_records[item->iItemNo].flags & W8_ITEM_FLAG_NO_DISCARD) == 0 &&
        g_item_records[item->iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK) {
        g_split_item_source = item;
        dialog = new W8SplitItemDialog(g_item_split_inventory_mode, item, -1,
                                       {g_split_dialog_x, g_split_dialog_y});
        dialog->SetText("");

        dialog->on_complete = SplitStackDialogResult;
        DisplayCampDialog(std::unique_ptr<W8DialogBase>(dialog));
        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
    }
}

// FUNCTION: WIZ8 0x005BA4F0
void UseCampItem(W8ItemInstance* item)
{
    unsigned short slot;

    if (ValidateItemSpellUse(giReviewCharSlot, item, 0)) {
        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
        return;
    }
    if (CanCastFromItem(g_review_character, item)) {
        LearnSpellFromItem(g_review_character, item);
    } else {
        if (!IsUsableItemClass(item) || IsSpecialItemId(item)) {
            gXStatus.item_drag_active = true;
            gXStatus.dragged_item = item;
            gXStatus.dragged_character_slot = static_cast<char>(giReviewCharSlot);
            GetOriginOfCharacterItem(giReviewCharSlot, item, &gXStatus.dragged_item_origin, &slot);
        } else {
            if (!g_status.item_in_cursor) {
                MarkCampCharacterPending(item);
                CopyItemInstance(&g_status.item_in_hand, item, g_review_character, true);
            }
        }
        DismissSelectedPartyCharacter();
    }
    RebuildCampItemList();
    g_camp_screen->redraw_flags |= W8_CAMP_REDRAW_ALL;
    SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
}

// FUNCTION: WIZ8 0x005BA5D0
void MergeItemStacksWithHeld(W8ItemInstance* item)
{
    if (item->iItemNo != -1) {
        if (MergeItems(g_review_character, item)) {
            RebuildCampItemList();
            g_camp_screen->redraw_flags |= W8_CAMP_REDRAW_ALL;
            SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
        }
    }
}

// FUNCTION: WIZ8 0x005BA620
void ReportCastResult(int party_slot)
{
    std::string text;
    W8Character* character;
    int result;

    StartBreathCycle(giReviewCharSlot, false);
    result = RevealCharacterItemBindingsByProfession(giReviewCharSlot, party_slot);
    character = g_status.buffers.Char + party_slot;
    if (result == 0) {
        wiz8::sound::play("Data\\Sound\\Misc\\Spell Fizzle 01.wav");
        text = fmt::sprintf(g_localized_strings[0x1be], character->name, 0, 1, 0);
        ShowCampNoticeLine(text, 0, true, false);
    } else if (result == 1) {
        wiz8::sound::play("Data\\Sound\\Misc\\GeneralMagic.wav");
        text = fmt::sprintf(g_localized_strings[0x1bd], character->name, 0, 1, 0);
        ShowCampNoticeLine(text, 0, true, false);
    } else if (result == 2) {
        wiz8::sound::play("Data\\Sound\\Misc\\GeneralMagic.wav");
        text = fmt::sprintf(g_localized_strings[0x1bc], character->name, 0, 1, 0);
        ShowCampNoticeLine(text, 0, true, false);
    }
    g_camp_screen->redraw_flags |= W8_CAMP_REDRAW_ALL;
    SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
}

// FUNCTION: WIZ8 0x005BA740
void UseHeldItemOnItem(W8ItemInstance* item)
{
    W8CombatSlot target;

    if (giCasterCharSlot < 0) {
        srAssertFail("giCasterCharSlot >= 0",
                     "C:\\Projects\\Wizardry 8\\Local Screens\\RCSItemsPage.cpp", 0x668, 0);
    }
    if (giCasterCharSlot > 7) {
        srAssertFail("giCasterCharSlot < MAX_CHARS",
                     "C:\\Projects\\Wizardry 8\\Local Screens\\RCSItemsPage.cpp", 0x669, 0);
    }
    if (!g_status.buffers.XChar[giCasterCharSlot].fOccupied) {
        srAssertFail("fCHAR_OCCUPIED(giCasterCharSlot)",
                     "C:\\Projects\\Wizardry 8\\Local Screens\\RCSItemsPage.cpp", 0x66a, 0);
    }
    if (item->iItemNo != -1) {
        if (CanItemLeaveItsSlot(item)) {
            ResetCombatSlot(&target);
            target.iType = W8_TARGET_KIND_ITEM;
            if (CommitPartySlotSpell(giCasterCharSlot, W8_SPELL_IDENTIFY_ITEM, 8, &target) == 1) {
                OpenItemInfoDialog(item);
                if (!item->identified) {
                    QueueCharacterEvent(g_status.buffers.Char + giCasterCharSlot,
                                        g_character_event_kind2, 0, g_character_event_no_flags,
                                        g_character_event_full_volume);
                }
            }
            RebuildCampItemList();
            g_camp_screen->redraw_flags |= W8_CAMP_REDRAW_ALL;
            SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
            giCasterCharSlot = -1;
            return;
        }
        QueueCharacterEvent(g_status.buffers.Char + giCasterCharSlot, g_character_event_kind2, 0,
                            g_character_event_no_flags, g_character_event_full_volume);
    }
}

// FUNCTION: WIZ8 0x005BA8E0
void TargetCharacterWithHeldItem(unsigned int uiTargetChar)
{
    W8CombatSlot target;

    if (uiTargetChar > 7) {
        srAssertFail("uiTargetChar < MAX_CHARS",
                     "C:\\Projects\\Wizardry 8\\Local Screens\\RCSItemsPage.cpp", 0x692, 0);
    }
    if (!g_status.buffers.XChar[uiTargetChar].fOccupied) {
        srAssertFail("fCHAR_OCCUPIED(uiTargetChar)",
                     "C:\\Projects\\Wizardry 8\\Local Screens\\RCSItemsPage.cpp", 0x693, 0);
    }
    if (giCasterCharSlot < 0) {
        srAssertFail("giCasterCharSlot >= 0",
                     "C:\\Projects\\Wizardry 8\\Local Screens\\RCSItemsPage.cpp", 0x694, 0);
    }
    if (giCasterCharSlot > 7) {
        srAssertFail("giCasterCharSlot < MAX_CHARS",
                     "C:\\Projects\\Wizardry 8\\Local Screens\\RCSItemsPage.cpp", 0x695, 0);
    }
    if (!g_status.buffers.XChar[giCasterCharSlot].fOccupied) {
        srAssertFail("fCHAR_OCCUPIED(giCasterCharSlot)",
                     "C:\\Projects\\Wizardry 8\\Local Screens\\RCSItemsPage.cpp", 0x696, 0);
    }
    ResetCombatSlot(&target);
    target.iType = W8_TARGET_KIND_CHARACTER;
    target.iChar = uiTargetChar;
    CommitPartySlotSpell(giCasterCharSlot, W8_SPELL_REMOVE_CURSE, 8, &target);
    RebuildCampItemList();
    g_camp_screen->redraw_flags |= W8_CAMP_REDRAW_ALL;
    SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
    giCasterCharSlot = -1;
}

// FUNCTION: WIZ8 0x005BAA10
int CanCharacterUseItemEntry(W8Character* character, W8ItemInstance* item)
{
    if (CanCharacterActivateItem(character, item) || CanCastFromItem(character, item) ||
        IsUsableItemClass(item)) {
        return 1;
    }
    return 0;
}

// FUNCTION: WIZ8 0x005BAA50
bool CanSplitItemStack(const W8ItemInstance* item)
{
    const W8ItemDatabaseRecord* record = g_item_records + item->iItemNo;
    if ((record->flags & W8_ITEM_FLAG_NO_DISCARD) != 0) {
        return false;
    }
    return record->quantity_kind == W8_ITEM_QUANTITY_STACK;
}

// FUNCTION: WIZ8 0x005BAA80
void SplitStackDialogResult(int count)
{
    W8ItemInstance split;
    W8ItemInstance* destination;
    W8Character* character;
    unsigned char remaining;
    unsigned char carried;

    remaining = g_split_item_source->stack_count - static_cast<unsigned char>(count);
    carried = static_cast<unsigned char>(count);
    if (count == 0) {
        return;
    }
    if (g_status.item_in_cursor) {
        if (count == g_split_item_source->stack_count) {
            return;
        }
        split = *g_split_item_source;
        split.stack_count = remaining;
        /* The remainder goes back to the slot the held stack came from when that
           slot can take it, otherwise anywhere on that character, then into the
           party pool, and as a last resort stays in hand. */
        destination = 0;
        character = 0;
        if (gXStatus.held_item_source != -1 &&
            (gXStatus.held_item_origin == W8_ITEM_ORIGIN_BACKPACK ||
             gXStatus.held_item_origin == W8_ITEM_ORIGIN_EQUIPPED)) {
            character = g_status.buffers.Char + gXStatus.held_item_source;
            if (gXStatus.held_item_origin == W8_ITEM_ORIGIN_EQUIPPED) {
                if (character->EquippedItem[static_cast<short>(gXStatus.held_item_slot)].iItemNo ==
                        -1 &&
                    CanEquipItemInSlot(character, g_status.item_in_hand.iItemNo,
                                       static_cast<unsigned char>(gXStatus.held_item_slot),
                                       false) &&
                    CanCharacterUseItem(character, g_status.item_in_hand.iItemNo)) {
                    destination =
                        character->EquippedItem + static_cast<short>(gXStatus.held_item_slot);
                }
            } else if (character->backpack[static_cast<short>(gXStatus.held_item_slot)].iItemNo ==
                       -1) {
                destination = character->backpack + static_cast<short>(gXStatus.held_item_slot);
            }
        }
        if (destination == 0 &&
            (character == 0 || !AddItemToCharacter(character, &split, false, false, false)) &&
            !AddItemToParty(&split, false, false)) {
            ShowCampNoticeLine(g_localized_strings[0x915], 0, true, false);
            g_status.item_in_hand.stack_count = remaining;
            if (ResolvePendingCampCharacter(true) && DropItemInHand(0)) {
                SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
            }
            destination = &g_status.item_in_hand;
        }
        if (destination != 0) {
            CopyItemInstance(destination, &split, 0, true);
        }
    } else {
        if (count == g_split_item_source->stack_count) {
            CopyItemInstance(&g_status.item_in_hand, g_split_item_source, g_review_character, true);
            carried = g_status.item_in_hand.stack_count;
        } else {
            split = *g_split_item_source;
            split.stack_count = static_cast<unsigned char>(count);
            g_split_item_source->stack_count = remaining;
            CopyItemInstance(&g_status.item_in_hand, &split, 0, true);
            gXStatus.held_item_source = giReviewCharSlot;
            GetOriginOfCharacterItem(giReviewCharSlot, g_split_item_source,
                                     &gXStatus.held_item_origin, &gXStatus.held_item_slot);
            carried = g_status.item_in_hand.stack_count;
        }
    }
    g_status.item_in_hand.stack_count = carried;
    RebuildEquipmentAndDerivedStatsForSlot(giReviewCharSlot);
    RebuildCampItemList();
    RecalculateCharacterDerivedStats(g_status.buffers.Char + giReviewCharSlot);
    RecalculateCarriedWeight(g_review_character);
    RedistributePartyEncumbrance();
    SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
    g_camp_screen->redraw_flags |= W8_CAMP_REDRAW_ALL;
}

/* Mode/tail bits the update pass keys on: the camp item_action byte at +0xd3f
   selects which use paths are live, and iTargetingMode selects what the item
   cursor is allowed to target. */
// FUNCTION: WIZ8 0x005BAD20
void UpdateItemCursorForState(int flag, W8ItemInstance* item, int slot)
{
    if (g_camp_screen->item_action == W8_CAMP_ITEM_ACTION_NONE) {
        return;
    }
    switch (gXStatus.iTargetingMode) {
    case W8_TARGET_NEED_ALLY:
        if ((g_camp_screen->item_action == W8_CAMP_ITEM_ACTION_CAST_SPELL ||
             g_camp_screen->item_action == W8_CAMP_ITEM_ACTION_USE_ON_CHARACTER) &&
            g_status.buffers.XChar[slot].fOccupied &&
            g_status.buffers.Char[slot].uiCondition[W8_CONDITION_MISSING] == 0) {
            SetTargetCursor(GetTargetingCursorForState(flag));
            return;
        }
        if (CanPartySlotParticipate(slot)) {
            SetTargetCursor(GetTargetingCursorForState(flag));
        }
        return;
    case W8_TARGET_NEED_ITEM:
        if (item == 0) {
            srAssertFail("pItem", "C:\\Projects\\Wizardry 8\\Local Screens\\RCSItemsPage.cpp",
                         0x763, 0);
        }
        if (item->iItemNo == -1) {
            return;
        }
        if ((g_camp_screen->item_action == W8_CAMP_ITEM_ACTION_IDENTIFY_SPELL ||
             g_camp_screen->item_action == W8_CAMP_ITEM_ACTION_USE_ON_ITEM) &&
            CanItemLeaveItsSlot(item)) {
            SetTargetCursor(GetTargetingCursorForState(flag));
            return;
        }
        if (g_camp_screen->item_action == W8_CAMP_ITEM_ACTION_IDENTIFY ||
            g_camp_screen->item_action == W8_CAMP_ITEM_ACTION_DROP) {
            SetTargetCursor(GetTargetingCursorForState(flag));
            return;
        }
        if (g_camp_screen->item_action == W8_CAMP_ITEM_ACTION_SPLIT_STACK) {
            const W8ItemDatabaseRecord* record = g_item_records + item->iItemNo;
            if ((record->flags & W8_ITEM_FLAG_NO_DISCARD) != 0 ||
                record->quantity_kind != W8_ITEM_QUANTITY_STACK) {
                return;
            }
            if (gXStatus.fCombatMode && !IsEquippableItemClass(item) &&
                (g_combat_state->equip_phase == 0 ||
                 g_status.buffers.XChar[giReviewCharSlot].pending_action != W8_ACTION_EQUIP)) {
                return;
            }
            SetTargetCursor(GetTargetingCursorForState(flag));
            return;
        }
        if (g_camp_screen->item_action == W8_CAMP_ITEM_ACTION_USE) {
            if (CanCharacterActivateItem(g_review_character, item) ||
                CanCastFromItem(g_review_character, item) || IsUsableItemClass(item)) {
                SetTargetCursor(GetTargetingCursorForState(flag));
            }
            return;
        }
        if (g_camp_screen->item_action != W8_CAMP_ITEM_ACTION_MOVE || item->iItemNo == -1) {
            return;
        }
        if (static_cast<char>(flag) == 0) {
            if (!g_status.item_in_cursor) {
                SetTargetCursor(3);
                return;
            }
            SetTargetCursor(0xf);
            SetItemCursor(0xe);
            return;
        }
        if (!g_status.item_in_cursor) {
            SetTargetCursor(4);
            return;
        }
        SetTargetCursor(0xe);
        SetItemCursor(0xf);
        return;
    case W8_TARGET_NEED_CHARACTER_INDIRECT:
        if (IsDeadCharacterTargetable(slot)) {
            SetTargetCursor(GetTargetingCursorForState(flag));
        }
        return;
    default:
        return;
    }
}

// FUNCTION: WIZ8 0x005BAFC0
void SetHandCursors(char mode)
{
    if (mode == 0) {
        if (g_status.item_in_cursor) {
            SetTargetCursor(0xf);
            SetItemCursor(0xe);
            return;
        }
        SetTargetCursor(3);
        return;
    }
    if (g_status.item_in_cursor) {
        SetTargetCursor(0xe);
        SetItemCursor(0xf);
        return;
    }
    SetTargetCursor(4);
}

/* Unequips both weapon slots of the selected character; refused outright in
   combat and while the party is moving, and per-slot when a bound item will
   not come off. */
// FUNCTION: WIZ8 0x005BB010
void UnequipBothHands(void)
{
    W8Character* character;

    if (gXStatus.fCombatMode && !g_combat_state->round_active && !gXStatus.fPartyMovementMode &&
        g_combat_state->equip_phase == 0) {
        ShowCampNoticeLine(g_localized_strings[0x903], 0, true, false);
        return;
    }
    if (!IsPartySlotEligible(giReviewCharSlot)) {
        ShowCampNoticeLine(g_localized_strings[0x917], 0, true, false);
        return;
    }
    character = g_status.buffers.Char + giReviewCharSlot;
    BindEquippedItem(character, W8_EQUIP_SLOT_PRIMARY_WEAPON);
    BindEquippedItem(character, W8_EQUIP_SLOT_SECONDARY_WEAPON);
    if (CanUnequipSlotItem(character, W8_EQUIP_SLOT_PRIMARY_WEAPON) &&
        CanUnequipSlotItem(character, W8_EQUIP_SLOT_SECONDARY_WEAPON)) {
        SwapWeaponSetSlots(giReviewCharSlot, false, true);
        g_camp_screen->item_redraw_flags |= W8_CAMP_ITEM_REDRAW_EQUIPMENT;
        g_camp_screen->redraw_flags |= W8_CAMP_REDRAW_PORTRAIT;
        g_camp_screen->redraw_flags |= W8_CAMP_REDRAW_CHARACTER_INFO;
        return;
    }
    ShowCampNoticeLine(g_localized_strings[0x916], 0, true, false);
}

// FUNCTION: WIZ8 0x005BB140
void TogglePartyRowFlag(void)
{
    if (item_controls->actions[1]->IsPressed()) {
        g_status.buffers.XChar[giReviewCharSlot].item_action_pending = true;
        item_controls->actions[0]->SetEnabled(false);
        item_controls->actions[0]->m_dirty = true;
        return;
    }
    g_status.buffers.XChar[giReviewCharSlot].item_action_pending = false;
    item_controls->actions[0]->SetEnabled(true);
    item_controls->actions[0]->m_dirty = true;
}

// FUNCTION: WIZ8 0x005BB1C0
void ToggleCampHandItemFilter(void)
{
    ToggleCampItemFilter(W8_CAMP_ITEM_FILTER_BUTTON_HAND);
}

// FUNCTION: WIZ8 0x005BB1D0
void ToggleCampBodyItemFilter(void)
{
    ToggleCampItemFilter(W8_CAMP_ITEM_FILTER_BUTTON_BODY);
}

// FUNCTION: WIZ8 0x005BB1E0
void ToggleCampOtherItemFilter(void)
{
    ToggleCampItemFilter(W8_CAMP_ITEM_FILTER_BUTTON_OTHER);
}

// FUNCTION: WIZ8 0x005BB1F0
void ToggleCampAccessoryItemFilter(void)
{
    ToggleCampItemFilter(W8_CAMP_ITEM_FILTER_BUTTON_ACCESSORY);
}

// FUNCTION: WIZ8 0x005BB200
void ToggleCampUsabilityFilter(void)
{
    ToggleCampItemFilter(W8_CAMP_ITEM_FILTER_BUTTON_USABLE);
}

// FUNCTION: WIZ8 0x005BB210
void ToggleCampUnidentifiedItemFilter(void)
{
    ToggleCampItemFilter(W8_CAMP_ITEM_FILTER_BUTTON_UNIDENTIFIED);
}

// FUNCTION: WIZ8 0x005BB220
void SortCampItemPool(void)
{
    SortPartyItemPool();
    g_camp_screen->item_scroll = 0;
    RebuildCampItemList();
    g_camp_screen->item_redraw_flags |= W8_CAMP_ITEM_REDRAW_POOL;
}

/* UI button order differs from the item-filter order. */
// FUNCTION: WIZ8 0x005BB250
void ToggleCampItemFilter(W8CampItemFilterButton tab)
{
    W8CampItemFilter filter;
    int index;

    switch (tab) {
    case W8_CAMP_ITEM_FILTER_BUTTON_HAND:
        filter = W8_CAMP_ITEM_FILTER_HAND;
        break;
    case W8_CAMP_ITEM_FILTER_BUTTON_BODY:
        filter = W8_CAMP_ITEM_FILTER_BODY;
        break;
    case W8_CAMP_ITEM_FILTER_BUTTON_OTHER:
        filter = W8_CAMP_ITEM_FILTER_OTHER;
        break;
    case W8_CAMP_ITEM_FILTER_BUTTON_ACCESSORY:
        filter = W8_CAMP_ITEM_FILTER_ACCESSORY;
        break;
    case W8_CAMP_ITEM_FILTER_BUTTON_USABLE:
        filter = W8_CAMP_ITEM_FILTER_USABLE;
        break;
    case W8_CAMP_ITEM_FILTER_BUTTON_UNIDENTIFIED:
        filter = W8_CAMP_ITEM_FILTER_UNIDENTIFIED;
        break;
    default:
        filter = static_cast<W8CampItemFilter>(tab);
    }
    if (!item_controls->filters[tab]->IsPressed()) {
        g_camp_screen->item_filters[filter] = 0;
    } else {
        g_camp_screen->item_filters[filter] = 1;
        if (filter != W8_CAMP_ITEM_FILTER_USABLE && filter != W8_CAMP_ITEM_FILTER_UNIDENTIFIED) {
            ClearOtherCampItemGroupFilters(filter);
            for (index = 0; index < 4; ++index) {
                if (index != tab && item_controls->filters[index]->IsPressed()) {
                    item_controls->filters[index]->SetPressed(false);
                }
            }
        }
    }
    g_camp_screen->item_scroll = 0;
    RebuildCampItemList();
    if (((GetInputState().modifiers & CTRL_DOWN) == 0)) {
        g_camp_screen->item_redraw_flags |= W8_CAMP_ITEM_REDRAW_POOL;
        return;
    }
    g_camp_screen->redraw_flags |= W8_CAMP_REDRAW_ALL;
}

/* Region reason values are bit codes set by the region manager: 0x8 left
   release, 0x10 left down, 0x40 right press, 0x80 right release, 0x100
   double-click/activate, 0x400 enter/leave transition, 0x800 mouse wheel. */
/* Descriptive name for the right-click identify/show operation shared by
   backpack, equipment and party-pool slots. */
static void ShowCampItemInfo(W8ItemInstance* item)
{
    g_camp_identifying_character = g_review_character;
    if (CanItemLeaveItsSlot(item) && PartyAttemptsToIdentifyItem(item, 0) &&
        g_camp_screen->item_filters[W8_CAMP_ITEM_FILTER_UNIDENTIFIED] != 0) {
        RebuildCampItemList();
        g_camp_screen->redraw_flags |= W8_CAMP_REDRAW_ALL;
    }
    OpenItemInfoDialog(item);
    SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
}

static std::pair<W8ItemInstance*, int> CampItemAt(int slot, int origin)
{
    if (origin == W8_ITEM_ORIGIN_BACKPACK) return {g_review_character->backpack + slot, slot};
    if (origin == W8_ITEM_ORIGIN_EQUIPPED) return {g_review_character->EquippedItem + slot, slot};
    const auto visible = g_camp_screen->item_scroll + slot;
    const int index =
        visible < g_camp_screen->item_list_count &&
                g_status.party_item_pool[g_camp_screen->item_list[visible]].iItemNo != -1
            ? g_camp_screen->item_list[visible]
            : g_status.party_item_count;
    return {g_status.party_item_pool + index, index};
}

static std::string CampItemTooltip(int slot, int origin)
{
    const auto [item, index] = CampItemAt(slot, origin);
    std::string text = item->iItemNo == -1 ? std::string{} : FormatItemDisplayName(item, false);
    if (origin == W8_ITEM_ORIGIN_EQUIPPED) {
        const auto& label = g_localized_strings[g_equip_slot_label_ids[slot]];
        return text.empty() ? std::string(label) : fmt::format("{} ({})", text, label);
    }
    if (item->iItemNo != -1) {
        const auto& record = g_item_records[item->iItemNo];
        if (record.category == W8_ITEM_CATEGORY_SPELL_SOURCE && record.spell_id != W8_SPELL_NONE &&
            g_review_character->spell_learned[record.spell_id] &&
            (item->identified || item->spell_hint))
            text += fmt::format(" ({})", g_localized_strings[0x930]);
    }
    return text;
}

UiResult HandleCampItem(const UiPointerEvent& event, int slot, int origin)
{
    const auto [item, index] = CampItemAt(slot, origin);
    const bool equipped = origin == W8_ITEM_ORIGIN_EQUIPPED;
    const bool pool = origin == W8_ITEM_ORIGIN_PARTY_POOL;
    auto& target = *(pool ? g_camp_screen->pool_targets[slot]
                          : g_camp_screen->inventory_targets[slot + (equipped ? 8 : 0)]);
    if (item->iItemNo == -1 &&
        (!g_status.item_in_cursor ||
         (equipped && (g_camp_screen->item_action == W8_CAMP_ITEM_ACTION_MOVE ||
                       !CanEquipItemInSlot(g_review_character, g_status.item_in_hand.iItemNo,
                                           static_cast<unsigned char>(slot), true) ||
                       !CanCharacterUseItem(g_review_character, g_status.item_in_hand.iItemNo)))))
        PushButtonSoundScheme(0, true);
    if (event.action == UiPointerAction::Enter || event.action == UiPointerAction::Leave) {
        if (item->iItemNo != -1 || g_status.item_in_cursor)
            g_camp_screen->item_redraw_flags |=
                (pool       ? W8_CAMP_ITEM_REDRAW_POOL_CELL_FIRST
                 : equipped ? W8_CAMP_ITEM_REDRAW_EQUIPMENT_CELL_FIRST
                            : W8_CAMP_ITEM_REDRAW_BACKPACK_CELL_FIRST)
                << slot;
        UpdateItemCursorForState(event.action == UiPointerAction::Enter, item, 0);
    }
    if (event.action == UiPointerAction::Leave ||
        (event.action == UiPointerAction::Down && event.button == UiPointerButton::Left)) {
        target.setTooltip({});
        return UiResult::Handled;
    }
    if (event.action == UiPointerAction::Wheel && pool)
        g_camp_screen->item_range->AdjustValue(event.wheel);
    if (event.action == UiPointerAction::Up && event.inside) {
        if (event.button == UiPointerButton::Left && (event.captured_buttons & SDL_BUTTON_LMASK))
            AfterUiDispatch([item, index, origin] {
                HandleCampItemClick(item, index, static_cast<W8ItemOrigin>(origin));
            });
        if (event.button == UiPointerButton::Right && (event.captured_buttons & SDL_BUTTON_RMASK) &&
            item->iItemNo != -1 && g_camp_screen->item_action != W8_CAMP_ITEM_ACTION_IDENTIFY)
            AfterUiDispatch([item] { ShowCampItemInfo(item); });
    }
    if (event.action == UiPointerAction::Enter || event.action == UiPointerAction::Up ||
        event.action == UiPointerAction::Wheel) {
        SetUiHelpDelay(std::min(g_settings.tooltip_delay_ms, 300));
        SetUiHelpForced(true);
        target.setTooltip([slot, origin] { return CampItemTooltip(slot, origin); });
    }
    return UiResult::Handled;
}

/* The items page's per-row label boxes: the backpack grid (two columns of
   four at 0x0d/0xc2), the visible pool rows (0x22c/0xc2), and the twelve
   equipment icons laid out by g_camp_screen_regions. Each occupied
   slot gets a hover label drawn by DrawCampItemLabel. */
// FUNCTION: WIZ8 0x005bbe30
void DrawCampItemIcons(void)
{
    unsigned int index;
    unsigned int count;
    int y;
    const W8CampScreenRegion* region;
    W8ItemInstance* item;
    W8Character* character = g_review_character;
    W8CampScreenState* state = g_camp_screen.get();

    for (index = 0; index < 8; ++index) {
        item = &character->backpack[index];
        if (item->iItemNo != -1) {
            DrawCampItemLabel(item, (index & 1) * 0x31 + 0xd,
                              (index >> 1) * 0x39 + 0xc2 + (index & 1) * 0x10, 0);
        }
    }
    index = 0;
    while (true) {
        count = state->item_list_count - state->item_scroll;
        if (count > 8) {
            count = 8;
        }
        if (count <= index) {
            break;
        }
        y = (index >> 1) * 0x39 + 0xc2;
        if ((index & 1) == 0) {
            y = (index >> 1) * 0x39 + 0xd2;
        }
        DrawCampItemLabel(&g_status.party_item_pool[state->item_list[state->item_scroll + index]],
                          (index & 1) * 0x31 + 0x22c, y, 1);
        ++index;
    }
    region = g_camp_screen_regions;
    for (index = 0; index < 12; ++index) {
        item = &character->EquippedItem[index];
        if (item->iItemNo != -1) {
            DrawCampItemLabel(item, region->label_x, region->label_y,
                              static_cast<char>(region->label_flag));
        }
        ++region;
    }
    ResetTransientRenderScenes();
}

/* Draws one boxed item name: the buffer measures the formatted display name,
   the box is left-shifted by its width for the pool column, then the filled
   background, the text and a one-pixel outline are painted straight into the
   locked primary surface. */
// FUNCTION: WIZ8 0x005bbf40
static void DrawCampItemLabel(W8ItemInstance* item, int left, int top, char flag)
{
    std::string name;
    W8ControlsRect bounds;
    W8TextBuffer* text;
    int width;
    int height;

    name = FormatItemDisplayName(item, false);
    g_camp_screen->text_buffer = name;
    bounds.left = left;
    bounds.top = top;
    bounds.right = left + 0xfa;
    bounds.bottom = top + 0xfa;
    text = new W8TextBuffer(&bounds, g_camp_screen->text_buffer, g_font10arial,
                            g_W8TextBufferAlignTop | g_W8TextBufferAlignLeft, 4);
    if (text != 0) {
        height = text->m_lineCount;
        width = text->m_maxLineWidth + 4;
        height = g_font10arial.height() * height + 2;
        if (flag != 0) {
            bounds.right -= width;
            bounds.left -= width;
            text->SetClipBounds(bounds);
            text->SetLayoutBounds(&bounds, true);
        }
        SDL_Surface& framebuffer = GetFrameBufferSurface();
        const SDL_Rect fill{bounds.left, bounds.top, width, height};
        SDL_FillSurfaceRect(&framebuffer, &fill, 0x8000);
        if (framebuffer.pixels != nullptr) {
            // reinterpret-ok: SGP frame-buffer bytes to W8TextBuffer's byte view
            text->RenderText(framebuffer, 2, 1, true);
            const Uint16 outline = static_cast<Uint16>(-0x6613);
            const SDL_Rect top_edge{bounds.left, bounds.top, width + 1, 1};
            const SDL_Rect right_edge{bounds.left + width, bounds.top, 1, height + 1};
            const SDL_Rect bottom_edge{bounds.left, bounds.top + height, width + 1, 1};
            const SDL_Rect left_edge{bounds.left, bounds.top, 1, height + 1};
            SDL_FillSurfaceRect(&framebuffer, &top_edge, outline);
            SDL_FillSurfaceRect(&framebuffer, &right_edge, outline);
            SDL_FillSurfaceRect(&framebuffer, &bottom_edge, outline);
            SDL_FillSurfaceRect(&framebuffer, &left_edge, outline);
            delete text;
        }
    }
}

void CreateCampItemControls()
{
    item_controls = std::make_unique<CampItemControls>();
    const auto button = [](UiLayer& layer, int art, std::array<int, 5> f, SDL_Rect bounds, int help,
                           bool toggle, void (*action)()) {
        auto result = std::make_unique<W8ButtonWidget>(layer);
        result->Configure(
            ButtonGraphics::Load(CatalogArtwork::Load(art, 0).spriteSheet(), f[0], f[1], f[2], f[3],
                                 f[4]),
            {.on_click = [action](auto*) { AfterUiDispatch(action); },
             .policy = {.primary_toggle = toggle ? ButtonToggle::Release : ButtonToggle::None,
                        .input_rule = ButtonInputRule::PrimaryPressedState},
             .tooltip = help,
             .priority = UI_PRIORITY_LOWEST,
             .menu_artwork = true});

        result->SetPosition(bounds.x, bounds.y);
        result->Resize(bounds.w, bounds.h);
        return result;
    };
    item_controls->actions[0] = button(item_controls->input, 0x11e, {3, 0, 1, 2, 2},
                                       {0x7d, 0x19c, 28, 24}, 2385, false, UnequipBothHands);
    item_controls->actions[1] = button(item_controls->input, 0x11e, {11, 8, 9, 4, 5},
                                       {0x9e, 0x19c, 28, 24}, 2386, true, TogglePartyRowFlag);
    constexpr int frames[] = {0, 15, 20, 5, 10, 40, 30};
    constexpr int y[] = {0, 25, 75, 50, 108, 142, 176};
    const std::array actions{ToggleCampHandItemFilter,  ToggleCampBodyItemFilter,
                             ToggleCampOtherItemFilter, ToggleCampAccessoryItemFilter,
                             ToggleCampUsabilityFilter, ToggleCampUnidentifiedItemFilter,
                             SortCampItemPool};
    for (int index = 0; index < 7; ++index) {
        const int f = frames[index];
        item_controls->filters[index] =
            button(item_controls->input, 0x11f, {f + 3, f, f + 1, f + 2, f + 4},
                   {0x1e5, 0xc1 + y[index], 24, 24}, 2378 + index, index < 6, actions[index]);
    }
    for (int index = 0; index < 2; ++index) {
        const int f = index * 5;
        item_controls->tabs[index] =
            button(item_controls->secondary, 0x121, {f + 3, f, f + 1, f + 2, f + 4},
                   {0x13e, 9 + index * 21, 20, 20}, 0x95d + index, true,
                   index ? ShowCampCharacterPage : ShowCampItemsPage);
    }
    const auto label = [](SDL_Rect bounds, std::function<void()> action, int help) {
        auto& target = item_controls->secondary.add(bounds, [action](const UiPointerEvent& event) {
            if (event.button != UiPointerButton::Right) PushButtonSoundScheme(0, true);
            if (event.action == UiPointerAction::Up && event.inside &&
                event.button == UiPointerButton::Right && event.captured_buttons)
                AfterUiDispatch(action);
            return UiResult::Handled;
        });
        if (help >= 0) target.setTooltip([help] { return std::string(g_localized_strings[help]); });
        return &target;
    };
    for (int index = 0; index < 7; ++index)
        item_controls->stats[index] = label(
            {0x1c2, 0x3a + index * 14, 77, 13},
            [index] { OpenAttributeInfoDialog(static_cast<W8Attribute>(index)); }, 0x958);
    constexpr int info_y[] = {0x3a, 0x48, 0x80, 0x8e};
    for (int index = 0; index < 4; ++index)
        item_controls->info[index] = label(
            {0x144, info_y[index], 118, 13},
            [index] { OpenSecondaryAttributeInfoDialog(index < 2 ? index : 3); }, 0x958);
    item_controls->help = label({0x141, 0x54, 120, 13}, OpenSecondaryAttributeInfoDialog4, -1);
    item_controls->help->setTooltip([] { return item_controls->help_text; });
}

void DestroyCampItemControls()
{
    item_controls.reset();
}

// FUNCTION: WIZ8 0x005b9270
void EnableCampActionButtons(void)
{
    item_controls->actions[0]->SetVisible(true);
    item_controls->actions[1]->SetVisible(true);
    if (g_status.game_started) {
        item_controls->actions[1]->SetEnabled(true);
        if (g_status.buffers.XChar[giReviewCharSlot].item_action_pending) {
            item_controls->actions[1]->SetPressed(true);
            item_controls->actions[0]->SetEnabled(false);
        } else {
            item_controls->actions[1]->SetPressed(false);
            item_controls->actions[0]->SetEnabled(true);
        }
    } else {
        item_controls->actions[0]->SetEnabled(false);
        item_controls->actions[1]->SetEnabled(false);
    }
}

// FUNCTION: WIZ8 0x005b9310
void DisableCampActionButtons(void)
{
    item_controls->actions[0]->SetVisible(false);
    item_controls->actions[1]->SetVisible(false);
}

// FUNCTION: WIZ8 0x005b9330
void RefreshCampActionPanel(bool invalidate)
{
    for (const auto& button : item_controls->actions) {
        button->m_dirty |= invalidate;
        button->Refresh();
    }
}

/* The right-hand panel of seven tabs: the six realm filters and the pool sort
   button. */
// FUNCTION: WIZ8 0x005b9350
/* Item filter buttons keep their secondary state when the corresponding
   filter is set. The sort button has no item-filter flag. */
// FUNCTION: WIZ8 0x005b97b0
void UpdateCampItemFilters(void)
{
    int index;
    unsigned char flag;

    for (index = 0; index < 7; ++index) {
        item_controls->filters[index]->SetVisible(true);
    }
    if (!g_status.game_started) {
        for (index = 0; index < 7; ++index) {
            item_controls->filters[index]->SetEnabled(false);
        }
        return;
    }
    for (index = 0; index < 7; ++index) {
        item_controls->filters[index]->SetEnabled(true);
        switch (index) {
        case 0:
            flag = g_camp_screen->item_filters[W8_CAMP_ITEM_FILTER_HAND];
            break;
        case 1:
            flag = g_camp_screen->item_filters[W8_CAMP_ITEM_FILTER_BODY];
            break;
        case 2:
            flag = g_camp_screen->item_filters[W8_CAMP_ITEM_FILTER_OTHER];
            break;
        case 3:
            flag = g_camp_screen->item_filters[W8_CAMP_ITEM_FILTER_ACCESSORY];
            break;
        case 4:
            flag = g_camp_screen->item_filters[W8_CAMP_ITEM_FILTER_USABLE];
            break;
        case 5:
            flag = g_camp_screen->item_filters[W8_CAMP_ITEM_FILTER_UNIDENTIFIED];
            break;
        default:
            continue;
        }
        if (flag != 0) {
            item_controls->filters[index]->SetPressed(true);
        }
    }
}

// FUNCTION: WIZ8 0x005b98c0
void DisableItemsRealmTabs(void)
{
    int index;

    for (index = 0; index < 7; ++index) {
        item_controls->filters[index]->SetVisible(false);
    }
}

// FUNCTION: WIZ8 0x005b98e0
void RefreshItemsTabPanel(bool invalidate)
{
    for (const auto& button : item_controls->filters) {
        button->m_dirty |= invalidate;
        button->Refresh();
    }
}

// FUNCTION: WIZ8 0x005b9ef0
void InvalidateCampPanel(void)
{
    item_controls->dirty = true;
}

// FUNCTION: WIZ8 0x005b9f00
void EnableCampSecondaryPanel(void)
{
    int index;

    item_controls->secondary.setEnabled(true);
    for (index = 0; index < 2; ++index) {
        item_controls->tabs[index]->SetVisible(true);
        item_controls->tabs[index]->SetEnabled(true);
    }
    if (g_camp_screen->info_page == W8_CAMP_INFO_PAGE_ITEMS) {
        SetCampInfoPageMode(W8_CAMP_INFO_PAGE_ITEMS);
        return;
    }
    SetCampInfoPageMode(W8_CAMP_INFO_PAGE_CHARACTER);
}

// FUNCTION: WIZ8 0x005b9f60
void DisableCampSecondaryPanel(void)
{
    int index;

    item_controls->secondary.setEnabled(false);
    for (index = 0; index < 2; ++index) {
        item_controls->tabs[index]->SetVisible(false);
    }
}

// FUNCTION: WIZ8 0x005b9f90
void RefreshCampSecondaryPanel(bool invalidate)
{
    if (invalidate) {
        InvalidateCampPanel();
    }
    for (const auto& tab : item_controls->tabs) item_controls->dirty |= tab->m_dirty;
    if (item_controls->dirty) {
        CatalogArtwork::Load(0x120, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0x13c, 7);
        for (const auto& tab : item_controls->tabs) {
            tab->m_dirty = true;
            tab->Refresh();
        }
        item_controls->dirty = false;
    }
}

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

#include "conditions.h"
#include "dialog_code/DialogBase.h"
#include "equipment_slots.h"
#include "layouts/item_instance.h"
#include "layouts/targeting.h"
#include "local_code/ControlsRect.h"
#include "local_code/RangeControl.h"
#include "local_code/TextBuffer.h"
#include "ui_input.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/button_interaction.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/character_skills.h"
#include "wiz8/cursor.h"
#include "wiz8/dialog_code/AssayDialog.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/dialog_code/DialogFactoryDialogs.h"
#include "wiz8/dialog_code/StatInfoDialogs.h"
#include "wiz8/fonts.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/live_party.h"
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
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/platform/input.h"
#include "wiz8/render/frame.h"
#include "wiz8/spell_ids.h"
#include "wiz8/ui_layer.h"
#include "wiz8/xstatus.h"

constexpr int kInfoDialogY = 0x61;

/* Retail Local Screens\RCSItemsPage.cpp - the items page of the camp
   review screen: page-mode toggling, the item-info/split/stat dialogs,
   use-item and targeting entry points, and the five region callbacks the
   camp region table registers for the backpack, equipment, item-pool,
   realm-tab and panel-tab areas. */

/* The twelve equip-slot label message ids indexed by region
   region id. */
unsigned short g_equip_slot_label_ids[12] = {
    0x433, 0x434, 0x435, 0x436, 0x437, 0x438, 0x439, 0x43a, 0x43b, 0x43c, 0x43d, 0x43e,
};

/* The two page-tab primary callbacks: the Items and Character info buttons
   created by CreateCampSecondaryPanel. */

/* The items page swaps two control panels in and out: mode zero shows the
   item page, mode one the character page. */
// FUNCTION: WIZ8 0x005B9FD0
void W8CampScreenState::SetCampInfoPageMode(W8CampInfoPage mode)
{
    int index;

    switch (mode) {
    default:
        Fail("FALSE", "Info toggle error");
        break;
    case W8_CAMP_INFO_PAGE_CHARACTER:
        item_controls.tabs[0]->SetPressed(false);
        item_controls.tabs[1]->SetPressed(true);
        item_controls.help->setEnabled(false);
        character_info->SetActive(true);
        for (index = 0; index < 7; ++index) {
            item_controls.stats[index]->setEnabled(false);
        }
        for (index = 0; index < 4; ++index) {
            item_controls.info[index]->setEnabled(false);
        }
        break;
    case W8_CAMP_INFO_PAGE_ITEMS:
        item_controls.tabs[0]->SetPressed(true);
        item_controls.tabs[1]->SetPressed(false);
        item_controls.help->setEnabled(true);
        character_info->SetActive(false);
        for (index = 0; index < 7; ++index) {
            item_controls.stats[index]->setEnabled(true);
        }
        for (index = 0; index < 4; ++index) {
            item_controls.info[index]->setEnabled(true);
        }
        break;
    }
    info_page = mode;
    redraw_flags |= W8_CAMP_REDRAW_CHARACTER_INFO;
}

// FUNCTION: WIZ8 0x005BA110
void W8CampScreenState::OpenItemInfoDialog(W8ItemInstance* item)
{
    W8DialogBase* dialog;

    if (!IsItemWornByCharacter(reviewed, item) && !IsItemCarriedByCharacter(reviewed, item)) {
        dialog = new W8AssayDialog(*item, 0, {kInfoDialogX, kInfoDialogY});
    } else {
        dialog = new W8AssayDialog(*item, reviewed, {kInfoDialogX, kInfoDialogY});
    }
    dialog->SetText("");

    DisplayCampDialog(std::unique_ptr<W8DialogBase>(dialog));
}

// FUNCTION: WIZ8 0x005BA2B0
void W8CampScreenState::OpenAttributeInfoDialog(W8Attribute attribute)
{
    DisplayCampDialog(std::make_unique<W8AttributeInfoDialog>(attribute));
}

// FUNCTION: WIZ8 0x005BA310
void W8CampScreenState::OpenSecondaryAttributeInfoDialog(unsigned int uiIndex)
{
    DisplayCampDialog(std::make_unique<W8SecondaryAttributeInfoDialog>(uiIndex));
}

// FUNCTION: WIZ8 0x005BA370
void W8CampScreenState::IdentifyAndOpenItemInfo(W8ItemInstance* item)
{
    if (CanItemLeaveItsSlot(item)) {
        if (PartyAttemptsToIdentifyItem(item, 0) &&
            item_filters[W8_CAMP_ITEM_FILTER_UNIDENTIFIED] != 0) {
            RebuildCampItemList();
            redraw_flags |= W8_CAMP_REDRAW_ALL;
        }
    }
    OpenItemInfoDialog(item);
    SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
}

// FUNCTION: WIZ8 0x005BA3D0
void W8CampScreenState::DropHeldCampItem(void)
{
    if (ResolvePendingCampCharacter(true)) {
        if (DropItemInHand(0)) {
            SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
        }
    }
}

// FUNCTION: WIZ8 0x005BA400
void W8CampScreenState::OpenSplitStackDialog(W8ItemLocation location)
{
    auto* item = g_status.party.itemAt(location);
    if (item->iItemNo != -1 && item->stack_count > 1 &&
        (g_item_records[item->iItemNo].flags & W8_ITEM_FLAG_NO_DISCARD) == 0 &&
        g_item_records[item->iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK) {
        auto dialog = std::make_unique<W8SplitItemDialog>(W8_ITEM_SPLIT_INVENTORY, item, -1,
                                                          SDL_Point{kSplitDialogX, kSplitDialogY});
        dialog->SetText("");

        dialog->on_complete = [screen = this, source = location,
                               actor = ReviewedPartySlot()](int count) {
            if (count <= 0) return;
            if (!SplitItemStack(source, static_cast<unsigned>(count), actor)) {
                screen->ShowNotice(g_localized_strings[0x915], 0, true, false);
                return;
            }
            SetItemCursor(0);
            screen->RebuildCampItemList();
            screen->SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
            screen->redraw_flags |= W8_CAMP_REDRAW_ALL;
        };
        DisplayCampDialog(std::move(dialog));
        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
    }
}

// FUNCTION: WIZ8 0x005BA4F0
void W8CampScreenState::UseCampItem(W8ItemLocation location)
{
    auto* item = g_status.party.itemAt(location);
    if (ValidateItemSpellUse(ReviewedPartySlot(), item, 0)) {
        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
        return;
    }
    if (CanCastFromItem(reviewed, item)) {
        LearnSpellFromItem(reviewed, item);
    } else {
        if (!IsUsableItemClass(item) || IsSpecialItemId(item)) {
            gXStatus.item_drag = W8ItemDrag{location, ReviewedPartySlot()};
        } else {
            if (!(g_status.party.held.item.iItemNo != -1)) {
                MarkCampCharacterPending(item);
                ExchangeInventoryItems(location, {W8ItemStorage::Hand},
                                       static_cast<unsigned>(g_status.selected_character));
            }
        }
        DismissSelectedPartyCharacter();
    }
    RebuildCampItemList();
    redraw_flags |= W8_CAMP_REDRAW_ALL;
    SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
}

// FUNCTION: WIZ8 0x005BA5D0
void W8CampScreenState::MergeItemStacksWithHeld(W8ItemLocation location)
{
    if (g_status.party.itemAt(location)->iItemNo != -1) {
        if (MergeItems(ReviewedPartySlot(), location)) {
            RebuildCampItemList();
            redraw_flags |= W8_CAMP_REDRAW_ALL;
            SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
        }
    }
}

// FUNCTION: WIZ8 0x005BA620
void W8CampScreenState::ReportCastResult(int party_slot)
{
    std::string text;
    W8Character* character;
    int result;

    StartBreathCycle(ReviewedPartySlot(), false);
    result = RevealCharacterItemBindingsByProfession(ReviewedPartySlot(), party_slot);
    character = g_status.party.characters + party_slot;
    if (result == 0) {
        wiz8::sound::play("Data\\Sound\\Misc\\Spell Fizzle 01.wav");
        text = fmt::sprintf(g_localized_strings[0x1be], character->name, 0, 1, 0);
        ShowNotice(text, 0, true, false);
    } else if (result == 1) {
        wiz8::sound::play("Data\\Sound\\Misc\\GeneralMagic.wav");
        text = fmt::sprintf(g_localized_strings[0x1bd], character->name, 0, 1, 0);
        ShowNotice(text, 0, true, false);
    } else if (result == 2) {
        wiz8::sound::play("Data\\Sound\\Misc\\GeneralMagic.wav");
        text = fmt::sprintf(g_localized_strings[0x1bc], character->name, 0, 1, 0);
        ShowNotice(text, 0, true, false);
    }
    redraw_flags |= W8_CAMP_REDRAW_ALL;
    SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
}

// FUNCTION: WIZ8 0x005BA740
void W8CampScreenState::UseHeldItemOnItem(W8ItemInstance* item)
{
    W8CombatSlot target;

    if (pending_item_action.caster_slot < 0) {
        Fail("pending_item_action.caster_slot >= 0");
    }
    if (pending_item_action.caster_slot > 7) {
        Fail("pending_item_action.caster_slot < MAX_CHARS");
    }
    if (!g_status.party.rows[pending_item_action.caster_slot].fOccupied) {
        Fail("fCHAR_OCCUPIED(pending_item_action.caster_slot)");
    }
    if (item->iItemNo != -1) {
        if (CanItemLeaveItsSlot(item)) {
            ResetCombatSlot(&target);
            target.iType = W8_TARGET_KIND_ITEM;
            target.item = g_status.party.locateItem(item);
            if (CommitPartySlotSpell(pending_item_action.caster_slot, W8_SPELL_IDENTIFY_ITEM, 8,
                                     &target) == 1) {
                OpenItemInfoDialog(item);
                if (!item->identified) {
                    QueueCharacterEvent(g_status.party.characters + pending_item_action.caster_slot,
                                        0x35, 0, kCharacterEventFullVolume);
                }
            }
            RebuildCampItemList();
            redraw_flags |= W8_CAMP_REDRAW_ALL;
            SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
            pending_item_action.caster_slot = -1;
            return;
        }
        QueueCharacterEvent(g_status.party.characters + pending_item_action.caster_slot, 0x35, 0,
                            kCharacterEventFullVolume);
    }
}

// FUNCTION: WIZ8 0x005BA8E0
void W8CampScreenState::TargetCharacterWithHeldItem(unsigned int uiTargetChar)
{
    W8CombatSlot target;

    if (uiTargetChar > 7) {
        Fail("uiTargetChar < MAX_CHARS");
    }
    if (!g_status.party.rows[uiTargetChar].fOccupied) {
        Fail("fCHAR_OCCUPIED(uiTargetChar)");
    }
    if (pending_item_action.caster_slot < 0) {
        Fail("pending_item_action.caster_slot >= 0");
    }
    if (pending_item_action.caster_slot > 7) {
        Fail("pending_item_action.caster_slot < MAX_CHARS");
    }
    if (!g_status.party.rows[pending_item_action.caster_slot].fOccupied) {
        Fail("fCHAR_OCCUPIED(pending_item_action.caster_slot)");
    }
    ResetCombatSlot(&target);
    target.iType = W8_TARGET_KIND_CHARACTER;
    target.iChar = uiTargetChar;
    CommitPartySlotSpell(pending_item_action.caster_slot, W8_SPELL_REMOVE_CURSE, 8, &target);
    RebuildCampItemList();
    redraw_flags |= W8_CAMP_REDRAW_ALL;
    SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
    pending_item_action.caster_slot = -1;
}

int CanCharacterUseItemEntry(W8Character* character, W8ItemInstance* item)
{
    if (CanCharacterActivateItem(character, item) || CanCastFromItem(character, item) ||
        IsUsableItemClass(item)) {
        return 1;
    }
    return 0;
}

bool CanSplitItemStack(const W8ItemInstance* item)
{
    const W8ItemDatabaseRecord* record = &g_item_records[item->iItemNo];
    if ((record->flags & W8_ITEM_FLAG_NO_DISCARD) != 0) {
        return false;
    }
    return record->quantity_kind == W8_ITEM_QUANTITY_STACK;
}

// FUNCTION: WIZ8 0x005BAA80

/* Mode/tail bits the update pass keys on: the camp item_action byte at +0xd3f
   selects which use paths are live, and iTargetingMode selects what the item
   cursor is allowed to target. */
// FUNCTION: WIZ8 0x005BAD20
void W8CampScreenState::UpdateItemCursorForState(bool alternate, W8ItemInstance* item, int slot)
{
    if (item_action == W8_CAMP_ITEM_ACTION_NONE) {
        return;
    }
    switch (gXStatus.iTargetingMode) {
    case W8_TARGET_NEED_ALLY:
        if ((item_action == W8_CAMP_ITEM_ACTION_CAST_SPELL ||
             item_action == W8_CAMP_ITEM_ACTION_USE_ON_CHARACTER) &&
            g_status.party.rows[slot].fOccupied &&
            g_status.party.characters[slot].uiCondition[W8_CONDITION_MISSING] == 0) {
            SetTargetCursor(GetTargetingCursorForState(alternate));
            return;
        }
        if (CanPartySlotParticipate(slot)) {
            SetTargetCursor(GetTargetingCursorForState(alternate));
        }
        return;
    case W8_TARGET_NEED_ITEM:
        if (item == 0) {
            Fail("pItem");
        }
        if (item->iItemNo == -1) {
            return;
        }
        if ((item_action == W8_CAMP_ITEM_ACTION_IDENTIFY_SPELL ||
             item_action == W8_CAMP_ITEM_ACTION_USE_ON_ITEM) &&
            CanItemLeaveItsSlot(item)) {
            SetTargetCursor(GetTargetingCursorForState(alternate));
            return;
        }
        if (item_action == W8_CAMP_ITEM_ACTION_IDENTIFY ||
            item_action == W8_CAMP_ITEM_ACTION_DROP) {
            SetTargetCursor(GetTargetingCursorForState(alternate));
            return;
        }
        if (item_action == W8_CAMP_ITEM_ACTION_SPLIT_STACK) {
            const W8ItemDatabaseRecord* record = &g_item_records[item->iItemNo];
            if ((record->flags & W8_ITEM_FLAG_NO_DISCARD) != 0 ||
                record->quantity_kind != W8_ITEM_QUANTITY_STACK) {
                return;
            }
            if (g_combat.active() && !IsEquippableItemClass(item) &&
                (g_combat.state()->equip_phase == W8CombatEquipPhase::action_selection ||
                 g_status.party.rows[ReviewedPartySlot()].pending_action != W8_ACTION_EQUIP)) {
                return;
            }
            SetTargetCursor(GetTargetingCursorForState(alternate));
            return;
        }
        if (item_action == W8_CAMP_ITEM_ACTION_USE) {
            if (CanCharacterActivateItem(reviewed, item) || CanCastFromItem(reviewed, item) ||
                IsUsableItemClass(item)) {
                SetTargetCursor(GetTargetingCursorForState(alternate));
            }
            return;
        }
        if (item_action != W8_CAMP_ITEM_ACTION_MOVE || item->iItemNo == -1) {
            return;
        }
        if (!alternate) {
            if (!(g_status.party.held.item.iItemNo != -1)) {
                SetTargetCursor(3);
                return;
            }
            SetTargetCursor(0xf);
            SetItemCursor(0xe);
            return;
        }
        if (!(g_status.party.held.item.iItemNo != -1)) {
            SetTargetCursor(4);
            return;
        }
        SetTargetCursor(0xe);
        SetItemCursor(0xf);
        return;
    case W8_TARGET_NEED_CHARACTER_INDIRECT:
        if (IsDeadCharacterTargetable(slot)) {
            SetTargetCursor(GetTargetingCursorForState(alternate));
        }
        return;
    default:
        return;
    }
}

// FUNCTION: WIZ8 0x005BAFC0
void SetHandCursors()
{
    if ((g_status.party.held.item.iItemNo != -1)) {
        SetTargetCursor(0xf);
        SetItemCursor(0xe);
        return;
    }
    SetTargetCursor(3);
}

/* Unequips both weapon slots of the selected character; refused outright in
   combat and while the party is moving, and per-slot when a bound item will
   not come off. */
// FUNCTION: WIZ8 0x005BB010
void W8CampScreenState::UnequipBothHands(void)
{
    W8Character* character;

    if (g_combat.active() && !g_combat.state()->round_active && !gXStatus.fPartyMovementMode &&
        g_combat.state()->equip_phase == W8CombatEquipPhase::action_selection) {
        ShowNotice(g_localized_strings[0x903], 0, true, false);
        return;
    }
    if (!IsPartySlotEligible(ReviewedPartySlot())) {
        ShowNotice(g_localized_strings[0x917], 0, true, false);
        return;
    }
    character = g_status.party.characters + ReviewedPartySlot();
    BindEquippedItem(character, W8_EQUIP_SLOT_PRIMARY_WEAPON);
    BindEquippedItem(character, W8_EQUIP_SLOT_SECONDARY_WEAPON);
    if (CanUnequipSlotItem(character, W8_EQUIP_SLOT_PRIMARY_WEAPON) &&
        CanUnequipSlotItem(character, W8_EQUIP_SLOT_SECONDARY_WEAPON)) {
        SwapWeaponSetSlots(ReviewedPartySlot(), false);
        item_redraw_flags |= W8_CAMP_ITEM_REDRAW_EQUIPMENT;
        redraw_flags |= W8_CAMP_REDRAW_PORTRAIT;
        redraw_flags |= W8_CAMP_REDRAW_CHARACTER_INFO;
        return;
    }
    ShowNotice(g_localized_strings[0x916], 0, true, false);
}

// FUNCTION: WIZ8 0x005BB140
void W8CampScreenState::TogglePartyRowFlag(void)
{
    if (item_controls.actions[1]->IsPressed()) {
        g_status.party.rows[ReviewedPartySlot()].item_action_pending = true;
        item_controls.actions[0]->SetEnabled(false);
        item_controls.actions[0]->m_dirty = true;
        return;
    }
    g_status.party.rows[ReviewedPartySlot()].item_action_pending = false;
    item_controls.actions[0]->SetEnabled(true);
    item_controls.actions[0]->m_dirty = true;
}

// FUNCTION: WIZ8 0x005BB220
void W8CampScreenState::SortCampItemPool(void)
{
    SortPartyItemPool();
    item_scroll = 0;
    RebuildCampItemList();
    item_redraw_flags |= W8_CAMP_ITEM_REDRAW_POOL;
}

/* UI button order differs from the item-filter order. */
// FUNCTION: WIZ8 0x005BB250
void W8CampScreenState::ToggleCampItemFilter(W8CampItemFilterButton tab)
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
    if (!item_controls.filters[tab]->IsPressed()) {
        item_filters[filter] = 0;
    } else {
        item_filters[filter] = 1;
        if (filter != W8_CAMP_ITEM_FILTER_USABLE && filter != W8_CAMP_ITEM_FILTER_UNIDENTIFIED) {
            ClearOtherCampItemGroupFilters(filter);
            for (index = 0; index < 4; ++index) {
                if (index != tab && item_controls.filters[index]->IsPressed()) {
                    item_controls.filters[index]->SetPressed(false);
                }
            }
        }
    }
    item_scroll = 0;
    RebuildCampItemList();
    if (((GetInputState().modifiers & CTRL_DOWN) == 0)) {
        item_redraw_flags |= W8_CAMP_ITEM_REDRAW_POOL;
        return;
    }
    redraw_flags |= W8_CAMP_REDRAW_ALL;
}

/* Region reason values are bit codes set by the region manager: 0x8 left
   release, 0x10 left down, 0x40 right press, 0x80 right release, 0x100
   double-click/activate, 0x400 enter/leave transition, 0x800 mouse wheel. */
/* Descriptive name for the right-click identify/show operation shared by
   backpack, equipment and party-pool slots. */
void W8CampScreenState::ShowCampItemInfo(W8ItemInstance* item)
{
    pending_item_action.identifying_character = reviewed;
    if (CanItemLeaveItsSlot(item) && PartyAttemptsToIdentifyItem(item, 0) &&
        item_filters[W8_CAMP_ITEM_FILTER_UNIDENTIFIED] != 0) {
        RebuildCampItemList();
        redraw_flags |= W8_CAMP_REDRAW_ALL;
    }
    OpenItemInfoDialog(item);
    SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
}

/* A visible cell: the reviewed character's own slot, or the pool row the scrolled
   list shows there (the first free pool entry past the list's end). */
W8ItemLocation W8CampScreenState::CampItemAt(int slot, W8ItemStorage storage) const
{
    const auto cell = static_cast<unsigned int>(slot);
    if (storage != W8ItemStorage::Pool)
        return {storage, static_cast<unsigned int>(ReviewedPartySlot()), cell};
    const auto visible = item_scroll + cell;
    const auto index =
        visible < item_list_count && g_status.party.item_pool[item_list[visible]].iItemNo != -1
            ? item_list[visible]
            : g_status.party.item_count;
    return {W8ItemStorage::Pool, 0, static_cast<unsigned int>(index)};
}

std::string W8CampScreenState::CampItemTooltip(int slot, W8ItemStorage storage)
{
    const auto* item = g_status.party.itemAt(CampItemAt(slot, storage));
    if (!item) return {};
    std::string text = item->iItemNo == -1 ? std::string{} : FormatItemDisplayName(item, false);
    if (storage == W8ItemStorage::Equipment) {
        const auto& label = g_localized_strings[g_equip_slot_label_ids[slot]];
        return text.empty() ? std::string(label) : fmt::format("{} ({})", text, label);
    }
    if (item->iItemNo != -1) {
        const auto& record = g_item_records[item->iItemNo];
        if (record.category == W8_ITEM_CATEGORY_SPELL_SOURCE && record.spell_id != W8_SPELL_NONE &&
            reviewed->spell_learned[record.spell_id] && (item->identified || item->spell_hint))
            text += fmt::format(" ({})", g_localized_strings[0x930]);
    }
    return text;
}

UiResult W8CampScreenState::HandleCampItem(const UiPointerEvent& event, int slot,
                                           W8ItemStorage storage)
{
    const auto location = CampItemAt(slot, storage);
    auto* item = g_status.party.itemAt(location);
    // A full pool has no free row to show past the end of its list.
    if (!item) return UiResult::Handled;
    const bool equipped = storage == W8ItemStorage::Equipment;
    const bool pool = storage == W8ItemStorage::Pool;
    auto& target = *(pool ? pool_targets[slot] : inventory_targets[slot + (equipped ? 8 : 0)]);
    const bool interactive =
        !(item->iItemNo == -1 &&
          (!(g_status.party.held.item.iItemNo != -1) ||
           (equipped && (item_action == W8_CAMP_ITEM_ACTION_MOVE ||
                         !CanEquipItemInSlot(reviewed, g_status.party.held.item.iItemNo,
                                             static_cast<unsigned char>(slot), true) ||
                         !CanCharacterUseItem(reviewed, g_status.party.held.item.iItemNo)))));
    if (interactive && (event.action != UiPointerAction::Up || event.captured_buttons))
        PlayButtonSound(event.action);
    if (event.action == UiPointerAction::Enter || event.action == UiPointerAction::Leave) {
        if (item->iItemNo != -1 || (g_status.party.held.item.iItemNo != -1))
            item_redraw_flags |= (pool       ? W8_CAMP_ITEM_REDRAW_POOL_CELL_FIRST
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
    if (event.action == UiPointerAction::Wheel && pool) item_range->AdjustValue(event.wheel);
    if (event.action == UiPointerAction::Up && event.inside) {
        if (event.button == UiPointerButton::Left && (event.captured_buttons & SDL_BUTTON_LMASK))
            AfterUiDispatch([this, location] { HandleCampItemClick(location); });
        if (event.button == UiPointerButton::Right && (event.captured_buttons & SDL_BUTTON_RMASK) &&
            item->iItemNo != -1 && item_action != W8_CAMP_ITEM_ACTION_IDENTIFY)
            AfterUiDispatch([this, item] { ShowCampItemInfo(item); });
    }
    if (event.action == UiPointerAction::Enter || event.action == UiPointerAction::Up ||
        event.action == UiPointerAction::Wheel) {
        SetUiHelpDelay(std::min({g_settings.tooltip_delay_ms, 300}));
        g_ui_help_forced = true;
        target.setTooltip([this, slot, storage] { return CampItemTooltip(slot, storage); });
    }
    return UiResult::Handled;
}

/* The items page's per-row label boxes: the backpack grid (two columns of
   four at 0x0d/0xc2), the visible pool rows (0x22c/0xc2), and the twelve
   equipment icons laid out by g_camp_screen_regions. Each occupied
   slot gets a hover label drawn by DrawCampItemLabel. */
// FUNCTION: WIZ8 0x005bbe30
void W8CampScreenState::DrawCampItemIcons(void)
{
    unsigned int index;
    unsigned int count;
    int y;
    const W8CampScreenRegion* region;
    W8ItemInstance* item;
    W8Character* character = reviewed;

    for (index = 0; index < 8; ++index) {
        item = &character->backpack[index];
        if (item->iItemNo != -1) {
            DrawCampItemLabel(item, (index & 1) * 0x31 + 0xd,
                              (index >> 1) * 0x39 + 0xc2 + (index & 1) * 0x10, 0);
        }
    }
    index = 0;
    while (true) {
        count = item_list_count - item_scroll;
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
        DrawCampItemLabel(&g_status.party.item_pool[item_list[item_scroll + index]],
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
void W8CampScreenState::DrawCampItemLabel(W8ItemInstance* item, int left, int top, char flag)
{
    std::string name;
    W8ControlsRect bounds;
    W8TextBuffer* text;
    int width;
    int height;

    name = FormatItemDisplayName(item, false);
    text_buffer = name;
    bounds.left = left;
    bounds.top = top;
    bounds.right = left + 0xfa;
    bounds.bottom = top + 0xfa;
    text = new W8TextBuffer(&bounds, text_buffer, g_font10arial,
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

void W8CampScreenState::CreateCampItemControls()
{
    const auto button = [](UiLayer& layer, int art, std::array<int, 5> f, SDL_Rect bounds, int help,
                           bool toggle, std::function<void()> action) {
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
    item_controls.actions[0] =
        button(item_controls.input, 0x11e, {3, 0, 1, 2, 2}, {0x7d, 0x19c, 28, 24}, 2385, false,
               [this] { UnequipBothHands(); });
    item_controls.actions[1] =
        button(item_controls.input, 0x11e, {11, 8, 9, 4, 5}, {0x9e, 0x19c, 28, 24}, 2386, true,
               [this] { TogglePartyRowFlag(); });
    constexpr int frames[] = {0, 15, 20, 5, 10, 40, 30};
    constexpr int y[] = {0, 25, 75, 50, 108, 142, 176};
    for (int index = 0; index < 7; ++index) {
        const int f = frames[index];
        item_controls.filters[index] =
            button(item_controls.input, 0x11f, {f + 3, f, f + 1, f + 2, f + 4},
                   {0x1e5, 0xc1 + y[index], 24, 24}, 2378 + index, index < 6, [this, index] {
                       if (index < 6)
                           ToggleCampItemFilter(static_cast<W8CampItemFilterButton>(index));
                       else
                           SortCampItemPool();
                   });
    }
    for (int index = 0; index < 2; ++index) {
        const int f = index * 5;
        item_controls.tabs[index] = button(
            item_controls.secondary, 0x121, {f + 3, f, f + 1, f + 2, f + 4},
            {0x13e, 9 + index * 21, 20, 20}, 0x95d + index, true, [this, index] {
                SetCampInfoPageMode(index ? W8_CAMP_INFO_PAGE_CHARACTER : W8_CAMP_INFO_PAGE_ITEMS);
            });
    }
    const auto label = [this](SDL_Rect bounds, std::function<void()> action, int help) {
        auto& target = item_controls.secondary.add(bounds, [action](const UiPointerEvent& event) {
            if ((event.button == UiPointerButton::Right) &&
                (event.action != UiPointerAction::Up || event.captured_buttons))
                PlayButtonSound(event.action);
            if (event.action == UiPointerAction::Up && event.inside &&
                event.button == UiPointerButton::Right && event.captured_buttons)
                AfterUiDispatch(action);
            return UiResult::Handled;
        });
        if (help >= 0) target.setTooltip([help] { return std::string(g_localized_strings[help]); });
        return &target;
    };
    for (int index = 0; index < 7; ++index)
        item_controls.stats[index] = label(
            {0x1c2, 0x3a + index * 14, 77, 13},
            [this, index] { OpenAttributeInfoDialog(static_cast<W8Attribute>(index)); }, 0x958);
    constexpr int info_y[] = {0x3a, 0x48, 0x80, 0x8e};
    for (int index = 0; index < 4; ++index)
        item_controls.info[index] = label(
            {0x144, info_y[index], 118, 13},
            [this, index] { OpenSecondaryAttributeInfoDialog(index < 2 ? index : 3); }, 0x958);
    item_controls.help =
        label({0x141, 0x54, 120, 13}, [this] { OpenSecondaryAttributeInfoDialog(4); }, -1);
    item_controls.help->setTooltip([this] { return item_controls.help_text; });
}

// FUNCTION: WIZ8 0x005b9270
void W8CampScreenState::EnableCampActionButtons(void)
{
    item_controls.actions[0]->SetVisible(true);
    item_controls.actions[1]->SetVisible(true);
    if (g_status.game_started) {
        item_controls.actions[1]->SetEnabled(true);
        if (g_status.party.rows[ReviewedPartySlot()].item_action_pending) {
            item_controls.actions[1]->SetPressed(true);
            item_controls.actions[0]->SetEnabled(false);
        } else {
            item_controls.actions[1]->SetPressed(false);
            item_controls.actions[0]->SetEnabled(true);
        }
    } else {
        item_controls.actions[0]->SetEnabled(false);
        item_controls.actions[1]->SetEnabled(false);
    }
}

// FUNCTION: WIZ8 0x005b9310
void W8CampScreenState::DisableCampActionButtons(void)
{
    item_controls.actions[0]->SetVisible(false);
    item_controls.actions[1]->SetVisible(false);
}

// FUNCTION: WIZ8 0x005b9330
void W8CampScreenState::RefreshCampActionPanel(bool invalidate)
{
    for (const auto& button : item_controls.actions) {
        button->m_dirty |= invalidate;
        button->Refresh();
    }
}

/* The right-hand panel of seven tabs: the six realm filters and the pool sort
   button. */
/* Item filter buttons keep their secondary state when the corresponding
   filter is set. The sort button has no item-filter flag. */
// FUNCTION: WIZ8 0x005b97b0
void W8CampScreenState::UpdateCampItemFilters(void)
{
    int index;
    unsigned char flag;

    for (index = 0; index < 7; ++index) {
        item_controls.filters[index]->SetVisible(true);
    }
    if (!g_status.game_started) {
        for (index = 0; index < 7; ++index) {
            item_controls.filters[index]->SetEnabled(false);
        }
        return;
    }
    for (index = 0; index < 7; ++index) {
        item_controls.filters[index]->SetEnabled(true);
        switch (index) {
        case 0:
            flag = item_filters[W8_CAMP_ITEM_FILTER_HAND];
            break;
        case 1:
            flag = item_filters[W8_CAMP_ITEM_FILTER_BODY];
            break;
        case 2:
            flag = item_filters[W8_CAMP_ITEM_FILTER_OTHER];
            break;
        case 3:
            flag = item_filters[W8_CAMP_ITEM_FILTER_ACCESSORY];
            break;
        case 4:
            flag = item_filters[W8_CAMP_ITEM_FILTER_USABLE];
            break;
        case 5:
            flag = item_filters[W8_CAMP_ITEM_FILTER_UNIDENTIFIED];
            break;
        default:
            continue;
        }
        if (flag != 0) {
            item_controls.filters[index]->SetPressed(true);
        }
    }
}

// FUNCTION: WIZ8 0x005b98c0
void W8CampScreenState::DisableItemsRealmTabs(void)
{
    int index;

    for (index = 0; index < 7; ++index) {
        item_controls.filters[index]->SetVisible(false);
    }
}

// FUNCTION: WIZ8 0x005b98e0
void W8CampScreenState::RefreshItemsTabPanel(bool invalidate)
{
    for (const auto& button : item_controls.filters) {
        button->m_dirty |= invalidate;
        button->Refresh();
    }
}

// FUNCTION: WIZ8 0x005b9f00
void W8CampScreenState::EnableCampSecondaryPanel(void)
{
    int index;

    item_controls.secondary.setEnabled(true);
    for (index = 0; index < 2; ++index) {
        item_controls.tabs[index]->SetVisible(true);
        item_controls.tabs[index]->SetEnabled(true);
    }
    if (info_page == W8_CAMP_INFO_PAGE_ITEMS) {
        SetCampInfoPageMode(W8_CAMP_INFO_PAGE_ITEMS);
        return;
    }
    SetCampInfoPageMode(W8_CAMP_INFO_PAGE_CHARACTER);
}

// FUNCTION: WIZ8 0x005b9f60
void W8CampScreenState::DisableCampSecondaryPanel(void)
{
    int index;

    item_controls.secondary.setEnabled(false);
    for (index = 0; index < 2; ++index) {
        item_controls.tabs[index]->SetVisible(false);
    }
}

// FUNCTION: WIZ8 0x005b9f90
void W8CampScreenState::RefreshCampSecondaryPanel(bool invalidate)
{
    if (invalidate) {
        item_controls.dirty = true;
    }
    for (const auto& tab : item_controls.tabs) item_controls.dirty |= tab->m_dirty;
    if (item_controls.dirty) {
        CatalogArtwork::Load(0x120, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0x13c, 7);
        for (const auto& tab : item_controls.tabs) {
            tab->m_dirty = true;
            tab->Refresh();
        }
        item_controls.dirty = false;
    }
}

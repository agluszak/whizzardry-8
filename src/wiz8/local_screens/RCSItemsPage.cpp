#include "wiz8/local_screens/RCSItemsPage.h"

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>
#include <fmt/printf.h>

#include "dialog_code/DialogBase.h"
#include "local_code/ControlsRect.h"
#include "local_code/RangeControl.h"
#include "local_code/TextBuffer.h"
#include "ui_input.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/button_interaction.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/canvas/image.h"
#include "wiz8/canvas/sprite.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/character_skills.h"
#include "wiz8/cursor.h"
#include "wiz8/data/records.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/dialog_code/AssayDialog.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/dialog_code/DialogFactoryDialogs.h"
#include "wiz8/dialog_code/StatInfoDialogs.h"
#include "wiz8/fonts.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/inventory.h"
#include "wiz8/item_rules.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/live_party.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/GameplayDatabase.h"
#include "wiz8/local_code/GameplayMods.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/RCSCommon.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/party_stats.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/platform/input.h"
#include "wiz8/platform/random.h"
#include "wiz8/platform/timer.h"
#include "wiz8/render/frame.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/ui_layer.h"
#include "wiz8/utility.h"
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

/* The items page swaps two control panels in and out: mode zero shows the
   item page, mode one the character page. */
// FUNCTION: WIZ8 0x005B9FD0
void W8CampItemsPage::SetCampInfoPageMode(W8CampInfoPage mode)
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
        screen.character_info->SetActive(true);
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
        screen.character_info->SetActive(false);
        for (index = 0; index < 7; ++index) {
            item_controls.stats[index]->setEnabled(true);
        }
        for (index = 0; index < 4; ++index) {
            item_controls.info[index]->setEnabled(true);
        }
        break;
    }
    info_page = mode;
    screen.redraw_flags |= W8_CAMP_REDRAW_CHARACTER_INFO;
}

// FUNCTION: WIZ8 0x005BA110
void W8CampItemsPage::OpenItemInfoDialog(W8ItemInstance* item)
{
    W8DialogBase* dialog;

    if (!IsItemWornByCharacter(screen.reviewed, item) &&
        !IsItemCarriedByCharacter(screen.reviewed, item)) {
        dialog = new W8AssayDialog(*item, 0, {kInfoDialogX, kInfoDialogY});
    } else {
        dialog = new W8AssayDialog(*item, screen.reviewed, {kInfoDialogX, kInfoDialogY});
    }
    dialog->SetText("");

    screen.DisplayCampDialog(std::unique_ptr<W8DialogBase>(dialog));
}

// FUNCTION: WIZ8 0x005BA2B0
void W8CampItemsPage::OpenAttributeInfoDialog(W8Attribute attribute)
{
    screen.DisplayCampDialog(std::make_unique<W8AttributeInfoDialog>(attribute));
}

// FUNCTION: WIZ8 0x005BA310
void W8CampItemsPage::OpenSecondaryAttributeInfoDialog(unsigned int uiIndex)
{
    screen.DisplayCampDialog(std::make_unique<W8SecondaryAttributeInfoDialog>(uiIndex));
}

// FUNCTION: WIZ8 0x005BA370
void W8CampItemsPage::IdentifyAndOpenItemInfo(W8ItemInstance* item)
{
    if (CanItemLeaveItsSlot(item)) {
        if (PartyAttemptsToIdentifyItem(item, 0) &&
            item_filters[W8_CAMP_ITEM_FILTER_UNIDENTIFIED] != 0) {
            RebuildCampItemList();
            screen.redraw_flags |= W8_CAMP_REDRAW_ALL;
        }
    }
    OpenItemInfoDialog(item);
    SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
}

// FUNCTION: WIZ8 0x005BA3D0
void W8CampItemsPage::DropHeldCampItem(void)
{
    if (ResolvePendingCampCharacter(true)) {
        if (DropItemInHand(0)) {
            SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
        }
    }
}

// FUNCTION: WIZ8 0x005BA400
void W8CampItemsPage::OpenSplitStackDialog(W8ItemLocation location)
{
    auto* item = g_status.party.itemAt(location);
    if (item->iItemNo != -1 && item->stack_count > 1 &&
        (g_item_records[item->iItemNo].flags & W8_ITEM_FLAG_NO_DISCARD) == 0 &&
        g_item_records[item->iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK) {
        auto dialog = std::make_unique<W8SplitItemDialog>(W8_ITEM_SPLIT_INVENTORY, item, -1,
                                                          SDL_Point{kSplitDialogX, kSplitDialogY});
        dialog->SetText("");

        dialog->on_complete = [this, source = location,
                               actor =
                                   static_cast<unsigned>(screen.ReviewedPartySlot())](int count) {
            if (count <= 0) return;
            if (!SplitItemStack(source, static_cast<unsigned>(count), actor)) {
                screen.ShowNotice(g_localized_strings[0x915], 0, true, false);
                return;
            }
            SetItemCursor(0);
            RebuildCampItemList();
            SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
            screen.redraw_flags |= W8_CAMP_REDRAW_ALL;
        };
        screen.DisplayCampDialog(std::move(dialog));
        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
    }
}

// FUNCTION: WIZ8 0x005BA4F0
void W8CampItemsPage::UseCampItem(W8ItemLocation location)
{
    auto* item = g_status.party.itemAt(location);
    if (ValidateItemSpellUse(screen.ReviewedPartySlot(), item, 0)) {
        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
        return;
    }
    if (CanCastFromItem(screen.reviewed, item)) {
        LearnSpellFromItem(screen.reviewed, item);
    } else {
        if (!IsUsableItemClass(item) || IsSpecialItemId(item)) {
            gXStatus.item_drag = W8ItemDrag{location, screen.ReviewedPartySlot()};
        } else {
            if (!(g_status.party.held.item.iItemNo != -1)) {
                MarkCampCharacterPending(item);
                ExchangeInventoryItems(location, {W8ItemStorage::Hand},
                                       static_cast<unsigned>(g_status.selected_character));
            }
        }
        screen.DismissSelectedPartyCharacter();
    }
    RebuildCampItemList();
    screen.redraw_flags |= W8_CAMP_REDRAW_ALL;
    SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
}

// FUNCTION: WIZ8 0x005BA5D0
void W8CampItemsPage::MergeItemStacksWithHeld(W8ItemLocation location)
{
    if (g_status.party.itemAt(location)->iItemNo != -1) {
        if (MergeItems(screen.ReviewedPartySlot(), location)) {
            RebuildCampItemList();
            screen.redraw_flags |= W8_CAMP_REDRAW_ALL;
            SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
        }
    }
}

// FUNCTION: WIZ8 0x005BA620
void W8CampItemsPage::ReportCastResult(int party_slot)
{
    std::string text;
    W8Character* character;
    int result;

    StartBreathCycle(screen.ReviewedPartySlot(), false);
    result = RevealCharacterItemBindingsByProfession(screen.ReviewedPartySlot(), party_slot);
    character = g_status.party.characters + party_slot;
    if (result == 0) {
        wiz8::sound::play("Data\\Sound\\Misc\\Spell Fizzle 01.wav");
        text = fmt::sprintf(g_localized_strings[0x1be], character->name, 0, 1, 0);
        screen.ShowNotice(text, 0, true, false);
    } else if (result == 1) {
        wiz8::sound::play("Data\\Sound\\Misc\\GeneralMagic.wav");
        text = fmt::sprintf(g_localized_strings[0x1bd], character->name, 0, 1, 0);
        screen.ShowNotice(text, 0, true, false);
    } else if (result == 2) {
        wiz8::sound::play("Data\\Sound\\Misc\\GeneralMagic.wav");
        text = fmt::sprintf(g_localized_strings[0x1bc], character->name, 0, 1, 0);
        screen.ShowNotice(text, 0, true, false);
    }
    screen.redraw_flags |= W8_CAMP_REDRAW_ALL;
    SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
}

// FUNCTION: WIZ8 0x005BA740
void W8CampItemsPage::UseHeldItemOnItem(W8ItemInstance* item)
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
            screen.redraw_flags |= W8_CAMP_REDRAW_ALL;
            SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
            pending_item_action.caster_slot = -1;
            return;
        }
        QueueCharacterEvent(g_status.party.characters + pending_item_action.caster_slot, 0x35, 0,
                            kCharacterEventFullVolume);
    }
}

// FUNCTION: WIZ8 0x005BA8E0
void W8CampItemsPage::TargetCharacterWithHeldItem(unsigned int uiTargetChar)
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
    screen.redraw_flags |= W8_CAMP_REDRAW_ALL;
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

/* Mode/tail bits the update pass keys on: the camp item_action byte at +0xd3f
   selects which use paths are live, and iTargetingMode selects what the item
   cursor is allowed to target. */
// FUNCTION: WIZ8 0x005BAD20
void W8CampItemsPage::UpdateItemCursorForState(bool alternate, W8ItemInstance* item, int slot)
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
                 g_status.party.rows[screen.ReviewedPartySlot()].pending_action !=
                     W8_ACTION_EQUIP)) {
                return;
            }
            SetTargetCursor(GetTargetingCursorForState(alternate));
            return;
        }
        if (item_action == W8_CAMP_ITEM_ACTION_USE) {
            if (CanCharacterActivateItem(screen.reviewed, item) ||
                CanCastFromItem(screen.reviewed, item) || IsUsableItemClass(item)) {
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
void W8CampItemsPage::UnequipBothHands(void)
{
    W8Character* character;

    if (g_combat.active() && !g_combat.state()->round_active && !gXStatus.fPartyMovementMode &&
        g_combat.state()->equip_phase == W8CombatEquipPhase::action_selection) {
        screen.ShowNotice(g_localized_strings[0x903], 0, true, false);
        return;
    }
    if (!IsPartySlotEligible(screen.ReviewedPartySlot())) {
        screen.ShowNotice(g_localized_strings[0x917], 0, true, false);
        return;
    }
    character = g_status.party.characters + screen.ReviewedPartySlot();
    BindEquippedItem(character, W8_EQUIP_SLOT_PRIMARY_WEAPON);
    BindEquippedItem(character, W8_EQUIP_SLOT_SECONDARY_WEAPON);
    if (CanUnequipSlotItem(character, W8_EQUIP_SLOT_PRIMARY_WEAPON) &&
        CanUnequipSlotItem(character, W8_EQUIP_SLOT_SECONDARY_WEAPON)) {
        SwapWeaponSetSlots(screen.ReviewedPartySlot(), false);
        item_redraw_flags |= W8_CAMP_ITEM_REDRAW_EQUIPMENT;
        screen.redraw_flags |= W8_CAMP_REDRAW_PORTRAIT;
        screen.redraw_flags |= W8_CAMP_REDRAW_CHARACTER_INFO;
        return;
    }
    screen.ShowNotice(g_localized_strings[0x916], 0, true, false);
}

// FUNCTION: WIZ8 0x005BB140
void W8CampItemsPage::TogglePartyRowFlag(void)
{
    if (item_controls.actions[1]->IsPressed()) {
        g_status.party.rows[screen.ReviewedPartySlot()].item_action_pending = true;
        item_controls.actions[0]->SetEnabled(false);
        item_controls.actions[0]->m_dirty = true;
        return;
    }
    g_status.party.rows[screen.ReviewedPartySlot()].item_action_pending = false;
    item_controls.actions[0]->SetEnabled(true);
    item_controls.actions[0]->m_dirty = true;
}

// FUNCTION: WIZ8 0x005BB220
void W8CampItemsPage::SortCampItemPool(void)
{
    SortPartyItemPool();
    item_scroll = 0;
    RebuildCampItemList();
    item_redraw_flags |= W8_CAMP_ITEM_REDRAW_POOL;
}

/* UI button order differs from the item-filter order. */
// FUNCTION: WIZ8 0x005BB250
void W8CampItemsPage::ToggleCampItemFilter(W8CampItemFilterButton tab)
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
    screen.redraw_flags |= W8_CAMP_REDRAW_ALL;
}

/* Region reason values are bit codes set by the region manager: 0x8 left
   release, 0x10 left down, 0x40 right press, 0x80 right release, 0x100
   double-click/activate, 0x400 enter/leave transition, 0x800 mouse wheel. */
/* Descriptive name for the right-click identify/show operation shared by
   backpack, equipment and party-pool slots. */
void W8CampItemsPage::ShowCampItemInfo(W8ItemInstance* item)
{
    pending_item_action.identifying_character = screen.reviewed;
    if (CanItemLeaveItsSlot(item) && PartyAttemptsToIdentifyItem(item, 0) &&
        item_filters[W8_CAMP_ITEM_FILTER_UNIDENTIFIED] != 0) {
        RebuildCampItemList();
        screen.redraw_flags |= W8_CAMP_REDRAW_ALL;
    }
    OpenItemInfoDialog(item);
    SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
}

/* A visible cell: the reviewed character's own slot, or the pool row the scrolled
   list shows there (the first free pool entry past the list's end). */
W8ItemLocation W8CampItemsPage::CampItemAt(int slot, W8ItemStorage storage) const
{
    const auto cell = static_cast<unsigned int>(slot);
    if (storage != W8ItemStorage::Pool)
        return {storage, static_cast<unsigned int>(screen.ReviewedPartySlot()), cell};
    const auto visible = item_scroll + cell;
    const auto index =
        visible < item_list_count && g_status.party.item_pool[item_list[visible]].iItemNo != -1
            ? item_list[visible]
            : g_status.party.item_count;
    return {W8ItemStorage::Pool, 0, static_cast<unsigned int>(index)};
}

std::string W8CampItemsPage::CampItemTooltip(int slot, W8ItemStorage storage)
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
            screen.reviewed->spell_learned[record.spell_id] &&
            (item->identified || item->spell_hint))
            text += fmt::format(" ({})", g_localized_strings[0x930]);
    }
    return text;
}

UiResult W8CampItemsPage::HandleCampItem(const UiPointerEvent& event, int slot,
                                         W8ItemStorage storage)
{
    const auto location = CampItemAt(slot, storage);
    auto* item = g_status.party.itemAt(location);
    // A full pool has no free row to show past the end of its list.
    if (!item) return UiResult::Handled;
    const bool equipped = storage == W8ItemStorage::Equipment;
    const bool pool = storage == W8ItemStorage::Pool;
    auto& target = *(pool ? pool_targets[slot] : inventory_targets[slot + (equipped ? 8 : 0)]);
    const bool interactive = !(
        item->iItemNo == -1 &&
        (!(g_status.party.held.item.iItemNo != -1) ||
         (equipped && (item_action == W8_CAMP_ITEM_ACTION_MOVE ||
                       !CanEquipItemInSlot(screen.reviewed, g_status.party.held.item.iItemNo,
                                           static_cast<unsigned char>(slot), true) ||
                       !CanCharacterUseItem(screen.reviewed, g_status.party.held.item.iItemNo)))));
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
void W8CampItemsPage::DrawCampItemIcons(void)
{
    unsigned int index;
    unsigned int count;
    int y;
    const W8CampScreenRegion* region;
    W8ItemInstance* item;
    W8Character* character = screen.reviewed;

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
void W8CampItemsPage::DrawCampItemLabel(W8ItemInstance* item, int left, int top, char flag)
{
    std::string label;
    std::string name;
    W8ControlsRect bounds;
    W8TextBuffer* text;
    int width;
    int height;

    name = FormatItemDisplayName(item, false);
    label = name;
    bounds.left = left;
    bounds.top = top;
    bounds.right = left + 0xfa;
    bounds.bottom = top + 0xfa;
    text = new W8TextBuffer(&bounds, label, g_font10arial,
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

void W8CampItemsPage::CreateCampItemControls()
{
    item_range = std::make_unique<W8RangeControl>(screen.scroll_input, 0x263, 0xc1, 0x275, 0x1a1);
    item_range->on_change = [this](auto* range) {
        item_scroll = range->m_value << 1;
        if (((GetInputState().modifiers & CTRL_DOWN) != 0))
            screen.redraw_flags |= W8_CAMP_REDRAW_ALL;
        else
            item_redraw_flags |= W8_CAMP_ITEM_REDRAW_POOL;
    };
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
void W8CampItemsPage::EnableCampActionButtons(void)
{
    item_controls.actions[0]->SetVisible(true);
    item_controls.actions[1]->SetVisible(true);
    if (g_status.game_started) {
        item_controls.actions[1]->SetEnabled(true);
        if (g_status.party.rows[screen.ReviewedPartySlot()].item_action_pending) {
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
void W8CampItemsPage::DisableCampActionButtons(void)
{
    item_controls.actions[0]->SetVisible(false);
    item_controls.actions[1]->SetVisible(false);
}

// FUNCTION: WIZ8 0x005b9330
void W8CampItemsPage::RefreshCampActionPanel(bool invalidate)
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
void W8CampItemsPage::UpdateCampItemFilters(void)
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
void W8CampItemsPage::DisableItemsRealmTabs(void)
{
    int index;

    for (index = 0; index < 7; ++index) {
        item_controls.filters[index]->SetVisible(false);
    }
}

// FUNCTION: WIZ8 0x005b98e0
void W8CampItemsPage::RefreshItemsTabPanel(bool invalidate)
{
    for (const auto& button : item_controls.filters) {
        button->m_dirty |= invalidate;
        button->Refresh();
    }
}

// FUNCTION: WIZ8 0x005b9f00
void W8CampItemsPage::EnableCampSecondaryPanel(void)
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
void W8CampItemsPage::DisableCampSecondaryPanel(void)
{
    int index;

    item_controls.secondary.setEnabled(false);
    for (index = 0; index < 2; ++index) {
        item_controls.tabs[index]->SetVisible(false);
    }
}

// FUNCTION: WIZ8 0x005b9f90
void W8CampItemsPage::RefreshCampSecondaryPanel(bool invalidate)
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

void W8CampItemsPage::CreateInput()
{
    inventory_input.setEnabled(false);
    pool_input.setEnabled(false);
    for (int slot = 0; slot < 8; ++slot) {
        inventory_targets[slot] =
            &inventory_input.add(SDL_Rect{(slot & 1) * 49 + 11, (slot / 2) * 57 + 192, 47, 55},
                                 [this, slot](const UiPointerEvent& event) {
                                     return HandleCampItem(event, slot, W8ItemStorage::Backpack);
                                 });
        pool_targets[slot] =
            &pool_input.add(SDL_Rect{(slot & 1) * 49 + 512, (slot / 2) * 57 + 192, 47, 55},
                            [this, slot](const UiPointerEvent& event) {
                                return HandleCampItem(event, slot, W8ItemStorage::Pool);
                            });
    }
    for (int slot = 0; slot < 12; ++slot) {
        const auto& bounds = g_camp_screen_regions[slot];
        inventory_targets[8 + slot] =
            &inventory_input.add(SDL_Rect{bounds.x, bounds.y, bounds.width + 1, bounds.height + 1},
                                 [this, slot](const UiPointerEvent& event) {
                                     return HandleCampItem(event, slot, W8ItemStorage::Equipment);
                                 });
    }
}

void W8CampItemsPage::Reset(W8Character* identifying_character)
{
    pending_item_action.identifying_character = identifying_character;
    item_action =
        identifying_character ? W8_CAMP_ITEM_ACTION_IDENTIFY_SPELL : W8_CAMP_ITEM_ACTION_NONE;
    std::ranges::fill(item_filters, 0);
    item_timer_active = false;
    item_timer_expired = false;
    info_page = W8_CAMP_INFO_PAGE_ITEMS;
    item_icons_drawn = false;
}

void W8CampItemsPage::Activate()
{
    inventory_input.setEnabled(true);
    EnableCampActionButtons();
    UpdateCampItemFilters();
    EnableCampSecondaryPanel();
    SetCampInfoPageMode(info_page);
    item_range->SetActive(true);
    if (g_status.game_started) {
        pool_input.setEnabled(true);
        item_scroll = 0;
        RebuildCampItemList();
    }
}

void W8CampItemsPage::Deactivate()
{
    inventory_input.setEnabled(false);
    DisableItemsRealmTabs();
    DisableCampActionButtons();
    DisableCampSecondaryPanel();
    screen.character_info->SetActive(false);
    item_range->SetActive(false);
    if (g_status.game_started) pool_input.setEnabled(false);
}

void W8CampItemsPage::ReviewedCharacterChanged()
{
    EnableCampActionButtons();
    if (item_filters[W8_CAMP_ITEM_FILTER_USABLE] != 0) {
        item_scroll = 0;
        RebuildCampItemList();
    }
    if (item_action != W8_CAMP_ITEM_ACTION_IDENTIFY &&
        item_action != W8_CAMP_ITEM_ACTION_IDENTIFY_SPELL &&
        item_action != W8_CAMP_ITEM_ACTION_USE_ON_ITEM) {
        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
    }
}

void W8CampItemsPage::Tick()
{
    if (item_timer_active && !item_timer_expired && !TimeUntilDeadline(item_timer)) {
        item_timer_expired = true;
        item_redraw_flags |= W8_CAMP_ITEM_REDRAW_EQUIPMENT;
    }
}

void W8CampItemsPage::BeginRedraw()
{
    if (screen.reviewed) item_redraw_flags |= ChangedItemCells();
    // Icons drawn while Ctrl was held are cleared by a full repaint once it is released.
    if (((GetInputState().modifiers & CTRL_DOWN) == 0) && item_icons_drawn) {
        screen.redraw_flags |= W8_CAMP_REDRAW_ALL;
        item_icons_drawn = false;
    }
}

void W8CampItemsPage::DrawControls(bool dialog_open)
{
    RefreshCampActionPanel(false);
    RefreshItemsTabPanel(false);
    RefreshCampSecondaryPanel(false);
    item_range->Redraw(GetFrameBufferSurface(), false);
    if (((GetInputState().modifiers & CTRL_DOWN) != 0) && !dialog_open) {
        DrawCampItemIcons();
        item_icons_drawn = true;
    }
}

/* The items-page redraw driver, run once per update: each pending group of
   redraw flags is cleared by repainting that block, and the two bottom/right
   panels repaint when their own bits are raised. */
void W8CampItemsPage::RedrawCampItemsPage(void)
{
    g_wiz_text_font_secondary.setShade(Font::kNormalShade);
    if ((screen.redraw_flags & W8_CAMP_REDRAW_CHARACTER_INFO) != 0) {
        DrawCampCharacterInfo();
    }
    if ((item_redraw_flags & W8_CAMP_ITEM_REDRAW_BACKPACK) != 0) {
        DrawCampBackpackItems();
    }
    if ((item_redraw_flags & W8_CAMP_ITEM_REDRAW_EQUIPMENT) != 0) {
        DrawCampEquipmentItems();
    }
    if ((item_redraw_flags & W8_CAMP_ITEM_REDRAW_POOL) != 0) {
        DrawCampItemPool();
        item_range->Redraw(GetFrameBufferSurface(), true);
    } else {
        item_range->Redraw(GetFrameBufferSurface(), false);
    }
    RefreshCampActionPanel((screen.redraw_flags & W8_CAMP_REDRAW_ACTION_PANEL) != 0);
    RefreshItemsTabPanel((screen.redraw_flags & W8_CAMP_REDRAW_REALM_TABS) != 0);
}

/* The character block of the items page: name-free value fields, the
   experience bar, the six realm icons with their point pools, the attribute
   rows and the armor summaries. */
void W8CampItemsPage::DrawCampCharacterInfo(void)
{
    std::string text;
    W8Character* character = screen.reviewed;
    int index;
    int realm;
    int top;
    unsigned int filled;
    SDL_Rect clip;

    if (info_page != W8_CAMP_INFO_PAGE_ITEMS) {
        screen.character_info->InvalidateAll();
        item_controls.dirty = true;
        return;
    }
    CatalogArtwork::Load(0x114, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0x136, 0);
    if (character->experience != character->experience_previous_goal) {
        if (character->experience < character->experience_goal) {
            filled = (character->experience - character->experience_previous_goal) * 0x67 /
                     (character->experience_goal - character->experience_previous_goal);
        } else {
            filled = 0x67;
        }
        if (filled != 0) {
            clip = {0x1c0, 0, static_cast<int>(filled), 0x1e0};
            ScopedClipRect clipped(GetFrameBufferSurface(), clip);
            CatalogArtwork::Load(0x11d, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0x1c0,
                                                             0x26);
        }
    }
    DrawRcsText(g_localized_strings[0x91b], 0x159, 10, 0xce,
                g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    DrawRcsText(g_localized_strings[0x91c], 0x15e, 0x18, 0x65,
                g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
    DrawRcsText(g_localized_strings[0x91d], 0x15e, 0x26, 0x65,
                g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
    text = FormatUnsignedIntegerWithCommas(character->experience);
    DrawRcsText(text, 0x1c0, 0x18, 0x67, g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    text = FormatUnsignedIntegerWithCommas(character->experience_goal);
    DrawRcsText(text, 0x1c0, 0x26, 0x67, g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    DrawRcsText(g_localized_strings[0x91e], 0x144, 0x3a, 0x45,
                g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
    DrawRcsText(g_localized_strings[0x91f], 0x144, 0x48, 0x45,
                g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
    DrawRcsText(g_localized_strings[0x920], 0x144, 0x56, 0x45,
                g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
    text = fmt::format("{:d}/{:d}", character->hp_current, character->uiHPMax);
    DrawRcsText(text, 0x18d, 0x3a, 0x2c, g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    text = fmt::format("{:d}/{:d}", character->stamina, character->uiStaminaMax);
    DrawRcsText(text, 0x18d, 0x48, 0x2c, g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    if (character->load_category != 0) {
        g_wiz_text_font_secondary.setPalette(
            g_font_state_palettes[g_load_category_palettes[character->load_category]]);
    }
    text = fmt::format("{:d}/{:d}", character->total_carried_weight / 10,
                       character->carrying_capacity / 10);
    DrawRcsTextJustified(text, 0x18d, 0x56, 0x2c, 0xc,
                         g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    text = fmt::format("{}: {:d}, {}: {:d}", g_localized_strings[0x8c0],
                       character->inventory_weight / 10, g_localized_strings[0x8c1],
                       character->party_weight_share / 10);
    item_controls.help_text = text;
    g_wiz_text_font_secondary.resetPalette();
    DrawRcsText(g_localized_strings[0x921], 0x144, 0x72, 0x75,
                g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    top = 0x3a;
    for (index = 0; index < 7; ++index) {
        DrawRcsText(g_localized_strings[g_attribute_label_ids[index]], 0x1c2, top, 0x4c,
                    g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
        text = fmt::format("{:d}", character->attributes[index].effective);
        DrawRcsText(text, 0x212, top, 0x14, g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
        top += 0xe;
    }
    top = 0xd;
    for (realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
        unsigned int frame;
        if (character->sp_max[realm] == 0) {
            frame = g_spell_realm_animations[realm].frame_count;
        } else {
            text = fmt::format(
                "{:d}/{:d}",
                GetCharacterRealmSpellPoints(character, static_cast<W8SpellRealm>(realm)),
                character->sp_max[realm]);
            DrawTallRcsText(text, 0x242, top, 0x32,
                            g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
            frame = g_spell_realm_animations[realm].initial_frame;
        }
        CatalogArtwork::Load(g_character_resistance_images[realm], 0)
            .drawAndInvalidate(GetFrameBufferSurface(), frame, 0x22d, top);
        top += 0x18;
    }
    DrawRcsText(g_localized_strings[0x922], 0x144, 0x80, 0x45,
                g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
    text = fmt::format("{:d}", character->armor_class_total);
    DrawRcsText(text, 0x18d, 0x80, 0x2c, g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    DrawRcsText(g_localized_strings[0x923], 0x144, 0x8e, 0x45,
                g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
    text = fmt::format("{:d}", character->armor_class_average);
    DrawRcsText(text, 0x18d, 0x8e, 0x2c, g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    item_controls.dirty = true;
}

/* The eight backpack cells under the character block, flag bits 1..8 of
   item_redraw_flags; bit 0 repaints the header strip and caption. */
void W8CampItemsPage::DrawCampBackpackItems(void)
{
    W8Character* character = screen.reviewed;
    unsigned int slot;
    int left;
    int top;
    int right;
    int bottom;
    int item_id;

    if ((item_redraw_flags & W8_CAMP_ITEM_REDRAW_BACKPACK_HEADER) != 0) {
        CatalogArtwork::Load(0x114, 0).drawAndInvalidate(GetFrameBufferSurface(), 1, 0, 0xa5);
        DrawRcsBoldText(g_localized_strings[0x92b], 0xc, 0xae, 0x5d,
                        g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    }
    for (slot = 0; slot < 8; ++slot) {
        left = (slot & 1) * 0x31 + 0xb;
        top = (slot >> 1) * 0x39 + 0xc0;
        right = left + 0x2e;
        bottom = top + 0x36;
        if ((item_redraw_flags & (W8_CAMP_ITEM_REDRAW_BACKPACK_CELL_FIRST << slot)) == 0) {
            continue;
        }
        InvalidateRegion(left, top, right, bottom);
        item_id = character->backpack[slot].iItemNo;
        if (item_id == -1 || CanCharacterUseItem(character, item_id)) {
            CatalogArtwork::Load(0x1b6, 0).drawRect(
                GetFrameBufferSurface(), SDL_Rect{0, 0, (right) - (left), (bottom) - (top)}, left,
                top);
        } else {
            CatalogArtwork::Load(0x11a, 0).draw(GetFrameBufferSurface(), 0, left, top);
        }
        if (item_id != -1) {
            DrawSprite(GetFrameBufferSurface(), *ItemSprites(item_id), 0, left + 1, top + 1);
            DrawCampItemQuantity(&character->backpack[slot], left + 1, top + 0x28, 0x2c);
            if (!character->backpack[slot].identified) {
                CatalogArtwork::Load(0x11b, 0).draw(GetFrameBufferSurface(), 0, left, top);
            }
        }
        if (item_id != -1 && g_item_records[item_id].binds_on_equip != 0 &&
            character->backpack[slot].bound) {
            CatalogArtwork::Load(0x115, 0).draw(GetFrameBufferSurface(), 0x32, left, top);
        }
        if (inventory_targets[slot]->hovered() &&
            (item_id != -1 || ((g_status.party.held.item.iItemNo != -1) &&
                               item_action != W8_CAMP_ITEM_ACTION_MOVE))) {
            CatalogArtwork::Load(0x115, 0).draw(GetFrameBufferSurface(), 0x30, left, top);
        }
    }
}

/* The twelve equipment cells; bit 9 of item_redraw_flags repaints the paper
   doll backdrop and race/gender portrait, bits 10..21 the slots. */
void W8CampItemsPage::DrawCampEquipmentItems(void)
{
    std::string text;
    W8Character* character = screen.reviewed;
    const W8CampScreenRegion* region;
    unsigned int slot;
    int item_id;
    int frame;

    if ((item_redraw_flags & W8_CAMP_ITEM_REDRAW_PAPER_DOLL) != 0) {
        CatalogArtwork::Load(0x114, 0).drawAndInvalidate(GetFrameBufferSurface(), 2, 0x71, 0xa5);
        CatalogArtwork::Load(
            g_race_portrait_images[character->iRace * W8_RACE_GNOME + character->gender], 0)
            .drawAndInvalidate(GetFrameBufferSurface(), 0, 0xc2, 0xa5);
        screen.redraw_flags |= W8_CAMP_REDRAW_ACTION_PANEL;
    }
    for (slot = 0; slot < 12; ++slot) {
        if ((item_redraw_flags & (W8_CAMP_ITEM_REDRAW_EQUIPMENT_CELL_FIRST << slot)) == 0) {
            continue;
        }
        region = &g_camp_screen_regions[slot];
        InvalidateRegion(region->x, region->y, region->x + region->width,
                         region->y + region->height);
        CatalogArtwork::Load(0x1b6, 0).drawRect(
            GetFrameBufferSurface(),
            SDL_Rect{0, 0, (region->x + region->width) - (region->x),
                     (region->y + region->height) - (region->y)},
            region->x, region->y);
        item_id = character->EquippedItem[slot].iItemNo;
        if (item_id == -1) {
            if (slot == 7 || slot == 9) {
                int paired =
                    character->EquippedItem[GetPairedEquipSlot(static_cast<W8EquipSlot>(slot))]
                        .iItemNo;
                if (paired != -1 && (g_item_records[paired].flags & W8_ITEM_FLAG_TWO_HANDED) != 0) {
                    CatalogArtwork::Load(0x146, 0).drawAndInvalidate(GetFrameBufferSurface(), 0,
                                                                     region->x, region->y);
                }
            }
        } else {
            DrawSpriteAndInvalidate(GetFrameBufferSurface(), *ItemSprites(item_id), 1, region->x,
                                    region->y);
            DrawCampItemQuantity(&character->EquippedItem[slot], region->x + 2,
                                 region->y + region->height - 0xd, region->width - 4);
            if (!character->EquippedItem[slot].identified) {
                CatalogArtwork::Load(0x11b, 0).draw(GetFrameBufferSurface(),
                                                    static_cast<short>(region->unidentified_frame),
                                                    region->x, region->y);
            }
        }
        switch (slot) {
        case W8_EQUIP_SLOT_HEAD:
            frame = 0;
            break;
        case W8_EQUIP_SLOT_TORSO:
            frame = 1;
            break;
        case W8_EQUIP_SLOT_HANDS:
            frame = 3;
            break;
        case W8_EQUIP_SLOT_LEGS:
            frame = 2;
            break;
        case 0xb:
            frame = 4;
            break;
        default:
            goto no_armor_label;
        }
        text = fmt::format("{:d}", character->armor_class_by_location[frame]);
        CatalogArtwork::Load(0x11c, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, region->x + 2,
                                                         region->y + 1);
        DrawRcsText(text, region->x + 2, region->y + 2, 0x11,
                    g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    no_armor_label:
        CatalogArtwork::Load(0x115, 0).drawAndInvalidate(GetFrameBufferSurface(), region->frame + 3,
                                                         region->x - 2, region->y - 2);
        if (item_id != -1 && g_item_records[item_id].binds_on_equip != 0 &&
            character->EquippedItem[slot].bound) {
            CatalogArtwork::Load(0x115, 0).drawAndInvalidate(
                GetFrameBufferSurface(), region->frame + 2, region->x - 2, region->y - 2);
        }
        if (inventory_targets[kCampEquipmentRegionOffset + slot]->hovered()) {
            if (!(g_status.party.held.item.iItemNo != -1)) {
                if (character->EquippedItem[slot].iItemNo == -1) {
                    continue;
                }
            } else if (item_action == W8_CAMP_ITEM_ACTION_MOVE ||
                       !CanEquipItemInSlot(character, g_status.party.held.item.iItemNo,
                                           static_cast<unsigned char>(slot), true) ||
                       !CanCharacterUseItem(character, g_status.party.held.item.iItemNo)) {
                if (!(g_status.party.held.item.iItemNo != -1) ||
                    item_action != W8_CAMP_ITEM_ACTION_MOVE) {
                    continue;
                }
                if (character->EquippedItem[slot].iItemNo == -1) {
                    continue;
                }
            }
            frame = region->frame;
        } else {
            if (!(g_status.party.held.item.iItemNo != -1) ||
                item_action == W8_CAMP_ITEM_ACTION_MOVE ||
                !IsPartySlotEligible(screen.ReviewedPartySlot()) ||
                !CanEquipItemInSlot(character, g_status.party.held.item.iItemNo,
                                    static_cast<unsigned char>(slot), true) ||
                !CanCharacterUseItem(character, g_status.party.held.item.iItemNo)) {
                continue;
            }
            frame = region->frame + 1;
        }
        CatalogArtwork::Load(0x115, 0).drawAndInvalidate(GetFrameBufferSurface(), frame,
                                                         region->x - 2, region->y - 2);
    }
}

/* The shared party item pool: the bottom-right grid of up to eight visible
   cells, plus the header strip and the gold counter when bit 22 is raised. */
void W8CampItemsPage::DrawCampItemPool(void)
{
    std::string text;
    W8Character* character = screen.reviewed;
    unsigned int visible;
    unsigned int i;
    int left;
    int top;
    int right;
    int bottom;
    W8ItemInstance* item;

    if ((item_redraw_flags & W8_CAMP_ITEM_REDRAW_POOL_HEADER) != 0) {
        CatalogArtwork::Load(0x114, 0).drawAndInvalidate(GetFrameBufferSurface(), 3, 0x1de, 0xa5);
        CatalogArtwork::Load(0x118, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0x202, 0x1a6);
        screen.redraw_flags |= W8_CAMP_REDRAW_REALM_TABS;
    }
    if (!g_status.game_started) {
        return;
    }
    if ((item_redraw_flags & W8_CAMP_ITEM_REDRAW_POOL_HEADER) != 0) {
        DrawRcsBoldText(g_localized_strings[0x92c], 0x1e6, 0xae, 0x8d,
                        g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
        text = FormatUnsignedIntegerWithCommas(g_status.party.gold);
        DrawRcsText(text, 0x229, 0x1a9, 0x30,
                    g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    }
    for (i = 0;; ++i) {
        visible = item_list_count - item_scroll;
        if (visible > 8) {
            visible = 8;
        }
        if (i >= visible) {
            break;
        }
        left = (i & 1) * 0x31 + 0x200;
        top = (i >> 1) * 0x39 + 0xc0;
        right = left + 0x2e;
        bottom = top + 0x36;
        item = &g_status.party.item_pool[item_list[item_scroll + i]];
        if ((item_redraw_flags & (W8_CAMP_ITEM_REDRAW_POOL_CELL_FIRST << i)) == 0) {
            continue;
        }
        InvalidateRegion(left, top, right, bottom);
        if (!CanCharacterUseItem(character, item->iItemNo)) {
            CatalogArtwork::Load(0x11a, 0).draw(GetFrameBufferSurface(), 0, left, top);
        } else {
            CatalogArtwork::Load(0x1b6, 0).drawRect(
                GetFrameBufferSurface(), SDL_Rect{0, 0, (right) - (left), (bottom) - (top)}, left,
                top);
        }
        DrawSprite(GetFrameBufferSurface(), *ItemSprites(item->iItemNo), 0, left + 1, top + 1);
        DrawCampItemQuantity(item, left + 1, top + 0x28, 0x2c);
        if (!item->identified) {
            CatalogArtwork::Load(0x11b, 0).draw(GetFrameBufferSurface(), 0, left, top);
        }
        if (g_item_records[item->iItemNo].binds_on_equip != 0 && item->bound) {
            CatalogArtwork::Load(0x115, 0).draw(GetFrameBufferSurface(), 0x32, left, top);
        }
        if (pool_targets[i]->hovered()) {
            CatalogArtwork::Load(0x115, 0).draw(GetFrameBufferSurface(), 0x30, left, top);
        }
    }
    for (i = item_list_count - item_scroll; i < 8; ++i) {
        left = (i & 1) * 0x31 + 0x200;
        top = (i >> 1) * 0x39 + 0xc0;
        right = left + 0x2e;
        bottom = top + 0x36;
        if ((item_redraw_flags & (W8_CAMP_ITEM_REDRAW_POOL_CELL_FIRST << i)) != 0) {
            InvalidateRegion(left, top, right, bottom);
            CatalogArtwork::Load(0x1b6, 0).drawRect(
                GetFrameBufferSurface(), SDL_Rect{0, 0, (right) - (left), (bottom) - (top)}, left,
                top);
            if (pool_targets[i]->hovered() && (g_status.party.held.item.iItemNo != -1) &&
                item_action != W8_CAMP_ITEM_ACTION_MOVE) {
                CatalogArtwork::Load(0x115, 0).draw(GetFrameBufferSurface(), 0x30, left, top);
            }
        }
    }
}

/* The stack count or charge figure in a cell's bottom-right corner, keyed off
   the item record's quantity kind; charge figures ride the alternate font
   palettes and restore the default afterwards. */
void W8CampItemsPage::DrawCampItemQuantity(W8ItemInstance* item, int left, int top, int width)
{
    std::string text;
    int height;

    switch (g_item_records[item->iItemNo].quantity_kind) {
    case W8_ITEM_QUANTITY_NONE:
        break;
    case W8_ITEM_QUANTITY_STACK:
        if (item->stack_count > 1) {
            text = fmt::format("{:d}", item->stack_count);
            DrawRcsText(text, left, top, width,
                        g_W8TextBufferAlignMiddle | g_W8TextBufferAlignRight);
        }
        break;
    case W8_ITEM_QUANTITY_CHARGES:
    case W8_ITEM_QUANTITY_USES:
        g_wiz_text_font_secondary.setPalette(g_font_state_palettes[W8_FONT_PALETTE_BLUE]);
        if (!item->identified) {
            text = "?";
        } else {
            text = fmt::format("{:d}", item->uses_or_charges);
        }
        height = g_wiz_text_font_secondary.height();
        DrawRcsTextJustified(text, left, top, width, height,
                             g_W8TextBufferAlignMiddle | g_W8TextBufferAlignRight);
        break;
    case W8_ITEM_QUANTITY_SHOTS:
        g_wiz_text_font_secondary.setPalette(g_font_state_palettes[W8_FONT_PALETTE_YELLOW]);
        text = fmt::format("{:d}", item->uses_or_charges);
        height = g_wiz_text_font_secondary.height();
        DrawRcsTextJustified(text, left, top, width, height,
                             g_W8TextBufferAlignMiddle | g_W8TextBufferAlignRight);
        break;
    }
    g_wiz_text_font_secondary.resetPalette();
}

/* The camp screen's per-frame update: on the first frame after combat mode
   ends it re-raises every redraw flag, then repaints whichever page is active
   together with the level-up/dismissal panels and the dialog on top of the
   frame. */
unsigned int W8CampItemsPage::ChangedItemCells()
{
    auto& shown = shown_inventory;
    unsigned int flags = 0;
    const bool same_character = shown.character == screen.reviewed;
    for (unsigned int slot = 0; slot < 8; ++slot)
        if (!same_character || shown.backpack[slot] != screen.reviewed->backpack[slot])
            flags |= W8_CAMP_ITEM_REDRAW_BACKPACK_CELL_FIRST << slot;
    for (unsigned int slot = 0; slot < 12; ++slot)
        if (!same_character || shown.equipment[slot] != screen.reviewed->EquippedItem[slot])
            flags |= W8_CAMP_ITEM_REDRAW_EQUIPMENT_CELL_FIRST << slot;
    const auto pool = std::span{g_status.party.item_pool}.first(g_status.party.item_count);
    if (!std::ranges::equal(shown.pool, pool)) flags |= W8_CAMP_ITEM_REDRAW_POOL;
    shown.character = screen.reviewed;
    std::ranges::copy(screen.reviewed->backpack, shown.backpack.begin());
    std::ranges::copy(screen.reviewed->EquippedItem, shown.equipment.begin());
    shown.pool.assign(pool.begin(), pool.end());
    return flags;
}

/* Equip-slot-group item filters are exclusive: selecting one clears the others. */
void W8CampItemsPage::ClearOtherCampItemGroupFilters(W8CampItemFilter filter)
{
    unsigned int index;

    for (index = 2; index < 6; ++index) {
        if (index != filter) {
            item_filters[index] = 0;
        }
    }
}

/* Rebuild the visible item list from the party pool under the item filters:
   flag 0 restricts to items the displayed character can use, flag 1 to
   unidentified items, and flags 2-5 each admit one equip-slot group. The
   scrollbar's range and value track the count; the scroll position snaps back
   inside an emptied or shrunken list. */
void W8CampItemsPage::RebuildCampItemList(void)
{
    unsigned int index;
    unsigned char filter;
    W8ItemInstance* pool;
    W8RangeControl* scrollbar;
    int rows;

    item_list_count = 0;
    filter = 0;
    for (index = 0; index < 6; ++index) {
        if (index != 0 && index != 1 && item_filters[index] != 0) {
            filter |= 1 << index;
        }
    }
    pool = g_status.party.item_pool;
    for (index = 0; index < g_status.party.item_count; ++index, ++pool) {
        if (pool->iItemNo != -1 &&
            (item_filters[W8_CAMP_ITEM_FILTER_USABLE] == 0 ||
             CanCharacterUseItem(screen.reviewed, pool->iItemNo)) &&
            (item_filters[W8_CAMP_ITEM_FILTER_UNIDENTIFIED] == 0 || !pool->identified) &&
            (filter == 0 || (filter & static_cast<unsigned char>(
                                          1 << GetItemEquipSlotGroup(pool->iItemNo))) != 0)) {
            item_list[item_list_count] = index;
            ++item_list_count;
        }
    }
    scrollbar = item_range.get();
    scrollbar->SetEnabled(item_list_count > 8);
    rows = item_list_count - 8;
    if (rows > 0) {
        scrollbar->SetRange(0, rows % 2 == 0 ? rows / 2 : rows / 2 + 1);
    }
    if (item_list_count == 0) {
        item_scroll = 0;
    } else if (item_list_count <= item_scroll) {
        item_scroll = (item_list_count - 1) / 8 * 8;
    }
    if ((item_scroll & 1) == 0) {
        scrollbar->SetValue(item_scroll >> 1);
    } else {
        scrollbar->SetValue((item_scroll >> 1) + 1);
    }
}

/* The shared click handler for the camp item regions: a backpack slot arrives
   with origin 0, a worn slot with origin 1 and a party-pool row with origin
   2. The screen's item_action picks the interpretation - plain handling,
   identify, stack split, use, use held item on item - and the held item can
   merge onto the clicked stack, swap with it, or drop into the pool. */
void W8CampItemsPage::SelectPendingCampCharacter()
{
    screen.SelectCampCharacter(*g_status.party.companion_inventory);
    if (IsPartySlotEligible(screen.ReviewedPartySlot())) {
        QueueCharacterEvent((&g_status.party.characters[*g_status.party.companion_inventory]), 109,
                            0, kCharacterEventFullVolume);
    } else {
        std::string text =
            fmt::sprintf(g_localized_strings[0x931],
                         g_status.party.characters[*g_status.party.companion_inventory].name);
        screen.ShowNotice(text, 0, true, false);
    }
}

void W8CampItemsPage::HandleCampItemClick(W8ItemLocation location)
{
    W8ItemInstance* item = g_status.party.itemAt(location);
    const auto origin = location.storage;
    const unsigned int slot_index = location.slot;
    W8CombatSlot target;
    short related_kind;
    bool changed = false;
    bool merged = false;
    bool partially_merged = false;
    bool merge_tried = false;
    bool same_kind = false;
    bool choose_character;
    unsigned int index;
    unsigned int old_pool_count;
    int result;
    int party_slot;
    W8EquipSlot paired_slot;
    W8EquipSlot equip_slot = static_cast<W8EquipSlot>(slot_index);
    bool reidentify;
    W8ItemInstance* paired;
    W8Character* character;
    std::string text;

    if (item == 0) {
        Fail("pPCItem != NULL");
    }

    /* A held stackable whose name kind the clicked item merges with counts as
       the same item for the use-merge path below. */
    if ((g_status.party.held.item.iItemNo != -1) && item->iItemNo != -1 &&
        GetItemMergeKind(item->iItemNo, &related_kind) &&
        g_item_records[g_status.party.held.item.iItemNo].unidentified_name_index == related_kind) {
        same_kind = true;
    }
    if (item->iItemNo == -1 && !(g_status.party.held.item.iItemNo != -1)) {
        return;
    }
    if (origin == W8ItemStorage::Equipment && (g_status.party.held.item.iItemNo != -1) &&
        item_action != W8_CAMP_ITEM_ACTION_MOVE) {
        if (!same_kind && !CanEquipItemInSlot(screen.reviewed, g_status.party.held.item.iItemNo,
                                              slot_index, true)) {
            return;
        }
        if (!CanCharacterUseItem(screen.reviewed, g_status.party.held.item.iItemNo)) {
            return;
        }
    }
    if (item_action == W8_CAMP_ITEM_ACTION_CAST_SPELL ||
        item_action == W8_CAMP_ITEM_ACTION_USE_ON_CHARACTER) {
        return;
    }
    if (!g_status.game_started) {
        text = g_localized_strings[0x900];
        screen.ShowNotice(text, 0, true, false);
        return;
    }
    character = &g_status.party.characters[screen.ReviewedPartySlot()];
    if (character->uiCondition[W8_CONDITION_MISSING] != 0 &&
        (origin == W8ItemStorage::Equipment || origin == W8ItemStorage::Backpack)) {
        text = g_localized_strings[0x907];
        screen.ShowNotice(text, 0, true, false);
        return;
    }
    if (character->uiCondition[W8_CONDITION_WEBBED] != 0 &&
        (origin == W8ItemStorage::Equipment || origin == W8ItemStorage::Backpack)) {
        text = g_localized_strings[0x908];
        screen.ShowNotice(text, 0, true, false);
        return;
    }
    if (character->uiCondition[W8_CONDITION_TURNCOAT] != 0 &&
        (origin == W8ItemStorage::Equipment || origin == W8ItemStorage::Backpack)) {
        text = g_localized_strings[0x909];
        screen.ShowNotice(text, 0, true, false);
        return;
    }
    if (character->highest_condition >= W8_CONDITION_TURNCOAT && origin != W8ItemStorage::Pool &&
        (g_status.party.held.item.iItemNo != -1) &&
        g_status.party.held.actor != screen.ReviewedPartySlot()) {
        text = g_localized_strings[0x901];
        screen.ShowNotice(text, 0, true, false);
        return;
    }

    /* In combat an item click spends the character's action allowance unless
       it only touches a weapon-class slot or folds into the same-kind merge.
       A weapon or shield being moved onto a slot it cannot pair with stays
       gated as well. */
    bool gated = true;
    if (g_combat.active() && origin != W8ItemStorage::Pool) {
        if (!(g_status.party.held.item.iItemNo != -1) ||
            ((g_item_records[g_status.party.held.item.iItemNo].equip_class ==
                  W8_ITEM_EQUIP_CLASS_THROWN_WEAPON ||
              g_item_records[g_status.party.held.item.iItemNo].equip_class ==
                  W8_ITEM_EQUIP_CLASS_AMMUNITION) &&
             g_status.party.held.actor == screen.ReviewedPartySlot() &&
             (origin != W8ItemStorage::Equipment ||
              HeldItemFitsPairedSlot(screen.ReviewedPartySlot(), equip_slot)))) {
            if (item->iItemNo == -1 ||
                g_item_records[item->iItemNo].equip_class == W8_ITEM_EQUIP_CLASS_THROWN_WEAPON ||
                g_item_records[item->iItemNo].equip_class == W8_ITEM_EQUIP_CLASS_AMMUNITION ||
                same_kind) {
                gated = false;
            }
        }
    }
    if (gated && !IsCampActionAllowed(screen.ReviewedPartySlot())) {
        return;
    }
    if (((GetInputState().modifiers & SHIFT_DOWN) != 0)) {
        TakeItemUnitToHand(location);
        return;
    }
    if (item_action == W8_CAMP_ITEM_ACTION_IDENTIFY_SPELL) {
        party_slot = CharacterPointerToPartySlot(pending_item_action.identifying_character);
        if (!CanItemLeaveItsSlot(item)) {
            QueueCharacterEvent(&g_status.party.characters[party_slot], 0x35, 0,
                                kCharacterEventFullVolume);
            return;
        }
        if (pending_item_action.identifying_character == 0) {
            Fail("gpIdentifyingPC != NULL");
        }
        old_pool_count = g_status.party.item_count;
        reidentify = false;
        ResetCombatSlot(&target);
        target.iType = W8_TARGET_KIND_ITEM;
        target.item = location;
        StartBreathCycle(party_slot, false);
        W8PartySlotRow* row = &g_status.party.rows[party_slot];
        if (row->pending_action == W8_ACTION_USE_ITEM &&
            g_item_records[g_status.party.itemAt(row->pending_action_detail.item_use.item)->iItemNo]
                    .spell_id == W8_SPELL_IDENTIFY_ITEM) {
            reidentify = true;
            result = CommitPartySlotItemUse(
                party_slot, g_status.party.itemAt(row->pending_action_detail.item_use.item),
                &target);
        } else {
            result = CommitPartySlotSpell(party_slot, W8_SPELL_IDENTIFY_ITEM, 8, &target);
        }
        if (origin == W8ItemStorage::Pool && reidentify &&
            old_pool_count != g_status.party.item_count) {
            item =
                &g_status.party.item_pool[g_status.party.item_count - old_pool_count + slot_index];
            RebuildCampItemList();

            screen.redraw_flags |= W8_CAMP_REDRAW_CHARACTER_INFO;
            item_redraw_flags |= W8_CAMP_ITEM_REDRAW_POOL;
        }
        if (result == 1 && item->iItemNo != -1) {
            OpenItemInfoDialog(item);
            if (!item->identified) {
                QueueCharacterEvent(&g_status.party.characters[party_slot], 0x35, 0,
                                    kCharacterEventFullVolume);
            }
        }
        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
        return;
    }
    if (item_action == W8_CAMP_ITEM_ACTION_USE_ON_ITEM) {
        UseHeldItemOnItem(item);
        return;
    }
    if (item_action == W8_CAMP_ITEM_ACTION_IDENTIFY) {
        IdentifyAndOpenItemInfo(item);
        return;
    }
    if (item_action == W8_CAMP_ITEM_ACTION_SPLIT_STACK) {
        if (item->iItemNo != -1) {
            OpenSplitStackDialog(location);
            return;
        }
    } else if (item_action == W8_CAMP_ITEM_ACTION_USE) {
        if (CanCharacterUseItemEntry(screen.reviewed, item) != 0) {
            UseCampItem(location);
        }
        return;
    } else if (item_action == W8_CAMP_ITEM_ACTION_MOVE) {
        if ((g_status.party.held.item.iItemNo != -1)) {
            if (item->iItemNo == -1) {
                SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
                return;
            }
            if (g_status.party.companion_inventory) {
                SelectPendingCampCharacter();
                return;
            }
            MergeItemStacksWithHeld(location);
            return;
        }
        if (item->iItemNo == -1) {
            SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
            return;
        }
    }

    /* Equipped items that may not be removed get one warning dialog and a
       bound mark; the next click then unequips them. */
    if (item->iItemNo != -1 && origin == W8ItemStorage::Equipment) {
        if (!CanUnequipSlotItem(screen.reviewed, equip_slot)) {
            text = g_localized_strings[0x90b];
            screen.ShowNotice(text, 0, true, false);
            if (item->bound) {
                return;
            }
            BindEquippedItem(screen.reviewed, equip_slot);
            return;
        }
        item->bound = true;
    }

    // Changing a weapon hand here supersedes a weapon-set swap queued for the round.
    const auto cancel_queued_swap = [&] {
        if (origin == W8ItemStorage::Equipment &&
            GetPairedEquipSlot(equip_slot) != W8_EQUIP_SLOT_NONE)
            g_status.party.rows[screen.ReviewedPartySlot()].weapon_swap_pending = false;
    };
    if (same_kind) {
        MergeItemUses(screen.ReviewedPartySlot(), location, {W8ItemStorage::Hand});
        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
        changed = true;
        cancel_queued_swap();
        RebuildCampItemList();
    } else {
        /* Fold the held stack onto a matching stack; for the pool the scan
           continues across every row until the held stack is consumed. */
        if ((g_status.party.held.item.iItemNo != -1) &&
            g_item_records[g_status.party.held.item.iItemNo].quantity_kind ==
                W8_ITEM_QUANTITY_STACK) {
            if (item->iItemNo != -1 && item->iItemNo == g_status.party.held.item.iItemNo) {
                const auto outcome = MergeInventoryStacks(location, {W8ItemStorage::Hand});
                merged = outcome.complete;
                partially_merged = outcome.changed && !outcome.complete;
                merge_tried = true;
            }
            if (!merged && origin == W8ItemStorage::Pool) {
                for (index = 0; index < g_status.party.item_count; ++index) {
                    const auto outcome = MergeInventoryStacks({W8ItemStorage::Pool, 0, index},
                                                              {W8ItemStorage::Hand});
                    partially_merged |= outcome.changed && !outcome.complete;
                    if (outcome.complete) {
                        merged = true;
                        break;
                    }
                }
            }
        }
        if (origin == W8ItemStorage::Pool) {
            if (!merged) {
                if ((g_status.party.held.item.iItemNo != -1)) {
                    if (g_status.party.companion_inventory) {
                        SelectPendingCampCharacter();
                    } else if (InsertHeldItemInPool(slot_index)) {
                        changed = true;
                        RebuildCampItemList();
                    } else {
                        text = g_localized_strings[0x90c];
                        screen.ShowNotice(text, 0, true, false);
                    }
                } else if (slot_index < g_status.party.item_count) {
                    /* An empty hand picks the clicked pool row up. */
                    ExchangeInventoryItems(location, {W8ItemStorage::Hand},
                                           static_cast<unsigned>(g_status.selected_character));
                    changed = true;
                    if (item_action == W8_CAMP_ITEM_ACTION_DROP) {
                        DropHeldCampItem();
                        RebuildCampItemList();
                    } else if (item_action == W8_CAMP_ITEM_ACTION_MOVE) {
                        SetHandCursors();
                        RebuildCampItemList();
                    } else {
                        RebuildCampItemList();
                    }
                }
            }
        } else {
            if (!merge_tried &&
                ((!(g_status.party.held.item.iItemNo != -1) && item->iItemNo != -1) ||
                 ((g_status.party.held.item.iItemNo != -1) && !merged))) {
                if (!g_status.party.companion_inventory ||
                    (&g_status.party.characters[*g_status.party.companion_inventory]) ==
                        screen.reviewed) {
                    MarkCampCharacterPending(item);
                    if ((g_status.party.held.item.iItemNo != -1)) {
                        choose_character = g_status.party.held.actor == -1;
                        DeliverExceptionalItemReaction(&g_status.party.held.item, choose_character,
                                                       screen.reviewed);
                    }
                    if (origin == W8ItemStorage::Equipment &&
                        (g_status.party.held.item.iItemNo != -1) &&
                        !HeldItemFitsPairedSlot(screen.ReviewedPartySlot(), equip_slot)) {
                        paired_slot = GetPairedEquipSlot(equip_slot);
                        paired = &screen.reviewed->EquippedItem[paired_slot];
                        if (!CanUnequipSlotItem(screen.reviewed, paired_slot)) {
                            text = g_localized_strings[0x916];
                            screen.ShowNotice(text, 0, true, false);
                            if (paired->bound) {
                                return;
                            }
                            BindEquippedItem(screen.reviewed, paired_slot);
                            return;
                        }
                        if (item->iItemNo == -1) {
                            MarkCampCharacterPending(paired);
                        }
                        if (!EquipHeldItemAndStowPartner(screen.ReviewedPartySlot(), equip_slot)) {
                            screen.ShowNotice(g_localized_strings[0x90c], 0, true, false);
                            return;
                        }
                        changed = true;
                    } else {
                        ExchangeInventoryItems(location, W8ItemLocation{W8ItemStorage::Hand},
                                               static_cast<unsigned>(screen.ReviewedPartySlot()));
                        changed = true;
                        if (origin == W8ItemStorage::Backpack &&
                            screen.reviewed->backpack[slot_index].iItemNo != -1 &&
                            Random(100) < 5) {
                            QueueCharacterEvent(screen.reviewed, g_special_event14, 0,
                                                kCharacterEventFullVolume);
                        }
                    }
                } else {
                    SelectPendingCampCharacter();
                }
            }
            if (item_action == W8_CAMP_ITEM_ACTION_DROP &&
                (g_status.party.held.item.iItemNo != -1)) {
                DropHeldCampItem();
            } else if (item_action == W8_CAMP_ITEM_ACTION_MOVE &&
                       (g_status.party.held.item.iItemNo != -1)) {
                SetHandCursors();
            }
            if (changed) {
                cancel_queued_swap();
                RebuildCampItemList();
            }
        }
    }
    if (!changed && !partially_merged && !merged) {
        if (same_kind) {
            screen.redraw_flags |= W8_CAMP_REDRAW_ITEM_ACTIONS;
        }
        return;
    }
    screen.redraw_flags |= W8_CAMP_REDRAW_ITEM_ACTIONS;
    if (((GetInputState().modifiers & CTRL_DOWN) != 0)) {
        screen.redraw_flags |= W8_CAMP_REDRAW_ALL;
    } else {
        if (origin == W8ItemStorage::Backpack) {
            if (partially_merged || merged) {
                item_redraw_flags |= W8_CAMP_ITEM_REDRAW_BACKPACK;
            } else {
                item_redraw_flags |= W8_CAMP_ITEM_REDRAW_BACKPACK_CELL_FIRST << slot_index;
            }
        } else if (origin == W8ItemStorage::Equipment) {
            if (partially_merged || merged) {
                item_redraw_flags |= W8_CAMP_ITEM_REDRAW_EQUIPMENT_CELL_FIRST << slot_index;
            } else {
                screen.redraw_flags |= W8_CAMP_REDRAW_CHARACTER_INFO;
                screen.redraw_flags |= W8_CAMP_REDRAW_PORTRAIT;
            }
        } else if (origin == W8ItemStorage::Pool) {
            item_redraw_flags |= W8_CAMP_ITEM_REDRAW_POOL;
        }
        item_redraw_flags |= W8_CAMP_ITEM_REDRAW_EQUIPMENT_CELLS;
    }
    screen.redraw_flags |= W8_CAMP_REDRAW_CHARACTER_INFO;
    screen.redraw_flags |= W8_CAMP_REDRAW_PORTRAIT;
}

void W8CampItemsPage::TakeItemUnitToHand(W8ItemLocation source)
{
    const auto* item = g_status.party.itemAt(source);
    if (!item) return;
    if (item_action != W8_CAMP_ITEM_ACTION_NONE || !CanSplitItemStack(item)) return;
    if (!SplitItemStack(source, 1, screen.ReviewedPartySlot())) return;
    RebuildCampItemList();
    screen.redraw_flags |= W8_CAMP_REDRAW_ALL;
}

/* Commits the pending companion swap left by MarkCampCharacterPending:
   selects the new character and either queues its join event or opens the
   refusal dialog. Returns zero when a swap was pending and consumed, one when
   the caller may proceed. force ignores the same-character early out. */
bool W8CampItemsPage::ResolvePendingCampCharacter(bool force)
{
    if (g_status.party.companion_inventory &&
        ((&g_status.party.characters[*g_status.party.companion_inventory]) != screen.reviewed ||
         force)) {
        SelectPendingCampCharacter();
        return false;
    }
    return true;
}

/* When the reviewed slot is one of the two companion slots and its bound NPC
   wants the item being used, the current character is remembered as pending a
   swap (cleared first so a failed attempt leaves none). */
void W8CampItemsPage::MarkCampCharacterPending(W8ItemInstance* item)
{
    W8NpcState* npc;

    g_status.party.companion_inventory.reset();
    if (item->iItemNo != -1 &&
        (screen.ReviewedPartySlot() == 0 || screen.ReviewedPartySlot() == 1)) {
        npc = GetNpcState(g_status.party.rows[screen.ReviewedPartySlot()].npc_index);
        if (npc != 0 && NpcWantsItem(npc, item)) {
            g_status.party.companion_inventory = screen.ReviewedPartySlot();
        }
    }
}

/* Refresh the enabled state of the eight item-action controls. Every button
   is enabled only while the items page is up and the party exists; the
   per-button cases then gate on the held item, combat, the reviewed slot's
   eligibility and the relevant item/spell tests. When invalidate is set the
   whole panel is additionally marked dirty before it is redrawn. */
// FUNCTION: WIZ8 0x005b5670
void W8CampItemsPage::RefreshCampItemActions(bool invalidate)
{
    int index;
    W8SpellRuntimeRecord* spell;
    W8ItemInstance* held = &g_status.party.held.item;

    for (index = 0; index < W8_CAMP_ACTION_BUTTON_COUNT; ++index) {
        auto& control = screen.camp_buttons.actions[index];
        if (screen.page == W8_CAMP_PAGE_ITEMS && g_status.game_started) {
            switch (index) {
            case W8_CAMP_ACTION_BUTTON_IDENTIFY:
                control->SetEnabled(IsPartySlotEligible(screen.ReviewedPartySlot()));
                continue;
            case W8_CAMP_ACTION_BUTTON_MOVE:
                control->SetEnabled(!g_combat.active() &&
                                    IsPartySlotEligible(screen.ReviewedPartySlot()));
                continue;
            case W8_CAMP_ACTION_BUTTON_SPLIT_STACK:
                if (!(g_status.party.held.item.iItemNo != -1)) {
                    control->SetEnabled(true);
                    continue;
                }
                if (!CanSplitItemStack(held)) {
                    control->SetEnabled(false);
                    continue;
                }
                if (!g_combat.active() ||
                    (g_combat.state()->equip_phase != W8CombatEquipPhase::action_selection &&
                     g_status.party.rows[screen.ReviewedPartySlot()].pending_action ==
                         W8_ACTION_EQUIP)) {
                    control->SetEnabled(true);
                    continue;
                }
                if (IsEquippableItemClass(held) &&
                    g_status.party.held.actor == screen.ReviewedPartySlot()) {
                    control->SetEnabled(true);
                    continue;
                }
                break;
            case W8_CAMP_ACTION_BUTTON_USE:
                if (g_combat.active()) {
                    control->SetEnabled(false);
                    continue;
                }
                if (!IsPartySlotEligible(screen.ReviewedPartySlot())) {
                    control->SetEnabled(false);
                    continue;
                }
                if (gXStatus.fCampMode || LockInteractionSuspended() ||
                    TrapInteractionSuspended()) {
                    control->SetEnabled(false);
                    continue;
                }
                if (!(g_status.party.held.item.iItemNo != -1)) {
                    control->SetEnabled(true);
                    continue;
                }
                control->SetEnabled(CanCharacterUseItemEntry(screen.reviewed, held) != 0);
                continue;
            case W8_CAMP_ACTION_BUTTON_DROP:
                control->SetEnabled(true);
                continue;
            case W8_CAMP_ACTION_BUTTON_CAST_SPELL:
                control->SetEnabled(
                    IsPartySlotEligible(screen.ReviewedPartySlot()) &&
                    CharacterHasTrait(screen.reviewed, W8_TRAIT_REMOVE_CURSED_ITEMS));
                continue;
            case W8_CAMP_ACTION_BUTTON_USE_ON_ITEM:
                if (screen.reviewed->spell_learned[0x17] != 1) {
                    break;
                }
                if (!IsPartySlotEligible(screen.ReviewedPartySlot()) || g_combat.active()) {
                    break;
                }
                spell = &g_spell_records[W8_SPELL_IDENTIFY_ITEM];
                if (spell->spell_point_cost > screen.reviewed->iSPLeft[spell->realm]) {
                    break;
                }
                if (!SpellUsableNow(W8_SPELL_IDENTIFY_ITEM, false)) {
                    break;
                }
                if (!(g_status.party.held.item.iItemNo != -1)) {
                    control->SetEnabled(true);
                    continue;
                }
                control->SetEnabled(CanItemLeaveItsSlot(held));
                continue;
            case W8_CAMP_ACTION_BUTTON_USE_ON_CHARACTER:
                if (screen.reviewed->spell_learned[0x3a] != 1) {
                    break;
                }
                if (!IsPartySlotEligible(screen.ReviewedPartySlot()) || g_combat.active()) {
                    break;
                }
                spell = &g_spell_records[W8_SPELL_REMOVE_CURSE];
                if (spell->spell_point_cost > screen.reviewed->iSPLeft[spell->realm]) {
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
    screen.camp_buttons.dirty |= invalidate;
    for (const auto& button : screen.camp_buttons.pages)
        screen.camp_buttons.dirty |= button->m_dirty;
    for (const auto& button : screen.camp_buttons.actions)
        screen.camp_buttons.dirty |= button->m_dirty;
    if (screen.camp_buttons.dirty) {
        auto& target = GetFrameBufferSurface();
        CatalogArtwork::Load(0x110, 0).drawAndInvalidate(target, 0, 0, 0x1c2);
        for (const auto& button : screen.camp_buttons.pages) {
            button->m_dirty = true;
            button->Refresh();
        }
        for (const auto& button : screen.camp_buttons.actions) {
            button->m_dirty = true;
            button->Refresh();
        }
        screen.camp_buttons.dirty = false;
    }
}

/* Switch the items-page action mode: every action button's secondary state is
   cleared, the mode maps onto the targeting mode and the button it highlights,
   and mode zero also drops a held item cursor back into the pool. */
// FUNCTION: WIZ8 0x005b59b0
void W8CampItemsPage::SetCampItemActionMode(W8CampItemAction mode)
{
    W8CampItemActionButton selected = W8_CAMP_ACTION_BUTTON_NONE;
    W8TargetNeed targeting;
    for (const auto& control : screen.camp_buttons.actions) control->SetPressed(false);
    item_action = mode;
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
    if (mode == W8_CAMP_ITEM_ACTION_NONE && (g_status.party.held.item.iItemNo != -1)) {
        SetItemCursor(0);
    }
    if (selected != W8_CAMP_ACTION_BUTTON_NONE &&
        screen.camp_buttons.actions[selected]->IsPressed() == 0) {
        screen.camp_buttons.actions[selected]->SetPressed(true);
    }
    screen.redraw_flags |= W8_CAMP_REDRAW_ITEM_ACTIONS;
}

void W8CampItemsPage::SelectCampItemAction(W8CampItemActionButton button)
{
    auto& control = *screen.camp_buttons.actions[button];
    if (button == W8_CAMP_ACTION_BUTTON_CAST_SPELL &&
        !IsCampActionAllowed(screen.ReviewedPartySlot())) {
        control.SetPressed(false);
        control.m_dirty = true;
        return;
    }
    if (!control.IsPressed()) {
        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
        return;
    }
    switch (button) {
    case W8_CAMP_ACTION_BUTTON_IDENTIFY:
        pending_item_action.identifying_character = screen.reviewed;
        if ((g_status.party.held.item.iItemNo != -1))
            return IdentifyAndOpenItemInfo(&g_status.party.held.item);
        return SetCampItemActionMode(W8_CAMP_ITEM_ACTION_IDENTIFY);
    case W8_CAMP_ACTION_BUTTON_MOVE:
        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_MOVE);
        if ((g_status.party.held.item.iItemNo != -1)) {
            SetHandCursors();
            item_redraw_flags |= W8_CAMP_ITEM_REDRAW_EQUIPMENT;
        }
        return;
    case W8_CAMP_ACTION_BUTTON_SPLIT_STACK:
        if ((g_status.party.held.item.iItemNo != -1))
            return OpenSplitStackDialog({W8ItemStorage::Hand});
        return SetCampItemActionMode(W8_CAMP_ITEM_ACTION_SPLIT_STACK);
    case W8_CAMP_ACTION_BUTTON_USE:
        if (g_status.party.held.item.iItemNo != -1) return UseCampItem({W8ItemStorage::Hand});
        return SetCampItemActionMode(W8_CAMP_ITEM_ACTION_USE);
    case W8_CAMP_ACTION_BUTTON_DROP:
        if ((g_status.party.held.item.iItemNo != -1)) return DropHeldCampItem();
        return SetCampItemActionMode(W8_CAMP_ITEM_ACTION_DROP);
    case W8_CAMP_ACTION_BUTTON_CAST_SPELL:
        return SetCampItemActionMode(W8_CAMP_ITEM_ACTION_CAST_SPELL);
    case W8_CAMP_ACTION_BUTTON_USE_ON_ITEM:
        pending_item_action.caster_slot = screen.ReviewedPartySlot();
        if ((g_status.party.held.item.iItemNo != -1)) {
            UseHeldItemOnItem(&g_status.party.held.item);
            control.SetPressed(false);
            control.m_dirty = true;
            return;
        }
        return SetCampItemActionMode(W8_CAMP_ITEM_ACTION_USE_ON_ITEM);
    case W8_CAMP_ACTION_BUTTON_USE_ON_CHARACTER:
        pending_item_action.caster_slot = screen.ReviewedPartySlot();
        return SetCampItemActionMode(W8_CAMP_ITEM_ACTION_USE_ON_CHARACTER);
    default:
        return;
    }
}

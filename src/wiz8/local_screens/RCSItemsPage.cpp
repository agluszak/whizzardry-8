#include "wiz8/local_screens/RCSItemsPage.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <functional>
#include <memory>
#include <optional>
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
#include "wiz8/combat/combat.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/dialog_code/AssayDialog.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/dialog_code/DialogFactoryDialogs.h"
#include "wiz8/dialog_code/StatInfoDialogs.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/live_party.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/GameplayMods.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/RCSCommon.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/platform/input.h"
#include "wiz8/platform/timer.h"
#include "wiz8/pointer.h"
#include "wiz8/render/frame.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/character_derivation.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/party_stats.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/ui_layer.h"
#include "wiz8/utility.h"

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
    case W8_CAMP_INFO_PAGE_CHARACTER:
        item_controls.tabs[0]->SetPressed(false);
        item_controls.tabs[1]->SetPressed(true);
        item_controls.help->setEnabled(false);
        character_info.SetActive(true);
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
        character_info.SetActive(false);
        for (index = 0; index < 7; ++index) {
            item_controls.stats[index]->setEnabled(true);
        }
        for (index = 0; index < 4; ++index) {
            item_controls.info[index]->setEnabled(true);
        }
        break;
    }
    info_page = mode;
    character_info_dirty = true;
}

// FUNCTION: WIZ8 0x005BA110
void W8CampItemsPage::OpenItemInfoDialog(W8ItemInstance* item)
{
    const bool owned =
        IsItemWornByCharacter(character, item) || IsItemCarriedByCharacter(character, item);
    auto dialog = std::make_unique<W8AssayDialog>(*item, owned ? character : nullptr,
                                                  SDL_Point{kInfoDialogX, kInfoDialogY});
    dialog->SetText("");

    dialogs.Display(std::move(dialog));
}

// FUNCTION: WIZ8 0x005BA2B0
void W8CampItemsPage::OpenAttributeInfoDialog(W8Attribute attribute)
{
    dialogs.Display(std::make_unique<W8AttributeInfoDialog>(attribute));
}

// FUNCTION: WIZ8 0x005BA310
void W8CampItemsPage::OpenSecondaryAttributeInfoDialog(unsigned int uiIndex)
{
    dialogs.Display(std::make_unique<W8SecondaryAttributeInfoDialog>(uiIndex));
}

// FUNCTION: WIZ8 0x005BA370
void W8CampItemsPage::IdentifyAndOpenItemInfo(W8ItemInstance* item)
{
    if (CanItemLeaveItsSlot(item)) {
        if (PartyAttemptsToIdentifyItem(item, 0) && unidentified_only) {
            RebuildCampItemList();
            camp_repaint.all = true;
        }
    }
    OpenItemInfoDialog(item);
    SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
}

// FUNCTION: WIZ8 0x005BA3D0
void W8CampItemsPage::DropHeldCampItem(void)
{
    if (ResolvePendingCampCharacter(true)) {
        if (auto dropped = DropItemInHand(CurrentGame(), 0); !dropped)
            ShowRefusal(dropped.error());
        else
            SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
    }
}

// FUNCTION: WIZ8 0x005BA400
void W8CampItemsPage::OpenSplitStackDialog(W8ItemLocation location)
{
    auto* item = CurrentGame().party.itemAt(location);
    if (item->iItemNo != -1 && item->stack_count > 1 &&
        (Data().items[item->iItemNo].flags & W8_ITEM_FLAG_NO_DISCARD) == 0 &&
        Data().items[item->iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK) {
        auto dialog = std::make_unique<W8SplitItemDialog>(W8_ITEM_SPLIT_INVENTORY, item, -1,
                                                          SDL_Point{kSplitDialogX, kSplitDialogY});
        dialog->SetText("");

        dialog->on_complete = [this, source = location,
                               actor = static_cast<unsigned>(PartySlot())](int count) {
            if (count <= 0) return;
            if (!SplitItemStack(CurrentGame(), source, static_cast<unsigned>(count), actor)) {
                dialogs.ShowNotice(Data().strings[0x915], 0, true, false);
                return;
            }
            RebuildCampItemList();
            SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
            camp_repaint.all = true;
        };
        dialogs.Display(std::move(dialog));
        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
    }
}

// FUNCTION: WIZ8 0x005BA4F0
void W8CampItemsPage::UseCampItem(W8ItemLocation location)
{
    auto* item = CurrentGame().party.itemAt(location);
    if (ValidateItemSpellUse(PartySlot(), item, 0)) {
        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
        return;
    }
    if (CanCastFromItem(character, item)) {
        LearnSpellFromItem(character, location);
    } else {
        if (!IsUsableItemClass(item) || IsSpecialItemId(item)) {
            if (auto* main_game = MainGame()) {
                main_game->item_drag =
                    MainGameScreen::ItemDrag{.item = location, .actor = PartySlot()};
                CurrentGame().aging_held = true;
            }
        } else {
            if (!(CurrentGame().party.held.item.iItemNo != -1)) {
                MarkCampCharacterPending(item);
                ExchangeInventoryItems(
                    CurrentGame(), location, {W8ItemStorage::Hand},
                    static_cast<unsigned>(CurrentGame().status.selected_character));
            }
        }
        leave_camp();
    }
    RebuildCampItemList();
    camp_repaint.all = true;
    SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
}

// FUNCTION: WIZ8 0x005BA5D0
void W8CampItemsPage::MergeItemStacksWithHeld(W8ItemLocation location)
{
    if (CurrentGame().party.itemAt(location)->iItemNo != -1) {
        const auto merged = MergeItems(CurrentGame(), PartySlot(), location);
        if (!merged)
            ShowRefusal(merged.error());
        else if (*merged) {
            RebuildCampItemList();
            camp_repaint.all = true;
            SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
        }
    }
}

// FUNCTION: WIZ8 0x005BA620
void W8CampItemsPage::ReportCastResult(int party_slot)
{
    std::string text;
    int result;

    StartBreathCycle(PartySlot(), false);
    result = RevealCharacterItemBindingsByProfession(CurrentGame().party, PartySlot(), party_slot);
    const W8Character& target = CurrentGame().party.characters[party_slot];
    if (result == 0) {
        wiz8::sound::play("Data\\Sound\\Misc\\Spell Fizzle 01.wav");
        text = fmt::sprintf(Data().strings[0x1be], target.name, 0, 1, 0);
        dialogs.ShowNotice(text, 0, true, false);
    } else if (result == 1) {
        wiz8::sound::play("Data\\Sound\\Misc\\GeneralMagic.wav");
        text = fmt::sprintf(Data().strings[0x1bd], target.name, 0, 1, 0);
        dialogs.ShowNotice(text, 0, true, false);
    } else if (result == 2) {
        wiz8::sound::play("Data\\Sound\\Misc\\GeneralMagic.wav");
        text = fmt::sprintf(Data().strings[0x1bc], target.name, 0, 1, 0);
        dialogs.ShowNotice(text, 0, true, false);
    }
    camp_repaint.all = true;
    SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
}

// FUNCTION: WIZ8 0x005BA740
void W8CampItemsPage::UseHeldItemOnItem(W8ItemLocation location)
{
    W8CombatSlot target;
    W8ItemInstance* item = CurrentGame().party.itemAt(location);

    assert(pending_item_action.caster_slot >= 0);
    assert(pending_item_action.caster_slot <= 7);
    assert(CurrentGame().party.members[pending_item_action.caster_slot].has_value());
    if (item->iItemNo != -1) {
        if (CanItemLeaveItsSlot(item)) {
            ResetCombatSlot(&target);
            target.iType = W8_TARGET_KIND_ITEM;
            target.item = location;
            if (CommitPartySlotSpell(pending_item_action.caster_slot, W8_SPELL_IDENTIFY_ITEM, 8,
                                     &target) == 1) {
                OpenItemInfoDialog(item);
                if (!item->identified) {
                    QueueCharacterEvent(CurrentGame().party.characters +
                                            pending_item_action.caster_slot,
                                        0x35, 0, kCharacterEventFullVolume);
                }
            }
            RebuildCampItemList();
            camp_repaint.all = true;
            SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
            pending_item_action.caster_slot = -1;
            return;
        }
        QueueCharacterEvent(CurrentGame().party.characters + pending_item_action.caster_slot, 0x35,
                            0, kCharacterEventFullVolume);
    }
}

// FUNCTION: WIZ8 0x005BA8E0
void W8CampItemsPage::TargetCharacterWithHeldItem(unsigned int uiTargetChar)
{
    W8CombatSlot target;

    assert(uiTargetChar <= 7);
    assert(CurrentGame().party.members[uiTargetChar].has_value());
    assert(pending_item_action.caster_slot >= 0);
    assert(pending_item_action.caster_slot <= 7);
    assert(CurrentGame().party.members[pending_item_action.caster_slot].has_value());
    ResetCombatSlot(&target);
    target.iType = W8_TARGET_KIND_CHARACTER;
    target.iChar = uiTargetChar;
    CommitPartySlotSpell(pending_item_action.caster_slot, W8_SPELL_REMOVE_CURSE, 8, &target);
    RebuildCampItemList();
    camp_repaint.all = true;
    SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
    pending_item_action.caster_slot = -1;
}

bool CanCharacterUseItemEntry(W8Character* character, W8ItemInstance* item)
{
    if (CanCharacterActivateItem(character, item) || CanCastFromItem(character, item) ||
        IsUsableItemClass(item)) {
        return true;
    }
    return false;
}

bool CanSplitItemStack(const W8ItemInstance* item)
{
    const W8ItemDatabaseRecord* record = &Data().items[item->iItemNo];
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
    switch (pointer.targeting()) {
    case W8_TARGET_NEED_ALLY:
        if ((item_action == W8_CAMP_ITEM_ACTION_CAST_SPELL ||
             item_action == W8_CAMP_ITEM_ACTION_USE_ON_CHARACTER) &&
            CurrentGame().party.members[slot].has_value() &&
            CurrentGame().party.characters[slot].uiCondition[W8_CONDITION_MISSING] == 0) {
            pointer.showSlot(pointer.targetingSlot(alternate));
            return;
        }
        if (CanPartySlotParticipate(slot)) {
            pointer.showSlot(pointer.targetingSlot(alternate));
        }
        return;
    case W8_TARGET_NEED_ITEM:
        assert(item != 0);
        if (item->iItemNo == -1) {
            return;
        }
        if ((item_action == W8_CAMP_ITEM_ACTION_IDENTIFY_SPELL ||
             item_action == W8_CAMP_ITEM_ACTION_USE_ON_ITEM) &&
            CanItemLeaveItsSlot(item)) {
            pointer.showSlot(pointer.targetingSlot(alternate));
            return;
        }
        if (item_action == W8_CAMP_ITEM_ACTION_IDENTIFY ||
            item_action == W8_CAMP_ITEM_ACTION_DROP) {
            pointer.showSlot(pointer.targetingSlot(alternate));
            return;
        }
        if (item_action == W8_CAMP_ITEM_ACTION_SPLIT_STACK) {
            const W8ItemDatabaseRecord* record = &Data().items[item->iItemNo];
            if ((record->flags & W8_ITEM_FLAG_NO_DISCARD) != 0 ||
                record->quantity_kind != W8_ITEM_QUANTITY_STACK) {
                return;
            }
            if (CurrentGame().combat.has_value() && !IsEquippableItemClass(item) &&
                (CurrentGame().combat->equip_phase == W8CombatEquipPhase::action_selection ||
                 CurrentGame().party.rows[PartySlot()].executing.kind != W8_ACTION_EQUIP)) {
                return;
            }
            pointer.showSlot(pointer.targetingSlot(alternate));
            return;
        }
        if (item_action == W8_CAMP_ITEM_ACTION_USE) {
            if (CanCharacterActivateItem(character, item) || CanCastFromItem(character, item) ||
                IsUsableItemClass(item)) {
                pointer.showSlot(pointer.targetingSlot(alternate));
            }
            return;
        }
        if (item_action != W8_CAMP_ITEM_ACTION_MOVE || item->iItemNo == -1) {
            return;
        }
        if (!alternate) {
            if (!(CurrentGame().party.held.item.iItemNo != -1)) {
                pointer.showSlot(3);
                return;
            }
            pointer.showHeldItem(0xe);
            return;
        }
        if (!(CurrentGame().party.held.item.iItemNo != -1)) {
            pointer.showSlot(4);
            return;
        }
        pointer.showHeldItem(0xf);
        return;
    case W8_TARGET_NEED_CHARACTER_INDIRECT:
        if (IsDeadCharacterTargetable(slot)) {
            pointer.showSlot(pointer.targetingSlot(alternate));
        }
        return;
    default:
        return;
    }
}

// FUNCTION: WIZ8 0x005BAFC0
void W8CampItemsPage::ShowHandCursor()
{
    if ((CurrentGame().party.held.item.iItemNo != -1)) {
        pointer.showHeldItem(0xe);
        return;
    }
    pointer.showSlot(3);
}

/* Unequips both weapon slots of the selected character; refused outright in
   combat and while the party is moving, and per-slot when a bound item will
   not come off. */
// FUNCTION: WIZ8 0x005BB010
void W8CampItemsPage::UnequipBothHands(void)
{
    if (CurrentGame().combat.has_value() && !CurrentGame().combat->round_active &&
        !CurrentGame().party_movement_mode &&
        CurrentGame().combat->equip_phase == W8CombatEquipPhase::action_selection) {
        dialogs.ShowNotice(Data().strings[0x903], 0, true, false);
        return;
    }
    if (!IsPartySlotEligible(PartySlot())) {
        dialogs.ShowNotice(Data().strings[0x917], 0, true, false);
        return;
    }
    CurrentGame().storage.bind(*character, W8_EQUIP_SLOT_PRIMARY_WEAPON);
    CurrentGame().storage.bind(*character, W8_EQUIP_SLOT_SECONDARY_WEAPON);
    if (CanUnequipSlotItem(Data(), character, W8_EQUIP_SLOT_PRIMARY_WEAPON) &&
        CanUnequipSlotItem(Data(), character, W8_EQUIP_SLOT_SECONDARY_WEAPON)) {
        SwapWeaponSetSlots(CurrentGame(), PartySlot(), false);
        cell_redraw.InvalidateEquipment();
        camp_repaint.portrait = true;
        character_info_dirty = true;
        return;
    }
    dialogs.ShowNotice(Data().strings[0x916], 0, true, false);
}

// FUNCTION: WIZ8 0x005BB140
void W8CampItemsPage::TogglePartyRowFlag(void)
{
    if (item_controls.actions[1]->IsPressed()) {
        CurrentGame().party.rows[PartySlot()].remembered.weapon_sets_locked = true;
        item_controls.actions[0]->SetEnabled(false);
        item_controls.actions[0]->m_dirty = true;
        return;
    }
    CurrentGame().party.rows[PartySlot()].remembered.weapon_sets_locked = false;
    item_controls.actions[0]->SetEnabled(true);
    item_controls.actions[0]->m_dirty = true;
}

// FUNCTION: WIZ8 0x005BB220
void W8CampItemsPage::SortCampItemPool(void)
{
    CurrentGame().storage.sortPool();
    item_scroll = 0;
    RebuildCampItemList();
    cell_redraw.InvalidatePool();
}

/* UI button order differs from the item-filter order. */
// FUNCTION: WIZ8 0x005BB250
void W8CampItemsPage::ToggleCampItemFilter(W8CampItemFilterButton tab)
{
    const bool pressed = item_controls.filters[tab]->IsPressed();
    if (tab == W8_CAMP_ITEM_FILTER_BUTTON_USABLE) {
        usable_only = pressed;
    } else if (tab == W8_CAMP_ITEM_FILTER_BUTTON_UNIDENTIFIED) {
        unidentified_only = pressed;
    } else {
        group_filter = pressed ? std::optional{tab} : std::nullopt;
        for (int index = 0; index < 4; ++index) {
            item_controls.filters[index]->SetPressed(group_filter == index);
        }
    }
    item_scroll = 0;
    RebuildCampItemList();
    if (((GetInputState().modifiers & CTRL_DOWN) == 0)) {
        cell_redraw.InvalidatePool();
        return;
    }
    camp_repaint.all = true;
}

/* Region reason values are bit codes set by the region manager: 0x8 left
   release, 0x10 left down, 0x40 right press, 0x80 right release, 0x100
   double-click/activate, 0x400 enter/leave transition, 0x800 mouse wheel. */
/* Descriptive name for the right-click identify/show operation shared by
   backpack, equipment and party-pool slots. */
void W8CampItemsPage::ShowCampItemInfo(W8ItemInstance* item)
{
    pending_item_action.identifying_character = character;
    if (CanItemLeaveItsSlot(item) && PartyAttemptsToIdentifyItem(item, 0) && unidentified_only) {
        RebuildCampItemList();
        camp_repaint.all = true;
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
        return {storage, static_cast<unsigned int>(PartySlot()), cell};
    const auto visible = item_scroll + cell;
    const auto index = visible < item_list.size() &&
                               CurrentGame().party.item_pool[item_list[visible]].iItemNo != -1
                           ? item_list[visible]
                           : CurrentGame().party.item_count;
    return {W8ItemStorage::Pool, 0, static_cast<unsigned int>(index)};
}

std::string W8CampItemsPage::CampItemTooltip(int slot, W8ItemStorage storage)
{
    const auto* item = CurrentGame().party.itemAt(CampItemAt(slot, storage));
    if (!item) return {};
    std::string text =
        item->iItemNo == -1 ? std::string{} : FormatItemDisplayName(Data(), item, false);
    if (storage == W8ItemStorage::Equipment) {
        const auto& label = Data().strings[g_equip_slot_label_ids[slot]];
        return text.empty() ? std::string(label) : fmt::format("{} ({})", text, label);
    }
    if (item->iItemNo != -1) {
        const auto& record = Data().items[item->iItemNo];
        if (record.category == W8_ITEM_CATEGORY_SPELL_SOURCE && record.spell_id != W8_SPELL_NONE &&
            character->spell_learned[record.spell_id] && (item->identified || item->spell_hint))
            text += fmt::format(" ({})", Data().strings[0x930]);
    }
    return text;
}

UiResult W8CampItemsPage::HandleCampItem(const UiPointerEvent& event, int slot,
                                         W8ItemStorage storage)
{
    const auto location = CampItemAt(slot, storage);
    auto* item = CurrentGame().party.itemAt(location);
    // A full pool has no free row to show past the end of its list.
    if (!item) return UiResult::Handled;
    const bool equipped = storage == W8ItemStorage::Equipment;
    const bool pool = storage == W8ItemStorage::Pool;
    auto& target = *(pool ? pool_targets[slot] : inventory_targets[slot + (equipped ? 8 : 0)]);
    const bool interactive =
        !(item->iItemNo == -1 &&
          (!(CurrentGame().party.held.item.iItemNo != -1) ||
           (equipped &&
            (item_action == W8_CAMP_ITEM_ACTION_MOVE ||
             !CanEquipItemInSlot(Data(), character, CurrentGame().party.held.item.iItemNo,
                                 static_cast<unsigned char>(slot), true) ||
             !CanCharacterUseItem(Data(), character, CurrentGame().party.held.item.iItemNo)))));
    if (interactive && (event.action != UiPointerAction::Up || event.captured_buttons))
        PlayButtonSound(event.action);
    if (event.action == UiPointerAction::Enter || event.action == UiPointerAction::Leave) {
        if (item->iItemNo != -1 || (CurrentGame().party.held.item.iItemNo != -1)) {
            if (pool)
                cell_redraw.pool.set(slot);
            else if (equipped)
                cell_redraw.equipment.set(slot);
            else
                cell_redraw.backpack.set(slot);
        }
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

    for (index = 0; index < 8; ++index) {
        item = &character->backpack[index];
        if (item->iItemNo != -1) {
            DrawCampItemLabel(item, (index & 1) * 0x31 + 0xd,
                              (index >> 1) * 0x39 + 0xc2 + (index & 1) * 0x10, 0);
        }
    }
    index = 0;
    while (true) {
        count = static_cast<int>(item_list.size()) - static_cast<int>(item_scroll);
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
        DrawCampItemLabel(&CurrentGame().party.item_pool[item_list[item_scroll + index]],
                          (index & 1) * 0x31 + 0x22c, y, 1);
        ++index;
    }
    region = g_camp_screen_regions;
    for (index = 0; index < 12; ++index) {
        item = &character->EquippedItem[index];
        if (item->iItemNo != -1) {
            DrawCampItemLabel(item, region->label_x, region->label_y,
                              static_cast<signed char>(region->label_flag));
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
void W8CampItemsPage::DrawCampItemLabel(W8ItemInstance* item, int left, int top, signed char flag)
{
    std::string label;
    std::string name;
    W8ControlsRect bounds;
    int width;
    int height;

    name = FormatItemDisplayName(Data(), item, false);
    label = name;
    bounds.left = left;
    bounds.top = top;
    bounds.right = left + 0xfa;
    bounds.bottom = top + 0xfa;
    W8TextBuffer text(&bounds, label, g_font10arial,
                      g_W8TextBufferAlignTop | g_W8TextBufferAlignLeft, 4);
    width = text.m_maxLineWidth + 4;
    height = g_font10arial.height() * text.m_lineCount + 2;
    if (flag != 0) {
        bounds.right -= width;
        bounds.left -= width;
        text.SetClipBounds(bounds);
        text.SetLayoutBounds(&bounds, true);
    }
    SDL_Surface& framebuffer = GetFrameBufferSurface();
    const SDL_Rect fill{bounds.left, bounds.top, width, height};
    SDL_FillSurfaceRect(&framebuffer, &fill, 0x8000);
    if (framebuffer.pixels != nullptr) {
        text.RenderText(framebuffer, 2, 1, true);
        const Uint16 outline = static_cast<Uint16>(-0x6613);
        const SDL_Rect top_edge{bounds.left, bounds.top, width + 1, 1};
        const SDL_Rect right_edge{bounds.left + width, bounds.top, 1, height + 1};
        const SDL_Rect bottom_edge{bounds.left, bounds.top + height, width + 1, 1};
        const SDL_Rect left_edge{bounds.left, bounds.top, 1, height + 1};
        SDL_FillSurfaceRect(&framebuffer, &top_edge, outline);
        SDL_FillSurfaceRect(&framebuffer, &right_edge, outline);
        SDL_FillSurfaceRect(&framebuffer, &bottom_edge, outline);
        SDL_FillSurfaceRect(&framebuffer, &left_edge, outline);
    }
}

void W8CampItemsPage::CreateCampItemControls(UiLayer& scroll_input)
{
    item_range = std::make_unique<W8RangeControl>(scroll_input, 0x263, 0xc1, 0x275, 0x1a1);
    item_range->on_change = [this](auto* range) {
        item_scroll = range->m_value << 1;
        if (((GetInputState().modifiers & CTRL_DOWN) != 0))
            camp_repaint.all = true;
        else
            cell_redraw.InvalidatePool();
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
        if (help >= 0) target.setTooltip([help] { return std::string(Data().strings[help]); });
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
    if (GameStarted()) {
        item_controls.actions[1]->SetEnabled(true);
        if (CurrentGame().party.rows[PartySlot()].remembered.weapon_sets_locked) {
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

    for (index = 0; index < 7; ++index) {
        item_controls.filters[index]->SetVisible(true);
    }
    if (!GameStarted()) {
        for (index = 0; index < 7; ++index) {
            item_controls.filters[index]->SetEnabled(false);
        }
        return;
    }
    for (index = 0; index < 7; ++index) {
        item_controls.filters[index]->SetEnabled(true);
        if (index < 4) {
            item_controls.filters[index]->SetPressed(group_filter == index);
        } else if (index == W8_CAMP_ITEM_FILTER_BUTTON_USABLE) {
            item_controls.filters[index]->SetPressed(usable_only);
        } else if (index == W8_CAMP_ITEM_FILTER_BUTTON_UNIDENTIFIED) {
            item_controls.filters[index]->SetPressed(unidentified_only);
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
    group_filter.reset();
    usable_only = unidentified_only = false;
    item_timer_active = false;
    item_timer_expired = false;
    info_page = W8_CAMP_INFO_PAGE_ITEMS;
    item_icons_drawn = false;
}

void W8CampItemsPage::Activate(W8Character& character)
{
    this->character = &character;
    inventory_input.setEnabled(true);
    EnableCampActionButtons();
    UpdateCampItemFilters();
    EnableCampSecondaryPanel();
    SetCampInfoPageMode(info_page);
    item_range->SetActive(true);
    if (GameStarted()) {
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
    character_info.SetActive(false);
    item_range->SetActive(false);
    if (GameStarted()) pool_input.setEnabled(false);
}

void W8CampItemsPage::ReviewedCharacterChanged(W8Character& character)
{
    this->character = &character;
    EnableCampActionButtons();
    if (usable_only) {
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
        cell_redraw.InvalidateEquipment();
    }
}

bool W8CampItemsPage::RedrawPending() const
{
    return cell_redraw.BackpackPending() || cell_redraw.EquipmentPending() ||
           cell_redraw.PoolPending() || character_info_dirty || action_buttons_invalidated ||
           hand_buttons_dirty || filter_buttons_dirty;
}

void W8CampItemsPage::EndRedraw()
{
    cell_redraw = {};
    character_info_dirty = false;
    action_buttons_invalidated = false;
    hand_buttons_dirty = false;
    filter_buttons_dirty = false;
}

void W8CampItemsPage::BeginRedraw()
{
    InvalidateChangedCells();
    // Icons drawn while Ctrl was held are cleared by a full repaint once it is released.
    if (((GetInputState().modifiers & CTRL_DOWN) == 0) && item_icons_drawn) {
        camp_repaint.all = true;
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
void W8CampItemsPage::RedrawCampItemsPage(bool full)
{
    g_wiz_text_font_secondary.setShade(Font::kNormalShade);
    if (full || character_info_dirty) {
        DrawCampCharacterInfo();
    }
    if (cell_redraw.BackpackPending()) {
        DrawCampBackpackItems();
    }
    if (cell_redraw.EquipmentPending()) {
        DrawCampEquipmentItems();
    }
    if (cell_redraw.PoolPending()) {
        DrawCampItemPool();
        item_range->Redraw(GetFrameBufferSurface(), true);
    } else {
        item_range->Redraw(GetFrameBufferSurface(), false);
    }
    RefreshCampActionPanel(full || hand_buttons_dirty);
    RefreshItemsTabPanel(full || filter_buttons_dirty);
}

/* The character block of the items page: name-free value fields, the
   experience bar, the six realm icons with their point pools, the attribute
   rows and the armor summaries. */
void W8CampItemsPage::DrawCampCharacterInfo(void)
{
    std::string text;
    int index;
    int realm;
    int top;
    unsigned int filled;
    SDL_Rect clip;

    if (info_page != W8_CAMP_INFO_PAGE_ITEMS) {
        character_info.InvalidateAll();
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
    DrawRcsText(Data().strings[0x91b], 0x159, 10, 0xce,
                g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    DrawRcsText(Data().strings[0x91c], 0x15e, 0x18, 0x65,
                g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
    DrawRcsText(Data().strings[0x91d], 0x15e, 0x26, 0x65,
                g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
    text = FormatUnsignedIntegerWithCommas(character->experience);
    DrawRcsText(text, 0x1c0, 0x18, 0x67, g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    text = FormatUnsignedIntegerWithCommas(character->experience_goal);
    DrawRcsText(text, 0x1c0, 0x26, 0x67, g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    DrawRcsText(Data().strings[0x91e], 0x144, 0x3a, 0x45,
                g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
    DrawRcsText(Data().strings[0x91f], 0x144, 0x48, 0x45,
                g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
    DrawRcsText(Data().strings[0x920], 0x144, 0x56, 0x45,
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
    text =
        fmt::format("{}: {:d}, {}: {:d}", Data().strings[0x8c0], character->inventory_weight / 10,
                    Data().strings[0x8c1], character->party_weight_share / 10);
    item_controls.help_text = text;
    g_wiz_text_font_secondary.resetPalette();
    DrawRcsText(Data().strings[0x921], 0x144, 0x72, 0x75,
                g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    top = 0x3a;
    for (index = 0; index < 7; ++index) {
        DrawRcsText(Data().strings[g_attribute_label_ids[index]], 0x1c2, top, 0x4c,
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
    DrawRcsText(Data().strings[0x922], 0x144, 0x80, 0x45,
                g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
    text = fmt::format("{:d}", character->armor_class_total);
    DrawRcsText(text, 0x18d, 0x80, 0x2c, g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    DrawRcsText(Data().strings[0x923], 0x144, 0x8e, 0x45,
                g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
    text = fmt::format("{:d}", character->armor_class_average);
    DrawRcsText(text, 0x18d, 0x8e, 0x2c, g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    item_controls.dirty = true;
}

/* The eight backpack cells under the character block, and the header strip
   and caption above them. */
void W8CampItemsPage::DrawCampBackpackItems(void)
{
    unsigned int slot;
    int left;
    int top;
    int right;
    int bottom;
    int item_id;

    if (cell_redraw.backpack_header) {
        CatalogArtwork::Load(0x114, 0).drawAndInvalidate(GetFrameBufferSurface(), 1, 0, 0xa5);
        DrawRcsBoldText(Data().strings[0x92b], 0xc, 0xae, 0x5d,
                        g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    }
    for (slot = 0; slot < 8; ++slot) {
        left = (slot & 1) * 0x31 + 0xb;
        top = (slot >> 1) * 0x39 + 0xc0;
        right = left + 0x2e;
        bottom = top + 0x36;
        if (!cell_redraw.backpack[slot]) {
            continue;
        }
        InvalidateRegion(left, top, right, bottom);
        item_id = character->backpack[slot].iItemNo;
        if (item_id == -1 || CanCharacterUseItem(Data(), character, item_id)) {
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
        if (item_id != -1 && Data().items[item_id].binds_on_equip != 0 &&
            character->backpack[slot].bound) {
            CatalogArtwork::Load(0x115, 0).draw(GetFrameBufferSurface(), 0x32, left, top);
        }
        if (inventory_targets[slot]->hovered() &&
            (item_id != -1 || ((CurrentGame().party.held.item.iItemNo != -1) &&
                               item_action != W8_CAMP_ITEM_ACTION_MOVE))) {
            CatalogArtwork::Load(0x115, 0).draw(GetFrameBufferSurface(), 0x30, left, top);
        }
    }
}

/* The twelve equipment cells, and the paper doll backdrop and race/gender
   portrait behind them. */
void W8CampItemsPage::DrawCampEquipmentItems(void)
{
    std::string text;
    const W8CampScreenRegion* region;
    unsigned int slot;
    int item_id;
    int frame;

    if (cell_redraw.paper_doll) {
        CatalogArtwork::Load(0x114, 0).drawAndInvalidate(GetFrameBufferSurface(), 2, 0x71, 0xa5);
        CatalogArtwork::Load(
            g_race_portrait_images[character->iRace * W8_RACE_GNOME + character->gender], 0)
            .drawAndInvalidate(GetFrameBufferSurface(), 0, 0xc2, 0xa5);
        hand_buttons_dirty = true;
    }
    for (slot = 0; slot < 12; ++slot) {
        if (!cell_redraw.equipment[slot]) {
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
                if (paired != -1 && (Data().items[paired].flags & W8_ITEM_FLAG_TWO_HANDED) != 0) {
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
        if (item_id != -1 && Data().items[item_id].binds_on_equip != 0 &&
            character->EquippedItem[slot].bound) {
            CatalogArtwork::Load(0x115, 0).drawAndInvalidate(
                GetFrameBufferSurface(), region->frame + 2, region->x - 2, region->y - 2);
        }
        if (inventory_targets[kCampEquipmentRegionOffset + slot]->hovered()) {
            if (!(CurrentGame().party.held.item.iItemNo != -1)) {
                if (character->EquippedItem[slot].iItemNo == -1) {
                    continue;
                }
            } else if (item_action == W8_CAMP_ITEM_ACTION_MOVE ||
                       !CanEquipItemInSlot(Data(), character, CurrentGame().party.held.item.iItemNo,
                                           static_cast<unsigned char>(slot), true) ||
                       !CanCharacterUseItem(Data(), character,
                                            CurrentGame().party.held.item.iItemNo)) {
                if (!(CurrentGame().party.held.item.iItemNo != -1) ||
                    item_action != W8_CAMP_ITEM_ACTION_MOVE) {
                    continue;
                }
                if (character->EquippedItem[slot].iItemNo == -1) {
                    continue;
                }
            }
            frame = region->frame;
        } else {
            if (!(CurrentGame().party.held.item.iItemNo != -1) ||
                item_action == W8_CAMP_ITEM_ACTION_MOVE || !IsPartySlotEligible(PartySlot()) ||
                !CanEquipItemInSlot(Data(), character, CurrentGame().party.held.item.iItemNo,
                                    static_cast<unsigned char>(slot), true) ||
                !CanCharacterUseItem(Data(), character, CurrentGame().party.held.item.iItemNo)) {
                continue;
            }
            frame = region->frame + 1;
        }
        CatalogArtwork::Load(0x115, 0).drawAndInvalidate(GetFrameBufferSurface(), frame,
                                                         region->x - 2, region->y - 2);
    }
}

/* The shared party item pool: the bottom-right grid of up to eight visible
   cells, plus the header strip and the gold counter. */
void W8CampItemsPage::DrawCampItemPool(void)
{
    std::string text;
    unsigned int visible;
    unsigned int i;
    int left;
    int top;
    int right;
    int bottom;
    W8ItemInstance* item;

    if (cell_redraw.pool_header) {
        CatalogArtwork::Load(0x114, 0).drawAndInvalidate(GetFrameBufferSurface(), 3, 0x1de, 0xa5);
        CatalogArtwork::Load(0x118, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0x202, 0x1a6);
        filter_buttons_dirty = true;
    }
    if (!GameStarted()) {
        return;
    }
    if (cell_redraw.pool_header) {
        DrawRcsBoldText(Data().strings[0x92c], 0x1e6, 0xae, 0x8d,
                        g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
        text = FormatUnsignedIntegerWithCommas(CurrentGame().party.gold);
        DrawRcsText(text, 0x229, 0x1a9, 0x30,
                    g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    }
    for (i = 0;; ++i) {
        visible = static_cast<int>(item_list.size()) - static_cast<int>(item_scroll);
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
        item = &CurrentGame().party.item_pool[item_list[item_scroll + i]];
        if (!cell_redraw.pool[i]) {
            continue;
        }
        InvalidateRegion(left, top, right, bottom);
        if (!CanCharacterUseItem(Data(), character, item->iItemNo)) {
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
        if (Data().items[item->iItemNo].binds_on_equip != 0 && item->bound) {
            CatalogArtwork::Load(0x115, 0).draw(GetFrameBufferSurface(), 0x32, left, top);
        }
        if (pool_targets[i]->hovered()) {
            CatalogArtwork::Load(0x115, 0).draw(GetFrameBufferSurface(), 0x30, left, top);
        }
    }
    for (i = static_cast<int>(item_list.size()) - static_cast<int>(item_scroll); i < 8; ++i) {
        left = (i & 1) * 0x31 + 0x200;
        top = (i >> 1) * 0x39 + 0xc0;
        right = left + 0x2e;
        bottom = top + 0x36;
        if (cell_redraw.pool[i]) {
            InvalidateRegion(left, top, right, bottom);
            CatalogArtwork::Load(0x1b6, 0).drawRect(
                GetFrameBufferSurface(), SDL_Rect{0, 0, (right) - (left), (bottom) - (top)}, left,
                top);
            if (pool_targets[i]->hovered() && (CurrentGame().party.held.item.iItemNo != -1) &&
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

    switch (Data().items[item->iItemNo].quantity_kind) {
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

/* Invalidate the cells whose items differ from what they last showed, and
   remember what they show now. */
void W8CampItemsPage::InvalidateChangedCells()
{
    auto& shown = shown_inventory;
    const bool same_character = shown.character == character;
    for (unsigned int slot = 0; slot < 8; ++slot)
        if (!same_character || shown.backpack[slot] != character->backpack[slot])
            cell_redraw.backpack.set(slot);
    for (unsigned int slot = 0; slot < 12; ++slot)
        if (!same_character || shown.equipment[slot] != character->EquippedItem[slot])
            cell_redraw.equipment.set(slot);
    const auto pool =
        std::span{CurrentGame().party.item_pool}.first(CurrentGame().party.item_count);
    if (!std::ranges::equal(shown.pool, pool)) cell_redraw.InvalidatePool();
    shown.character = character;
    std::ranges::copy(character->backpack, shown.backpack.begin());
    std::ranges::copy(character->EquippedItem, shown.equipment.begin());
    shown.pool.assign(pool.begin(), pool.end());
}

/* The group filter is exclusive; the two independent restrictions compose with it. */
void W8CampItemsPage::RebuildCampItemList()
{
    constexpr std::array groups{W8_ITEM_EQUIP_GROUP_HAND, W8_ITEM_EQUIP_GROUP_BODY,
                                W8_ITEM_EQUIP_GROUP_OTHER, W8_ITEM_EQUIP_GROUP_ACCESSORY};
    item_list.clear();
    for (unsigned int index = 0; index < CurrentGame().party.item_count; ++index) {
        const auto& item = CurrentGame().party.item_pool[index];
        if (item.iItemNo != -1 &&
            (!usable_only || CanCharacterUseItem(Data(), character, item.iItemNo)) &&
            (!unidentified_only || !item.identified) &&
            (!group_filter || GetItemEquipSlotGroup(item.iItemNo) == groups[*group_filter])) {
            item_list.push_back(index);
        }
    }
    const int rows = static_cast<int>(item_list.size()) - 8;
    item_range->SetEnabled(rows > 0);
    if (rows > 0) {
        item_range->SetRange(0, (rows + 1) / 2);
    }
    if (item_list.empty()) {
        item_scroll = 0;
    } else if (item_list.size() <= item_scroll) {
        item_scroll = static_cast<unsigned int>((item_list.size() - 1) / 8 * 8);
    }
    item_range->SetValue((item_scroll + 1) / 2);
}

/* The shared click handler for the camp item regions: a backpack slot arrives
   with origin 0, a worn slot with origin 1 and a party-pool row with origin
   2. The screen's item_action picks the interpretation - plain handling,
   identify, stack split, use, use held item on item - and the held item can
   merge onto the clicked stack, swap with it, or drop into the pool. */
void W8CampItemsPage::SelectPendingCampCharacter()
{
    const int slot = *CurrentGame().party.companion_inventory;
    select_character(slot);
    if (IsPartySlotEligible(slot)) {
        QueueCharacterEvent(&CurrentGame().party.characters[slot], 109, 0,
                            kCharacterEventFullVolume);
    } else {
        std::string text =
            fmt::sprintf(Data().strings[0x931], CurrentGame().party.characters[slot].name);
        dialogs.ShowNotice(text, 0, true, false);
    }
}

void W8CampItemsPage::HandleCampItemClick(W8ItemLocation location)
{
    W8ItemInstance* item = CurrentGame().party.itemAt(location);
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
    std::string text;

    assert(item != 0);

    /* A held stackable whose name kind the clicked item merges with counts as
       the same item for the use-merge path below. */
    if ((CurrentGame().party.held.item.iItemNo != -1) && item->iItemNo != -1 &&
        GetItemMergeKind(Data(), item->iItemNo, &related_kind) &&
        Data().items[CurrentGame().party.held.item.iItemNo].unidentified_name_index ==
            related_kind) {
        same_kind = true;
    }
    if (item->iItemNo == -1 && !(CurrentGame().party.held.item.iItemNo != -1)) {
        return;
    }
    if (origin == W8ItemStorage::Equipment && (CurrentGame().party.held.item.iItemNo != -1) &&
        item_action != W8_CAMP_ITEM_ACTION_MOVE) {
        if (!same_kind &&
            !CanEquipItemInSlot(Data(), character, CurrentGame().party.held.item.iItemNo,
                                slot_index, true)) {
            return;
        }
        if (!CanCharacterUseItem(Data(), character, CurrentGame().party.held.item.iItemNo)) {
            return;
        }
    }
    if (item_action == W8_CAMP_ITEM_ACTION_CAST_SPELL ||
        item_action == W8_CAMP_ITEM_ACTION_USE_ON_CHARACTER) {
        return;
    }
    if (!GameStarted()) {
        text = Data().strings[0x900];
        dialogs.ShowNotice(text, 0, true, false);
        return;
    }
    if (character->uiCondition[W8_CONDITION_MISSING] != 0 &&
        (origin == W8ItemStorage::Equipment || origin == W8ItemStorage::Backpack)) {
        text = Data().strings[0x907];
        dialogs.ShowNotice(text, 0, true, false);
        return;
    }
    if (character->uiCondition[W8_CONDITION_WEBBED] != 0 &&
        (origin == W8ItemStorage::Equipment || origin == W8ItemStorage::Backpack)) {
        text = Data().strings[0x908];
        dialogs.ShowNotice(text, 0, true, false);
        return;
    }
    if (character->uiCondition[W8_CONDITION_TURNCOAT] != 0 &&
        (origin == W8ItemStorage::Equipment || origin == W8ItemStorage::Backpack)) {
        text = Data().strings[0x909];
        dialogs.ShowNotice(text, 0, true, false);
        return;
    }
    if (character->highest_condition >= W8_CONDITION_TURNCOAT && origin != W8ItemStorage::Pool &&
        (CurrentGame().party.held.item.iItemNo != -1) &&
        CurrentGame().party.held.actor != PartySlot()) {
        text = Data().strings[0x901];
        dialogs.ShowNotice(text, 0, true, false);
        return;
    }

    /* In combat an item click spends the character's action allowance unless
       it only touches a weapon-class slot or folds into the same-kind merge.
       A weapon or shield being moved onto a slot it cannot pair with stays
       gated as well. */
    bool gated = true;
    if (CurrentGame().combat.has_value() && origin != W8ItemStorage::Pool) {
        if (!(CurrentGame().party.held.item.iItemNo != -1) ||
            ((Data().items[CurrentGame().party.held.item.iItemNo].equip_class ==
                  W8_ITEM_EQUIP_CLASS_THROWN_WEAPON ||
              Data().items[CurrentGame().party.held.item.iItemNo].equip_class ==
                  W8_ITEM_EQUIP_CLASS_AMMUNITION) &&
             CurrentGame().party.held.actor == PartySlot() &&
             (origin != W8ItemStorage::Equipment ||
              HeldItemFitsPairedSlot(PartySlot(), equip_slot)))) {
            if (item->iItemNo == -1 ||
                Data().items[item->iItemNo].equip_class == W8_ITEM_EQUIP_CLASS_THROWN_WEAPON ||
                Data().items[item->iItemNo].equip_class == W8_ITEM_EQUIP_CLASS_AMMUNITION ||
                same_kind) {
                gated = false;
            }
        }
    }
    if (gated && !IsCampActionAllowed(PartySlot())) {
        return;
    }
    if (((GetInputState().modifiers & SHIFT_DOWN) != 0)) {
        TakeItemUnitToHand(location);
        return;
    }
    if (item_action == W8_CAMP_ITEM_ACTION_IDENTIFY_SPELL) {
        party_slot = CharacterPointerToPartySlot(pending_item_action.identifying_character);
        if (!CanItemLeaveItsSlot(item)) {
            QueueCharacterEvent(&CurrentGame().party.characters[party_slot], 0x35, 0,
                                kCharacterEventFullVolume);
            return;
        }
        assert(pending_item_action.identifying_character != 0);
        old_pool_count = CurrentGame().party.item_count;
        reidentify = false;
        ResetCombatSlot(&target);
        target.iType = W8_TARGET_KIND_ITEM;
        target.item = location;
        StartBreathCycle(party_slot, false);
        W8PartySlotRow* row = &CurrentGame().party.rows[party_slot];
        if (row->executing.kind == W8_ACTION_USE_ITEM &&
            Data().items
                    [CurrentGame().party.itemAt(row->executing.detail_block.item_use.item)->iItemNo]
                        .spell_id == W8_SPELL_IDENTIFY_ITEM) {
            reidentify = true;
            result =
                CommitPartySlotItemUse(party_slot, row->executing.detail_block.item_use, &target);
        } else {
            result = CommitPartySlotSpell(party_slot, W8_SPELL_IDENTIFY_ITEM, 8, &target);
        }
        if (origin == W8ItemStorage::Pool && reidentify &&
            old_pool_count != CurrentGame().party.item_count) {
            item =
                &CurrentGame()
                     .party.item_pool[CurrentGame().party.item_count - old_pool_count + slot_index];
            RebuildCampItemList();

            character_info_dirty = true;
            cell_redraw.InvalidatePool();
        }
        if (result == 1 && item->iItemNo != -1) {
            OpenItemInfoDialog(item);
            if (!item->identified) {
                QueueCharacterEvent(&CurrentGame().party.characters[party_slot], 0x35, 0,
                                    kCharacterEventFullVolume);
            }
        }
        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
        return;
    }
    if (item_action == W8_CAMP_ITEM_ACTION_USE_ON_ITEM) {
        UseHeldItemOnItem(location);
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
        if (CanCharacterUseItemEntry(character, item) != 0) {
            UseCampItem(location);
        }
        return;
    } else if (item_action == W8_CAMP_ITEM_ACTION_MOVE) {
        if ((CurrentGame().party.held.item.iItemNo != -1)) {
            if (item->iItemNo == -1) {
                SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
                return;
            }
            if (CurrentGame().party.companion_inventory) {
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
        if (!CanUnequipSlotItem(Data(), character, equip_slot)) {
            text = Data().strings[0x90b];
            dialogs.ShowNotice(text, 0, true, false);
            if (item->bound) {
                return;
            }
            CurrentGame().storage.bind(*character, equip_slot);
            return;
        }
        item->bound = true;
    }

    // Changing a weapon hand here supersedes a weapon-set swap queued for the round.
    const auto cancel_queued_swap = [&] {
        if (origin == W8ItemStorage::Equipment &&
            GetPairedEquipSlot(equip_slot) != W8_EQUIP_SLOT_NONE)
            CurrentGame().party.rows[PartySlot()].weapon_swap_pending = false;
    };
    if (same_kind) {
        MergeItemUses(CurrentGame(), PartySlot(), location, {W8ItemStorage::Hand});
        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
        changed = true;
        cancel_queued_swap();
        RebuildCampItemList();
    } else {
        /* Fold the held stack onto a matching stack; for the pool the scan
           continues across every row until the held stack is consumed. */
        if ((CurrentGame().party.held.item.iItemNo != -1) &&
            Data().items[CurrentGame().party.held.item.iItemNo].quantity_kind ==
                W8_ITEM_QUANTITY_STACK) {
            if (item->iItemNo != -1 && item->iItemNo == CurrentGame().party.held.item.iItemNo) {
                const auto outcome =
                    MergeInventoryStacks(CurrentGame(), location, {W8ItemStorage::Hand});
                merged = outcome.complete;
                partially_merged = outcome.changed && !outcome.complete;
                merge_tried = true;
            }
            if (!merged && origin == W8ItemStorage::Pool) {
                for (index = 0; index < CurrentGame().party.item_count; ++index) {
                    const auto outcome = MergeInventoryStacks(
                        CurrentGame(), {W8ItemStorage::Pool, 0, index}, {W8ItemStorage::Hand});
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
                if ((CurrentGame().party.held.item.iItemNo != -1)) {
                    if (CurrentGame().party.companion_inventory) {
                        SelectPendingCampCharacter();
                    } else if (InsertHeldItemInPool(CurrentGame(), slot_index)) {
                        changed = true;
                        RebuildCampItemList();
                    } else {
                        text = Data().strings[0x90c];
                        dialogs.ShowNotice(text, 0, true, false);
                    }
                } else if (slot_index < CurrentGame().party.item_count) {
                    /* An empty hand picks the clicked pool row up. */
                    ExchangeInventoryItems(
                        CurrentGame(), location, {W8ItemStorage::Hand},
                        static_cast<unsigned>(CurrentGame().status.selected_character));
                    changed = true;
                    if (item_action == W8_CAMP_ITEM_ACTION_DROP) {
                        DropHeldCampItem();
                        RebuildCampItemList();
                    } else if (item_action == W8_CAMP_ITEM_ACTION_MOVE) {
                        ShowHandCursor();
                        RebuildCampItemList();
                    } else {
                        RebuildCampItemList();
                    }
                }
            }
        } else {
            if (!merge_tried &&
                ((!(CurrentGame().party.held.item.iItemNo != -1) && item->iItemNo != -1) ||
                 ((CurrentGame().party.held.item.iItemNo != -1) && !merged))) {
                if (!CurrentGame().party.companion_inventory ||
                    (&CurrentGame().party.characters[*CurrentGame().party.companion_inventory]) ==
                        character) {
                    MarkCampCharacterPending(item);
                    if ((CurrentGame().party.held.item.iItemNo != -1)) {
                        choose_character = CurrentGame().party.held.actor == -1;
                        DeliverExceptionalItemReaction(CurrentGame(),
                                                       &CurrentGame().party.held.item,
                                                       choose_character, character);
                    }
                    if (origin == W8ItemStorage::Equipment &&
                        (CurrentGame().party.held.item.iItemNo != -1) &&
                        !HeldItemFitsPairedSlot(PartySlot(), equip_slot)) {
                        paired_slot = GetPairedEquipSlot(equip_slot);
                        paired = &character->EquippedItem[paired_slot];
                        if (!CanUnequipSlotItem(Data(), character, paired_slot)) {
                            text = Data().strings[0x916];
                            dialogs.ShowNotice(text, 0, true, false);
                            if (paired->bound) {
                                return;
                            }
                            CurrentGame().storage.bind(*character, paired_slot);
                            return;
                        }
                        if (item->iItemNo == -1) {
                            MarkCampCharacterPending(paired);
                        }
                        if (!EquipHeldItemAndStowPartner(CurrentGame(), PartySlot(), equip_slot)) {
                            dialogs.ShowNotice(Data().strings[0x90c], 0, true, false);
                            return;
                        }
                        changed = true;
                    } else {
                        ExchangeInventoryItems(CurrentGame(), location,
                                               W8ItemLocation{W8ItemStorage::Hand},
                                               static_cast<unsigned>(PartySlot()));
                        changed = true;
                        if (origin == W8ItemStorage::Backpack &&
                            character->backpack[slot_index].iItemNo != -1 &&
                            CurrentGame().rng.below(100) < 5) {
                            QueueCharacterEvent(character, g_special_event14, 0,
                                                kCharacterEventFullVolume);
                        }
                    }
                } else {
                    SelectPendingCampCharacter();
                }
            }
            if (item_action == W8_CAMP_ITEM_ACTION_DROP &&
                (CurrentGame().party.held.item.iItemNo != -1)) {
                DropHeldCampItem();
            } else if (item_action == W8_CAMP_ITEM_ACTION_MOVE &&
                       (CurrentGame().party.held.item.iItemNo != -1)) {
                ShowHandCursor();
            }
            if (changed) {
                cancel_queued_swap();
                RebuildCampItemList();
            }
        }
    }
    if (!changed && !partially_merged && !merged) {
        if (same_kind) {
            action_buttons_invalidated = true;
        }
        return;
    }
    action_buttons_invalidated = true;
    if (((GetInputState().modifiers & CTRL_DOWN) != 0)) {
        camp_repaint.all = true;
    } else {
        if (origin == W8ItemStorage::Backpack) {
            if (partially_merged || merged) {
                cell_redraw.InvalidateBackpack();
            } else {
                cell_redraw.backpack.set(slot_index);
            }
        } else if (origin == W8ItemStorage::Equipment) {
            if (partially_merged || merged) {
                cell_redraw.equipment.set(slot_index);
            } else {
                character_info_dirty = true;
                camp_repaint.portrait = true;
            }
        } else if (origin == W8ItemStorage::Pool) {
            cell_redraw.InvalidatePool();
        }
        cell_redraw.equipment.set();
    }
    character_info_dirty = true;
    camp_repaint.portrait = true;
}

void W8CampItemsPage::TakeItemUnitToHand(W8ItemLocation source)
{
    const auto* item = CurrentGame().party.itemAt(source);
    if (!item) return;
    if (item_action != W8_CAMP_ITEM_ACTION_NONE || !CanSplitItemStack(item)) return;
    if (!SplitItemStack(CurrentGame(), source, 1, PartySlot())) return;
    RebuildCampItemList();
    camp_repaint.all = true;
}

/* Commits the pending companion swap left by MarkCampCharacterPending:
   selects the new character and either queues its join event or opens the
   refusal dialog. Returns zero when a swap was pending and consumed, one when
   the caller may proceed. force ignores the same-character early out. */
bool W8CampItemsPage::ResolvePendingCampCharacter(bool force)
{
    if (CurrentGame().party.companion_inventory &&
        ((&CurrentGame().party.characters[*CurrentGame().party.companion_inventory]) != character ||
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

    CurrentGame().party.companion_inventory.reset();
    if (item->iItemNo != -1 && (PartySlot() == 0 || PartySlot() == 1)) {
        npc = GetNpcState(CurrentGame().party.npcIndex(PartySlot()));
        if (npc != 0 && NpcWantsItem(npc, item)) {
            CurrentGame().party.companion_inventory = PartySlot();
        }
    }
}

void W8CampItemsPage::CreateActionButtons()
{
    constexpr int frames[] = {0, 10, 5, 15, 20, 25, 30, 35};
    constexpr int x[] = {0, 44, 88, 132, 176, 308, 220, 264};
    for (int index = 0; index < W8_CAMP_ACTION_BUTTON_COUNT; ++index) {
        const int frame = frames[index];
        auto& button = action_buttons[index];
        button = std::make_unique<W8ButtonWidget>(action_input);
        button->Configure(ButtonGraphics::Load(CatalogArtwork::Load(0x112, 0).spriteSheet(),
                                               frame + 3, frame, frame + 1, frame + 2, frame + 4),
                          {.on_click =
                               [this, index](auto*) {
                                   AfterUiDispatch([this, index] {
                                       SelectCampItemAction(
                                           static_cast<W8CampItemActionButton>(index));
                                   });
                               },
                           .policy = {.primary_toggle = ButtonToggle::Release,
                                      .input_rule = ButtonInputRule::PrimaryPressedState},
                           .tooltip = 2369 + index,
                           .priority = UI_PRIORITY_LOWEST,
                           .menu_artwork = true});
        button->SetPosition(x[index], 0x1c2);
        button->Resize(44, 30);
    }
}

/* Refresh the enabled state of the eight item-action controls. Every button
   is enabled only while the items page is shown and the party exists; the
   per-button cases then gate on the held item, combat, the reviewed slot's
   eligibility and the relevant item/spell tests. */
void W8CampItemsPage::UpdateActionButtons(bool shown)
{
    int index;
    const W8SpellRuntimeRecord* spell;
    W8ItemInstance* held = &CurrentGame().party.held.item;

    for (index = 0; index < W8_CAMP_ACTION_BUTTON_COUNT; ++index) {
        auto& control = action_buttons[index];
        if (shown && GameStarted()) {
            switch (index) {
            case W8_CAMP_ACTION_BUTTON_IDENTIFY:
                control->SetEnabled(IsPartySlotEligible(PartySlot()));
                continue;
            case W8_CAMP_ACTION_BUTTON_MOVE:
                control->SetEnabled(!CurrentGame().combat.has_value() &&
                                    IsPartySlotEligible(PartySlot()));
                continue;
            case W8_CAMP_ACTION_BUTTON_SPLIT_STACK:
                if (!(CurrentGame().party.held.item.iItemNo != -1)) {
                    control->SetEnabled(true);
                    continue;
                }
                if (!CanSplitItemStack(held)) {
                    control->SetEnabled(false);
                    continue;
                }
                if (!CurrentGame().combat.has_value() ||
                    (CurrentGame().combat->equip_phase != W8CombatEquipPhase::action_selection &&
                     CurrentGame().party.rows[PartySlot()].executing.kind == W8_ACTION_EQUIP)) {
                    control->SetEnabled(true);
                    continue;
                }
                if (IsEquippableItemClass(held) && CurrentGame().party.held.actor == PartySlot()) {
                    control->SetEnabled(true);
                    continue;
                }
                break;
            case W8_CAMP_ACTION_BUTTON_USE:
                if (CurrentGame().combat.has_value()) {
                    control->SetEnabled(false);
                    continue;
                }
                if (!IsPartySlotEligible(PartySlot())) {
                    control->SetEnabled(false);
                    continue;
                }
                if (CurrentGame().activity.conversation_aside || LockInteractionSuspended() ||
                    TrapInteractionSuspended()) {
                    control->SetEnabled(false);
                    continue;
                }
                if (!(CurrentGame().party.held.item.iItemNo != -1)) {
                    control->SetEnabled(true);
                    continue;
                }
                control->SetEnabled(CanCharacterUseItemEntry(character, held) != 0);
                continue;
            case W8_CAMP_ACTION_BUTTON_DROP:
                control->SetEnabled(true);
                continue;
            case W8_CAMP_ACTION_BUTTON_CAST_SPELL:
                control->SetEnabled(IsPartySlotEligible(PartySlot()) &&
                                    CharacterHasTrait(character, W8_TRAIT_REMOVE_CURSED_ITEMS));
                continue;
            case W8_CAMP_ACTION_BUTTON_USE_ON_ITEM:
                if (character->spell_learned[0x17] != 1) {
                    break;
                }
                if (!IsPartySlotEligible(PartySlot()) || CurrentGame().combat.has_value()) {
                    break;
                }
                spell = &Data().spells[W8_SPELL_IDENTIFY_ITEM];
                if (spell->spell_point_cost > character->iSPLeft[spell->realm]) {
                    break;
                }
                if (!SpellUsableNow(W8_SPELL_IDENTIFY_ITEM, false)) {
                    break;
                }
                if (!(CurrentGame().party.held.item.iItemNo != -1)) {
                    control->SetEnabled(true);
                    continue;
                }
                control->SetEnabled(CanItemLeaveItsSlot(held));
                continue;
            case W8_CAMP_ACTION_BUTTON_USE_ON_CHARACTER:
                if (character->spell_learned[0x3a] != 1) {
                    break;
                }
                if (!IsPartySlotEligible(PartySlot()) || CurrentGame().combat.has_value()) {
                    break;
                }
                spell = &Data().spells[W8_SPELL_REMOVE_CURSE];
                if (spell->spell_point_cost > character->iSPLeft[spell->realm]) {
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
}

bool W8CampItemsPage::ActionButtonsDirty() const
{
    return std::ranges::any_of(action_buttons, [](const auto& button) { return button->m_dirty; });
}

void W8CampItemsPage::RefreshActionButtons()
{
    for (const auto& button : action_buttons) {
        button->m_dirty = true;
        button->Refresh();
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
    for (const auto& control : action_buttons) control->SetPressed(false);
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
    if (selected != W8_CAMP_ACTION_BUTTON_NONE && action_buttons[selected]->IsPressed() == 0) {
        action_buttons[selected]->SetPressed(true);
    }
    action_buttons_invalidated = true;
}

void W8CampItemsPage::SelectCampItemAction(W8CampItemActionButton button)
{
    auto& control = *action_buttons[button];
    if (button == W8_CAMP_ACTION_BUTTON_CAST_SPELL && !IsCampActionAllowed(PartySlot())) {
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
        pending_item_action.identifying_character = character;
        if ((CurrentGame().party.held.item.iItemNo != -1))
            return IdentifyAndOpenItemInfo(&CurrentGame().party.held.item);
        return SetCampItemActionMode(W8_CAMP_ITEM_ACTION_IDENTIFY);
    case W8_CAMP_ACTION_BUTTON_MOVE:
        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_MOVE);
        if ((CurrentGame().party.held.item.iItemNo != -1)) {
            ShowHandCursor();
            cell_redraw.InvalidateEquipment();
        }
        return;
    case W8_CAMP_ACTION_BUTTON_SPLIT_STACK:
        if ((CurrentGame().party.held.item.iItemNo != -1))
            return OpenSplitStackDialog({W8ItemStorage::Hand});
        return SetCampItemActionMode(W8_CAMP_ITEM_ACTION_SPLIT_STACK);
    case W8_CAMP_ACTION_BUTTON_USE:
        if (CurrentGame().party.held.item.iItemNo != -1) return UseCampItem({W8ItemStorage::Hand});
        return SetCampItemActionMode(W8_CAMP_ITEM_ACTION_USE);
    case W8_CAMP_ACTION_BUTTON_DROP:
        if ((CurrentGame().party.held.item.iItemNo != -1)) return DropHeldCampItem();
        return SetCampItemActionMode(W8_CAMP_ITEM_ACTION_DROP);
    case W8_CAMP_ACTION_BUTTON_CAST_SPELL:
        return SetCampItemActionMode(W8_CAMP_ITEM_ACTION_CAST_SPELL);
    case W8_CAMP_ACTION_BUTTON_USE_ON_ITEM:
        pending_item_action.caster_slot = PartySlot();
        if ((CurrentGame().party.held.item.iItemNo != -1)) {
            UseHeldItemOnItem({W8ItemStorage::Hand});
            control.SetPressed(false);
            control.m_dirty = true;
            return;
        }
        return SetCampItemActionMode(W8_CAMP_ITEM_ACTION_USE_ON_ITEM);
    case W8_CAMP_ACTION_BUTTON_USE_ON_CHARACTER:
        pending_item_action.caster_slot = PartySlot();
        return SetCampItemActionMode(W8_CAMP_ITEM_ACTION_USE_ON_CHARACTER);
    default:
        return;
    }
}

#include "wiz8/local_screens/MGSUseItemSelect.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <fmt/format.h>

#include "layouts/main_game_screen.h"
#include "local_code/TextBuffer.h"
#include "wiz8/button_interaction.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/fight.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/records.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/dialog_code/AssayDialog.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_specific_code/Trynnie2.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/MGSFormation.h"
#include "wiz8/local_screens/MGSPortraitCombat.h"
#include "wiz8/local_screens/MGSSpellCasting.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/pointer.h"
#include "wiz8/render/frame.h"
#include "wiz8/rules/action_rules.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/screen.h"
#include "wiz8/script_context.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"

struct W8NpcState;

W8UseItemView* UseItemPicker()
{
    auto* main_game = MainGame();
    return main_game ? main_game->item_picker.get() : nullptr;
}

W8UseItemView::W8UseItemView(MainGameScreen& owner_screen, int party_slot,
                             std::move_only_function<void()> on_return)
    : on_return(std::move(on_return)), screen(owner_screen), owner(party_slot)
{
    const auto make_button = [this](int artwork, int normal, int pressed, int hover,
                                    int pressed_hover, int disabled, SDL_Rect bounds, int help,
                                    std::function<void()> action) {
        auto button = std::make_unique<W8ButtonWidget>(input);
        button->Configure(ButtonGraphics::Load(CatalogArtwork::Load(artwork, 0).spriteSheet(),
                                               disabled, normal, hover, pressed, pressed_hover),
                          {.on_click = [action](auto*) { AfterUiDispatch(action); },
                           .policy = {.input_rule = ButtonInputRule::PrimaryPressedState},
                           .tooltip = help,
                           .priority = UI_PRIORITY_LOWEST,
                           .menu_artwork = true});

        button->SetPosition(bounds.x, bounds.y);
        button->Resize(bounds.w, bounds.h);
        return button;
    };
    for (int index = 0; index < 2; ++index) {
        const int base = index * 5;
        pools[index] =
            make_button(0x98, base, base + 2, base + 1, base + 4, base + 3,
                        {0x17 + (index ? 0x48 : 0x24), 0x183, 32, 32}, 96 + index,
                        [this, index] { SelectPool(index ? Pool::Party : Pool::Character); });
        pools[index]->Policy().primary_toggle = ButtonToggle::Release;
    }
    filter = make_button(0x1ab, 10, 11, 12, 14, 13, {0x234, 0x16c, 14, 14}, 99,
                         [this] { filter->SetPressed(true); });
    filter->Policy().primary_toggle = ButtonToggle::Release;
    filter->SetEnabled(false);
    close = make_button(0x8e, 4, -1, 5, 6, 7, {0x248, 0x16c, 27, 27}, 17,
                        [this] { screen.closeItemPicker(); });
    caption.SetLayoutBounds(0x1b, 0x16a, 0xa0, 0x179);
    caption.SetLayoutMode(g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    caption.SetText(Data().strings[0x77a], g_wiz_text_font_secondary);
    quantity.SetLayoutBounds(0x1e1, 0x16b, 0x20d, 0x19f);
    quantity.SetLayoutMode(g_W8TextBufferAlignBottom | g_W8TextBufferAlignRight);
    auto& item = input.add(SDL_Rect{0x1e1, 0x16b, 45, 53}, [this](const UiPointerEvent& event) {
        if (event.action != UiPointerAction::Up || event.captured_buttons)
            PlayButtonSound(event.action);
        const auto mask =
            event.button == UiPointerButton::Right ? SDL_BUTTON_RMASK : SDL_BUTTON_LMASK;
        if (event.action == UiPointerAction::Up && event.inside && (event.captured_buttons & mask))
            AfterUiDispatch([this] {
                if (detail_location && item_image) OpenAssayDialog(*detail_location);
            });
        return UiResult::Handled;
    });
    item.setTooltip([] { return Data().strings[98]; });
}

bool MainGameScreen::openItemPicker(int party_slot, std::move_only_function<void()> on_return)
{
    if (!IsPartySlotEligible(party_slot)) return false;
    // A new detour keeps its already-suspended parent; replacement abandons the old one.
    if (spell_picker || item_picker || lock_active || trap_active) dismissOverlays(false);
    if (FormationPanelOpen()) CloseFormationPanel();
    item_picker = std::make_unique<W8UseItemView>(*this, party_slot, std::move(on_return));
    if (submenu) {
        closeSubMenu();
    }
    CloseNpcDialogueIfActive();
    refreshLayout();
    SelectTextBox(2);
    ResetEditorStatusLine(-1);
    hud->text_box_visible = false;
    hud->repaint.layout = true;
    hud->repaint.roof_and_spell_icons = true;
    hud->repaint.submenu_buttons = true;
    item_picker->RefreshForSlot(party_slot);
    if (item_picker) SelectSpellCastingPartySlot(party_slot, item_picker->draft);
    return item_picker != nullptr;
}

/* Discard the picker and restore the HUD it took over. */
void MainGameScreen::dismissItemPicker()
{
    if (!item_picker) return;
    const UiTransaction transaction;
    OpenModal(nullptr);
    text_input->setScrollEnabled(false);
    text_input->setEnabled(false);
    SetTargetingMode(W8_TARGET_NEED_NONE);
    ResetEditorStatusLine(-1);
    hud->text_box_visible = true;
    SelectTextBox(game.combat.has_value());
    item_picker->SetInputEnabled(false);
    DestroyAfterUiDispatch(std::move(item_picker));
    refreshLayout();
    hud->repaint.layout = true;
    hud->repaint.roof_and_spell_icons = true;
    hud->repaint.submenu_buttons = true;
    item_drag.reset();
    game.aging_held = false;
}

void MainGameScreen::closeItemPicker()
{
    if (!item_picker) return;
    auto on_return = std::move(item_picker->on_return);
    dismissItemPicker();
    if (on_return) on_return();
}

// Retarget the picker, preferring a dragged item or the character's remembered use.
void W8UseItemView::RefreshForSlot(int party_slot)
{
    const W8ItemInstance* item;

    if (!IsPartySlotEligible(party_slot)) {
        screen.closeItemPicker();
        return;
    }
    owner = party_slot;
    if (screen.item_drag && screen.item_drag->pending) {
        screen.item_drag->pending = false;
        screen.game.aging_held = false;
        const auto dragged = screen.item_drag->item;
        if (!screen.game.inventory.held()) {
            const bool party_pool = dragged.storage == W8ItemStorage::Pool;
            assert(!party_pool || !screen.game.combat.has_value());
            Rebuild(party_pool ? Pool::Party : Pool::Character,
                    screen.game.inventory.itemAt(dragged));
        } else {
            Rebuild(Pool::Hand, &screen.game.inventory.held()->item);
        }
        assert(selected_line != -1);
        screen.scrollTextBoxTo(selected_line);
        return;
    }
    if (CanPartySlotUseRecordedItem(party_slot)) {
        screen.item_drag.reset();
        item = screen.game.inventory.itemAt(
            screen.game.party.rows[party_slot].remembered.item_detail.item_use.item);
        if (CanUseItemForAction(owner, item)) {
            const bool party_pool =
                screen.game.party.rows[party_slot].remembered.item_detail.item_use.item.storage ==
                W8ItemStorage::Pool;
            assert(!party_pool || !screen.game.combat.has_value());
            Rebuild(party_pool ? Pool::Party : Pool::Character, item);
            assert(selected_line != -1);
            screen.scrollTextBoxTo(selected_line);
            SetHoveredTextLine(selected_line, 2);
            RedrawTextBox();
            return;
        }
    }
    screen.item_drag.reset();
    Rebuild(pool, nullptr);
}

void W8UseItemView::Repaint(int panel)
{
    screen.hud->repaint.mode_panels.set(panel);
}

void W8UseItemView::UpdatePanels(std::bitset<7>& panels)
{
    if (pools[0]->m_dirty || pools[1]->m_dirty) panels.set(0);
    if (filter->m_dirty || close->m_dirty) panels.set(2);
}

/* Paint the given item-use panels: the source caption and buttons, the list's
   scroll chrome, and the actions with the chosen item and its quantity. */
void W8UseItemView::PaintPanels(std::bitset<7> panels)
{
    auto& target = GetFrameBufferSurface();
    const int x[]{0x17, 0xa4, 0x1dc};
    for (int panel = 0; panel < 3; ++panel) {
        if (!panels.test(panel)) continue;
        CatalogArtwork::Load(0x97, 0).drawAndInvalidate(target, panel, x[panel], 0x166);
        if (panel == 0) {
            caption.RenderToTarget(0, true, target);
            for (auto& button : pools) {
                button->Refresh(true);
            }
        } else if (panel == 1)
            RedrawTextBoxScrollChrome();
        else {
            // These unavailable actions are artwork, not six disabled widgets.
            const auto actions = CatalogArtwork::Load(0x1ab, 0);
            for (int index : {0, 1, 3, 4, 5})
                actions.draw(target, index * 5 + 3, 0x212 + (index % 3) * 17,
                             0x16c + (index / 3) * 17);
            CatalogArtwork::Load(0x1aa, 0).draw(target, 0x21, 0x211, 0x18d);
            filter->Refresh(true);
            close->Refresh(true);
            if (item_image) DrawSpriteAndInvalidate(target, *item_image, 0, 0x1e1, 0x16b);
            quantity.RenderToTarget(0, true, target);
        }
    }
}

/* The two scroll buttons are mutually exclusive views of the same list. */
void W8UseItemView::SelectPool(Pool source)
{
    if (pool != source) Rebuild(source, nullptr);
    UpdatePoolButtons();
}

// Both controls project the selected source. Their pressed states never select a source.
void W8UseItemView::UpdatePoolButtons()
{
    if (screen.game.combat.has_value() && pool == Pool::Party) Rebuild(Pool::Character, nullptr);
    const bool hand = pool == Pool::Hand;
    pools[0]->SetEnabled(!hand);
    pools[1]->SetEnabled(!hand && !screen.game.combat.has_value());
    pools[0]->SetPressed(pool == Pool::Character);
    pools[1]->SetPressed(pool == Pool::Party);
}

// The owner commits values; gameplay may discard or replace its picker.
void MainGameScreen::updateItemPicker()
{
    auto* picker = item_picker.get();
    if (!picker) return;
    const UiTransaction transaction;
    const auto chosen = picker->draft;
    auto* item = game.inventory.itemAt(chosen.detail_block.item_use.item);
    const int slot = picker->owner;
    if (!item || !CanUseItemForAction(slot, item) ||
        !IsItemTargetOfNeededKind(&chosen.target, item))
        return;
    const bool identify = GetItemSpell(item) == W8_SPELL_IDENTIFY_ITEM;
    StartBreathCycle(slot, true);
    SetCharacterItemUse(slot, chosen);
    if (!identify && item_picker.get() == picker) closeItemPicker();
}

// Rebuild from the selected inventory source; usable entries retain inventory order.
void W8UseItemView::Rebuild(Pool source, const W8ItemInstance* select)
{
    ResetEditorStatusLine(2);
    ClearSelectedTextLine(2);
    draft.detail_block.item_use.item = {};
    detail_location.reset();
    item_image.reset();
    quantity.SetText("", g_large_font);
    Repaint(2);
    SelectSpellCastingPartySlot(owner, draft);
    pool = source;
    filter->SetEnabled(true);
    filter->SetPressed(true);
    rows.clear();
    if (pool == Pool::Hand) {
        AppendEntry({W8ItemStorage::Hand});
    } else if (pool == Pool::Character) {
        const auto character = static_cast<unsigned int>(owner);
        for (unsigned int slot = 0; slot < W8_EQUIP_SLOT_COUNT; ++slot)
            AppendEntry({W8ItemStorage::Equipment, character, slot});
        for (unsigned int slot = 0; slot < 8; ++slot)
            AppendEntry({W8ItemStorage::Backpack, character, slot});
    } else {
        for (unsigned int i = 0; i < screen.game.inventory.pool().size(); ++i)
            AppendEntry({W8ItemStorage::Pool, 0, i});
    }
    ShowList(select);
    UpdatePoolButtons();
    screen.scrollTextBoxTo(0);
    Repaint(1);
}

// Classify once; stable partition preserves inventory order within each band.
void W8UseItemView::AppendEntry(W8ItemLocation location)
{
    const W8ItemInstance* item = screen.game.inventory.itemAt(location);
    if (!item || item->iItemNo == -1 || IsFilteredOut(item)) return;
    const bool usable = CanUseItemForAction(owner, item);
    if (!usable && (screen.game.activity.conversation_aside || LockInteractionSuspended() ||
                    TrapInteractionSuspended()))
        return;
    rows.push_back({location, usable});
}

void W8UseItemView::ShowList(const W8ItemInstance* select)
{
    std::stable_partition(rows.begin(), rows.end(), [](const auto& row) { return row.usable; });
    // The shared text box stores at most 350 lines. Keep its rows and ours aligned.
    if (rows.size() > 350) rows.resize(350);
    selected_line = -1;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const auto [location, usable] = rows[index];
        auto* item = screen.game.inventory.itemAt(location);
        if (item == select) selected_line = static_cast<int>(index);
        if (!usable) {
            const bool silenced =
                Data().items[item->iItemNo].equip_class == W8_ITEM_EQUIP_CLASS_INSTRUMENT &&
                screen.game.party.characters[owner].uiCondition[W8_CONDITION_SILENCED];
            ShowNotice(silenced ? 4 : 0, FormatItemDisplayName(Data(), item, true), 2);
            continue;
        }
        int count = 0;
        bool charged = false;
        switch (Data().items[item->iItemNo].quantity_kind) {
        case W8_ITEM_QUANTITY_STACK:
            count = item->stack_count;
            break;
        case W8_ITEM_QUANTITY_CHARGES:
        case W8_ITEM_QUANTITY_USES:
            if (!item->identified) {
                ShowNotice(0xf, fmt::format("{} (?)", FormatItemDisplayName(Data(), item, false)),
                           2);
                continue;
            }
            [[fallthrough]];
        case W8_ITEM_QUANTITY_SHOTS:
            charged = true;
            count = item->uses_or_charges;
            break;
        default:
            break;
        }
        auto text = FormatItemDisplayName(Data(), item, false);
        if (count == -1)
            text = fmt::format("{} (?)", text);
        else if (count > 1 || charged)
            text = fmt::format("{} ({:d})", text, count);
        ShowNotice(W8_FONT_PALETTE_TEXT_BOX, text, 2);
    }
}

/* Entries the character cannot activate or cast from are dropped,
   except usable-class items stay unless the slot's bound NPC wants the
   item. */
bool W8UseItemView::IsFilteredOut(const W8ItemInstance* item) const
{
    W8NpcState* npc;

    if (item->iItemNo == -1) {
        return true;
    }
    if (!CanCharacterActivateItem(CurrentGame().data, &screen.game.party.characters[owner], item,
                                  CurrentGame().status.game_started)) {
        if (CanCastFromItem(&screen.game.party.characters[owner], item)) {
            return false;
        }
        if (!IsUsableItemClass(item)) {
            return true;
        }
        if ((owner == 0 || owner == 1) &&
            (npc = GetNpcState(screen.game.party.npcIndex(owner)), npc != 0) &&
            NpcWantsItem(npc, item)) {
            return true;
        }
    }
    return false;
}

/* After the cursor item is dropped mid-select, rebuild the list under the
   saved select mode so the rows reflect the new state, then repaint. */
void W8UseItemView::Refresh()
{
    Rebuild(pool, nullptr);
    screen.hud->repaint.layout = true;
}

/* Right release on the use-item text box: open the assay dialog for the item
   on the clicked row. Cleanup restores the saved target cursor. */
void W8UseItemView::OpenAssayDialog(W8ItemLocation location)
{
    auto* item = screen.game.inventory.itemAt(location);
    if (!item || item->iItemNo == -1) return;
    auto dialog = std::make_unique<W8AssayDialog>(*item, &screen.game.party.characters[owner],
                                                  SDL_Point{kInfoDialogX, 0x48});
    dialog->SetText("");

    dialog->cleanup = [owner = &screen, picker = this, pointer = screen.pointer.request()] {
        if (owner->item_picker.get() != picker) return;
        owner->pointer.restore(pointer);
        owner->hud->repaint.everything = true;
    };
    OpenModal(std::move(dialog));
}

UiResult W8UseItemView::HandleTextBox(const UiPointerEvent& event)
{
    if (event.action == UiPointerAction::Leave) {
        ClearHoveredTextLine(2);
        hover_row = -1;
    } else if (event.action == UiPointerAction::Move || event.action == UiPointerAction::Enter) {
        const int row = event.local.y / 11;
        if (event.inside && row != hover_row) HighlightNoticeRow(row);
        hover_row = row;
    } else if (event.action == UiPointerAction::Up && event.inside) {
        if (event.button == UiPointerButton::Left && (event.captured_buttons & SDL_BUTTON_LMASK)) {
            const int slot = screen.hud->hovered_text_lines[2];
            if (slot != -1) AfterUiDispatch([this, slot] { SelectLine(slot); });
        }
        if (event.button == UiPointerButton::Right && (event.captured_buttons & SDL_BUTTON_RMASK)) {
            const int row = event.local.y / 11;
            if (row >= 0 && row < static_cast<int>(screen.game.status.text_box_lines_shown[2])) {
                const auto location = rows[screen.hud->text_lines[2] + row].location;
                AfterUiDispatch([this, location] { OpenAssayDialog(location); });
            }
        }
    }
    return UiResult::Handled;
}

/* Wheel rotation over the use-item text box re-selects the row under the
   cursor, even when it is already the hovered row. */
void W8UseItemView::TextBoxWheelAt(unsigned short y)
{
    if (y < screen.hud->text_box_top || screen.hud->text_box_bottom < y) {
        return;
    }
    hover_row = (y - screen.hud->text_box_top) / 0xb;
    HighlightNoticeRow(hover_row);
}

/* Left-click commit of a use-item list row: validate the item's spell for
   this owner, learn it directly when castable-from-item, otherwise point the
   targeting filter at the embedded spell. */
void W8UseItemView::SelectLine(int iTextLine)
{
    W8SpellTargetType target_type;

    assert(iTextLine >= 0);
    assert(iTextLine < static_cast<int>(rows.size()));
    SetTargetingMode(W8_TARGET_NEED_NONE);
    UpdateDetailPanel(rows[iTextLine].location);
    if (ValidateItemSpellUse(
            owner, screen.game.inventory.itemAt(rows[iTextLine].location),
            [owner = &screen](bool) { owner->hud->repaint.layout = true; }, true)) {
        QueueCharacterEvent(&screen.game.party.characters[owner], 0x35, 0,
                            kCharacterEventFullVolume);
        return;
    }
    auto* item = screen.game.inventory.itemAt(rows[iTextLine].location);
    const auto location = rows[iTextLine].location;
    draft.detail_block.item_use.item = location;
    draft.detail_block.item_use.item_id = item->iItemNo;
    if (Trynnie2UseItem(MakeScriptContext(screen.game), location)) {
        screen.closeItemPicker();
        return;
    }
    if (IsUsableItemClass(item)) {
        TakeIntoHand();
        screen.closeItemPicker();
        return;
    }
    if (CanCastFromItem(&screen.game.party.characters[owner], item)) {
        LearnSpellFromItem(&screen.game.party.characters[owner], location);
        screen.closeItemPicker();
        return;
    }
    SetSelectedTextLine(iTextLine, 2);
    ClearHoveredTextLine(2);
    RedrawTextBox();
    target_type = GetSpellTargetType(GetItemSpell(item),
                                     ItemClassNormalizesTarget(&Data().items[item->iItemNo]));
    ConfigureSpellTargetFilter(target_type, ItemTargetNeed(CurrentGame().data, item, false),
                               &draft);
}

/* Show the item in the detail control: its catalog icon plus a quantity line -
   stack counts only past one, uses/charges always once the item is identified,
   '?' while it still is not. */
void W8UseItemView::UpdateDetailPanel(W8ItemLocation location)
{
    auto* item = screen.game.inventory.itemAt(location);
    std::string text;
    std::string value;
    short count;
    bool charges = false;

    item_image = ItemSprites(item->iItemNo);
    count = 0;
    switch (Data().items[item->iItemNo].quantity_kind) {
    case W8_ITEM_QUANTITY_NONE:
        break;
    case W8_ITEM_QUANTITY_STACK:
        count = item->stack_count;
        break;
    case W8_ITEM_QUANTITY_CHARGES:
    case W8_ITEM_QUANTITY_USES:
        charges = true;
        if (!item->identified) {
            count = -1;
        } else {
            count = item->uses_or_charges;
        }
        break;
    case W8_ITEM_QUANTITY_SHOTS:
        charges = true;
        count = item->uses_or_charges;
        break;
    }
    if (count == -1) {
        value = "?";
    } else if (count > 1 || charges) {
        text = fmt::format("{:d}", count);
        value = text;
    } else {
        value = "";
    }
    quantity.SetText(value, g_wiz_text_font_secondary);
    Repaint(2);
    detail_location = location;
}

/* Move the selected item into the cursor hand. When the hand is already
   holding an item that one goes back to the owner or party first; if the
   store shifted the party pool, the selection is re-anchored onto the entry
   the move slid it to before the hand copy is refreshed. */
void W8UseItemView::TakeIntoHand()
{
    auto& selected = draft.detail_block.item_use.item;
    auto& inventory = screen.game.inventory;
    if (inventory.held()) {
        assert(owner != -1);
        const auto old_count = inventory.pool().size();
        (void)inventory.giveHeld(static_cast<unsigned int>(owner), StoreOrder::PoolFirst);
        // A new pool entry goes to the front and moves the selection down one row.
        if (pool == Pool::Party && old_count != inventory.pool().size() &&
            selected.storage == W8ItemStorage::Pool && selected.slot < inventory.pool().size())
            ++selected.slot;
    }
    inventory.swapWithHand(selected, static_cast<unsigned>(owner));
}

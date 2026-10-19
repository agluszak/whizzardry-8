#include "wiz8/local_screens/MGSUseItemSelect.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <fmt/format.h>

#include "conditions.h"
#include "layouts/character.h"
#include "layouts/gameplay_databases.h"
#include "layouts/item_instance.h"
#include "layouts/main_game_screen.h"
#include "layouts/screen_state.h"
#include "layouts/targeting.h"
#include "local_code/TextBuffer.h"
#include "wiz8/button_interaction.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/dialog_code/AssayDialog.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/fonts.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/level_specific_code/Trynnie2.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/MGSPortraitCombat.h"
#include "wiz8/local_screens/MGSSpellCasting.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/modal_content.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/render/frame.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"
#include "wiz8/xstatus.h"

struct W8NpcState;

namespace {
enum class ItemPool { Character, Party, Hand };
struct UseItemView {
    struct Row {
        W8ItemInstance* item;
        bool usable;
    };
    W8Action draft{W8_ACTION_USE_ITEM, -1};
    ItemPool pool = ItemPool::Character;
    int selected_line = -1;
    int owner = -1;
    int hover_row = -1;
    W8ItemInstance* detail_item = nullptr;
    std::vector<Row> rows;
    UiLayer input{UiLayerOrder::ItemSelection};
    std::array<std::unique_ptr<W8ButtonWidget>, 2> pools;
    std::unique_ptr<W8ButtonWidget> filter;
    std::unique_ptr<W8ButtonWidget> close;
    W8TextBuffer caption;
    W8TextBuffer quantity;
    std::shared_ptr<const SpriteSheet> item_image;
    std::array<bool, 3> dirty{true, true, true};
};
std::unique_ptr<UseItemView> use_item;
}

W8Action* ItemUseDraft()
{
    return use_item ? &use_item->draft : nullptr;
}

void ResetUseItemSelection()
{
    use_item.reset();
}

void SetUseItemSelectInputEnabled(bool enabled)
{
    if (use_item) use_item->input.setEnabled(enabled);
}

void UpdateUseItemScrollButtons(void);
static void SelectUseItemPool(ItemPool pool);
static void RebuildUseItemSelectList(ItemPool pool, W8ItemInstance* select);
static void AppendUseItemListEntry(W8ItemInstance* item);
static void ShowUseItemList(W8ItemInstance* select);
static bool IsUseItemFilteredOut(W8ItemInstance* item);
static void UseItemSelectAssayButton(void);
static void CreateUseItemSelectControls(void);

static void CreateUseItemSelectControls(void)
{
    const auto make_button = [](int artwork, int normal, int pressed, int hover, int pressed_hover,
                                int disabled, SDL_Rect bounds, int help, void (*action)()) {
        auto button = std::make_unique<W8ButtonWidget>(use_item->input);
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
        use_item->pools[index] = make_button(
            0x98, base, base + 2, base + 1, base + 4, base + 3,
            {0x17 + (index ? 0x48 : 0x24), 0x183, 32, 32}, 96 + index,
            index ? +[] { SelectUseItemPool(ItemPool::Party); }
                  : +[] { SelectUseItemPool(ItemPool::Character); });
        use_item->pools[index]->Policy().primary_toggle = ButtonToggle::Release;
    }
    use_item->filter = make_button(
        0x1ab, 10, 11, 12, 14, 13, {0x234, 0x16c, 14, 14}, 99,
        +[] { use_item->filter->SetPressed(true); });
    use_item->filter->Policy().primary_toggle = ButtonToggle::Release;
    use_item->filter->SetEnabled(false);
    use_item->close =
        make_button(0x8e, 4, -1, 5, 6, 7, {0x248, 0x16c, 27, 27}, 17, CloseUseItemSelection);
    use_item->caption.SetLayoutBounds(0x1b, 0x16a, 0xa0, 0x179);
    use_item->caption.SetLayoutMode(g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    use_item->caption.SetText(g_localized_strings[0x77a], g_wiz_text_font_secondary);
    use_item->quantity.SetLayoutBounds(0x1e1, 0x16b, 0x20d, 0x19f);
    use_item->quantity.SetLayoutMode(g_W8TextBufferAlignBottom | g_W8TextBufferAlignRight);
    auto& item =
        use_item->input.add(SDL_Rect{0x1e1, 0x16b, 45, 53}, [](const UiPointerEvent& event) {
            if (event.action != UiPointerAction::Up || event.captured_buttons)
                PlayButtonSound(event.action);
            const auto mask =
                event.button == UiPointerButton::Right ? SDL_BUTTON_RMASK : SDL_BUTTON_LMASK;
            if (event.action == UiPointerAction::Up && event.inside &&
                (event.captured_buttons & mask))
                AfterUiDispatch(UseItemSelectAssayButton);
            return UiResult::Handled;
        });
    item.setTooltip([] { return g_localized_strings[98]; });
}

unsigned char OpenUseItemSelectView(int slot)
{

    UpdateScreenOverlays(0);
    use_item = std::make_unique<UseItemView>();
    if (g_level_block->combat_end_notification != -1) {
        DestroySubMenuControls();
    }
    CloseNpcDialogueIfActive();
    CloseMainGameOverlays();
    RefreshMainGameLayout();
    RestoreSpellCastingRegions();
    SelectTextBox(2);
    ResetEditorStatusLine(-1);
    g_level_block->text_box_visible = false;
    CreateUseItemSelectControls();
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
    RequestRedrawCombatBar();
    RequestRedraw(W8_MAIN_REDRAW_SUBMENU_BUTTONS);
    RefreshUseItemSelectionForSlot(slot);
    SelectSpellCastingPartySlot(slot);
    SyncWorldAdvancement();
    return 1;
}

void CloseUseItemSelectView(void)
{
    if (use_item) {
        SetTextBoxScrollInput(false);
        SetTextBoxEnabled(false);
        g_level_block->action_panel_visible = 0;
        SetTargetingMode(W8_TARGET_NEED_NONE);
        ResetEditorStatusLine(-1);
        g_level_block->text_box_visible = true;
        SelectTextBox(gXStatus.fCombatMode);
        use_item.reset();
        RefreshMainGameLayout();
        RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
        RequestRedrawCombatBar();
        RequestRedraw(W8_MAIN_REDRAW_SUBMENU_BUTTONS);
        SyncWorldAdvancement();
        ClearItemDrag();
        if (LockInteractionSuspended() && !IsScreenTransitionPending()) {
            OpenLockInteraction(0);
            return;
        }
        if (TrapInteractionSuspended() && !IsScreenTransitionPending()) {
            OpenTrapInteraction(0);
            return;
        }
        if (gXStatus.fCampMode && g_pending_screen_state.id != W8_SCREEN_CAMP) {
            SyncDialogueNpcState();
        }
    }
}

// Retarget the picker, preferring a dragged item or the character's remembered use.
void RefreshUseItemSelectionForSlot(int party_slot)
{
    W8ItemInstance* item;

    if (!IsPartySlotEligible(party_slot)) {
        CloseUseItemSelectView();
        return;
    }
    use_item->owner = party_slot;
    if (gXStatus.item_drag_active) {
        if (!g_status.item_in_cursor) {
            const bool party_pool = gXStatus.dragged_item_origin == W8_ITEM_ORIGIN_PARTY_POOL;
            if (party_pool && gXStatus.fCombatMode) Fail("!gXStatus.fCombatMode");
            RebuildUseItemSelectList(party_pool ? ItemPool::Party : ItemPool::Character,
                                     gXStatus.dragged_item);
        } else {
            RebuildUseItemSelectList(ItemPool::Hand, &g_status.item_in_hand);
        }
        if (use_item->selected_line == -1) {
            Fail("giReuseItemLineNumber != -1");
        }
        ScrollTextBoxTo(use_item->selected_line);
        gXStatus.item_drag_active = false;
        return;
    }
    if (CanPartySlotUseRecordedItem(party_slot)) {
        gXStatus.dragged_item = 0;
        item = FindCharacterItemAt(
            party_slot, static_cast<unsigned char>(g_status.buffers.XChar[party_slot].item_origin),
            g_status.buffers.XChar[party_slot].item_slot);
        if (CanUseItemForAction(use_item->owner, item)) {
            const bool party_pool =
                g_status.buffers.XChar[party_slot].item_origin == W8_ITEM_ORIGIN_PARTY_POOL;
            if (party_pool && gXStatus.fCombatMode) Fail("!gXStatus.fCombatMode");
            RebuildUseItemSelectList(party_pool ? ItemPool::Party : ItemPool::Character, item);
            if (use_item->selected_line == -1) {
                Fail("giReuseItemLineNumber != -1");
            }
            ScrollTextBoxTo(use_item->selected_line);
            SetHoveredTextLine(use_item->selected_line, 2);
            RedrawTextBox();
            return;
        }
    }
    gXStatus.dragged_item = 0;
    RebuildUseItemSelectList(use_item->pool, nullptr);
}

void InvalidateUseItemSelectPanel(void)
{
    use_item->dirty[1] = true;
}

/* Per-frame update: keep the scroll buttons synced, run the
   dirty panels' redraw pass, then re-check the pending commit through the
   shared helper. */
void UpdateUseItemSelect(bool active)
{
    UpdateUseItemScrollButtons();
    use_item->dirty[0] |= use_item->pools[0]->m_dirty || use_item->pools[1]->m_dirty;
    use_item->dirty[2] |= use_item->filter->m_dirty || use_item->close->m_dirty;
    auto& target = GetFrameBufferSurface();
    const int x[]{0x17, 0xa4, 0x1dc};
    for (int panel = 0; panel < 3; ++panel) {
        if (!active && !use_item->dirty[panel]) continue;
        CatalogArtwork::Load(0x97, 0).drawAndInvalidate(target, panel, x[panel], 0x166);
        if (panel == 0) {
            use_item->caption.RenderToTarget(0, true, target);
            for (auto& button : use_item->pools) {
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
            use_item->filter->m_dirty = use_item->close->m_dirty = true;
            use_item->filter->Refresh();
            use_item->close->Refresh();
            if (use_item->item_image)
                DrawSpriteAndInvalidate(target, *use_item->item_image, 0, 0x1e1, 0x16b);
            use_item->quantity.RenderToTarget(0, true, target);
        }
        use_item->dirty[panel] = false;
    }
    CommitSelectedItemUse();
}

/* The two scroll buttons are mutually exclusive views of the same list. */
static void SelectUseItemPool(ItemPool pool)
{
    if (use_item->pool != pool) RebuildUseItemSelectList(pool, nullptr);
    UpdateUseItemScrollButtons();
}

// Both controls project the selected source. Their pressed states never select a source.
void UpdateUseItemScrollButtons(void)
{
    if (gXStatus.fCombatMode && use_item->pool == ItemPool::Party)
        RebuildUseItemSelectList(ItemPool::Character, nullptr);
    const bool hand = use_item->pool == ItemPool::Hand;
    use_item->pools[0]->SetEnabled(!hand);
    use_item->pools[1]->SetEnabled(!hand && !gXStatus.fCombatMode);
    use_item->pools[0]->SetPressed(use_item->pool == ItemPool::Character);
    use_item->pools[1]->SetPressed(use_item->pool == ItemPool::Party);
}

static void UseItemSelectAssayButton(void)
{
    if (use_item->detail_item != 0 && use_item->item_image) {
        OpenUseItemAssayDialog(use_item->detail_item);
    }
}

// Commit a copy of the draft; execution may close the picker. Identify keeps
// the view available for its second selection step.
void CommitSelectedItemUse(void)
{
    const auto draft = use_item->draft;
    auto* item = draft.detail_block.item_use.item;
    const int slot = use_item->owner;
    if (!item || !CanUseItemForAction(slot, item) || !IsItemTargetOfNeededKind(&draft.target, item))
        return;

    StartBreathCycle(slot, true);
    SetCharacterItemUse(slot, draft);
    if (item->iItemNo != -1 && GetItemSpell(item) == 0x17) return;
    CloseUseItemSelectView();
}

// Rebuild from the selected inventory source; usable entries retain inventory order.
static void RebuildUseItemSelectList(ItemPool pool, W8ItemInstance* select)
{
    ResetEditorStatusLine(2);
    ClearSelectedTextLine(2);
    use_item->draft.detail_block.item_use.item = 0;
    use_item->detail_item = 0;
    use_item->item_image.reset();
    use_item->quantity.SetText("", g_large_font);
    use_item->dirty[2] = true;
    SelectSpellCastingPartySlot(g_status.selected_character);
    use_item->pool = pool;
    use_item->filter->SetEnabled(true);
    use_item->filter->SetPressed(true);
    use_item->rows.clear();
    if (pool == ItemPool::Hand) {
        AppendUseItemListEntry(&g_status.item_in_hand);
    } else if (pool == ItemPool::Character) {
        auto& character = g_status.buffers.Char[use_item->owner];
        for (auto& item : character.EquippedItem) AppendUseItemListEntry(&item);
        for (auto& item : character.backpack) AppendUseItemListEntry(&item);
    } else {
        for (unsigned int i = 0; i < g_status.party_item_count; ++i)
            AppendUseItemListEntry(&g_status.party_item_pool[i]);
    }
    ShowUseItemList(select);
    UpdateUseItemScrollButtons();
    ScrollTextBoxTo(0);
    InvalidateUseItemSelectPanel();
}

// Classify once; stable partition preserves inventory order within each band.
static void AppendUseItemListEntry(W8ItemInstance* item)
{
    if (item->iItemNo == -1 || IsUseItemFilteredOut(item)) return;
    const bool usable = CanUseItemForAction(use_item->owner, item);
    if (!usable && (gXStatus.fCampMode || LockInteractionSuspended() || TrapInteractionSuspended()))
        return;
    use_item->rows.push_back({item, usable});
}

static void ShowUseItemList(W8ItemInstance* select)
{
    auto& rows = use_item->rows;
    std::stable_partition(rows.begin(), rows.end(), [](const auto& row) { return row.usable; });
    // The shared text box stores at most 350 lines. Keep its rows and ours aligned.
    if (rows.size() > 350) rows.resize(350);
    use_item->selected_line = -1;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const auto [item, usable] = rows[index];
        if (item == select) use_item->selected_line = static_cast<int>(index);
        if (!usable) {
            const bool silenced =
                g_item_records[item->iItemNo].equip_class == W8_ITEM_EQUIP_CLASS_INSTRUMENT &&
                g_status.buffers.Char[use_item->owner].uiCondition[W8_CONDITION_SILENCED];
            ShowNotice(silenced ? 4 : 0, FormatItemDisplayName(item, true), 2);
            continue;
        }
        int count = 0;
        bool charged = false;
        switch (g_item_records[item->iItemNo].quantity_kind) {
        case W8_ITEM_QUANTITY_STACK:
            count = item->stack_count;
            break;
        case W8_ITEM_QUANTITY_CHARGES:
        case W8_ITEM_QUANTITY_USES:
            if (!item->identified) {
                ShowNotice(0xf, fmt::format("{} (?)", FormatItemDisplayName(item, false)), 2);
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
        auto text = FormatItemDisplayName(item, false);
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
static bool IsUseItemFilteredOut(W8ItemInstance* item)
{
    W8NpcState* npc;

    if (item->iItemNo == -1) {
        return true;
    }
    if (!CanCharacterActivateItem(&g_status.buffers.Char[g_status.selected_character], item)) {
        if (CanCastFromItem(&g_status.buffers.Char[g_status.selected_character], item)) {
            return false;
        }
        if (!IsUsableItemClass(item)) {
            return true;
        }
        if ((g_status.selected_character == 0 || g_status.selected_character == 1) &&
            (npc = GetNpcState(g_status.buffers.XChar[g_status.selected_character].npc_index),
             npc != 0) &&
            NpcWantsItem(npc, item)) {
            return true;
        }
    }
    return false;
}

/* After the cursor item is dropped mid-select, rebuild the list under the
   saved select mode so the rows reflect the new state, then repaint. */
void RefreshUseItemSelection(void)
{
    RebuildUseItemSelectList(use_item->pool, nullptr);
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
}

/* Right release on the use-item text box: open the assay dialog for the item
   on the clicked row. Cleanup restores the saved target cursor. */
void OpenUseItemAssayDialog(W8ItemInstance* item)
{
    W8AssayDialog* dialog;

    dialog =
        new W8AssayDialog(*item, &g_status.buffers.Char[use_item->owner], {kInfoDialogX, 0x48});
    dialog->SetText("");

    dialog->cleanup = [cursor = gXStatus.iCurrentCursor] {
        SetTargetCursor(cursor);
        RequestRedraw(W8_MAIN_REDRAW_ALL);
    };
    OpenModal(std::unique_ptr<ModalContent>(dialog));
}

void CloseUseItemSelection(void)
{
    CloseUseItemSelectView();
    ClearSlotTargeting(g_status.selected_character);
}

UiResult HandleUseItemTextBox(const UiPointerEvent& event)
{
    if (event.action == UiPointerAction::Leave) {
        ClearHoveredTextLine(2);
        use_item->hover_row = -1;
    } else if (event.action == UiPointerAction::Move || event.action == UiPointerAction::Enter) {
        const int row = event.local.y / 11;
        if (event.inside && row != use_item->hover_row) HighlightNoticeRow(row);
        use_item->hover_row = row;
    } else if (event.action == UiPointerAction::Up && event.inside) {
        if (event.button == UiPointerButton::Left && (event.captured_buttons & SDL_BUTTON_LMASK)) {
            const int slot = g_level_block->hovered_text_lines[2];
            if (slot != -1) AfterUiDispatch([slot] { SelectUseItemLine(slot); });
        }
        if (event.button == UiPointerButton::Right && (event.captured_buttons & SDL_BUTTON_RMASK)) {
            const int row = event.local.y / 11;
            if (row >= 0 && row < static_cast<int>(g_status.text_box_lines_shown[2])) {
                const auto item = use_item->rows[g_level_block->text_lines[2] + row].item;
                if (item) AfterUiDispatch([item] { OpenUseItemAssayDialog(item); });
            }
        }
    }
    return UiResult::Handled;
}

/* Wheel rotation over the use-item text box re-selects the row under the
   cursor, even when it is already the hovered row. */
void UseItemSelectTextBoxWheelAt(short x, unsigned short y, bool flag)
{
    int row;

    if (y < g_level_block->text_box_top || g_level_block->text_box_bottom < y) {
        return;
    }
    row = (y - g_level_block->text_box_top) / 0xb;
    if (row != use_item->hover_row || flag) {
        HighlightNoticeRow(row);
    }
    use_item->hover_row = row;
}

/* Left-click commit of a use-item list row: validate the item's spell for
   this owner, learn it directly when castable-from-item, otherwise point the
   targeting filter at the embedded spell. */
void SelectUseItemLine(int iTextLine)
{
    W8SpellTargetType target_type;

    if (iTextLine < 0) {
        Fail("iTextLine >= 0");
    }
    if (iTextLine >= static_cast<int>(use_item->rows.size())) {
        // c-style-cast-ok: the assertion text itself spells (INT32)
        Fail("iTextLine < (INT32) guiNumItemsInList");
    }
    SetTargetingMode(W8_TARGET_NEED_NONE);
    UpdateUseItemDetailPanel(use_item->rows[iTextLine].item);
    if (ValidateItemSpellUse(use_item->owner, use_item->rows[iTextLine].item,
                             SpellCastingNoticeClosed)) {
        QueueCharacterEvent(&g_status.buffers.Char[use_item->owner], 0x35, 0,
                            kCharacterEventFullVolume);
        return;
    }
    use_item->draft.detail_block.item_use.item = use_item->rows[iTextLine].item;
    if (Trynnie2UseItem(use_item->draft.detail_block.item_use.item)) {
        CloseUseItemSelectView();
        return;
    }
    if (IsUsableItemClass(use_item->draft.detail_block.item_use.item)) {
        TakeUseItemIntoHand();
        CloseUseItemSelectView();
        return;
    }
    if (CanCastFromItem(&g_status.buffers.Char[g_status.selected_character],
                        use_item->draft.detail_block.item_use.item)) {
        LearnSpellFromItem(&g_status.buffers.Char[g_status.selected_character],
                           use_item->draft.detail_block.item_use.item);
        CloseUseItemSelectView();
        return;
    }
    SetSelectedTextLine(iTextLine, 2);
    ClearHoveredTextLine(2);
    RedrawTextBox();
    target_type = GetSpellTargetType(
        GetItemSpell(use_item->draft.detail_block.item_use.item),
        ItemClassNormalizesTarget(
            &g_item_records[use_item->draft.detail_block.item_use.item->iItemNo]));
    ConfigureSpellTargetFilter(target_type,
                               GetTargetNeededForItem(use_item->draft.detail_block.item_use.item,
                                                      g_level_block == nullptr));
}

/* Show the item in the detail control: its catalog icon plus a quantity line -
   stack counts only past one, uses/charges always once the item is identified,
   '?' while it still is not. */
void UpdateUseItemDetailPanel(W8ItemInstance* item)
{
    std::string text;
    std::string value;
    short count;
    bool charges = false;

    use_item->item_image = ItemSprites(item->iItemNo);
    count = 0;
    switch (g_item_records[item->iItemNo].quantity_kind) {
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
    use_item->quantity.SetText(value, g_wiz_text_font_secondary);
    use_item->dirty[2] = true;
    use_item->detail_item = item;
}

void SelectCurrentUseItemLine(void)
{
    SelectUseItemLine(use_item->selected_line);
}

/* Move the selected item into the cursor hand. When the hand is already
   holding an item that one goes back to the owner or party first; if the
   store shifted the party pool, the selection is re-anchored onto the entry
   the move slid it to before the hand copy is refreshed. */
void TakeUseItemIntoHand(void)
{
    unsigned int i;
    unsigned int old_count;

    if (!g_status.item_in_cursor) {
        CopyItemInstance(&g_status.item_in_hand, use_item->draft.detail_block.item_use.item, 0,
                         true);
        return;
    }
    if (use_item->owner == -1) {
        Fail("giUseItemChar != BAD_INDEX");
    }
    old_count = g_status.party_item_count;
    GiveItemToCharacterOrParty(use_item->owner, &g_status.item_in_hand, true);
    if (use_item->pool == ItemPool::Party && old_count != g_status.party_item_count &&
        g_status.party_item_count != 0) {
        for (i = 0; i < g_status.party_item_count; i++) {
            if (use_item->draft.detail_block.item_use.item == &g_status.party_item_pool[i]) {
                use_item->draft.detail_block.item_use.item = &g_status.party_item_pool[i + 1];
                CopyItemInstance(&g_status.item_in_hand, use_item->draft.detail_block.item_use.item,
                                 0, true);
                return;
            }
        }
    }
    CopyItemInstance(&g_status.item_in_hand, use_item->draft.detail_block.item_use.item, 0, true);
}

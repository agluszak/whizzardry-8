#include <SDL3/SDL_mouse.h>
#include "wiz8/button_interaction.h"
#include <SDL3/SDL_rect.h>
#include "wiz8/modal_content.h"
#include <memory>
#include <fmt/format.h>
#include <array>
#include <string>
#include <cstring>
#include <vector>

#include "wiz8/layouts/game_status.h"
#include "wiz8/integer_constants.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/ui_layer.h"
#include "wiz8/item_video_object_vector.h"
#include "image.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/local_screens/MGSUseItemSelect.h"
#include "wiz8/dialog_code/AssayDialog.h"
#include "wiz8/engine_code/Spells.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/MGSPortraitCombat.h"
#include "wiz8/local_screens/MGSSpellCasting.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/level_specific_code/Trynnie2.h"
#include "wiz8/sr_api.h"
#include "wiz8/fonts.h"
#include "wiz8/xstatus.h"
#include "conditions.h"
#include "layouts/character.h"
#include "layouts/gameplay_databases.h"
#include "layouts/item_instance.h"
#include "layouts/main_game_screen.h"
#include "layouts/screen_state.h"
#include "layouts/targeting.h"
#include "local_code/TextBuffer.h"

struct W8NpcState;

#define MGSUSEITEMSELECT_CPP "C:\\Projects\\Wizardry 8\\Local Screens\\MGSUseItemSelect.cpp"

// GLOBAL: WIZ8 0x0069B984
static int g_use_item_select_flags;
// GLOBAL: WIZ8 0x0069b988
W8MainUiMode g_use_item_select_return_mode;
// GLOBAL: WIZ8 0x0069B98C
static int g_use_item_select_mode;

// GLOBAL: WIZ8 0x0069B950

// GLOBAL: WIZ8 0x0069B95C
static int g_selected_use_item_line;
// GLOBAL: WIZ8 0x0069B960

// GLOBAL: WIZ8 0x0069B9A0
W8ItemInstance* g_use_item_selected;
// GLOBAL: WIZ8 0x0069B9A4
W8ItemInstance* g_use_item_select_override_item;
// GLOBAL: WIZ8 0x0069B990
static int g_use_item_list_count;
// GLOBAL: WIZ8 0x0069B994

// GLOBAL: WIZ8 0x0069B9A8
static int g_use_item_cursor_x;
// GLOBAL: WIZ8 0x0069B9AC
static int g_use_item_cursor_y;
// GLOBAL: WIZ8 0x0069B9B0
static int g_use_item_owner_index;
// GLOBAL: WIZ8 0x0069B9B4
static W8ItemInstance* g_use_item_list[0x15e];
// GLOBAL: WIZ8 0x0069BF2C
static W8ItemInstance* g_use_item_detail_item;
// GLOBAL: WIZ8 0x0069BF30
int g_saved_target_cursor;
// GLOBAL: WIZ8 0x0069BF34
static int g_use_item_hover_row;
/* Held while CommitSelectedSpellTarget runs for a use-item
   commit; CloseUseItemSelectView early-outs on it so the commit's side
   effects cannot tear the view down mid-call. */
// GLOBAL: WIZ8 0x0069BF38
static bool g_use_item_commit_active;

namespace {
struct UseItemView {
    UiLayer input{0x1a};
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

void EnableUseItemSelectInput()
{
    if (use_item) use_item->input.setEnabled(true);
}

void UpdateUseItemScrollButtons(void);
static void SelectUseItemScrollMode(int mode);
static void RebuildUseItemSelectList(int mode, W8ItemInstance* select);
static bool AppendUseItemListEntry(W8ItemInstance* item, W8ItemInstance* select,
                                   unsigned char pass);
bool IsUseItemFilteredOut(W8ItemInstance* item);
static void UseItemSelectScrollUp(void);
static void UseItemSelectFilterToggle(void);
static void UseItemSelectAssayButton(void);
static void CreateUseItemSelectControls(void);

static void CreateUseItemSelectControls(void)
{
    use_item = std::make_unique<UseItemView>();
    const auto make_button = [](int artwork, int normal, int pressed, int hover, int pressed_hover,
                                int disabled, SDL_Rect bounds, int help, void (*action)()) {
        auto button = std::make_unique<W8ButtonWidget>(use_item->input);
        button->SetTooltipIndex(help);
        button->Configure(
            ButtonGraphics::Load(CatalogArtwork::Load(artwork, 0).spriteSheet(), disabled, normal, hover,
                                 pressed, pressed_hover),
            [action](auto*) { AfterUiDispatch(action); }, 0);
        button->policy.input_rule = ButtonInputRule::PrimaryPressedState;
        button->menu_artwork = true;
        button->SetPosition(bounds.x, bounds.y);
        button->Resize(bounds.w, bounds.h);
        return button;
    };
    for (int index = 0; index < 2; ++index) {
        const int base = index * 5;
        use_item->pools[index] = make_button(0x98, base, base + 2, base + 1, base + 4, base + 3,
            {0x17 + (index ? 0x48 : 0x24), 0x183, 32, 32}, 96 + index,
            index ? +[] { SelectUseItemScrollMode(1); } : UseItemSelectScrollUp);
        use_item->pools[index]->policy.primary_toggle = ButtonToggle::Release;
    }
    use_item->filter = make_button(0x1ab, 10, 11, 12, 14, 13,
        {0x234, 0x16c, 14, 14}, 99, UseItemSelectFilterToggle);
    use_item->filter->policy.primary_toggle = ButtonToggle::Release;
    use_item->filter->SetEnabled(false);
    use_item->close = make_button(0x8e, 4, -1, 5, 6, 7,
        {0x248, 0x16c, 27, 27}, 17, CloseUseItemSelection);
    use_item->caption.SetLayoutBounds(0x1b, 0x16a, 0xa0, 0x179);
    use_item->caption.SetLayoutMode(g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    use_item->caption.SetText(g_localized_strings[0x77a], g_wiz_text_font_secondary);
    use_item->quantity.SetLayoutBounds(0x1e1, 0x16b, 0x20d, 0x19f);
    use_item->quantity.SetLayoutMode(g_W8TextBufferAlignBottom | g_W8TextBufferAlignRight);
    auto& item = use_item->input.add(SDL_Rect{0x1e1, 0x16b, 45, 53},
        [](const UiPointerEvent& event) {
            const auto mask = event.button == UiPointerButton::Right ? SDL_BUTTON_RMASK : SDL_BUTTON_LMASK;
            if (event.action == UiPointerAction::Up && event.inside && (event.captured_buttons & mask))
                AfterUiDispatch(UseItemSelectAssayButton);
            return UiResult::Handled;
        });
    item.setTooltip([] { return g_localized_strings[98]; });
}

// FUNCTION: WIZ8 0x0059C930
unsigned char OpenUseItemSelectView(int slot)
{
    W8MainUiMode mode;

    g_use_item_commit_active = false;
    UpdateScreenOverlays(0);
    gXStatus.fItemSelectMode = true;
    if (g_level_block->combat_end_notification != -1) {
        DestroySubMenuControls();
    }
    CloseNpcDialogueIfActive();
    CloseMainGameOverlays();
    mode = g_settings.main_ui_mode;
    SetMainGameOverlayViewport(mode);
    g_use_item_select_return_mode = mode;
    RestoreSpellCastingRegions();
    SelectTextBox(2);
    ResetEditorStatusLine(-1);
    g_level_block->text_box_visible = false;
    CreateUseItemSelectControls();
    g_use_item_select_mode = -1;
    g_use_item_select_flags = 0;
    g_use_item_cursor_x = -1;
    g_use_item_cursor_y = -1;
    g_use_item_hover_row = -1;
    g_use_item_selected = 0;
    g_use_item_select_override_item = 0;
    g_use_item_detail_item = 0;
    memset(g_use_item_list, 0, sizeof(g_use_item_list));
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
    RequestRedrawCombatBar();
    RequestRedraw(W8_MAIN_REDRAW_SUBMENU_BUTTONS);
    g_use_item_owner_index = -1;
    RefreshUseItemSelectionForSlot(slot);
    SelectSpellCastingPartySlot(slot);
    PauseMainGameWorld();
    return 1;
}

// FUNCTION: WIZ8 0x0059CAC0
void CloseUseItemSelectView(void)
{
    if (!g_use_item_commit_active) {
        SetTextBoxScrollInput(false);
        SetTextBoxEnabled(false);
        g_level_block->action_panel_visible = 0;
        SetTargetingMode(W8_TARGET_NEED_NONE);
        ResetEditorStatusLine(-1);
        g_level_block->text_box_visible = true;
        SelectTextBox(gXStatus.fCombatMode);
        use_item.reset();
        gXStatus.fItemSelectMode = false;
        ApplyMainGameModeFlag(g_use_item_select_return_mode, true);
        RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
        RequestRedrawCombatBar();
        RequestRedraw(W8_MAIN_REDRAW_SUBMENU_BUTTONS);
        ResumeMainGameWorld();
        ClearItemDrag();
        if (gXStatus.fLockInteract && !IsScreenTransitionPending()) {
            OpenLockInteraction(0);
            return;
        }
        if (gXStatus.fTrapInteract && !IsScreenTransitionPending()) {
            OpenTrapInteraction(0);
            return;
        }
        if (gXStatus.fCampMode && g_pending_screen_state.id != W8_SCREEN_CAMP) {
            SyncDialogueNpcState();
        }
    }
}

/* Rebuild the list for the character the open use-item view now targets. A
   dragged item switches the list to that item's usable/unusable passes; a
   recorded item on the party slot re-selects its line; otherwise the current
   mode is rebuilt or the scroll buttons resynced for an idle list. */
// FUNCTION: WIZ8 0x0059CC40
void RefreshUseItemSelectionForSlot(int party_slot)
{
    W8ItemInstance* item;

    if (!IsPartySlotEligible(party_slot)) {
        CloseUseItemSelectView();
        return;
    }
    g_use_item_owner_index = party_slot;
    if (gXStatus.item_drag_active) {
        if (!g_status.item_in_cursor) {
            const bool party_pool = gXStatus.dragged_item_origin == W8_ITEM_ORIGIN_PARTY_POOL;
            if (party_pool && gXStatus.fCombatMode)
                srAssertFail("!gXStatus.fCombatMode", MGSUSEITEMSELECT_CPP, 0x1f0, 0);
            use_item->pools[party_pool ? 1 : 0]->SetPressed(true);
            use_item->pools[party_pool ? 0 : 1]->SetPressed(false);
            RebuildUseItemSelectList(party_pool ? 1 : 0, gXStatus.dragged_item);
        } else {
            ResetEditorStatusLine(2);
            use_item->filter->SetEnabled(true);
            UseItemSelectFilterToggle();
            g_use_item_list_count = 0;
            g_selected_use_item_line = -1;
            AppendUseItemListEntry(&g_status.item_in_hand, &g_status.item_in_hand, 0);
            AppendUseItemListEntry(&g_status.item_in_hand, &g_status.item_in_hand, 1);
        }
        if (g_selected_use_item_line == -1) {
            srAssertFail("giReuseItemLineNumber != -1", MGSUSEITEMSELECT_CPP, 0x1fe, 0);
        }
        ScrollTextBoxTo(g_selected_use_item_line);
        gXStatus.item_drag_active = false;
        return;
    }
    if (CanPartySlotUseRecordedItem(party_slot)) {
        gXStatus.dragged_item = 0;
        item = FindCharacterItemAt(party_slot, g_status.buffers.XChar[party_slot].item_origin,
                                   g_status.buffers.XChar[party_slot].item_slot);
        if (CanUseItemForAction(g_use_item_owner_index, item)) {
            const bool party_pool = g_status.buffers.XChar[party_slot].item_origin == W8_ITEM_ORIGIN_PARTY_POOL;
            if (party_pool && gXStatus.fCombatMode)
                srAssertFail("!gXStatus.fCombatMode", MGSUSEITEMSELECT_CPP, 0x216, 0);
            use_item->pools[party_pool ? 1 : 0]->SetPressed(true);
            use_item->pools[party_pool ? 0 : 1]->SetPressed(false);
            RebuildUseItemSelectList(party_pool ? 1 : 0, item);
            if (g_selected_use_item_line == -1) {
                srAssertFail("giReuseItemLineNumber != -1", MGSUSEITEMSELECT_CPP, 0x221, 0);
            }
            ScrollTextBoxTo(g_selected_use_item_line);
            SetHoveredTextLine(g_selected_use_item_line, 2);
            RedrawTextBox();
            return;
        }
    }
    gXStatus.dragged_item = 0;
    if (g_use_item_select_mode != -1) {
        RebuildUseItemSelectList(g_use_item_select_mode, 0);
        return;
    }
    use_item->pools[0]->SetPressed(true);
    UseItemSelectScrollUp();
}

// FUNCTION: WIZ8 0x0059CF30
void SetUseItemSelectReturnMode(W8MainUiMode value)
{
    g_use_item_select_return_mode = value;
}
// FUNCTION: WIZ8 0x0059CF40
void InvalidateUseItemSelectPanel(void)
{
    use_item->dirty[1] = true;
}

/* fItemSelectMode per-frame update: keep the scroll buttons synced, run the
   dirty panels' redraw pass, then re-check the pending commit through the
   shared helper. */
// FUNCTION: WIZ8 0x0059CF50
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
                button->Draw(target);
                InvalidateRegion(button->Bounds());
                button->m_dirty = false;
            }
        } else if (panel == 1) RedrawTextBoxScrollChrome();
        else {
            // These unavailable actions are artwork, not six disabled widgets.
            const auto actions = CatalogArtwork::Load(0x1ab, 0);
            for (int index : {0, 1, 3, 4, 5})
                actions.draw(target, index * 5 + 3, 0x212 + (index % 3) * 17, 0x16c + (index / 3) * 17);
            CatalogArtwork::Load(0x1aa, 0).draw(target, 0x21, 0x211, 0x18d);
            use_item->filter->m_dirty = use_item->close->m_dirty = true;
            if (use_item->filter->m_dirty) {
                use_item->filter->Draw(target);
                InvalidateRegion(use_item->filter->Bounds());
                use_item->filter->m_dirty = false;
            }
            if (use_item->close->m_dirty) {
                use_item->close->Draw(target);
                InvalidateRegion(use_item->close->Bounds());
                use_item->close->m_dirty = false;
            }
            if (use_item->item_image) DrawSpriteAndInvalidate(target, *use_item->item_image, 0, 0x1e1, 0x16b);
            use_item->quantity.RenderToTarget(0, true, target);
        }
        use_item->dirty[panel] = false;
    }
    CommitSelectedItemUse();
}

/* The two scroll buttons are mutually exclusive views of the same list. */
static void SelectUseItemScrollMode(int mode)
{
    auto* selected = use_item->pools[mode].get();
    auto* other = use_item->pools[1 - mode].get();
    if (selected->IsPressed()) {
        if (other->IsPressed()) {
            other->SetPressed(false);
            other->m_dirty = true;
        }
        RebuildUseItemSelectList(mode, 0);
    } else {
        selected->SetPressed(true);
    }
}

// FUNCTION: WIZ8 0x0059D790
static void UseItemSelectScrollUp(void)
{
    SelectUseItemScrollMode(0);
}

/* Keep the scroll buttons' enabled and secondary states in step with the
   list: a single-entry list locks both, combat shows the secondary-state
   paging instead of plain enabling. */
// FUNCTION: WIZ8 0x0059D070
void UpdateUseItemScrollButtons(void)
{
    if (g_use_item_select_mode == -1 && g_use_item_list_count == 1) {
        use_item->pools[0]->SetEnabled(false);
        use_item->pools[1]->SetEnabled(false);
        return;
    }
    if (gXStatus.fCombatMode) {
        if (use_item->pools[0]->IsPressed() == 0) {
            use_item->pools[0]->SetPressed(true);
            use_item->pools[0]->m_dirty = true;
            UseItemSelectScrollUp();
        }
        if (use_item->pools[1]->IsEnabled()) {
            use_item->pools[1]->SetEnabled(false);
            use_item->pools[1]->m_dirty = true;
        }
        return;
    }
    if (!use_item->pools[1]->IsEnabled()) {
        use_item->pools[1]->SetEnabled(true);
        use_item->pools[1]->m_dirty = true;
    }
    if (!use_item->pools[0]->IsEnabled()) {
        use_item->pools[0]->SetEnabled(true);
        use_item->pools[0]->m_dirty = true;
    }
}

// FUNCTION: WIZ8 0x0059D830
static void UseItemSelectFilterToggle(void)
{
    if (use_item->filter->IsPressed() == 0) {
        use_item->filter->SetPressed(true);
        use_item->filter->m_dirty = true;
    }
    g_use_item_select_flags |= 1;
}

// FUNCTION: WIZ8 0x0059D860
static void UseItemSelectAssayButton(void)
{
    if (g_use_item_detail_item != 0 && use_item->item_image) {
        OpenUseItemAssayDialog(g_use_item_detail_item);
    }
}

/* Commit the pending use-item action: while the selected item is still usable
   by the owner and its recorded target suits it, commit the target and aim
   the item use. Spell 0x17 keeps the view open for the follow-up pick;
   anything else closes it. */
// FUNCTION: WIZ8 0x0059D180
void CommitSelectedItemUse(void)
{
    W8Character* character;

    if (g_use_item_selected != 0 &&
        CanUseItemForAction(g_status.selected_character, g_use_item_selected) &&
        IsItemTargetOfNeededKind(g_status.selected_character, g_use_item_selected)) {
        character = &g_status.buffers.Char[g_status.selected_character];
        if (g_use_item_selected != 0) {
            g_use_item_commit_active = true;
            CommitSelectedSpellTarget();
            g_use_item_commit_active = false;
            AimItemUseAtCurrentTarget(character, g_use_item_selected);
            if (g_use_item_selected != 0 && g_use_item_selected->iItemNo != -1 &&
                GetItemSpell(g_use_item_selected) == 0x17) {
                return;
            }
            CloseUseItemSelectView();
        }
    }
}

/* Reset the detail control and rebuild the use-item list for a mode:
   -1 clears the view state and disables the action controls, 0 lists the
   owning character's equipment then backpack, and any other mode lists the
   party item pool. Each mode appends entries in two passes (usable first,
   then the rest) through AppendUseItemListEntry; select re-highlights
   the line holding that item. */
// FUNCTION: WIZ8 0x0059D230
static void RebuildUseItemSelectList(int mode, W8ItemInstance* select)
{
    W8Character* character;
    unsigned char pass;
    unsigned int i;
    int slot;

    slot = g_status.selected_character;
    ResetEditorStatusLine(2);
    ClearSelectedTextLine(2);
    g_use_item_selected = 0;
    g_use_item_detail_item = 0;
    use_item->item_image.reset();
    use_item->quantity.SetText("", g_large_font);
    use_item->dirty[2] = true;
    SelectSpellCastingPartySlot(g_status.selected_character);
    g_use_item_select_mode = mode;
    if (mode == -1) {
        use_item->filter->SetEnabled(false);
        return;
    }
    use_item->filter->SetEnabled(true);
    UseItemSelectFilterToggle();
    g_use_item_list_count = 0;
    g_selected_use_item_line = -1;
    if (mode != 0) {
        for (pass = 0; pass < 2; pass++) {
            for (i = 0; i < g_status.party_item_count; i++) {
                if (!AppendUseItemListEntry(&g_status.party_item_pool[i], select, pass)) {
                    return;
                }
            }
            ScrollTextBoxTo(0);
        }
        InvalidateUseItemSelectPanel();
        return;
    }
    character = &g_status.buffers.Char[slot];
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < 12; i++) {
            if (!AppendUseItemListEntry(&character->EquippedItem[i], select, pass)) {
                return;
            }
        }
        for (i = 0; i < 8; i++) {
            if (!AppendUseItemListEntry(&character->backpack[i], select, pass)) {
                return;
            }
        }
    }
    ScrollTextBoxTo(0);
    InvalidateUseItemSelectPanel();
}

/* Append one item to the use-item list under the active filter. Pass 0 takes
   items the owner can use; pass 1 takes the rest outside camp/lock/trap
   contexts (where only the usable pass runs). Entries store the item, select
   it when it matches select, and show a display name with stack/charge counts
   or the unidentified "(?)". Returns false when the list is full. */
// FUNCTION: WIZ8 0x0059D450
static bool AppendUseItemListEntry(W8ItemInstance* item, W8ItemInstance* select, unsigned char pass)
{
    std::string text;
    unsigned int color;
    short count;
    bool charged;

    if (item->iItemNo == -1) {
        return true;
    }
    if (IsUseItemFilteredOut(item)) {
        return true;
    }
    if (pass != 0 || !CanUseItemForAction(g_use_item_owner_index, item)) {
        if (gXStatus.fCampMode) {
            return true;
        }
        if (gXStatus.fLockInteract) {
            return true;
        }
        if (gXStatus.fTrapInteract) {
            return true;
        }
        if (pass != 1) {
            return true;
        }
        if (CanUseItemForAction(g_use_item_owner_index, item)) {
            return true;
        }
        if (g_use_item_list_count + 1U > 0x15e) {
            g_selected_use_item_line = g_use_item_list_count;
            return false;
        }
        if (select != 0 && select == item) {
            g_selected_use_item_line = g_use_item_list_count;
        }
        g_use_item_list[g_use_item_list_count] = item;
        if (g_item_records[item->iItemNo].equip_class != W8_ITEM_EQUIP_CLASS_INSTRUMENT ||
            g_status.buffers.Char[g_use_item_owner_index].uiCondition[W8_CONDITION_SILENCED] == 0) {
            color = 0;
        } else {
            color = 4;
        }
        ShowNotice(color, FormatItemDisplayName(item, true), 2);
        ++g_use_item_list_count;
        return true;
    }
    if (g_use_item_list_count + 1U > 0x15e) {
        g_selected_use_item_line = g_use_item_list_count;
        return false;
    }
    if (select != 0 && select == item) {
        g_selected_use_item_line = g_use_item_list_count;
    }
    g_use_item_list[g_use_item_list_count] = item;
    charged = false;
    switch (g_item_records[item->iItemNo].quantity_kind) {
    case W8_ITEM_QUANTITY_STACK:
        count = item->stack_count;
        break;
    case W8_ITEM_QUANTITY_CHARGES:
    case W8_ITEM_QUANTITY_USES:
        if (!item->identified) {
            ShowNotice(0xf, fmt::format("{} (?)", FormatItemDisplayName(item, false)), 2);
            ++g_use_item_list_count;
            return true;
        }
        charged = true;
        count = item->uses_or_charges;
        break;
    case W8_ITEM_QUANTITY_SHOTS:
        charged = true;
        count = item->uses_or_charges;
        break;
    default:
        ShowNotice(W8_FONT_PALETTE_TEXT_BOX, FormatItemDisplayName(item, false), 2);
        ++g_use_item_list_count;
        return true;
    }
    if (count == -1) {
        text = fmt::format("{} (?)", FormatItemDisplayName(item, false));
    } else if (count > 1 || charged) {
        text = fmt::format("{} ({:d})", FormatItemDisplayName(item, false), count);
    } else {
        text = FormatItemDisplayName(item, false);
    }
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, text, 2);
    ++g_use_item_list_count;
    return true;
}

/* Filter predicate for AppendUseItemListEntry: while the select flags
   are set, entries the character cannot activate or cast from are dropped,
   except usable-class items stay unless the slot's bound NPC wants the
   item. */
// FUNCTION: WIZ8 0x0059D6B0
bool IsUseItemFilteredOut(W8ItemInstance* item)
{
    W8NpcState* npc;

    if (g_use_item_select_flags == 0) {
        return false;
    }
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
// FUNCTION: WIZ8 0x0059D690
void RefreshUseItemSelection(void)
{
    if (g_use_item_select_mode != -1) {
        RebuildUseItemSelectList(g_use_item_select_mode, 0);
        RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
    }
}

/* Right release on the use-item text box: open the assay dialog for the item
   on the clicked row. The target cursor is saved so the destroy callback can
   restore it. */
// FUNCTION: WIZ8 0x0059D880
void OpenUseItemAssayDialog(W8ItemInstance* item)
{
    W8AssayDialog* dialog;

    g_saved_target_cursor = gXStatus.iCurrentCursor;
    dialog = new W8AssayDialog(item, &g_status.buffers.Char[g_use_item_owner_index]);
    dialog->SetText("");
    dialog->SetOrigin(g_info_dialog_x, 0x48);
    dialog->m_destroy_callback = RestoreTargetCursor;
    OpenModal(std::unique_ptr<ModalContent>(dialog));
}

/* Destroy callback for the item-assay and spell-info dialogs: put back the
   cursor that was current before the dialog opened and repaint everything. */
// FUNCTION: WIZ8 0x0059D930
void RestoreTargetCursor(W8DialogBase*)
{
    SetTargetCursor(g_saved_target_cursor);
    RequestRedraw(W8_MAIN_REDRAW_ALL);
}

// FUNCTION: WIZ8 0x0059D950
void CloseUseItemSelection(void)
{
    CloseUseItemSelectView();
    ClearSlotTargeting(g_status.selected_character);
}

UiResult HandleUseItemTextBox(const UiPointerEvent& event)
{
    if (event.action == UiPointerAction::Leave) {
        ClearHoveredTextLine(2);
        g_use_item_hover_row = -1;
    } else if (event.action == UiPointerAction::Move || event.action == UiPointerAction::Enter) {
        g_use_item_cursor_x = event.position.x;
        g_use_item_cursor_y = event.position.y;
        const int row = event.local.y / 11;
        if (event.inside && row != g_use_item_hover_row) HighlightNoticeRow(row);
        g_use_item_hover_row = row;
    } else if (event.action == UiPointerAction::Up && event.inside) {
        if (event.button == UiPointerButton::Left && (event.captured_buttons & SDL_BUTTON_LMASK)) {
            const int slot = GetHoveredTextLine(2);
            if (slot != -1) AfterUiDispatch([slot] { SelectUseItemLine(slot); });
        }
        if (event.button == UiPointerButton::Right && (event.captured_buttons & SDL_BUTTON_RMASK)) {
            const int row = event.local.y / 11;
            if (row >= 0 && row < static_cast<int>(g_status.text_box_lines_shown[2])) {
                const auto item = g_use_item_list[g_level_block->text_lines[2] + row];
                if (item) AfterUiDispatch([item] { OpenUseItemAssayDialog(item); });
            }
        }
    }
    return UiResult::Handled;
}

/* Wheel rotation over the use-item text box re-selects the row under the
   cursor, even when it is already the hovered row. */
// FUNCTION: WIZ8 0x0059DD30
void UseItemSelectTextBoxWheelAt(short x, unsigned short y, bool flag)
{
    int row;

    if (y < g_level_block->text_box_top || g_level_block->text_box_bottom < y) {
        return;
    }
    row = (y - g_level_block->text_box_top) / 0xb;
    if (row != g_use_item_hover_row || flag) {
        HighlightNoticeRow(row);
    }
    g_use_item_hover_row = row;
}

/* Left-click commit of a use-item list row: validate the item's spell for
   this owner, learn it directly when castable-from-item, otherwise point the
   targeting filter at the embedded spell. */
// FUNCTION: WIZ8 0x0059DDC0
void SelectUseItemLine(int iTextLine)
{
    W8SpellTargetType target_type;

    if (iTextLine < 0) {
        srAssertFail("iTextLine >= 0", MGSUSEITEMSELECT_CPP, 0x61d, 0);
    }
    if (iTextLine >= g_use_item_list_count) {
        // c-style-cast-ok: the assertion text itself spells (INT32)
        srAssertFail("iTextLine < (INT32) guiNumItemsInList", MGSUSEITEMSELECT_CPP, 0x61e, 0);
    }
    SetTargetingMode(W8_TARGET_NEED_NONE);
    UpdateUseItemDetailPanel(g_use_item_list[iTextLine]);
    if (ValidateItemSpellUse(g_use_item_owner_index, g_use_item_list[iTextLine],
                             SpellCastingNoticeClosed)) {
        QueueCharacterEvent(&g_status.buffers.Char[g_use_item_owner_index], g_character_event_kind2,
                            0, g_character_event_flags_mask | g_character_event_no_flags,
                            g_character_event_full_volume);
        return;
    }
    g_use_item_selected = g_use_item_list[iTextLine];
    if (Trynnie2UseItem(g_use_item_selected)) {
        CloseUseItemSelectView();
        return;
    }
    if (IsUsableItemClass(g_use_item_selected)) {
        TakeUseItemIntoHand();
        CloseUseItemSelectView();
        return;
    }
    if (CanCastFromItem(&g_status.buffers.Char[g_status.selected_character], g_use_item_selected)) {
        LearnSpellFromItem(&g_status.buffers.Char[g_status.selected_character],
                           g_use_item_selected);
        CloseUseItemSelectView();
        return;
    }
    SetSelectedTextLine(iTextLine, 2);
    ClearHoveredTextLine(2);
    RedrawTextBox();
    target_type = GetSpellTargetType(
        GetItemSpell(g_use_item_selected),
        ItemClassNormalizesTarget(&g_item_records[g_use_item_selected->iItemNo]));
    ConfigureSpellTargetFilter(target_type, GetTargetNeededForItem(g_use_item_selected));
}

/* Show the item in the detail control: its catalog icon plus a quantity line -
   stack counts only past one, uses/charges always once the item is identified,
   '?' while it still is not. */
// FUNCTION: WIZ8 0x0059DFA0
void UpdateUseItemDetailPanel(W8ItemInstance* item)
{
    std::string text;
    std::string value;
    short count;
    bool charges = false;

    use_item->item_image = g_item_video_objects.SharedSprites(item->iItemNo);
    count = 0;
    switch (g_item_records[item->iItemNo].quantity_kind) {
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
    g_use_item_detail_item = item;
}

// FUNCTION: WIZ8 0x0059E0D0
W8ItemInstance* GetSelectedOrFallbackValue(void)
{
    W8ItemInstance* value = g_use_item_select_override_item;
    if (value == 0) {
        value = g_use_item_selected;
    }
    return value;
}

// FUNCTION: WIZ8 0x0059E0E0
void SelectCurrentUseItemLine(void)
{
    SelectUseItemLine(g_selected_use_item_line);
}

/* Move the selected item into the cursor hand. When the hand is already
   holding an item that one goes back to the owner or party first; if the
   store shifted the party pool, the selection is re-anchored onto the entry
   the move slid it to before the hand copy is refreshed. */
// FUNCTION: WIZ8 0x0059E0F0
void TakeUseItemIntoHand(void)
{
    unsigned int i;
    unsigned int old_count;

    if (!g_status.item_in_cursor) {
        CopyItemInstance(&g_status.item_in_hand, g_use_item_selected, 0, true);
        return;
    }
    if (g_use_item_owner_index == -1) {
        srAssertFail("giUseItemChar != BAD_INDEX", MGSUSEITEMSELECT_CPP, 0x6c1, 0);
    }
    old_count = g_status.party_item_count;
    GiveItemToCharacterOrParty(g_use_item_owner_index, &g_status.item_in_hand, true);
    if (g_use_item_select_mode == 1 && old_count != g_status.party_item_count &&
        g_status.party_item_count != 0) {
        for (i = 0; i < g_status.party_item_count; i++) {
            if (g_use_item_selected == &g_status.party_item_pool[i]) {
                g_use_item_selected = &g_status.party_item_pool[i + 1];
                CopyItemInstance(&g_status.item_in_hand, g_use_item_selected, 0, true);
                return;
            }
        }
    }
    CopyItemInstance(&g_status.item_in_hand, g_use_item_selected, 0, true);
}

// FUNCTION: WIZ8 0x0059E1E0
void SetUseItemSelectOverrideItem(W8ItemInstance* value)
{
    g_use_item_select_override_item = value;
}

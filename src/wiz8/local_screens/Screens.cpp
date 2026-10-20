#include "wiz8/local_screens/Screens.h"

#include <cstring>
#include <functional>
#include <memory>
#include <string_view>
#include <variant>

#include "compat/native.h"
#include "layouts/main_game_screen.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/character_skills.h"
#include "wiz8/cursor.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/dialog_code/DialogTextArea.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_screens/AutomapScreen.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/local_screens/CreditsScreen.h"
#include "wiz8/local_screens/IntroScreen.h"
#include "wiz8/local_screens/JournalScreen.h"
#include "wiz8/local_screens/MGSPortraitCombat.h"
#include "wiz8/local_screens/MGSPortraits.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/MainMenuScreen.h"
#include "wiz8/local_screens/OptionsScreen.h"
#include "wiz8/local_screens/PartySelectionScreen.h"
#include "wiz8/local_screens/PleaseWaitScreen.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/screen.h"
#include "wiz8/xstatus.h"

std::unique_ptr<Screen> CreateScreen(W8ScreenId id)
{
    switch (id) {
    case W8_SCREEN_INTRO:
        return CreateIntroScreen();
    case W8_SCREEN_MAIN_MENU:
        return std::make_unique<MainMenuScreen>();
    case W8_SCREEN_GAME_START_ROUTER:
        return CreateGameStartRouterScreen();
    case W8_SCREEN_CHARACTER:
        return std::make_unique<CharacterScreen>();
    case W8_SCREEN_PLEASE_WAIT:
        return CreatePleaseWaitScreen();
    case W8_SCREEN_PARTY_SELECTION:
        return CreatePartySelectionScreen();
    case W8_SCREEN_CAMP:
        return std::make_unique<CampScreen>();
    case W8_SCREEN_MAIN_GAME:
        return std::make_unique<MainGameScreen>();
    case W8_SCREEN_AUTOMAP:
        return CreateAutomapScreen();
    case W8_SCREEN_CREDITS:
        return CreateCreditsScreen();
    case W8_SCREEN_OPTIONS:
        return std::make_unique<OptionsScreen>();
    case W8_SCREEN_JOURNAL:
        return CreateJournalScreen();
    case W8_SCREEN_EXIT:
        return CreateExitScreen();
    case W8_SCREEN_NONE:
    case W8_SCREEN_COUNT:
        break;
    }
    Fail("unknown screen");
}

/* Query whether scrolling is possible, or scroll and invalidate the transcript. */
bool W8NpcDialogueTextController::HandleScrollDownCommand(bool check_only)
{
    if (text_area.ScrollDown(check_only)) {
        if (!check_only) {
            InvalidateAll();
        }
        return true;
    }
    return false;
}

bool W8NpcDialogueTextController::HandleScrollUpCommand(bool check_only)
{
    if (text_area.ScrollUp(check_only)) {
        if (!check_only) {
            InvalidateAll();
        }
        return true;
    }
    return false;
}

/* Route one redraw bit to the open camp or the main game. The slot travels as
   an int: the body only ever reads its low byte for the shift. */
void RequestPartySlotRedraw(int bit)
{
    if (auto* camp = FindScreen<CampScreen>()) {
        camp->InvalidatePortrait();
    } else if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME) {
        RequestRedraw(1 << (bit & 31));
    }
}

/* Refresh one party slot's on-screen presentation for the active screen:
   party selection forwards into its portrait helper; camp marks
   the review panels dirty; main game redraws the portrait overlay. */
void RefreshPartySlotDisplay(unsigned int party_slot)
{
    unsigned int top;
    bool overlay_ready;
    bool highlighted;

    switch (CurrentScreenState().id) {
    case W8_SCREEN_CHARACTER:
        return;
    case W8_SCREEN_PLEASE_WAIT:
        break;
    case W8_SCREEN_PARTY_SELECTION:
        RefreshPartySelectionPortrait(party_slot);
        break;
    case W8_SCREEN_CAMP:
        FindScreen<CampScreen>()->RefreshPartySlot(static_cast<int>(party_slot));
        return;
    case W8_SCREEN_MAIN_GAME:
        if (GetEffectiveMainUiMode() == W8_MAIN_UI_MODE_PORTRAITS ||
            g_level_block->portrait_refresh_pending[party_slot] != 0) {
            switch (party_slot) {
            case 0:
            case 1:
                top = 0x12;
                break;
            case 2:
            case 3:
                top = 0x67;
                break;
            case 4:
            case 5:
                top = 0xbc;
                break;
            case 6:
            case 7:
                top = 0x111;
                break;
            default:
                top = party_slot;
                break;
            }
            overlay_ready =
                PreparePartyPortraitOverlay(party_slot, (party_slot & 1) << 9 | 0x14, top);
            highlighted = false;
            if (g_level_block->highlight.LightsPortrait(static_cast<int>(party_slot)) ||
                party_slot == static_cast<unsigned int>(g_level_block->held_item_display)) {
                highlighted = true;
            }
            RedrawPartyPortraitOverlay(party_slot, highlighted, overlay_ready,
                                       g_level_block->portrait_refresh_pending[party_slot] == 0);
            CurrentGame().monster_manager_entries[party_slot].combat_portrait_dirty = true;
            InvalidatePortraitControl(party_slot);
            return;
        }
        break;
    default:
        break;
    }
}

/* Install a named cursor, or restore the held-item / default cursor when the
   caller passes W8_CURSOR_NONE (-1). Unchanged ids are ignored; a new id resets
   the frame and applies through ApplyCurrentCursor. */
void SetTargetCursor(int cursor)
{
    if (cursor == gXStatus.iCurrentCursor) {
        return;
    }
    if (cursor == -1) {
        UpdateHeldItemCursor();
    } else {
        gXStatus.iCurrentCursor = cursor;
        gXStatus.current_cursor_frame = 0;
        ApplyCurrentCursor();
    }
}

void UpdateHeldItemCursor(void)
{
    if ((CurrentScreenState().id == W8_SCREEN_MAIN_GAME ||
         CurrentScreenState().id == W8_SCREEN_CAMP) &&
        (CurrentGame().party.held.item.iItemNo != -1)) {
        if (CurrentGame().party.held.item.iItemNo != -1) {

            const auto sprites = ItemSprites(CurrentGame().party.held.item.iItemNo);
            SetMouseCursorFromVideoObject(*sprites, 0, 0, 0);
            const auto cursor = CatalogArtwork::Load(0);
            BlitToMouseCursor(*cursor.spriteSheet(), cursor.imageOffset(), 0, 0);
            RefreshMouseCursorTexture();
            gXStatus.iCurrentCursor = W8_CURSOR_INVALID_TARGET;
            return;
        }
    } else if (gXStatus.iCurrentCursor != W8_CURSOR_NONE) {
        const auto cursor = CatalogArtwork::Load(0);
        SetMouseCursorFromVideoObject(*cursor.spriteSheet(), cursor.imageOffset(), 0, 0);
        RefreshMouseCursorTexture();
        gXStatus.iCurrentCursor = W8_CURSOR_NONE;
        gXStatus.current_cursor_frame = 0;
        gXStatus.current_cursor_time = 0;
    }
}

/* Drive the mouse cursor from gXStatus.iCurrentCursor against the main-game
   resource-slot table: resize, install the slot's texture anim, select the
   current frame, and refresh the hotspot. Multi-frame cursors also arm the
   animation countdown. */
void ApplyCurrentCursor(void)
{
    if (gXStatus.iCurrentCursor == W8_CURSOR_NONE) {
        Fail("gXStatus.iCurrentCursor != -1");
    }
    if (g_main_game_resource_slots[gXStatus.iCurrentCursor].object != 0) {
        ResizeMouseCursorSurface(g_main_game_resource_slots[gXStatus.iCurrentCursor].size_x,
                                 g_main_game_resource_slots[gXStatus.iCurrentCursor].size_y);
        SetMouseCursorTexture(g_main_game_resource_slots[gXStatus.iCurrentCursor].object);
        g_main_game_resource_slots[gXStatus.iCurrentCursor].object->SetFrame(
            gXStatus.current_cursor_frame);
        SetMouseCursorHotspot(g_main_game_resource_slots[gXStatus.iCurrentCursor].hotspot_x,
                              g_main_game_resource_slots[gXStatus.iCurrentCursor].hotspot_y);
    }
    if (g_main_game_resource_slots[gXStatus.iCurrentCursor].frame_count > 1) {
        gXStatus.current_cursor_time = w8_get_ticks() + (0xfa);
    }
}

/* Restore the normal cursor. Inventory operations own the held item. */
void ClearHeldItemDisplay(void)
{
    memset(&CurrentGame().party.held.item, 0, sizeof(CurrentGame().party.held.item));

    CurrentGame().party.held.item.iItemNo = -1;

    if (gXStatus.iCurrentCursor != W8_CURSOR_NONE) {
        const auto cursor = CatalogArtwork::Load(0);
        SetMouseCursorFromVideoObject(*cursor.spriteSheet(), cursor.imageOffset(), 0, 0);
        RefreshMouseCursorTexture();
        gXStatus.iCurrentCursor = W8_CURSOR_NONE;
        gXStatus.current_cursor_frame = 0;
        gXStatus.current_cursor_time = 0;
    }
}

/* Draw the held item as the cursor, then blit the supplied catalog video
   object over it. A negative held item id leaves the cursor unchanged. */
void SetItemCursor(int overlay_video_object)
{
    if (CurrentGame().party.held.item.iItemNo != -1) {

        const auto sprites = ItemSprites(CurrentGame().party.held.item.iItemNo);
        SetMouseCursorFromVideoObject(*sprites, 0, 0, 0);
        const auto overlay = CatalogArtwork::Load(overlay_video_object);
        BlitToMouseCursor(*overlay.spriteSheet(), overlay.imageOffset(), 0, 0);
        RefreshMouseCursorTexture();
        gXStatus.iCurrentCursor = W8_CURSOR_INVALID_TARGET;
    }
}

/* Dispatch one already-built notice line to the camp or main-game dialog. */
void ShowNoticeLine(std::string_view text, std::function<void(bool)> callback, bool confirmation,
                    bool cancel)
{
    switch (CurrentScreenState().id) {
    case W8_SCREEN_CAMP:
        ShowCampNoticeLine(text, callback, confirmation, cancel);
        break;
    case W8_SCREEN_MAIN_GAME:
        ShowMainGameNoticeLine(text, callback, confirmation, cancel);
        break;
    default:
        break;
    }
}

namespace {
// Each game event shown the way the active screen shows it.
struct GameEventPresentation {
    void operator()(const TextNotice& notice) const
    {
        ShowNotice(notice.palette, notice.text, notice.text_box, notice.wrap_width,
                   notice.force_dialog);
    }
    void operator()(const CharacterNotice& notice) const
    {
        PostCharacterNotice(static_cast<int>(notice.party_slot), notice.text, notice.context);
    }
    void operator()(const NoticeDialog& dialog) const
    {
        ShowNoticeLine(dialog.text, dialog.on_closed, dialog.confirmation, dialog.cancel);
    }
    void operator()(const TextBoxModeChange& change) const
    {
        SetTextBoxMode(change.mode, change.value);
    }
    void operator()(const PartySlotChanged& change) const
    {
        if (CurrentScreenState().id != W8_SCREEN_CHARACTER)
            RequestPartySlotRedraw(static_cast<int>(change.party_slot));
    }
    void operator()(const PartyLoadChanged&) const
    {
        if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME &&
            !CurrentGame().activity.conversation) {
            RequestRedraw(W8_MAIN_REDRAW_PORTRAITS);
        } else if (auto* camp = FindScreen<CampScreen>()) {
            camp->RefreshPartyLoad();
        }
    }
    void operator()(const CombatPanelChanged&) const { RequestRefreshPartyState(); }
    void operator()(const CharacterActionChanged&) const
    {
        RequestRedraw(W8_MAIN_REDRAW_CHARACTER_ACTION);
    }
    void operator()(const EquipmentSettled&) const
    {
        if (CurrentScreenState().id != W8_SCREEN_MAIN_GAME || !g_level_block) return;
        if (g_level_block->combat_end_notification != -1) ReopenSubMenuPanel();
        RefreshFlaggedMainGameState();
    }
    void operator()(const HeldItemChanged&) const { UpdateHeldItemCursor(); }
    void operator()(const SkillAvailabilityChanged& change) const
    {
        if (auto* screen = FindScreen<CharacterScreen>())
            screen->SkillAvailabilityChanged(change.skill, change.available);
    }
    // Shown at once only over the idle exploring main game; otherwise it waits.
    void operator()(const SkillImproved& improved) const
    {
        auto& game = CurrentGame();
        const bool now = !game.combat.active() && !IsModalOpen() && !game.item_pick_pending &&
                         CurrentScreenState().id == W8_SCREEN_MAIN_GAME && IsScreenIdle() &&
                         !improved.quiet;
        AnnounceSkillIncrease(game, improved.party_slot, improved.skill, now);
    }
};
} // namespace

void PresentGameEvent(const Event& event)
{
    std::visit(GameEventPresentation{}, event);
}

/* Default the live main-game level block after ResetMainGameScreenState: clear
   selection and text-box state, arm the timers, size the text regions, and
   rebuild the logged lines' word hit boxes. */
void InitializeMainGameLevelBlock(void)
{
    int slot;

    if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME && g_level_block != 0) {
        g_level_block->redraw_flags = static_cast<unsigned int>(-1);
    }
    g_level_block->camera_mode = W8_VIEWPORT_MODE_UNINITIALIZED;
    g_level_block->highlight = {};
    g_level_block->combat_action_hover_party_slot = -1;
    g_level_block->portrait_assay_hover_mode = W8_PORTRAIT_ASSAY_HOVER_NONE;
    g_level_block->held_item_display = -1;
    g_level_block->highlight_row = -1;
    g_level_block->highlight_graphic = 0;
    g_level_block->portrait_overlay_party_slot = -1;
    g_level_block->condition_orb_party_slot = -1;
    g_level_block->enchantment_orb_party_slot = -1;
    g_level_block->condition_highlight_party_slot = -1;
    g_level_block->text_content_region =
        CurrentTextLineHasContent() ? kUpArrowEnabled : kTextBoxArrowNone;
    g_level_block->dialogue_content_region =
        CurrentDialogueLineHasContent() ? kDownArrowEnabled : kTextBoxArrowNone;
    g_level_block->world_update_flags = 0;
    g_level_block->camera_motion_flags = 0;
    g_level_block->highlighted_item = -1;
    g_level_block->selected_item = -1;
    g_level_block->clock = w8_get_ticks();
    g_level_block->portrait_flash = 0;
    for (slot = 0; slot < 8; ++slot) {
        g_level_block->portrait_refresh_pending[slot] = 0;
        g_level_block->portrait_refresh_image[slot] = 0;
    }
    g_level_block->combat_end_notification = -1;
    RefreshMainGameLayout();
    g_level_block->character_update_timer = w8_get_ticks() + (0);
    g_level_block->world_update_timer = w8_get_ticks() + (0);
    g_level_block->countdown0 = w8_get_ticks() + (60000);
    g_level_block->countdown1 = w8_get_ticks() + (0);
    g_level_block->text_box_visible = true;
    g_level_block->tick = w8_get_ticks();
    g_level_block->group_list_rows = 0;
    g_level_block->group_list_width = 0;
    MainGame()->monster_list->setEnabled(false);
    g_level_block->action_group = -1;
    g_level_block->text_lines[4 + CurrentGame().status.text_line_cursor] = FindStoppedTextLine();
    g_level_block->refresh_combat_panel = 1;
    g_level_block->combat_panel_timer = w8_get_ticks() + (0);
    g_level_block->text_scroll_drag_idle = 1;
    SetTextBoxRegionBounds(0xa8, 0x16e, 0x1c4, 0x1ba);
    for (unsigned int& text_line : g_level_block->text_lines) {
        text_line = 0;
    }
    for (int slot_index = 0; slot_index < 4; ++slot_index) {
        g_level_block->hovered_text_lines[slot_index] = -1;
        g_level_block->selected_text_lines[slot_index] = -1;
    }
    g_level_block->text_box_font = &g_wiz_text_font_secondary;
    g_level_block->palette = g_wiz_text_font_secondary.glyphs->colors;
    g_level_block->selection_kind = W8_ACTION_NONE;
    g_level_block->selection_settled = false;
    g_level_block->tooltip_since = 0;
    g_level_block->tooltip_pending = false;
    g_level_block->pending_highlight = {};
    g_level_block->countdown3 = w8_get_ticks() + (0);
    g_level_block->combat_slot = -1;
    g_level_block->keyboard_menu_open = false;
    g_level_block->hover_combat_slot = -1;
    g_level_block->cursor_grace = 0;
    g_level_block->countdown4 = w8_get_ticks() + (0);
    g_level_block->portrait_right_hold_armed = false;
    g_level_block->formation_board_alternate = 0;
    g_level_block->radar_map_alternate = 0;
    g_level_block->review_transition_active = false;
    g_level_block->countdown5 = w8_get_ticks() + (0);
    RebuildMessageLogWords();
}

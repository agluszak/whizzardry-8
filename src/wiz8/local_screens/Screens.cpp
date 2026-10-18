#include "wiz8/local_screens/Screens.h"

#include <cstring>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#include "Font.h"
#include "compat/native.h"
#include "layouts/item_instance.h"
#include "layouts/main_game_screen.h"
#include "layouts/targeting.h"
#include "surrender/srDebug.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/cursor.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/dialog_code/DialogTextArea.h"
#include "wiz8/engine_code/stTextureAnim.h" // IWYU pragma: keep - complete animation type for cursor texture conversion and frame access.
#include "wiz8/fonts.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_screens/MGSPortraits.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/PartySelectionScreen.h"
#include "wiz8/local_screens/RCSCommon.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/utility.h"
#include "wiz8/xstatus.h"

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

/* Return the requested screen id, falling back to the state at the top of the
   return stack when there is no explicit pending state. */
W8ScreenId GetPendingScreenState(void)
{
    if (g_pending_screen_state.id != W8_SCREEN_NONE) {
        return g_pending_screen_state.id;
    }
    if (!g_screen_return_stack.empty()) {
        return g_screen_return_stack.back().id;
    }
    return W8_SCREEN_NONE;
}
void RequestScreenTransition(void)
{
    g_screen_return_requested = true;
}

bool IsScreenTransitionPending(void)
{
    if (g_pending_screen_state.id == W8_SCREEN_NONE && !g_screen_return_requested) {
        return false;
    }
    return true;
}

/* Route one redraw bit to the active camp or main-game screen state. The slot
   travels as an int: the body only ever reads its low byte for the shift. */
void RequestPartySlotRedraw(int bit)
{
    if (g_current_screen_state.id == W8_SCREEN_CAMP) {
        g_camp_screen->redraw_flags |= W8_CAMP_REDRAW_PORTRAIT;
    } else if (g_current_screen_state.id == W8_SCREEN_MAIN_GAME) {
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

    switch (g_current_screen_state.id) {
    case W8_SCREEN_CHARACTER:
        return;
    case W8_SCREEN_PLEASE_WAIT:
        break;
    case W8_SCREEN_PARTY_SELECTION:
        RefreshPartySelectionPortrait(party_slot);
        break;
    case W8_SCREEN_CAMP:
        if (giReviewCharSlot == static_cast<int>(party_slot) &&
            g_camp_screen->header_mode == W8_CAMP_HEADER_SUMMARY) {
            if (g_camp_screen->portrait_hovered[0] != 0) {
                g_camp_screen->redraw_flags |= W8_CAMP_REDRAW_PORTRAIT;
                return;
            }
            g_camp_screen->redraw_flags |= W8_CAMP_REDRAW_PORTRAIT;
            RedrawRcsLevelUpPanel();
            RedrawRcsDismissPanel();
            return;
        }
        break;
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
                (g_level_block->held_item_display &&
                 party_slot == static_cast<unsigned int>(*g_level_block->held_item_display))) {
                highlighted = true;
            }
            RedrawPartyPortraitOverlay(party_slot, highlighted, overlay_ready,
                                       g_level_block->portrait_refresh_pending[party_slot] == 0);
            gXStatus.monster_manager_entries[party_slot].combat_portrait_dirty = true;
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
    if ((g_current_screen_state.id == W8_SCREEN_MAIN_GAME ||
         g_current_screen_state.id == W8_SCREEN_CAMP) &&
        g_status.item_in_cursor) {
        if (g_status.item_in_hand.iItemNo != -1) {
            g_status.item_in_cursor = true;
            const auto sprites = ItemSprites(g_status.item_in_hand.iItemNo);
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
        srAssertFail("gXStatus.iCurrentCursor != -1", __FILE__, 0x18d, 0);
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

/* Empty the item-in-hand record and restore the normal cursor. The held item is
   the 0x0c-byte record at g_status.item_in_hand; the
   byte directly before it is item_in_cursor. */
void ClearHeldItemDisplay(void)
{
    memset(&g_status.item_in_hand, 0, sizeof(g_status.item_in_hand));
    g_status.item_in_cursor = false;
    g_status.item_in_hand.iItemNo = -1;

    if (gXStatus.iCurrentCursor != W8_CURSOR_NONE) {
        const auto cursor = CatalogArtwork::Load(0);
        SetMouseCursorFromVideoObject(*cursor.spriteSheet(), cursor.imageOffset(), 0, 0);
        RefreshMouseCursorTexture();
        gXStatus.iCurrentCursor = W8_CURSOR_NONE;
        gXStatus.current_cursor_frame = 0;
        gXStatus.current_cursor_time = 0;
    }
}

/* Inline text colors selected by party order for skill and level-up notices. */

// GLOBAL: WIZ8 0x00647ccc
static unsigned char g_party_order_text_colors[8] = {7, 4, 6, 2, 5, 8, 9, 3};

unsigned char GetPartyOrderTextColor(signed char party_order)
{
    return g_party_order_text_colors[party_order];
}

/* Draw the held item as the cursor, then blit the supplied catalog video
   object over it. A negative held item id leaves the cursor unchanged. */
void SetItemCursor(int overlay_video_object)
{
    if (g_status.item_in_hand.iItemNo != -1) {
        g_status.item_in_cursor = true;
        const auto sprites = ItemSprites(g_status.item_in_hand.iItemNo);
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
    switch (g_current_screen_state.id) {
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

/* Default the live main-game level block after ResetMainGameScreenState: clear
   selection and text-box state, arm the timers, size the text regions, and
   reset message storage. */
void InitializeMainGameLevelBlock(void)
{
    int slot;

    if (g_current_screen_state.id == W8_SCREEN_MAIN_GAME && g_level_block != 0) {
        g_level_block->redraw_flags = static_cast<unsigned int>(-1);
    }
    g_level_block->camera_mode = W8_VIEWPORT_MODE_UNINITIALIZED;
    g_level_block->highlight = {};
    g_level_block->combat_action_hover_party_slot.reset();
    g_level_block->portrait_assay_hover_mode = W8_PORTRAIT_ASSAY_HOVER_NONE;
    g_level_block->held_item_display.reset();
    g_level_block->highlight_row.reset();
    g_level_block->highlight_graphic = 0;
    g_level_block->portrait_overlay_party_slot.reset();
    g_level_block->condition_orb_party_slot.reset();
    g_level_block->enchantment_orb_party_slot.reset();
    g_level_block->condition_highlight_party_slot.reset();
    if (CurrentTextLineHasContent()) {
        g_level_block->text_content_region = kUpArrowEnabled;
    } else {
        g_level_block->text_content_region.reset();
    }
    if (CurrentDialogueLineHasContent()) {
        g_level_block->dialogue_content_region = kDownArrowEnabled;
    } else {
        g_level_block->dialogue_content_region.reset();
    }
    g_level_block->world_update_flags = 0;
    g_level_block->camera_motion_flags = 0;
    g_level_block->highlighted_item.reset();
    g_level_block->selected_item.reset();
    g_level_block->clock = w8_get_ticks();
    g_level_block->portrait_flash = 0;
    for (slot = 0; slot < 8; ++slot) {
        g_level_block->portrait_refresh_pending[slot] = 0;
        g_level_block->portrait_refresh_image[slot] = 0;
    }
    g_level_block->combat_end_notification.reset();
    RefreshMainGameLayout();
    g_level_block->character_update_timer = w8_get_ticks() + (0);
    g_level_block->world_update_timer = w8_get_ticks() + (0);
    g_level_block->countdown0 = w8_get_ticks() + (60000);
    g_level_block->countdown1 = w8_get_ticks() + (0);
    g_level_block->text_box_visible = true;
    g_level_block->tick = w8_get_ticks();
    g_level_block->group_list_rows = 0;
    g_level_block->group_list_width = 0;
    g_main_game_state->monster_list->setEnabled(false);
    g_level_block->action_group.reset();
    g_level_block->stopped_text_line = FindStoppedTextLine();
    g_level_block->refresh_combat_panel = 1;
    g_level_block->combat_panel_timer = w8_get_ticks() + (0);
    g_level_block->text_scroll_drag_idle = 1;
    SetTextBoxRegionBounds(0xa8, 0x16e, 0x1c4, 0x1ba);
    for (unsigned int& text_line : g_level_block->text_lines) {
        text_line = 0;
    }
    for (int slot_index = 0; slot_index < 4; ++slot_index) {
        g_level_block->hovered_text_lines[slot_index].reset();
        g_level_block->selected_text_lines[slot_index].reset();
    }
    g_level_block->text_box_font = &g_wiz_text_font_secondary;
    g_level_block->palette = g_wiz_text_font_secondary.glyphs->colors;
    g_level_block->selection_kind = W8_ACTION_NONE;
    g_level_block->pending_action = -1;
    g_level_block->selection_settled = false;
    g_level_block->tooltip_since = 0;
    g_level_block->tooltip_pending = false;
    g_level_block->pending_highlight = {};
    g_level_block->countdown3 = w8_get_ticks() + (0);
    g_level_block->combat_slot.reset();
    g_level_block->keyboard_menu_open = false;
    g_level_block->hover_combat_slot.reset();
    g_level_block->cursor_grace = 0;
    g_level_block->countdown4 = w8_get_ticks() + (0);
    g_level_block->portrait_right_hold_armed = false;
    g_level_block->formation_board_alternate = 0;
    g_level_block->radar_map_alternate = 0;
    g_level_block->review_transition_active = false;
    g_level_block->countdown5 = w8_get_ticks() + (0);
    ResetMessageStorage();
}

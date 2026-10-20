#include "wiz8/local_screens/Screens.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <variant>

#include "wiz8/canvas/Font.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/character_skills.h"
#include "wiz8/cursor.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/dialog_code/DialogTextArea.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Targeting.h"
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
#include "wiz8/platform/timer.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/screen.h"

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
            MainGameHud()->portrait_refresh_pending[party_slot] != 0) {
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
            if (MainGameHud()->highlight.LightsPortrait(static_cast<int>(party_slot)) ||
                party_slot == static_cast<unsigned int>(MainGameHud()->held_item_display)) {
                highlighted = true;
            }
            RedrawPartyPortraitOverlay(party_slot, highlighted, overlay_ready,
                                       MainGameHud()->portrait_refresh_pending[party_slot] == 0);
            CurrentGame().monster_manager_entries[party_slot].combat_portrait_dirty = true;
            InvalidatePortraitControl(party_slot);
            return;
        }
        break;
    default:
        break;
    }
}

namespace {
// What the pointer shows: the arrow, a cursor-table slot, the held item under an overlay, or
// an image a screen drew itself.
struct PointerLook {
    PointerRequest::Kind kind = PointerRequest::Kind::None;
    int value = W8_CURSOR_NONE;
    int item = -1;
    bool operator==(const PointerLook&) const = default;
};

struct Pointer {
    PointerRequest request;
    PointerLook shown;
    // A multi-frame slot's animation frame and when the next is due.
    int frame = 0;
    std::uint64_t next_frame = 0;
};
Pointer pointer;

int HeldItemId()
{
    return GameInProgress() ? CurrentGame().party.held.item.iItemNo : -1;
}

/* The look the pointer should have. A request made while the hand held something else has
   lapsed; without one, the main game and the camp show the held item. */
PointerLook WantedLook()
{
    const int hand = HeldItemId();
    if (pointer.request.hand != hand) pointer.request = {};
    const auto& request = pointer.request;
    using Kind = PointerRequest::Kind;
    switch (request.kind) {
    case Kind::Slot:
    case Kind::Custom:
        return {.kind = request.kind, .value = request.value};
    case Kind::HeldItem:
        if (hand != -1) return {.kind = Kind::HeldItem, .value = request.value, .item = hand};
        return {};
    case Kind::None:
        break;
    }
    const auto screen = CurrentScreenState().id;
    if (hand != -1 && (screen == W8_SCREEN_MAIN_GAME || screen == W8_SCREEN_CAMP))
        return {.kind = Kind::HeldItem, .value = 0, .item = hand};
    return {};
}

/* Install the shown slot from the main-game resource table: resize, install the slot's
   texture, select the current frame and refresh the hotspot. A multi-frame cursor arms its
   animation. */
void ApplyShownSlot()
{
    auto& slot = g_main_game_resource_slots[pointer.shown.value];
    if (slot.object != 0) {
        ResizeMouseCursorSurface(slot.size_x, slot.size_y);
        SetMouseCursorTexture(slot.object);
        slot.object->SetFrame(pointer.frame);
        SetMouseCursorHotspot(slot.hotspot_x, slot.hotspot_y);
    }
    if (slot.frame_count > 1) {
        pointer.next_frame = MonotonicMilliseconds() + (0xfa);
    }
}

void ShowLook(const PointerLook& look)
{
    pointer.shown = look;
    pointer.frame = 0;
    switch (look.kind) {
    case PointerRequest::Kind::None: {
        const auto cursor = CatalogArtwork::Load(0);
        SetMouseCursorFromVideoObject(*cursor.spriteSheet(), cursor.imageOffset(), 0, 0);
        RefreshMouseCursorTexture();
        break;
    }
    case PointerRequest::Kind::Slot:
        ApplyShownSlot();
        break;
    case PointerRequest::Kind::HeldItem: {
        const auto sprites = ItemSprites(look.item);
        SetMouseCursorFromVideoObject(*sprites, 0, 0, 0);
        const auto overlay = CatalogArtwork::Load(look.value);
        BlitToMouseCursor(*overlay.spriteSheet(), overlay.imageOffset(), 0, 0);
        RefreshMouseCursorTexture();
        break;
    }
    case PointerRequest::Kind::Custom:
        // The screen drew the image itself.
        break;
    }
}

void Ask(PointerRequest request)
{
    request.hand = HeldItemId();
    pointer.request = request;
    PresentPointer();
}
} // namespace

void PresentPointer()
{
    if (const auto look = WantedLook(); look != pointer.shown) ShowLook(look);
}

int ShownCursor()
{
    const auto look = WantedLook();
    switch (look.kind) {
    case PointerRequest::Kind::None:
        return W8_CURSOR_NONE;
    case PointerRequest::Kind::Slot:
        return look.value;
    case PointerRequest::Kind::HeldItem:
    case PointerRequest::Kind::Custom:
        break;
    }
    return W8_CURSOR_INVALID_TARGET;
}

PointerRequest CurrentPointerRequest()
{
    WantedLook();
    return pointer.request;
}

void RestorePointerRequest(const PointerRequest& request)
{
    pointer.request = request;
    PresentPointer();
}

void ShowCustomCursor()
{
    Ask({.kind = PointerRequest::Kind::Custom});
}

void ShowHeldItemCursor(int overlay)
{
    Ask({.kind = PointerRequest::Kind::HeldItem, .value = overlay});
}

void AnimateShownCursor()
{
    if (pointer.shown.kind != PointerRequest::Kind::Slot) return;
    const auto frame_count =
        static_cast<int>(g_main_game_resource_slots[pointer.shown.value].frame_count);
    if (frame_count <= 1 || TimeUntilDeadline(pointer.next_frame)) return;
    if (++pointer.frame == frame_count) pointer.frame = 0;
    ApplyShownSlot();
}

void SetTargetCursor(int cursor)
{
    if (cursor == W8_CURSOR_NONE) {
        ResetPointerCursor();
        return;
    }
    Ask({.kind = PointerRequest::Kind::Slot, .value = cursor});
}

void ResetPointerCursor()
{
    pointer.request = {};
    PresentPointer();
}

/* Empty the hand and drop whatever cursor a screen asked for. Inventory operations own the
   held item. */
void ClearHeldItemDisplay(void)
{
    CurrentGame().party.held.item = {};
    ResetPointerCursor();
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

void ShowRefusal(const InventoryRefusal& refusal)
{
    ShowNoticeLine(refusal.notice, {}, true, false);
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
    void operator()(const CombatantsMoved&) const
    {
        if (MainGameHud()) MainGameHud()->refresh_combat_panel = 1;
    }
    void operator()(const CombatExecutionBegan&) const
    {
        if (MainGameHud() && MainGameHud()->open_submenu) {
            if (g_settings.continuous_combat == 0) {
                DestroySubMenuControls();
            } else {
                ReopenSubMenuPanel();
            }
        }
        if (g_settings.continuous_combat == 0 && TargetingMode() != W8_TARGET_NEED_NONE)
            SetTargetingMode(W8_TARGET_NEED_NONE);
    }
    void operator()(const CombatCalledOff&) const
    {
        if (MainGameHud() && MainGameHud()->open_submenu) DestroySubMenuControls();
    }
    void operator()(const CharacterActionChanged&) const
    {
        RequestRedraw(W8_MAIN_REDRAW_CHARACTER_ACTION);
    }
    void operator()(const EquipmentSettled&) const
    {
        if (CurrentScreenState().id != W8_SCREEN_MAIN_GAME || !MainGameHud()) return;
        if (MainGameHud()->open_submenu) ReopenSubMenuPanel();
        RefreshFlaggedMainGameState();
    }
    void operator()(const SkillAvailabilityChanged& change) const
    {
        if (auto* screen = FindScreen<CharacterScreen>())
            screen->SkillAvailabilityChanged(change.skill, change.available);
    }
    void operator()(const SkillImproved& improved) const
    {
        auto& game = CurrentGame();
        AnnounceSkillIncrease(game, improved.party_slot, improved.skill,
                              !improved.quiet && SkillIncreasesShownNow(game));
    }
    void operator()(const WorldAdvancementChanged& change) const
    {
        PresentWorldAdvancement(change.held, change.hold_changed);
    }
};
} // namespace

bool SkillIncreasesShownNow(const Game& game)
{
    return !game.combat.has_value() && !IsModalOpen() && !game.item_pick_pending &&
           CurrentScreenState().id == W8_SCREEN_MAIN_GAME && IsScreenIdle();
}

void PresentGameEvent(const Event& event)
{
    std::visit(GameEventPresentation{}, event);
}

/* Default the main game's HUD state after ResetMainGameScreenState: clear
   selection and text-box state, arm the timers, size the text regions, and
   rebuild the logged lines' word hit boxes. */
void InitializeMainGameLevelBlock(void)
{
    int slot;

    if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME && MainGameHud() != nullptr) {
        MainGameHud()->redraw_flags = static_cast<unsigned int>(-1);
    }
    MainGameHud()->camera_mode = W8_VIEWPORT_MODE_UNINITIALIZED;
    MainGameHud()->highlight = {};
    MainGameHud()->combat_action_hover_party_slot = -1;
    MainGameHud()->portrait_assay_hover_mode = W8_PORTRAIT_ASSAY_HOVER_NONE;
    MainGameHud()->held_item_display = -1;
    MainGameHud()->highlight_row = -1;
    MainGameHud()->highlight_graphic = 0;
    MainGameHud()->portrait_overlay_party_slot = -1;
    MainGameHud()->condition_orb_party_slot = -1;
    MainGameHud()->enchantment_orb_party_slot = -1;
    MainGameHud()->condition_highlight_party_slot = -1;
    MainGameHud()->text_content_region =
        CurrentTextLineHasContent() ? kUpArrowEnabled : kTextBoxArrowNone;
    MainGameHud()->dialogue_content_region =
        CurrentDialogueLineHasContent() ? kDownArrowEnabled : kTextBoxArrowNone;
    MainGameHud()->world_update_flags = 0;
    MainGameHud()->camera_motion_flags = 0;
    MainGameHud()->highlighted_item = -1;
    MainGameHud()->selected_item = -1;
    MainGameHud()->clock = MonotonicMilliseconds();
    MainGameHud()->portrait_flash = 0;
    for (slot = 0; slot < 8; ++slot) {
        MainGameHud()->portrait_refresh_pending[slot] = 0;
        MainGameHud()->portrait_refresh_image[slot] = 0;
    }
    MainGameHud()->open_submenu.reset();
    RefreshMainGameLayout();
    MainGameHud()->countdown0 = MonotonicMilliseconds() + (60000);
    MainGameHud()->countdown1 = MonotonicMilliseconds() + (0);
    MainGameHud()->text_box_visible = true;
    MainGameHud()->tick = MonotonicMilliseconds();
    MainGameHud()->group_list_rows = 0;
    MainGameHud()->group_list_width = 0;
    MainGame()->monster_list->setEnabled(false);
    MainGameHud()->action_group = -1;
    MainGameHud()->text_lines[4 + CurrentGame().status.text_line_cursor] = FindStoppedTextLine();
    MainGameHud()->refresh_combat_panel = 1;
    MainGameHud()->combat_panel_timer = MonotonicMilliseconds() + (0);
    MainGameHud()->text_scroll_drag_idle = 1;
    SetTextBoxRegionBounds(0xa8, 0x16e, 0x1c4, 0x1ba);
    for (unsigned int& text_line : MainGameHud()->text_lines) {
        text_line = 0;
    }
    for (int slot_index = 0; slot_index < 4; ++slot_index) {
        MainGameHud()->hovered_text_lines[slot_index] = -1;
        MainGameHud()->selected_text_lines[slot_index] = -1;
    }
    MainGameHud()->text_box_font = &g_wiz_text_font_secondary;
    MainGameHud()->palette = g_wiz_text_font_secondary.glyphs->colors;
    MainGameHud()->selection_kind = W8_ACTION_NONE;
    MainGameHud()->selection_settled = false;
    MainGameHud()->tooltip_since = 0;
    MainGameHud()->tooltip_pending = false;
    MainGameHud()->pending_highlight = {};
    MainGameHud()->countdown3 = MonotonicMilliseconds() + (0);
    MainGameHud()->combat_slot = -1;
    MainGameHud()->keyboard_menu_open = false;
    MainGameHud()->hover_combat_slot = -1;
    MainGameHud()->cursor_grace = 0;
    MainGameHud()->countdown4 = MonotonicMilliseconds() + (0);
    MainGameHud()->portrait_right_hold_armed = false;
    MainGameHud()->formation_board_alternate = 0;
    MainGameHud()->radar_map_alternate = 0;
    MainGameHud()->review_transition_active = false;
    MainGameHud()->countdown5 = MonotonicMilliseconds() + (0);
    RebuildMessageLogWords();
}

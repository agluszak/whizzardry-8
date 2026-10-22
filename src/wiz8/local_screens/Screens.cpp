#include "wiz8/local_screens/Screens.h"

#include <bitset>
#include <cassert>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>

#include "wiz8/audio/soundman.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/character_skills.h"
#include "wiz8/cursor.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/AutomapScreen.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/local_screens/CreditsScreen.h"
#include "wiz8/local_screens/IntroScreen.h"
#include "wiz8/local_screens/JournalScreen.h"
#include "wiz8/local_screens/MGSPartyMovement.h"
#include "wiz8/local_screens/MGSPortraitCombat.h"
#include "wiz8/local_screens/MGSPortraits.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/MainMenuScreen.h"
#include "wiz8/local_screens/OptionsScreen.h"
#include "wiz8/local_screens/PartySelectionScreen.h"
#include "wiz8/local_screens/PleaseWaitScreen.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/platform/timer.h"
#include "wiz8/pointer.h"
#include "wiz8/render/frame.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/screen.h"

// The screens that belong to one game take it when they are built.
static Game& GameOf(GameSession& games)
{
    if (auto* game = games.game()) return *game;
    throw std::logic_error("a game screen was requested with no game in progress");
}

std::unique_ptr<Screen> CreateScreen(W8ScreenId id, GameSession& games, Pointer& pointer)
{
    switch (id) {
    case W8_SCREEN_INTRO:
        return CreateIntroScreen();
    case W8_SCREEN_MAIN_MENU:
        return std::make_unique<MainMenuScreen>(games);
    case W8_SCREEN_GAME_START_ROUTER:
        return CreateGameStartRouterScreen();
    case W8_SCREEN_CHARACTER:
        return std::make_unique<CharacterScreen>(GameOf(games));
    case W8_SCREEN_PLEASE_WAIT:
        return CreatePleaseWaitScreen(games);
    case W8_SCREEN_PARTY_SELECTION:
        return CreatePartySelectionScreen(games);
    case W8_SCREEN_CAMP:
        return std::make_unique<CampScreen>(GameOf(games), pointer);
    case W8_SCREEN_MAIN_GAME:
        return std::make_unique<MainGameScreen>(GameOf(games), pointer);
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
    std::unreachable();
}

namespace {
// The main game's HUD while the main game is the screen shown.
W8LevelRuntimeBlock* ShownHud()
{
    if (CurrentScreenState().id != W8_SCREEN_MAIN_GAME) return nullptr;
    auto* main_game = MainGame();
    return main_game ? main_game->hud.get() : nullptr;
}
} // namespace

// Repaint a member's portrait on the open camp or the shown main game.
void RequestPartySlotRedraw(int bit)
{
    if (auto* camp = FindScreen<CampScreen>()) {
        camp->InvalidatePortrait();
    } else if (auto* hud = ShownHud()) {
        hud->repaint.portrait(bit);
    }
}

/* Refresh one party slot's on-screen presentation for the active screen:
   party selection forwards into its portrait helper; camp marks
   the review panels dirty; main game requests the portrait's next frame. */
void RefreshPartySlotDisplay(unsigned int party_slot)
{
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
        if (auto* hud = ShownHud()) hud->repaint.portrait_frames.set(party_slot);
        break;
    default:
        break;
    }
}

/* Empty the hand and drop whatever cursor a screen asked for. Inventory operations own the
   held item. */
void ClearHeldItemDisplay(void)
{
    CurrentGame().party.held.item = {};
    ScreenPointer().reset();
}

/* Dispatch one already-built notice line to the camp or main-game dialog. */
void ShowNoticeLine(std::string_view text, std::function<void(bool)> callback, bool confirmation,
                    bool cancel)
{
    switch (CurrentScreenState().id) {
    case W8_SCREEN_CAMP:
        FindScreen<CampScreen>()->dialogs.ShowNotice(text, std::move(callback), confirmation,
                                                     cancel);
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
    void operator()(const CharacterSpeech& speech) const
    {
        QueueCharacterEvent(&CurrentGame().party.characters[speech.party_slot], speech.event_type,
                            0, kCharacterEventFullVolume);
    }
    void operator()(const CharacterDied&) const
    {
        wiz8::sound::play("Data\\Sound\\Misc\\CharacterDead.wav");
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
            if (auto* hud = ShownHud()) hud->repaint.portraits.set();
        } else if (auto* camp = FindScreen<CampScreen>()) {
            camp->RefreshPartyLoad();
        }
    }
    void operator()(const CombatPanelChanged&) const { RequestRefreshPartyState(); }
    void operator()(const CombatantsMoved&) const
    {
        if (auto* main_game = MainGame(); main_game && main_game->hud)
            main_game->hud->refresh_combat_panel = 1;
    }
    void operator()(const CombatExecutionBegan&) const
    {
        if (auto* main_game = MainGame();
            main_game && main_game->hud && main_game->hud->open_submenu) {
            if (g_settings.continuous_combat == 0) {
                DestroySubMenuControls();
            } else {
                ReopenSubMenuPanel();
            }
        }
        if (g_settings.continuous_combat == 0 && ScreenPointer().targeting() != W8_TARGET_NEED_NONE)
            SetTargetingMode(W8_TARGET_NEED_NONE);
    }
    void operator()(const CombatCalledOff&) const
    {
        if (auto* main_game = MainGame();
            main_game && main_game->hud && main_game->hud->open_submenu)
            DestroySubMenuControls();
    }
    void operator()(const CombatBegan&) const
    {
        ScreenPointer().reset();
        auto* main_game = MainGame();
        if (main_game) main_game->party_movement.reset();
        DisablePortraitControls();
        if (main_game) main_game->combat.setEnabled(true);
        if (auto* hud = ShownHud()) {
            hud->repaint.portraits.set();
            hud->repaint.submenu_buttons = true;
            hud->repaint.combat_stance = true;
        }
    }
    void operator()(const CombatEnded&) const
    {
        auto* hud = ShownHud();
        if (!hud) return;
        /* A suspended main game re-arms its input when it is entered again. While
           quitting, the suspended main game is the current screen as it is left,
           but its controls were already released when it was suspended. */
        if (PortraitControlsExist()) {
            EnablePortraitAdvanceRegions();
            MainGame()->combat.setEnabled(false);
        }
        // The party-death or ending screen keeps the frame it covers.
        if (hud->review_transition_active) return;
        ClearSurfaceRect(0x17, 0x34, 0x2d, 0x159);
        ClearSurfaceRect(0x253, 0x34, 0x269, 0x159);
        hud->repaint.portraits.set();
        hud->repaint.submenu_buttons = true;
        hud->repaint.combat_stance = true;
    }
    void operator()(const CombatProgressChanged& change) const
    {
        if (auto* hud = ShownHud()) {
            hud->repaint.combat_progress = true;
            if (change.whole_party) hud->repaint.portraits.set();
        }
    }
    void operator()(const CombatStanceChanged&) const
    {
        if (auto* hud = ShownHud()) hud->repaint.combat_stance = true;
    }
    void operator()(const MonstersChanged&) const
    {
        if (auto* hud = ShownHud()) hud->repaint.monster_list = true;
    }
    void operator()(const PartyEffectsChanged&) const
    {
        if (auto* hud = ShownHud()) hud->repaint.roof_and_spell_icons = true;
    }
    void operator()(const CombatEffectsChanged&) const
    {
        if (auto* hud = ShownHud()) hud->repaint.combat_effects = true;
    }
    void operator()(const PartyPanelChanged&) const
    {
        if (auto* hud = ShownHud()) hud->repaint.portrait_panel = true;
    }
    void operator()(const PartyMembershipChanged&) const
    {
        if (auto* hud = ShownHud()) hud->repaint.everything = true;
    }
    void operator()(const WorldItemRemoved& removed) const
    {
        if (auto* hud = ShownHud(); hud && hud->selected_item == removed.runtime_id)
            hud->selected_item = -1;
    }
    void operator()(const CharacterActionChanged&) const
    {
        if (auto* hud = ShownHud()) hud->repaint.character_action = true;
    }
    void operator()(const EquipmentSettled&) const
    {
        auto* hud = ShownHud();
        if (!hud) return;
        if (hud->open_submenu) ReopenSubMenuPanel();
        RefreshFlaggedMainGameState();
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
    void operator()(const TargetingRequested& request) const { SetTargetingMode(request.need); }
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

/* Default the main game's HUD state on entry: clear
   selection and text-box state, arm the timers, size the text regions, and
   rebuild the logged lines' word hit boxes. */
void InitializeMainGameLevelBlock(W8LevelRuntimeBlock& hud)
{
    int slot;

    hud.repaint.everything = true;
    hud.highlight = {};
    hud.combat_action_hover_party_slot = -1;
    hud.portrait_assay_hover_mode = W8_PORTRAIT_ASSAY_HOVER_NONE;
    hud.held_item_display = -1;
    hud.highlight_row = -1;
    hud.highlight_graphic = 0;
    hud.portrait_overlay_party_slot = -1;
    hud.condition_orb_party_slot = -1;
    hud.enchantment_orb_party_slot = -1;
    hud.condition_highlight_party_slot = -1;
    hud.text_content_region = CurrentTextLineHasContent() ? kUpArrowEnabled : kTextBoxArrowNone;
    hud.dialogue_content_region =
        CurrentDialogueLineHasContent() ? kDownArrowEnabled : kTextBoxArrowNone;
    hud.world_update_flags = 0;
    hud.camera_motion_flags = 0;
    hud.highlighted_item = -1;
    hud.selected_item = -1;
    hud.clock = MonotonicMilliseconds();
    hud.portrait_flash = 0;
    for (slot = 0; slot < 8; ++slot) {
        hud.portrait_refresh_pending[slot] = 0;
        hud.portrait_refresh_image[slot] = 0;
    }
    hud.open_submenu.reset();
    hud.countdown0 = MonotonicMilliseconds() + (60000);
    hud.countdown1 = MonotonicMilliseconds() + (0);
    hud.text_box_visible = true;
    hud.tick = MonotonicMilliseconds();
    hud.group_list_rows = 0;
    hud.group_list_width = 0;
    MainGame()->monster_list->setEnabled(false);
    hud.action_group = -1;
    hud.text_lines[4 + CurrentGame().status.text_line_cursor] = FindStoppedTextLine();
    hud.refresh_combat_panel = 1;
    hud.combat_panel_timer = MonotonicMilliseconds() + (0);
    hud.text_scroll_drag_idle = 1;
    SetTextBoxRegionBounds(0xa8, 0x16e, 0x1c4, 0x1ba);
    for (unsigned int& text_line : hud.text_lines) {
        text_line = 0;
    }
    for (int slot_index = 0; slot_index < 4; ++slot_index) {
        hud.hovered_text_lines[slot_index] = -1;
        hud.selected_text_lines[slot_index] = -1;
    }
    hud.text_box_font = &g_wiz_text_font_secondary;
    hud.palette = g_wiz_text_font_secondary.glyphs->colors;
    hud.tooltip_since = 0;
    hud.tooltip_pending = false;
    hud.pending_highlight = {};
    hud.countdown3 = MonotonicMilliseconds() + (0);
    hud.combat_slot = -1;
    hud.hover_combat_slot = -1;
    hud.cursor_grace = 0;
    hud.countdown4 = MonotonicMilliseconds() + (0);
    hud.portrait_right_hold_armed = false;
    hud.formation_board_alternate = 0;
    hud.review_transition_active = false;
    hud.countdown5 = MonotonicMilliseconds() + (0);
    RebuildMessageLogWords();
}

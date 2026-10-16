#include "wiz8/design_rect.h"
#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string_view>

#include <SDL3/SDL_keycode.h>

#include "compat/compiler.h"
#include "input.h"
#include "layouts/targeting.h"
#include "wiz8/audio_resources.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/local_screens/MGSButtons.h"
#include "wiz8/modal_content.h"
#include "wiz8/ui_input.h"

class Trigger;

struct MainGameScreenState {
    MainGameScreenState();
    bool resume_world = false;
    MainGameHudButtons hud_buttons;
    std::unique_ptr<MainGameSubMenu> submenu;
    UiLayer combat{UiLayerOrder::Combat}, edges{UiLayerOrder::ScreenEdges},
        radar{UiLayerOrder::Radar}, formation{UiLayerOrder::FormationBase},
        world{UiLayerOrder::GameplayOverlays};
    std::array<std::unique_ptr<UiLayer>, 8> portraits;
    std::array<std::array<HitTarget*, 6>, 8> portrait_targets{};
    std::array<HitTarget*, 8> combat_targets{}, edge_targets{};
    HitTarget *radar_target = nullptr, *monster_list = nullptr, *world_view = nullptr;
};
extern std::unique_ptr<MainGameScreenState> g_main_game_state;
void EnableMainGamePortraitSlot(unsigned int slot);
void DisableMainGamePortraitSlot(unsigned int slot);

void RequestRedrawParty(void);
void RedrawCombatMonsterList(void);
void RefreshSelectedPartyPortrait(unsigned int party_slot);
void ClearHighlightIfItIs(const int* item);

#include "wiz8/layouts/main_game_screen.h"

extern W8MainGameResourceSlot g_main_game_resource_slots[17];
extern DesignRect g_viewport_modes[];
/* String-list ids naming each trap row; Traps.cpp indexes it with the
   trigger's trap type for the disarm/spring notices. */
extern unsigned short g_trap_name_string_ids[];
#include "wiz8/dialog_code/DialogTextArea.h"
#include "wiz8/engine_code/game_timer.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/RangeControl.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/ui_layer.h"

class W8MainGameScreen;

class W8MainGameTextKeyHandler {
public:
    W8MainGameTextKeyHandler();
    void Redraw(SDL_Surface& target, bool full_redraw);
    void SetSelectedLine(int line);
    char HandleKey(SDL_Keycode key);
    UiLayer input;
    W8RangeControl m_range{input, 0x1d0, 0x16b, 0x1e2, 0x1bf};
    int m_line_count = 15, m_visible_lines = 6;
    int m_selected_line = 0, m_hover_line = -1, m_first_visible_line = 0;
    bool m_dirty = true;
    std::function<void()> on_selection;
};

class W8MainGameTextPanel {
public:
    W8MainGameTextPanel();
    void BeginProgress(std::string_view text, float duration, float hold);
    void Redraw(SDL_Surface& target, bool full_redraw);
    void OnRangeChanged();
    void InvalidateAll() { m_dirty = true; }
    void EnableInput(bool enabled)
    {
        input.setEnabled(enabled);
        m_key_handler->input.setEnabled(enabled);
    }
    UiLayer input;
    struct Cell {
        HitTarget* target;
        ButtonInteraction interaction;
        bool enabled = true, blocked = false, dirty = true;
        int image = -1;
    };
    std::array<Cell, 8> cells;
    std::unique_ptr<W8MainGameTextKeyHandler> m_key_handler;
    bool m_dirty = true;
    int m_selection;
    W8MainGameScreen* m_screen;
    int* m_values;
    bool m_progress_display; /* the timed progress text block is displayed */
    float m_progress_duration;
    float m_progress_elapsed;
    int m_progress_drawn; /* widest progress extent drawn so far */
    W8GameTimer m_timer0;
    W8ControlsRect m_text_bounds;
    W8TextBuffer m_text_buffer;
    W8GameTimer m_timer1;
    int m_marker_anim_time; /* drives the 12-frame target-changed marker */
    bool m_target_changed;
    /* Draw the animated target-changed marker this frame; raised while
       m_dirty, consumed by Redraw. */
    bool m_target_marker_pending;
};

class W8MainGameStatusPanel {
public:
    W8MainGameStatusPanel();
    void Redraw(SDL_Surface& target, bool full_redraw);
    void RefreshStatusTexts();
    int m_target = 0;

private:
    std::array<W8TextBuffer, 7> m_text;
    bool m_dirty = true;
};

/* The NPC dialogue keyword transcript. */
// VTABLE: WIZ8 0x005ee920
class W8NpcDialogueTextController {
public:
    W8NpcDialogueTextController();
    void Redraw(SDL_Surface& target, bool full_redraw);
    void InvalidateAll() { m_dirty = true; }
    void InvalidateLayout() { m_dirty = true; }
    UiLayer input{UiLayerOrder::LockInput};
    HitTarget* expanded_target = nullptr;
    bool m_active = false, m_dirty = true;
    bool HandleScrollDownCommand(bool check_only);
    bool HandleScrollUpCommand(bool check_only);
    /* Add one keyword line to the transcript unless the text is already
       present; a nonzero mark puts the new entry in state 0x60, and any
       leftover "[No Keywords]" placeholder is removed afterwards. */
    unsigned char AddTranscriptEntry(std::string_view text, signed char category, char mark);
    /* Whether the expanded transcript's top edge reaches above the portrait
       band for an odd party slot (1 -> 0x67, 3 -> 0xbc, 5 -> 0x111, 7 ->
       always covered). Callers use it to skip portrait work on rows the
       open transcript covers. */
    bool IsSlotPortraitTranscriptCovered(unsigned int party_slot);
    /* Re-apply the category filter, rebuild the expansion and restate the
       scroll widgets. */
    void SetTranscriptCategoryFilter(signed char category);
    /* Replay every saved dialogue_transcript record into the text area,
       adding the "[No Keywords]" placeholder and forcing the all filter
       when nothing was saved. */
    void RestoreTranscriptEntries();
    /* Snapshot the transcript lines into the screen state's
       dialogue_transcript records (text plus category byte). */
    void SaveTranscriptEntries();
    void HighlightFirstEntry() { text_area.SelectEntry(text_area.m_first_visible_entry); }
    /* Drop every transcript line from the text area and invalidate. */
    void ClearTranscriptEntries();
    int GetSelectedTranscriptEntryIndex();
    void SetTranscriptSorted(bool sorted);
    void RemoveSelectedTranscriptEntry();
    /* Open the transcript upward to fit its lines (capped at 0xff pixels)
       and enable its scroll region. */
    void Expand();
    /* Shrink the transcript back to one line, clearing and redrawing the
       area it covered. */
    void Collapse();
    /* Clear and redraw the backdrop the transcript covers. */
    void ClearBackground();
    bool IsExpanded();

    /* Catalog image ids whose measured heights seed margin and
       line_height/scroll_height in the constructor; never read again. */
    int margin_image;
    int line_image;
    int visible;
    int line_height;
    int margin;
    int scroll_height;
    W8DialogTextArea text_area;
    void SelectTranscriptKeywordAtPoint(int x, int y);
};

class W8LockInteraction;

struct W8LockTumbler {
    SDL_Rect bounds{};
    bool m_pin_set = false, m_rising = false, m_falling = false, m_at_top = false;
    bool m_hovered = false, m_enabled = true, m_dirty = true;
    int m_pin_index = 0, m_pin_height = 0x22;
    void Redraw(SDL_Surface& target, bool full_redraw);
};

class W8LockTumblerPanel {
public:
    W8LockTumblerPanel(int tumbler_count, const unsigned char* pin_data, W8LockInteraction& owner);
    void UpdateTumblerAnimation();
    void InvalidateAll() { m_dirty = true; }
    void Redraw(SDL_Surface& target, bool full_redraw);
    UiLayer input;
    std::array<W8LockTumbler, 8> m_tumblers;
    int m_tumbler_count;
    bool m_animating = false, m_dirty = true;
    int m_phase = 0;
    W8GameTimer m_phase_timer{0.04f, 0};
    W8GameTimer m_rise_timer{0.03f, 0};
    W8GameTimer m_fall_timer{0.01f, 0};
};

class W8LockInfoPanel {
public:
    explicit W8LockInfoPanel(int tumbler_count);
    void Redraw(SDL_Surface& target, bool full_redraw);
    void RefreshInfo();
    void InvalidateAll() { m_dirty = true; }
    int m_tumbler_count;
    bool m_dirty = true;
    std::array<W8TextBuffer, 7> text;
};

class W8LockInteraction {
public:
    W8LockInteraction(Trigger* trigger);

    void OnTumblerPicked(int index);
    void OnPrimary(W8ButtonWidget* control);
    void DrawActions(SDL_Surface& target);
    int ReleaseOwnedTumblers(int slot);
    void Process();
    void ResolvePick();
    void AttemptForce();
    void EnablePanels(int enable);
    void BeginUnlock();
    /* Knock-knock resolution: rolls the per-level chance over a shuffled pin
       order, raising (or on backfire dropping) them, then re-derives the
       control enables and enters state 7. The flag parameter is unused. */
    void ApplyKnockKnock(int level, int flag, char backfire);

    UiLayer input;
    std::array<std::unique_ptr<W8ButtonWidget>, 4> buttons;
    bool actions_dirty = true;
    Trigger* m_trigger;
    int m_tumbler_count; /* trigger->lock_state.difficulty clamped to [2,8] */
    std::unique_ptr<W8LockTumblerPanel> m_tumbler_panel;
    std::unique_ptr<W8LockInfoPanel> m_info_panel;
    W8ButtonWidget* m_done_button;      /* OnPrimary target, state 9 */
    W8ButtonWidget* m_spell_button;     /* gated by spell-0x27 power */
    W8ButtonWidget* m_force_button;     /* gated by the force chance */
    W8ButtonWidget* m_cancel_button;    /* OnPrimary target, state 5 (cancel) */
    int m_selected_slot;               /* party slot owning the raised pins */
    int m_picked_tumbler;              /* index OnTumblerPicked recorded */
    int m_state;                       /* Process() state */
    int m_tumbler_owner[8];            /* owning party slot per pin, -1 unset */
    unsigned char m_tumbler_locked[8]; /* pin kept when the slot is released */
    int m_slot_attempts[8];            /* pick attempts per party slot */
    W8GameTimer m_timer;               /* state-8 completion delay */
    W8MainUiMode return_mode = W8_MAIN_UI_MODE_PORTRAITS;
};

class W8MainGameScreen {
public:
    W8MainGameScreen(Trigger* owner);
    void SelectTextEntry(int index);
    void OnPrimary(int action);
    void DrawActions(SDL_Surface& target);
    void Update();
    void RefreshActionPanel();
    void EnablePanelInput(bool enable);
    int GetTrapInteractionChance() const;
    void ApplyInspectSuccess();
    void CastTrapSpell();
    void UseTrapItem();

    UiLayer input;
    bool actions_dirty = true;
    Trigger* m_owner;
    std::unique_ptr<W8MainGameTextPanel> m_text_panel;
    std::unique_ptr<W8MainGameStatusPanel> m_status_panel;
    int m_disarm_state;
    int m_selected_character;
    std::array<std::unique_ptr<W8ButtonWidget>, 5> m_action_controls;
    int m_device_id;
    int m_difficulty;
    unsigned char m_column_filled[8];
    unsigned char m_slot_attempted[8];
    int m_slot_columns[8][8];
    int m_target_difficulty;
    wiz8::sound::Playback m_sound_handle;
    W8GameTimer m_timer;
    W8MainUiMode return_mode = W8_MAIN_UI_MODE_PORTRAITS;
};

extern std::unique_ptr<W8LevelRuntimeBlock> g_level_block;
extern std::unique_ptr<W8MainGameScreen> g_main_game_screen;

/* Insanity (spell 0x3c) world-cursor extent rows and the per spell-power
   index into them; CastSpellFromSource scans the same extents when it
   places the insanity point. */
extern double g_world_cursor_extent_table[18];
extern signed char g_spell_power_extent_index[8];

struct W8ItemInstance;

extern std::unique_ptr<ModalContent> g_modal_owner;
/* Replaces the owner. Safe from inside the current owner's ProcessInput():
   the replaced owner is destroyed after that call returns. */
void OpenModal(std::unique_ptr<ModalContent> owner);
/* Runs the current owner's input (g_modal_owner must be set). Returns true when
   it closed, resuming the parked dialog if any; false when it stays open or
   was replaced by another owner opened from its own callback. */
bool ProcessModalOwner();
void RetireModal(std::unique_ptr<ModalContent> owner, bool completed);
extern std::unique_ptr<ModalContent> g_pending_main_game_dialog;

/* Open the assay (item info) dialog for an item, evaluated against the party
   slot's character; -1 means no character. The current modal owner, if any,
   moves to the pending slot. */
void OpenMonsterInfoDialog(int location_id);
void OpenAssayDialog(W8ItemInstance* item, int character_slot);

void OnQuitGameDialogClosed(bool accepted);
void OnLeaveGameConfirmClosed(bool accepted);

void PauseMainGameWorld(void);
void ResumeMainGameWorld(void);
void ResetMainGameScreenState(void);

void RefreshTrackedPortraitOverlay(void);
/* Which party portrait the pointer is over, if any. */
void ClearCombatSelection(void);
void UpdateWorldViewCursor(const UiPointerEvent& event, W8TargetNeed target_needed);
void RequestRefreshPartyState(void);
void RefreshFlaggedMainGameState(void);
bool IsScreenIdle(void);
bool IsModalOpen(void);

void RequestRedraw(unsigned int mask);
void ApplyMainGameRedrawFlags(void);
void DrawMainGameScreen(void);
void CancelMouselook();
void SetTooltipSubject(W8PortraitHighlightKind kind, int subject);
int IsScreenInputBlocked(void);
void DisableCombatRegions(void);

extern bool g_radar_panel_shown;
extern bool g_action_panel_shown;
extern bool g_formation_panel_shown;
extern bool g_mouselook_active;
extern bool g_mouselook_left_held;
extern bool g_node_cull_pending;
enum W8MainGameMode {
    W8_MAIN_GAME_DEFAULT = 0,
    W8_MAIN_GAME_NPC_DIALOGUE = 3,
    W8_MAIN_GAME_PORTRAIT_REFRESH = 4,
    W8_MAIN_GAME_MODAL = 5,
    W8_MAIN_GAME_HIGHLIGHT_OVERLAY = 6
};

extern W8MainGameMode g_main_game_mode;
extern int g_selected_party_slot;
int GetSelectedPartySlot(void);
void RequestLevelTransition(int level, int entry, unsigned char flag);
extern bool g_build_level_links;
extern int g_next_link_level;
extern bool g_navigator_position_changed;
void BeginLevelTransition(void);
void SetViewportMode(int mode);
/* Apply a main-game UI mode (0=portraits, 1=formation, 2=radar): drop raised
   panels, optionally re-raise them from settings prefs, refresh tooltip and
   region state, and sync both settings and the level-block mode field. */
void ApplyMainGameModeFlag(W8MainUiMode mode, bool enable);
unsigned char ProcessMainGameInput(void);
void TickAmbientFollowUpIdle(unsigned char input_handled);
/* Re-sync the eight party slots' region sets and portrait hit
   regions with occupancy, the monster-entry flag and the display mode; the
   party add/remove entries and the keyboard menu's close run it. */
void RefreshPartySlotRegions(void);
/* Re-sync mouse hotspot, region enables and viewport after a
   main-game mode change. */
void SyncMainGameModeInput(void);
void ClearHighlightOverlayRegion(void);
void DismissHighlightOverlay(void);
/* The portrait-hover panel's producer; the four hover entry
   points hand it the slot, a content row count and a minimum plate width. */
void DrawHighlightOverlay(unsigned int party_slot, int row_count, unsigned int min_width);
void DrawPortraitVitalsOverlay(int party_slot);
void DrawPortraitConditionOverlay(int party_slot);
void DrawPortraitStatusOverlay(int party_slot);
void DrawPortraitEnchantmentOverlay(int party_slot);
void SelectPartyCharacter(int party_slot);
void RefreshLockInteractionControls(void);
void RefreshMainGameActionPanel(void);
void OpenAutomapScreen(void);
/* Clear one slot's pending portrait refresh while the screen is not in
   portrait mode, and disable that slot's portrait region set. */
void ClearPortraitRefreshSlot(int slot);
/* Clear whatever the screen was waiting on and open the options screen. */
// FUNCTION: WIZ8 0x00565970
inline void ClearScreenWait(void)
{
    g_pending_screen_state.mode = 0;
    SetPendingScreenState(W8_SCREEN_OPTIONS);
}
/* Portrait condition / enchantment orbs (help 25 / 26): hold opens the
   mode-6 hover overlay; leave and release tear it down. */
UiResult PortraitConditionOrbInput(const UiPointerEvent& event, HitTarget& target, int slot);
UiResult PortraitEnchantmentOrbInput(const UiPointerEvent& event, HitTarget& target, int slot);
/* Party portrait hit regions: select, target, open camp, and drag-hover. */
UiResult PortraitSelectInput(const UiPointerEvent& event, HitTarget& target, int slot);
/* Help 28: portrait side bar that opens Assay on the hovered weapon/item. */
UiResult PortraitAssaySidebarInput(const UiPointerEvent& event, HitTarget& target, int slot);
/* Help 24: portrait overlay hover strip; drives portrait_overlay_party_slot. */
UiResult PortraitOverlayHoverInput(const UiPointerEvent& event, HitTarget& target, int slot);
/* Region set 4: the eight combat-action hit regions beside the portraits. */
UiResult PartyCombatActionInput(const UiPointerEvent& event, HitTarget& target, int slot);
/* Help 31: radar-map button beside the text area. */
UiResult RadarMapButtonInput(const UiPointerEvent& event);
/* Region 23: the 3D world view. Hover refreshes the combat selection/target,
   left-up runs the targeting/item/monster dispatch, right-down opens monster
   info or the assay dialog, and the mouselook latch arms and releases here. */
UiResult WorldViewInput(const UiPointerEvent& event);
/* Help 36: combat monster-list hit rows beside the radar map. */
UiResult MonsterListInput(const UiPointerEvent& event);
void SetMainGameMode(W8MainGameMode mode);
void SetFormationBoardVisible(bool visible);
void ToggleMainGamePause(void);
/* The numbered action-key space IsMGSActionKeyEnabled, RunMGSActionKey and
   TryMGSActionKey share: the interface commands map to views and recorded
   actions, the combat commands map to ChooseAction selections, and
   W8_MGS_ACTION_REPEAT re-dispatches the slot's queued action. */
enum W8MGSAction {
    W8_MGS_ACTION_JOURNAL = 0,
    W8_MGS_ACTION_USE_ITEM_VIEW = 1,
    W8_MGS_ACTION_USE_RECORDED_ITEM = 2,
    W8_MGS_ACTION_SPELL_VIEW = 3,
    W8_MGS_ACTION_CAST_RECORDED_SPELL = 4,
    W8_MGS_ACTION_BREATHE = 5,
    W8_MGS_ACTION_BREATH_ATTACK = 6,
    W8_MGS_ACTION_ATTACK = 7,
    W8_MGS_ACTION_BERSERK = 8,
    W8_MGS_ACTION_TURN_UNDEAD = 9,
    W8_MGS_ACTION_PRAY = 0xa,
    W8_MGS_ACTION_DEFEND = 0xb,
    W8_MGS_ACTION_PROTECT = 0xc,
    W8_MGS_ACTION_EQUIP = 0xd,
    W8_MGS_ACTION_WALK = 0xe,
    W8_MGS_ACTION_RUN = 0xf,
    W8_MGS_ACTION_REPEAT = 0x10
};

void TryMGSActionKey(W8MGSAction command);

bool IsMGSActionKeyEnabled(unsigned short command);
void RunMGSActionKey(unsigned short command);
void LoadMainGameCursorResources(void);
short GetMainGameViewportMode(void);
void CloseMainGameOverlays(void);
/* Overlay entry points share the radar-mode fallback. The caller captures and
   stores its return mode before or after this operation as its own path requires. */
inline void SetMainGameOverlayViewport(W8MainUiMode mode)
{
    if (mode == W8_MAIN_UI_MODE_RADAR) {
        ApplyMainGameModeFlag(W8_MAIN_UI_MODE_FORMATION, false);
    } else {
        SetViewportMode(GetMainGameViewportMode());
    }
}

void SetRadarMapVisible(bool visible);
void SetActionPanelVisible(bool visible);
void OpenCharacterScreenForPartySlot(unsigned int party_slot, bool flag);
void RebuildNpcTradeItemList(bool scroll_to_top);
/* The trade-stock index behind a visible NPC item row. */
int ResolveNpcTradeStockIndex(int index);
/* Append one NPC stock item's name and price lines to the trade
   text box. */
void ShowNpcTradeItemNotice(W8ItemInstance* item);
/* Refill the trade text box from the pending item pool or the NPC
   stock, then re-enable the filter buttons. */
void PopulateNpcTradeList(void);
/* Validate the pending trade selection; queues a refusal quote and
   fails when the NPC declines the item or the party cannot pay. */
bool ValidateNpcTradeSelection(void);
/* Run one NPC trade offer; the result selects the accepted,
   refused or offended script path. */
bool AttemptNpcItemTrade(W8ItemInstance* item, unsigned char quantity, int index);
/* Destroy callback on the NPC trade split dialog; commits the
   chosen count to the editor slot and refreshes the trade selection. */
void NpcTradeSplitDialogResult(int count);
void RefreshFormationPanel(bool show_portraits);
void EndLockInteractMode(char suspend);
void UpdateMainGameScreen(void);
void EndTrapInteractMode(char suspend);
int GetPartySlotLocksTrapsLevel(int slot);
int OpenLockInteraction(Trigger* trigger);
int OpenTrapInteraction(Trigger* trigger);
/* The interaction exists but is suspended while a spell or item targets it. */
bool LockInteractionSuspended();
bool TrapInteractionSuspended();
/* When a slot's committed action cannot execute, re-choose a
   fallback hand, breath or character attack, or reroute spell/item aiming. */
void FallbackFromUnreachableAction(int party_slot);
void SetCombatAction(int value);
void SetCombatSelection(int value);
void SetCombatTarget(int value);

void RequestRedrawCombatBar(void);
void UpdateScreenOverlays(int frame);
bool LoadCurrentLevelData(void);
void ResetMainGameMode(void);
void CreateSurpriseFade(void);
void ReverseSurpriseFade(void);
void DestroySurpriseFade(void);
unsigned char UpdateSurpriseFade(void);
unsigned char OpenUseItemSelectView(int slot);

unsigned char MainGameScreenEnter(void);
void MainGameScreenFrame(void);
unsigned char MainGameScreenLeave(int leaving);
void ShowMainGameNoticeLine(std::string_view text, std::function<void(bool)> callback,
                            bool confirmation, bool cancel);

unsigned char HandleWorldResumeInput(const InputEvent* event);
void SetWorldResumeInput(bool enabled);
void SelectNpcBuyMode(void);
void ConfirmNpcTradeItem(void);
void RestockNpcTradeStock(void);
void OpenNpcTradeQuantityDialog(void);

#pragma once
#include <array>
#include <bitset>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <SDL3/SDL_keycode.h>

#include "wiz8/audio_resources.h"
#include "wiz8/design_rect.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_screens/MGSButtons.h"
#include "wiz8/local_screens/npc_dialogue_panel.h"
#include "wiz8/modal_content.h"
#include "wiz8/platform/input.h"
#include "wiz8/platform/timer.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/screen.h"
#include "wiz8/ui_input.h"

class PartyMovementPanel;
struct PortraitBandPaint;
struct RepaintedPortraitBands;
struct W8TargetingPresentation;
class RadarMap;
class KeyboardActionMenu;
class Pointer;
class Trigger;
class W8LockInteraction;
class W8TrapInteraction;
class W8UseItemView;
struct W8LevelRuntimeBlock;
struct W8SpellCastingView;

/* The world view with its HUD, portraits, text box and overlays. The level
   block, cursor resources and HUD are created on first entry and survive the
   camp, options, automap, journal and character screens opened over it. */
struct MainGameScreen final : Screen {
    static constexpr W8ScreenId id = W8_SCREEN_MAIN_GAME;

    MainGameScreen(Game& game, Pointer& pointer);
    ~MainGameScreen() override;
    bool enter(const ScreenRequest& request) override;
    void frame() override;
    void resetStepClock() override { step_timer.restart(); }
    bool leave(ScreenDeparture departure) override;

    /* Open the spell or use-item picker for a party slot, replacing the other one.
       `on_return` runs once the picker closes with a choice made or cancelled. */
    bool openSpellPicker(int party_slot, std::move_only_function<void()> on_return = {});
    bool openItemPicker(int party_slot, std::move_only_function<void()> on_return = {});
    // Open the spell picker for the selected character with one spell preselected.
    void beginSpellCast(int spell_id, std::move_only_function<void()> on_return);
    // Discard the picker, then run its opener's return.
    void closeSpellPicker();
    void closeItemPicker();
    // Abandon overlays for a replacement or screen/combat transition.
    void dismissOverlays(bool suspend_interaction);
    // Discard the picker without returning to its opener.
    void dismissSpellPicker();
    void dismissItemPicker();
    /* Decide the layout from the open interaction and the player's preferences, and
       apply it once. Only the panels and viewport that change are touched; an
       unchanged layout keeps its panels, hover and tooltip state. */
    void refreshLayout();
    // Store the player's preferred mode and apply the layout it gives.
    void setPreferredMode(W8MainUiMode mode);
    /* Drop the formation board, radar map and bottom panel while the screen is
       covered; the next refreshLayout raises what its layout wants. */
    void closeOverlays();
    /* Re-sync the party slots' portrait and edge hit regions with occupancy, the
       keyboard menu and the shown mode. */
    void refreshPartySlotRegions();
    // Raise or lower one panel of the formation layout, as the layout arrows do.
    void toggleRadarMap();
    void toggleBottomPanel();
    void toggleFormationBoard();
    // Step the preferred mode forward (1) or back (-1), as the layout keys do.
    void stepLayoutMode(int step);
    // Build the roof mode buttons and the layout arrows.
    void createLayoutButtons();
    // Restate the roof buttons and layout arrows from the applied layout.
    void updateLayoutButtons();
    // The layout the panels, viewport and input currently follow.
    const MainGameLayout& layout() const { return applied_layout; }

    /* A frame updates what it shows, then paints it. The update brings the party's
       facing, the targeting presentation, panel and button state and the repaint
       record in line with the game; the draw paints the recorded parts and changes
       no state. */
    W8TargetingPresentation updatePresentation();
    void draw(const W8TargetingPresentation& presentation);
    /* Lay the tracked portrait plate out again when the draw repaints what it sits
       over; the draw paints the laid-out plate. */
    void updatePortraitPlate();
    void paintPortraitPlate() const;

    // The game this main game belongs to; it is discarded before the game ends.
    Game& game;
    bool world_pick_enabled = true;
    std::unique_ptr<KeyboardActionMenu> keyboard_menu;
    // The radar map panel, present exactly while the applied layout shows it.
    std::unique_ptr<RadarMap> radar_map;
    bool keyboardMenuForSlot(int slot) const;
    void openKeyboardMenu(int slot);
    void closeKeyboardMenu();
    void chooseKeyboardMenuAction(int slot, int menu, int item);
    // The pointer the main game aims with, shared with a camp opened over it.
    Pointer& pointer;
    // The time since the screen last stepped the game.
    LapTimer step_timer;
    std::unique_ptr<W8LockInteraction> lock_interaction;
    std::unique_ptr<W8TrapInteraction> trap_interaction;
    bool lock_active = false;
    bool trap_active = false;
    /* The open spell or use-item picker. Closing one returns to whatever opened it;
       leaving the screen discards it without that return. */
    std::unique_ptr<W8SpellCastingView> spell_picker;
    std::unique_ptr<W8UseItemView> item_picker;
    bool resume_world = false;
    /* The camp closed over a conversation set aside to open it; the next frame takes the
       conversation up again. */
    bool resume_conversation = false;
    /* The slot whose NPC the camp just dismissed; its portrait ignores input
       until the screen state is reset. */
    int dismissed_npc_slot = -1;
    /* An item the camp's Use action handed to the use-item picker, and the member who
       uses it. Until the picker opens on it, the game's aging is held. */
    struct ItemDrag {
        W8ItemLocation item;
        int actor = -1;
        bool pending = true;
    };
    std::optional<ItemDrag> item_drag;
    // The formation panel is open.
    bool formation_panel_open = false;
    // The combat party-movement panel, while it is shown.
    std::unique_ptr<PartyMovementPanel> party_movement;
    /* The HUD's presentation state; created on the first entry and released when the
       screen is discarded. */
    std::unique_ptr<W8LevelRuntimeBlock> hud;
    MainGameHudButtons hud_buttons;
    std::unique_ptr<MainGameSubMenu> submenu;
    // The NPC dialogue's interface: its layouts, trade table and quote bubble.
    W8NpcDialoguePanel dialogue{*this};
    UiLayer combat{UiLayerOrder::Combat}, edges{UiLayerOrder::ScreenEdges},
        radar{UiLayerOrder::Radar}, formation{UiLayerOrder::FormationBase},
        world{UiLayerOrder::GameplayOverlays};
    std::array<std::unique_ptr<UiLayer>, 8> portraits;
    std::array<std::array<HitTarget*, 6>, 8> portrait_targets{};
    std::array<HitTarget*, 8> combat_targets{}, edge_targets{};
    HitTarget *radar_target = nullptr, *monster_list = nullptr, *world_view = nullptr;

private:
    // Advance choices and commit them from their owner, not from a retiring view.
    void updateSpellPicker();
    void updateItemPicker();
    MainGameLayout applied_layout;

    void chooseRoofMode(W8MainUiMode mode);
    void showRadarMap(bool visible);
    void showFormationBoard(bool visible);
    void syncInput(const MainGameLayout& layout);

    /* The presentation update's button sync: bank buttons, scroll arrows,
       panel/options/stance/roof buttons and the layout arrows. */
    void updateHudButtons();
    /* The draw's per-frame button refresh: every changed button except the
       layout arrows, which the layout repaint draws. */
    void refreshHudButtons();
    void paintRequested(const W8TargetingPresentation& presentation,
                        RepaintedPortraitBands& bands) const;
    /* The widgets over the portrait bands: the given combat side-strip portraits,
       the level-advance buttons (forced over repainted bands), and the condition
       icons that were requested or whose band was repainted. */
    void paintCombatPortraits(std::bitset<8> slots) const;
    void refreshPortraitAdvanceButtons(std::bitset<8> repainted) const;
    void paintConditionIcons(std::bitset<8> requested, std::bitset<8> repainted_bands) const;
    // A party slot's next animated portrait frame, as the portrait animation asked.
    PortraitBandPaint paintPartyPortraitFrame(unsigned int party_slot) const;
    /* Whether painting the slot's requested animation frame repaints its complete
       band too, as it does for a character dead in combat, surprised or missing. */
    bool partyPortraitFrameCoversBand(unsigned int party_slot) const;
};
/* The main game screen, current or suspended, or null. */
inline MainGameScreen* MainGame()
{
    return FindScreen<MainGameScreen>();
}
// The main game's HUD state, or null while there is no main game.
W8LevelRuntimeBlock* MainGameHud();
// The main game exists with its formation panel open.
bool FormationPanelOpen();
// The main game exists with its combat party-movement panel shown.
bool PartyMovementPanelShown();
void EnableMainGamePortraitSlot(unsigned int slot);
void DisableMainGamePortraitSlot(unsigned int slot);

void RedrawCombatMonsterList(bool subject_valid);
void RefreshSelectedPartyPortrait(unsigned int party_slot);
void ClearHighlightIfItIs(const int* item);

extern W8MainGameResourceSlot g_main_game_resource_slots[17];
extern DesignRect g_viewport_modes[];
/* String-list ids naming each trap row; Traps.cpp indexes it with the
   trigger's trap type for the disarm/spring notices. */
extern unsigned short g_trap_name_string_ids[];
#include "wiz8/layouts/npc_dialogue.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/RangeControl.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/ui_layer.h"

class Game;

class W8TrapInteraction;

class W8MainGameTextKeyHandler {
public:
    W8MainGameTextKeyHandler();
    void Redraw(SDL_Surface& target, bool full_redraw);
    void SetSelectedLine(int line);
    signed char HandleKey(SDL_Keycode key);
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
    /* Enable the unselected cells the selected trap row has a device for. */
    void RefreshCellEnables();
    /* Show m_values' inspected devices, blanking cells with nothing to show. */
    void RefreshCellImages();
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
    W8TrapInteraction* m_screen;
    int* m_values;
    bool m_progress_display; /* the timed progress text block is displayed */
    float m_progress_duration;
    float m_progress_elapsed;
    int m_progress_drawn; /* widest progress extent drawn so far */
    W8GameTimer m_timer0 = W8GameTimer::presentation(1.0f);
    W8ControlsRect m_text_bounds;
    W8TextBuffer m_text_buffer;
    W8GameTimer m_timer1 = W8GameTimer::presentation(1.0f);
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

// A view of the owning game's notebook, not a second keyword collection.
class W8NpcDialogueTextController {
public:
    explicit W8NpcDialogueTextController(Game& game);
    void Redraw(SDL_Surface& target, bool full_redraw);
    void InvalidateAll() { m_dirty = true; }
    void InvalidateLayout() { m_dirty = true; }
    UiLayer input{UiLayerOrder::LockInput};
    HitTarget* expanded_target = nullptr;
    bool m_active = false, m_dirty = true;
    bool HandleScrollDownCommand(bool check_only);
    bool HandleScrollUpCommand(bool check_only);
    void MarkNewKeyword(std::string_view text);
    bool IsSlotPortraitTranscriptCovered(unsigned int party_slot);
    void RefreshNotebook();
    void ShowTranscriptEntries();
    void HighlightFirstEntry();
    void ClearHover();
    void ClearTranscriptEntries();
    bool showing_notebook = false;
    int GetSelectedTranscriptEntryIndex();
    void RemoveSelectedTranscriptEntry();
    void Expand();
    void Collapse();
    void ClearBackground();
    bool IsExpanded();
    int visible;
    int line_height;
    int margin;
    int scroll_height;
    void SelectTranscriptKeywordAtPoint(int x, int y);

private:
    std::vector<std::size_t> VisibleKeywords() const;
    std::optional<std::size_t> HitKeyword(int y) const;
    void PaintKeywords(SDL_Surface& target);
    Game& game;
    W8ControlsRect bounds{};
    std::size_t first = 0;
    std::optional<std::size_t> hovered, selected;
    std::unordered_set<std::string> marked;
};

class W8LockInteraction;

struct W8LockTumbler {
    SDL_Rect bounds{};
    bool m_pin_set = false, m_rising = false, m_falling = false, m_at_top = false;
    bool m_hovered = false, m_enabled = true, m_dirty = true;
    int m_pin_index = 0, m_pin_height = 0x22;
    void Redraw(SDL_Surface& target, bool full_redraw, int phase);
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
    W8GameTimer m_phase_timer = W8GameTimer::presentation(0.04f);
    W8GameTimer m_rise_timer = W8GameTimer::presentation(0.03f);
    W8GameTimer m_fall_timer = W8GameTimer::presentation(0.01f);
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

enum W8LockInteractionState {
    W8_LOCK_STATE_IDLE = 0,
    W8_LOCK_STATE_PICK_TUMBLER = 1,
    W8_LOCK_STATE_FORCE = 2,
    W8_LOCK_STATE_PREPARE_SPELL = 3,
    W8_LOCK_STATE_RELEASE_FOR_SPELL = 4,
    W8_LOCK_STATE_CANCEL = 5,
    W8_LOCK_STATE_RELEASE_FOR_ITEM = 6,
    W8_LOCK_STATE_CHECK_PICK_SUCCESS = 7,
    W8_LOCK_STATE_WAIT_FOR_UNLOCK = 8,
    W8_LOCK_STATE_EXIT = 9
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
    /* Re-derive the control enables for the selected character: tumblers
       need lockpick skill, the spell button a castable knock-knock and the
       force button a force chance. The info text and actions repaint. */
    void RefreshControls();
    void EnablePanels(int enable);
    void BeginUnlock();
    /* Knock-knock resolution: rolls the per-level chance over a shuffled pin
       order, raising (or on backfire dropping) them, then re-derives the
       control enables and enters state 7. The flag parameter is unused. */
    void ApplyKnockKnock(int level, int flag, signed char backfire);

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
    W8LockInteractionState m_state;      /* Process() state */
    int m_tumbler_owner[8];            /* owning party slot per pin, -1 unset */
    unsigned char m_tumbler_locked[8]; /* pin kept when the slot is released */
    int m_slot_attempts[8];            /* pick attempts per party slot */
    W8GameTimer m_timer = W8GameTimer::presentation(1.0f); /* state-8 completion delay */
};

enum W8TrapInteractionAction {
    W8_TRAP_ACTION_EXIT = 0,
    W8_TRAP_ACTION_CAST_SPELL = 1,
    W8_TRAP_ACTION_TRIGGER = 2,
    W8_TRAP_ACTION_USE_ITEM = 3,
    W8_TRAP_ACTION_DISARM = 4
};

enum W8TrapDisarmState {
    W8_TRAP_DISARM_IDLE = 0,
    W8_TRAP_DISARM_INSPECT_SUCCESS = 1,
    W8_TRAP_DISARM_COLUMN_SUCCESS = 2,
    W8_TRAP_DISARM_COMPLETE = 3,
    W8_TRAP_DISARM_SPRUNG = 4,
    W8_TRAP_DISARM_CAST_SPELL = 5,
    W8_TRAP_DISARM_USE_ITEM = 6,
    W8_TRAP_DISARM_KNOCK_KNOCK_SUCCESS = 7,
    W8_TRAP_DISARM_KNOCK_KNOCK_FAILURE = 8,
    W8_TRAP_DISARM_WAIT_FOR_TRAP = 9,
    W8_TRAP_DISARM_EXIT = 10
};

class W8TrapInteraction {
public:
    W8TrapInteraction(Trigger* owner);
    void SelectTextEntry(int index);
    void OnPrimary(W8TrapInteractionAction action);
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
    W8TrapDisarmState m_disarm_state;
    int m_selected_character;
    std::array<std::unique_ptr<W8ButtonWidget>, 5> m_action_controls;
    int m_device_id;
    int m_difficulty;
    unsigned char m_column_filled[8];
    unsigned char m_slot_attempted[8];
    int m_slot_columns[8][8];
    int m_target_difficulty;
    wiz8::sound::Playback m_sound_handle;
    W8GameTimer m_timer = W8GameTimer::presentation(1.0f);
};

W8TrapInteraction* TrapInteraction();
bool IsLockInteractionActive();
bool IsTrapInteractionActive();

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
void OpenAssayDialog(const W8ItemInstance& item, int character_slot);

void OnQuitGameDialogClosed(bool accepted);
void OnLeaveGameConfirmClosed(bool accepted);

// Show that the world was held or released, or that its time stopped or resumed.
void PresentWorldAdvancement(bool held, bool hold_changed);

/* Which party portrait the pointer is over, if any. */
void ClearCombatSelection(void);
void UpdateWorldViewCursor(const UiPointerEvent& event, W8TargetNeed target_needed);
void RequestRefreshPartyState(void);
void RefreshFlaggedMainGameState(void);
bool IsScreenIdle(void);
bool IsModalOpen(void);

void CancelMouselook();
void SetTooltipSubject(W8PortraitHighlightKind kind, int subject);
bool IsScreenInputBlocked(void);

extern bool g_mouselook_active;
extern bool g_mouselook_left_held;
enum W8MainGameMode {
    W8_MAIN_GAME_DEFAULT = 0,
    W8_MAIN_GAME_NPC_DIALOGUE = 3,
    W8_MAIN_GAME_PORTRAIT_REFRESH = 4,
    W8_MAIN_GAME_MODAL = 5,
    W8_MAIN_GAME_HIGHLIGHT_OVERLAY = 6
};

extern W8MainGameMode g_main_game_mode;
void RequestLevelTransition(int level, int entry, unsigned char flag);
// Change to `level`, arriving at `entry` (-1 for a saved location).
void BeginLevelTransition(int level, int entry);
bool ProcessMainGameInput(void);
void TickAmbientFollowUpIdle(unsigned char input_handled);
/* Forget the shown plate and, while the overlay mode is up, have the next draw
   erase its rectangle and repaint the roof or text box it crossed. */
void ClearHighlightOverlayRegion(void);
void DismissHighlightOverlay(void);
void SelectPartyCharacter(int party_slot);
void RefreshMainGameActionPanel(void);
void OpenAutomapScreen(void);
/* Clear one slot's pending portrait refresh while the screen is not in
   portrait mode, and disable that slot's portrait region set. */
void ClearPortraitRefreshSlot(int slot);
/* Clear whatever the screen was waiting on and open the options screen. */
inline void ClearScreenWait(void)
{
    RequestScreen({.id = W8_SCREEN_OPTIONS, .mode = W8_OPTIONS_DEFAULT});
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
void OpenCharacterScreenForPartySlot(unsigned int party_slot, bool flag);
void RebuildNpcTradeItemList(bool scroll_to_top);
/* The trade-stock index behind a visible NPC item row. */
/* Append one NPC stock item's name and price lines to the trade
   text box. */
void ShowNpcTradeItemNotice(W8ItemInstance* item);
/* Refill the trade text box from the pending item pool or the NPC
   stock, then re-enable the filter buttons. */
void PopulateNpcTradeList(void);
/* Validate the pending trade selection; queues a refusal quote and
   fails when the NPC declines the item or the party cannot pay. */
/* Run one NPC trade offer; the result selects the accepted,
   refused or offended script path. */
// Redraw the formation panel when asked or when one of its buttons changed.
void RefreshFormationPanel(bool force);
void EndLockInteractMode(bool suspend);
void UpdateTrapInteraction(void);
void EndTrapInteractMode(bool suspend);
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

void ResetMainGameMode(void);
void CreateSurpriseFade(void);
void ReverseSurpriseFade(void);
void DestroySurpriseFade(void);
unsigned char UpdateSurpriseFade(void);

void ShowMainGameNoticeLine(std::string_view text, std::function<void(bool)> callback,
                            bool confirmation, bool cancel);

bool HandleWorldResumeInput(const InputEvent* event);
void SetWorldResumeInput(bool enabled);
void SelectNpcBuyMode(void);
void ConfirmNpcTradeItem(void);
void RestockNpcTradeStock(void);
void OpenNpcTradeQuantityDialog(void);

/* The action the slot is editing: the open picker's draft for the selected slot, otherwise
   its stored in-combat or out-of-combat action. Out of combat with no picker open the
   player is sighting: aiming the executing action without needing a particular target. */
W8Action* EditedDraft(int party_slot);
W8Action EditedAction(int party_slot);
bool IsSighting(int party_slot);

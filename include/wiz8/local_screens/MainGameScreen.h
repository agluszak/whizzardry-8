#pragma once

#include <cstdint>
#include "wiz8/audio_resources.h"

#include <memory>
#include <string_view>
#include <array>
#include <optional>
#include "wiz8/regions.h"

#include "input.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "compat/compiler.h"
#include "layouts/targeting.h"

class Trigger;

extern ScreenRegionSet g_main_party_controls;
extern ScreenRegionSet g_main_screen;
extern ScreenRegionSet g_main_dialogue;
extern ScreenRegionSet g_main_combat_actions;
extern ScreenRegionSet g_main_portrait_controls;
extern ScreenRegionSet g_main_conditions;
extern ScreenRegionSet g_main_portrait_regions;
extern ScreenRegionSet g_main_portrait_edge_regions;
extern ScreenRegionSet g_main_radar_regions;
extern ScreenRegionSet g_main_formation_board_regions;
extern ScreenRegionSet g_main_submenu_regions;
extern ScreenRegionSet g_main_effect_regions;
enum MainEffectRegionOffset {
    kMonsterListRegionOffset = 24,
    kWorldViewRegionOffset = 25
};
extern std::array<std::optional<RegionSetView>, 8> g_main_portrait_slot_views;
extern std::optional<RegionSetView> g_main_portrait_edge_view;
void EnableMainGamePortraitSlot(std::uint32_t slot);
void DisableMainGamePortraitSlot(std::uint32_t slot);
struct W8ScreenRect;

void RequestRedrawParty(void);
void RedrawCombatMonsterList(void);
void RefreshSelectedPartyPortrait(std::uint32_t party_slot);
void ClearHighlightIfItIs(const std::int32_t* item);

#include "wiz8/layouts/main_game_screen.h"

extern W8MainGameResourceSlot g_main_game_resource_slots[17];
extern W8ScreenRect g_viewport_modes[];
extern const float g_float_one_fiftieth;
extern const float g_float_half_turn_degrees;
/* String-list ids naming each trap row; Traps.cpp indexes it with the
   trigger's trap type for the disarm/spring notices. */
extern std::uint16_t g_trap_name_string_ids[];
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_code/Widget.h"
#include "wiz8/local_code/TextControl.h"
#include "wiz8/local_code/RangeControl.h"
#include "wiz8/engine_code/game_timer.h"
#include "wiz8/local_code/Controls.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/dialog_code/DialogTextArea.h"

class W8MainGameScreen;

// VTABLE: WIZ8 0x005eeafc
class W8MainGameTextKeyHandler : public W8Widget, public W8RangeListener {
public:
    W8MainGameTextKeyHandler(Controls* panel, std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom,
                             std::int32_t line_count, const std::uint16_t* line_string_ids);
    virtual void Redraw(bool full_redraw) override;
    virtual void OnMouseLeave(std::int32_t event) override;
    virtual void OnMouseMove(std::int32_t event) override;
    virtual void AdjustValue(std::int32_t steps) override;
    virtual void OnLeftButtonUp(std::int32_t event) override;
    void SetSelectedLine(std::int32_t line);
    virtual char HandleKey(std::uint16_t key);
    virtual void OnRangeChanged(W8RangeControl* control) override;

    W8RangeControl m_range;
    std::int32_t m_line_count;
    std::int32_t m_visible_lines;
    const std::uint16_t* m_line_string_ids;
    std::int32_t m_selected_line;
    std::int32_t m_hover_line;
    std::int32_t m_first_visible_line;
    W8RangeListener* m_range_listener;
};

/* One text-panel cell: the catalog image it shows (or -1) and a flag that
   gates mouse handling. */
// VTABLE: WIZ8 0x005eeb4c
class W8MainGameTextEntry : public W8TextControl {
public:
    W8MainGameTextEntry(Controls* panel, std::int32_t index);
    virtual void Redraw(bool full_redraw) override;
    virtual void OnMouseEnter(std::int32_t event) override;
    virtual void OnLeftButtonDown(std::int32_t event) override;
    virtual void OnLeftButtonUp(std::int32_t event) override;

    std::int32_t m_image;
    bool m_input_blocked;
};

// VTABLE: WIZ8 0x005eeba8
// VTABLE: WIZ8 0x005eeba0 W8TextControl::Listener
class W8MainGameTextPanel : public Controls,
                            public W8TextControl::Listener,
                            public W8RangeListener {
public:
    void BeginProgress(std::string_view text, float duration, float hold);

    W8MainGameTextPanel();
    virtual ~W8MainGameTextPanel();
    virtual void Redraw() override;
    virtual void OnPrimary(W8TextControl* control) override;
    virtual void OnSecondary(W8TextControl*) override {}
    virtual void OnRangeChanged(W8RangeControl* control) override;

    W8MainGameTextEntry* m_entries[8];
    W8MainGameTextKeyHandler* m_key_handler;
    std::int32_t m_selection;
    W8MainGameScreen* m_screen;
    std::int32_t* m_values;
    bool m_progress_display; /* the timed progress text block is displayed */
    float m_progress_duration;
    float m_progress_elapsed;
    std::int32_t m_progress_drawn; /* widest progress extent drawn so far */
    W8GameTimer m_timer0;
    W8ControlsRect m_text_bounds;
    W8TextBuffer m_text_buffer;
    W8GameTimer m_timer1;
    std::int32_t m_marker_anim_time; /* drives the 12-frame target-changed marker */
    bool m_target_changed;
    /* Draw the animated target-changed marker this frame; raised while
       m_fDirty, consumed by Redraw. */
    bool m_target_marker_pending;
};

// VTABLE: WIZ8 0x005eebc0
class W8MainGameStatusPanel : public Controls {
public:
    W8MainGameStatusPanel();
    virtual void Redraw() override;
    void RefreshStatusTexts();

    std::unique_ptr<W8TextBuffer> m_text;
    std::unique_ptr<W8TextBuffer> m_text0;
    std::unique_ptr<W8TextBuffer> m_text1;
    std::unique_ptr<W8TextBuffer> m_text2;
    std::unique_ptr<W8TextBuffer> m_text3;
    std::unique_ptr<W8TextBuffer> m_text4;
    std::unique_ptr<W8TextBuffer> m_text5;
    std::int32_t m_target;
};

/* The NPC dialogue keyword transcript. */
// VTABLE: WIZ8 0x005ee920
class W8NpcDialogueTextController : public Controls {
public:
    W8NpcDialogueTextController(std::int32_t panel_left, std::int32_t panel_top, std::int32_t panel_right, std::int32_t panel_bottom,
                                CatalogArtwork artwork, std::int32_t background_image,
                                std::int32_t margin_image, std::int32_t line_image);
    virtual void Redraw() override;
    bool HandleScrollDownCommand(bool check_only);
    bool HandleScrollUpCommand(bool check_only);
    /* Add one keyword line to the transcript unless the text is already
       present; a nonzero mark puts the new entry in state 0x60, and any
       leftover "[No Keywords]" placeholder is removed afterwards. */
    bool AddTranscriptEntry(std::string_view text, std::int8_t category, char mark);
    /* Whether the expanded transcript's top edge reaches above the portrait
       band for an odd party slot (1 -> 0x67, 3 -> 0xbc, 5 -> 0x111, 7 ->
       always covered). Callers use it to skip portrait work on rows the
       open transcript covers. */
    bool IsSlotPortraitTranscriptCovered(std::uint32_t party_slot);
    /* Re-apply the category filter, rebuild the expansion and restate the
       scroll widgets. */
    void SetTranscriptCategoryFilter(std::int8_t category);
    /* Replay every saved dialogue_transcript record into the text area,
       adding the "[No Keywords]" placeholder and forcing the all filter
       when nothing was saved. */
    void RestoreTranscriptEntries();
    /* Snapshot the transcript lines into the screen state's
       dialogue_transcript records (text plus category byte). */
    void SaveTranscriptEntries();
    /* Drop every transcript line from the text area and invalidate. */
    void ClearTranscriptEntries();
    std::int32_t GetSelectedTranscriptEntryIndex();
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
    std::int32_t margin_image;
    std::int32_t line_image;
    std::int32_t visible;
    std::int32_t line_height;
    std::int32_t margin;
    std::int32_t scroll_height;
    W8DialogTextArea text_area;
    void SelectTranscriptKeywordAtPoint(std::int32_t x, std::int32_t y);
};

/* The hover/click target over the dialogue transcript. Entering selects the
   first visible entry, leaving clears the selection, and the button handlers
   track the press in m_flags before firing the widget callbacks. */
// VTABLE: WIZ8 0x005ee92c
class W8NpcDialogueScrollWidget : public W8Widget {
public:
    W8NpcDialogueScrollWidget(Controls* panel, std::uint32_t region, std::int32_t left, std::int32_t top, std::int32_t right,
                              std::int32_t bottom);

    virtual void OnMouseEnter(std::int32_t event) override;
    virtual void OnMouseLeave(std::int32_t event) override;
    virtual void OnLeftButtonDown(std::int32_t event) override;
    virtual void OnLeftButtonUp(std::int32_t event) override;

    std::uint32_t m_flags; /* bit 0 while the primary button is held */
};

class W8LockTumbler;

/* Notified by a W8LockTumbler when it is released. */
// VTABLE: WIZ8 0x005eeabc
class W8LockTumblerListener {
public:
    virtual void OnTumblerReleased(W8LockTumbler* tumbler) = 0;
};

/* One lock-picking pin. */
// VTABLE: WIZ8 0x005eea60
class W8LockTumbler : public W8Widget {
public:
    W8LockTumbler(Controls* panel, std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom, std::int32_t pin_index)
        : W8Widget(panel, 0xffffffff, left, top, right, bottom), m_pin_set(0), m_rising(0),
          m_falling(0), m_at_top(0), m_hovered(0), m_pin_index(pin_index), m_pin_height(0x22),
          m_listener(0)
    {
    }
    virtual void Redraw(bool full_redraw) override;
    virtual void OnMouseEnter(std::int32_t event) override;
    virtual void OnMouseLeave(std::int32_t event) override;
    virtual void OnLeftButtonDown(std::int32_t event) override;
    virtual void OnLeftButtonUp(std::int32_t event) override;

    bool m_pin_set; /* raised and holding */
    bool m_rising;  /* animating toward its target height */
    bool m_falling; /* dropping back to rest */
    bool m_at_top;  /* redrawn against the shared tumble phase */
    bool m_hovered;
    std::int32_t m_pin_index;  /* column into the pin pattern tables */
    std::int32_t m_pin_height; /* pixel offset, 0x22 at rest */
    W8LockTumblerListener* m_listener;
};

/* Notified by W8LockTumblerPanel with the index of a released tumbler. */
// VTABLE: WIZ8 0x005eeae0
class W8LockTumblerPanelListener {
public:
    virtual void OnTumblerPicked(std::int32_t index) = 0;
};

/* The tumbler strip of the lock interaction. Owns the eight tumblers and the
   three animation timers: the phase timer steps the shared sway, the rise
   timer moves a pin toward g_lock_pin_target_height, the fall timer returns it
   to rest. */
// VTABLE: WIZ8 0x005eeaac
class W8LockTumblerPanel : public Controls, public W8LockTumblerListener {
public:
    W8LockTumblerPanel(std::int32_t tumbler_count, const std::uint8_t* pin_data);

    virtual ~W8LockTumblerPanel();
    virtual void OnTumblerReleased(W8LockTumbler* tumbler) override;
    void UpdateTumblerAnimation();

    std::int32_t m_tumbler_count; /* pins in use, clamped to [2,8] */
    W8LockTumbler* m_tumblers[8];
    bool m_animating;          /* a pin is in flight; input is locked out */
    std::int32_t m_phase;               /* sway accumulator feeding g_lock_phase */
    W8GameTimer m_phase_timer; /* 0.04s */
    W8GameTimer m_rise_timer;  /* 0.03s */
    W8GameTimer m_fall_timer;  /* 0.01s */
    W8LockTumblerPanelListener* m_listener;
};

/* The lock interaction's readout column: the selected character's name plus
   the lockpick-skill, spell-power and force-chance percentages. */
// VTABLE: WIZ8 0x005eeac0
class W8LockInfoPanel : public Controls {
public:
    W8LockInfoPanel(std::int32_t tumbler_count);

    virtual void Redraw() override;
    void RefreshInfo();

    std::int32_t m_tumbler_count;
    std::unique_ptr<W8TextBuffer> m_text0; /* character name */
    std::unique_ptr<W8TextBuffer> m_text1;
    std::unique_ptr<W8TextBuffer> m_text2; /* lockpick skill */
    std::unique_ptr<W8TextBuffer> m_text3;
    std::unique_ptr<W8TextBuffer> m_text4; /* spell power */
    std::unique_ptr<W8TextBuffer> m_text5;
    std::unique_ptr<W8TextBuffer> m_text; /* force chance */
};

/* The lock-picking interaction. It answers the tumbler panel's "released
   tumbler N" callback and receives the action-panel buttons; Process() is the
   per-frame state machine ProcessLockInteractMode drives. */
// VTABLE: WIZ8 0x005eead8 W8LockTumblerPanelListener
// VTABLE: WIZ8 0x005eead0 W8TextControl::Listener
class W8LockInteraction : public W8LockTumblerPanelListener, public W8TextControl::Listener {
public:
    W8LockInteraction(Trigger* trigger);

    virtual void OnTumblerPicked(std::int32_t index) override;
    virtual void OnPrimary(W8TextControl* control) override;
    std::int32_t ReleaseOwnedTumblers(std::int32_t slot);
    void Process();
    void ResolvePick();
    void AttemptForce();
    void EnablePanels(std::int32_t enable);
    void BeginUnlock();
    /* Knock-knock resolution: rolls the per-level chance over a shuffled pin
       order, raising (or on backfire dropping) them, then re-derives the
       control enables and enters state 7. The flag parameter is unused. */
    void ApplyKnockKnock(std::int32_t level, std::int32_t flag, char backfire);

    Trigger* m_trigger;
    std::int32_t m_tumbler_count; /* trigger->lock_state.difficulty clamped to [2,8] */
    std::unique_ptr<W8LockTumblerPanel> m_tumbler_panel;
    std::unique_ptr<W8LockInfoPanel> m_info_panel;
    std::unique_ptr<Controls> m_action_panel;
    W8TextControl* m_done_button;      /* OnPrimary target, state 9 */
    W8TextControl* m_spell_button;     /* gated by spell-0x27 power */
    W8TextControl* m_force_button;     /* gated by the force chance */
    W8TextControl* m_cancel_button;    /* OnPrimary target, state 5 (cancel) */
    std::int32_t m_selected_slot;               /* party slot owning the raised pins */
    std::int32_t m_picked_tumbler;              /* index OnTumblerPicked recorded */
    std::int32_t m_state;                       /* Process() state */
    std::int32_t m_tumbler_owner[8];            /* owning party slot per pin, -1 unset */
    bool m_tumbler_locked[8]; /* pin kept when the slot is released */
    std::int32_t m_slot_attempts[8];            /* pick attempts per party slot */
    W8GameTimer m_timer;               /* state-8 completion delay */
};

// VTABLE: WIZ8 0x005eebdc
class W8MainGameTextSelectionListener {
public:
    virtual void SelectTextEntry(std::int32_t index) = 0;
};

// VTABLE: WIZ8 0x005eebd8 W8MainGameTextSelectionListener
// VTABLE: WIZ8 0x005eebd0 W8TextControl::Listener
class W8MainGameScreen final : public W8MainGameTextSelectionListener, public W8TextControl::Listener {
public:
    W8MainGameScreen(Trigger* owner);
    virtual void SelectTextEntry(std::int32_t index) override;
    virtual void OnPrimary(W8TextControl* control) override;
    virtual void OnSecondary(W8TextControl*) override {}
    void Update();
    void RefreshActionPanel();
    void EnablePanelRegionSets(bool enable);
    std::int32_t GetTrapInteractionChance() const;
    void ApplyInspectSuccess();
    void CastTrapSpell();
    void UseTrapItem();

    Trigger* m_owner;
    std::unique_ptr<W8MainGameTextPanel> m_text_panel;
    std::unique_ptr<W8MainGameStatusPanel> m_status_panel;
    std::unique_ptr<Controls> m_action_panel;
    std::int32_t m_disarm_state;
    std::int32_t m_selected_character;
    W8TextControl* m_action_controls[5];
    std::int32_t m_device_id;
    std::int32_t m_difficulty;
    std::uint8_t m_column_filled[8];
    std::uint8_t m_slot_attempted[8];
    std::int32_t m_slot_columns[8][8];
    std::int32_t m_target_difficulty;
    wiz8::sound::Playback m_sound_handle;
    W8GameTimer m_timer;
};

extern std::unique_ptr<W8LevelRuntimeBlock> g_level_block;
extern std::unique_ptr<W8MainGameScreen> g_main_game_screen;

/* Insanity (spell 0x3c) world-cursor extent rows and the per spell-power
   index into them; CastSpellFromSource scans the same extents when it
   places the insanity point. */
extern double g_world_cursor_extent_table[18];
extern std::int8_t g_spell_power_extent_index[8];

struct W8ItemInstance;

extern W8DialogBase* g_modal_owner;
void OpenModal(W8DialogBase* owner);
extern W8DialogBase* g_pending_main_game_dialog;

/* Open the assay (item info) dialog for an item, evaluated against the party
   slot's character; -1 means no character. The current modal owner, if any,
   moves to the pending slot. */
void OpenMonsterInfoDialog(std::int32_t location_id);
void OpenAssayDialog(W8ItemInstance* item, std::int32_t character_slot);

void OnQuitGameDialogClosed(W8DialogBase* dialog);
void OnLeaveGameConfirmClosed(W8DialogBase* dialog);

void PauseMainGameWorld(void);
void ResumeMainGameWorld(void);
void ResetMainGameScreenState(void);

void RefreshTrackedPortraitOverlay(void);
/* Which party portrait the pointer is over, if any. */
std::uint8_t HitTestPartyPortrait(const InputEvent* event);
void ClearCombatSelection(void);
void UpdateWorldViewCursor(const InputEvent* event, W8TargetNeed target_needed);
void RequestRefreshPartyState(void);
void RefreshFlaggedMainGameState(void);
bool IsScreenIdle(void);
bool IsModalOpen(void);

void RequestRedraw(std::uint32_t mask);
void ApplyMainGameRedrawFlags(void);
void DrawMainGameScreen(void);
void CancelMouselook();
void SetTooltipSubject(std::int32_t kind, std::int32_t subject);
bool IsScreenInputBlocked(void);
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
extern std::int32_t g_selected_party_slot;
std::int32_t GetSelectedPartySlot(void);
void RequestLevelTransition(std::int32_t level, std::int32_t entry, bool flag);
extern bool g_build_level_links;
extern std::int32_t g_next_link_level;
extern bool g_navigator_position_changed;
void BeginLevelTransition(void);
void SetViewportMode(std::int32_t mode);
/* Apply a main-game UI mode (0=portraits, 1=formation, 2=radar): drop raised
   panels, optionally re-raise them from settings prefs, refresh tooltip and
   region state, and sync both settings and the level-block mode field. */
void ApplyMainGameModeFlag(W8MainUiMode mode, bool enable);
bool ProcessMainGameInput(void);
void TickAmbientFollowUpIdle(bool input_handled);
/* Re-sync the eight party slots' region sets and portrait hit
   regions with occupancy, the monster-entry flag and the display mode; the
   party add/remove entries and the keyboard menu's close run it. */
void RefreshPartySlotRegions(void);
/* Re-sync mouse hotspot, region enables and viewport after a
   main-game mode change. */
void SyncMainGameModeRegions(void);
void ClearHighlightOverlayRegion(void);
void DismissHighlightOverlay(void);
/* The portrait-hover panel's producer; the four hover entry
   points hand it the slot, a content row count and a minimum plate width. */
void DrawHighlightOverlay(std::uint32_t party_slot, std::int32_t row_count, std::uint32_t min_width);
void DrawPortraitVitalsOverlay(std::int32_t party_slot);
void DrawPortraitConditionOverlay(std::int32_t party_slot);
void DrawPortraitStatusOverlay(std::int32_t party_slot);
void DrawPortraitEnchantmentOverlay(std::int32_t party_slot);
void SelectPartyCharacter(std::int32_t party_slot);
void RefreshLockInteractionControls(void);
void EnableLockInteractionPanels(void);
void RefreshMainGameActionPanel(void);
void EnableTrapInteractionPanelRegions(void);
void OpenAutomapScreen(void);
/* Clear one slot's pending portrait refresh while the screen is not in
   portrait mode, and disable that slot's portrait region set. */
void ClearPortraitRefreshSlot(std::int32_t slot);
/* Clear whatever the screen was waiting on and open the options screen. */
// FUNCTION: WIZ8 0x00565970
inline void ClearScreenWait(void)
{
    g_pending_screen_state.mode = 0;
    SetPendingScreenState(W8_SCREEN_OPTIONS);
}
/* Portrait condition / enchantment orbs (help 25 / 26): hold opens the
   mode-6 hover overlay; leave and release tear it down. */
bool PortraitConditionOrbRegionEvent(const InputEvent* event, struct W8Region* region);
bool PortraitEnchantmentOrbRegionEvent(const InputEvent* event, struct W8Region* region);
/* Party portrait hit regions: select, target, open camp, and drag-hover. */
bool PortraitSelectRegionEvent(const InputEvent* event, struct W8Region* region);
/* Help 28: portrait side bar that opens Assay on the hovered weapon/item. */
bool PortraitAssaySidebarRegionEvent(const InputEvent* event, struct W8Region* region);
/* Help 24: portrait overlay hover strip; drives portrait_overlay_party_slot. */
bool PortraitOverlayHoverRegionEvent(const InputEvent* event, struct W8Region* region);
/* Region set 4: the eight combat-action hit regions beside the portraits. */
bool PartyCombatActionRegionEvent(const InputEvent* event, struct W8Region* region);
/* Help 31: radar-map button beside the text area. */
bool RadarMapButtonRegionEvent(const InputEvent* event, struct W8Region* region);
/* Region 23: the 3D world view. Hover refreshes the combat selection/target,
   left-up runs the targeting/item/monster dispatch, right-down opens monster
   info or the assay dialog, and the mouselook latch arms and releases here. */
bool WorldViewRegionEvent(const InputEvent* event, struct W8Region* region);
/* Help 36: combat monster-list hit rows beside the radar map. */
bool MonsterListRegionEvent(const InputEvent* event, struct W8Region* region);
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

bool IsMGSActionKeyEnabled(std::uint16_t command);
void RunMGSActionKey(std::uint16_t command);
void LoadMainGameCursorResources(void);
std::int16_t GetMainGameViewportMode(void);
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
void OpenCharacterScreenForPartySlot(std::uint32_t party_slot, bool flag);
void RebuildNpcTradeItemList(bool scroll_to_top);
/* The trade-stock index behind a visible NPC item row. */
std::int32_t ResolveNpcTradeStockIndex(std::int32_t index);
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
bool AttemptNpcItemTrade(W8ItemInstance* item, std::uint8_t quantity, std::int32_t index);
/* Destroy callback on the NPC trade split dialog; commits the
   chosen count to the editor slot and refreshes the trade selection. */
void NpcTradeSplitDialogResult(W8DialogBase* dialog);
void RefreshFormationPanel(bool show_portraits);
void EndLockInteractMode(char suspend);
void UpdateMainGameScreen(void);
void EndTrapInteractMode(char suspend);
std::int32_t GetPartySlotLocksTrapsLevel(std::int32_t slot);
bool OpenLockInteraction(Trigger* trigger);
bool OpenTrapInteraction(Trigger* trigger);
/* When a slot's committed action cannot execute, re-choose a
   fallback hand, breath or character attack, or reroute spell/item aiming. */
void FallbackFromUnreachableAction(std::int32_t party_slot);
void SetCombatAction(std::int32_t value);
void SetCombatSelection(std::int32_t value);
void SetCombatTarget(std::int32_t value);

void RequestRedrawCombatBar(void);
void UpdateScreenOverlays(std::int32_t frame);
bool LoadCurrentLevelData(void);
void ResetMainGameMode(void);
void CreateSurpriseFade(void);
void ReverseSurpriseFade(void);
void DestroySurpriseFade(void);
bool UpdateSurpriseFade(void);
bool OpenUseItemSelectView(std::int32_t slot);

bool MainGameScreenEnter(void);
void MainGameScreenFrame(void);
bool MainGameScreenLeave(std::int32_t leaving);
void ShowMainGameNoticeLine(std::string_view text, W8DialogDestroyCallback callback, bool confirmation,
                            bool cancel);

bool CombatBarRegionEvent(const InputEvent* event, struct W8Region* region);
bool DialogueTranscriptRegionEvent(const InputEvent* event, struct W8Region* region);
void SelectNpcBuyMode(void);
void ConfirmNpcTradeItem(void);
void RestockNpcTradeStock(void);
void OpenNpcTradeQuantityDialog(void);

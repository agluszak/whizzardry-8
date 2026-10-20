#include "wiz8/local_screens/MainGameScreen.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>
#include <fmt/printf.h>

#include "camera_motion.h"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "mgs_commands.h"
#include "npc_trade_price.h"
#include "texture_animation.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/button_interaction.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/canvas/himage.h"
#include "wiz8/canvas/image.h"
#include "wiz8/canvas/sprite.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/character_skills.h"
#include "wiz8/combat/combat.h"
#include "wiz8/cursor.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/design_rect.h"
#include "wiz8/dialog_code/AssayDialog.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/dialog_code/DialogFactoryDialogs.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/dialog_code/DialogTextArea.h"
#include "wiz8/dialog_code/DialogTextEntry.h"
#include "wiz8/dialog_code/MessageDialogBase.h"
#include "wiz8/dialog_code/MonsterInfoDialog.h"
#include "wiz8/difficulty.h"
#include "wiz8/entities/cursor.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/spell_visual.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/inventory.h"
#include "wiz8/item_spawning.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/layouts/npc_dialogue.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/level_population.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/CombatAttack.h"
#include "wiz8/local_code/CombatRange.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/FormationAndFacing.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayInit.h"
#include "wiz8/local_code/GameplayTime.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/ItemManager.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MagicEffects.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/RangeControl.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_code/Traps.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_code/npc_story_events.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/AutomapScreen.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/local_screens/MGSButtons.h"
#include "wiz8/local_screens/MGSFormation.h"
#include "wiz8/local_screens/MGSKeyboard.h"
#include "wiz8/local_screens/MGSPartyMovement.h"
#include "wiz8/local_screens/MGSPortraitCombat.h"
#include "wiz8/local_screens/MGSPortraits.h"
#include "wiz8/local_screens/MGSRadarMap.h"
#include "wiz8/local_screens/MGSSpellCasting.h"
#include "wiz8/local_screens/MGSSpellIcons.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MGSUseItemSelect.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/modal_content.h"
#include "wiz8/monster_generators.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/npc_items.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/platform/input.h"
#include "wiz8/platform/timer.h"
#include "wiz8/render/compositor.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/rules/activity.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/party_stats.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/screen.h"
#include "wiz8/script_context.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/string_database.h"
#include "wiz8/targeting_presentation.h"
#include "wiz8/text_input.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"
#include "wiz8/unicode.h"
#include "wiz8/utility.h"
#include "wiz8/world/camera_control.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"
#include "wiz8/world_cursor.h"

struct SDL_Surface;

/* Every redraw request checks the screen state first, so a request made from
   another screen is simply dropped. */

/* The region set the two enable/disable wrappers below own. */
std::unique_ptr<ModalContent> g_modal_owner;
std::unique_ptr<ModalContent> g_pending_main_game_dialog;

static uint64_t g_mouselook_last_tick;
static bool g_mouselook_tick_init;

static float g_mouselook_pending_yaw;
static float g_mouselook_pending_pitch;

const float g_mouselook_smooth_max = 0.39269906f;
const float g_mouselook_smooth_min = 0.006135923f;

bool g_mouselook_active;

/* Saved mouse position while mouselook is latched (WarpSystemCursor restore). */
static SDL_Point g_mouselook_cursor_pos;

W8MainGameMode g_main_game_mode;

W8MainGameResourceSlot g_main_game_resource_slots[17] = {
    {1, 0, 1, 0, 0, 0, 0},  {2, 0, 5, 0, 0, 0, 0},  {3, 0, 1, 0, 0, 0, 0},  {4, 0, 1, 0, 0, 0, 0},
    {5, 0, 1, 0, 0, 0, 0},  {6, 0, 4, 0, 0, 0, 0},  {7, 0, 4, 0, 0, 0, 0},  {0, 0, 0, 0, 0, 0, 0},
    {8, 0, 1, 0, 0, 0, 0},  {9, 0, 1, 0, 0, 0, 0},  {10, 0, 5, 0, 0, 0, 0}, {11, 0, 1, 0, 0, 0, 0},
    {12, 0, 1, 0, 0, 0, 0}, {13, 0, 4, 0, 0, 0, 0}, {15, 0, 1, 0, 0, 0, 0}, {14, 0, 1, 0, 0, 0, 0},
    {16, 0, 1, 0, 0, 0, 0},
};

bool g_mouselook_left_held;

/* When set, the draw path clears FLAG_DISABLE on level model instances before
   RenderFrame, then re-evaluates each instance afterward. */
bool g_node_cull_pending;

unsigned short g_trap_name_string_ids[] = {
    0x542, 0x543, 0x544, 0x545, 0x546, 0x547, 0x548, 0x549, 0x54a, 0x54b,
    0x54c, 0x54d, 0x54e, 0x54f, 0x550, 0,     0x551, 0x552, 0x553, 0x554,
    0x555, 0,     0x556, 0x557, 0x558, 0x559, 0x55a, 0x55b,
};

const char* g_trap_sounds[8] = {
    "Data\\Sound\\Misc\\Trap 03.wav", "Data\\Sound\\Misc\\Trap 07.wav",
    "Data\\Sound\\Misc\\Trap 01.wav", "Data\\Sound\\Misc\\Trap 05.wav",
    "Data\\Sound\\Misc\\Trap 02.wav", "Data\\Sound\\Misc\\Trap 08.wav",
    "Data\\Sound\\Misc\\Trap 06.wav", "Data\\Sound\\Misc\\Trap 04.wav",
};
static char s_lock_open_fail[] = "Data\\Sound\\Misc\\Lock_Open_Fail.wav";
static char s_trap_detect[] = "Data\\Sound\\Misc\\Trap Detect.wav";

static char g_trap_inspection_sound[] = "Data\\Sound\\Misc\\Trap Inspection.wav";

static char g_trap_sprung_sound[] = "Data\\Sound\\Misc\\Trap Sprung.wav";

static char g_text_enter_default_level[] = "Enter default level ?";

const float g_mouselook_yaw_scale = 0.004908738192170858f;
const float g_mouselook_pitch_scale = 0.00654498441144824f;
const float g_text_panel_progress_scale = 120.0f;

static void ApplyPendingMouselook(void);
void ApplyPendingTooltip(void);
void UpdateCombatPortraitStatus(void);
void UpdateKeyboardMenu(void);
void InvalidateRedrawRegions(unsigned int redraw_flags);
void InvalidatePortraitPanel(void);

void RedrawPartyPortraitBars(unsigned int party_slot, bool slot_enabled);

static void DrawMainGamePrompt(void);
void InvalidateLockInteractionPanels(void);
void RedrawTextBoxComplete(void);
bool HandleMouselookInput(const InputEvent* input);

static bool IsPartyPortraitUnderCursor(unsigned int party_slot);
void UpdateFormationPortraitRefresh(void);
/* Insanity (spell 0x3c) world-cursor extent rows: six doubles per row.
   Three rows fill through 0x00616f40, immediately before the power index. */
double g_world_cursor_extent_table[18] = {
    -750.0,  0.0, -750.0,  750.0,  1349.9999642372131, 750.0,
    -1125.0, 0.0, -1125.0, 1125.0, 2024.9999463558197, 1125.0,
    -1500.0, 0.0, -1500.0, 1500.0, 2699.9999284744263, 1500.0,
};
/* Per spell-power index into g_world_cursor_extent_table. The
   Insanity cursor reads it byte-indexed by the power field; the eleven bytes
   run to 0x00616f4c, where the separate Magic Effects dword table starts. */
signed char g_spell_power_extent_index[8] = {0, 0, 0, 1, 1, 2, 2, 0};

void ServiceNpcDialogue(void);
static int g_lock_pin_target_height[4] = {10, 16, 22, 28};

static int g_knock_knock_chance[8] = {0, 344, 459, 516, 550, 573, 589, 602};
static char s_lock_pin_falling[] = "Data\\Sound\\Misc\\Lock_Pin_Falling.wav";
static char s_lock_picking_success[] = "Data\\Sound\\Misc\\Lock_Picking_Success.wav";
static char s_lock_pin_rising[] = "Data\\Sound\\Misc\\Lock_Pin_Rising.wav";
static char s_lock_forcing_fail[] = "Data\\Sound\\Misc\\Lock_Forcing_Fail.wav";
static char s_lock_forcing_success[] = "Data\\Sound\\Misc\\Lock_Forcing_Successful.wav";
static W8LockInteraction* LockInteraction()
{
    return MainGame() ? MainGame()->lock_interaction.get() : nullptr;
}
W8TrapInteraction* TrapInteraction()
{
    return MainGame() ? MainGame()->trap_interaction.get() : nullptr;
}
bool IsLockInteractionActive()
{
    return MainGame() && MainGame()->lock_active;
}
bool IsTrapInteractionActive()
{
    return MainGame() && MainGame()->trap_active;
}
MainGameScreen::~MainGameScreen() = default;
/* The selected slot's effective power with spell 0x27. */
/* A party slot's casting level for a spell it has learned, or -1. */
static int LearnedSpellPower(int slot, W8SpellId spell)
{
    W8Character* character = &CurrentGame().party.characters[slot];
    if (!IsPartySlotEligible(slot) || character->spell_learned[spell] != 1) {
        return -1;
    }
    const W8Skill book = GetBestSpellbookSkillForSpell(character, spell, true, false, 7);
    return static_cast<int>(GetSpellCastingSkillLevel(character, book, Data().spells[spell].realm));
}
void CancelMouselook()
{
    if (g_mouselook_active) {
        EnableCursorScene();
        g_mouselook_active = false;
        SetMouseMoveEventsEnabled(0);
    }
}

static void QueueInteractionNotice(int event_type)
{
    W8CharacterEvent* event =
        ApplyItemEffectToRandomCharacter(event_type, -1, kCharacterEventNoPreempt);
    if (event != 0) {
        event->DelayDispatch(600);
    }
}

static void EnableActionPanelRegions()
{
    if (MainGameHud()->action_panel_visible != 0) {
        SetTextBoxEnabled(true);
        SetTextBoxMuteInput(true);
        SetTextBoxScrollInput(true);
        SetTextBoxChannelTabInput(true);
    }
}

bool LockInteractionSuspended()
{
    return LockInteraction() && !IsLockInteractionActive();
}

bool TrapInteractionSuspended()
{
    return TrapInteraction() && !IsTrapInteractionActive();
}

/* The interface holds the world: a pending or current other screen, a picker, dialogue,
   the formation panel, a dialog, a minimised window, the targeting cone or the review
   transition. */
static bool InteractionBlocksWorldAdvancement()
{
    return CurrentScreenState().id != W8_SCREEN_MAIN_GAME || IsScreenTransitionPending() ||
           gpSCSV || ItemUseDraft() || CurrentGame().activity.conversation ||
           FormationPanelOpen() || IsModalOpen() || !IsVideoActive() ||
           TargetingMode() == W8_TARGET_NEED_CONE ||
           (MainGameHud() && MainGameHud()->review_transition_active);
}

/* Open the lock interaction over a trigger, or re-raise its panels while one
   is suspended; outside those paths a random party member eats the trigger's
   event and the failure sound plays. */
int OpenLockInteraction(Trigger* trigger)
{
    // A trap interaction, active or suspended, excludes a lock interaction.
    if (TrapInteraction()) {
        return 0;
    }
    const bool resume = LockInteractionSuspended();
    if (IsLockInteractionActive() || (resume && trigger != 0) || CurrentGame().combat.has_value()) {
        QueueInteractionNotice(g_lock_notice_event);
        wiz8::sound::play(s_lock_open_fail);
        return 1;
    }
    MainGame()->lock_active = true;
    if (MainGameHud()->open_submenu) {
        DestroySubMenuControls();
    }
    CloseMainGameOverlays();
    RefreshMainGameLayout();
    SelectTextBox(2);
    ResetEditorStatusLine(-1);
    ResetLevelDataVectors();
    RequestRedrawCombatBar();
    RequestRedraw(W8_MAIN_REDRAW_SUBMENU_BUTTONS);
    if (resume) {
        LockInteraction()->EnablePanels(1);
        LockInteraction()->m_tumbler_panel->InvalidateAll();
        LockInteraction()->RefreshControls();
    } else {
        QueueInteractionNotice(g_lock_notice_event);
        wiz8::sound::play(s_lock_open_fail);
        MainGame()->lock_interaction = std::make_unique<W8LockInteraction>(trigger);
    }
    return 1;
}

void ProcessLockInteractMode(void)
{
    LockInteraction()->Process();
}

/* Redraw the lock interaction's tumbler, info and action panels. */
void RedrawLockInteractionPanels(void)
{
    LockInteraction()->m_tumbler_panel->Redraw(GetFrameBufferSurface(), false);
    LockInteraction()->m_info_panel->Redraw(GetFrameBufferSurface(), false);
    LockInteraction()->DrawActions(GetFrameBufferSurface());
}

/* Leave lock interact mode. With `suspend` clear the interaction object is
   destroyed outright; otherwise it stays alive with its panels inactive so the
   session can resume. Either path returns to the UI mode the interaction was
   opened from and repaints the portrait, status and action regions. */
void EndLockInteractMode(char suspend)
{
    MainGame()->lock_active = false;
    if (suspend != 0) {
        LockInteraction()->EnablePanels(0);
    } else {
        MainGame()->lock_interaction.reset();
        ClearLevelMovementStopped();
    }
    SelectTextBox(0);
    RefreshMainGameLayout();
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
    RequestRedrawCombatBar();
    RequestRedraw(W8_MAIN_REDRAW_SUBMENU_BUTTONS);
}

static bool CanSelectedCharacterCastKnockKnock()
{
    const int slot = CurrentGame().status.selected_character;
    return LearnedSpellPower(slot, W8_SPELL_KNOCK_KNOCK) > -1 &&
           CanCharacterCastSpell(&CurrentGame().party.characters[slot], W8_SPELL_KNOCK_KNOCK);
}

/* The selected character's percent chance to force a lock with
   `tumbler_count` pins, or nothing when they are too weak or tired to try. */
/* Whether a party slot has learned knock-knock or Divine Trap and can cast
   either of them now. */
static bool CanCastTrapSpell(int slot)
{
    W8Character* character = &CurrentGame().party.characters[slot];
    return (LearnedSpellPower(slot, W8_SPELL_KNOCK_KNOCK) > -1 ||
            LearnedSpellPower(slot, W8_SPELL_DIVINE_TRAP) > -1) &&
           (CanCharacterCastSpell(character, W8_SPELL_KNOCK_KNOCK) ||
            CanCharacterCastSpell(character, W8_SPELL_DIVINE_TRAP));
}

static std::optional<int> PercentOrNothing(int value)
{
    return value > -1 ? std::optional(value) : std::nullopt;
}

/* Show a percentage in an info line, or the dimmed dash when there is none. */
static void ShowPercent(W8TextBuffer& line, std::optional<int> percent, int font_state = -1)
{
    line.SetFontStateIndex(percent ? font_state : 0);
    line.SetText(percent ? fmt::format("{:d}%", *percent) : std::string(g_dash),
                 g_wiz_text_font_secondary);
}

static std::optional<int> LockForceChance(int tumbler_count)
{
    const W8Character& character =
        CurrentGame().party.characters[CurrentGame().status.selected_character];
    const int strength = character.attributes[W8_ATTRIBUTE_STRENGTH].effective;
    if (!IsPartySlotEligible(CurrentGame().status.selected_character) || character.stamina < 0x50 ||
        strength <= 0x32) {
        return std::nullopt;
    }
    const int divisor = std::clamp(g_settings.difficulty - 1 + tumbler_count, 2, 8);
    return (strength - 0x32) / IntegerPower(2, divisor - 2);
}

void W8LockInteraction::RefreshControls()
{
    const bool can_pick = GetPartySlotLocksTrapsLevel(CurrentGame().status.selected_character) > -1;
    for (int i = 0; i < m_tumbler_panel->m_tumbler_count; i++) {
        m_tumbler_panel->m_tumblers[i].m_enabled = can_pick;
    }
    m_spell_button->SetEnabled(CanSelectedCharacterCastKnockKnock());
    m_force_button->SetEnabled(LockForceChance(m_tumbler_count).has_value());
    m_info_panel->RefreshInfo();
    actions_dirty = true;
}

/* Mark the lock interaction's tumbler and action panels dirty and rebuild the
   info text; run from the redraw sweep when flag 0x200 is raised while lock
   interact mode is up. */
void InvalidateLockInteractionPanels(void)
{
    W8LockInteraction* interaction = LockInteraction();

    interaction->m_tumbler_panel->InvalidateAll();
    interaction->m_info_panel->RefreshInfo();
    interaction->actions_dirty = true;
}

void W8LockTumbler::Redraw(SDL_Surface& target, bool full_redraw, int phase)
{
    int left;
    int top;
    int right;
    int bottom;
    int frame;

    if (!full_redraw && !m_dirty) {
        return;
    }
    const auto bounds = this->bounds;
    left = bounds.x;
    right = bounds.x + bounds.w;
    top = bounds.y;
    bottom = bounds.y + bounds.h;
    InvalidateRegion(left - 3, top, right + 3, bottom);
    const SDL_Rect top_fill{left, top, right - left, 0x24};
    const SDL_Rect bottom_fill{left - 3, top + 0x24, right - left + 6, bottom - top - 0x24};
    SDL_FillSurfaceRect(&target, &top_fill, 0x8000);
    SDL_FillSurfaceRect(&target, &bottom_fill, 0x8000);
    CatalogArtwork::Load(0x1ae, 0).draw(target, 0, left + 3, top);
    frame = m_pin_index * 4 + 1;
    if (m_pin_set) {
        frame += m_at_top ? 2 : 1;
    } else if (m_hovered && !m_rising && !m_falling) {
        frame += 3;
    }
    CatalogArtwork::Load(0x1ae, 0).draw(target, frame, left, top);
    if (m_at_top) {
        CatalogArtwork::Load(0x1ae, 0).draw(target, phase + 0x11, right, top - 4);
    }
    m_dirty = false;
}

W8LockTumblerPanel::W8LockTumblerPanel(int tumbler_count, const unsigned char* pin_data,
                                       W8LockInteraction& owner)
    : m_tumbler_count(tumbler_count)
{
    for (int i = 0; i < tumbler_count; ++i) {
        auto& pin = m_tumblers[i];
        pin.bounds = {0xfb + i * 0x1c, 0x16e, 0x14, 0x4a};
        pin.m_pin_index = pin_data[i];
        input.add(SDL_Rect{pin.bounds.x, pin.bounds.y, pin.bounds.w + 1, pin.bounds.h + 1},
                  [this, i, &owner](const UiPointerEvent& event) {
                      auto& pin = m_tumblers[i];
                      if (event.action == UiPointerAction::Enter ||
                          event.action == UiPointerAction::Leave) {

                          pin.m_hovered = event.action == UiPointerAction::Enter;
                          pin.m_dirty = true;
                      } else if (event.button == UiPointerButton::Left) {

                          if (event.action == UiPointerAction::Up && event.inside &&
                              pin.m_enabled && !m_animating && !pin.m_pin_set)
                              AfterUiDispatch([&owner, i] { owner.OnTumblerPicked(i); });
                      }
                      return UiResult::Handled;
                  });
    }
}

void W8LockTumblerPanel::Redraw(SDL_Surface& target, bool full_redraw)
{
    full_redraw |= m_dirty;
    if (full_redraw) CatalogArtwork::Load(0x1af, 0).drawAndInvalidate(target, 1, 0xd3, 0x166);
    const int phase = m_phase % 12;
    const int displayed_phase = phase > 6 ? 12 - phase : phase;
    for (int i = 0; i < m_tumbler_count; ++i)
        m_tumblers[i].Redraw(target, full_redraw, displayed_phase);
    m_dirty = false;
}

void W8LockTumblerPanel::UpdateTumblerAnimation()
{
    W8LockTumbler* tumbler;
    int phase;
    int rise;
    int fall;
    int i;

    phase = static_cast<int>(m_phase_timer.GetProgress());
    rise = static_cast<int>(m_rise_timer.GetProgress());
    fall = static_cast<int>(m_fall_timer.GetProgress());
    if (phase != 0) {
        m_phase += phase;
        for (i = 0; i < m_tumbler_count; i++) {
            if (m_tumblers[i].m_at_top) {
                m_tumblers[i].m_dirty = true;
            }
        }
    }
    if (rise != 0 || fall != 0) {
        m_animating = false;
        for (i = 0; i < m_tumbler_count; i++) {
            tumbler = &m_tumblers[i];
            if (rise != 0 && tumbler->m_rising) {
                tumbler->m_pin_height -= rise;
                if (tumbler->m_pin_height <= g_lock_pin_target_height[tumbler->m_pin_index]) {
                    tumbler->m_pin_height = g_lock_pin_target_height[tumbler->m_pin_index];
                    tumbler->m_rising = false;
                    tumbler->m_pin_set = true;
                }
                tumbler->m_dirty = true;
            }
            if (fall != 0 && tumbler->m_falling) {
                tumbler->m_pin_height += fall;
                if (tumbler->m_pin_height > 0x21) {
                    tumbler->m_pin_height = 0x22;
                    tumbler->m_falling = false;
                }
                tumbler->m_dirty = true;
            }
            if (tumbler->m_rising || tumbler->m_falling) {
                m_animating = true;
            }
        }
    }
}

W8LockInfoPanel::W8LockInfoPanel(int tumbler_count)
{
    W8ControlsRect bounds;

    m_tumbler_count = tumbler_count;
    bounds.left = 0x17 + 0x25;
    bounds.top = 0x166 + 7;
    bounds.right = 0x17 + 0xb4;
    bounds.bottom = 0x166 + 0x11;
    text[0] = W8TextBuffer(&bounds, {}, g_wiz_text_font_secondary, g_W8TextBufferAlignCenter, 4);
    bounds.top += 0xe;
    bounds.bottom += 0xe;
    text[1] = W8TextBuffer(&bounds, Data().strings[0x7a9], g_wiz_text_font_secondary,
                           g_W8TextBufferAlignLeft, 4);
    bounds.top += 0xe;
    bounds.bottom += 0xe;
    text[3] = W8TextBuffer(&bounds, Data().strings[0x7aa], g_wiz_text_font_secondary,
                           g_W8TextBufferAlignLeft, 4);
    bounds.top += 0xe;
    bounds.bottom += 0xe;
    text[5] = W8TextBuffer(&bounds, Data().strings[0x7ab], g_wiz_text_font_secondary,
                           g_W8TextBufferAlignLeft, 4);
    bounds.left = 0x17 + 0x98;
    bounds.top = 0x166 + 0x15;
    bounds.right = 0x17 + 0xb5;
    bounds.bottom = 0x166 + 0x1f;
    text[2] = W8TextBuffer(&bounds, {}, g_wiz_text_font_secondary, g_W8TextBufferAlignCenter, 4);
    bounds.top += 0xe;
    bounds.bottom += 0xe;
    text[4] = W8TextBuffer(&bounds, {}, g_wiz_text_font_secondary, g_W8TextBufferAlignCenter, 4);
    bounds.top += 0xe;
    bounds.bottom += 0xe;
    text[6] = W8TextBuffer(&bounds, {}, g_wiz_text_font_secondary, g_W8TextBufferAlignCenter, 4);
    InvalidateAll();
}

void W8LockInfoPanel::RefreshInfo()
{
    W8Character* character =
        &CurrentGame().party.characters[CurrentGame().status.selected_character];
    text[0].SetText(character->name, g_wiz_text_font_secondary);
    ShowPercent(text[2], PercentOrNothing(
                             GetPartySlotLocksTrapsLevel(CurrentGame().status.selected_character)));
    ShowPercent(text[4], PercentOrNothing(LearnedSpellPower(CurrentGame().status.selected_character,
                                                            W8_SPELL_KNOCK_KNOCK)));
    ShowPercent(text[6], LockForceChance(m_tumbler_count));
    text[1].SetGeometryDirty();
    text[3].SetGeometryDirty();
    text[5].SetGeometryDirty();
    InvalidateAll();
}

void W8LockInfoPanel::Redraw(SDL_Surface& target, bool full_redraw)
{
    if (m_dirty || full_redraw) {
        CatalogArtwork::Load(0x1af, 0).drawAndInvalidate(target, 0, 0x17, 0x166);
        m_dirty = false;
        text[0].RenderToTarget(0, false, target);
        text[1].RenderToTarget(0, false, target);
        text[2].RenderToTarget(0, false, target);
        text[3].RenderToTarget(0, false, target);
        text[4].RenderToTarget(0, false, target);
        text[5].RenderToTarget(0, false, target);
        text[6].RenderToTarget(0, false, target);
    }
}

W8LockInteraction::W8LockInteraction(Trigger* trigger) : m_timer()
{
    int i;

    m_trigger = trigger;
    m_state = W8_LOCK_STATE_IDLE;
    m_tumbler_count = trigger->lock_state.difficulty;
    if (m_tumbler_count < 2) {
        m_tumbler_count = 2;
    } else if (m_tumbler_count > 8) {
        m_tumbler_count = 8;
    }
    m_tumbler_panel = std::make_unique<W8LockTumblerPanel>(
        m_tumbler_count, trigger->lock_state.device_state.pins, *this);
    m_info_panel = std::make_unique<W8LockInfoPanel>(m_tumbler_count);
    const int normal[]{8, 0, 4, 4};
    const SDL_Point positions[]{{0x1ed, 0x16c}, {0x20b, 0x16c}, {0x1ed, 0x18a}, {0x22b, 0x16c}};
    const int help[]{0x7cb, 0x7cd, 0x7cc, -1};
    for (int i = 0; i < 4; ++i) {
        auto& button = buttons[i] = std::make_unique<W8ButtonWidget>(input);
        button->Configure(
            ButtonGraphics::Load(CatalogArtwork::Load(i == 3 ? 0x8e : 0x1b0, 0).spriteSheet(),
                                 normal[i] + 3, normal[i], normal[i] + 1, normal[i] + 2,
                                 normal[i] + 2),
            {.on_click =
                 [this](auto* control) {
                     AfterUiDispatch([this, control] { OnPrimary(control); });
                 },
             .policy = {.input_rule = ButtonInputRule::PrimaryPressedState},
             .tooltip = help[i],
             .priority = UI_PRIORITY_LOWEST,
             .menu_artwork = true});

        button->SetPosition(positions[i].x, positions[i].y);
    }
    m_spell_button = buttons[0].get();
    m_force_button = buttons[1].get();
    m_cancel_button = buttons[2].get();
    m_done_button = buttons[3].get();
    m_selected_slot = -1;
    for (i = 0; i < 8; i++) {
        m_tumbler_owner[i] = -1;
        m_tumbler_locked[i] = 0;
    }
    for (i = 0; i < 8; i++) {
        m_slot_attempts[i] = 0;
    }
    RefreshControls();
}

int W8LockInteraction::ReleaseOwnedTumblers(int slot)
{
    int dropped = 0;
    int i;
    for (i = 0; i < m_tumbler_count; i++) {
        if (m_tumbler_owner[i] == slot && m_tumbler_locked[i] != 0) {
            m_tumbler_owner[i] = -1;
            m_tumbler_locked[i] = 0;
            m_tumbler_panel->m_tumblers[i].m_at_top = false;
            m_tumbler_panel->m_tumblers[i].m_pin_set = false;
            m_tumbler_panel->m_tumblers[i].m_falling = true;
            m_tumbler_panel->m_animating = true;
            dropped = 1;
        }
    }
    m_tumbler_panel->InvalidateAll();
    return dropped;
}

void W8LockInteraction::Process()
{
    int i;
    int dropped;

    if (m_state == W8_LOCK_STATE_WAIT_FOR_UNLOCK && m_timer.GetProgress() >= 1.0f) {
        m_trigger->CompleteItemInteraction();
        m_trigger->Run(-1);
        m_state = W8_LOCK_STATE_EXIT;
    }
    if (m_state == W8_LOCK_STATE_EXIT) {
        EndLockInteractMode(0);
        return;
    }
    m_tumbler_panel->UpdateTumblerAnimation();
    if (m_tumbler_panel->m_animating) {
        return;
    }
    switch (m_state) {
    case W8_LOCK_STATE_IDLE:
    case W8_LOCK_STATE_WAIT_FOR_UNLOCK:
    case W8_LOCK_STATE_EXIT:
        break;
    case W8_LOCK_STATE_PICK_TUMBLER:
        m_state = W8_LOCK_STATE_IDLE;
        ResolvePick();
        return;
    case W8_LOCK_STATE_FORCE:
        m_state = W8_LOCK_STATE_IDLE;
        AttemptForce();
        return;
    case W8_LOCK_STATE_PREPARE_SPELL:
        m_state = W8_LOCK_STATE_RELEASE_FOR_SPELL;
        dropped = ReleaseOwnedTumblers(CurrentGame().status.selected_character);
        goto lock_release_done;
    case W8_LOCK_STATE_RELEASE_FOR_SPELL:
        m_state = W8_LOCK_STATE_IDLE;
        if (CanSelectedCharacterCastKnockKnock()) {
            EndLockInteractMode(1);
            BeginSpellCast(W8_SPELL_KNOCK_KNOCK, -1, W8_DIALOGUE_LAYOUT_UNSET);
            return;
        }
        break;
    case W8_LOCK_STATE_CANCEL:
        m_state = W8_LOCK_STATE_RELEASE_FOR_ITEM;
        dropped = ReleaseOwnedTumblers(CurrentGame().status.selected_character);
    lock_release_done:
        if (dropped) {
            wiz8::sound::play(s_lock_pin_falling);
            return;
        }
        break;
    case W8_LOCK_STATE_RELEASE_FOR_ITEM:
        m_state = W8_LOCK_STATE_IDLE;
        EndLockInteractMode(1);
        OpenUseItemSelectView(CurrentGame().status.selected_character);
        return;
    case W8_LOCK_STATE_CHECK_PICK_SUCCESS:
        m_state = W8_LOCK_STATE_IDLE;
        for (i = 0; i < m_tumbler_count; i++) {
            if (m_tumbler_owner[i] == -1) {
                return;
            }
        }
        wiz8::sound::play(s_lock_picking_success);
        ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[0x7ac]);
        BeginUnlock();
        break;
    }
}

void W8LockInteraction::EnablePanels(int enable)
{
    m_tumbler_panel->input.setEnabled(enable);
    input.setEnabled(enable);
}

void W8LockInteraction::OnTumblerPicked(int index)
{
    W8Character* character;
    int i;

    if (!IsPartySlotEligible(CurrentGame().status.selected_character)) {
        return;
    }
    character = &CurrentGame().party.characters[CurrentGame().status.selected_character];
    if (!character->skills[W8_SKILL_LOCKS_TRAPS].active &&
        character->skills[W8_SKILL_LOCKS_TRAPS].level == 0) {
        return;
    }
    if (static_cast<int>(character->skills[W8_SKILL_LOCKS_TRAPS].level) < 0) {
        return;
    }
    if (m_selected_slot != CurrentGame().status.selected_character) {
        if (m_selected_slot != -1) {
            for (i = 0; i < m_tumbler_count; i++) {
                if (m_tumbler_owner[i] == m_selected_slot && m_tumbler_locked[i] == 0) {
                    m_tumbler_owner[i] = -1;
                    m_tumbler_panel->m_tumblers[i].m_pin_set = false;
                    m_tumbler_panel->m_tumblers[i].m_falling = true;
                    m_tumbler_panel->m_animating = true;
                }
            }
        }
        m_selected_slot = CurrentGame().status.selected_character;
    }
    m_picked_tumbler = index;
    m_state = W8_LOCK_STATE_PICK_TUMBLER;
    m_tumbler_panel->m_tumblers[index].m_hovered = false;
    m_tumbler_panel->m_tumblers[index].m_rising = true;
    m_tumbler_panel->m_animating = true;
    wiz8::sound::play(s_lock_pin_rising);
}

void W8LockInteraction::OnPrimary(W8ButtonWidget* control)
{
    if (control == m_done_button) {
        m_state = W8_LOCK_STATE_EXIT;
        return;
    }
    if (m_tumbler_panel->m_animating && m_state != W8_LOCK_STATE_IDLE) {
        return;
    }
    if (control == m_force_button) {
        m_state = W8_LOCK_STATE_FORCE;
    } else if (control == m_spell_button) {
        m_state = W8_LOCK_STATE_PREPARE_SPELL;
    } else if (control == m_cancel_button) {
        m_state = W8_LOCK_STATE_CANCEL;
    }
}

void W8LockInteraction::ResolvePick()
{
    W8Character* character;
    int chance;
    int level;
    int i;
    int dropped;

    dropped = 0;
    for (i = 0; i < m_tumbler_count; i++) {
        if (m_tumbler_owner[i] == m_selected_slot && m_tumbler_locked[i] == 0) {
            character = &CurrentGame().party.characters[m_selected_slot];
            chance = character->skills[W8_SKILL_LOCKS_TRAPS].level + g_settings.difficulty * -5 + 5;
            if ((chance < 0 ? 0 : static_cast<unsigned char>(chance)) <=
                static_cast<int>(CurrentGame().rng.below(100))) {
                m_tumbler_owner[i] = -1;
                m_tumbler_panel->m_tumblers[i].m_pin_set = false;
                m_tumbler_panel->m_tumblers[i].m_falling = true;
                m_tumbler_panel->m_animating = true;
                dropped = 1;
            }
        }
    }
    m_slot_attempts[m_selected_slot]++;
    m_tumbler_owner[m_picked_tumbler] = m_selected_slot;
    if (CurrentGame().rng.below(5) == 0 && m_trigger->lock_state.ConsumeCountdown()) {
        character = &CurrentGame().party.characters[m_selected_slot];
        level = character->skills[W8_SKILL_LOCKS_TRAPS].level;
        PracticeCharacterSkill(CurrentGame(), character, W8_SKILL_LOCKS_TRAPS, 1, false);
        if (level != static_cast<int>(character->skills[W8_SKILL_LOCKS_TRAPS].level)) {
            m_info_panel->RefreshInfo();
        }
    }
    if (dropped) {
        wiz8::sound::play(s_lock_pin_falling);
    }
    for (i = 0; i < m_tumbler_count; i++) {
        if (m_tumbler_owner[i] == -1) {
            return;
        }
    }
    wiz8::sound::play(s_lock_picking_success);
    ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[0x7ac]);
    BeginUnlock();
}

void W8LockInteraction::AttemptForce()
{
    W8Character* character =
        &CurrentGame().party.characters[CurrentGame().status.selected_character];
    const auto chance = LockForceChance(m_tumbler_count);
    if (!chance) {
        return;
    }
    FatigueCharacter(CurrentGame().status.selected_character, 0x50, false, 0);
    if (static_cast<int>(CurrentGame().rng.below(100)) < *chance) {
        wiz8::sound::play(s_lock_forcing_success);
        ShowString(fmt::format("{} {}", character->name, Data().strings[0x7ae]));
        BeginUnlock();
        return;
    }
    wiz8::sound::play(s_lock_forcing_fail);
    ShowString(fmt::format("{} {}", character->name, Data().strings[0x7ad]));
    RefreshControls();
}

/* Roll the knock-knock chance against a shuffled pin order. At most
   `level + 1` pins are rolled; each success raises an unowned pin (or, on
   backfire, drops a raised one) and animates the tumbler. The control enables
   are then re-derived and the interaction enters state 7. */
void W8LockInteraction::ApplyKnockKnock(int level, int /*flag*/, char backfire)
{
    std::array<int, 8> order{0, 1, 2, 3, 4, 5, 6, 7};
    int slot;
    int pins;
    int i;
    int pin;
    unsigned int count;
    unsigned int chance;
    bool rolled;
    W8LockTumbler* tumbler;
    W8LockTumblerPanel* panel;

    slot = CurrentGame().status.selected_character;
    count = m_tumbler_count;
    rolled = false;
    for (i = 0; i < static_cast<int>(count); ++i) {
        unsigned int first = CurrentGame().rng.below(count);
        unsigned int second = CurrentGame().rng.below(m_tumbler_count);
        if (first != second) {
            std::swap(order[first], order[second]);
        }
        count = m_tumbler_count;
    }
    pins = level + 1;
    if (m_tumbler_count <= level + 1) {
        pins = m_tumbler_count;
    }
    chance = g_knock_knock_chance[level];
    for (i = 0; i < pins; ++i) {
        if (CurrentGame().rng.below(1000) < chance) {
            pin = order[i];
            panel = m_tumbler_panel.get();
            if (backfire == 0) {
                if (m_tumbler_owner[pin] == -1) {
                    tumbler = &panel->m_tumblers[pin];
                    tumbler->m_hovered = false;
                    tumbler->m_rising = true;
                    panel->m_animating = true;
                }
                m_tumbler_panel->m_tumblers[pin].m_at_top = true;
                m_tumbler_locked[pin] = 1;
                m_tumbler_owner[pin] = slot;
            } else {
                if (m_tumbler_owner[pin] == -1) {
                    continue;
                }
                m_tumbler_locked[pin] = 0;
                m_tumbler_owner[pin] = -1;
                m_tumbler_panel->m_tumblers[pin].m_at_top = false;
                tumbler = &panel->m_tumblers[pin];
                tumbler->m_pin_set = false;
                tumbler->m_falling = true;
                panel->m_animating = true;
            }
            rolled = true;
        }
    }
    if (rolled) {
        if (backfire == 0) {
            wiz8::sound::play(s_lock_pin_rising);
        } else {
            wiz8::sound::play(s_lock_pin_falling);
            m_tumbler_panel->InvalidateAll();
        }
    }
    RefreshControls();
    m_state = W8_LOCK_STATE_CHECK_PICK_SUCCESS;
}

void W8LockInteraction::BeginUnlock()
{
    EnablePanels(0);
    m_state = W8_LOCK_STATE_WAIT_FOR_UNLOCK;
    m_timer.SetDuration(1.0f);
    m_timer.Restart();
}

void CastSpellAtLockInteraction(unsigned int level, int flag, int backfire)
{
    if (IsTrapInteractionActive()) {
        AttemptTrapDisarm(level, flag, backfire);
        return;
    }
    if (!IsLockInteractionActive()) {
        ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[0x7af]);
        return;
    }
    LockInteraction()->ApplyKnockKnock(level, flag, backfire);
}

// The scroll bar fills the rightmost 0x13 pixels; OnGeometryChanged places it
// once the panel origin is known.
void W8LockInteraction::DrawActions(SDL_Surface& target)
{
    if (actions_dirty) {
        CatalogArtwork::Load(0x1af, 0).drawAndInvalidate(target, 2, 0x1e7, 0x166);
        for (auto& button : buttons) button->m_dirty = true;
        actions_dirty = false;
    }
    for (auto& button : buttons) button->Refresh();
}

W8MainGameTextKeyHandler::W8MainGameTextKeyHandler()
{
    m_range.on_change = [this](auto* range) {
        m_first_visible_line = range->m_value;
        m_dirty = true;
    };
    m_range.SetRange(0, m_line_count - m_visible_lines);
    input.add(SDL_Rect{0x161, 0x16b, 0x6f, 0x55}, [this](const UiPointerEvent& event) {
        if (event.action != UiPointerAction::Up || event.captured_buttons)
            PlayButtonSound(event.action);
        if (event.action == UiPointerAction::Leave)
            m_hover_line = -1;
        else if (event.action == UiPointerAction::Move || event.action == UiPointerAction::Enter)
            m_hover_line = event.local.y / 14 + m_first_visible_line;
        else if (event.action == UiPointerAction::Wheel)
            m_range.AdjustValue(event.wheel);
        else if (event.action == UiPointerAction::Up && event.button == UiPointerButton::Left)
            SetSelectedLine(event.local.y / 14 + m_first_visible_line);
        m_dirty = true;
        return UiResult::Handled;
    });
}

void W8MainGameTextKeyHandler::Redraw(SDL_Surface& target, bool full_redraw)
{
    int left;
    int top;
    int right;
    int bottom;
    int line;
    int last;
    Palette16 colour;

    if (!m_dirty && !full_redraw) {
        return;
    }
    const SDL_Rect bounds{0x161, 0x16b, 0x6e, 0x54};
    left = bounds.x;
    top = bounds.y;
    right = bounds.x + bounds.w;
    bottom = bounds.y + bounds.h;
    InvalidateRegion(left, top, right, bottom);
    CatalogArtwork::Load(0x1b6, 0).drawRect(
        target, SDL_Rect{0, 0, (right) - (left), (bottom) - (top)}, left, top);
    last = m_first_visible_line + m_visible_lines;
    left += 2;
    top += 1;
    for (line = m_first_visible_line; line < last; ++line) {
        if (line == m_selected_line) {
            colour = g_font_state_palettes[W8_FONT_PALETTE_BLUE];
        } else if (line == m_hover_line) {
            colour = g_font_state_palettes[W8_FONT_PALETTE_YELLOW];
        } else {
            colour = g_wiz_text_font_secondary.glyphs->colors;
        }
        g_wiz_text_font_secondary.setPalette(colour);
        DrawText(target, g_wiz_text_font_secondary, {left, top},
                 Data().strings[g_trap_name_string_ids[line]]);
        top += 0xe;
    }
    g_wiz_text_font_secondary.resetPalette();
    m_range.InvalidateAll();
    m_dirty = false;
}

void W8MainGameTextKeyHandler::SetSelectedLine(int line)
{
    line = std::clamp(line, 0, m_line_count - 1);
    if (line == m_selected_line) {
        return;
    }
    m_selected_line = line;
    m_dirty = true;
    if (m_selected_line < m_range.m_value || m_selected_line >= m_range.m_value + m_visible_lines) {
        int first = m_selected_line;
        if (m_selected_line >= m_range.m_value) {
            first = m_selected_line - m_visible_lines + 1;
        }
        m_first_visible_line = first;
        m_range.SetValue(first);
    }
    if (on_selection) on_selection();
}

char W8MainGameTextKeyHandler::HandleKey(SDL_Keycode key)
{
    int line;

    switch (key) {
    case SDLK_UP:
        SetSelectedLine(m_selected_line - 1 < 0 ? 0 : m_selected_line - 1);
        return 1;
    case SDLK_DOWN:
        line = m_selected_line + 1;
        if (m_line_count - 1 < line) {
            line = m_line_count - 1;
        }
        SetSelectedLine(line);
        return 1;
    case SDLK_PAGEUP:
        line = m_selected_line - 5;
        if (line < 0) {
            line = 0;
        }
        SetSelectedLine(line);
        return 1;
    case SDLK_PAGEDOWN:
        line = m_selected_line + 5;
        if (m_line_count - 1 < line) {
            line = m_line_count - 1;
        }
        SetSelectedLine(line);
        return 1;
    case SDLK_HOME:
        SetSelectedLine(0);
        return 1;
    case SDLK_END:
        SetSelectedLine(m_line_count - 1);
        return 1;
    default:
        return 0;
    }
}

void W8MainGameTextPanel::BeginProgress(std::string_view text, float duration, float hold)
{
    m_progress_display = true;
    m_text_buffer.SetText(text, g_wiz_text_font_secondary);
    m_progress_duration = duration;
    m_progress_elapsed = 0.0f;
    m_progress_drawn = 0;
    m_timer0.SetDuration(hold);
    m_timer0.Restart();
    for (int i = 0; i < 8; ++i) {
        if (!cells[i].interaction.selected) {
            cells[i].blocked = true;
        }
    }
}

W8MainGameTextPanel::W8MainGameTextPanel()
    : m_selection(0), m_screen(0), m_values(0), m_progress_display(0), m_timer1(0.04f, 0)
{
    int index;

    m_marker_anim_time = 0;
    m_target_changed = false;
    m_target_marker_pending = false;
    for (index = 0; index < 8; ++index) {
        const auto size = CatalogArtwork::Load(0x1b2, 0).size(index * 4);
        cells[index].target = &input.add(
            SDL_Rect{0xe0 + (index % 4) * 30, 0x16e + (index / 4) * 50, size.x + 1, size.y + 1},
            [this, index](const UiPointerEvent& event) {
                if (event.action != UiPointerAction::Up || event.captured_buttons)
                    PlayButtonSound(event.action);
                auto& cell = cells[index];
                const ButtonPolicy policy{.primary_toggle = ButtonToggle::Release,
                                          .input_rule = ButtonInputRule::PrimaryPressedState};
                cell.dirty = true;
                if (event.action == UiPointerAction::Leave) cell.interaction.leave(policy);
                if (!cell.enabled || cell.blocked) return UiResult::Handled;
                if (event.action == UiPointerAction::Down &&
                    event.button == UiPointerButton::Left) {
                    cell.interaction.press(SDL_BUTTON_LMASK, policy);
                    PlayButtonSound(UiPointerAction::Enter);
                } else if (event.action == UiPointerAction::Up &&
                           event.button == UiPointerButton::Left && event.inside) {
                    if (cell.interaction.release(SDL_BUTTON_LMASK, policy).activate) {
                        cell.blocked = true;

                        AfterUiDispatch([this, index] {
                            if (m_screen) m_screen->SelectTextEntry(index);
                        });
                    }
                }
                return UiResult::Handled;
            });
        cells[index].target->setTooltip([] { return Data().strings[0x7d1]; });
    }
    m_key_handler = std::make_unique<W8MainGameTextKeyHandler>();
    m_key_handler->on_selection = [this] { OnRangeChanged(); };
    m_text_bounds.left = 0xd3 + 0xc;
    m_text_bounds.top = 0x166 + 0x29;
    m_text_bounds.right = 0xd3 + 0x84;
    m_text_bounds.bottom = 0x166 + 0x33;
    m_text_buffer.SetClipBounds(m_text_bounds);
    m_text_buffer.SetLayoutBounds(&m_text_bounds, true);
    m_key_handler->m_range.SetActive(true);
    InvalidateAll();
    RefreshCellEnables();
    RefreshCellImages();
}

void W8MainGameTextPanel::RefreshCellEnables()
{
    for (int column = 0; column < 8; ++column) {
        if (!cells[column].interaction.selected) {
            cells[column].enabled = GetTable650434Entry(m_selection, column) != 0;
            cells[column].dirty = true;
        }
    }
}

void W8MainGameTextPanel::RefreshCellImages()
{
    for (int column = 0; column < 8; ++column) {
        auto& cell = cells[column];
        cell.image = m_values == nullptr || (m_values[column] == 0 &&
                                             GetTable650434Entry(m_selection, column) == 0)
                         ? -1
                         : m_values[column];
        cell.dirty = true;
    }
}

void W8MainGameTextPanel::Redraw(SDL_Surface& target, bool full_redraw)
{
    SDL_Rect clip;
    int progress;

    if (m_dirty) {
        m_target_marker_pending = true;
    }
    full_redraw |= m_dirty;
    if (full_redraw) CatalogArtwork::Load(0x1b1, 0).drawAndInvalidate(target, 1, 0xd3, 0x166);
    for (int index = 0; index < 8; ++index) {
        auto& cell = cells[index];
        if (!full_redraw && !cell.dirty) continue;
        const int x = 0xe0 + (index % 4) * 30, y = 0x16e + (index / 4) * 50;
        const int frame = index * 4 + (!cell.enabled                             ? 3
                                       : cell.interaction.selected               ? 2
                                       : cell.target->hovered() && !cell.blocked ? 1
                                                                                 : 0);
        CatalogArtwork::Load(0x1b2, 0).drawAndInvalidate(target, frame, x, y);
        if (cell.image >= 0)
            CatalogArtwork::Load(0x1b3, 0).draw(target,
                                                cell.image == 0   ? 2
                                                : cell.image == 1 ? 0
                                                                  : 1,
                                                x, y);
        cell.dirty = false;
    }
    m_key_handler->Redraw(target, full_redraw);
    m_dirty = false;
    m_key_handler->m_range.Redraw(target, false);
    if (m_progress_display) {
        progress = static_cast<int>(m_progress_elapsed * g_text_panel_progress_scale);
        if (progress > m_progress_drawn) {
            m_progress_drawn = progress;
            InvalidateRegion(m_text_bounds.left, m_text_bounds.top, m_text_bounds.right,
                             m_text_bounds.bottom);
            auto& framebuffer = target;
            SDL_GetSurfaceClipRect(&framebuffer, &clip);
            clip.h = (m_text_bounds.left + m_progress_drawn) - clip.y;
            ScopedClipRect clipped(framebuffer, clip);
            CatalogArtwork::Load(0x1b5, 0).draw(target, 0, m_text_bounds.left, m_text_bounds.top);
            m_text_buffer.RenderToTarget(0, true, target);
        }
    }
    if (m_target_changed && m_target_marker_pending) {
        int frame = m_marker_anim_time % 12;
        if (frame >= 7) {
            frame = 12 - frame;
        }
        CatalogArtwork::Load(0x1b4, 0).drawAndInvalidate(target, frame, 0xd3 + 1, 0x166 + 2);
        m_target_marker_pending = false;
    }
}

void W8MainGameTextPanel::OnRangeChanged()
{
    m_selection = m_key_handler->m_selected_line;
    RefreshCellEnables();
    RefreshCellImages();
}

W8MainGameStatusPanel::W8MainGameStatusPanel()
{
    m_text[0].SetLayoutBounds(0x17 + 0x25, 0x166 + 7, 0x17 + 0xb4, 0x166 + 0x11);
    m_text[0].SetLayoutMode(g_W8TextBufferAlignCenter);
    const int labels[]{0x7a9, 0x7aa, 0x7b0};
    for (int row = 0; row < 3; ++row) {
        auto& label = m_text[row * 2 + 1];
        label.SetLayoutBounds(0x17 + 0x25, 0x17b + row * 14, 0x17 + 0xb4, 0x185 + row * 14);
        label.SetLayoutMode(g_W8TextBufferAlignLeft);
        label.SetText(Data().strings[labels[row]], g_wiz_text_font_secondary);
        auto& value = m_text[row * 2 + 2];
        value.SetLayoutBounds(0x17 + 0x98, 0x17b + row * 14, 0x17 + 0xb5, 0x185 + row * 14);
        value.SetLayoutMode(g_W8TextBufferAlignCenter);
    }
}

void W8MainGameStatusPanel::RefreshStatusTexts()
{
    const int slot = CurrentGame().status.selected_character;
    const int level = GetPartySlotLocksTrapsLevel(slot);

    m_text[0].SetText(CurrentGame().party.characters[slot].name, g_wiz_text_font_secondary);
    ShowPercent(m_text[2], PercentOrNothing(level < 0 ? level : level + m_target * 6),
                m_target < 1 ? -1 : 3);
    ShowPercent(m_text[4], PercentOrNothing(LearnedSpellPower(slot, W8_SPELL_KNOCK_KNOCK)));
    ShowPercent(m_text[6], PercentOrNothing(LearnedSpellPower(slot, W8_SPELL_DIVINE_TRAP)));
    m_text[1].SetGeometryDirty();
    m_text[3].SetGeometryDirty();
    m_text[5].SetGeometryDirty();
    m_dirty = true;
}

int GetPartySlotLocksTrapsLevel(int slot)
{
    W8Character* character;

    if (!IsPartySlotEligible(slot)) {
        return -1;
    }
    character = &CurrentGame().party.characters[slot];
    if (!character->skills[W8_SKILL_LOCKS_TRAPS].active &&
        character->skills[W8_SKILL_LOCKS_TRAPS].level == 0) {
        return -1;
    }
    return static_cast<int>(character->skills[W8_SKILL_LOCKS_TRAPS].level);
}

void W8MainGameStatusPanel::Redraw(SDL_Surface& target, bool full_redraw)
{
    if (!m_dirty && !full_redraw) return;
    CatalogArtwork::Load(0x1b1, 0).drawAndInvalidate(target, 0, 0x17, 0x166);
    for (auto& text : m_text) text.RenderToTarget(0, true, target);
    m_dirty = false;
}

W8TrapInteraction::W8TrapInteraction(Trigger* owner)
    : m_owner(owner), m_disarm_state(W8_TRAP_DISARM_IDLE), m_target_difficulty(0), m_sound_handle()
{
    m_device_id = owner->lock_state.device_id;
    m_difficulty = owner->lock_state.difficulty;
    m_text_panel = std::make_unique<W8MainGameTextPanel>();
    m_text_panel->m_screen = this;
    m_status_panel = std::make_unique<W8MainGameStatusPanel>();
    const int normal[]{4, 8, 0, 4, 12};
    const SDL_Point positions[]{
        {0x22b, 0x16c}, {0x1ed, 0x16c}, {0x20b, 0x16c}, {0x1ed, 0x18a}, {0x20b, 0x18a}};
    const int help[]{-1, 0x7ce, 0x7cf, 0x7cc, 0x7d0};
    for (int i = 0; i < 5; ++i) {
        auto& button = m_action_controls[i] = std::make_unique<W8ButtonWidget>(input);
        button->Configure(
            ButtonGraphics::Load(CatalogArtwork::Load(i == 0 ? 0x8e : 0x1b0, 0).spriteSheet(),
                                 normal[i] + 3, normal[i], normal[i] + 1, normal[i] + 2,
                                 normal[i] + 2),
            {.on_click =
                 [this, i](auto*) {
                     AfterUiDispatch(
                         [this, i] { OnPrimary(static_cast<W8TrapInteractionAction>(i)); });
                 },
             .policy = {.input_rule = ButtonInputRule::PrimaryPressedState},
             .tooltip = help[i],
             .priority = UI_PRIORITY_LOWEST,
             .menu_artwork = true});

        button->SetPosition(positions[i].x, positions[i].y);
    }
    for (int i = 0; i < 8; ++i) {
        m_column_filled[i] = 0;
        m_slot_attempted[i] = 0;
    }
    RefreshActionPanel();
}

void W8TrapInteraction::DrawActions(SDL_Surface& target)
{
    if (actions_dirty) {
        CatalogArtwork::Load(0x1b1, 0).drawAndInvalidate(target, 2, 0x1e7, 0x166);
        for (auto& button : m_action_controls) button->m_dirty = true;
        actions_dirty = false;
    }
    for (auto& button : m_action_controls) button->Refresh();
}

void W8TrapInteraction::SelectTextEntry(int index)
{
    int slot = CurrentGame().status.selected_character;
    int skill;
    int chance;
    int roll;
    float hold;
    float duration;

    m_selected_character = slot;
    skill = GetPartySlotLocksTrapsLevel(slot);
    hold = g_navigator_linked_radius_scale - skill * 0.02f;
    chance = m_difficulty;
    if (chance < 0) {
        chance = 0;
    }
    skill = GetPartySlotLocksTrapsLevel(m_selected_character);
    if (skill > -1) {
        skill += ((m_target_difficulty + 1) / 2) * 6;
    }
    skill -= g_table2[chance];
    chance = (skill * 3) / 2 + (11 - g_settings.difficulty) * 10;
    if (chance < 0) {
        chance = 0;
    } else if (chance > 0x63) {
        chance = 0x63;
    }
    if (GetTable650434Entry(m_device_id, index) == 0) {
        m_disarm_state = W8_TRAP_DISARM_SPRUNG;
        duration = (CurrentGame().rng.below(0x18) + 0x32) * kMovementSpeedStep;
    } else {
        roll = static_cast<int>(CurrentGame().rng.below(0x64));
        if (roll < (chance * chance) / 100) {
            m_column_filled[index] = 1;
            m_disarm_state = W8_TRAP_DISARM_COLUMN_SUCCESS;
            duration = 1.0f;
        } else {
            m_disarm_state = W8_TRAP_DISARM_SPRUNG;
            duration = (CurrentGame().rng.below(0x31) + 0x32) * kMovementSpeedStep;
        }
    }
    m_action_controls[1]->SetEnabled(false);
    m_action_controls[2]->SetEnabled(false);
    m_action_controls[3]->SetEnabled(false);
    m_action_controls[4]->SetEnabled(false);
    actions_dirty = true;
    m_text_panel->BeginProgress(Data().strings[0x7b5], duration, hold);
    m_sound_handle = wiz8::sound::play(g_trap_sounds[index]);
}

int W8TrapInteraction::GetTrapInteractionChance() const
{
    int chance;
    int skill;

    chance = m_difficulty;
    if (chance < 0) {
        chance = 0;
    }
    skill = GetPartySlotLocksTrapsLevel(m_selected_character);
    if (skill > -1) {
        skill += m_target_difficulty * 6;
    }
    skill -= g_table2[chance];
    chance = (skill * 3) / 2 + (11 - g_settings.difficulty) * 10;
    if (chance < 0) {
        chance = 0;
    } else if (chance > 0x63) {
        chance = 0x63;
    }
    return chance;
}

void W8TrapInteraction::OnPrimary(W8TrapInteractionAction action)
{
    int slot;
    int skill;
    int chance;
    int roll;
    float hold;
    float duration;

    if (action == W8_TRAP_ACTION_EXIT) {
        m_disarm_state = W8_TRAP_DISARM_EXIT;
        return;
    }
    if (action != W8_TRAP_ACTION_DISARM) {
        if (action == W8_TRAP_ACTION_TRIGGER) {
            m_disarm_state = W8_TRAP_DISARM_SPRUNG;
            return;
        }
        if (action == W8_TRAP_ACTION_CAST_SPELL) {
            m_disarm_state = W8_TRAP_DISARM_CAST_SPELL;
            return;
        }
        if (action == W8_TRAP_ACTION_USE_ITEM) {
            m_disarm_state = W8_TRAP_DISARM_USE_ITEM;
        }
        return;
    }
    slot = CurrentGame().status.selected_character;
    m_selected_character = slot;
    skill = GetPartySlotLocksTrapsLevel(slot);
    hold = 6.0f - skill * kCameraSnapEpsilon;
    chance = GetTrapInteractionChance();
    roll = static_cast<int>(CurrentGame().rng.below(0x64));
    if (roll < chance) {
        m_disarm_state = W8_TRAP_DISARM_INSPECT_SUCCESS;
        duration = 1.0f;
    } else {
        m_disarm_state = W8_TRAP_DISARM_SPRUNG;
        duration = (CurrentGame().rng.below(0x18) + 0x4b) * kMovementSpeedStep;
    }
    m_action_controls[1]->SetEnabled(false);
    m_action_controls[2]->SetEnabled(false);
    m_action_controls[3]->SetEnabled(false);
    m_action_controls[4]->SetEnabled(false);
    actions_dirty = true;
    m_text_panel->BeginProgress(Data().strings[0x7b6], duration, hold);
    m_sound_handle = wiz8::sound::stream(g_trap_inspection_sound);
}

void W8TrapInteraction::Update()
{
    W8MainGameTextPanel* panel;
    int column;
    int elapsed;
    float progress;

    if (m_disarm_state == W8_TRAP_DISARM_WAIT_FOR_TRAP && m_timer.GetProgress() >= 1.0f &&
        PartyPortraitEventsIdle() != 0) {
        m_owner->Run(-1);
        m_disarm_state = W8_TRAP_DISARM_EXIT;
    }
    if (m_disarm_state == W8_TRAP_DISARM_EXIT) {
        EndTrapInteractMode(0);
        return;
    }

    panel = m_text_panel.get();
    if (panel->m_target_changed) {
        elapsed = static_cast<int>(panel->m_timer1.GetProgress());
        if (elapsed != 0) {
            panel->m_marker_anim_time += elapsed;
            panel->m_target_marker_pending = true;
        }
    }

    panel = m_text_panel.get();
    if (panel->m_progress_display) {
        progress = panel->m_timer0.GetProgress();
        if (progress < panel->m_progress_duration) {
            panel->m_progress_elapsed = progress;
            return;
        }
        panel->m_progress_display = false;
        panel->InvalidateAll();
        for (column = 0; column < 8; ++column) {
            if (!panel->cells[column].interaction.selected) {
                panel->cells[column].blocked = false;
            }
        }
    }

    switch (m_disarm_state) {
    case W8_TRAP_DISARM_IDLE:
    case W8_TRAP_DISARM_WAIT_FOR_TRAP:
    case W8_TRAP_DISARM_EXIT:
        break;
    case W8_TRAP_DISARM_INSPECT_SUCCESS:
        m_disarm_state = W8_TRAP_DISARM_IDLE;
        ApplyInspectSuccess();
        return;
    case W8_TRAP_DISARM_COLUMN_SUCCESS:
        m_disarm_state = W8_TRAP_DISARM_IDLE;
        m_sound_handle = nullptr;
        PracticeCharacterSkill(CurrentGame(), &CurrentGame().party.characters[m_selected_character],
                               W8_SKILL_LOCKS_TRAPS, 1, false);
        RefreshActionPanel();
        for (column = 0; column < 8; ++column) {
            if (GetTable650434Entry(m_device_id, column) != 0 && m_column_filled[column] == 0) {
                return;
            }
        }
        m_disarm_state = W8_TRAP_DISARM_COMPLETE;
        return;
    case W8_TRAP_DISARM_COMPLETE:
        panel = m_text_panel.get();
        panel->EnableInput(false);
        panel->m_key_handler->m_range.SetActive(false);
        input.setEnabled(false);
        CompleteTrapDisarm(m_owner);
        EndTrapInteractMode(0);
        return;
    case W8_TRAP_DISARM_SPRUNG:
        if (m_sound_handle != nullptr) {
            wiz8::sound::stop(m_sound_handle);
            m_sound_handle = nullptr;
        }
        // c-style-cast-ok: SGP spells filenames STR (UINT8*), the historical ABI boundary
        wiz8::sound::play(g_trap_sprung_sound);
        EnablePanelInput(false);
        ResolveSprungTrap(m_owner);
        m_disarm_state = W8_TRAP_DISARM_WAIT_FOR_TRAP;
        m_timer.SetDuration(2.0f);
        m_timer.Restart();
        return;
    case W8_TRAP_DISARM_CAST_SPELL:
        m_disarm_state = W8_TRAP_DISARM_IDLE;
        CastTrapSpell();
        return;
    case W8_TRAP_DISARM_USE_ITEM:
        m_disarm_state = W8_TRAP_DISARM_IDLE;
        UseTrapItem();
        return;
    case W8_TRAP_DISARM_KNOCK_KNOCK_SUCCESS:
    case W8_TRAP_DISARM_KNOCK_KNOCK_FAILURE:
        if (m_timer.GetProgress() >= 1.0f) {
            m_disarm_state = m_disarm_state == W8_TRAP_DISARM_KNOCK_KNOCK_SUCCESS
                                 ? W8_TRAP_DISARM_COMPLETE
                                 : W8_TRAP_DISARM_SPRUNG;
        }
        break;
    }
}

void W8TrapInteraction::RefreshActionPanel()
{
    int slot = CurrentGame().status.selected_character;
    int skill = GetPartySlotLocksTrapsLevel(slot);
    int i;

    m_action_controls[4]->SetEnabled(skill >= 0);
    for (i = 0; i < 8; ++i) {
        if (!m_text_panel->cells[i].interaction.selected) {
            m_text_panel->cells[i].blocked = skill < 0;
        }
    }
    m_action_controls[1]->SetEnabled(CanCastTrapSpell(slot));
    m_text_panel->m_values = m_slot_attempted[slot] ? m_slot_columns[slot] : nullptr;
    m_text_panel->RefreshCellImages();
    m_action_controls[2]->SetEnabled(true);
    m_action_controls[3]->SetEnabled(true);
    m_status_panel->RefreshStatusTexts();
    actions_dirty = true;
}

void W8TrapInteraction::EnablePanelInput(bool enable)
{
    W8MainGameTextPanel* panel = m_text_panel.get();

    panel->EnableInput(enable);
    panel->m_key_handler->m_range.SetActive(enable);
    input.setEnabled(enable);
}

void W8TrapInteraction::ApplyInspectSuccess()
{
    int chance;
    int column;
    int roll;

    m_sound_handle = nullptr;
    PracticeCharacterSkill(CurrentGame(), &CurrentGame().party.characters[m_selected_character],
                           W8_SKILL_LOCKS_TRAPS, 1, false);
    RefreshActionPanel();
    chance = GetTrapInteractionChance();
    for (column = 0; column < 8; ++column) {
        roll = static_cast<int>(CurrentGame().rng.below(0x64));
        if (roll < chance) {
            m_slot_columns[m_selected_character][column] = GetTable650434Entry(m_device_id, column);
        } else {
            m_slot_columns[m_selected_character][column] = 2;
        }
    }
    m_slot_attempted[m_selected_character] = 1;
    m_text_panel->m_values = m_slot_columns[m_selected_character];
    m_text_panel->RefreshCellImages();
}

void W8TrapInteraction::CastTrapSpell()
{
    const int slot = CurrentGame().status.selected_character;
    if (!CanCastTrapSpell(slot)) {
        return;
    }
    const int spell =
        CanCharacterCastSpell(&CurrentGame().party.characters[slot], W8_SPELL_DIVINE_TRAP) ? 0x12
                                                                                           : 0x27;
    EndTrapInteractMode(1);
    BeginSpellCast(spell, -1, W8_DIALOGUE_LAYOUT_UNSET);
}

void W8TrapInteraction::UseTrapItem()
{
    EndTrapInteractMode(1);
    OpenUseItemSelectView(CurrentGame().status.selected_character);
}

/* Open the trap interaction over a trigger, or re-raise its panels while one
   is suspended; a trigger in state 9 is already open. Outside those paths a
   random party member eats the detection event and the detect sound plays. */
int OpenTrapInteraction(Trigger* trigger)
{
    unsigned int event_type;

    // A lock interaction, active or suspended, excludes a trap interaction.
    if (LockInteraction()) {
        return 0;
    }
    const bool resume = TrapInteractionSuspended();
    if (IsTrapInteractionActive() || (resume && trigger != 0)) {
        if (TrapInteraction()->m_disarm_state == W8_TRAP_DISARM_WAIT_FOR_TRAP) {
            return 1;
        }
        QueueInteractionNotice(g_trap_notice_event);
        wiz8::sound::play(s_trap_detect);
        return 1;
    }
    if (CurrentGame().combat.has_value()) {
        QueueInteractionNotice(g_trap_notice_event);
        wiz8::sound::play(s_trap_detect);
        return 1;
    }
    MainGame()->trap_active = true;
    if (MainGameHud()->open_submenu) {
        DestroySubMenuControls();
    }
    CloseMainGameOverlays();
    RefreshMainGameLayout();
    SelectTextBox(2);
    ResetEditorStatusLine(-1);
    ResetLevelDataVectors();
    RequestRedrawCombatBar();
    RequestRedraw(W8_MAIN_REDRAW_SUBMENU_BUTTONS);
    if (resume) {
        TrapInteraction()->EnablePanelInput(true);
        TrapInteraction()->m_text_panel->InvalidateAll();
        TrapInteraction()->RefreshActionPanel();
        return 1;
    }
    if (trigger->lock_state.device_id == -1) {
        SelectTrapType(trigger);
    }
    MainGame()->trap_interaction = std::make_unique<W8TrapInteraction>(trigger);
    if (trigger->lock_state.last_interaction_clock == -1 ||
        0x3840 < static_cast<unsigned int>(CurrentGame().status.world_clock -
                                           trigger->lock_state.last_interaction_clock)) {
        event_type = 0x19;
        if (0x13 < CurrentGame().rng.below(100)) {
            event_type = g_trap_notice_event;
        }
        QueueInteractionNotice(event_type);
    }
    trigger->lock_state.last_interaction_clock = CurrentGame().status.world_clock;
    wiz8::sound::play(s_trap_detect);
    return 1;
}

void UpdateTrapInteraction(void)
{
    TrapInteraction()->Update();
}

/* Redraw the trap interaction's text, status and action panels. */
void RedrawTrapInteractionPanels(void)
{
    TrapInteraction()->m_text_panel->Redraw(GetFrameBufferSurface(), false);
    TrapInteraction()->m_status_panel->Redraw(GetFrameBufferSurface(), false);
    TrapInteraction()->DrawActions(GetFrameBufferSurface());
}

/* Leave trap interact mode. With `suspend` clear the interaction object is
   destroyed outright; otherwise it stays alive with its panels inactive so the
   session can resume. Either path returns to the UI mode the interaction was
   opened from and repaints the affected regions. */
void EndTrapInteractMode(char suspend)
{
    MainGame()->trap_active = false;
    if (suspend != 0) {
        TrapInteraction()->EnablePanelInput(false);
    } else {
        MainGame()->trap_interaction.reset();
        ClearLevelMovementStopped();
    }
    SelectTextBox(0);
    RefreshMainGameLayout();
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
    RequestRedrawCombatBar();
    RequestRedraw(W8_MAIN_REDRAW_SUBMENU_BUTTONS);
}

/* Per-frame trap interact service: repaint the screen's action panel. */
void RefreshMainGameActionPanel(void)
{
    TrapInteraction()->RefreshActionPanel();
}

W8NpcDialogueTextController::W8NpcDialogueTextController()
{
    visible = 0;
    line_height = scroll_height = CatalogArtwork::Load(0x1a9, 0).size(3).y;
    margin = CatalogArtwork::Load(0x1a9, 0).size(4).y;
    W8ControlsRect bounds{0x1e2, 0x12d - line_height, 0x258, 0x12d};
    text_area.Configure(&bounds, g_wiz_text_font_secondary,
                        g_W8DialogTextAreaShortenText | g_W8DialogTextAreaPointSelection |
                            g_W8DialogTextAreaNoWrap);
    text_area.SetLineHeight(scroll_height);
    expanded_target = &input.add(SDL_Rect{0, 0, 1, 1}, [this](const UiPointerEvent& event) {
        if (event.action != UiPointerAction::Up || event.captured_buttons)
            PlayButtonSound(event.action);
        if (event.action == UiPointerAction::Up && event.button == UiPointerButton::Left &&
            (event.captured_buttons & SDL_BUTTON_LMASK))
            AfterUiDispatch([this, point = event.position] {
                SelectTranscriptKeywordAtPoint(point.x, point.y);
            });
        else if (event.action == UiPointerAction::Wheel) {
            if (event.wheel > 0)
                text_area.ScrollUp(false);
            else
                text_area.ScrollDown(false);
            InvalidateAll();
        } else if (event.action == UiPointerAction::Leave) {
            if (text_area.ClearPointSelection()) InvalidateLayout();
        } else if (event.action == UiPointerAction::Move) {
            if (text_area.UpdateSelectionFromPoint(event.position.x, event.position.y))
                InvalidateLayout();
        }
        return UiResult::Handled;
    });
    expanded_target->setEnabled(false);
}

void W8NpcDialogueTextController::Redraw(SDL_Surface& target, bool full_redraw)
{
    if (!m_active) return;
    full_redraw |= m_dirty;
    if (full_redraw) {
        const auto artwork = CatalogArtwork::Load(0x1a9, 0);
        artwork.drawAndInvalidate(target, 2, 0x1dc, 0x11b);
        if (visible == 1) {
            int height = line_height;
            int top = 0x11b - height + margin;
            while (height < scroll_height) {
                artwork.drawAndInvalidate(target, 3, 0x1dc, top);
                top -= line_height;
                height += line_height;
            }
            artwork.drawAndInvalidate(target, 4, 0x1dc, line_height - margin + top);
        }
    }
    const bool text_dirty = text_area.TakeDirty();
    if (full_redraw || text_dirty) {
        text_area.Paint(target);
        const auto& bounds = text_area.m_bounds;
        InvalidateRegion(bounds.left, bounds.top, bounds.right, bounds.bottom);
    }
    m_dirty = false;
}

bool W8NpcDialogueTextController::AddTranscriptEntry(std::string_view text,
                                                     W8DialogueCategory category, char mark)
{
    int index;
    int added;

    for (index = 0; index < static_cast<int>(text_area.m_all_lines.size()); ++index) {
        if (CompareTextIgnoreAsciiCase(text_area.GetEntryText(index).value(), text) == 0) {
            return false;
        }
    }
    added = text_area.AddEntry({}, text, 0, 7, static_cast<unsigned char>(category));
    InvalidateLayout();
    if (mark != 0) {
        text_area.SetEntryMarked(added, true);
    }
    if (text_area.m_all_lines.size() > 1) {
        for (index = 0; index < static_cast<int>(text_area.m_all_lines.size()); ++index) {
            if (CompareTextIgnoreAsciiCase(text_area.GetEntryText(index).value(),
                                           " [No Keywords]") == 0) {
                text_area.RemoveEntry(index);
                InvalidateAll();
                break;
            }
        }
    }
    return true;
}

bool W8NpcDialogueTextController::IsSlotPortraitTranscriptCovered(unsigned int party_slot)
{
    int top;

    if (visible == 1) {
        top = ((1 - scroll_height / line_height) * line_height - margin) + 0x11b;
        switch (party_slot) {
        case 1:
            if (top < 0x67) {
                return true;
            }
            break;
        case 3:
            if (top < 0xbc) {
                return true;
            }
            break;
        case 5:
            if (top < 0x111) {
                return true;
            }
            break;
        case 7:
            return true;
        }
    }
    return false;
}

void W8NpcDialogueTextController::Collapse()
{
    W8ControlsRect bounds;
    int top;

    visible = 0;
    top = (0x11b - (scroll_height / line_height) * line_height) - margin;
    ClearSurfaceRect(0x1dc, top, 0x269, 0x140);
    InvalidateAll();
    InvalidateRegion(0x1dc, top, 0x269, 0x140);
    if (top < 0x67) {
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_1);
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_3);
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_5);
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_7);
    } else if (top < 0xbc) {
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_3);
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_5);
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_7);
    } else if (top < 0x111) {
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_5);
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_7);
    } else {
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_7);
    }
    scroll_height = line_height;
    bounds.left = 0x1dc + 6;
    bounds.right = 0x1dc + 0x7c;
    bounds.bottom = 0x11b + 0x12;
    bounds.top = bounds.bottom - line_height;
    text_area.Configure(&bounds, g_wiz_text_font_secondary,
                        g_W8DialogTextAreaNoWrap | g_W8DialogTextAreaPointSelection |
                            g_W8DialogTextAreaShortenText);
    expanded_target->setEnabled(false);
    g_npc_interaction_state->ui->panels[3].dirty = true;
}

void W8NpcDialogueTextController::Expand()
{
    W8ControlsRect bounds;
    int line_height;
    int text_height;

    visible = 1;
    line_height = text_area.GetLineHeight();
    text_height = line_height * text_area.GetTotalLineCount();
    if (text_height >= 0xff) {
        text_height = 0xff;
    }
    bounds.left = 0x1dc + 6;
    bounds.right = 0x1dc + 0x7c;
    bounds.bottom = 0x11b + 0x12;
    bounds.top = bounds.bottom - text_height;
    scroll_height = text_height;
    text_area.Configure(&bounds, g_wiz_text_font_secondary,
                        g_W8DialogTextAreaNoWrap | g_W8DialogTextAreaPointSelection |
                            g_W8DialogTextAreaShortenText);
    if (scroll_height != 0xff) {
        text_area.SetFirstVisibleEntry(0);
    }
    expanded_target->setShape(SDL_Rect{bounds.left, bounds.top, bounds.right - bounds.left + 1,
                                       bounds.bottom - bounds.top + 1});
    input.setEnabled(true);
    expanded_target->setEnabled(true);
    InvalidateAll();
}

void W8NpcDialogueTextController::ClearBackground()
{
    int top;

    top = ((1 - scroll_height / line_height) * line_height - margin) + 0x11b;
    EraseRegion(0x1dc, top, 0x269, 0x140);
    if (top < 0x67) {
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_1);
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_3);
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_5);
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_7);
        return;
    }
    if (top < 0xbc) {
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_3);
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_5);
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_7);
        return;
    }
    if (top < 0x111) {
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_5);
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_7);
        return;
    }
    RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_7);
}

bool W8NpcDialogueTextController::IsExpanded()
{
    return scroll_height == 0xff;
}

void W8NpcDialogueTextController::SetTranscriptCategoryFilter(W8DialogueCategory category)
{
    text_area.SetCategoryFilter(category);
    Collapse();
    Expand();
    SyncNpcDialogueTranscriptScrollButtons();
}

void W8NpcDialogueTextController::RestoreTranscriptEntries()
{
    W8DialogueTranscriptRecord* record;
    W8NpcDialogueTextController* controller;
    int index;

    for (index = 0; index < static_cast<int>(g_npc_interaction_state->dialogue_transcript.size());
         ++index) {
        record = g_npc_interaction_state->dialogue_transcript[index].get();
        AddTranscriptEntry(record->text, record->category, 0);
    }
    InvalidateAll();
    if (g_npc_interaction_state->dialogue_transcript.empty()) {
        g_npc_interaction_state->dialogue_category_filter = W8_DIALOGUE_CATEGORY_ALL;
        controller = g_npc_interaction_state->ui->transcript.get();
        controller->SetTranscriptCategoryFilter(g_npc_interaction_state->dialogue_category_filter);
        SyncDialogueCategoryButtons();
        AddTranscriptEntry(" [No Keywords]", W8_DIALOGUE_CATEGORY_ALL, 0);
    }
}

void RefreshFlaggedMainGameState(void)
{
    if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME && MainGameHud() != nullptr &&
        MainGameHud()->keyboard_menu_open) {
        RefreshKeyboardMenuRows();
    }
}

/* Invalidate rectangles keyed by redraw bits. The
   first eight entries are the portrait-slot columns (zero base rects, scaled
   by portrait_hover_x_origin and shifted by portrait_refresh_image). */
struct W8MainGameInvalidateRect {
    int left;
    int top;
    int right;
    int bottom;
    int x_scale;
    int apply_y_offset;
    unsigned int refresh_image_index;
};

extern W8MainGameInvalidateRect g_main_game_invalidate_rects[23];

W8LevelRuntimeBlock* MainGameHud()
{
    auto* main_game = MainGame();
    return main_game ? main_game->hud.get() : nullptr;
}

bool FormationPanelOpen()
{
    const auto* main_game = MainGame();
    return main_game && main_game->formation_panel_open;
}

bool PartyMovementPanelShown()
{
    const auto* main_game = MainGame();
    return main_game && main_game->party_movement_panel;
}

/* Reset the owned Main Game state and its coupled UI/selection state. */
void ResetMainGameScreenState(void)
{
    if (MainGameHud()) {
        gpSCSV.reset();
        ResetUseItemSelection();
        *MainGameHud() = W8LevelRuntimeBlock{};
        TurnPartyToImmediate(CurrentGame().status.party_facing, 0);
        CurrentGame().activity.conversation = false;
        if (auto* main_game = MainGame()) {
            main_game->lock_active = false;
            main_game->trap_active = false;
            main_game->lock_interaction.reset();
            main_game->trap_interaction.reset();
            main_game->dismissed_npc_slot = -1;
            main_game->formation_panel_open = false;
            main_game->item_drag.reset();
        }
        CurrentGame().surprise_possible = false;
        CurrentGame().advancement.held = false;
        CurrentGame().advancement.paused = false;
        CurrentGame().gameplay_timer.Restart();
        ResetAssayTab();
        CurrentGame().aging_held = false;
        ResetTargetingState();
    }
}

void SetWorldResumeInput(bool enabled)
{
    if (MainGame()) MainGame()->resume_world = enabled;
}

MainGameScreen::MainGameScreen()
{
    for (int slot = 0; slot < 8; ++slot) {
        const int row = slot / 2;
        const bool right = slot % 2;
        portraits[slot] = std::make_unique<UiLayer>(UiLayerOrder::PortraitSlots);
        auto& input = *portraits[slot];
        auto& targets = portrait_targets[slot];
        const auto add = [&](int index, HitShape shape, auto handler, int help = -1) {
            targets[index] =
                &input.add(shape, [this, slot, index, handler](const UiPointerEvent& event) {
                    return handler(event, *portrait_targets[slot][index], slot);
                });
            if (help != -1) targets[index]->setTooltip([help] { return Data().strings[help]; });
        };
        constexpr int orb_y[] = {27, 112, 198, 283};
        constexpr int portrait_y[] = {18, 103, 189, 274};
        constexpr int portrait_h[] = {72, 72, 67, 71};
        constexpr int sidebar_y[] = {42, 125, 210, 295};
        add(0, UiCircle{{right ? 608 : 32, orb_y[row]}, 8}, PortraitConditionOrbInput, 25);
        add(1, UiCircle{{right ? 544 : 95, orb_y[row]}, 8}, PortraitEnchantmentOrbInput, 26);
        add(2, SDL_Rect{right ? 535 : 23, portrait_y[row], 82, portrait_h[row]},
            PortraitSelectInput);
        targets[3] = &input.add(SDL_Rect{right ? 539 : 21, 91 + row * 85, right ? 74 : 85, 11}, {});
        add(4, SDL_Rect{right ? 621 : 4, sidebar_y[row], right ? 15 : 16, 49},
            PortraitAssaySidebarInput, 28);
        add(5, SDL_Rect{right ? 517 : 109, sidebar_y[row], 15, 49}, PortraitOverlayHoverInput, 24);
        combat_targets[slot] =
            &combat.add(SDL_Rect{right ? 595 : 24, 68 + row * 85, 19, 18},
                        [this, slot](const UiPointerEvent& event) {
                            return PartyCombatActionInput(event, *combat_targets[slot], slot);
                        });
        edge_targets[slot] =
            &edges.add(SDL_Rect{right ? 617 : 0, 18 + row * 85, right ? 23 : 22, 85},
                       [this, slot](const UiPointerEvent& event) {
                           return PortraitSelectInput(event, *edge_targets[slot], slot);
                       });
        input.setEnabled(false);
    }
    radar_target = &radar.add(UiCircle{{75, 402}, 46}, RadarMapButtonInput);
    radar_target->setTooltip([] { return Data().strings[31]; });
    auto& board = formation.add(UiCircle{{564, 402}, 40}, HandleFormationBoard);
    board.setTooltip([] { return Data().strings[32]; });
    formation.add(SDL_Rect{512, 358, 106, 93}, {});
    monster_list = &world.add(SDL_Rect{0, 0, 1, 1}, MonsterListInput);
    monster_list->setTooltip([] { return Data().strings[36]; });
    world_view = &world.add(SDL_Rect{0, 0, 1, 1}, WorldViewInput);
    hud_buttons.input.setEnabled(false);
    combat.setEnabled(false);
    edges.setEnabled(false);
    radar.setEnabled(false);
    formation.setEnabled(false);
    world.setEnabled(false);
}

void EnableMainGamePortraitSlot(unsigned int slot)
{
    if (MainGame() && slot < 8) MainGame()->portraits[slot]->setEnabled(true);
}
void DisableMainGamePortraitSlot(unsigned int slot)
{
    if (MainGame() && slot < 8) MainGame()->portraits[slot]->setEnabled(false);
}

/* Enter the live game screen, constructing its owned native state on first entry. */
bool MainGameScreen::enter(const ScreenRequest&)
{
    int display_mode;
    W8Difficulty difficulty = CurrentGame().status.difficulty;

    if (!hud) {
        hud = std::make_unique<W8LevelRuntimeBlock>();
        ResetMainGameScreenState();
        LoadMainGameCursorResources();
    }
    CurrentGame().level_up_check_due = true;
    CreatePartyPortraitEventRegions();

    InitTextBoxRegions();
    ConfigureMainGameHudButtons();
    CreateLevelButtons();
    CreateConditionButtons();
    InitializeMainGameLevelBlock();
    ScrollTextBoxToCursor();
    g_world_render_enabled = 1;
    g_monster_shadow_updates_enabled = 1;
    ClearPrimarySurface();
    if (GetWorld() && GetWorld()->environment.fog_enabled) {
        EnableSky();
    } else {
        DisableSky();
    }
    if (TakePendingSaveFlag()) {
        ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[0x782]);
    }
    if (g_settings.difficulty != difficulty) {
        g_settings.difficulty = difficulty;
        switch (difficulty) {
        case W8_DIFFICULTY_NOVICE:
            display_mode = 0x7f8;
            break;
        case W8_DIFFICULTY_NORMAL:
            display_mode = 0x7f9;
            break;
        case W8_DIFFICULTY_EXPERT:
            display_mode = 0x7fa;
            break;
        }
        ShowNotice(W8_FONT_PALETTE_BEIGE,
                   fmt::sprintf(Data().strings[0x78c], Data().strings[display_mode]));
    }
    ResetTransientRenderScenes();
    MoveTimer(W8_MOVE_TIMER_RESUME);
    // Time spent under other screens does not reach the world.
    step_timer.restart();
    {
        W8GameTimer* timer = &CurrentGame().gameplay_timer;
        if (timer->m_flags.paused || (g_shared_timer_paused && !timer->m_flags.raw_time) ||
            g_shared_timer_flag0) {
            timer->m_flags.paused = false;
            timer->m_start = timer->GetTime() - timer->m_start;
            timer->SetDuration(-1.0f);
        }
    }
    DrainInputEventQueue();
    ResetPointerCursor();
    SetTargetingMode(W8_TARGET_NEED_NONE);
    SetPrimarySurfaceTextureHint2Enabled(true);
    if (LockInteractionSuspended()) {
        OpenLockInteraction(0);
    }
    if (TrapInteractionSuspended()) {
        OpenTrapInteraction(0);
    }
    if (item_drag && item_drag->pending) {
        if (IsPartySlotEligible(item_drag->actor)) {
            SelectPartyCharacter(item_drag->actor);
            OpenUseItemSelectView(item_drag->actor);
            SelectCurrentUseItemLine();
        } else {
            item_drag.reset();
            CurrentGame().aging_held = false;
        }
    }
    if (!CurrentGame().combat.has_value()) {
        StartLevelMusic(1, 1);
    }
    return true;
}

/* While the party is idle, arm a one-minute countdown after input and, once the
   cursor has also been still for a minute and that countdown expires, advance
   ambient follow-up chatter. Busy modes keep refreshing the countdown and
   clear any armed follow-up bits. */
/* The "Enter <level>?" confirmation's completion callback: a cancelled dialog
   restores the camera the trigger cached, a confirmed one runs the pending
   transition teardown. */
/* Leave modes 3/5/6, then hand the level change to the please-wait screen.
   Retail never calls this out-of-line; the same teardown is emitted inline at
   the dialog callback and at RequestLevelTransition's unconfirmed path. */
void BeginLevelTransition(int level, int entry)
{
    SetMainGameMode(W8_MAIN_GAME_DEFAULT);
    RequestScreen({.id = W8_SCREEN_PLEASE_WAIT,
                   .mode = W8_PLEASE_WAIT_CHANGE_LEVEL,
                   .parameter = level,
                   .parameter_2 = entry});
}

void TickAmbientFollowUpIdle(unsigned char input_handled)
{
    if (!CurrentGame().combat.has_value() && !gpSCSV && !ItemUseDraft() &&
        !CurrentGame().activity.conversation && !IsLockInteractionActive() &&
        !IsTrapInteractionActive() && !CurrentGame().surprise_possible) {
        if (input_handled == 0) {
            if (GetMillisecondsSinceCursorMove() > 60000) {
                if (TimeUntilDeadline(MainGameHud()->countdown0) != 0) {
                    return;
                }
                CurrentGame().speech.ProcessFollowUpEvents();
                return;
            }
        } else {
            MainGameHud()->countdown0 = MonotonicMilliseconds() + (60000);
        }
    } else {
        MainGameHud()->countdown0 = MonotonicMilliseconds() + (60000);
    }
    CurrentGame().speech.follow_up.reset();
}

/* Leave one main-game mode for another: mode 3 ends a live NPC dialogue
   session, mode 5 releases its screen state, and mode 6 releases the dialogue
   highlight sprite then clears and dirties the dialogue rectangle when it
   crosses the viewport band. The new mode is stored last. */
void SetMainGameMode(W8MainGameMode mode)
{
    switch (g_main_game_mode) {
    case W8_MAIN_GAME_NPC_DIALOGUE:
        CloseNpcDialogueIfActive();
        break;
    case W8_MAIN_GAME_HIGHLIGHT_OVERLAY:
        if (MainGameHud()->highlight_graphic != 0) {
            MainGameHud()->highlight_graphic.reset();
            MainGameHud()->highlight_graphic = 0;
            if (g_main_game_mode != W8_MAIN_GAME_HIGHLIGHT_OVERLAY) {
                break;
            }
        }
        ClearHighlightOverlayRegion();
        break;
    default:
        break;
    }
    g_main_game_mode = mode;
}

bool ProcessMainGameInput(void)
{
    if (IsModalOpen()) return false;
    return PumpUiInput({
        .before =
            [](const InputEvent& input) {
                if (g_mouselook_active && HandleMouselookInput(&input)) return InputRoute::Handled;
                if (MainGame()->resume_world && !IsModalOpen() && HandleWorldResumeInput(&input))
                    return InputRoute::Handled;
                return InputRoute::Pass;
            },
        .unhandled =
            [](const InputEvent& input) {
                switch (input.type) {
                case InputEventType::LeftButtonDown:
                case InputEventType::LeftButtonUp:
                case InputEventType::RightButtonDown:
                case InputEventType::RightButtonUp:
                    return InputRoute::Handled;
                default:
                    break;
                }
                if (HandleMainGameInputEvent(&input) == 0) {
                    ResetMainGameMode();
                    return InputRoute::Stop;
                }
                return InputRoute::Handled;
            },
        .track_hover = !MainGame()->resume_world,
    });
}

/* The active screen's frame, including modal input and pending transitions. */
void MainGameScreen::frame()
{
    if (g_dev_mode) {
        RequestExitScreen();
    }
    ApplyPendingMouselook();
    ApplyPendingTooltip();
    if (g_modal_owner) {
        // A modal takes the pointer back from mouselook and from any targeting cursor.
        CancelMouselook();
        ResetPointerCursor();
    }
    // A screen fade, and the party-death or ending screen it leads to, takes the frame.
    if (MainGameHud()->screen_fade_running) {
        if (UpdateScreenFade()) return;
    } else if (MainGameHud()->review_transition_active) {
        // The review transition holds the world, so its time is not stepped later.
        PumpReviewTransition();
        step_timer.restart();
        return;
    }
    // Nobody left who can act, or the party fell out of the world: the game is lost.
    if (!AnyCharacterActive() || CurrentGame().party_moving) BeginPartyDeath();
    if (dismissed_npc_slot != -1) UpdateNpcPartyMember(std::exchange(dismissed_npc_slot, -1));
    if (std::exchange(resume_conversation, false)) SyncDialogueNpcStateAndMarkPending();
    if (SkillIncreasesShownNow(CurrentGame())) FlushDeferredSkillNotices(CurrentGame());
    SyncPartyPortraitVitalsBars();
    if (g_modal_owner && ProcessModalOwner()) RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_PANEL);
    SetWorldResumeInput(CurrentGame().advancement.paused && !InteractionBlocksWorldAdvancement());
    DrainNpcDialogueDeferralInput();
    FlushInputWhileWorldCursorGate();
    TickPartyPortraitFx();
    if (CurrentGame().combat.has_value()) {
        UpdateCombatPortraitStatus();
    }
    CurrentGame().status.world_suspended = false;
    if (MainGameHud()->keyboard_menu_open || MainGameHud()->combat_slot != -1) {
        UpdateKeyboardMenu();
    }
    if (CurrentGame().combat.has_value()) {
        for (int slot = 0; slot < 8; ++slot) {
            if (!CurrentGame().party.rows[slot].fOccupied ||
                CurrentGame().party.characters[slot].highest_condition > W8_CONDITION_UNCONSCIOUS ||
                (MainGameHud()->keyboard_menu_open && MainGameHud()->combat_slot == slot)) {
                MainGame()->combat_targets[slot]->setEnabled(false);
            } else {
                MainGame()->combat_targets[slot]->setEnabled(true);
            }
        }
    }
    if (MainGameHud()->portrait_flash != 0 && TimeUntilDeadline(MainGameHud()->clock) == 0) {
        MainGameHud()->portrait_flash = 0;
        RequestPartySlotRedraw(CurrentGame().status.selected_character);
    }
    if (CurrentGame().status.text_box_lines_shown[CurrentGame().status.text_line_cursor] <
        CurrentGame().status.text_box_lines_used[CurrentGame().status.text_line_cursor]) {
        AdvanceNoticeLine(CurrentGame().status.text_line_cursor);
    }
    if (IsScreenTransitionPending()) {
        // A pending transition holds the world until another screen covers it.
        step_timer.restart();
        DrawMainGameScreen();
        return;
    }
    SDL_Point point;
    point = CursorPosition();
    if (!IsWorldCursorVisible()) {
        if (!g_modal_owner) {
            if ((!ShouldDeferCharacterEventForNpcScript(true) ||
                 !CurrentGame().activity.conversation) &&
                !CurrentGame().status.world_cursor_gate)
                UpdateUiPointer(point.x, point.y);
        } else {
            for (int slot = 0; slot < 8; ++slot) {
                if (CurrentGame().party.rows[slot].fOccupied &&
                    (MainGame()->portrait_targets[slot][2]->hitAt(point) ||
                     MainGame()->edge_targets[slot]->hitAt(point))) {
                    UpdateUiPointer(point.x, point.y);
                    break;
                }
            }
        }
    } else
        UpdateWorldCursor();
    if (!MainGameHud()->keyboard_menu_open && MainGameHud()->hover_combat_slot != -1 &&
        !MainGame()->combat_targets[MainGameHud()->hover_combat_slot]->hitAt(point))
        MainGameHud()->hover_combat_slot = -1;
    TickAmbientFollowUpIdle(ProcessMainGameInput());
    if (!IsRecordModeActive()) {
        HandleManualCameraHotkeys();
    }
    if (!g_camera_path_active && g_mouselook_active && g_mouselook_left_held) {
        MainGameHud()->camera_motion_flags |= W8_CAMERA_MOTION_FORWARD;
        if (GetInputState().modifiers & SHIFT_DOWN) {
            MainGameHud()->camera_motion_flags |= W8_CAMERA_MOTION_FAST;
        }
    }
    if (!IsScreenTransitionPending()) {

        UpdateWorldCameraAndPaths(GetWorld(), MainGameHud()->camera_motion_flags);
        if (GetSecondaryWorld() && !g_camera_path_active) {
            UpdateWorldCameraAndPaths(GetSecondaryWorld(),
                                      MainGameHud()->camera_motion_flags | W8_CAMERA_MOTION_SKIP);
        }

        ApplyWorldUpdateFlags(GetWorld(), MainGameHud()->world_update_flags);
        if (GetSecondaryWorld() &&
            (MainGameHud()->world_update_flags & W8_WORLD_CHANGE_FAR_CLIP) == 0) {
            ApplyWorldUpdateFlags(GetSecondaryWorld(), MainGameHud()->world_update_flags);
        }
        MainGameHud()->world_update_flags = 0;
        MainGameHud()->camera_motion_flags = 0;
        if (g_settings.main_ui_mode != W8_MAIN_UI_MODE_PORTRAITS) {
            UpdateFormationPortraitRefresh();
        }
        if (!IsWorldCursorVisible() && !g_mouselook_active) AnimateShownCursor();
        CurrentGame().advance(step_timer.lap(),
                              {.world = {.world = InteractionBlocksWorldAdvancement(),
                                         .party = LockInteraction() || TrapInteraction()},
                               .combat = gpSCSV || ItemUseDraft()});
        AnimatePartyPortraits();
        if (CurrentGame().combat.has_value() && MainGameHud()->refresh_combat_panel &&
            !TimeUntilDeadline(MainGameHud()->combat_panel_timer)) {
            RefreshMonsterTargetCounts();
            MainGameHud()->combat_panel_timer = MonotonicMilliseconds() + (500);
            MainGameHud()->refresh_combat_panel = 0;
        }
        if (gpSCSV) CommitSpellCastingSelection();
        if (ItemUseDraft()) CommitSelectedItemUse();
        if (CurrentGame().activity.conversation) ServiceNpcDialogue();
        if (IsLockInteractionActive()) ProcessLockInteractMode();
        if (IsTrapInteractionActive()) UpdateTrapInteraction();
        int active;
        if (!AnyPropTriggerInView() && !AnyMonsterVisible() && !AnyWorldItemVisible() &&
            !SelectWorldCursorNode()) {
            active = 0;
        } else {
            active = 1;
        }
        SetWorldModelPickingEnabled(active);
        if (CurrentGame().level_up_check_due && !CurrentGame().activity.conversation &&
            !CurrentGame().combat.has_value()) {
            RefreshLevelUpReadyNotices();
        }
        if (CurrentGame().status.selected_character == -1) {
            int next = GetNextCharacter(1, 1, -1);
            if (next == -1) {
                Fail("iNextChar != BAD_INDEX");
            }
            SelectPartyCharacter(next);
        }
    }
    DrawMainGameScreen();
}

/* Suspension retains the allocation and resource strip. A full leave also
   unloads the level and releases the per-screen block. */
bool MainGameScreen::leave(ScreenDeparture departure)
{
    const bool leaving = departure == ScreenDeparture::Discard;
    int index;

    if (leaving) {
        OpenModal(nullptr);
        RetireModal(std::move(g_pending_main_game_dialog), false);
        // End a fight while the interface it updates still exists.
        if (CurrentGame().combat.has_value()) {
            CurrentGame().endCombat(true);
        }
    }
    SetMainGameMode(W8_MAIN_GAME_DEFAULT);

    CancelMouselook();
    if (IsWorldCursorVisible()) {
        ToggleWorldCursor();
    }
    ResetPartyPortraitFx();
    if (IsLockInteractionActive()) EndLockInteractMode(0);
    if (IsTrapInteractionActive()) EndTrapInteractMode(0);
    if (gpSCSV) CloseSpellCastingView();
    if (ItemUseDraft()) CloseUseItemSelectView();
    if (FormationPanelOpen()) CloseFormationPanel();
    if (CurrentGame().activity.conversation) EndNpcDialogueSession(false);
    if (MainGameHud()->keyboard_menu_open) CloseKeyboardMenu();
    ReleasePortraitControls();
    ReleaseConditionButtons();
    if (PartyMovementPanelShown()) DisablePartyMovementRegions();
    RestoreCurrentNpcQuoteBubble();
    MoveTimer(W8_MOVE_TIMER_SUSPEND);
    // The world stays covered, and held, until this screen steps it again.
    ApplyWorldAdvancement(CurrentGame(), {.covered = true});

    if (!CurrentGame().gameplay_timer.m_flags.paused) {
        CurrentGame().gameplay_timer.m_flags.paused = true;
        CurrentGame().gameplay_timer.m_start =
            CurrentGame().gameplay_timer.GetTime() - CurrentGame().gameplay_timer.m_start;
    }

    if (leaving) {
        for (index = 0; index < 17; ++index) {
            g_main_game_resource_slots[index].object.reset();
        }
    }

    MainGameHud()->held_item_display = -1;
    ResetPointerCursor();

    CloseMainGameOverlays();

    if (leaving) {
        if (GetLoadedLevelID() != -1) {
            UnloadSkyWorld();
            LeaveLevel();
        }
        if (PartyMovementPanelShown()) {
            ReleasePartyMovement();
        }

        HideNpcQuoteBubbleRegion();
        ShutdownTextBoxRegions();
        DestroyFormationPanel();
        wiz8::sound::release_cache();
        hud.reset();
        ReleaseCatalogArtwork();
    }
    g_world_render_enabled = 0;
    g_monster_shadow_updates_enabled = 0;
    DisableSky();
    DestroySubMenuPanel();
    DestroySpellIconHudControls();
    return true;
}

/* The "leave the current game" confirmation callback: on accept it tears
   down whichever modal mode is up (NPC dialogue, message box or the hover
   overlay), then runs the leave tail - dismiss a live message box, end or
   autosave the game, restore the surprise view, and hand off to the
   transition. */
void OnLeaveGameConfirmClosed(bool accepted)
{
    if (!accepted) {
        return;
    }
    ResetMainGameMode();
}

void OnQuitGameDialogClosed(bool accepted)
{
    if (accepted) {
        if (AnyCharacterActive()) {
            AutoSaveIfAllowed(true);
        }
        RequestExitScreen();
    }
}

/* Ask for part of the screen to be redrawn. A request made while another
   screen is up, or before the HUD state exists, is dropped rather than
   queued - which is what makes the block the only place redraw state lives. */
void RequestRedraw(unsigned int mask)
{
    if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME && MainGameHud() != nullptr) {
        MainGameHud()->redraw_flags |= mask;
    }
}

/* Consume the live redraw_flags word: clear the primary surface, refresh the
   chrome panels the current UI mode owns, and repaint any dirty party slots. */
void ApplyMainGameRedrawFlags(const W8TargetingPresentation& presentation)
{
    unsigned int redraw_flags;
    bool keyboard_menu_invalidate;
    int party_slot;
    int text_box_variant;
    W8TextBoxMode text_box_mode;
    bool show_portraits;
    SDL_Rect combat_clip;
    short health_percent;
    W8MainGameInvalidateRect* portrait_rect;

    redraw_flags = MainGameHud()->redraw_flags;
    if (redraw_flags == W8_MAIN_REDRAW_ALL) {
        ClearPrimarySurface();
    } else if ((redraw_flags & W8_MAIN_REDRAW_LAYOUT) != 0) {
        MainGameHud()->redraw_flags = redraw_flags | W8_MAIN_REDRAW_CHROME;
    }
    if ((MainGameHud()->redraw_flags & W8_MAIN_REDRAW_PORTRAIT_PANEL) != 0) {
        if (CurrentGame().combat.has_value()) {
            RequestRedraw(W8_MAIN_REDRAW_COMBAT_EFFECTS);
        } else {
            InvalidatePortraitPanel();
        }
    }
    if ((MainGameHud()->redraw_flags & W8_MAIN_REDRAW_COMBAT_EFFECTS) != 0) {
        RefreshCombatEffectHud();
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_PANEL);
        for (party_slot = 0; party_slot < 8; ++party_slot) {
            if (MainGameHud()->portrait_refresh_pending[party_slot] != 0) {
                RequestRedraw(1U << (party_slot & 0x1f));
            }
        }
    }
    if ((MainGameHud()->redraw_flags & W8_MAIN_REDRAW_PORTRAIT_PANEL) != 0) {
        if (!CurrentGame().advancement.held) {
            if (PartyMovementPanelShown()) {
                InvalidatePartyMovementPanel();
            }
        } else {
            DrawMainGamePrompt();
        }
        if (!PartyMovementPanelShown()) {
            RefreshTrackedPortraitOverlay();
        }
        if (g_modal_owner != 0) {
            g_modal_owner->RequestModalRedraw();
        }
    }
    if ((MainGameHud()->redraw_flags & W8_MAIN_REDRAW_MONSTER_LIST) != 0) {
        RedrawCombatMonsterList(presentation.subject_valid);
    }
    if ((MainGameHud()->redraw_flags & W8_MAIN_REDRAW_ROOF_AND_SPELL_ICONS) != 0) {
        MainGameHud()->redraw_flags |= W8_MAIN_REDRAW_COMBAT_PROGRESS;
        RedrawRoofButtons();
        RefreshSpellIconHudRows();
    }
    if ((MainGameHud()->redraw_flags & W8_MAIN_REDRAW_COMBAT_PROGRESS) != 0 &&
        CurrentGame().combat.has_value() && CurrentGame().combat->round_active) {
        health_percent = GetCombatActionProgress(0);
        combat_clip = {0, 0, health_percent * 0x11e / 100 + 0xb1, 0x1e0};
        ScopedClipRect clipped(GetFrameBufferSurface(), combat_clip);
        CatalogArtwork::Load(0x8d, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0xb1, 2);
    }
    if (FormationPanelOpen()) {
        if (((MainGameHud()->redraw_flags & W8_MAIN_REDRAW_PORTRAIT_PANEL) == 0) ||
            MainGameHud()->condition_orb_party_slot != -1 ||
            MainGameHud()->enchantment_orb_party_slot != -1 ||
            MainGameHud()->portrait_overlay_party_slot != -1) {
            show_portraits = false;
        } else {
            show_portraits = true;
        }
        RefreshFormationPanel(show_portraits);
    }
    for (party_slot = 0; party_slot < 8; ++party_slot) {
        portrait_rect = &g_main_game_invalidate_rects[party_slot];
        if ((MainGameHud()->redraw_flags & (1U << (party_slot & 0x1f))) == 0) {
            if (CurrentGame().monster_manager_entries[party_slot].portrait_stats_dirty) {
                RedrawPartyPortraitBars(party_slot,
                                        MainGameHud()->portrait_refresh_pending[party_slot] == 0);
            }
            if (CurrentGame().monster_manager_entries[party_slot].keyboard_menu_open) {
                RedrawKeyboardMenuPanel(false);
            }
        } else {
            keyboard_menu_invalidate = false;
            if (MainGameHud()->highlight.LightsPortrait(party_slot) ||
                party_slot == MainGameHud()->held_item_display) {
                keyboard_menu_invalidate = true;
            }
            RedrawPartyPortraitOverlay(party_slot, keyboard_menu_invalidate, true,
                                       MainGameHud()->portrait_refresh_pending[party_slot] == 0);
            if (MainGameHud()->portrait_refresh_pending[party_slot] != 0) {
                InvalidateRegion(portrait_rect->left, portrait_rect->top, portrait_rect->right,
                                 portrait_rect->bottom);
            }
            if (CurrentGame().monster_manager_entries[party_slot].keyboard_menu_open) {
                RedrawKeyboardMenuPanel(true);
            }
        }
    }
    if ((MainGameHud()->redraw_flags & W8_MAIN_REDRAW_LAYOUT) != 0) {
        RedrawLayoutArrowButtons();
        CatalogArtwork::Load(0x7e, 0).drawAndInvalidate(GetFrameBufferSurface(), 1, 0x269, 0x166);
        InvalidateRegion(0x269, 0x166, 0x27f, 0x1c1);
    }
    if (gpSCSV) {
        if ((MainGameHud()->redraw_flags & W8_MAIN_REDRAW_TEXT_BOX) != 0) {
            gpSCSV->dirty[1] = true;
        }
        if ((MainGameHud()->redraw_flags & W8_MAIN_REDRAW_LAYOUT) == 0) {
            SetSpellCastingPanelsActive(false);
        } else {
            SetSpellCastingPanelsActive(true);
        }
        goto finish_mode_overlays;
    }
    if (ItemUseDraft()) {
        if ((MainGameHud()->redraw_flags & W8_MAIN_REDRAW_TEXT_BOX) != 0) {
            InvalidateUseItemSelectPanel();
        }
        if ((MainGameHud()->redraw_flags & W8_MAIN_REDRAW_LAYOUT) == 0) {
            UpdateUseItemSelect(false);
        } else {
            UpdateUseItemSelect(true);
        }
        goto finish_mode_overlays;
    }
    if (!CurrentGame().activity.conversation ||
        (CurrentGame().activity.conversation && g_npc_interaction_state->scripted_dialogue)) {
        if (IsLockInteractionActive()) {
            if ((MainGameHud()->redraw_flags & W8_MAIN_REDRAW_LAYOUT) != 0) {
                InvalidateLockInteractionPanels();
            }
            goto finish_mode_overlays;
        }
        if (IsTrapInteractionActive()) {
            if ((MainGameHud()->redraw_flags & W8_MAIN_REDRAW_LAYOUT) != 0) {
                RedrawTextBoxComplete();
            }
            goto finish_mode_overlays;
        }
        redraw_flags = MainGameHud()->redraw_flags & W8_MAIN_REDRAW_RADAR;
        if (redraw_flags == 0 || MainGameHud()->radar_map_visible == 0) {
            if (redraw_flags != 0) {
                ClearSurfaceRect(0x17, 0x166, 0x80, 0x1c2);
            }
        } else {
            CatalogArtwork::Load(0x84, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0x17,
                                                            0x166);
        }
        redraw_flags = MainGameHud()->redraw_flags & W8_MAIN_REDRAW_TEXT_BOX;
        if (redraw_flags == 0 || MainGameHud()->action_panel_visible == 0) {
            if (redraw_flags != 0) {
                ClearSurfaceRect(0x80, 0x166, 0x200, 0x1c2);
            }
        } else {
            CatalogArtwork::Load(0x87, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0x80,
                                                            0x166);
            text_box_mode = GetTextBoxMode();
            if (text_box_mode == W8_TEXT_BOX_MODE_NOTICE ||
                text_box_mode == W8_TEXT_BOX_MODE_APPEND) {
                text_box_variant = text_box_mode;
            } else if (text_box_mode == W8_TEXT_BOX_MODE_SPECIAL_PANEL) {
                text_box_variant = 2;
            } else {
                DebugMessage(
                    0, fmt::format("WARNING: Drawing text box while Text Buffer = {:d} (AlexP)",
                                   static_cast<unsigned int>(text_box_mode)));
                goto draw_formation_panel;
            }
            CatalogArtwork::Load(0x88, 0).drawAndInvalidate(GetFrameBufferSurface(),
                                                            text_box_variant, 0x8a, 0x168);
        }
    draw_formation_panel:
        redraw_flags = MainGameHud()->redraw_flags & W8_MAIN_REDRAW_FORMATION;
        if (redraw_flags == 0 || !MainGameHud()->formation_board_visible) {
            if (redraw_flags != 0) {
                ClearSurfaceRect(0x200, 0x166, 0x268, 0x1c2);
            }
        } else {
            CatalogArtwork::Load(0x83, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0x200,
                                                            0x166);
        }
        goto finish_mode_overlays;
    }
    if ((MainGameHud()->redraw_flags & W8_MAIN_REDRAW_TEXT_BOX) != 0) {
        InvalidateMainGameActionPanelRect(0);
    }
    if ((MainGameHud()->redraw_flags & W8_MAIN_REDRAW_LAYOUT) == 0) {
        if (HasNpcDialogueDirtyPanels()) {
            ActivateNpcDialoguePanels(false);
        }
    } else {
        ActivateNpcDialoguePanels(true);
    }
finish_mode_overlays:
    if ((MainGameHud()->redraw_flags & W8_MAIN_REDRAW_TEXT_BOX) != 0) {
        RedrawTextBoxScrollChrome();
    }
    if ((MainGameHud()->redraw_flags & W8_MAIN_REDRAW_SUBMENU_BUTTONS) != 0) {
        RedrawSubMenuButtons();
    }
    if ((MainGameHud()->redraw_flags & W8_MAIN_REDRAW_CHARACTER_ACTION) != 0) {
        DrawSubMenuCharacterAction();
    }
    if (MainGameHud()->open_submenu) {
        RefreshSubMenuPanel((MainGameHud()->redraw_flags & W8_MAIN_REDRAW_SUBMENU_PANEL) != 0);
    }
    if ((MainGameHud()->redraw_flags & W8_MAIN_REDRAW_OPTIONS_BUTTON) != 0) {
        RedrawOptionsDiskButton();
    }
    if ((MainGameHud()->redraw_flags & W8_MAIN_REDRAW_COMBAT_STANCE) != 0) {
        RedrawCombatStanceButtons();
    }
}

/* Draw the main-game frame: sync facing and radar, repaint the requested
   regions and invalidate their rectangles, paint overlays, and present.
   MainGameScreenFrame calls it after its update and on a pending transition. */
void DrawMainGameScreen(void)
{
    int stopped_line;

    SyncPartyFacingFromCamera();
    const auto presentation = BuildTargetingPresentation();
    ApplyTargetingPresentation(presentation);
    UpdateRadarBlips(presentation);
    if (MainGameHud()->action_panel_visible == 0 && !CurrentGame().activity.conversation &&
        !IsLockInteractionActive() && !IsTrapInteractionActive() && !gpSCSV && !ItemUseDraft()) {
        stopped_line = FindStoppedTextLine();
        if (stopped_line != -1 &&
            MainGameHud()->text_lines[4 + CurrentGame().status.text_line_cursor] !=
                static_cast<unsigned int>(stopped_line)) {
            RequestRedraw(W8_MAIN_REDRAW_TEXT_BOX);
        }
    }
    const unsigned int requested = MainGameHud()->redraw_flags;
    if (requested != 0) {
        ApplyMainGameRedrawFlags(presentation);
    }
    // A layout repaint draws the chrome panels, which invalidate themselves.
    unsigned int invalidated = requested;
    if (requested != W8_MAIN_REDRAW_ALL && (requested & W8_MAIN_REDRAW_LAYOUT) != 0) {
        invalidated &= ~W8_MAIN_REDRAW_CHROME;
    }
    if (invalidated != 0) {
        InvalidateRedrawRegions(invalidated);
    }
    MainGameHud()->redraw_flags = 0;
    if (!IsScreenTransitionPending()) {
        if (g_modal_owner != 0) {
            g_modal_owner->RefreshModal();
        }
        if (MainGameHud()->open_submenu) {
            UpdateSubMenuAutoClose();
        }
        if (!CurrentGame().combat.has_value()) {
            UpdatePortraitAdvanceButtons();
        } else {
            RedrawCombatPortraits();
        }
        UpdateConditionButtons();
        if (PartyMovementPanelShown() && !CurrentGame().advancement.held) {
            DrawPartyMovementPanel();
        }
        DrawNpcQuoteBubble();
        if (IsLockInteractionActive()) {
            RedrawLockInteractionPanels();
        }
        if (IsTrapInteractionActive()) {
            RedrawTrapInteractionPanels();
        }
        UpdateMainGameButtons();
        RedrawPortraitQuoteBubbles();
        if (auto* text_fields = TextFieldGroup::Focused())
            text_fields->RenderAll(GetFrameBufferSurface());
        if (MonotonicMilliseconds() - MainGameHud()->tick > 499) {
            if (g_video_inspector_enabled && !gpSCSV && !CurrentGame().activity.conversation &&
                !ItemUseDraft() && !IsLockInteractionActive() && !IsTrapInteractionActive()) {
                DrawVideoInspector(0xdc, 0x32);
            }
            MainGameHud()->tick = MonotonicMilliseconds();
        }
        if (MainGameHud()->formation_board_visible) {
            CreateFormationBoardOverlay();
        }
        if (MainGameHud()->radar_map_visible != 0) {
            EnsureRadarMapOverlay();
        }
        if (g_node_cull_pending) {
            for (const auto& instance : GetWorld()->scene->static_meshes) instance->visible = true;
        }
        RenderFrame();
        if (g_node_cull_pending) {
            for (const auto& instance : GetWorld()->scene->static_meshes)
                instance->visible = MeasureMeshRenderWithoutPositionalOption(instance.get()) != 0;
            g_node_cull_pending = false;
        }
        if (g_dev_mode) {
            CurrentGame().status.dev_flagged = true;
        }
    }
}

/* Activate whichever portrait overlay is pending: the tracked slots are
   checked in priority order, the highlight sprite and the mode-6 hover overlay
   are torn down first, then the matching launcher runs for that slot. The same
   sequence is inlined inside the mode-6 block of the redraw orchestrator. */
void RefreshTrackedPortraitOverlay(void)
{
    if (MainGameHud()->condition_orb_party_slot != -1) {
        DismissHighlightOverlay();
        DrawPortraitConditionOverlay(MainGameHud()->condition_orb_party_slot);
        return;
    }
    if (MainGameHud()->enchantment_orb_party_slot != -1) {
        DismissHighlightOverlay();
        DrawPortraitEnchantmentOverlay(MainGameHud()->enchantment_orb_party_slot);
        return;
    }
    if (MainGameHud()->portrait_overlay_party_slot != -1) {
        DismissHighlightOverlay();
        DrawPortraitVitalsOverlay(MainGameHud()->portrait_overlay_party_slot);
        return;
    }
    if (MainGameHud()->condition_highlight_party_slot != -1) {
        DismissHighlightOverlay();
        DrawPortraitStatusOverlay(MainGameHud()->condition_highlight_party_slot);
    }
}

/* Request the party portion of the main-game redraw. */
void RequestRedrawParty(void)
{
    RequestRedraw(W8_MAIN_REDRAW_MONSTER_LIST);
}

/* Redraw the combat monster-group list at the left edge of the main screen:
   clear the previous rows, walk live groups, format each line, tint the
   hovered action group and current target, and resize input region 0xe5. */
void RedrawCombatMonsterList(bool subject_valid)
{
    unsigned int group_list_index;
    unsigned int live_row_count;
    unsigned int max_text_width;
    std::size_t list_length;
    int row_y;
    W8MonsterGroup* monster_group;
    short text_width;
    bool is_action_group;
    bool is_target_group;
    int context_slot;
    const W8CombatSlot* target;
    Palette16 palette;
    W8MonsterInfo* monster_info;
    std::string scratch_text;

    live_row_count = 0;
    max_text_width = 0;
    row_y = 0x19;
    scratch_text = MainGameHud()->text_paint_scratch;
    if (MainGameHud()->group_list_rows != 0) {
        unsigned int clear_bottom = MainGameHud()->group_list_rows * 0xb + 0x1a;
        int clear_right = MainGameHud()->group_list_width + 0xfb;
        EraseRegion(0xfa, 0x19, clear_right, clear_bottom);
    }
    if (CurrentLevelPopulation().monster_roster.hasActiveMonsters()) {
        group_list_index = 0;
        list_length = CurrentLevelPopulation().monster_groups.size();
        if (list_length != 0) {
            do {
                monster_group = CurrentLevelPopulation().monster_groups[group_list_index].get();
                if (IsMonsterGroupLive(monster_group)) {
                    scratch_text =
                        fmt::format("{:d} {} ({:d}/{:d}/{:d})", monster_group->MemberCount(),
                                    GetMonsterGroupName(monster_group),
                                    CurrentLevelPopulation().activeMemberCount(*monster_group),
                                    monster_group->selectable_member_count,
                                    monster_group->visible_member_count);
                    text_width = g_wiz_text_font_secondary.textWidth(scratch_text);
                    is_action_group = false;
                    if (monster_group->group_id == MainGameHud()->action_group) {
                        is_action_group = true;
                    } else if (MainGameHud()->highlighted_item != -1) {
                        monster_info = FindMonster(MainGameHud()->highlighted_item);
                        is_action_group = monster_group->group_id == monster_info->monster_group_id;
                    }
                    is_target_group = false;
                    context_slot = MainGameHud()->pending_highlight.SlotFor(W8_HIGHLIGHT_TARGET);
                    if (context_slot == -1) {
                        context_slot = CurrentGame().status.selected_character;
                    }
                    const auto group_action =
                        SelectCombatAction(context_slot, GetEditedActionContext(context_slot));
                    target = &group_action.target;
                    if (target->iType == W8_TARGET_KIND_GROUP &&
                        monster_group->group_id == target->iGroupID) {
                        is_target_group = true;
                    } else if (target->iType == W8_TARGET_KIND_MONSTER) {
                        monster_info = FindMonster(target->iMonsterID, false);
                        if (monster_info != 0) {
                            is_target_group =
                                monster_group->group_id == monster_info->monster_group_id;
                        }
                    }
                    if (is_action_group) {
                        palette = g_font_state_palettes[W8_FONT_PALETTE_RED];
                        if (subject_valid) {
                            palette = g_font_state_palettes[W8_FONT_PALETTE_GREEN];
                        }
                    } else {
                        palette = g_font_state_palettes[W8_FONT_PALETTE_BLUE];
                        if (!is_target_group) {
                            palette = g_wiz_text_font_secondary.glyphs->colors;
                        }
                    }
                    g_wiz_text_font_secondary.setPalette(palette);
                    DrawTextAndInvalidate(GetFrameBufferSurface(), g_wiz_text_font_secondary,
                                          {0xfa, row_y}, scratch_text);
                    row_y += 0xb;
                    ++live_row_count;
                    if (max_text_width < static_cast<unsigned int>(text_width)) {
                        max_text_width = static_cast<unsigned int>(text_width);
                    }
                    if (0x18 < live_row_count) {
                        break;
                    }
                }
                ++group_list_index;
                list_length = CurrentLevelPopulation().monster_groups.size();
            } while (group_list_index < list_length);
        }
    }
    if (live_row_count != static_cast<unsigned int>(MainGameHud()->group_list_rows) ||
        max_text_width != static_cast<unsigned int>(MainGameHud()->group_list_width)) {
        g_main_game_invalidate_rects[17].right = max_text_width + 0xfa;
        g_main_game_invalidate_rects[17].bottom = row_y;
        if (live_row_count == 0) {
            MainGame()->monster_list->setEnabled(false);
        } else {
            MainGame()->monster_list->setShape(
                SDL_Rect{250, 25, int(max_text_width), int(live_row_count) * 11});
            MainGame()->monster_list->setEnabled(true);
        }
        MainGameHud()->group_list_rows = live_row_count;
        MainGameHud()->group_list_width = max_text_width;
    }
}

/* Move the selection to another party slot. Re-selecting the current slot
   only re-aims the camera; a real change redraws both slots, hands the new
   slot to each open mode, drops the submenu and keyboard menu state and
   queues the slot's pick quote when not in combat. */
void SelectPartyCharacter(int party_slot)
{
    W8Character* character;
    int previous;

    if (!CurrentGame().party.rows[party_slot].fOccupied) {
        Fail("gStatus.XChar[uiChar].fOccupied");
    }
    character = &CurrentGame().party.characters[party_slot];
    if (character->highest_condition >= W8_CONDITION_MISSING) {
        return;
    }
    if (CurrentGame().status.selected_character == party_slot) {
        FaceCameraToSelection(party_slot);
        return;
    }
    if (!CurrentGame().combat.has_value()) {
        QueueCharacterEvent(character, g_special_event17, 0, kCharacterEventFullVolume);
    }
    if (CurrentGame().status.selected_character != -1) {
        RequestPartySlotRedraw(CurrentGame().status.selected_character);
    }
    previous = CurrentGame().status.selected_character;
    CurrentGame().status.selected_character = party_slot;
    RequestPartySlotRedraw(party_slot);
    if (previous != -1 && (gpSCSV || ItemUseDraft())) {
        ClearSlotTargeting(previous);
    }
    MainGameHud()->clock = MonotonicMilliseconds() + (500);
    MainGameHud()->portrait_flash = 1;
    if (MainGameHud() != nullptr) {
        if (CurrentGame().combat.has_value()) {
            MainGameHud()->refresh_combat_panel = 1;
        }
    }
    FaceCameraToSelection(party_slot);
    if (gpSCSV) {
        SelectSpellCastingCharacter(CurrentGame().status.selected_character);
    }
    if (ItemUseDraft()) {
        RefreshUseItemSelectionForSlot(CurrentGame().status.selected_character);
    }
    if (CurrentGame().activity.conversation) {
        SyncNpcServiceButtons(CurrentGame().status.selected_character);
    }
    if (IsLockInteractionActive()) {
        LockInteraction()->RefreshControls();
    }
    if (IsTrapInteractionActive()) {
        RefreshMainGameActionPanel();
    }
    if (FormationPanelOpen()) {
        SelectFormationSlotCell(CurrentGame().status.selected_character);
    }
    if (MainGameHud()->open_submenu) {
        ResetSubMenuPanel();
    }
    if (MainGameHud()->keyboard_menu_open && party_slot != g_selected_party_slot) {
        CloseKeyboardMenu();
    }
    SetTargetingMode(W8_TARGET_NEED_NONE);
    RequestRedraw(1 << (party_slot & 0x1f) | W8_MAIN_REDRAW_SUBMENU_BUTTONS |
                  W8_MAIN_REDRAW_CHARACTER_ACTION);
    if (auto& combat = CurrentGame().combat) combat->target_picked = true;
}

void RefreshSelectedPartyPortrait(unsigned int party_slot)
{
    if (GetEffectiveMainUiMode() == W8_MAIN_UI_MODE_PORTRAITS ||
        MainGameHud()->portrait_refresh_pending[party_slot] != 0) {
        return;
    }
    if (MainGameHud()->keyboard_menu_open &&
        party_slot == static_cast<unsigned int>(g_selected_party_slot) &&
        !CurrentGame().monster_manager_entries[party_slot].effect_icon_active &&
        !CurrentGame().monster_manager_entries[party_slot].damage_splat_active) {
        if (!CurrentGame().monster_manager_entries[party_slot].quote.bubble ||
            g_settings.pc_subtitles == 0) {
            return;
        }
    }

    if (MainGameHud()->condition_highlight_party_slot == static_cast<int>(party_slot)) {
        MainGameHud()->condition_highlight_party_slot = -1;
        DismissHighlightOverlay();
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_PANEL | W8_MAIN_REDRAW_PORTRAITS);
    } else if (MainGameHud()->condition_highlight_party_slot != -1) {
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_PANEL);
    }

    if (MainGameHud()->portrait_overlay_party_slot == static_cast<int>(party_slot)) {
        MainGameHud()->portrait_overlay_party_slot = -1;
        DismissHighlightOverlay();
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_PANEL | W8_MAIN_REDRAW_PORTRAITS);
    } else if (MainGameHud()->portrait_overlay_party_slot != -1) {
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_PANEL);
    }

    if (g_main_game_mode == W8_MAIN_GAME_NPC_DIALOGUE) {
        CloseNpcDialogueIfActive();
    } else if (g_main_game_mode == W8_MAIN_GAME_HIGHLIGHT_OVERLAY) {
        if (MainGameHud()->highlight_graphic != 0) {
            MainGameHud()->highlight_graphic.reset();
            MainGameHud()->highlight_graphic = 0;
        }
        ClearSurfaceRect(MainGameHud()->dialogue_x, MainGameHud()->dialogue_y,
                         MainGameHud()->dialogue_x + MainGameHud()->dialogue_width,
                         MainGameHud()->dialogue_y + MainGameHud()->dialogue_height);
        InvalidateRegion(MainGameHud()->dialogue_x, MainGameHud()->dialogue_y,
                         MainGameHud()->dialogue_x + MainGameHud()->dialogue_width,
                         MainGameHud()->dialogue_y + MainGameHud()->dialogue_height);
    }

    g_main_game_mode = W8_MAIN_GAME_PORTRAIT_REFRESH;
    if (MainGameHud()->highlight.kind == W8_HIGHLIGHT_TARGET) MainGameHud()->highlight = {};
    MainGameHud()->portrait_refresh_pending[party_slot] = 1;
    MainGameHud()->portrait_refresh_image[party_slot] = 0x69;
    RequestRedraw(1u << (party_slot & 0x1f));
    MainGame()->edge_targets[party_slot]->setEnabled(false);
    if (!CurrentGame().monster_manager_entries[party_slot].keyboard_menu_open) {
        EnableMainGamePortraitSlot(party_slot);
    }
}

/* Take the mode-6 hover overlay down: the portrait highlight graphic is
   released, the dialogue-area rectangle it drew over is cleared and
   invalidated, the rectangle's position against the viewport decides the
   extra redraw flags, and the mode resets to the default. */
void DismissHighlightOverlay(void)
{
    if (MainGameHud()->highlight_graphic != 0) {
        MainGameHud()->highlight_graphic.reset();
        MainGameHud()->highlight_graphic = 0;
    }
    ClearHighlightOverlayRegion();
    g_main_game_mode = W8_MAIN_GAME_DEFAULT;
}

static void DrawHighlightFrameRow(int left, int right, int y, unsigned int tiles)
{
    CatalogArtwork::Load(0x70, 0).draw(GetFrameBufferSurface(), 3, left, y);
    int inner = left + 6;
    for (unsigned int tile = tiles; tile != 0; --tile) {
        CatalogArtwork::Load(0x70, 0).draw(GetFrameBufferSurface(), 8, inner, y);
        inner += 6;
    }
    CatalogArtwork::Load(0x70, 0).draw(GetFrameBufferSurface(), 4, right, y);
}

/* Build the mode-6 hover panel over a portrait: the framed plate carrying the
   character's name and `row_count` content rows, plus the lazily created 0x72
   highlight sprite pinned to highlight_row. An active mode-3 NPC dialogue,
   mode-5 panel or previous overlay is torn down first; the panel is anchored
   off the slot's portrait column and clamped inside the viewport. The retail
   assertion names the sprite gpMGSV->pHighlightGraphic. */
void DrawHighlightOverlay(unsigned int party_slot, int row_count, unsigned int min_width)
{
    SetMainGameMode(W8_MAIN_GAME_HIGHLIGHT_OVERLAY);

    std::string name = CurrentGame().party.characters[party_slot].name;
    int width = g_wiz_text_font.textWidth(name);
    if (min_width < static_cast<unsigned int>(width)) {
        width = g_wiz_text_font.textWidth(name);
        min_width = width;
    }
    min_width += 6;
    unsigned int tiles = min_width / 6;
    if (min_width % 6 != 0) {
        ++tiles;
        min_width = tiles * 6;
    }
    unsigned int panel_width = min_width + 0xc;

    int left;
    unsigned char pending = MainGameHud()->portrait_refresh_pending[party_slot];
    if ((party_slot & 1) == 0) {
        left = MainGameHud()->portrait_refresh_image[party_slot] -
               MainGameHud()->portrait_hover_x_origin + 0x80;
        if (pending == 0 && (MainGameHud()->condition_highlight_party_slot != -1 ||
                             MainGameHud()->portrait_overlay_party_slot != -1)) {
            left += 0x18;
        }
    } else {
        left = MainGameHud()->portrait_hover_x_origin -
               MainGameHud()->portrait_refresh_image[party_slot] - static_cast<int>(panel_width) +
               0x200;
        if (pending == 0 && (MainGameHud()->condition_highlight_party_slot != -1 ||
                             MainGameHud()->portrait_overlay_party_slot != -1)) {
            left -= 0x18;
        }
    }

    unsigned int content_height = row_count * 9 + 0x12;
    int row_y = (party_slot >> 1) * 0x55;
    int top = g_viewport_modes[static_cast<unsigned int>(MainGameHud()->camera_mode)].top;
    int candidate = row_y - static_cast<int>(content_height) + 0x3c;
    if (candidate >= top) {
        if (candidate + content_height * 2 > 0x166) {
            top = 0x166 - content_height * 2;
        } else {
            top = candidate;
            if (pending == 0 && MainGameHud()->condition_highlight_party_slot != -1) {
                top = row_y + 0x12;
            }
        }
    }
    MainGameHud()->dialogue_x = left;
    MainGameHud()->dialogue_y = top;
    MainGameHud()->dialogue_height = content_height * 2;
    MainGameHud()->dialogue_width = panel_width;
    MainGameHud()->dialogue_text_x = left + 9;
    MainGameHud()->dialogue_text_width = panel_width - 0x12;

    unsigned int tile;
    int row;
    int inner;
    CatalogArtwork::Load(0x70, 0).draw(GetFrameBufferSurface(), 0, left, top);
    inner = left + 6;
    for (tile = tiles; tile != 0; --tile) {
        CatalogArtwork::Load(0x70, 0).draw(GetFrameBufferSurface(), 1, inner, top);
        inner += 6;
    }
    int right = left + (tiles * 3 + 3) * 2;
    CatalogArtwork::Load(0x70, 0).draw(GetFrameBufferSurface(), 2, right, top);
    int row_y_pos = top + 6;
    for (row = 0; row < 2; ++row) {
        DrawHighlightFrameRow(left, right, row_y_pos, tiles);
        row_y_pos += 6;
    }

    g_wiz_text_font.setPalette(
        g_font_state_palettes[CurrentGame().party.rows[party_slot].party_order_index]);
    int font_height = g_wiz_text_font.height();
    int name_width = g_wiz_text_font.textWidth(name);
    DrawText(GetFrameBufferSurface(), g_wiz_text_font,
             {left + static_cast<int>(panel_width >> 1) - name_width / 2,
              (0xc - font_height) / 2 + top + 6},
             name);
    g_wiz_text_font.resetPalette();

    row_y_pos = top + 0x12;
    CatalogArtwork::Load(0x70, 0).draw(GetFrameBufferSurface(), 3, left, row_y_pos);
    CatalogArtwork::Load(0x71, 0).draw(GetFrameBufferSurface(), 0, left + 6, row_y_pos);
    if (tiles > 2) {
        inner = left + 0xc;
        for (tile = 0; tile < tiles - 2; ++tile) {
            CatalogArtwork::Load(0x71, 0).draw(GetFrameBufferSurface(), 1, inner, row_y_pos);
            inner += 6;
        }
    }
    CatalogArtwork::Load(0x71, 0).draw(GetFrameBufferSurface(), 2, left + tiles * 6, row_y_pos);
    CatalogArtwork::Load(0x70, 0).draw(GetFrameBufferSurface(), 4, right, row_y_pos);

    row_y_pos += 6;
    MainGameHud()->dialogue_row_y = row_y_pos + 6;
    for (row = row_count * 3 + 1; row != 0; --row) {
        DrawHighlightFrameRow(left, right, row_y_pos, tiles);
        row_y_pos += 6;
    }
    CatalogArtwork::Load(0x70, 0).draw(GetFrameBufferSurface(), 5, left, row_y_pos);
    inner = left + 6;
    for (tile = tiles; tile != 0; --tile) {
        CatalogArtwork::Load(0x70, 0).draw(GetFrameBufferSurface(), 6, inner, row_y_pos);
        inner += 6;
    }
    CatalogArtwork::Load(0x70, 0).draw(GetFrameBufferSurface(), 7, right, row_y_pos);

    if (MainGameHud()->highlight_graphic == 0) {
        const auto artwork = CatalogArtwork::Load(0x72);
        if (auto surface = RenderSpriteToImage(*artwork.spriteSheet(), 0)) {
            MainGameHud()->highlight_graphic = makeOverlayFromSurface(*surface, 0, 0, 1);
        }
    }
    if (MainGameHud()->highlight_graphic == 0) {
        Fail("gpMGSV->pHighlightGraphic");
    }
    positionOverlay(MainGameHud()->highlight_graphic.get(), MainGameHud()->dialogue_x + 7,
                    MainGameHud()->dialogue_row_y - 1 + MainGameHud()->highlight_row * 0x12, false);
    InvalidateRegion(MainGameHud()->dialogue_x, MainGameHud()->dialogue_y,
                     MainGameHud()->dialogue_x + MainGameHud()->dialogue_width,
                     MainGameHud()->dialogue_y + MainGameHud()->dialogue_height);
}

static void BeginPortraitOverlay(unsigned int party_slot, int row_count, unsigned int width)
{
    MainGameHud()->highlight_row = 0;
    DrawHighlightOverlay(party_slot, row_count, width);
    if (MainGameHud()->highlight_graphic != 0) {
        MainGameHud()->highlight_graphic->visible = false;
    }
}

static void DrawPortraitConditionRow(int condition, int text_x, int row_y, unsigned int width)
{
    CatalogArtwork::Load(condition + 0xb6, 0).draw(GetFrameBufferSurface(), 0, text_x, row_y);
    MainGameHud()->text_paint_scratch =
        fmt::format("{}", Data().strings[g_condition_notices[condition].name]);
    int text_width = g_wiz_text_font.textWidth(MainGameHud()->text_paint_scratch);
    DrawText(GetFrameBufferSurface(), g_wiz_text_font,
             {static_cast<int>(((width >> 1) - text_width / 2) + 0x1a + text_x), row_y},
             MainGameHud()->text_paint_scratch);
}

static void DrawPortraitEnchantmentRow(const W8Character* character, int slot, int text_x,
                                       int row_y, unsigned int width)
{
    CatalogArtwork::Load(slot + 0xc9, 0).draw(GetFrameBufferSurface(), 0, text_x, row_y);
    MainGameHud()->text_paint_scratch =
        fmt::format("{} ({:d})", Data().strings[g_enchantment_notices[slot]],
                    character->enchantments[slot].power);
    int text_width = g_wiz_text_font.textWidth(MainGameHud()->text_paint_scratch);
    DrawText(GetFrameBufferSurface(), g_wiz_text_font,
             {static_cast<int>(((width >> 1) - text_width / 2) + 0x1a + text_x), row_y},
             MainGameHud()->text_paint_scratch);
}

/* The portrait overlay's vitals content: "HP:"/"Stamina:" label rows then one
   icon row per spell realm, each "cur/max" value centered in the column right
   of the labels. Retail seeds the value-column width from the HP/stamina
   strings; the realm pass widens it only when a realm string beats the label
   width rather than the running value width. */
void DrawPortraitVitalsOverlay(int party_slot)
{
    int realm_icons[6] = {0x193, 0x194, 0x195, 0x196, 0x197, 0x198};
    W8Character* character = &CurrentGame().party.characters[party_slot];

    MainGameHud()->text_paint_scratch = fmt::format("{}: ", Data().strings[0x282]);
    unsigned int label_width = g_wiz_text_font.textWidth(MainGameHud()->text_paint_scratch);
    MainGameHud()->text_paint_scratch = fmt::format("{}: ", Data().strings[0x283]);
    int width = g_wiz_text_font.textWidth(MainGameHud()->text_paint_scratch);
    if (label_width < static_cast<unsigned int>(width)) {
        width = g_wiz_text_font.textWidth(MainGameHud()->text_paint_scratch);
        label_width = width;
    }
    if (label_width < 0x12) {
        label_width = 0x12;
    }
    label_width += 10;

    MainGameHud()->text_paint_scratch =
        fmt::format("{:d}/{:d}", character->hp_current, character->uiHPMax);
    unsigned int value_width = g_wiz_text_font.textWidth(MainGameHud()->text_paint_scratch);
    MainGameHud()->text_paint_scratch =
        fmt::format("{:d}/{:d}", character->stamina, character->uiStaminaMax);
    width = g_wiz_text_font.textWidth(MainGameHud()->text_paint_scratch);
    if (value_width < static_cast<unsigned int>(width)) {
        width = g_wiz_text_font.textWidth(MainGameHud()->text_paint_scratch);
        value_width = width;
    }
    int realm;
    for (realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
        MainGameHud()->text_paint_scratch = fmt::format(
            "{:d}/{:d}", GetCharacterRealmSpellPoints(character, static_cast<W8SpellRealm>(realm)),
            character->sp_max[realm]);
        width = g_wiz_text_font.textWidth(MainGameHud()->text_paint_scratch);
        if (label_width < static_cast<unsigned int>(width)) {
            width = g_wiz_text_font.textWidth(MainGameHud()->text_paint_scratch);
            value_width = width;
        }
    }

    BeginPortraitOverlay(party_slot, 8, value_width + label_width);

    int text_width = MainGameHud()->dialogue_text_width;
    int text_x = MainGameHud()->dialogue_text_x;
    int row_y = MainGameHud()->dialogue_row_y;

    MainGameHud()->text_paint_scratch = fmt::format("{}: ", Data().strings[0x282]);
    DrawText(GetFrameBufferSurface(), g_wiz_text_font, {text_x, row_y},
             MainGameHud()->text_paint_scratch);
    MainGameHud()->text_paint_scratch =
        fmt::format("{:d}/{:d}", character->hp_current, character->uiHPMax);
    unsigned int value_center = (text_width - label_width) >> 1;
    width = g_wiz_text_font.textWidth(MainGameHud()->text_paint_scratch);
    DrawText(GetFrameBufferSurface(), g_wiz_text_font,
             {static_cast<int>(value_center - width / 2 + text_x + label_width), row_y},
             MainGameHud()->text_paint_scratch);
    row_y += 0x12;

    MainGameHud()->text_paint_scratch = fmt::format("{}: ", Data().strings[0x283]);
    DrawText(GetFrameBufferSurface(), g_wiz_text_font, {text_x, row_y},
             MainGameHud()->text_paint_scratch);
    MainGameHud()->text_paint_scratch =
        fmt::format("{:d}/{:d}", character->stamina, character->uiStaminaMax);
    width = g_wiz_text_font.textWidth(MainGameHud()->text_paint_scratch);
    DrawText(GetFrameBufferSurface(), g_wiz_text_font,
             {static_cast<int>(value_center - width / 2 + text_x + label_width), row_y},
             MainGameHud()->text_paint_scratch);
    row_y += 0x12;

    for (realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
        CatalogArtwork::Load(realm_icons[realm], 0)
            .draw(GetFrameBufferSurface(),
                  static_cast<short>(g_spell_realm_animations[realm].initial_frame), text_x, row_y);
        MainGameHud()->text_paint_scratch = fmt::format(
            "{:d}/{:d}", GetCharacterRealmSpellPoints(character, static_cast<W8SpellRealm>(realm)),
            character->sp_max[realm]);
        int text_y = (0x12 - g_wiz_text_font.height()) / 2 + row_y;
        width = g_wiz_text_font.textWidth(MainGameHud()->text_paint_scratch);
        DrawText(GetFrameBufferSurface(), g_wiz_text_font,
                 {static_cast<int>(value_center - width / 2 + text_x + label_width), text_y},
                 MainGameHud()->text_paint_scratch);
        row_y += 0x12;
    }
}

static int MeasurePortraitConditions(const W8Character* character, unsigned int& max_width)
{
    int condition;
    int row_count = 0;
    for (condition = 0; condition < W8_CONDITION_COUNT; ++condition) {
        if (character->uiCondition[condition] != 0) {
            ++row_count;
        }
    }
    for (condition = W8_CONDITION_COUNT - 1; condition > 0; --condition) {
        if (character->uiCondition[condition] != 0) {
            MainGameHud()->text_paint_scratch =
                fmt::format("{}", Data().strings[g_condition_notices[condition].name]);
            int width = g_wiz_text_font.textWidth(MainGameHud()->text_paint_scratch);
            if (max_width < static_cast<unsigned int>(width)) {
                max_width = width;
            }
        }
    }

    return row_count;
}

static int MeasurePortraitEnchantments(const W8Character* character, unsigned int& max_width)
{
    int slot;
    int row_count = 0;
    for (slot = 0; slot < 8; ++slot) {
        if (character->enchantments[slot].turns != 0) {
            ++row_count;
        }
    }
    for (slot = 7; slot > 0; --slot) {
        if (character->enchantments[slot].turns != 0) {
            MainGameHud()->text_paint_scratch =
                fmt::format("{} ({:d})", Data().strings[g_enchantment_notices[slot]],
                            character->enchantments[slot].power);
            int width = g_wiz_text_font.textWidth(MainGameHud()->text_paint_scratch);
            if (max_width < static_cast<unsigned int>(width)) {
                max_width = width;
            }
        }
    }

    return row_count;
}

/* The portrait overlay's condition content: one row per active condition with
   its status icon and notice name, drawn bottom-up. The count covers all
   twenty condition slots while the measure and draw passes visit 0x13 down to
   1 only, paired with the g_condition_notices
   row names. */
void DrawPortraitConditionOverlay(int party_slot)
{
    W8Character* character = &CurrentGame().party.characters[party_slot];

    unsigned int max_width = 0;
    int row_count = MeasurePortraitConditions(character, max_width);

    BeginPortraitOverlay(party_slot, row_count, max_width + 0x1a);

    int text_x = MainGameHud()->dialogue_text_x;
    int row_y = MainGameHud()->dialogue_row_y;
    int text_width = MainGameHud()->dialogue_text_width;
    for (int condition = W8_CONDITION_COUNT - 1; condition > 0; --condition) {
        if (character->uiCondition[condition] != 0) {
            DrawPortraitConditionRow(condition, text_x, row_y, text_width - 0x1aU);
            row_y += 0x12;
        }
    }
}

/* The condition-highlight panel's combined content: the active conditions
   followed by the active enchantments, capped at 0x14 rows. When the lists
   overflow, the last row instead prints the "more" notice (0x7d8) - the extra
   enchantment power argument it pushes is dead, "%s" never consumes it. */
void DrawPortraitStatusOverlay(int party_slot)
{
    W8Character* character = &CurrentGame().party.characters[party_slot];
    unsigned int max_width = 0;
    bool truncated = false;
    int condition;
    int slot;

    int condition_count = MeasurePortraitConditions(character, max_width);
    int enchantment_count = MeasurePortraitEnchantments(character, max_width);

    int row_count = condition_count + enchantment_count;
    if (0x14 < row_count) {
        row_count = 0x14;
        truncated = true;
        int width = g_wiz_text_font.textWidth(Data().strings[0x7d8]);
        if (max_width < static_cast<unsigned int>(width - 0x1a)) {
            width = g_wiz_text_font.textWidth(MainGameHud()->text_paint_scratch);
            max_width = width - 0x1a;
        }
    }

    BeginPortraitOverlay(party_slot, row_count, max_width + 0x1a);

    int text_x = MainGameHud()->dialogue_text_x;
    int row_y = MainGameHud()->dialogue_row_y;
    unsigned int text_width = MainGameHud()->dialogue_text_width - 0x1a;
    int rows_drawn = 0;
    for (condition = W8_CONDITION_COUNT - 1; condition > 0; --condition) {
        if (character->uiCondition[condition] != 0) {
            DrawPortraitConditionRow(condition, text_x, row_y, text_width);
            row_y += 0x12;
            ++rows_drawn;
        }
    }
    for (slot = 7; slot > 0; --slot) {
        if (rows_drawn == 0x13 && truncated) {
            MainGameHud()->text_paint_scratch = Data().strings[0x7d8];
            int width = g_wiz_text_font.textWidth(MainGameHud()->text_paint_scratch);
            DrawText(GetFrameBufferSurface(), g_wiz_text_font,
                     {static_cast<int>((((text_width - width) + 0x1a) >> 1) + text_x), row_y},
                     MainGameHud()->text_paint_scratch);
            return;
        }
        if (character->enchantments[slot].turns != 0) {
            DrawPortraitEnchantmentRow(character, slot, text_x, row_y, text_width);
            row_y += 0x12;
            ++rows_drawn;
        }
    }
}

/* The enchantment orb's hover content: one row per active enchantment showing
   its icon and "name (power)" text. The count covers all eight slots while the
   measure and draw passes visit 7 down to 1 - slot 0's row is counted but
   never drawn. */
void DrawPortraitEnchantmentOverlay(int party_slot)
{
    W8Character* character = &CurrentGame().party.characters[party_slot];

    unsigned int max_width = 0;
    int row_count = MeasurePortraitEnchantments(character, max_width);

    BeginPortraitOverlay(party_slot, row_count, max_width + 0x1a);

    int text_x = MainGameHud()->dialogue_text_x;
    int row_y = MainGameHud()->dialogue_row_y;
    int text_width = MainGameHud()->dialogue_text_width;
    for (int slot = 7; slot > 0; --slot) {
        if (character->enchantments[slot].turns != 0) {
            DrawPortraitEnchantmentRow(character, slot, text_x, row_y, text_width - 0x1aU);
            row_y += 0x12;
        }
    }
}

void RequestRedrawCombatBar(void)
{
    RequestRedraw(W8_MAIN_REDRAW_ROOF_AND_SPELL_ICONS);
}

/* Raise a modal notice line over the main game: enter mode 5 (tearing down
   whichever of the NPC-dialogue, message-box, or dialogue-highlight modes was
   current), then create the kind-1 message dialog, size it, and install the
   caller's confirmation callback as the modal owner. */
void ShowMainGameNoticeLine(std::string_view text, std::function<void(bool)> callback,
                            bool confirmation, bool cancel)
{
    W8MessageDialogBase* dialog;

    SetMainGameMode(W8_MAIN_GAME_MODAL);
    dialog = CreateMessageDialog();
    dialog->SetClientExtent(0xfa, 200);
    dialog->SetMessage(text, 1, 0x32, confirmation, cancel, true, true, 0, 0x15e);
    dialog->on_complete = std::move(callback);
    OpenModal(std::unique_ptr<ModalContent>(dialog));
}

/* Note that the party's state changed. The combat half is only asked for while
   a fight is on; the party half always. */
void RequestRefreshPartyState(void)
{
    if (MainGameHud() == nullptr) {
        return;
    }
    if (CurrentGame().combat.has_value()) {
        MainGameHud()->refresh_combat_panel = 1;
    }
}

/* The party slot's chosen action turned out unreachable: re-arm the matching
   targeting mode or view so the player can pick a fresh target instead of
   silently dropping the action. */
void FallbackFromUnreachableAction(int party_slot)
{
    W8PartySlotRow* row = &CurrentGame().party.rows[party_slot];
    if (!row->fOccupied || CurrentGame().combat->characters[party_slot].dead) {
        return;
    }
    switch (row->committed.kind) {
    case W8_ACTION_ATTACK:
    case W8_ACTION_BERSERK:
        if (CanAnyHandReachTarget(party_slot) &&
            (CurrentGame().combat->round_active || CurrentGame().party_movement_mode)) {
            SelectPartyCharacter(party_slot);
            if (party_slot == CurrentGame().status.selected_character) {
                SetTargetingMode(W8_TARGET_NEED_ENEMY);
                return;
            }
        }
        break;
    case W8_ACTION_CAST_SPELL:
    case W8_ACTION_USE_ITEM: {
        const bool casting = row->committed.kind == W8_ACTION_CAST_SPELL;
        if (!(casting ? CharacterHasCastableSpell(&CurrentGame().party.characters[party_slot]) &&
                            ActionNeedsExplicitTarget(row->committed,
                                                      CurrentGame().activity.conversation_aside)
                      : ItemUseNeedsTarget(party_slot))) {
            break;
        }
        if (!CurrentGame().combat->round_active) {
            if (CurrentGame().party_movement_mode &&
                (SelectPartyCharacter(party_slot),
                 party_slot == CurrentGame().status.selected_character)) {
                const int spell_id = casting ? row->committed.detail
                                             : GetItemSpell(CurrentGame().party.itemAt(
                                                   row->committed.detail_block.item_use.item));
                const W8TargetNeed needed_kind =
                    GetTargetNeededForSpellFriendly(spell_id, false, MainGameHud() == nullptr);
                ConfigureSpellTargetFilter(GetSpellTargetType(spell_id, false), needed_kind);
                return;
            }
        } else {
            SelectPartyCharacter(party_slot);
            if (party_slot == CurrentGame().status.selected_character) {
                if (casting) {
                    OpenSpellCastingView(party_slot);
                    SelectSpellPowerLevel(row->committed.detail_block.spell.power_level - 1);
                } else {
                    OpenUseItemSelectView(party_slot);
                    SelectCurrentUseItemLine();
                }
                return;
            }
        }
        break;
    }
    case W8_ACTION_PROTECT:
        if (CanCharacterAttack(party_slot) &&
            (CurrentGame().combat->round_active || CurrentGame().party_movement_mode)) {
            SelectPartyCharacter(party_slot);
            if (party_slot == CurrentGame().status.selected_character) {
                SetTargetingMode(W8_TARGET_NEED_ALLY);
                return;
            }
        }
        break;
    case W8_ACTION_BREATHE:
        if (CanCharReBreathe(party_slot) &&
            (CurrentGame().combat->round_active || CurrentGame().party_movement_mode)) {
            SelectPartyCharacter(party_slot);
            if (party_slot == CurrentGame().status.selected_character) {
                SetTargetingMode(W8_TARGET_NEED_CONE);
            }
        }
        break;
    default:
        break;
    }
}

/* Whether a modal owner has the screen. */
bool IsModalOpen(void)
{
    return g_modal_owner != 0;
}

/* The 3D view's screen rectangle for each of the seven viewport modes, with
   exclusive right and bottom edges. Mode 7, which leaving a level stores, sits
   outside the table so the next change always applies. */
DesignRect g_viewport_modes[7] = {
    {23, 18, 617, 450},  {23, 18, 617, 358},  {128, 18, 512, 358}, {128, 18, 512, 279},
    {128, 18, 512, 450}, {128, 18, 512, 416}, {23, 18, 617, 416},
};

W8MainGameInvalidateRect g_main_game_invalidate_rects[23] = {
    {0, 18, 127, 103, -1, 0, 0},     {512, 18, 639, 103, 1, 0, 1},
    {0, 103, 127, 188, -1, 0, 2},    {512, 103, 639, 188, 1, 0, 3},
    {0, 188, 127, 273, -1, 0, 4},    {512, 188, 639, 273, 1, 0, 5},
    {0, 273, 127, 358, -1, 0, 6},    {512, 273, 639, 358, 1, 0, 7},
    {0, 0, 639, 16, 0, 0, ~0u},      {0, 358, 639, 449, 0, 1, ~0u},
    {23, 360, 127, 449, 0, 1, ~0u},  {129, 358, 510, 449, 0, 1, ~0u},
    {31, 450, 606, 476, 0, 1, ~0u},  {469, 382, 500, 461, 0, 1, ~0u},
    {512, 358, 616, 449, 0, 1, ~0u}, {0, 0, 0, 0, 0, 0, ~0u},
    {127, 260, 513, 356, 0, 0, ~0u}, {250, 25, 0, 0, 0, 0, ~0u},
    {1, 450, 29, 476, 0, 1, ~0u},    {293, 450, 524, 476, 0, 1, ~0u},
    {612, 452, 636, 473, 0, 1, ~0u}, {0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0},
};

/* Invalidate the rectangle of each requested redraw bit, placed by the
   current portrait layout. */
void InvalidateRedrawRegions(unsigned int redraw_flags)
{
    unsigned char bit;
    W8MainGameInvalidateRect* entry;
    int left;
    int top;
    int right;
    int bottom;
    int delta;

    if (redraw_flags == W8_MAIN_REDRAW_ALL) {
        ClearVideoDirtyBlocks();
        return;
    }

    for (bit = 0, entry = g_main_game_invalidate_rects; bit < 23; ++bit, ++entry) {
        if ((redraw_flags & (1u << (bit & 0x1f))) == 0) {
            continue;
        }
        left = entry->left;
        top = entry->top;
        right = entry->right;
        bottom = entry->bottom;
        if (entry->x_scale != 0) {
            delta = MainGameHud()->portrait_hover_x_origin * entry->x_scale;
            left += delta;
            right += delta;
        }
        if (entry->apply_y_offset != 0) {
            top += MainGameHud()->portrait_y_shift;
            bottom += MainGameHud()->portrait_y_shift;
        }
        if (entry->refresh_image_index != ~0u) {
            delta = MainGameHud()->portrait_refresh_image[entry->refresh_image_index];
            if ((entry->refresh_image_index & 1) == 0) {
                left += delta;
                right += delta;
            } else {
                left -= delta;
                right -= delta;
            }
        }
        InvalidateRegion(left, top, right + 1, bottom + 1);
    }
}

/* Queue a level change. With `flag` set the request is confirmed with a modal
   "Enter <level>?" dialog - NPC departure events decide whether the camera is
   simply snapped to the trigger location or the dialog owns the modal region
   until OnEnterLevelDialogClosed runs. With it clear the transition is staged
   straight into the pending screen state and modes 3/5/6 are torn down. */
void RequestLevelTransition(int level, int entry, unsigned char flag)
{
    unsigned int normalized = NormalizeMasterFunctionValue(level);

    const int level_id = static_cast<int>(normalized);
    if (flag != 0) {
        std::string question;
        if (normalized < 0x2f) {
            question = fmt::format("{} {}?", Data().strings[0x799],
                                   Data().strings[g_level_name_indices[normalized]]);
        } else if (normalized == 0x38) {
            question = g_text_enter_default_level;
        } else {
            question = fmt::format("Enter test level {:c} ?", normalized + 2);
        }
        if (QueueNpcDepartureEvents(normalized)) {
            WorldSetCameraLocation(&g_trigger_camera);
            return;
        }
        SetMainGameMode(W8_MAIN_GAME_MODAL);
        W8MessageDialogBase* dialog = CreateMessageDialog();
        dialog->SetClientExtent(0xfa, 0xc8);
        dialog->SetMessage(question, 1, 0x32, true, true, true, true, 0, 0x15e);
        dialog->on_complete = [level_id, entry](bool accepted) {
            if (accepted) {
                BeginLevelTransition(level_id, entry);
            } else {
                WorldSetCameraLocation(&g_trigger_camera);
            }
        };
        OpenModal(std::unique_ptr<ModalContent>(dialog));
        return;
    }
    BeginLevelTransition(level_id, entry);
}

/* Switch the 3D view to another viewport mode: resize the view region to the
   inclusive rectangle and hand the renderer the exclusive one. */
void SetViewportMode(W8ViewportMode mode)
{
    DesignRect* rect;

    if (mode == MainGameHud()->camera_mode) {
        return;
    }
    rect = &g_viewport_modes[static_cast<unsigned int>(mode)];
    MainGame()->world_view->setShape(
        SDL_Rect{rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top});
    W8MainGameInvalidateRect& viewport = g_main_game_invalidate_rects[15];
    viewport.left = rect->left;
    viewport.top = rect->top;
    viewport.right = rect->right;
    viewport.bottom = rect->bottom;
    SetViewport({viewport.left, viewport.top, viewport.right + 1, viewport.bottom + 1});
    MainGameHud()->camera_mode = mode;
}

/* True when the cursor hotspot lies in the formation-mode portrait rect for
   `party_slot`, accounting for the hover panel's x origin and the slot's
   mirrored even/odd layout. */
static bool IsPartyPortraitUnderCursor(unsigned int party_slot)
{
    W8MainGameInvalidateRect& row = g_main_game_invalidate_rects[party_slot];
    int image = MainGameHud()->portrait_refresh_image[party_slot];
    int left;
    int right;

    if ((party_slot & 1) == 0) {
        left = (row.left - MainGameHud()->portrait_hover_x_origin) + image;
        right = (row.right - MainGameHud()->portrait_hover_x_origin) + image;
    } else {
        left = (row.left - image) + MainGameHud()->portrait_hover_x_origin;
        right = (row.right - image) + MainGameHud()->portrait_hover_x_origin;
    }
    if (right < 0) {
        return false;
    }
    if (left < 0) {
        left = 0;
    }
    return IsCursorInRectangle(left, row.top, right, row.bottom);
}

/* The per-frame combat-strip update: while combat mode is on, recompute each
   party slot's status (-1 empty/dead, 0 ready, 1 cannot switch, 2 ineligible,
   3 acting), refresh the portrait catalog images, and pulse the acting
   combatant's portrait while the action pacing clock holds. */
void UpdateCombatPortraitStatus(void)
{
    for (int slot = 0; slot < 8; ++slot) {
        W8PartySlotRow* row = &CurrentGame().party.rows[slot];
        W8Character* character = &CurrentGame().party.characters[slot];
        W8CombatCharacterRow* combat_row = &CurrentGame().combat->characters[slot];
        W8MonsterManagerEntry* entry = &CurrentGame().monster_manager_entries[slot];
        int image = -1;
        int alternate = -1;
        W8CombatPortraitStatus status = W8_COMBAT_PORTRAIT_EMPTY;

        if (row->fOccupied && character->hp_current > 0 &&
            character->highest_condition <= W8_CONDITION_UNCONSCIOUS) {
            if (CurrentGame().combat->actingSlot() != slot ||
                TimeUntilDeadline(CurrentGame().combat->action_clock) > 800) {
                if (IsPartySlotEligible(slot)) {
                    if (combat_row->dead) {
                        status = W8_COMBAT_PORTRAIT_INELIGIBLE;
                    } else {
                        status =
                            CharacterCanSwitchTo(slot, W8_TARGETING_CONTEXT_IN_COMBAT, false, false)
                                ? W8_COMBAT_PORTRAIT_READY
                                : W8_COMBAT_PORTRAIT_CANNOT_SWITCH;
                    }
                } else {
                    status = W8_COMBAT_PORTRAIT_INELIGIBLE;
                }
            } else {
                status = W8_COMBAT_PORTRAIT_ACTING;
            }
            image =
                GetCombatPortraitImage(row->committed.kind, row->committed.detail, status, slot);
            alternate = image;
            if (status == W8_COMBAT_PORTRAIT_ACTING) {
                if (combat_row->combat_status == W8_COMBAT_PORTRAIT_ACTING) {
                    if (TimeUntilDeadline(entry->acting_portrait_pulse_clock) == 0) {
                        ++entry->acting_portrait_pulse;
                        if (entry->acting_portrait_pulse == 0xc) {
                            entry->acting_portrait_pulse = 1;
                            entry->acting_portrait_pulse_clock = MonotonicMilliseconds() + (100);
                        } else if (entry->acting_portrait_pulse < 7) {
                            image = combat_row->portrait_image + 1;
                            entry->acting_portrait_pulse_clock = MonotonicMilliseconds() + (100);
                        } else {
                            image = combat_row->portrait_image - 1;
                            entry->acting_portrait_pulse_clock = MonotonicMilliseconds() + (100);
                        }
                    } else {
                        image = combat_row->portrait_image;
                    }
                } else {
                    entry->acting_portrait_pulse_clock = MonotonicMilliseconds() + (100);
                    entry->acting_portrait_pulse = 1;
                }
            } else {
                alternate = image + 10;
            }
            if (image != -1 && image != combat_row->portrait_image) {
                entry->combat_portrait_dirty = true;
            }
        }
        combat_row->combat_status = status;
        combat_row->portrait_image = image;
        combat_row->portrait_image_alternate = alternate;
    }
}

/* Per-frame keyboard menu service: close the menu when it outlives combat,
   its party slot becomes ineligible, or the selected slot empties. While the
   cursor sits outside the menu a 600 ms countdown runs; drifting back in
   re-arms the slot from the cursor row, and expiry closes the menu. */
void UpdateKeyboardMenu(void)
{
    RequestRedraw(W8_MAIN_REDRAW_FRAME);
    if ((!CurrentGame().combat.has_value() && MainGameHud()->keyboard_menu_open) ||
        (MainGameHud()->combat_slot != -1 && !IsPartySlotEligible(MainGameHud()->combat_slot))) {
        CloseKeyboardMenu();
        return;
    }
    if (CurrentGame().combat.has_value() && !CurrentGame().combat->round_active) {
        CloseKeyboardMenu();
        return;
    }
    if (MainGameHud()->keyboard_menu_open) {
        if (MainGameHud()->cursor_grace == 0) {
            if (CurrentGame().party.rows[MainGameHud()->combat_slot].fOccupied == 0) {
                CloseKeyboardMenu();
                return;
            }
            if (!KeyboardMenuContainsCursor()) {
                MainGameHud()->cursor_grace = 1;
                MainGameHud()->countdown3 = MonotonicMilliseconds() + (0x258);
                return;
            }
        } else {
            if (KeyboardMenuContainsCursor()) {
                MainGameHud()->countdown3 = MonotonicMilliseconds() + (0);
                MainGameHud()->combat_slot = g_selected_party_slot;
                MainGameHud()->cursor_grace = 0;
            } else if (TimeUntilDeadline(MainGameHud()->countdown3) == 0) {
                CloseKeyboardMenu();
            }
        }
    }
}

/* While the main UI is not in portrait mode, keep each party slot's formation
   portrait refresh latch in sync with dirty flags, occupancy, and hover. */
void UpdateFormationPortraitRefresh(void)
{
    unsigned int party_slot;
    W8MonsterManagerEntry* entry;

    for (party_slot = 0; party_slot < 8; ++party_slot) {
        entry = &CurrentGame().monster_manager_entries[party_slot];
        if (MainGameHud()->portrait_refresh_pending[party_slot] == 0) {
            if (entry->damage_splat_active || entry->effect_icon_active ||
                (entry->portrait_event_active)) {
                RefreshSelectedPartyPortrait(party_slot);
                entry->auto_portrait_refresh = true;
            }
        } else if (!CurrentGame().party.rows[party_slot].fOccupied) {
            ClearPortraitRefreshSlot(static_cast<int>(party_slot));
            entry->auto_portrait_refresh = false;
            MainGame()->edge_targets[party_slot]->setEnabled(false);
        } else if (!entry->portrait_refresh_pinned && !entry->damage_splat_active &&
                   !entry->effect_icon_active && !entry->portrait_event_active) {
            if (!IsPartyPortraitUnderCursor(party_slot) || entry->auto_portrait_refresh) {
                ClearPortraitRefreshSlot(static_cast<int>(party_slot));
                entry->auto_portrait_refresh = false;
            }
        }
    }
}

/* Clear one party slot's pending portrait refresh while the screen is not in
   portrait mode: drop the pending latch and cached image/mode, wipe the
   startup-grid portrait rect, re-enable the portrait input region and disable
   the slot's region set. */
void ClearPortraitRefreshSlot(int slot)
{
    W8MainGameInvalidateRect* row;

    if (g_settings.main_ui_mode != W8_MAIN_UI_MODE_PORTRAITS &&
        MainGameHud()->portrait_refresh_pending[slot] != 0) {
        MainGameHud()->portrait_refresh_pending[slot] = 0;
        CurrentGame().monster_manager_entries[slot].portrait_refresh_pinned = false;
        MainGameHud()->portrait_refresh_image[slot] = 0;
        row = &g_main_game_invalidate_rects[slot];
        EraseRegion(row->left - 1, row->top, row->right + 1, row->bottom);
        RequestRedraw(1 << (slot & 0x1f));
        MainGame()->edge_targets[slot]->setEnabled(true);
        DisableMainGamePortraitSlot(slot);

        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_PANEL);
    }
}

/* Note what the pointer is hovering over. Moving to anything else restarts the
   tooltip clock; staying put leaves it running, which is what makes the four
   fields one tooltip rather than four settings. */
void SetTooltipSubject(W8PortraitHighlightKind kind, int subject)
{
    auto& pending = MainGameHud()->pending_highlight;
    if (pending.kind != kind || pending.slot != subject) {
        MainGameHud()->tooltip_pending = true;
        MainGameHud()->tooltip_since = MonotonicMilliseconds();
        pending = {kind, subject};
    }
}

/* Apply a main-game UI mode: drop any raised formation/radar/action panels,
   optionally re-raise them from settings prefs, clear portrait refresh when
   entering portrait mode, refresh tooltip kind, and sync region/layout state. */
W8MainUiMode GetEffectiveMainUiMode()
{
    if (CurrentGame().activity.conversation && g_npc_interaction_state &&
        !g_npc_interaction_state->scripted_dialogue)
        return W8_MAIN_UI_MODE_PORTRAITS;
    const bool picker =
        gpSCSV || ItemUseDraft() || IsLockInteractionActive() || IsTrapInteractionActive();
    return picker && g_settings.main_ui_mode == W8_MAIN_UI_MODE_RADAR ? W8_MAIN_UI_MODE_FORMATION
                                                                      : g_settings.main_ui_mode;
}

void SetMainUiPreference(W8MainUiMode mode)
{
    g_settings.main_ui_mode = mode;
    RefreshMainGameLayout();
}

void RefreshMainGameLayout()
{
    const auto mode = GetEffectiveMainUiMode();
    const bool portraits = mode == W8_MAIN_UI_MODE_PORTRAITS;
    const bool constrained =
        gpSCSV || ItemUseDraft() || IsLockInteractionActive() || IsTrapInteractionActive() ||
        (CurrentGame().activity.conversation && !g_npc_interaction_state->scripted_dialogue);
    SetTooltipSubject(portraits ? W8_HIGHLIGHT_TARGET : W8_HIGHLIGHT_VITALS, -1);
    CloseMainGameOverlays();
    if (portraits) {
        for (unsigned int slot = 0; slot < 8; ++slot) ClearPortraitRefreshSlot(slot);
    }
    if (constrained) {
        // The open interaction supplies the bottom panel instead of the action bar.
        MainGameHud()->action_panel_visible = true;
    } else if (mode != W8_MAIN_UI_MODE_RADAR) {
        const bool formation = portraits || g_settings.formation_board_preference;
        const bool radar = portraits || g_settings.formation_radar_map_preference;
        const bool action = portraits ? g_settings.portraits_action_panel_preference
                                      : g_settings.formation_action_panel_preference;
        if (formation) SetFormationBoardVisible(true);
        if (radar) SetRadarMapVisible(true);
        if (action) SetActionPanelVisible(true);
    }
    ClearUiHover();
    if (!portraits) {
        MainGameHud()->portrait_hover_x_origin = 0x69;
    } else {
        MainGameHud()->portrait_hover_x_origin = 0;
    }
    if (MainGameHud()->action_panel_visible == 0) {
        MainGameHud()->portrait_y_shift = 0x76;
    } else {
        MainGameHud()->portrait_y_shift = 0;
    }
    RequestRedraw(W8_MAIN_REDRAW_ALL);
    SetMouseCursorHotspot(0, 0);
    SyncSystemCursor();
    SetViewportMode(GetMainGameViewportMode());
    if (CurrentGame().combat.has_value()) {
        MainGameHud()->refresh_combat_panel = 1;
    }
    RequestRedrawCombatBar();
    SyncMainGameModeInput();
}

void RetireModal(std::unique_ptr<ModalContent> owner, bool completed)
{
    if (!owner) return;
    const UiTransaction transaction;
    auto* closing = owner.get();
    DestroyAfterUiDispatch(std::move(owner));
    AfterUiDispatch([closing, completed] {
        if (auto completion = closing->TakeCompletion(completed)) completion();
    });
}

bool ProcessModalOwner()
{
    {
        const UiTransaction transaction;
        ModalContent* const running = g_modal_owner.get();
        if (running->ProcessInput() || g_modal_owner.get() != running) return false;
        auto closing = std::exchange(g_modal_owner, std::move(g_pending_main_game_dialog));
        if (g_modal_owner) g_modal_owner->RequestModalRedraw();
        RetireModal(std::move(closing), true);
    }
    return !g_modal_owner;
}

/* Take the screen for a modal owner and retire the previous one. */
void OpenModal(std::unique_ptr<ModalContent> owner)
{
    RetireModal(std::exchange(g_modal_owner, std::move(owner)), false);
}

/* Re-sync each party slot's region set and portrait hit region with the
   slot's occupancy, its monster-entry flag and the display mode - the party
   add/remove entries and the keyboard menu's close run it after slot state
   changes. */
void RefreshPartySlotRegions(void)
{
    if (!MainGame()) return;
    for (int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        const bool available = CurrentGame().party.rows[slot].fOccupied &&
                               !CurrentGame().monster_manager_entries[slot].keyboard_menu_open;
        const bool full = GetEffectiveMainUiMode() == W8_MAIN_UI_MODE_PORTRAITS ||
                          MainGameHud()->portrait_refresh_pending[slot] != 0;
        MainGame()->portraits[slot]->setEnabled(available && full);
        MainGame()->edge_targets[slot]->setEnabled(available && !full);
    }
}

/* Set which main-game controls take part in input for the current UI mode
   and open view. Radar mode hides the action panel and every overlay view. */
void SyncMainGameModeInput(void)
{
    auto& state = *MainGame();
    const W8MainUiMode mode = GetEffectiveMainUiMode();
    const bool panels = mode != W8_MAIN_UI_MODE_RADAR;
    const bool npc_view =
        CurrentGame().activity.conversation &&
        !(CurrentGame().activity.conversation && g_npc_interaction_state->scripted_dialogue);
    const bool view_open = gpSCSV || npc_view || ItemUseDraft() || IsLockInteractionActive() ||
                           IsTrapInteractionActive();

    state.world.setEnabled(true);
    state.hud_buttons.input.setEnabled(true);
    state.monster_list->setEnabled(MainGameHud()->group_list_rows != 0);
    state.edges.setEnabled(mode != W8_MAIN_UI_MODE_PORTRAITS);
    RefreshPartySlotRegions();
    state.combat.setEnabled(CurrentGame().combat.has_value());
    if (CurrentGame().combat.has_value()) {
        DisablePortraitControls();
    } else {
        EnablePortraitAdvanceRegions();
    }
    if (mode == W8_MAIN_UI_MODE_PORTRAITS) {
        DisableConditionButtons();
    } else {
        EnableConditionButtons();
    }

    // Spell and item views always raise the text box; otherwise it follows the action panel.
    const bool spell_text_box = gpSCSV || (!npc_view && ItemUseDraft());
    if (panels && spell_text_box) {
        RestoreSpellCastingRegions();
    } else if (panels && MainGameHud()->action_panel_visible != 0) {
        EnableActionPanelRegions();
    } else {
        SetTextBoxEnabled(false);
    }
    if (gpSCSV) gpSCSV->input.setEnabled(panels);
    if (npc_view) SetNpcDialogueInputEnabled(2, panels);
    if (ItemUseDraft()) SetUseItemSelectInputEnabled(panels);
    if (IsLockInteractionActive()) LockInteraction()->EnablePanels(panels);
    if (IsTrapInteractionActive()) TrapInteraction()->EnablePanelInput(panels);
    state.formation.setEnabled(panels && !view_open && MainGameHud()->formation_board_visible);
    state.radar.setEnabled(panels && !view_open && MainGameHud()->radar_map_visible != 0);

    if (PartyMovementPanelShown() && !CurrentGame().advancement.held) {
        UpdatePartyMovementPanel();
    } else {
        DisablePartyMovementRegions();
    }
}

/* Party portrait hit region: left-click selects / aims, right-hold opens camp,
   drag-hover arms the mode-6 overlay, and InputEventType::MouseMove drives help plus targeting
   cursor. Skips the slot whose NPC the camp just dismissed. */
UiResult PortraitSelectInput(const UiPointerEvent& event, HitTarget& target, int id)
{
    if (event.action != UiPointerAction::Enter &&
        (event.action != UiPointerAction::Up || event.captured_buttons))
        PlayButtonSound(event.action);
    int slot = id;
    bool targeting = false;
    bool aim_ok = false;
    bool front_rank = false;
    W8TargetNeed needed;
    std::string help_text;

    if (slot == MainGame()->dismissed_npc_slot) {
        return UiResult::Pass;
    }

    const auto editing_context = GetEditedActionContext(CurrentGame().status.selected_character);
    const auto selected_action =
        CurrentGame().status.selected_character < 0
            ? W8Action{W8_ACTION_NONE, -1, {}, {}}
            : SelectCombatAction(CurrentGame().status.selected_character, editing_context);
    needed = editing_context == W8_TARGETING_CONTEXT_OUT_OF_COMBAT
                 ? W8_TARGET_NEED_NONE
                 : GetTargetNeededForAction(CurrentGame().party, selected_action);
    if (TargetingMode() == W8_TARGET_NEED_ALLY ||
        (needed == W8_TARGET_NEED_ALLY && (event.modifiers & CTRL_DOWN) != 0)) {
        targeting = true;
        if (CanPartySlotParticipate(slot)) {
            front_rank = false;
            aim_ok = true;
            if (selected_action.kind == W8_ACTION_PROTECT) {
                if (CurrentGame().status.selected_character == slot) {
                    front_rank = false;
                    aim_ok = false;
                } else if (FrontRankScreens(CurrentGame().status.selected_character, slot)) {
                    front_rank = true;
                    aim_ok = true;
                }
            }
        }
    } else if (TargetingMode() == W8_TARGET_NEED_CHARACTER_INDIRECT ||
               (needed == W8_TARGET_NEED_CHARACTER_INDIRECT &&
                (event.modifiers & CTRL_DOWN) != 0)) {
        targeting = true;
        IsDeadCharacterTargetable(slot);
    }

    if ((event.button == UiPointerButton::Left)) {
        if ((event.button == UiPointerButton::Left && event.action == UiPointerAction::Repeat)) {
            if (MainGameHud()->portrait_refresh_pending[slot] == 0 &&
                g_settings.main_ui_mode != W8_MAIN_UI_MODE_PORTRAITS &&
                MainGameHud()->portrait_overlay_party_slot == -1 &&
                TimeUntilDeadline(MainGameHud()->countdown5) == 0 &&
                (event.captured_buttons & SDL_BUTTON_LMASK) != 0) {
                MainGameHud()->portrait_overlay_party_slot = slot;
                if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME && MainGameHud() != nullptr) {
                    RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_PANEL);
                    return UiResult::Handled;
                }
            }
            /* retail falls through to return 1 at the end when the arm did not
               redraw */
        } else if ((event.button == UiPointerButton::Left &&
                    event.action == UiPointerAction::Down && event.clicks != 2)) {

            if (MainGameHud()->portrait_refresh_pending[slot] == 0 &&
                g_settings.main_ui_mode != W8_MAIN_UI_MODE_PORTRAITS &&
                MainGameHud()->portrait_overlay_party_slot == -1) {
                MainGameHud()->countdown5 = MonotonicMilliseconds() + (500);
                return UiResult::Handled;
            }
        } else if ((event.button == UiPointerButton::Left && event.action == UiPointerAction::Up)) {
            if ((event.captured_buttons & SDL_BUTTON_LMASK) != 0) {
                if (MainGameHud()->portrait_overlay_party_slot == slot) {
                    MainGameHud()->portrait_overlay_party_slot = -1;
                    DismissHighlightOverlay();
                    if (CurrentScreenState().id != W8_SCREEN_MAIN_GAME) {
                        g_main_game_mode = W8_MAIN_GAME_DEFAULT;
                        return UiResult::Handled;
                    }
                    if (MainGameHud() == nullptr) {
                        g_main_game_mode = W8_MAIN_GAME_DEFAULT;
                        return UiResult::Handled;
                    }
                    RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_PANEL);
                    if (CurrentScreenState().id != W8_SCREEN_MAIN_GAME) {
                        return UiResult::Handled;
                    }
                    if (MainGameHud() == nullptr) {
                        return UiResult::Handled;
                    }
                    RequestRedraw(W8_MAIN_REDRAW_PORTRAITS);
                } else if (IsNpcDialogueCursorActive() == 0) {
                    if (targeting) {
                        if (aim_ok) {
                            if (!CurrentGame().activity.scripted_scene) {
                                if (!CanPartySlotParticipate(slot)) {
                                    AimAtCharacterIndirect(
                                        CurrentGame().status.selected_character, slot,
                                        GetEditedActionContext(
                                            CurrentGame().status.selected_character));
                                    StartBreathCycle(CurrentGame().status.selected_character,
                                                     false);
                                } else if (front_rank) {
                                    QueueCharacterEvent(
                                        &CurrentGame()
                                             .party
                                             .characters[CurrentGame().status.selected_character],
                                        0x35, 0, kCharacterEventFullVolume);
                                    ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[0x7dc]);
                                } else {
                                    AimAtCharacter(CurrentGame().status.selected_character, slot,
                                                   GetEditedActionContext(
                                                       CurrentGame().status.selected_character));
                                    StartBreathCycle(CurrentGame().status.selected_character,
                                                     false);
                                }
                            } else {
                                EndScriptedPortraitPick(MakeScriptContext(CurrentGame()), slot);
                            }
                        } else {
                            QueueCharacterEvent(
                                &CurrentGame()
                                     .party.characters[CurrentGame().status.selected_character],
                                0x35, 0, kCharacterEventFullVolume);
                        }
                    } else if (CurrentGame().party.held.item.iItemNo == -1 ||
                               ShownCursor() != W8_CURSOR_INVALID_TARGET ||
                               (MainGame()->item_drag &&
                                MainGame()->item_drag->item.storage == W8ItemStorage::Hand)) {
                        if (g_settings.main_ui_mode != W8_MAIN_UI_MODE_PORTRAITS) {
                            RefreshSelectedPartyPortrait(slot);
                        }
                        SelectPartyCharacter(slot);
                    } else {
                        bool force_to_party;
                        if (!CurrentGame().combat.has_value() && IsPartySlotEligible(slot)) {
                            force_to_party = false;
                        } else {
                            force_to_party = true;
                        }
                        if (auto given =
                                GiveHeldItemToCharacterOrParty(CurrentGame(), slot, force_to_party);
                            !given)
                            ShowRefusal(given.error());
                        if (ItemUseDraft()) {
                            RefreshUseItemSelection();
                        }
                    }
                } else {
                    TryNpcDialoguePickpocket(slot);
                }
            }
            if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME && MainGameHud() != nullptr) {
                RequestRedraw(1 << (slot & 0x1f));
                if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME && MainGameHud() != nullptr) {
                    RequestRedraw(W8_MAIN_REDRAW_CHARACTER_ACTION);
                    return UiResult::Handled;
                }
            }
        } else {
            if (!(event.button == UiPointerButton::Left && event.action == UiPointerAction::Down &&
                  event.clicks == 2)) {
                return UiResult::Pass;
            }
            if (IsNpcDialogueCursorActive() == 0 && !CurrentGame().activity.scripted_scene) {
                AfterUiDispatch([=] { OpenCharacterScreenForPartySlot(slot, false); });
                return UiResult::Handled;
            }
        }
    } else if ((event.button == UiPointerButton::Right && event.action == UiPointerAction::Down)) {

        if (!targeting && IsNpcDialogueCursorActive() == 0 &&
            !CurrentGame().activity.scripted_scene && !MainGameHud()->portrait_right_hold_armed) {
            MainGameHud()->countdown4 = MonotonicMilliseconds() + (1000);
            MainGameHud()->portrait_right_hold_armed = true;
        }
    } else {
        if (!(event.button == UiPointerButton::Right && event.action == UiPointerAction::Up)) {
            if (!(event.action == UiPointerAction::Move || event.action == UiPointerAction::Enter ||
                  event.action == UiPointerAction::Leave)) {
                return UiResult::Pass;
            }
            if (MainGameHud()->portrait_right_hold_armed &&
                TimeUntilDeadline(MainGameHud()->countdown4) == 0) {
                MainGameHud()->portrait_right_hold_armed = false;
                AfterUiDispatch([=] { OpenCharacterScreenForPartySlot(slot, false); });
            }
            if (event.action != UiPointerAction::Leave) {
                if (event.action != UiPointerAction::Enter) {
                    if (IsNpcDialogueCursorActive() == 0) {
                        if (targeting && aim_ok) {
                            if (TargetingMode() == W8_TARGET_NEED_NONE) {
                                RevalidateSelectedTarget(CurrentGame().status.selected_character);
                            }
                            SetTargetCursor(4);
                            return UiResult::Pass;
                        }
                        SetTargetCursor(GetTargetingCursorForState(false));
                    }
                } else {
                    if (MainGameHud()->portrait_refresh_pending[slot] == 0) {
                        if (CurrentGame().party.held.item.iItemNo == -1 ||
                            ShownCursor() != W8_CURSOR_INVALID_TARGET ||
                            (MainGame()->item_drag &&
                             MainGame()->item_drag->item.storage == W8ItemStorage::Hand)) {
                            help_text = Data().strings[0x1d];
                        } else {
                            help_text = Data().strings[0x1e];
                        }
                        target.setTooltip([text = help_text] { return std::string(text); });
                    }

                    MainGameHud()->portrait_right_hold_armed = false;
                    if (IsNpcDialogueCursorActive() == 0) {
                        SetTooltipSubject(W8_HIGHLIGHT_TARGET, slot);
                        if (TargetingMode() == W8_TARGET_NEED_NONE) {

                            return UiResult::Pass;
                        }
                    }
                }
            } else {
                MainGameHud()->portrait_right_hold_armed = false;
                if (IsNpcDialogueCursorActive() == 0) {
                    SetTooltipSubject(W8_HIGHLIGHT_TARGET, -1);
                    SetTargetCursor(GetTargetingCursorForState(false));
                }
                if (MainGameHud()->portrait_overlay_party_slot == slot) {
                    MainGameHud()->portrait_overlay_party_slot = -1;
                    DismissHighlightOverlay();
                    if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME &&
                        MainGameHud() != nullptr) {
                        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_PANEL);
                        if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME &&
                            MainGameHud() != nullptr) {
                            RequestRedraw(W8_MAIN_REDRAW_PORTRAITS);
                            return UiResult::Pass;
                        }
                    }
                }
            }
            return UiResult::Pass;
        }
        if ((event.captured_buttons & SDL_BUTTON_RMASK) != 0 && !targeting &&
            IsNpcDialogueCursorActive() == 0 && !CurrentGame().activity.scripted_scene) {
            MainGameHud()->portrait_right_hold_armed = false;
            if (!(CurrentGame().party.held.item.iItemNo != -1) ||
                ((GetInputState().modifiers & CTRL_DOWN) != 0)) {
                AfterUiDispatch([=] { OpenCharacterScreenForPartySlot(slot, false); });
                return UiResult::Handled;
            }
            if (auto given = GiveHeldItemToCharacterOrParty(CurrentGame(), slot, true); !given)
                ShowRefusal(given.error());
            if (ItemUseDraft()) {
                RefreshUseItemSelection();
                return UiResult::Handled;
            }
        }
    }
    return UiResult::Handled;
}

/* Both portrait orbs share this event policy. Member pointers pick each orb's
   effect field and its overlay slot in the HUD. */
template<class Effect>
static UiResult DispatchPortraitOrbInput(const UiPointerEvent& event, HitTarget& target, int id,
                                         Effect W8Character::* effect, Effect none,
                                         int W8LevelRuntimeBlock::* selection,
                                         W8PortraitHighlightKind tooltip_kind)
{
    W8LevelRuntimeBlock* hud = MainGameHud();
    if (hud == nullptr) return UiResult::Pass;
    int slot = id;
    W8Character* character = &CurrentGame().party.characters[slot];

    if (((character->*effect) != none) &&
        (event.action != UiPointerAction::Up || event.captured_buttons))
        PlayButtonSound(event.action);

    if (!(event.button == UiPointerButton::Left && event.action == UiPointerAction::Down)) {
        if (!(event.button == UiPointerButton::Left && event.action == UiPointerAction::Up)) {
            if (!(event.action == UiPointerAction::Move || event.action == UiPointerAction::Enter ||
                  event.action == UiPointerAction::Leave)) {
                return UiResult::Pass;
            }
            if (event.action == UiPointerAction::Leave) {
                SetTooltipSubject(tooltip_kind, -1);
                if ((hud->*selection) != -1) {
                    (hud->*selection) = -1;
                    DismissHighlightOverlay();
                    RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_PANEL);
                }
            } else {
                if (event.action == UiPointerAction::Enter) {
                    if ((character->*effect) != none) {
                        SetTooltipSubject(tooltip_kind, slot);
                        if (IsLeftButtonHeld() && (character->*effect) != none) {
                            (hud->*selection) = slot;
                            if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME)
                                RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_PANEL);
                        }
                        target.setTooltip(
                            [tooltip_kind] { return Data().strings[24 + tooltip_kind]; });
                        return UiResult::Pass;
                    }
                } else {
                    return UiResult::Pass;
                }
            }
            target.setTooltip({});
            return UiResult::Pass;
        }
        if ((hud->*selection) == -1) {
            return UiResult::Handled;
        }
        (hud->*selection) = -1;
        DismissHighlightOverlay();
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_PANEL);
        return UiResult::Handled;
    }

    if ((character->*effect) == none) {
        return UiResult::Handled;
    }
    (hud->*selection) = slot;
    RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_PANEL);
    return UiResult::Handled;
}

/* Condition orb on a party portrait (help 25): press while highest_condition
   is set arms the overlay slot; release and leave dismiss the hover plate;
   enter drives tooltip kind 1 and region help. */
UiResult PortraitConditionOrbInput(const UiPointerEvent& event, HitTarget& target, int id)
{
    return DispatchPortraitOrbInput(
        event, target, id, &W8Character::highest_condition, W8_CONDITION_NONE,
        &W8LevelRuntimeBlock::condition_orb_party_slot, W8_HIGHLIGHT_CONDITION);
}

/* Enchantment orb on a party portrait (help 26): near-clone of the condition
   orb against enchantment_top and tooltip kind 2. */
UiResult PortraitEnchantmentOrbInput(const UiPointerEvent& event, HitTarget& target, int id)
{
    return DispatchPortraitOrbInput(
        event, target, id, &W8Character::enchantment_top, W8_ENCHANTMENT_NONE,
        &W8LevelRuntimeBlock::enchantment_orb_party_slot, W8_HIGHLIGHT_ENCHANTMENT);
}

/* Help 28: portrait side bar — hover picks upper/lower weapon or assayable
   item; click opens Assay or binds items with Shift. */
UiResult PortraitAssaySidebarInput(const UiPointerEvent& event, HitTarget& target, int id)
{
    if (event.action != UiPointerAction::Enter &&
        (event.action != UiPointerAction::Up || event.captured_buttons))
        PlayButtonSound(event.action);
    W8PortraitAssayHoverMode previous_mode = MainGameHud()->portrait_assay_hover_mode;
    int slot = id;
    W8Character* character;
    W8ItemInstance* item;
    int item_id;

    if (!IsPartySlotEligible(slot)) {
        return UiResult::Pass;
    }

    character = &CurrentGame().party.characters[slot];
    item_id = character->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo;
    if (item_id == -1 || (Data().items[item_id].flags & W8_ITEM_FLAG_TWO_HANDED) == 0) {
        if (event.local.y < 0x19) {
            MainGameHud()->portrait_assay_hover_mode = W8_PORTRAIT_ASSAY_HOVER_PRIMARY_WEAPON;
        } else {
            MainGameHud()->portrait_assay_hover_mode = W8_PORTRAIT_ASSAY_HOVER_SECONDARY_WEAPON;
        }
    } else {
        MainGameHud()->portrait_assay_hover_mode = W8_PORTRAIT_ASSAY_HOVER_TWO_HANDED_WEAPON;
    }

    if (previous_mode != MainGameHud()->portrait_assay_hover_mode) {
        RequestRedraw(1 << (slot & 0x1f));
    }

    if ((event.button == UiPointerButton::Left) ||
        (event.button == UiPointerButton::Right && event.action == UiPointerAction::Down)) {
        if ((event.action == UiPointerAction::Down)) {
            return UiResult::Handled;
        }
        if (!(event.button == UiPointerButton::Left && event.action == UiPointerAction::Up)) {
            return UiResult::Pass;
        }
        if ((event.captured_buttons & SDL_BUTTON_LMASK) == 0) {
            return UiResult::Handled;
        }
        if ((event.modifiers & SHIFT_DOWN) == 0) {
            BindCharacterItems(CurrentGame(), slot, true);
            return UiResult::Handled;
        }
        BindEveryPartyItem();
        return UiResult::Handled;
    }
    if (!(event.button == UiPointerButton::Right && event.action == UiPointerAction::Up)) {
        if (!(event.action == UiPointerAction::Move || event.action == UiPointerAction::Enter ||
              event.action == UiPointerAction::Leave)) {
            return UiResult::Pass;
        }
        if (event.action != UiPointerAction::Leave) {
            if (event.action == UiPointerAction::Enter) {

                SetTooltipSubject(W8_HIGHLIGHT_ASSAY, slot);
            }
        } else {
            SetTooltipSubject(W8_HIGHLIGHT_ASSAY, -1);
            return UiResult::Pass;
        }
        return UiResult::Pass;
    }
    if ((event.captured_buttons & SDL_BUTTON_RMASK) == 0) {
        return UiResult::Handled;
    }
    {
        W8PortraitAssayHoverMode mode = MainGameHud()->portrait_assay_hover_mode;
        if (mode != W8_PORTRAIT_ASSAY_HOVER_PRIMARY_WEAPON) {
            if (mode == W8_PORTRAIT_ASSAY_HOVER_SECONDARY_WEAPON) {
                item = &character->EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON];
                goto open_assay;
            }
            if (mode != W8_PORTRAIT_ASSAY_HOVER_TWO_HANDED_WEAPON) {
                return UiResult::Handled;
            }
        }
        item = &character->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON];
    open_assay:
        if (item != 0 && item->iItemNo != -1) {
            AfterUiDispatch([=] { OpenAssayDialog(*item, slot); });
        }
    }
    return UiResult::Handled;
}

/* Help 24: portrait overlay hover strip; left-down and enter (while held)
   set portrait_overlay_party_slot, leave and up dismiss the mode-6 plate. */
UiResult PortraitOverlayHoverInput(const UiPointerEvent& event, HitTarget& target, int id)
{
    if (event.action != UiPointerAction::Enter &&
        (event.action != UiPointerAction::Up || event.captured_buttons))
        PlayButtonSound(event.action);
    int slot = id;

    if ((event.button == UiPointerButton::Left && event.action == UiPointerAction::Down)) {
        MainGameHud()->portrait_overlay_party_slot = slot;
    } else {
        if (!(event.button == UiPointerButton::Left && event.action == UiPointerAction::Up)) {
            if (!(event.action == UiPointerAction::Move || event.action == UiPointerAction::Enter ||
                  event.action == UiPointerAction::Leave)) {
                return UiResult::Pass;
            }
            if (event.action != UiPointerAction::Leave) {
                if (event.action != UiPointerAction::Enter) {
                    return UiResult::Pass;
                }

                SetTooltipSubject(W8_HIGHLIGHT_VITALS, slot);
                if (!IsLeftButtonHeld()) {
                    return UiResult::Pass;
                }
                MainGameHud()->portrait_overlay_party_slot = slot;
            } else {
                SetTooltipSubject(W8_HIGHLIGHT_VITALS, -1);
                if (MainGameHud()->portrait_overlay_party_slot == -1) {
                    return UiResult::Pass;
                }
                MainGameHud()->portrait_overlay_party_slot = -1;
                DismissHighlightOverlay();
            }
            RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_PANEL);
            return UiResult::Pass;
        }
        MainGameHud()->portrait_overlay_party_slot = -1;
        DismissHighlightOverlay();
    }
    RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_PANEL);
    return UiResult::Handled;
}

/* Combat-action hit regions beside the eight party portraits (region set 4):
   open the keyboard menu on release while combat input is live, drive hover
   help for a queued spell/item, and right-click falls back when an action is
   unreachable. */
UiResult PartyCombatActionInput(const UiPointerEvent& event, HitTarget& target, int id)
{
    if (event.action != UiPointerAction::Up || event.captured_buttons)
        PlayButtonSound(event.action);
    int slot = id;
    if (!IsPartySlotEligible(slot)) {
        return UiResult::Pass;
    }

    if (event.button == UiPointerButton::Right && event.action == UiPointerAction::Down) {

        return UiResult::Handled;
    }
    if (event.button == UiPointerButton::Left && event.action == UiPointerAction::Down) {

        return UiResult::Handled;
    }
    if (event.button == UiPointerButton::Left && event.action == UiPointerAction::Up) {
        if ((event.captured_buttons & SDL_BUTTON_LMASK) != 0 &&
            CurrentGame().party.rows[slot].fOccupied) {
            if (MainGameHud()->keyboard_menu_open && slot != MainGameHud()->combat_slot) {
                CloseKeyboardMenu();
            }
            if (CurrentGame().combat->round_active) {
                MainGameHud()->combat_slot = slot;
                OpenKeyboardMenuForSlot(slot);
                return UiResult::Handled;
            }
        }
        return UiResult::Handled;
    }
    if (event.button == UiPointerButton::Right && event.action == UiPointerAction::Up) {
        if ((event.captured_buttons & SDL_BUTTON_RMASK) != 0 && !gpSCSV && !ItemUseDraft()) {
            FallbackFromUnreachableAction(slot);
        }
        return UiResult::Handled;
    }
    if (event.action == UiPointerAction::Move || event.action == UiPointerAction::Enter ||
        event.action == UiPointerAction::Leave) {
        if (event.action == UiPointerAction::Leave) {
            if (!MainGameHud()->keyboard_menu_open) {
                MainGameHud()->combat_slot = -1;
                MainGameHud()->countdown3 = MonotonicMilliseconds() + (0);
                MainGameHud()->hover_combat_slot = -1;
            }
            MainGameHud()->combat_action_hover_party_slot = -1;
            CurrentGame().monster_manager_entries[slot].combat_portrait_dirty = true;
            target.setTooltip({});
            return UiResult::Pass;
        }
        if (event.action != UiPointerAction::Enter) {
            return UiResult::Pass;
        }
        if (CurrentGame().combat->round_active) {
            MainGameHud()->combat_action_hover_party_slot = slot;
            CurrentGame().monster_manager_entries[slot].combat_portrait_dirty = true;
        }
        {
            W8PartySlotRow* row = &CurrentGame().party.rows[slot];
            if (row->committed.kind == W8_ACTION_CAST_SPELL) {

                target.setTooltip(
                    [text = fmt::format("{}: {} ({:d})", Data().strings[0x68],
                                        Data().spells[row->committed.detail].display_name,
                                        row->committed.detail_block.spell.power_level)] {
                        return std::string(text);
                    });
                return UiResult::Pass;
            }
            if (row->committed.kind == W8_ACTION_USE_ITEM) {

                target.setTooltip(
                    [text = fmt::format(
                         "{}: {}", Data().strings[0x69],
                         FormatItemDisplayName(
                             CurrentGame().party.itemAt(row->committed.detail_block.item_use.item),
                             false))] { return std::string(text); });
                return UiResult::Pass;
            }
        }
        target.setTooltip({});
        return UiResult::Pass;
    }
    return UiResult::Pass;
}

/* Help 31: radar-map button near the text area — left-up opens automap after
   tearing down mode 3/5/6 overlays; right-up toggles zoom; enter/leave hover
   the map button art. */
UiResult RadarMapButtonInput(const UiPointerEvent& event)
{
    if (event.action != UiPointerAction::Up || event.captured_buttons)
        PlayButtonSound(event.action);

    if ((event.button == UiPointerButton::Left) ||
        (event.button == UiPointerButton::Right && event.action == UiPointerAction::Down)) {
        if ((event.action == UiPointerAction::Down)) {
            return UiResult::Handled;
        }
        if (!(event.button == UiPointerButton::Left && event.action == UiPointerAction::Up)) {
            return UiResult::Pass;
        }
        if ((event.captured_buttons & SDL_BUTTON_LMASK) != 0) {
            SetMainGameMode(W8_MAIN_GAME_DEFAULT);
            RequestScreen({.id = W8_SCREEN_AUTOMAP});
            return UiResult::Handled;
        }
    } else if ((event.button == UiPointerButton::Right && event.action == UiPointerAction::Up)) {
        if ((event.captured_buttons & SDL_BUTTON_RMASK) != 0) {
            ToggleRadarMapZoom();
        }
    } else {
        if (!(event.action == UiPointerAction::Move || event.action == UiPointerAction::Enter ||
              event.action == UiPointerAction::Leave)) {
            return UiResult::Pass;
        }
        if (event.action == UiPointerAction::Leave) {
            MainGameHud()->radar_map_alternate = 0;
            RefreshRadarMap();
            return UiResult::Handled;
        }
        if (event.action == UiPointerAction::Enter) {
            MainGameHud()->radar_map_alternate = 1;
            RefreshRadarMap();
            return UiResult::Handled;
        }
    }
    return UiResult::Handled;
}

/* Restore the cursor and clear the mouse-look state on both exit paths. */
static void StopWorldMouseLook()
{
    WarpSystemCursor(g_mouselook_cursor_pos.x, g_mouselook_cursor_pos.y);
    EnableCursorScene();
    g_mouselook_active = false;
    g_mouselook_left_held = false;
    SetMouseMoveEventsEnabled(0);
    partyCamera().manual_mouse_input = false;
}

/* Region 23: the 3D world view. Mouse-move refreshes the combat hover, the
   world-item hover and the portrait strip; left-up runs the targeting, item
   and monster dispatch; right-down opens monster info or the assay dialog or
   toggles the mouselook latch. */
UiResult WorldViewInput(const UiPointerEvent& event)
{
    int cursor_x;
    int cursor_y;
    W8TargetNeed needed;
    int slot;

    if (FormationPanelOpen() || g_camera_path_active) {
        return UiResult::Pass;
    }
    const auto editing_context = GetEditedActionContext(CurrentGame().status.selected_character);
    const auto selected_action =
        CurrentGame().status.selected_character < 0
            ? W8Action{W8_ACTION_NONE, -1, {}, {}}
            : SelectCombatAction(CurrentGame().status.selected_character, editing_context);
    needed = editing_context == W8_TARGETING_CONTEXT_OUT_OF_COMBAT
                 ? W8_TARGET_NEED_NONE
                 : GetTargetNeededForAction(CurrentGame().party, selected_action);
    if (!(event.button == UiPointerButton::Left) &&
        !(event.button == UiPointerButton::Right && event.action == UiPointerAction::Down)) {
        if ((event.button == UiPointerButton::Right && event.action == UiPointerAction::Up)) {
            if (g_mouselook_active && g_settings.mouselook_toggle == 0) {
                StopWorldMouseLook();
                return UiResult::Handled;
            }

            return UiResult::Handled;
        }
        if (!(event.action == UiPointerAction::Move || event.action == UiPointerAction::Enter ||
              event.action == UiPointerAction::Leave)) {
            return UiResult::Pass;
        }
        cursor_y = event.position.y;
        cursor_x = event.position.x;

        int hover;
        if (!CurrentGame().activity.conversation && TargetingMode() != W8_TARGET_NEED_PLACE &&
            TargetingMode() != W8_TARGET_NEED_CONE && TargetingMode() != W8_TARGET_NEED_ITEM &&
            CurrentLevelPopulation().monster_roster.hasActiveMonsters() &&
            !IsWorldCursorVisible() && !g_mouselook_active) {
            hover = PickNearestMonsterUnderCursor(cursor_x, cursor_y);
        } else {
            hover = -1;
        }
        SetCombatSelection(hover);
        if (MainGameHud()->highlighted_item == -1) {
            if (CurrentGame().item_manager_pending != 0 && !IsWorldCursorVisible() &&
                !g_mouselook_active) {
                hover = PickNearestItemUnderCursor(cursor_x, cursor_y, 5000.0f);
            } else {
                hover = -1;
            }
            SetCombatTarget(hover);
        }

        if (event.action != UiPointerAction::Leave && g_modal_owner == 0) {
            for (slot = 0; slot < 8; ++slot) {
                if (CurrentGame().party.rows[slot].fOccupied &&
                    MainGameHud()->portrait_refresh_pending[slot] != 0 &&
                    IsPartyPortraitUnderCursor(slot)) {
                    return UiResult::Pass;
                }
            }
            UpdateWorldViewCursor(event, needed);
            return UiResult::Handled;
        }
        ClearCombatSelection();
        return UiResult::Handled;
    }
    if ((event.button == UiPointerButton::Right && event.action == UiPointerAction::Down)) {

        if (MainGameHud()->highlighted_item != -1 && !gpSCSV &&
            !CurrentGame().activity.conversation && !ItemUseDraft() && !IsLockInteractionActive() &&
            !IsTrapInteractionActive() &&
            (g_settings.ctrl_right_click_info == 0 ||
             ((GetInputState().modifiers & CTRL_DOWN) != 0))) {
            AfterUiDispatch([=] { OpenMonsterInfoDialog(MainGameHud()->highlighted_item); });
            return UiResult::Handled;
        }
        if (!(CurrentGame().party.held.item.iItemNo != -1) && MainGameHud()->selected_item != -1 &&
            !gpSCSV && !CurrentGame().activity.conversation && !ItemUseDraft() &&
            !IsLockInteractionActive() && !IsTrapInteractionActive() &&
            (g_settings.ctrl_right_click_info == 0 ||
             ((GetInputState().modifiers & CTRL_DOWN) != 0))) {
            W8WorldItem* world_item = ItemInfo(ItemIndex(MainGameHud()->selected_item));
            PartyAttemptsToIdentifyItem(&world_item->item, 0);
            OpenAssayDialog(world_item->item, -1);
            return UiResult::Handled;
        }
        if (!IsWorldCursorVisible() && !CurrentGame().activity.conversation) {
            if (g_settings.mouselook_toggle != 0) {
                if (g_mouselook_active) {
                    StopWorldMouseLook();
                    return UiResult::Handled;
                }
            } else if (g_mouselook_active) {
                return UiResult::Handled;
            }
            g_mouselook_cursor_pos = CursorPosition();
            DisableCursorScene();
            SetMouseCursorHotspot(0, 0);
            WarpSystemCursor(0x140, 0xf0);
            g_mouselook_active = true;
            g_mouselook_left_held = false;
            partyCamera().manual_mouse_input = true;
            SetMouseMoveEventsEnabled(1);
        }
        return UiResult::Handled;
    }
    if ((event.button == UiPointerButton::Left && event.action == UiPointerAction::Down)) {
        if (g_mouselook_active) {
            return UiResult::Pass;
        }

        return UiResult::Handled;
    }
    if (!(event.button == UiPointerButton::Left && event.action == UiPointerAction::Up)) {
        return UiResult::Pass;
    }
    if ((event.captured_buttons & SDL_BUTTON_LMASK) == 0) {
        return UiResult::Handled;
    }
    if (g_mouselook_active) {
        return UiResult::Handled;
    }

    if (FinishNpcVoiceIfSessionActive()) {
        return UiResult::Handled;
    }
    if (TargetingMode() == W8_TARGET_NEED_PLACE ||
        (needed == W8_TARGET_NEED_PLACE && (event.modifiers & CTRL_DOWN) != 0)) {
        if (!IsWorldCursorVisible()) {
            InitializeWorldCursor();
        }
        SetWorldCursorRange(CalcRangeDistanceFromParty(
            GetCharActionRange(CurrentGame().status.selected_character, 0, selected_action)));
        if (gpSCSV && GetActionSpellLikeId(CurrentGame().party, selected_action) == 0x3c) {
            signed char extent_index;
            glm::vec3 minimum;
            glm::vec3 maximum;
            if (gpSCSV->draft.detail_block.spell.power_level == 0) {
                extent_index = 2;
            } else {
                extent_index =
                    g_spell_power_extent_index[gpSCSV->draft.detail_block.spell.power_level - 1];
            }
            minimum =
                glm::vec3(static_cast<float>(g_world_cursor_extent_table[extent_index * 6]),
                          static_cast<float>(g_world_cursor_extent_table[extent_index * 6 + 1]),
                          static_cast<float>(g_world_cursor_extent_table[extent_index * 6 + 2]));
            maximum =
                glm::vec3(static_cast<float>(g_world_cursor_extent_table[extent_index * 6 + 3]),
                          static_cast<float>(g_world_cursor_extent_table[extent_index * 6 + 4]),
                          static_cast<float>(g_world_cursor_extent_table[extent_index * 6 + 5]));
            SetWorldCursorExtents(&minimum, &maximum);
        }
    } else if (TargetingMode() == W8_TARGET_NEED_CONE ||
               (needed == W8_TARGET_NEED_CONE && (event.modifiers & CTRL_DOWN) != 0)) {
        AimAtGroundTarget(CurrentGame().status.selected_character);
    } else if (MainGameHud()->highlighted_item == -1) {
        if ((CurrentGame().party.held.item.iItemNo != -1) && GetSelectedPropIndex() == -1) {
            if (!CurrentGame().advancement.held) {
                if (auto dropped = DropItemInHand(CurrentGame(), 1); !dropped)
                    ShowRefusal(dropped.error());
            }
        } else if (MainGameHud()->selected_item != -1) {
            if (!CurrentGame().advancement.held &&
                InteractWithWorldItem(MainGameHud()->selected_item) != 0) {
                MainGameHud()->selected_item = -1;
                VideoRemoveToolTip();
                SetTargetingMode(W8_TARGET_NEED_NONE);
            }
        } else if (!CurrentGame().advancement.held) {
            ActivateSelectedProp();
        }
    } else {
        W8MonsterInfo* monster_info = FindMonster(MainGameHud()->highlighted_item);
        bool assign = true;
        if (!CurrentGame().combat.has_value()) {
            if (needed != W8_TARGET_NEED_ENEMY && needed != W8_TARGET_NEED_ALLY &&
                needed != W8_TARGET_NEED_GROUP) {
                assign = false;
                if ((ShownCursor() == W8_CURSOR_VALID_TARGET ||
                     (CurrentGame().party.held.item.iItemNo != -1)) &&
                    CurrentGame().status.selected_character != -1 &&
                    AssessMonsterAim(
                        CurrentGame().status.selected_character, 2, monster_info,
                        SelectCombatAction(
                            CurrentGame().status.selected_character,
                            GetEditedActionContext(CurrentGame().status.selected_character)))) {
                    if ((GetMonsterDataForInfo(monster_info)->flags & W8_MONSTER_FLAG_NPC) == 0) {
                        ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[0x7de]);
                    } else {
                        W8ItemInstance* item = 0;
                        if ((CurrentGame().party.held.item.iItemNo != -1)) {
                            item = &CurrentGame().party.held.item;
                        }
                        if (monster_info->highest_condition < W8_CONDITION_ASLEEP) {
                            W8NpcState* npc = FindNpcBindingForMonster(
                                FindMonster(MainGameHud()->highlighted_item));
                            QueueNpcScriptNotice(npc, item, -1, false, 0);
                            SetCombatSelection(-1);
                        } else {
                            ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[0x7de]);
                        }
                    }
                }
            }
        } else if (!CurrentGame().combat->round_active && !CurrentGame().party_movement_mode) {
            ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[0x7dd]);
            assign = false;
        } else if (((GetInputState().modifiers & SHIFT_DOWN) != 0)) {
            for (slot = 0; slot < 8; ++slot) {
                if (CurrentGame().party.rows[slot].fOccupied &&
                    CurrentGame().party.characters[slot].hp_current != 0) {
                    AimAtMonsterLocation(slot, MainGameHud()->highlighted_item, false);
                }
            }
            assign = false;
        }
        if (assign) {
            if (CurrentGame().status.selected_character == -1) {
                Fail("gStatus.iSelectedCharacter != -1");
            }
            AimAtMonsterLocation(CurrentGame().status.selected_character,
                                 MainGameHud()->highlighted_item, true);
        }
    }

    return UiResult::Handled;
}

/* Help 36: combat monster-list rows. Hit-test by 11-pixel row, hover via
   SetCombatAction, left-up aims, right-up posts the group info notice. */
UiResult MonsterListInput(const UiPointerEvent& event)
{
    if (event.action != UiPointerAction::Up || event.captured_buttons)
        PlayButtonSound(event.action);
    int row = -1;
    W8MonsterGroup* group = 0;
    int group_id = -1;

    if (event.action != UiPointerAction::Leave && !CurrentGame().activity.conversation &&
        TargetingMode() != W8_TARGET_NEED_PLACE && TargetingMode() != W8_TARGET_NEED_CONE &&
        TargetingMode() != W8_TARGET_NEED_ITEM &&
        CurrentLevelPopulation().monster_roster.hasActiveMonsters() && !IsWorldCursorVisible() &&
        !g_mouselook_active) {
        row = (event.local.y) / 0xb;
        group = GetLiveMonsterGroupAtIndex(row);
        if (group == 0) {
            row = -1;
        } else {
            group_id = group->group_id;
        }
    }
    SetCombatAction(group_id);
    if (event.button == UiPointerButton::Left && event.action == UiPointerAction::Down) {

        return UiResult::Handled;
    }
    if (event.button == UiPointerButton::Left && event.action == UiPointerAction::Up) {
        if ((event.captured_buttons & SDL_BUTTON_LMASK) != 0 &&
            CurrentGame().status.selected_character != -1 && group_id != -1 &&
            CurrentCombatSubjectIsValid()) {
            AimAtMonsterGroupMember(CurrentGame().status.selected_character, group);
        }
        return UiResult::Handled;
    }
    if (event.button == UiPointerButton::Right && event.action == UiPointerAction::Down) {

        return UiResult::Handled;
    }
    if (event.button == UiPointerButton::Right && event.action == UiPointerAction::Up) {
        if ((event.captured_buttons & SDL_BUTTON_RMASK) != 0 && group_id != -1 && !gpSCSV &&
            !CurrentGame().activity.conversation && !ItemUseDraft() && !IsLockInteractionActive() &&
            !IsTrapInteractionActive()) {
            ShowMonsterGroupInfoNotice(group_id);
        }
        return UiResult::Handled;
    }
    return row == -1 ? UiResult::Handled : UiResult::Pass;
}

/* Forget the whole combat selection - what is picked, what it is aimed at, and
   what is going to be done - and then re-derive who is acting. */
void ClearCombatSelection(void)
{
    SetCombatSelection(-1);
    SetCombatTarget(-1);
    SetCombatAction(-1);
    SetTargetCursor(GetTargetingCursorForState(false));
}

/* Refresh the target cursor as the mouse moves over the world view. A
   highlighted monster checks targetability and the mode flags; a picked prop
   checks whether its trigger takes the in-cursor item or shows a message;
   otherwise the cursor comes from the current targeting state. */
void UpdateWorldViewCursor(const UiPointerEvent& event, W8TargetNeed target_needed)
{
    const auto editing_context = GetEditedActionContext(CurrentGame().status.selected_character);
    const auto edited_action =
        CurrentGame().status.selected_character < 0
            ? W8Action{W8_ACTION_NONE, -1, {}, {}}
            : SelectCombatAction(CurrentGame().status.selected_character, editing_context);
    const bool sighting = editing_context == W8_TARGETING_CONTEXT_OUT_OF_COMBAT;

    int cursor = ShownCursor();
    if (cursor == W8_CURSOR_INVALID_TARGET) {
        return;
    }
    if (MainGameHud()->highlighted_item == -1) {
        if (TargetingMode() == W8_TARGET_NEED_NONE) {
            if (MainGameHud()->selected_item != -1) {
                if (!CurrentGame().advancement.held) {
                    SetTargetCursor(5);
                    return;
                }
                SetTargetCursor(cursor);
                return;
            }
            int prop_index = GetSelectedPropIndex();
            if (prop_index > -1) {
                Prop* prop = GetWorldProp(GetWorld(), prop_index);
                if (!CurrentGame().advancement.held) {
                    if (prop != 0) {
                        if (prop->TriggerRequiresItem() &&
                            (CurrentGame().party.held.item.iItemNo != -1)) {
                            SetTargetCursor(0x10);
                            return;
                        }
                        SetTargetCursor(prop->TriggerHasActionMessage() ? 13 : 5);
                        return;
                    }
                    SetTargetCursor(5);
                    return;
                }
                SetTargetCursor(cursor);
                return;
            }
        }
        cursor = 0;
    } else {
        FindMonster(MainGameHud()->highlighted_item);
        if (!AssessMonsterTarget(CurrentGame().status.selected_character,
                                 MainGameHud()->highlighted_item, edited_action, true, sighting)) {
            SetTargetCursor(cursor);
            return;
        }
        if (target_needed == W8_TARGET_NEED_NONE && !CurrentGame().combat.has_value() && !gpSCSV &&
            !ItemUseDraft() && !CurrentGame().activity.conversation && !IsLockInteractionActive() &&
            !IsTrapInteractionActive()) {
            SetTargetCursor(W8_CURSOR_VALID_TARGET);
            return;
        }
        cursor = 1;
    }
    SetTargetCursor(GetTargetingCursorForState(cursor));
}

/* Drop the highlight when the thing being highlighted is the one going away. */
void ClearHighlightIfItIs(const int* item)
{
    if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME && MainGameHud() != nullptr &&
        MainGameHud()->highlighted_item != -1 && *item == MainGameHud()->highlighted_item) {
        VideoRemoveToolTip();
    }
}

/* Whether the screen is in one of the states that takes the player's input
   away. The first flag settles it outright; otherwise one state only counts
   while a further check disagrees, and three more count on their own. */
bool IsScreenInputBlocked(void)
{
    if (gpSCSV) {
        return true;
    }
    if (CurrentGame().activity.conversation &&
        !(CurrentGame().activity.conversation && g_npc_interaction_state->scripted_dialogue)) {
        return true;
    }
    if (!IsLockInteractionActive() && !IsTrapInteractionActive() && !ItemUseDraft()) {
        return false;
    }
    return true;
}

/* Whether the screen is idle - none of the six overlays is up. The same six
   flags the input block reads, but all of them and unconditionally. */
bool IsScreenIdle(void)
{
    if (!CurrentGame().combat.has_value() && !gpSCSV && !ItemUseDraft() &&
        !CurrentGame().activity.conversation && !IsLockInteractionActive() &&
        !IsTrapInteractionActive()) {
        return true;
    }
    return false;
}

/* The automap key: tear down whichever dialogue overlay mode is active - the
   mode-3 NPC dialogue pane, the mode-5 spell panel or the mode-6
   portrait-hover overlay - reset the mode and transition to the automap
   screen. */
void OpenAutomapScreen(void)
{
    SetMainGameMode(W8_MAIN_GAME_DEFAULT);
    RequestScreen({.id = W8_SCREEN_AUTOMAP});
}

/* Load the level the party is on. With no level yet there is nothing to load
   and the answer is yes; otherwise the loading flag is up for the duration so
   whatever watches it knows. */
bool LoadCurrentLevelData(void)
{
    if (GetLoadedLevelID() != -1) {
        SetTargetCursor(W8_CURSOR_MAP_LOAD);
        g_world_cleanup_flag = true;
        UnloadSkyWorld();
        LeaveLevel();
        g_world_cleanup_flag = false;
        ResetPointerCursor();
    }
    return true;
}

/* Put the seven combat regions into their inactive mode, and the eighth with
   its whole set only when the screen says it is not needed. */
void DisableCombatRegions(void)
{
    SetTextBoxScrollInput(false);
    SetTextBoxChannelTabInput(false);
    if (MainGameHud()->action_panel_visible == 0) {
        SetTextBoxMuteInput(false);
        SetTextBoxEnabled(false);
    }
}

/* Drain the pending mouselook yaw/pitch into the camera, optionally scaling
   mid-range deltas by frame time when mouselook smoothing is enabled. */
static void ApplyPendingMouselook(void)
{
    uint64_t now;
    uint64_t elapsed;
    float scale;
    float pitch_step;
    float yaw_step;
    CameraOrientation orientation;

    if ((g_mouselook_tick_init & 1) == 0) {
        g_mouselook_tick_init = static_cast<unsigned char>(g_mouselook_tick_init | 1);
        g_mouselook_last_tick = MonotonicMilliseconds();
    }
    now = MonotonicMilliseconds();
    elapsed = now - g_mouselook_last_tick;
    g_mouselook_last_tick = now;
    scale = g_generator_jitter_fraction;
    if (elapsed > 9 && elapsed < 0x33) {
        scale = (elapsed / 10) * g_generator_jitter_fraction;
    } else if (elapsed > 9) {
        scale = 1.0f;
    }
    if (g_mouselook_pending_pitch == 0.0f && g_mouselook_pending_yaw == 0.0f) {
        return;
    }
    if (g_settings.mouselook_smoothing == 0) {
        pitch_step = g_mouselook_pending_pitch;
        yaw_step = g_mouselook_pending_yaw;
    } else {
        if (fabs(g_mouselook_pending_pitch) < g_mouselook_smooth_min ||
            fabs(g_mouselook_pending_pitch) > g_mouselook_smooth_max) {
            pitch_step = g_mouselook_pending_pitch;
        } else {
            pitch_step = g_mouselook_pending_pitch * scale;
        }
        if (fabs(g_mouselook_pending_yaw) < g_mouselook_smooth_min ||
            fabs(g_mouselook_pending_yaw) > g_mouselook_smooth_max) {
            yaw_step = g_mouselook_pending_yaw;
        } else {
            yaw_step = g_mouselook_pending_yaw * scale;
        }
    }
    orientation = cameraOrientation();
    orientation.yaw += yaw_step;
    orientation.pitch += pitch_step;
    g_mouselook_pending_pitch -= pitch_step;
    g_mouselook_pending_yaw -= yaw_step;
    setCameraOrientation(orientation);
}

/* After the tooltip delay elapses, clear any previous highlight slots (and
   redraw them), then park the pending subject on the slot its kind selects. */
void ApplyPendingTooltip(void)
{
    if (!MainGameHud()->tooltip_pending ||
        MonotonicMilliseconds() - MainGameHud()->tooltip_since < 0x33) {
        return;
    }
    const W8PortraitHighlight previous = std::exchange(MainGameHud()->highlight, {});
    const W8PortraitHighlight next =
        std::exchange(MainGameHud()->pending_highlight, W8PortraitHighlight{});
    MainGameHud()->tooltip_pending = false;
    if (previous.slot != -1 &&
        (!previous.IsFormation() || CurrentScreenState().id == W8_SCREEN_MAIN_GAME)) {
        RequestRedraw(1u << (previous.slot & 0x1f));
    }
    if (next.slot != -1) {
        RequestRedraw(1u << (next.slot & 0x1f));
        MainGameHud()->highlight = next;
    }
    if ((previous.slot != -1 && previous.IsFormation()) ||
        (next.slot != -1 && next.kind == W8_HIGHLIGHT_FORMATION_BOARD)) {
        RefreshFormationBoard();
    }
}

static void PositionSelectionToolTip(SDL_Point point)
{
    int y = point.y - g_cursor_image_height / 2;
    if (point.x < 0) {
        point.x = 2;
    }
    if (point.x + g_help_box_width + 2 > 0x27f) {
        point.x = 0x280 - (g_help_box_width + 2);
    }
    if (y < 0) {
        y = 2;
    }
    if (y + g_help_box_height + 2 > 0x1df) {
        y = 0x1e0 - (g_help_box_height + 2);
    }
    VideoPositionToolTip(point.x, y);
}

/* Re-picking the same monster while its tooltip clock is idle pops a
   name-plus-health tooltip at the cursor. A pick change drops the old
   monster's group or target highlight, and a valid new pick relights it in
   the colour CanTargetMonster just judged, so the group tint always agrees
   with the selection marker. */
void SetCombatSelection(int value)
{
    int current = MainGameHud()->highlighted_item;

    if (value == current) {
        if (current != -1 && !TimeUntilDeadline(MainGameHud()->countdown1) &&
            !HasScreenTransitionObjects()) {
            SDL_Point point;
            std::string text;
            std::string health;

            point = CursorPosition();
            W8MonsterInfo* monster_info = FindMonster(MainGameHud()->highlighted_item);
            if (monster_info != 0) {
                text = GetMonsterName(monster_info, 0, 0);
                text += " (";
                health = FormatMonsterHealth(monster_info);
                text += health;
                text += ")";
                VideoToolTip(text);
                PositionSelectionToolTip(point);
            }
        }
        return;
    }
    MainGameHud()->highlighted_item = -1;
    if (value != -1) {
        auto* info = FindMonster(value, false);
        if (info && info->p3D) {
            const auto bounds = info->p3D->GetWorldAnimationBounds();
            if (bounds && traceLineOfSightToBounds(cameraLocation(), *bounds))
                MainGameHud()->highlighted_item = value;
        }
    }
    RequestRedraw(W8_MAIN_REDRAW_MONSTER_LIST);
    VideoRemoveToolTip();
    unsigned int delay = g_settings.tooltip_delay_ms;
    if (delay > 500) {
        delay = 500;
    }
    MainGameHud()->countdown1 = MonotonicMilliseconds() + (delay);
}

/* The world-item twin of SetCombatSelection: a pick change drops the old
   item's highlight and lights a visible new one; re-picking the same item
   while its tooltip clock is idle pops its display name at the cursor. */
void SetCombatTarget(int value)
{
    int current = MainGameHud()->selected_item;

    if (value != current) {
        MainGameHud()->selected_item = -1;
        if (value == -1) {
            MainGameHud()->selected_item = -1;
        } else {
            W8WorldItem* item = ItemInfo(ItemIndex(value));
            if (item != 0) {
                if (const auto bounds = GetItemWorldBounds(item->p3D)) {
                    const glm::vec3 camera(cameraLocation());
                    if (traceLineOfSightToBounds(camera, *bounds) != 0) {
                        MainGameHud()->selected_item = value;
                    }
                }
            }
        }
        VideoRemoveToolTip();
        unsigned int delay = g_settings.tooltip_delay_ms;
        if (delay > 500) {
            delay = 500;
        }
        MainGameHud()->countdown1 = MonotonicMilliseconds() + (delay);
        return;
    }
    if (current != -1 && !TimeUntilDeadline(MainGameHud()->countdown1) &&
        !HasScreenTransitionObjects()) {
        SDL_Point point;
        W8WorldItem* item;

        point = CursorPosition();
        item = ItemInfo(ItemIndex(MainGameHud()->selected_item));
        if (item != 0) {
            auto text = FormatItemDisplayName(&item->item, true);

            VideoToolTip(text);
            PositionSelectionToolTip(point);
        }
    }
}

/* The group-targeting member of the selection family: a pick change clears
   the old group's tint, then either empties the selection or re-tints the new
   group - its lead member when the action picks monsters individually, the
   whole group when it wants the group. The cursor follows the result. */
void SetCombatAction(int value)
{
    if (MainGameHud()->action_group == value) return;
    MainGameHud()->action_group = value;
    SetTargetCursor(GetTargetingCursorForState(CurrentCombatSubjectIsValid()));
    RequestRedraw(W8_MAIN_REDRAW_MONSTER_LIST);
}

/* With no modal or NPC dialogue up, draw the bottom prompt strip: the catalog
   backdrop plus the "choose action" line - the combat variant while combat
   mode is on. */
static void DrawMainGamePrompt(void)
{
    W8ControlsRect bounds;
    W8TextBuffer* buffer;

    if (g_modal_owner == 0 && !CurrentGame().activity.conversation) {
        CatalogArtwork::Load(0xa9, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0xb1, 0x13f);
        bounds.left = 0xb1;
        bounds.top = 0x13f;
        bounds.right = 0x1cf;
        bounds.bottom = 0x153;
        buffer = new W8TextBuffer(&bounds, {}, g_wiz_text_font_secondary,
                                  g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter, 4);
        buffer->RenderString(Data().strings[CurrentGame().combat.has_value() ? 0x780 : 0x77f],
                             g_wiz_text_font_secondary, false, GetFrameBufferSurface());
        delete buffer;
    }
}

/* Raise or drop the radar map panel and restore the viewport mode when the
   raise latch changes. */
void SetRadarMapVisible(bool visible)
{
    const bool was_visible = MainGameHud()->radar_map_visible;
    if (visible) {
        MainGameHud()->radar_map_visible = visible;
        MainGameHud()->radar_map_alternate = 0;
        MainGame()->radar.setEnabled(true);
        MainGame()->radar_target->setEnabled(true);
        EnableRadarMap(true);
        if (CurrentGame().combat.has_value()) {
            ZoomRadarMapIn();
        } else {
            ZoomRadarMapOut();
        }
        RequestRedraw(W8_MAIN_REDRAW_LAYOUT | W8_MAIN_REDRAW_PORTRAIT_PANEL);
    } else {
        MainGameHud()->radar_map_visible = 0;
        MainGame()->radar_target->setEnabled(false);
        MainGame()->radar.setEnabled(false);
        EnableRadarMap(false);
        ReleaseRadarMap();
        RequestRedraw(W8_MAIN_REDRAW_LAYOUT | W8_MAIN_REDRAW_PORTRAIT_PANEL);
    }
    if (visible != was_visible) {
        SetViewportMode(GetMainGameViewportMode());
    }
}

/* Raise or drop the combat action panel and restore the viewport mode when the
   raise latch changes. */
void SetActionPanelVisible(bool visible)
{
    const bool was_visible = MainGameHud()->action_panel_visible;
    if (visible) {
        MainGameHud()->action_panel_visible = visible;
        EnableActionPanelRegions();
        RequestRedraw(W8_MAIN_REDRAW_LAYOUT | W8_MAIN_REDRAW_PORTRAIT_PANEL);
    } else {
        MainGameHud()->action_panel_visible = 0;
        DisableCombatRegions();
        RequestRedraw(W8_MAIN_REDRAW_LAYOUT | W8_MAIN_REDRAW_PORTRAIT_PANEL);
    }
    if (visible != was_visible) {
        SetViewportMode(GetMainGameViewportMode());
    }
}

/* Raise or drop the formation board and restore the viewport mode when the
   raise latch changes. */
void SetFormationBoardVisible(bool visible)
{
    const bool was_visible = MainGameHud()->formation_board_visible;
    if (visible) {
        MainGameHud()->formation_board_visible = true;
        MainGameHud()->formation_board_alternate = 0;
        MainGame()->formation.setEnabled(true);
        RefreshFormationBoard();
    } else {
        MainGameHud()->formation_board_visible = false;
        MainGame()->formation.setEnabled(false);
        ReleaseFormationBoard();
    }
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT | W8_MAIN_REDRAW_PORTRAIT_PANEL);
    if (visible != was_visible) {
        SetViewportMode(GetMainGameViewportMode());
    }
}

/* Hand one frame to whichever overlays are up. Each is independent, so more
   than one can take the same frame. */
void UpdateScreenOverlays(int frame)
{
    if (IsLockInteractionActive()) {
        EndLockInteractMode(frame);
    }
    if (IsTrapInteractionActive()) {
        EndTrapInteractMode(frame);
    }
    if (gpSCSV) {
        CloseSpellCastingView();
    }
    if (ItemUseDraft()) {
        CloseUseItemSelectView();
    }
    if (FormationPanelOpen()) {
        CloseFormationPanel();
    }
}

/* Whether the numbered action-key command may run in the current UI mode:
   dialogue, interaction, camp and surprise lock most of them out, the item
   and spell views admit only their own toggle, and combat commands check the
   entry's submenu state. Command 0x10 re-checks the slot's queued action's
   own command. */
bool IsMGSActionKeyEnabled(unsigned short command)
{
    short state;

    state = -1;
    if (CurrentGame().activity.conversation) {
        if (command != W8_MGS_ACTION_JOURNAL || IsNpcDialogueCursorActive() != 0 ||
            (CurrentGame().activity.conversation && g_npc_interaction_state->scripted_dialogue)) {
            return false;
        }
    }
    if (IsLockInteractionActive() || IsTrapInteractionActive() || FormationPanelOpen()) {
        return false;
    }
    if (ItemUseDraft() && command != W8_MGS_ACTION_USE_ITEM_VIEW) {
        return false;
    }
    if (gpSCSV && command != W8_MGS_ACTION_SPELL_VIEW) {
        return false;
    }
    if (CurrentGame().surprise_possible || CurrentGame().activity.conversation_aside) {
        return false;
    }
    if (CurrentGame().combat.has_value() && !CurrentGame().combat->round_active) {
        return false;
    }
    switch (command) {
    case W8_MGS_ACTION_USE_ITEM_VIEW:
        state = GetSubMenuEntryState(W8_SUBMENU_ITEMS, 1, CurrentGame().status.selected_character);
        break;
    case W8_MGS_ACTION_USE_RECORDED_ITEM:
        state = GetSubMenuEntryState(W8_SUBMENU_ITEMS, 2, CurrentGame().status.selected_character);
        break;
    case W8_MGS_ACTION_SPELL_VIEW:
        state = GetSubMenuEntryState(W8_SUBMENU_SPELLS, 0, CurrentGame().status.selected_character);
        break;
    case W8_MGS_ACTION_CAST_RECORDED_SPELL:
        state = GetSubMenuEntryState(W8_SUBMENU_SPELLS, 1, CurrentGame().status.selected_character);
        break;
    default:
        if (!CurrentGame().combat.has_value()) {
            return command == W8_MGS_ACTION_JOURNAL;
        }
        switch (command) {
        case W8_MGS_ACTION_JOURNAL:
            return false;
        case W8_MGS_ACTION_BREATHE:
            state =
                GetSubMenuEntryState(W8_SUBMENU_ATTACK, 2, CurrentGame().status.selected_character);
            break;
        case W8_MGS_ACTION_BREATH_ATTACK:
            return IsPartySlotEligible(CurrentGame().status.selected_character) &&
                   CanPartySlotReBreathe(CurrentGame().status.selected_character);
        case W8_MGS_ACTION_ATTACK:
            state =
                GetSubMenuEntryState(W8_SUBMENU_ATTACK, 0, CurrentGame().status.selected_character);
            break;
        case W8_MGS_ACTION_BERSERK:
            state =
                GetSubMenuEntryState(W8_SUBMENU_ATTACK, 1, CurrentGame().status.selected_character);
            break;
        case W8_MGS_ACTION_TURN_UNDEAD:
            state =
                GetSubMenuEntryState(W8_SUBMENU_ATTACK, 3, CurrentGame().status.selected_character);
            break;
        case W8_MGS_ACTION_PRAY:
            state =
                GetSubMenuEntryState(W8_SUBMENU_ATTACK, 4, CurrentGame().status.selected_character);
            break;
        case W8_MGS_ACTION_DEFEND:
            state =
                GetSubMenuEntryState(W8_SUBMENU_DEFEND, 0, CurrentGame().status.selected_character);
            break;
        case W8_MGS_ACTION_PROTECT:
            state =
                GetSubMenuEntryState(W8_SUBMENU_DEFEND, 1, CurrentGame().status.selected_character);
            break;
        case W8_MGS_ACTION_EQUIP:
            state =
                GetSubMenuEntryState(W8_SUBMENU_ITEMS, 0, CurrentGame().status.selected_character);
            break;
        case W8_MGS_ACTION_WALK:
        case W8_MGS_ACTION_RUN:
            return AnyCharacterEngaged();
        case W8_MGS_ACTION_REPEAT:
            switch (CurrentGame()
                        .party.rows[CurrentGame().status.selected_character]
                        .remembered.repeat_action) {
            case W8_ACTION_BREATHE:
                return IsMGSActionKeyEnabled(W8_MGS_ACTION_BREATH_ATTACK);
            case W8_ACTION_TURN_UNDEAD:
                return IsMGSActionKeyEnabled(W8_MGS_ACTION_TURN_UNDEAD);
            case W8_ACTION_PRAY:
                return IsMGSActionKeyEnabled(W8_MGS_ACTION_PRAY);
            case W8_ACTION_CAST_SPELL:
                return IsMGSActionKeyEnabled(W8_MGS_ACTION_CAST_RECORDED_SPELL);
            case W8_ACTION_USE_ITEM:
                return IsMGSActionKeyEnabled(W8_MGS_ACTION_USE_RECORDED_ITEM);
            case W8_ACTION_EQUIP:
                return IsMGSActionKeyEnabled(W8_MGS_ACTION_EQUIP);
            default:
                return false;
            }
        default:
            Fail("FALSE", "Error with handling main game action keyboard equivalent");
            break;
        }
    }
    if (state == -1) {
        Fail("iActionState != -1", "Error with handling main game action keyboard equivalent");
    }
    if (!IsPartySlotEligible(CurrentGame().status.selected_character)) {
        return false;
    }
    return state == 0 || state == 1;
}

/* Run the numbered action-key command: dismiss the transient menu first, then
   act - toggle the item or spell view, fire the recorded spell/item, choose
   the combat action, or dispatch the slot's queued action's own command. */
void RunMGSActionKey(unsigned short command)
{
    if (MainGameHud()->open_submenu) {
        DestroySubMenuControls();
    }
    if (MainGameHud()->keyboard_menu_open) {
        CloseKeyboardMenu();
    }
    switch (command) {
    case W8_MGS_ACTION_JOURNAL:
        if (CurrentGame().activity.conversation) {
            CurrentGame().activity.conversation_aside = true;
        }
        RequestScreen({.id = W8_SCREEN_JOURNAL});
        break;
    case W8_MGS_ACTION_USE_ITEM_VIEW:
        if (ItemUseDraft()) {
            CloseUseItemSelectView();
        } else {
            OpenUseItemSelectView(CurrentGame().status.selected_character);
        }
        break;
    case W8_MGS_ACTION_USE_RECORDED_ITEM:
        StartCharacterItemUse(CurrentGame().status.selected_character);
        break;
    case W8_MGS_ACTION_SPELL_VIEW:
        if (gpSCSV) {
            CloseSpellCastingView();
        } else {
            OpenSpellCastingView(CurrentGame().status.selected_character);
        }
        break;
    case W8_MGS_ACTION_CAST_RECORDED_SPELL:
        StartCharacterSpellCast(CurrentGame().status.selected_character, 0);
        break;
    case W8_MGS_ACTION_BREATHE:
        ChooseAction(CurrentGame().status.selected_character, W8_ACTION_BREATHE, -1, 0, false, 1);
        DrawSubMenuCharacterAction();
        break;
    case W8_MGS_ACTION_BREATH_ATTACK:
        StartCharacterBreathAttack(CurrentGame().status.selected_character);
        break;
    case W8_MGS_ACTION_ATTACK:
        ChooseAction(CurrentGame().status.selected_character, W8_ACTION_ATTACK, -1, 0, false, 1);
        DrawSubMenuCharacterAction();
        break;
    case W8_MGS_ACTION_BERSERK:
        ChooseAction(CurrentGame().status.selected_character, W8_ACTION_BERSERK, -1, 0, false, 1);
        DrawSubMenuCharacterAction();
        break;
    case W8_MGS_ACTION_TURN_UNDEAD:
        ChooseAction(CurrentGame().status.selected_character, W8_ACTION_TURN_UNDEAD, -1, 0, false,
                     1);
        DrawSubMenuCharacterAction();
        break;
    case W8_MGS_ACTION_PRAY:
        ChooseAction(CurrentGame().status.selected_character, W8_ACTION_PRAY, -1, 0, false, 1);
        DrawSubMenuCharacterAction();
        break;
    case W8_MGS_ACTION_DEFEND:
        ChooseAction(CurrentGame().status.selected_character, W8_ACTION_DEFEND, -1, 0, false, 1);
        DrawSubMenuCharacterAction();
        break;
    case W8_MGS_ACTION_PROTECT:
        ChooseAction(CurrentGame().status.selected_character, W8_ACTION_PROTECT, -1, 0, false, 1);
        DrawSubMenuCharacterAction();
        break;
    case W8_MGS_ACTION_EQUIP:
        ChooseAction(CurrentGame().status.selected_character, W8_ACTION_EQUIP, -1, 0, false, 1);
        DrawSubMenuCharacterAction();
        break;
    case W8_MGS_ACTION_WALK:
        ChooseAction(CurrentGame().status.selected_character, W8_ACTION_WALK, -1, 0, false, 1);
        DrawSubMenuCharacterAction();
        break;
    case W8_MGS_ACTION_RUN:
        ChooseAction(CurrentGame().status.selected_character, W8_ACTION_RUN, -1, 0, false, 1);
        DrawSubMenuCharacterAction();
        break;
    case W8_MGS_ACTION_REPEAT:
        switch (CurrentGame()
                    .party.rows[CurrentGame().status.selected_character]
                    .remembered.repeat_action) {
        case W8_ACTION_BREATHE:
            RunMGSActionKey(W8_MGS_ACTION_BREATH_ATTACK);
            break;
        case W8_ACTION_TURN_UNDEAD:
            RunMGSActionKey(W8_MGS_ACTION_TURN_UNDEAD);
            break;
        case W8_ACTION_PRAY:
            RunMGSActionKey(W8_MGS_ACTION_PRAY);
            break;
        case W8_ACTION_CAST_SPELL:
            RunMGSActionKey(W8_MGS_ACTION_CAST_RECORDED_SPELL);
            break;
        case W8_ACTION_USE_ITEM:
            RunMGSActionKey(W8_MGS_ACTION_USE_RECORDED_ITEM);
            break;
        case W8_ACTION_EQUIP:
            RunMGSActionKey(W8_MGS_ACTION_EQUIP);
            break;
        default:
            break;
        }
        break;
    default:
        Fail("FALSE", "Error executing main game screen action");
        break;
    }
}

/* Fire the numbered action-key command when the current UI mode allows it. */
void TryMGSActionKey(W8MGSAction command)
{
    if (IsMGSActionKeyEnabled(command)) {
        RunMGSActionKey(command);
    }
}

/* Load each main-game cursor catalog entry into an Texture and record the
   ETRLE frame size onto the slot. Called once when the HUD state is first
   allocated. */
void LoadMainGameCursorResources(void)
{
    W8MainGameResourceSlot* slot = g_main_game_resource_slots;
    W8MainGameResourceSlot* end = g_main_game_resource_slots + 17;

    for (; slot < end; ++slot) {
        const auto artwork = CatalogArtwork::Load(slot->image_id);
        const auto sprites = artwork.spriteSheet();
        short frame = artwork.imageOffset();
        if (frame < static_cast<int>(sprites->frames.size())) {
            slot->size_x = sprites->frames[frame].width;
            slot->size_y = sprites->frames[frame].height;
        }
        slot->object =
            VideoVObjectToTextureAnim(*sprites, static_cast<unsigned short>(frame),
                                      static_cast<unsigned short>(slot->frame_count), true);
        slot->object->playback->animation_mode = W8_TEXTURE_ANIM_MANUAL;
    }
}

/* Input routed here while mouselook is latched: the left button arms the
   look flag, and each mouse move accumulates yaw/pitch from the offset off
   the 320x240 anchor before recentering the cursor. Smoothing defers the
   apply to the frame tick; otherwise it applies immediately. */
bool HandleMouselookInput(const InputEvent* input)
{
    if (!g_camera_path_active &&
        (!CurrentGame().advancement.paused || CurrentGame().combat.has_value())) {
        const InputEventType us_event = input->type;
        switch (us_event) {
        case InputEventType::LeftButtonDown:
            g_mouselook_left_held = true;
            return true;
        case InputEventType::LeftButtonUp:
            g_mouselook_left_held = false;
            return true;
        case InputEventType::MouseMove:
            g_mouselook_pending_yaw =
                (input->position.x - 0x140) * g_mouselook_yaw_scale + g_mouselook_pending_yaw;
            g_mouselook_pending_pitch =
                g_mouselook_pending_pitch + (input->position.y - 0xf0) *
                                                (g_settings.invert_mouse_y != 0 ? -1 : 1) *
                                                g_mouselook_pitch_scale;
            WarpSystemCursor(0x140, 0xf0);
            if (g_settings.mouselook_smoothing == 0) {
                ApplyPendingMouselook();
            }
            return true;
        default:
            break;
        }
    }
    return false;
}

void PresentWorldAdvancement(bool held, bool hold_changed)
{
    if (held && hold_changed && PartyMovementPanelShown()) DisablePartyMovementRegions();
    if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME && MainGameHud()) {
        if (!held && !MainGameHud()->review_transition_active) {
            if (PartyMovementPanelShown()) UpdatePartyMovementPanel();
            EraseRegion(0xb1, 0x13f, 0x1cf, 0x153);
        }
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_PANEL);
    }
}

void ToggleMainGamePause()
{
    auto& advancement = CurrentGame().advancement;
    if (advancement.paused || !InteractionBlocksWorldAdvancement()) {
        advancement.paused = !advancement.paused;
        SetWorldResumeInput(advancement.paused);
    }
}

/* Open the monster information dialog over the main game screen for the
   highlighted monster: only while the entry is live, not dying and still has
   hit points. Closing requests a full screen repaint. */
void OpenMonsterInfoDialog(int location_id)
{
    W8MonsterInfo* monster_info = FindMonster(location_id);
    if (!monster_info->fActive || monster_info->p3D->IsDying() || monster_info->hp_current == 0) {
        return;
    }
    W8MonsterInfoDialog* dialog = new W8MonsterInfoDialog(location_id);
    dialog->SetText("");
    dialog->on_close = [] { RequestRedraw(W8_MAIN_REDRAW_ALL); };
    OpenModal(std::unique_ptr<ModalContent>(dialog));
}

/* Open the assay dialog over the main game screen. A live modal dialog is
   parked in the pending slot so it resumes when this one closes. */
void OpenAssayDialog(const W8ItemInstance& item, int character_slot)
{
    W8DialogBase* dialog;

    if (g_modal_owner != 0) {
        g_pending_main_game_dialog = std::move(g_modal_owner);
    }
    if (character_slot != -1) {
        dialog = new W8AssayDialog(item, &CurrentGame().party.characters[character_slot],
                                   {kInfoDialogX, 0x48});
    } else {
        dialog = new W8AssayDialog(item, 0, {kInfoDialogX, 0x48});
    }
    dialog->SetText("");

    dialog->on_close = [] { RequestRedraw(W8_MAIN_REDRAW_ALL); };
    OpenModal(std::unique_ptr<ModalContent>(dialog));
}

/* Queue `party_slot` for the pending screen and unwind whichever main-game
   mode is live - dialogue, review screen or the mode-6 highlight overlay -
   before handing off. The flag decides whether the pending payload carries
   the slot's character pointer. */
void OpenCharacterScreenForPartySlot(unsigned int party_slot, bool flag)
{
    if (CurrentGame().activity.conversation) {
        CloseNpcDialogueForCamp();
    }
    ScreenRequest request;
    request.character = &CurrentGame().party.characters[party_slot];
    request.parameter_4 = flag ? request.character : nullptr;
    SetMainGameMode(W8_MAIN_GAME_DEFAULT);
    CurrentGame().activity.place = Activity::Place::Camp;
    request.id = W8_SCREEN_CAMP;
    RequestScreen(std::move(request));
    UpdateScreenOverlays(1);
    SetPrimarySurfaceTextureHint2Enabled(false);
}

/* The viewport mode the screen falls back to after a raised overlay drops:
   full-3d while every overlay flag is clear and no main-game mode override is
   set, the plain mode when any of the board, radar or combat latches is still
   down, and otherwise the override field's own mapping. */
W8ViewportMode GetMainGameViewportMode(void)
{
    if (!gpSCSV &&
        (!CurrentGame().activity.conversation ||
         (CurrentGame().activity.conversation && g_npc_interaction_state->scripted_dialogue)) &&
        !IsLockInteractionActive() && !IsTrapInteractionActive() && !ItemUseDraft() &&
        MainGameHud()->action_panel_visible == 0 && MainGameHud()->formation_board_visible &&
        MainGameHud()->radar_map_visible != 0 &&
        GetEffectiveMainUiMode() == W8_MAIN_UI_MODE_PORTRAITS) {
        return W8_VIEWPORT_MODE_CENTERED_FULL_HEIGHT;
    }
    if (!gpSCSV &&
        (!CurrentGame().activity.conversation ||
         (CurrentGame().activity.conversation && g_npc_interaction_state->scripted_dialogue)) &&
        !IsLockInteractionActive() && !IsTrapInteractionActive() && !ItemUseDraft() &&
        (!MainGameHud()->formation_board_visible || MainGameHud()->radar_map_visible == 0 ||
         MainGameHud()->action_panel_visible == 0)) {
        return W8_VIEWPORT_MODE_FULL;
    }
    switch (g_settings.main_ui_mode) {
    case W8_MAIN_UI_MODE_RADAR:
        return W8_VIEWPORT_MODE_FULL;
    case W8_MAIN_UI_MODE_FORMATION:
        return W8_VIEWPORT_MODE_FULL_WITH_BOTTOM_PANEL;
    case W8_MAIN_UI_MODE_PORTRAITS:
    default:
        return W8_VIEWPORT_MODE_CENTERED_WITH_BOTTOM_PANEL;
    }
}

/* Drop whichever of the formation board, the radar map and the combat bar is
   up, restoring the viewport mode that was in effect when each was raised. */
void CloseMainGameOverlays(void)
{
    if (MainGameHud()->formation_board_visible) {
        SetFormationBoardVisible(false);
    }
    if (MainGameHud()->radar_map_visible != 0) {
        SetRadarMapVisible(false);
    }
    if (MainGameHud()->action_panel_visible != 0) {
        SetActionPanelVisible(false);
    }
}

void ClearHighlightOverlayRegion(void)
{
    if (g_main_game_mode == W8_MAIN_GAME_HIGHLIGHT_OVERLAY) {
        ClearSurfaceRect(MainGameHud()->dialogue_x, MainGameHud()->dialogue_y,
                         MainGameHud()->dialogue_x + MainGameHud()->dialogue_width,
                         MainGameHud()->dialogue_y + MainGameHud()->dialogue_height);
        InvalidateRegion(MainGameHud()->dialogue_x, MainGameHud()->dialogue_y,
                         MainGameHud()->dialogue_x + MainGameHud()->dialogue_width,
                         MainGameHud()->dialogue_y + MainGameHud()->dialogue_height);
        if (MainGameHud()->dialogue_y <
            static_cast<unsigned int>(
                g_viewport_modes[static_cast<unsigned int>(MainGameHud()->camera_mode)].top)) {
            RequestRedrawCombatBar();
        }
        if (MainGameHud()->dialogue_y + MainGameHud()->dialogue_height > 0x166) {
            RequestRedraw(W8_MAIN_REDRAW_TEXT_BOX);
        }
    }
}

/* Remove the transcript entry that owns the selected visible line, then
   redraw the controller. */
void W8NpcDialogueTextController::SaveTranscriptEntries()
{
    ClearNpcDialogueTranscript();
    for (int index = 0; index < static_cast<int>(text_area.m_all_lines.size()); ++index) {
        auto record = std::make_unique<W8DialogueTranscriptRecord>();
        W8DialogTextEntry* entry = text_area.GetEntry(index);
        record->text = entry->GetText();
        record->category = entry->m_category;
        g_npc_interaction_state->dialogue_transcript.push_back(std::move(record));
    }
}

void W8NpcDialogueTextController::ClearTranscriptEntries()
{
    while (!text_area.m_all_lines.empty()) {
        text_area.RemoveEntry(0);
    }
    InvalidateAll();
}

void W8NpcDialogueTextController::RemoveSelectedTranscriptEntry()
{
    if (text_area.m_highlighted_entry != -1) {
        int index = text_area.GetOwningEntryIndex(text_area.m_highlighted_entry);
        if (index != -1) {
            text_area.RemoveEntry(index);
            InvalidateAll();
        }
    }
}

int W8NpcDialogueTextController::GetSelectedTranscriptEntryIndex()
{
    return text_area.m_highlighted_entry;
}

void W8NpcDialogueTextController::SetTranscriptSorted(bool sorted)
{
    text_area.SetSorted(sorted);
    InvalidateAll();
}

/* The trade confirm button's activation callback. Selling mode first offers
   the selected stack outright when the NPC's wanted list matches it; the
   per-mode cases then move stock between the NPC list and the backpack or
   party pool, and every exit repaints the purse and price readouts. */
void ConfirmNpcTradeItem(void)
{
    auto& state = *g_npc_interaction_state;
    if (!state.trade_selection) return;
    const auto selection = *state.trade_selection;
    const int row = MainGameHud()->selected_text_lines[2];
    if (state.trade_mode == W8_NPC_TRADE_GIVE) {
        if (selection.source != W8TradeSource::Gold &&
            !ResolveTradeSelection(state.dialogue_npc, selection))
            return;
        if (selection.source == W8TradeSource::Backpack) {
            const int owner = CurrentGame().party.rows[selection.character].npc_index;
            auto* npc = owner == -1 ? nullptr : GetNpcState(owner);
            auto* item = ResolveTradeSelection(state.dialogue_npc, selection);
            if (npc && item && NpcWantsItem(npc, item)) {
                QueueCharacterEvent(&CurrentGame().party.characters[selection.character], 109, 0,
                                    kCharacterEventFullVolume);
                return;
            }
        }
        HandleNpcDialogueItemChoice();
    } else if (state.trade_mode == W8_NPC_TRADE_SHOPLIFT) {
        if (auto* item = ResolveTradeSelection(state.dialogue_npc, selection))
            AttemptNpcItemTrade(item, static_cast<unsigned char>(state.trade_quantity),
                                selection.slot);
    } else if (state.trade_mode == W8_NPC_TRADE_BUY || state.trade_mode == W8_NPC_TRADE_SELL) {
        if (state.trade_quantity <= 0 || state.trade_quantity > 255) return;
        const auto result = CompleteNpcTrade(state.dialogue_npc, selection,
                                             static_cast<unsigned char>(state.trade_quantity));
        switch (result.outcome) {
        case W8TradeOutcome::Complete:
            if (result.dropped_overflow) DisplayNpcQuote(Data().strings[0x6b1], false);
            SortNpcItems(state.dialogue_npc);
            RebuildNpcTradeItemList(false);
            SetSelectedTextLine(row, 2);
            UpdateNpcTradeSelection(row, 0, 1);
            if (result.accepted_topic) g_npc_dialogue.QueueQuote(0x15, false, false, false);
            break;
        case W8TradeOutcome::Refused:
            g_npc_dialogue.QueueQuote(0x11, false, false, false);
            break;
        case W8TradeOutcome::InsufficientGold:
            g_npc_dialogue.QueueQuote(0x14, false, false, false);
            break;
        case W8TradeOutcome::WantedByOwner:
            QueueCharacterEvent(&CurrentGame().party.characters[selection.character], 109, 0,
                                kCharacterEventFullVolume);
            break;
        case W8TradeOutcome::InvalidSelection:
            RebuildNpcTradeItemList(false);
            break;
        }
    }
    RefreshNpcTradePartyGold();
    RefreshNpcTradePrice();
}

void ShowNpcTradeItemNotice(W8ItemInstance* item)
{
    W8NpcTradeMode mode = g_npc_interaction_state->trade_mode;
    unsigned int font_palette = 0xf;
    unsigned int price;
    W8TradePriceKind price_kind = W8_TRADE_PRICE_PARTY_SELLS;

    if (mode == W8_NPC_TRADE_BUY || mode == W8_NPC_TRADE_SHOPLIFT) {
        price_kind = W8_TRADE_PRICE_PARTY_BUYS;
    }
    if (mode == W8_NPC_TRADE_BUY || mode == W8_NPC_TRADE_SHOPLIFT || mode == W8_NPC_TRADE_SELL) {
        unsigned char stack_count;
        if (Data().items[item->iItemNo].equip_class == W8_ITEM_EQUIP_CLASS_AMMUNITION) {
            stack_count = item->stack_count;
        } else {
            stack_count = 1;
        }
        price = CalculateNpcTradeStackPrice(g_npc_interaction_state->dialogue_npc, item->iItemNo,
                                            price_kind, stack_count, item->identified);
    } else {
        price = GetItemStackValue(item);
    }
    if (CurrentGame().party.gold < price &&
        g_npc_interaction_state->trade_mode != W8_NPC_TRADE_SHOPLIFT) {
        font_palette = 0;
    }
    mode = g_npc_interaction_state->trade_mode;
    if (mode != W8_NPC_TRADE_SELL && mode != W8_NPC_TRADE_BUY && mode != W8_NPC_TRADE_SHOPLIFT) {
        ShowNotice(font_palette, FormatItemDisplayName(item, true), 2);
        return;
    }
    MainGameHud()->text_paint_scratch = fmt::format("{:d}{}", price, Data().strings[0x797]);
    ShowNotice(font_palette, FormatItemDisplayName(item, true), 2,
               (MainGameHud()->text_box_right - MainGameHud()->text_box_left) -
                   (*MainGameHud()->text_box_font).textWidth(MainGameHud()->text_paint_scratch));
    AppendTextBoxLine(MainGameHud()->text_paint_scratch, 2);
}

void RebuildNpcTradeItemList(bool scroll_to_top)
{
    g_npc_interaction_state->trade_rows.clear();
    g_npc_interaction_state->trade_selection.reset();
    ResetEditorStatusLine(2);
    ResetNpcDialogueItemEditor();
    EnableNpcTradeFilterButtons();
    if (scroll_to_top) {
        MainGameHud()->text_lines[2] = 0;
    }
    if (g_npc_interaction_state->trade_mode == W8_NPC_TRADE_BUY ||
        g_npc_interaction_state->trade_mode == W8_NPC_TRADE_SHOPLIFT) {
        unsigned int shown = 0;
        const auto count = GetNpcItemCount(g_npc_interaction_state->dialogue_npc);
        for (int index = 0; index < static_cast<int>(count); ++index) {
            W8NpcItemEntry* entry = GetNpcItemAt(g_npc_interaction_state->dialogue_npc, index);
            ++shown;
            if (shown > 0x15e) {
                break;
            }
            if (!NpcTradeItemAllowed(&entry->item) && entry->available_at == 0) {
                g_npc_interaction_state->trade_rows.push_back(
                    {W8TradeSource::NpcStock, -1, index, entry->item, entry->quantity});
                ShowNpcTradeItemNotice(&entry->item);
            }
        }
    } else {
        PopulateNpcTradeList();
    }
    if (scroll_to_top) {
        ScrollTextBoxTo(0);
    }
}

static void ShowNpcPlayerTradeItem(W8ItemInstance* item, bool acceptable, unsigned int font_palette)
{
    if (g_npc_interaction_state->trade_mode == W8_NPC_TRADE_SELL) {
        unsigned char count =
            Data().items[item->iItemNo].equip_class == W8_ITEM_EQUIP_CLASS_AMMUNITION
                ? item->stack_count
                : 1;
        int price =
            CalculateNpcTradeStackPrice(g_npc_interaction_state->dialogue_npc, item->iItemNo,
                                        W8_TRADE_PRICE_PARTY_SELLS, count, item->identified);
        if (acceptable) {
            MainGameHud()->text_paint_scratch = fmt::format("{:d}{}", price, Data().strings[0x797]);
        } else {
            MainGameHud()->text_paint_scratch = "---";
        }
        ShowNotice(
            font_palette, FormatItemDisplayName(item, true), 2,
            (MainGameHud()->text_box_right - MainGameHud()->text_box_left) -
                (*MainGameHud()->text_box_font).textWidth(MainGameHud()->text_paint_scratch));
        AppendTextBoxLine(MainGameHud()->text_paint_scratch, 2);
    } else {
        ShowNotice(font_palette, FormatItemDisplayName(item, true), 2);
    }
}

void PopulateNpcTradeList(void)
{
    g_npc_interaction_state->trade_rows.clear();
    g_npc_interaction_state->trade_selection.reset();
    W8Character* trading = &CurrentGame().party.characters[CurrentGame().status.selected_character];
    unsigned int shown = 0;

    ResetEditorStatusLine(2);
    ResetNpcDialogueItemEditor();
    EnableNpcTradeFilterButtons();
    if (g_npc_interaction_state->trade_mode == W8_NPC_TRADE_GIVE) {
        MainGameHud()->text_paint_scratch =
            fmt::format("{:d}{}", CurrentGame().party.gold, Data().strings[0x797]);
        ShowNotice(
            W8_FONT_PALETTE_TEXT_BOX, Data().strings[0x72d], 2,
            (MainGameHud()->text_box_right - MainGameHud()->text_box_left) -
                (*MainGameHud()->text_box_font).textWidth(MainGameHud()->text_paint_scratch));
        AppendTextBoxLine(MainGameHud()->text_paint_scratch, 2);
        g_npc_interaction_state->trade_rows.push_back({W8TradeSource::Gold});
    }
    if (!g_npc_interaction_state->trade_pc_items) {
        for (unsigned int index = 0; index < CurrentGame().party.item_count; ++index) {
            W8ItemInstance* item = &CurrentGame().party.item_pool[index];
            bool acceptable = true;
            unsigned int font_palette;
            if (item->iItemNo == -1) {
                continue;
            }
            if (!NpcAcceptsTradeItem(g_npc_interaction_state->dialogue_npc, item) &&
                g_npc_interaction_state->trade_mode != W8_NPC_TRADE_GIVE) {
                font_palette = 0;
                acceptable = false;
            } else {
                font_palette = 0xf;
            }
            if (NpcTradeItemAllowed(item)) {
                continue;
            }
            ++shown;
            if (shown > 0x15d) {
                break;
            }
            g_npc_interaction_state->trade_rows.push_back(
                {W8TradeSource::PartyPool, -1, static_cast<int>(index), *item});
            ShowNpcPlayerTradeItem(item, acceptable, font_palette);
        }
    } else {
        for (auto& index : trading->backpack) {
            W8ItemInstance* item = &index;
            bool acceptable = true;
            unsigned int font_palette;
            if (item->iItemNo == -1) {
                continue;
            }
            ++shown;
            if (shown > 0x15e) {
                break;
            }
            if (NpcTradeItemAllowed(item)) {
                continue;
            }
            if (!NpcAcceptsTradeItem(g_npc_interaction_state->dialogue_npc, item) &&
                g_npc_interaction_state->trade_mode != W8_NPC_TRADE_GIVE) {
                font_palette = 0;
                acceptable = false;
            } else {
                font_palette = 0xf;
            }
            g_npc_interaction_state->trade_rows.push_back(
                {W8TradeSource::Backpack, CurrentGame().status.selected_character,
                 static_cast<int>(item - trading->backpack), *item});
            ShowNpcPlayerTradeItem(item, acceptable, font_palette);
        }
    }
    g_npc_interaction_state->ui->panels[1].dirty = true;
}

void OpenNpcTradeQuantityDialog(void)
{
    W8SplitItemDialog* dialog;

    if (g_npc_interaction_state->trade_item == 0 ||
        g_npc_interaction_state->trade_item->stack_count < 2) {
        return;
    }
    if (g_npc_interaction_state->trade_mode == W8_NPC_TRADE_SELL) {
        dialog = new W8SplitItemDialog(W8_ITEM_SPLIT_SELL, g_npc_interaction_state->trade_item,
                                       g_npc_interaction_state->trade_quantity,
                                       {kSplitDialogX, kSplitDialogY});
    } else if (g_npc_interaction_state->trade_mode == W8_NPC_TRADE_BUY ||
               g_npc_interaction_state->trade_mode == W8_NPC_TRADE_SHOPLIFT) {
        dialog = new W8SplitItemDialog(W8_ITEM_SPLIT_BUY, g_npc_interaction_state->trade_item,
                                       g_npc_interaction_state->trade_quantity,
                                       {kSplitDialogX, kSplitDialogY});
    } else {
        dialog = new W8SplitItemDialog(W8_ITEM_SPLIT_INVENTORY, g_npc_interaction_state->trade_item,
                                       g_npc_interaction_state->trade_quantity,
                                       {kSplitDialogX, kSplitDialogY});
    }
    dialog->SetText("");

    dialog->on_complete = NpcTradeSplitDialogResult;
    OpenModal(std::unique_ptr<ModalContent>(dialog));
}

void SelectNpcBuyMode(void)
{
    g_npc_interaction_state->trade_mode = W8_NPC_TRADE_BUY;
    UpdateNpcDialogueSubMode();
}

void RestockNpcTradeStock(void)
{
    RestockNpcInventory(g_npc_interaction_state->dialogue_npc);
}

void NpcTradeSplitDialogResult(int count)
{
    if (count != 0) {
        int slot = MainGameHud()->selected_text_lines[2];
        g_npc_interaction_state->trade_quantity = count - 1;
        SetSelectedTextLine(slot, 2);
        UpdateNpcTradeSelection(slot, 0, 1);
    }
}

bool AttemptNpcItemTrade(W8ItemInstance* item, unsigned char quantity, int index)
{
    std::string text;
    char result;
    W8Character* character =
        &CurrentGame().party.characters[CurrentGame().status.selected_character];

    result = AttemptNpcItemTheft(character, g_npc_interaction_state->dialogue_npc, item->iItemNo,
                                 quantity);
    if (result == W8_ITEM_THEFT_SUCCEEDED) {
        text = fmt::sprintf(Data().strings[0x74d], character->name, GetItemDisplayName(item));
        DisplayNpcQuote(text, true);
        if (Data().items[item->iItemNo].identify_difficulty != 0 && quantity == 1) {
            AddNpcTopic(g_npc_interaction_state->dialogue_npc, item->iItemNo);
        }
        const auto purchase =
            CompleteNpcItemPurchase(g_npc_interaction_state->dialogue_npc, index, quantity, true);
        if (purchase.outcome != W8TradeOutcome::Complete) return false;
        if (purchase.dropped_overflow) DisplayNpcQuote(Data().strings[0x6b1], false);
        RebuildNpcTradeItemList(false);
        return true;
    }
    if (result == W8_ITEM_THEFT_FAILED) {
        text = fmt::sprintf(Data().strings[0x74f], character->name);
        DisplayNpcQuote(text, false);
        return false;
    }
    if (result == W8_ITEM_THEFT_CAUGHT) {
        g_npc_dialogue.QueueQuote(0x17, false, false, false);
        SetNpcDispositionBand(g_npc_interaction_state->dialogue_npc, W8_NPC_BAND_NEUTRAL);
        ApplyFactionChange(3, g_npc_interaction_state->dialogue_npc->record->faction, 1, -5);
        CloseNpcDialogueOptionLayout();
        ShowNpcDialogueTopicMenu();
        return false;
    }
    return false;
}

/* Highlight a clicked transcript keyword; clicking it again submits it to
   the dialogue input handler and clears its marked-entry palette. */

void W8NpcDialogueTextController::SelectTranscriptKeywordAtPoint(int x, int y)
{
    unsigned int hit;

    if (g_npc_interaction_state->modal_dialog_open) {
        return;
    }

    hit = text_area.HitTestEntry(x, y);
    if (hit == static_cast<unsigned int>(-1)) {
        return;
    }
    const auto keyword = text_area.GetVisibleEntryText(hit);
    if (!keyword) return;

    if (text_area.m_highlighted_entry == static_cast<int>(hit)) {
        text_area.ClearEntryHighlight();
        text_area.HighlightVisibleEntry(static_cast<int>(hit));
        SetDialogueFieldKeyword(std::string(*keyword), false);
        HandleNpcDialogueInput();
        text_area.SetEntryMarked(text_area.GetOwningEntryIndex(static_cast<int>(hit)), false);
        InvalidateLayout();
        return;
    }

    text_area.ClearEntryHighlight();
    text_area.HighlightVisibleEntry(static_cast<int>(hit));
    SetDialogueFieldKeyword(std::string(*keyword), false);
    InvalidateLayout();
}

bool HandleWorldResumeInput(const InputEvent* event)
{
    const InputEventType us_event = event->type;

    if (IsLeftButtonEvent(us_event) || us_event == InputEventType::RightButtonDown) {
        if (us_event == InputEventType::RightButtonDown ||
            us_event == InputEventType::LeftButtonDown ||
            us_event == InputEventType::LeftButtonUp) {
            goto resume_world;
        }
    } else {
        if (us_event == InputEventType::RightButtonUp) {
            goto resume_world;
        }
        if (us_event == InputEventType::MouseMove) {
            return true;
        }
    }

    if (g_mgs_keyboard.FindCommandForEvent(event) == W8_MGS_COMMAND_NONE) {
        return false;
    }

resume_world:
    if (CurrentGame().surprise_possible) {
        AcknowledgeSurprise();
    }
    if (!CurrentGame().advancement.paused) {
        return true;
    }
    CurrentGame().advancement.paused = false;
    SetWorldResumeInput(false);
    return true;
}

/* Note that the party's state changed. The combat half is only asked for while
   a fight is on; the party half always. */

static std::uint64_t g_surprise_fade_tick_base;
const float g_fade_resume_scale = -500.0f;
static bool g_surprise_fade_in;
static std::shared_ptr<SDL_Surface> g_surprise_snapshot_surface;
static std::unique_ptr<OverlayQuad> g_surprise_snapshot_overlay;
static std::unique_ptr<OverlayQuad> g_surprise_fade_node;

void ResetMainGameMode(void)
{
    SetMainGameMode(W8_MAIN_GAME_DEFAULT);
    if (CurrentGame().combat.has_value()) {
        CurrentGame().endCombat(true);
    } else if (AnyCharacterActive() && !CurrentGame().party_moving) {
        AutoSaveIfAllowed(true);
    }
    if (CurrentGame().surprise_possible) {
        RestoreSurpriseView();
    }
    CurrentGame().status.game_started = false;
    ClearHeldItemDisplay();
    ReturnToPreviousScreen();
    SetPrimarySurfaceTextureHint2Enabled(false);
}

void CreateSurpriseFade(void)
{
    DrawShader shader;
    glm::vec4 color;

    SetFullscreenSceneLast(0);
    color = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    g_surprise_fade_node = makeColorOverlay(0x280, 0x1e0, color, true);
    positionOverlay(g_surprise_fade_node.get(), 0, 0, false);
    shader = g_surprise_fade_node->shader;
    shader.value = (shader.value & ~0x6040) | 0xa0;
    g_surprise_fade_node->shader = shader;
    g_surprise_fade_node->color.a = 0.0;
    g_surprise_fade_tick_base = MonotonicMilliseconds();
    g_surprise_fade_in = true;
}

void ReverseSurpriseFade(void)
{
    if (SurpriseFadingIn()) {
        OverlayQuad* material = g_surprise_fade_node.get();
        float opacity = material->color.a;

        std::uint64_t now = MonotonicMilliseconds();
        g_surprise_fade_in = !g_surprise_fade_in;
        if (g_surprise_fade_in) {
            g_surprise_fade_tick_base =
                now + static_cast<int>((1.0f - opacity) * g_fade_resume_scale);
        } else {
            g_surprise_fade_tick_base = now + static_cast<int>(opacity * g_fade_resume_scale);
        }
    } else {
        g_surprise_fade_node->visible = true;
        g_surprise_fade_tick_base = MonotonicMilliseconds();
        g_surprise_fade_in = true;
    }
}

void DestroySurpriseFade(void)
{
    g_surprise_snapshot_overlay.reset();
    g_surprise_snapshot_surface.reset();
    g_surprise_fade_node.reset();
    SetFullscreenSceneLast(1);
    g_world_render_enabled = 1;
}

unsigned char UpdateSurpriseFade(void)
{
    bool done = false;
    bool boundary = false;
    float opacity;
    std::uint64_t now = MonotonicMilliseconds();

    if (g_surprise_fade_tick_base + 500 < now) {
        if (!g_surprise_fade_in) {
            opacity = 0.0f;
            done = true;
        } else {
            boundary = true;
            opacity = 1.0f;
            g_surprise_fade_in = false;
            g_surprise_fade_tick_base = now;
        }
    } else {
        opacity = (now - g_surprise_fade_tick_base) * 0.002f;
        if (!g_surprise_fade_in) {
            opacity = 1.0f - opacity;
        }
    }

    g_surprise_fade_node->color.a = opacity;

    if (boundary) {
        if (SurpriseFadingIn()) {
            const auto snapshot = CatalogArtwork::Load(0x1e0).bitmap();
            g_surprise_snapshot_surface = snapshot;
            g_surprise_snapshot_overlay = makeOverlayFromTexture(
                std::make_shared<Texture>(g_surprise_snapshot_surface), 1, 1, false, false);
            g_compositor.attach(*g_surprise_snapshot_overlay, OverlayLayer::Fullscreen);
            g_world_render_enabled = 0;
            g_surprise_fade_tick_base = MonotonicMilliseconds();
        } else {
            g_surprise_snapshot_overlay.reset();
            g_surprise_snapshot_surface.reset();
            g_world_render_enabled = 1;
        }
        return done;
    }
    if (done) {
        if (SurpriseFadingIn()) {
            g_surprise_fade_node->visible = false;

            return done;
        }
        g_surprise_fade_node.reset();
        SetFullscreenSceneLast(1);
    }
    return done;
}

W8TargetingContext GetEditedActionContext(int party_slot)
{
    if (party_slot == CurrentGame().status.selected_character && (gpSCSV || ItemUseDraft())) {
        return W8_TARGETING_CONTEXT_SHARED;
    }
    if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME && MainGameHud() != nullptr &&
        MainGameHud()->selection_kind != W8_ACTION_NONE) {
        if (MainGameHud()->selection_kind == W8_ACTION_CAST_SPELL &&
            MainGameHud()->selection_settled) {
            return W8_TARGETING_CONTEXT_SPELL;
        }
        if (MainGameHud()->selection_kind == W8_ACTION_USE_ITEM &&
            MainGameHud()->selection_settled) {
            return W8_TARGETING_CONTEXT_ITEM;
        }
        return W8_TARGETING_CONTEXT_DIALOGUE;
    }
    return CurrentGame().combat.has_value() ? W8_TARGETING_CONTEXT_IN_COMBAT
                                            : W8_TARGETING_CONTEXT_OUT_OF_COMBAT;
}

// The interaction controller evaluates a choice, commits its gameplay state,
// and then refreshes the presentation of that committed choice.
void AimAtTarget(int actor, const W8CombatSlot* target, W8TargetingContext context)
{
    const auto action = SelectCombatAction(actor, context);
    const auto chosen = *target; // The caller may borrow one of the remembered blocks.
    if (action.target != chosen) {

        CommitActionTarget(actor, context, chosen,
                           context == W8_TARGETING_CONTEXT_IN_COMBAT &&
                               CurrentGame().combat.has_value());
        if (context != W8_TARGETING_CONTEXT_OUT_OF_COMBAT) {

            RequestPartySlotRedraw(actor);
            if (actor == CurrentGame().status.selected_character) {
                RequestRedrawParty();
                RequestRedraw(W8_MAIN_REDRAW_CHARACTER_ACTION);
            }
            if (auto& combat = CurrentGame().combat) combat->target_picked = false;
        }
    }
    if (chosen.iType != W8_TARGET_KIND_NONE && context != W8_TARGETING_CONTEXT_OUT_OF_COMBAT &&
        actor == CurrentGame().status.selected_character) {
        const auto editing = GetEditedActionContext(actor);
        const auto edited_action = SelectCombatAction(actor, editing);
        const auto needed = editing == W8_TARGETING_CONTEXT_OUT_OF_COMBAT
                                ? W8_TARGET_NEED_NONE
                                : GetTargetNeededForAction(CurrentGame().party, edited_action);
        SetTargetingMode(TargetMatchesNeeded(&edited_action.target, needed) ? W8_TARGET_NEED_NONE
                                                                            : needed);
    }
}

namespace {
W8TargetNeed targeting_mode = W8_TARGET_NEED_NONE;
}

W8TargetNeed TargetingMode()
{
    return targeting_mode;
}

void SetTargetingMode(W8TargetNeed state)
{
    int cursor;

    targeting_mode = state;
    switch (state) {
    case W8_TARGET_NEED_ALLY:
    case W8_TARGET_NEED_ITEM:
    case W8_TARGET_NEED_CHARACTER_INDIRECT:
        cursor = 3;
        break;
    case W8_TARGET_NEED_ENEMY:
        cursor = 0;
        break;
    case W8_TARGET_NEED_PLACE:
        cursor = 2;
        break;
    case W8_TARGET_NEED_CONE:
        cursor = 12;
        break;
    case W8_TARGET_NEED_GROUP:
        cursor = 11;
        break;
    default:
        cursor = W8_CURSOR_NONE;
        break;
    }
    if (cursor != ShownCursor()) {
        SetTargetCursor(cursor);
    }
    RequestRefreshPartyState();
    SetTargetConeEnabled(state == W8_TARGET_NEED_CONE);
}

void ConfigureSpellTargetFilter(W8SpellTargetType target_type, W8TargetNeed needed_kind)
{
    W8CombatSlot target;
    int cursor = -1;
    int selected = CurrentGame().status.selected_character;

    switch (target_type) {
    case W8_TARGET_TYPE_CASTER:
        ResetCombatSlot(&target);
        target.iType = W8_TARGET_KIND_CHARACTER;
        target.iChar = selected;
        break;
    case W8_TARGET_TYPE_PARTY:
        ResetCombatSlot(&target);
        target.iType = W8_TARGET_KIND_PARTY;
        break;
    case W8_TARGET_TYPE_ENEMY:
        if (CurrentGame().activity.conversation_aside) {
            ResetCombatSlot(&target);
            target.iType = W8_TARGET_KIND_MONSTER;
            target.iMonsterID = g_npc_interaction_state->target_location_id;
            AimAtTarget(selected, &target, GetEditedActionContext(selected));
            needed_kind = W8_TARGET_NEED_NONE;
        } else if (g_settings.autotarget_spells != 0 &&
                   RepickActionTarget(selected, W8_TARGETING_CONTEXT_SHARED, 0)) {
            W8TargetSource source;
            SetTargetSourceToCharacter(selected, &source);
            PointCameraAtCombatTarget(&source,
                                      &CurrentGame().party.rows[selected].committed.target);
        }
        goto aim_done;
    case W8_TARGET_TYPE_ALL_ENEMIES:
        ResetCombatSlot(&target);
        target.iType = W8_TARGET_KIND_ALL_ENEMIES;
        break;
    case W8_TARGET_TYPE_LOCK_OR_TRAP:
        ResetCombatSlot(&target);
        target.iType = W8_TARGET_KIND_EIGHT;
        break;
    default:
        ResetCombatSlot(&target);
        break;
    }
    AimAtTarget(selected, &target, GetEditedActionContext(selected));
aim_done:
    targeting_mode = needed_kind;
    switch (needed_kind) {
    case W8_TARGET_NEED_ALLY:
    case W8_TARGET_NEED_ITEM:
    case W8_TARGET_NEED_CHARACTER_INDIRECT:
        cursor = 3;
        break;
    case W8_TARGET_NEED_ENEMY:
        cursor = 0;
        break;
    case W8_TARGET_NEED_PLACE:
        cursor = 2;
        break;
    case W8_TARGET_NEED_CONE:
        cursor = 0xc;
        break;
    case W8_TARGET_NEED_GROUP:
        cursor = 0xb;
        break;
    default:
        break;
    }
    if (cursor != ShownCursor()) {
        SetTargetCursor(cursor);
    }
    RequestRefreshPartyState();
    SetTargetConeEnabled(needed_kind == W8_TARGET_NEED_CONE);
}

void ClearSlotTargeting(int party_slot)
{

    targeting_mode = W8_TARGET_NEED_NONE;
    if (ShownCursor() != W8_CURSOR_NONE) {
        SetTargetCursor(W8_CURSOR_NONE);
    }
    RequestRefreshPartyState();
    SetTargetConeEnabled(false);
}

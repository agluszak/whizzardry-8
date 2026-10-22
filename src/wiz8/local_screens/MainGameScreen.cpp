#include "wiz8/local_screens/MainGameScreen.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <chrono>
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
#include "wiz8/combat/attack.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/fight.h"
#include "wiz8/combat/range.h"
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
#include "wiz8/local_screens/npc_dialogue_panel.h"
#include "wiz8/modal_content.h"
#include "wiz8/monster_generators.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/npc_interaction_state.h"
#include "wiz8/npc_items.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/platform/input.h"
#include "wiz8/platform/timer.h"
#include "wiz8/pointer.h"
#include "wiz8/render/compositor.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/texture.h"
#include "wiz8/rules/activity.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/item_rules.h"
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
void InvalidatePortraitPanel(void);

static void DrawMainGamePrompt(void);
void InvalidateLockInteractionPanels(void);
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
        event->DelayDispatch(std::chrono::milliseconds{600});
    }
}

// The action bar's text box regions follow the bottom panel.
static void SetActionPanelRegions(bool enabled)
{
    SetTextBoxEnabled(enabled);
    SetTextBoxMuteInput(enabled);
    SetTextBoxScrollInput(enabled);
    SetTextBoxChannelTabInput(enabled);
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
           ScreenPointer().targeting() == W8_TARGET_NEED_CONE ||
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
    MainGame()->refreshLayout();
    SelectTextBox(2);
    ResetEditorStatusLine(-1);
    ResetLevelDataVectors();
    if (auto* hud = MainGameHud()) {
        hud->repaint.roof_and_spell_icons = true;
        hud->repaint.submenu_buttons = true;
    }
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
void EndLockInteractMode(signed char suspend)
{
    MainGame()->lock_active = false;
    if (suspend != 0) {
        LockInteraction()->EnablePanels(0);
    } else {
        MainGame()->lock_interaction.reset();
        ClearLevelMovementStopped();
    }
    SelectTextBox(0);
    MainGame()->refreshLayout();
    if (auto* hud = MainGameHud()) {
        hud->repaint.layout = true;
        hud->repaint.roof_and_spell_icons = true;
        hud->repaint.submenu_buttons = true;
    }
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
   info text; run from the update phase when the layout is repainted. */
void InvalidateLockInteractionPanels(void)
{
    W8LockInteraction* interaction = LockInteraction();

    interaction->m_tumbler_panel->InvalidateAll();
    interaction->m_info_panel->RefreshInfo();
    interaction->actions_dirty = true;
}

/* The trap interaction's counterpart: its text and action panels, and the
   status texts. */
static void InvalidateTrapInteractionPanels()
{
    W8TrapInteraction* interaction = TrapInteraction();

    interaction->m_text_panel->InvalidateAll();
    interaction->m_status_panel->RefreshStatusTexts();
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

W8LockInteraction::W8LockInteraction(Trigger* trigger) : m_timer(1.0f, TimerClock::Presentation)
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
void W8LockInteraction::ApplyKnockKnock(int level, int /*flag*/, signed char backfire)
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

signed char W8MainGameTextKeyHandler::HandleKey(SDL_Keycode key)
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
    : m_selection(0), m_screen(0), m_values(0), m_progress_display(0),
      m_timer1(0.04f, TimerClock::Presentation)
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
    MainGame()->refreshLayout();
    SelectTextBox(2);
    ResetEditorStatusLine(-1);
    ResetLevelDataVectors();
    if (auto* hud = MainGameHud()) {
        hud->repaint.roof_and_spell_icons = true;
        hud->repaint.submenu_buttons = true;
    }
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
void EndTrapInteractMode(signed char suspend)
{
    MainGame()->trap_active = false;
    if (suspend != 0) {
        TrapInteraction()->EnablePanelInput(false);
    } else {
        MainGame()->trap_interaction.reset();
        ClearLevelMovementStopped();
    }
    SelectTextBox(0);
    MainGame()->refreshLayout();
    if (auto* hud = MainGameHud()) {
        hud->repaint.layout = true;
        hud->repaint.roof_and_spell_icons = true;
        hud->repaint.submenu_buttons = true;
    }
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

void W8NpcDialogueTextController::AddTranscriptEntry(std::string_view text,
                                                     W8DialogueCategory category, bool marked)
{
    const int added = text_area.AddEntry({}, text, 0, 7, static_cast<unsigned char>(category));
    InvalidateLayout();
    if (marked) {
        text_area.SetEntryMarked(added, true);
    }
    if (text_area.m_all_lines.size() > 1) {
        for (int index = 0; index < static_cast<int>(text_area.m_all_lines.size()); ++index) {
            if (CompareTextIgnoreAsciiCase(text_area.GetEntryText(index).value(),
                                           W8_NO_KEYWORDS_PLACEHOLDER) == 0) {
                text_area.RemoveEntry(index);
                InvalidateAll();
                break;
            }
        }
    }
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
        if (auto* hud = MainGameHud()) {
            hud->repaint.portraits.set(1);
            hud->repaint.portraits.set(3);
            hud->repaint.portraits.set(5);
            hud->repaint.portraits.set(7);
        }
    } else if (top < 0xbc) {
        if (auto* hud = MainGameHud()) {
            hud->repaint.portraits.set(3);
            hud->repaint.portraits.set(5);
            hud->repaint.portraits.set(7);
        }
    } else if (top < 0x111) {
        if (auto* hud = MainGameHud()) {
            hud->repaint.portraits.set(5);
            hud->repaint.portraits.set(7);
        }
    } else {
        if (auto* hud = MainGameHud()) hud->repaint.portraits.set(7);
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
    RepaintNpcPanel(3);
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
        if (auto* hud = MainGameHud()) {
            hud->repaint.portraits.set(1);
            hud->repaint.portraits.set(3);
            hud->repaint.portraits.set(5);
            hud->repaint.portraits.set(7);
        }
        return;
    }
    if (top < 0xbc) {
        if (auto* hud = MainGameHud()) {
            hud->repaint.portraits.set(3);
            hud->repaint.portraits.set(5);
            hud->repaint.portraits.set(7);
        }
        return;
    }
    if (top < 0x111) {
        if (auto* hud = MainGameHud()) {
            hud->repaint.portraits.set(5);
            hud->repaint.portraits.set(7);
        }
        return;
    }
    if (auto* hud = MainGameHud()) hud->repaint.portraits.set(7);
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

void W8NpcDialogueTextController::ShowTranscriptEntries()
{
    const auto& notebook = CurrentGame().story.keywords;
    showing_notebook = true;
    for (const auto& record : notebook) AddTranscriptEntry(record.text, record.category, false);
    InvalidateAll();
    if (notebook.empty()) {
        CurrentGame().npc_interaction.dialogue_category_filter = W8_DIALOGUE_CATEGORY_ALL;
        SetTranscriptCategoryFilter(CurrentGame().npc_interaction.dialogue_category_filter);
        SyncDialogueCategoryButtons();
        AddTranscriptEntry(W8_NO_KEYWORDS_PLACEHOLDER, W8_DIALOGUE_CATEGORY_ALL, false);
    }
}

void RefreshFlaggedMainGameState(void)
{
    if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME && MainGameHud() != nullptr &&
        MainGameHud()->keyboard_menu_open) {
        RefreshKeyboardMenuRows();
    }
}

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

void SetWorldResumeInput(bool enabled)
{
    if (MainGame()) MainGame()->resume_world = enabled;
}

MainGameScreen::MainGameScreen(Game& game, Pointer& pointer) : game(game), pointer(pointer)
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
    W8Difficulty difficulty = game.status.difficulty;

    /* A main game belongs to one game: it is built when that game first shows its world
       and discarded before another game replaces it. It faces the party as saved, starts
       the world running and the gameplay timer afresh, and readies the portraits. */
    if (!hud) {
        hud = std::make_unique<W8LevelRuntimeBlock>();
        TurnPartyToImmediate(game.status.party_facing, 0);
        game.advancement.held = false;
        game.advancement.paused = false;
        game.aging_held = false;
        game.autosave_due = Game::kAutosaveInterval;
        ResetAssayTab();
        ResetTargetingState();
        LoadMainGameCursorResources();
    }
    game.level_up_check_due = true;
    CreatePartyPortraitEventRegions();

    InitTextBoxRegions();
    ConfigureMainGameHudButtons();
    CreateLevelButtons();
    CreateConditionButtons();
    InitializeMainGameLevelBlock(*hud);
    // The renderer starts each entry without a viewport.
    applied_layout.viewport = W8_VIEWPORT_MODE_UNINITIALIZED;
    refreshLayout();
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
    DrainInputEventQueue();
    pointer.reset();
    SetTargetingMode(W8_TARGET_NEED_NONE);
    SetUiCanvasAlphaTestEnabled(true);
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
            game.aging_held = false;
        }
    }
    if (!game.combat.has_value()) {
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
        MainGameHud()->highlight_graphic.reset();
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
    ApplyPendingMouselook();
    ApplyPendingTooltip();
    if (g_modal_owner) {
        // A modal takes the pointer back from mouselook and from any targeting cursor.
        CancelMouselook();
        pointer.reset();
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
    if (!AnyCharacterActive() || game.party_moving) BeginPartyDeath();
    if (dismissed_npc_slot != -1) UpdateNpcPartyMember(std::exchange(dismissed_npc_slot, -1));
    if (std::exchange(resume_conversation, false)) SyncDialogueNpcStateAndMarkPending();
    if (SkillIncreasesShownNow(game)) FlushDeferredSkillNotices(game);
    SyncPartyPortraitVitalsBars();
    if (g_modal_owner && ProcessModalOwner()) hud->repaint.portrait_panel = true;
    SetWorldResumeInput(game.advancement.paused && !InteractionBlocksWorldAdvancement());
    DrainNpcDialogueDeferralInput();
    FlushInputWhileWorldCursorGate();
    TickPartyPortraitFx();
    if (game.combat.has_value()) {
        UpdateCombatPortraitStatus();
    }
    game.status.world_suspended = false;
    if (MainGameHud()->keyboard_menu_open || MainGameHud()->combat_slot != -1) {
        UpdateKeyboardMenu();
    }
    if (game.combat.has_value()) {
        for (int slot = 0; slot < 8; ++slot) {
            if (!game.party.members[slot].has_value() ||
                game.party.characters[slot].highest_condition > W8_CONDITION_UNCONSCIOUS ||
                (MainGameHud()->keyboard_menu_open && MainGameHud()->combat_slot == slot)) {
                MainGame()->combat_targets[slot]->setEnabled(false);
            } else {
                MainGame()->combat_targets[slot]->setEnabled(true);
            }
        }
    }
    if (MainGameHud()->portrait_flash != 0 && TimeUntilDeadline(MainGameHud()->clock) == 0) {
        MainGameHud()->portrait_flash = 0;
        RequestPartySlotRedraw(game.status.selected_character);
    }
    if (game.status.text_box_lines_shown[game.status.text_line_cursor] <
        game.status.text_box_lines_used[game.status.text_line_cursor]) {
        AdvanceNoticeLine(game.status.text_line_cursor);
    }
    if (IsScreenTransitionPending()) {
        // A pending transition holds the world until another screen covers it.
        step_timer.restart();
        DrawMainGameScreen(*hud, UpdateMainGamePresentation(*hud));
        return;
    }
    SDL_Point point;
    point = CursorPosition();
    if (!IsWorldCursorVisible()) {
        if (!g_modal_owner) {
            if ((!ShouldDeferCharacterEventForNpcScript(true) || !game.activity.conversation) &&
                !game.status.world_cursor_gate)
                UpdateUiPointer(point.x, point.y);
        } else {
            for (int slot = 0; slot < 8; ++slot) {
                if (game.party.members[slot].has_value() &&
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
    HandleManualCameraHotkeys();
    if (!g_camera_path_active && g_mouselook_active && g_mouselook_left_held) {
        MainGameHud()->camera_motion_flags |= W8_CAMERA_MOTION_FORWARD;
        if (GetInputState().modifiers & SHIFT_DOWN) {
            MainGameHud()->camera_motion_flags |= W8_CAMERA_MOTION_FAST;
        }
    }
    if (!IsScreenTransitionPending()) {

        updatePartyMovement(*GetWorld(), MainGameHud()->camera_motion_flags);

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
        if (!IsWorldCursorVisible() && !g_mouselook_active) pointer.animate();
        game.advance(step_timer.lap(), {.world = {.world = InteractionBlocksWorldAdvancement(),
                                                  .party = LockInteraction() || TrapInteraction()},
                                        .combat = gpSCSV || ItemUseDraft()});
        AnimatePartyPortraits();
        if (game.combat.has_value() && MainGameHud()->refresh_combat_panel &&
            !TimeUntilDeadline(MainGameHud()->combat_panel_timer)) {
            RefreshMonsterTargetCounts();
            MainGameHud()->combat_panel_timer = MonotonicMilliseconds() + (500);
            MainGameHud()->refresh_combat_panel = 0;
        }
        if (gpSCSV) CommitSpellCastingSelection();
        if (ItemUseDraft()) CommitSelectedItemUse();
        if (game.activity.conversation) ServiceNpcDialogue();
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
        if (game.level_up_check_due && !game.activity.conversation && !game.combat.has_value()) {
            RefreshLevelUpReadyNotices();
        }
        if (game.status.selected_character == -1) {
            int next = GetNextCharacter(1, 1, -1);
            assert(next != -1);
            SelectPartyCharacter(next);
        }
    }
    DrawMainGameScreen(*hud, UpdateMainGamePresentation(*hud));
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
        if (game.combat.has_value()) {
            game.endCombat(true);
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
    if (game.activity.conversation) EndNpcDialogueSession(false);
    if (MainGameHud()->keyboard_menu_open) CloseKeyboardMenu();
    ReleasePortraitControls();
    ReleaseConditionButtons();
    if (PartyMovementPanelShown()) DisablePartyMovementRegions();
    RestoreCurrentNpcQuoteBubble();
    MoveTimer(W8_MOVE_TIMER_SUSPEND);
    // The world stays covered, and held, until this screen steps it again.
    ApplyWorldAdvancement(game, {.covered = true});

    if (leaving) {
        for (index = 0; index < 17; ++index) {
            g_main_game_resource_slots[index].object.reset();
        }
    }

    MainGameHud()->held_item_display = -1;
    pointer.reset();

    closeOverlays();

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

/* Every part, as a full repaint draws them. */
MainGameRepaint RepaintOfEverything()
{
    MainGameRepaint all;
    all.everything = true;
    all.portraits.set();
    all.roof_and_spell_icons = all.layout = all.radar = all.text_box = all.submenu_buttons =
        all.submenu_panel = all.formation = all.portrait_panel = all.monster_list =
            all.options_button = all.combat_stance = all.combat_progress = all.character_action =
                all.combat_effects = true;
    return all;
}

/* The party slots whose portrait bands a draw repainted, for the widgets drawn over them. */
struct RepaintedPortraitBands {
    std::bitset<8> any;
    std::bitset<8> complete;
    std::bitset<8> side_strips;

    void add(int slot, PortraitBandPaint painted)
    {
        any.set(slot);
        complete[slot] = painted.band;
        side_strips[slot] = painted.side_strip;
    }
};

/* Repaint what the HUD's record holds: clear the primary surface for a full repaint,
   refresh the chrome panels the current UI mode owns, and repaint the recorded party
   slots. Dependencies have already been resolved by the presentation update. */
static void ApplyMainGameRepaint(const W8LevelRuntimeBlock& hud,
                                 const W8TargetingPresentation& presentation,
                                 RepaintedPortraitBands& bands)
{
    const auto& repaint = hud.repaint;
    const auto& layout = MainGame()->layout();
    const auto publish_chrome = [&layout](int left, int top, int right, int bottom) {
        InvalidateRegion(left, top + layout.PortraitYShift(), right,
                         bottom + layout.PortraitYShift());
    };
    int party_slot;
    int text_box_variant;
    W8TextBoxMode text_box_mode;
    bool show_portraits;
    SDL_Rect combat_clip;
    short health_percent;

    if (repaint.everything) ClearPrimarySurface();
    if (repaint.portrait_panel && !CurrentGame().combat) InvalidatePortraitPanel();
    if (repaint.combat_effects) RefreshCombatEffectHud();
    if (repaint.portrait_panel) {
        const auto bounds = MainGame()->world_view->bounds();
        InvalidateRegion(bounds.x, bounds.y, bounds.x + bounds.w + 1, bounds.y + bounds.h + 1);
        if (CurrentGame().advancement.held) DrawMainGamePrompt();
    }
    if (repaint.monster_list) {
        RedrawCombatMonsterList(presentation.subject_valid);
    }
    if (repaint.roof_and_spell_icons) {
        RedrawRoofButtons();
        RefreshSpellIconHudRows();
        InvalidateRegion(0, 0, 640, 17);
    }
    if (repaint.combat_progress && CurrentGame().combat.has_value() &&
        CurrentGame().combat->round_active) {
        health_percent = GetCombatActionProgress(0);
        combat_clip = {0, 0, health_percent * 0x11e / 100 + 0xb1, 0x1e0};
        ScopedClipRect clipped(GetFrameBufferSurface(), combat_clip);
        CatalogArtwork::Load(0x8d, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0xb1, 2);
    }
    if (FormationPanelOpen()) {
        if ((!repaint.portrait_panel) || hud.condition_orb_party_slot != -1 ||
            hud.enchantment_orb_party_slot != -1 || hud.portrait_overlay_party_slot != -1) {
            show_portraits = false;
        } else {
            show_portraits = true;
        }
        RefreshFormationPanel(show_portraits || repaint.formation_panel);
    }
    for (party_slot = 0; party_slot < 8; ++party_slot) {
        const bool dirty = repaint.portraits.test(party_slot);
        if (dirty) {
            bands.add(party_slot, RedrawPartyPortraitOverlay(
                                      party_slot,
                                      hud.highlight.LightsPortrait(party_slot) ||
                                          party_slot == hud.held_item_display,
                                      true, hud.portrait_refresh_pending[party_slot] == 0));
            auto rect = PartyPortraitBounds(party_slot, hud.portrait_refresh_image[party_slot] -
                                                            layout.PortraitHoverXOrigin());
            if (hud.portrait_refresh_pending[party_slot]) {
                auto backing = PartyPortraitBounds(party_slot);
                --backing.right;
                --backing.bottom;
                rect = UnionScreenRects(rect, backing);
            }
            InvalidateRegion(rect.left, rect.top, rect.right, rect.bottom);
        } else if (repaint.portrait_bars.test(party_slot)) {
            RedrawPartyPortraitBars(party_slot, hud.portrait_refresh_pending[party_slot] == 0);
        }
        if (CurrentGame().monster_manager_entries[party_slot].keyboard_menu_open)
            RedrawKeyboardMenuPanel(dirty || repaint.keyboard_menu);
    }
    if (repaint.portrait_plate) PaintPortraitPlate(hud);
    if (repaint.layout) {
        publish_chrome(0, 358, 640, 450);
        RedrawLayoutArrowButtons();
        CatalogArtwork::Load(0x7e, 0).drawAndInvalidate(GetFrameBufferSurface(), 1, 0x269, 0x166);
        InvalidateRegion(0x269, 0x166, 0x27f, 0x1c1);
    }
    if (gpSCSV) {
        PaintSpellCastingPanels(repaint.mode_panels);
        goto finish_mode_overlays;
    }
    if (ItemUseDraft()) {
        PaintUseItemPanels(repaint.mode_panels);
        goto finish_mode_overlays;
    }
    if (!CurrentGame().activity.conversation ||
        (CurrentGame().activity.conversation && CurrentGame().npc_interaction.scripted_dialogue)) {
        if (IsLockInteractionActive()) {
            goto finish_mode_overlays;
        }
        if (IsTrapInteractionActive()) {
            goto finish_mode_overlays;
        }
        if (!repaint.radar || !layout.radar_map) {
            if (repaint.radar) {
                ClearSurfaceRect(0x17, 0x166, 0x80, 0x1c2);
            }
        } else {
            CatalogArtwork::Load(0x84, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0x17,
                                                            0x166);
        }
        if (!repaint.text_box || !layout.bottom_panel) {
            if (repaint.text_box) {
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
                goto draw_formation_panel;
            }
            CatalogArtwork::Load(0x88, 0).drawAndInvalidate(GetFrameBufferSurface(),
                                                            text_box_variant, 0x8a, 0x168);
        }
    draw_formation_panel:
        if (!repaint.formation || !layout.formation_board) {
            if (repaint.formation) {
                ClearSurfaceRect(0x200, 0x166, 0x268, 0x1c2);
            }
        } else {
            CatalogArtwork::Load(0x83, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0x200,
                                                            0x166);
        }
        goto finish_mode_overlays;
    }
    if (repaint.mode_panels.any()) PaintNpcDialoguePanels(repaint.mode_panels);
finish_mode_overlays:
    if (repaint.radar) publish_chrome(23, 360, 128, 450);
    if (repaint.text_box) publish_chrome(129, 358, 511, 450);
    if (repaint.formation) publish_chrome(512, 358, 617, 450);
    if (repaint.combat_progress) InvalidateRegion(612, 452, 637, 474);
    if (repaint.text_box) {
        RedrawTextBoxScrollChrome();
    }
    if (repaint.submenu_buttons) {
        RedrawSubMenuButtons();
        publish_chrome(31, 450, 607, 477);
    }
    if (repaint.character_action) {
        DrawSubMenuCharacterAction();
    }
    if (hud.open_submenu) {
        RefreshSubMenuPanel(repaint.submenu_panel);
    }
    if (repaint.submenu_panel) publish_chrome(469, 382, 501, 462);
    if (repaint.options_button) {
        RedrawOptionsDiskButton();
        publish_chrome(1, 450, 30, 477);
    }
    if (repaint.combat_stance) {
        RedrawCombatStanceButtons();
        publish_chrome(293, 450, 525, 477);
    }
    if (repaint.everything) ClearVideoDirtyBlocks();
}

W8TargetingPresentation UpdateMainGamePresentation(W8LevelRuntimeBlock& hud)
{
    SyncPartyFacingFromCamera();
    auto presentation = BuildTargetingPresentation(hud);
    ApplyTargetingPresentation(presentation);
    UpdateRadarBlips(presentation);
    // Interactions hold the bottom panel up, so this is the plain text box.
    if (!MainGame()->layout().bottom_panel && !CurrentGame().activity.conversation) {
        const int stopped_line = FindStoppedTextLine();
        if (stopped_line != -1 && hud.text_lines[4 + CurrentGame().status.text_line_cursor] !=
                                      static_cast<unsigned int>(stopped_line)) {
            hud.repaint.text_box = true;
        }
    }
    if (!gpSCSV && ItemUseDraft()) UpdateUseItemScrollButtons();
    if (!IsScreenTransitionPending()) {
        if (hud.open_submenu) UpdateSubMenuAutoClose();
        if (!CurrentGame().combat.has_value()) UpdatePortraitAdvanceButtons();
        UpdateConditionButtons();
        UpdateMainGameButtons();
        if (PartyMovementPanelShown() && !CurrentGame().advancement.held)
            UpdatePartyMovementGauge();
        if (MonotonicMilliseconds() - hud.tick > 499) {
            hud.repaint.video_inspector = g_video_inspector_enabled && !gpSCSV &&
                                          !CurrentGame().activity.conversation && !ItemUseDraft() &&
                                          !IsLockInteractionActive() && !IsTrapInteractionActive();
            hud.tick = MonotonicMilliseconds();
        }
    }
    auto& repaint = hud.repaint;
    if (repaint.everything) {
        repaint = RepaintOfEverything();
    } else if (repaint.layout) {
        repaint.radar = repaint.text_box = repaint.submenu_buttons = repaint.submenu_panel =
            repaint.formation = repaint.options_button = repaint.combat_stance =
                repaint.character_action = true;
    }
    if (repaint.portrait_panel && CurrentGame().combat) repaint.combat_effects = true;
    if (repaint.combat_effects) {
        repaint.portrait_panel = true;
        for (int slot = 0; slot < 8; ++slot)
            if (hud.portrait_refresh_pending[slot]) repaint.portraits.set(slot);
    }
    if (repaint.portrait_panel) {
        // The world view's frame repaints the movement panel and the modal over it.
        if (!CurrentGame().advancement.held && PartyMovementPanelShown())
            repaint.movement_panel = true;
        if (g_modal_owner) g_modal_owner->RequestModalRedraw();
    }
    UpdatePortraitPlate(hud);
    for (int slot = 0; slot < 8; ++slot) {
        if (repaint.portraits.test(slot) ||
            (repaint.portrait_frames.test(slot) && PartyPortraitFrameCoversBand(slot))) {
            KeepNpcDialogueTranscriptOverPortraits();
            break;
        }
    }
    if (repaint.roof_and_spell_icons) repaint.combat_progress = true;
    // The active mode's bottom panels repaint with the layout, and panel 1 with the text box.
    if (repaint.layout) repaint.mode_panels.set();
    if (repaint.text_box) repaint.mode_panels.set(1);
    if (gpSCSV) {
        UpdateSpellCastingPanels(repaint.mode_panels);
    } else if (ItemUseDraft()) {
        UpdateUseItemPanels(repaint.mode_panels);
    } else if (CurrentGame().activity.conversation &&
               !CurrentGame().npc_interaction.scripted_dialogue) {
        UpdateNpcDialoguePanels(repaint.mode_panels);
    } else {
        repaint.mode_panels.reset();
        if (repaint.layout && IsLockInteractionActive()) InvalidateLockInteractionPanels();
        if (repaint.layout && IsTrapInteractionActive()) InvalidateTrapInteractionPanels();
    }
    return presentation;
}

/* Draw the main-game frame: repaint the requested regions and invalidate their
   rectangles, paint overlays, and present. */
void DrawMainGameScreen(W8LevelRuntimeBlock& hud, const W8TargetingPresentation& presentation)
{
    if (const auto& erase = hud.repaint.plate_erase) {
        ClearSurfaceRect(erase->left, erase->top, erase->right, erase->bottom);
        InvalidateRegion(erase->left, erase->top, erase->right, erase->bottom);
    }
    RepaintedPortraitBands bands;
    for (int slot = 0; slot < 8; ++slot) {
        if (hud.repaint.portrait_frames.test(slot) && !hud.repaint.portraits.test(slot))
            bands.add(slot, PaintPartyPortraitFrame(slot));
    }
    if (hud.repaint.any()) {
        ApplyMainGameRepaint(hud, presentation, bands);
    }
    const MainGameRepaint repaint = std::exchange(hud.repaint, {});
    if (!IsScreenTransitionPending()) {
        if (g_modal_owner != 0) {
            g_modal_owner->RefreshModal();
        }
        if (!CurrentGame().combat.has_value()) {
            RefreshPortraitAdvanceButtons(bands.any);
        } else {
            PaintCombatPortraits(repaint.combat_portraits | bands.side_strips);
        }
        PaintConditionIcons(repaint.condition_icons, bands.complete);
        if (PartyMovementPanelShown() && !CurrentGame().advancement.held) {
            DrawPartyMovementPanel(repaint.movement_panel);
        }
        DrawNpcQuoteBubble();
        if (IsLockInteractionActive()) {
            RedrawLockInteractionPanels();
        }
        if (IsTrapInteractionActive()) {
            RedrawTrapInteractionPanels();
        }
        RefreshMainGameButtons();
        RedrawPortraitQuoteBubbles();
        if (auto* text_fields = TextFieldGroup::Focused())
            text_fields->RenderAll(GetFrameBufferSurface());
        if (repaint.video_inspector) DrawVideoInspector(0xdc, 0x32);
        if (MainGame()->layout().formation_board) {
            CreateFormationBoardOverlay();
        }
        if (MainGame()->layout().radar_map) {
            EnsureRadarMapOverlay();
        }
        RenderFrame();
    }
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
                    const auto group_action = EditedAction(context_slot);
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

    assert(CurrentGame().party.members[party_slot].has_value());
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
        SetTargetingMode(W8_TARGET_NEED_NONE);
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
    if (auto* hud = MainGameHud()) {
        hud->repaint.portrait(party_slot);
        hud->repaint.submenu_buttons = true;
        hud->repaint.character_action = true;
    }
    if (auto& combat = CurrentGame().combat) combat->target_picked = true;
}

void RefreshSelectedPartyPortrait(unsigned int party_slot)
{
    if (MainGame()->layout().mode == W8_MAIN_UI_MODE_PORTRAITS ||
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
        if (auto* hud = MainGameHud()) {
            hud->repaint.portrait_panel = true;
            hud->repaint.portraits.set();
        }
    } else if (MainGameHud()->condition_highlight_party_slot != -1) {
        if (auto* hud = MainGameHud()) hud->repaint.portrait_panel = true;
    }

    if (MainGameHud()->portrait_overlay_party_slot == static_cast<int>(party_slot)) {
        MainGameHud()->portrait_overlay_party_slot = -1;
        DismissHighlightOverlay();
        if (auto* hud = MainGameHud()) {
            hud->repaint.portrait_panel = true;
            hud->repaint.portraits.set();
        }
    } else if (MainGameHud()->portrait_overlay_party_slot != -1) {
        if (auto* hud = MainGameHud()) hud->repaint.portrait_panel = true;
    }

    if (g_main_game_mode == W8_MAIN_GAME_NPC_DIALOGUE) {
        CloseNpcDialogueIfActive();
    } else if (g_main_game_mode == W8_MAIN_GAME_HIGHLIGHT_OVERLAY) {
        MainGameHud()->highlight_graphic.reset();
        ClearHighlightOverlayRegion();
    }

    g_main_game_mode = W8_MAIN_GAME_PORTRAIT_REFRESH;
    if (MainGameHud()->highlight.kind == W8_HIGHLIGHT_TARGET) MainGameHud()->highlight = {};
    MainGameHud()->portrait_refresh_pending[party_slot] = 1;
    MainGameHud()->portrait_refresh_image[party_slot] = 0x69;
    if (auto* hud = MainGameHud()) hud->repaint.portrait(party_slot);
    MainGame()->edge_targets[party_slot]->setEnabled(false);
    if (!CurrentGame().monster_manager_entries[party_slot].keyboard_menu_open) {
        EnableMainGamePortraitSlot(party_slot);
    }
}

/* Take the mode-6 hover overlay down: the portrait highlight graphic is
   released, the plate's rectangle is erased by the next draw, and the mode
   resets to the default. */
void DismissHighlightOverlay(void)
{
    MainGameHud()->highlight_graphic.reset();
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

/* The plate a held orb, vitals hover or condition highlight asks for; the first
   one set wins. */
static std::optional<std::pair<PortraitPlate, int>>
TrackedPortraitPlate(const W8LevelRuntimeBlock& hud)
{
    if (hud.condition_orb_party_slot != -1)
        return std::pair{PortraitPlate::Conditions, hud.condition_orb_party_slot};
    if (hud.enchantment_orb_party_slot != -1)
        return std::pair{PortraitPlate::Enchantments, hud.enchantment_orb_party_slot};
    if (hud.portrait_overlay_party_slot != -1)
        return std::pair{PortraitPlate::Vitals, hud.portrait_overlay_party_slot};
    if (hud.condition_highlight_party_slot != -1)
        return std::pair{PortraitPlate::Status, hud.condition_highlight_party_slot};
    return std::nullopt;
}

/* Count the active conditions and widen max_width to the widest name; `text`
   is left holding the last name measured. */
static int MeasurePortraitConditions(const W8Character* character, unsigned int& max_width,
                                     std::string& text)
{
    int row_count = 0;
    for (int condition = 0; condition < W8_CONDITION_COUNT; ++condition) {
        if (character->uiCondition[condition] != 0) {
            ++row_count;
        }
    }
    for (int condition = W8_CONDITION_COUNT - 1; condition > 0; --condition) {
        if (character->uiCondition[condition] != 0) {
            text = Data().strings[kConditionNotices[condition].name];
            const unsigned int width = g_wiz_text_font.textWidth(text);
            max_width = std::max(max_width, width);
        }
    }
    return row_count;
}

static std::string PortraitEnchantmentText(const W8Character* character, int slot)
{
    return fmt::format("{} ({:d})", Data().strings[g_enchantment_notices[slot]],
                       character->enchantments[slot].power);
}

/* Count the active enchantments and widen max_width to the widest row; `text`
   is left holding the last row measured. */
static int MeasurePortraitEnchantments(const W8Character* character, unsigned int& max_width,
                                       std::string& text)
{
    int row_count = 0;
    for (int slot = 0; slot < 8; ++slot) {
        if (character->enchantments[slot].turns != 0) {
            ++row_count;
        }
    }
    for (int slot = 7; slot > 0; --slot) {
        if (character->enchantments[slot].turns != 0) {
            text = PortraitEnchantmentText(character, slot);
            const unsigned int width = g_wiz_text_font.textWidth(text);
            max_width = std::max(max_width, width);
        }
    }
    return row_count;
}

static std::string PortraitRealmPointsText(const W8Character* character, int realm)
{
    return fmt::format("{:d}/{:d}",
                       GetCharacterRealmSpellPoints(character, static_cast<W8SpellRealm>(realm)),
                       character->sp_max[realm]);
}

/* The vitals plate's label column: the wider of the "HP:" and "Stamina:"
   labels, at least 0x12, plus a gap. */
static unsigned int PortraitVitalsLabelWidth()
{
    const unsigned int hp = g_wiz_text_font.textWidth(fmt::format("{}: ", Data().strings[0x282]));
    const unsigned int stamina =
        g_wiz_text_font.textWidth(fmt::format("{}: ", Data().strings[0x283]));
    return std::max({hp, stamina, 0x12u}) + 10;
}

struct PortraitPlateContent {
    int rows;
    unsigned int width;
};

/* The rows and content width a plate needs. Retail seeds the vitals' value
   column from the HP/stamina strings and lets a realm string replace it only
   when it beats the label column; a status list over 0x14 rows ends in the
   "more" notice, and widens to the last row measured when that notice is
   wider. */
static PortraitPlateContent MeasurePortraitPlate(PortraitPlate kind, int party_slot)
{
    const W8Character* character = &CurrentGame().party.characters[party_slot];
    unsigned int max_width = 0;
    std::string text;
    switch (kind) {
    case PortraitPlate::Vitals: {
        const unsigned int label_width = PortraitVitalsLabelWidth();
        unsigned int value_width =
            std::max(g_wiz_text_font.textWidth(
                         fmt::format("{:d}/{:d}", character->hp_current, character->uiHPMax)),
                     g_wiz_text_font.textWidth(
                         fmt::format("{:d}/{:d}", character->stamina, character->uiStaminaMax)));
        for (int realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
            const unsigned int width =
                g_wiz_text_font.textWidth(PortraitRealmPointsText(character, realm));
            if (label_width < width) value_width = width;
        }
        return {8, value_width + label_width};
    }
    case PortraitPlate::Conditions: {
        const int rows = MeasurePortraitConditions(character, max_width, text);
        return {rows, max_width + 0x1a};
    }
    case PortraitPlate::Enchantments: {
        const int rows = MeasurePortraitEnchantments(character, max_width, text);
        return {rows, max_width + 0x1a};
    }
    case PortraitPlate::Status: {
        int rows = MeasurePortraitConditions(character, max_width, text);
        rows += MeasurePortraitEnchantments(character, max_width, text);
        if (rows > 0x14) {
            rows = 0x14;
            const int more = g_wiz_text_font.textWidth(Data().strings[0x7d8]);
            if (max_width < static_cast<unsigned int>(more - 0x1a))
                max_width = g_wiz_text_font.textWidth(text) - 0x1a;
        }
        return {rows, max_width + 0x1a};
    }
    }
    return {};
}

/* Lay out the plate beside a portrait: enter the overlay mode (tearing down an
   NPC dialogue, panel or previous plate), anchor the framed plate off the
   slot's portrait column inside the viewport, and pin the lazily created 0x72
   highlight sprite to the first row, hidden. */
static void LayOutPortraitPlate(W8LevelRuntimeBlock& hud, PortraitPlate kind, int party_slot)
{
    const auto content = MeasurePortraitPlate(kind, party_slot);
    const auto& layout = MainGame()->layout();
    DismissHighlightOverlay();
    SetMainGameMode(W8_MAIN_GAME_HIGHLIGHT_OVERLAY);

    unsigned int min_width =
        std::max(content.width, static_cast<unsigned int>(g_wiz_text_font.textWidth(
                                    CurrentGame().party.characters[party_slot].name)));
    min_width += 6;
    const unsigned int tiles = (min_width + 5) / 6;
    const unsigned int panel_width = tiles * 6 + 0xc;

    int left;
    const unsigned char pending = hud.portrait_refresh_pending[party_slot];
    const bool shifted = pending == 0 && (hud.condition_highlight_party_slot != -1 ||
                                          hud.portrait_overlay_party_slot != -1);
    if ((party_slot & 1) == 0) {
        left = hud.portrait_refresh_image[party_slot] - layout.PortraitHoverXOrigin() + 0x80;
        if (shifted) left += 0x18;
    } else {
        left = layout.PortraitHoverXOrigin() - hud.portrait_refresh_image[party_slot] -
               static_cast<int>(panel_width) + 0x200;
        if (shifted) left -= 0x18;
    }

    const unsigned int content_height = content.rows * 9 + 0x12;
    const int row_y = (party_slot >> 1) * 0x55;
    int top = g_viewport_modes[static_cast<unsigned int>(layout.viewport)].top;
    const int candidate = row_y - static_cast<int>(content_height) + 0x3c;
    if (candidate >= top) {
        if (candidate + content_height * 2 > 0x166) {
            top = 0x166 - content_height * 2;
        } else {
            top = candidate;
            if (pending == 0 && hud.condition_highlight_party_slot != -1) {
                top = row_y + 0x12;
            }
        }
    }
    hud.dialogue_x = left;
    hud.dialogue_y = top;
    hud.dialogue_height = content_height * 2;
    hud.dialogue_width = panel_width;
    hud.dialogue_text_x = left + 9;
    hud.dialogue_text_width = panel_width - 0x12;
    hud.dialogue_row_y = top + 0x1e;
    hud.highlight_row = 0;
    hud.shown_plate = ShownPortraitPlate{kind, party_slot, content.rows};

    const auto artwork = CatalogArtwork::Load(0x72);
    if (auto surface = RenderSpriteToImage(*artwork.spriteSheet(), 0)) {
        hud.highlight_graphic = makeOverlayFromSurface(*surface, 0, 0, 1);
    }
    assert(hud.highlight_graphic != 0);
    positionOverlay(hud.highlight_graphic.get(), hud.dialogue_x + 7,
                    hud.dialogue_row_y - 1 + hud.highlight_row * 0x12, false);
    hud.highlight_graphic->visible = false;
}

/* Draw the plate's frame around `rows` content rows and the character's name in
   their party colour. */
static void PaintPortraitPlateFrame(const W8LevelRuntimeBlock& hud, int party_slot, int rows)
{
    const int left = hud.dialogue_x;
    const int top = static_cast<int>(hud.dialogue_y);
    const unsigned int panel_width = hud.dialogue_width;
    const unsigned int tiles = (panel_width - 0xc) / 6;
    auto& target = GetFrameBufferSurface();
    const auto frame = CatalogArtwork::Load(0x70, 0);
    const auto divider = CatalogArtwork::Load(0x71, 0);

    frame.draw(target, 0, left, top);
    int inner = left + 6;
    for (unsigned int tile = tiles; tile != 0; --tile) {
        frame.draw(target, 1, inner, top);
        inner += 6;
    }
    const int right = left + (tiles * 3 + 3) * 2;
    frame.draw(target, 2, right, top);
    int row_y = top + 6;
    for (int row = 0; row < 2; ++row) {
        DrawHighlightFrameRow(left, right, row_y, tiles);
        row_y += 6;
    }

    const std::string& name = CurrentGame().party.characters[party_slot].name;
    g_wiz_text_font.setPalette(g_font_state_palettes[CurrentGame().party.orderIndexOf(party_slot)]);
    const int name_width = g_wiz_text_font.textWidth(name);
    DrawText(target, g_wiz_text_font,
             {left + static_cast<int>(panel_width >> 1) - name_width / 2,
              (0xc - g_wiz_text_font.height()) / 2 + top + 6},
             name);
    g_wiz_text_font.resetPalette();

    row_y = top + 0x12;
    frame.draw(target, 3, left, row_y);
    divider.draw(target, 0, left + 6, row_y);
    if (tiles > 2) {
        inner = left + 0xc;
        for (unsigned int tile = 0; tile < tiles - 2; ++tile) {
            divider.draw(target, 1, inner, row_y);
            inner += 6;
        }
    }
    divider.draw(target, 2, left + tiles * 6, row_y);
    frame.draw(target, 4, right, row_y);

    row_y += 6;
    for (int row = rows * 3 + 1; row != 0; --row) {
        DrawHighlightFrameRow(left, right, row_y, tiles);
        row_y += 6;
    }
    frame.draw(target, 5, left, row_y);
    inner = left + 6;
    for (unsigned int tile = tiles; tile != 0; --tile) {
        frame.draw(target, 6, inner, row_y);
        inner += 6;
    }
    frame.draw(target, 7, right, row_y);
}

static void DrawPortraitPlateRow(int icon, std::string_view text, int text_x, int row_y,
                                 unsigned int width)
{
    CatalogArtwork::Load(icon, 0).draw(GetFrameBufferSurface(), 0, text_x, row_y);
    const int text_width = g_wiz_text_font.textWidth(text);
    DrawText(GetFrameBufferSurface(), g_wiz_text_font,
             {static_cast<int>(((width >> 1) - text_width / 2) + 0x1a + text_x), row_y}, text);
}

/* The vitals plate: "HP:"/"Stamina:" label rows then one icon row per spell
   realm, each "cur/max" value centered in the column right of the labels. */
static void PaintPortraitVitals(const W8LevelRuntimeBlock& hud, const W8Character* character)
{
    const unsigned int label_width = PortraitVitalsLabelWidth();
    const int text_x = hud.dialogue_text_x;
    const unsigned int value_center = (hud.dialogue_text_width - label_width) >> 1;
    int row_y = hud.dialogue_row_y;
    const auto draw_value = [&](const std::string& value, int y) {
        const int width = g_wiz_text_font.textWidth(value);
        DrawText(GetFrameBufferSurface(), g_wiz_text_font,
                 {static_cast<int>(value_center - width / 2 + text_x + label_width), y}, value);
    };

    DrawText(GetFrameBufferSurface(), g_wiz_text_font, {text_x, row_y},
             fmt::format("{}: ", Data().strings[0x282]));
    draw_value(fmt::format("{:d}/{:d}", character->hp_current, character->uiHPMax), row_y);
    row_y += 0x12;
    DrawText(GetFrameBufferSurface(), g_wiz_text_font, {text_x, row_y},
             fmt::format("{}: ", Data().strings[0x283]));
    draw_value(fmt::format("{:d}/{:d}", character->stamina, character->uiStaminaMax), row_y);
    row_y += 0x12;

    for (int realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
        CatalogArtwork::Load(0x193 + realm, 0)
            .draw(GetFrameBufferSurface(),
                  static_cast<short>(g_spell_realm_animations[realm].initial_frame), text_x, row_y);
        draw_value(PortraitRealmPointsText(character, realm),
                   (0x12 - g_wiz_text_font.height()) / 2 + row_y);
        row_y += 0x12;
    }
}

/* Condition rows, bottom-up: the measure counts all twenty slots while the rows
   visit 0x13 down to 1 only. Returns the next row's y and adds to `rows_drawn`. */
static int PaintPortraitConditionRows(const W8Character* character, int text_x, int row_y,
                                      unsigned int width, int& rows_drawn)
{
    for (int condition = W8_CONDITION_COUNT - 1; condition > 0; --condition) {
        if (character->uiCondition[condition] != 0) {
            DrawPortraitPlateRow(condition + 0xb6,
                                 Data().strings[kConditionNotices[condition].name], text_x, row_y,
                                 width);
            row_y += 0x12;
            ++rows_drawn;
        }
    }
    return row_y;
}

/* Paint the laid-out plate: its frame, then the vitals or the condition and
   enchantment rows. Enchantment rows visit slots 7 down to 1, so slot 0's
   counted row is never drawn; a truncated status list ends in the "more"
   notice. */
void PaintPortraitPlate(const W8LevelRuntimeBlock& hud)
{
    if (g_main_game_mode != W8_MAIN_GAME_HIGHLIGHT_OVERLAY || !hud.shown_plate) return;
    const auto& plate = *hud.shown_plate;
    const W8Character* character = &CurrentGame().party.characters[plate.slot];
    PaintPortraitPlateFrame(hud, plate.slot, plate.rows);

    const int text_x = hud.dialogue_text_x;
    const unsigned int width = hud.dialogue_text_width - 0x1a;
    int row_y = hud.dialogue_row_y;
    int rows_drawn = 0;
    const bool truncated = plate.kind == PortraitPlate::Status &&
                           std::ranges::count_if(character->uiCondition, [](auto c) {
                               return c != 0;
                           }) + std::ranges::count_if(character->enchantments, [](const auto& e) {
                               return e.turns != 0;
                           }) > 0x14;
    if (plate.kind == PortraitPlate::Vitals) {
        PaintPortraitVitals(hud, character);
    } else {
        if (plate.kind != PortraitPlate::Enchantments)
            row_y = PaintPortraitConditionRows(character, text_x, row_y, width, rows_drawn);
        if (plate.kind != PortraitPlate::Conditions) {
            for (int slot = 7; slot > 0; --slot) {
                if (truncated && rows_drawn == 0x13) {
                    const std::string& more = Data().strings[0x7d8];
                    const int more_width = g_wiz_text_font.textWidth(more);
                    DrawText(
                        GetFrameBufferSurface(), g_wiz_text_font,
                        {static_cast<int>((((width - more_width) + 0x1a) >> 1) + text_x), row_y},
                        more);
                    break;
                }
                if (character->enchantments[slot].turns != 0) {
                    DrawPortraitPlateRow(slot + 0xc9, PortraitEnchantmentText(character, slot),
                                         text_x, row_y, width);
                    row_y += 0x12;
                    ++rows_drawn;
                }
            }
        }
    }
    InvalidateRegion(hud.dialogue_x, hud.dialogue_y, hud.dialogue_x + hud.dialogue_width,
                     hud.dialogue_y + hud.dialogue_height);
}

/* Lay out the tracked plate again when the draw repaints what it sits over:
   the world view's frame (unless the movement panel shows), or a band of a
   slot that holds an orb, the vitals or the condition highlight. A slid-out
   portrait does so for any vitals or condition plate. */
void UpdatePortraitPlate(W8LevelRuntimeBlock& hud)
{
    auto& repaint = hud.repaint;
    const auto tracked = TrackedPortraitPlate(hud);
    if (!tracked) return;
    bool lay_out = repaint.portrait_panel && !PartyMovementPanelShown();
    const bool portraits_mode = MainGame()->layout().mode == W8_MAIN_UI_MODE_PORTRAITS;
    for (int slot = 0; slot < 8 && !lay_out; ++slot) {
        const bool band = repaint.portraits.test(slot) ||
                          (repaint.portrait_frames.test(slot) &&
                           (portraits_mode || hud.portrait_refresh_pending[slot] != 0));
        if (!band) continue;
        lay_out =
            slot == hud.condition_orb_party_slot || slot == hud.enchantment_orb_party_slot ||
            slot == hud.portrait_overlay_party_slot || slot == hud.condition_highlight_party_slot ||
            (!portraits_mode && hud.portrait_refresh_pending[slot] != 0 &&
             (hud.condition_highlight_party_slot != -1 || hud.portrait_overlay_party_slot != -1));
    }
    if (!lay_out) return;
    LayOutPortraitPlate(hud, tracked->first, tracked->second);
    repaint.portrait_plate = true;
}

/* Raise a modal notice line over the main game: enter mode 5 (tearing down
   whichever of the NPC-dialogue, message-box, or dialogue-highlight modes was
   current), then create the kind-1 message dialog, size it, and install the
   caller's confirmation callback as the modal owner. */
void ShowMainGameNoticeLine(std::string_view text, std::function<void(bool)> callback,
                            bool confirmation, bool cancel)
{
    SetMainGameMode(W8_MAIN_GAME_MODAL);
    auto dialog = CreateMessageDialog();
    dialog->SetClientExtent(0xfa, 200);
    dialog->SetMessage(text, 1, 0x32, confirmation, cancel, true, true, 0, 0x15e);
    dialog->on_complete = std::move(callback);
    OpenModal(std::move(dialog));
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
    if (!CurrentGame().party.members[party_slot].has_value() ||
        CurrentGame().combat->characters[party_slot].dead) {
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
                ConfigureSpellTargetFilter(GetSpellTargetType(spell_id, false), needed_kind,
                                           nullptr);
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
            placeParty(*GetWorld(), g_trigger_camera);
            return;
        }
        SetMainGameMode(W8_MAIN_GAME_MODAL);
        auto dialog = CreateMessageDialog();
        dialog->SetClientExtent(0xfa, 0xc8);
        dialog->SetMessage(question, 1, 0x32, true, true, true, true, 0, 0x15e);
        dialog->on_complete = [level_id, entry](bool accepted) {
            if (accepted) {
                BeginLevelTransition(level_id, entry);
            } else {
                placeParty(*GetWorld(), g_trigger_camera);
            }
        };
        OpenModal(std::move(dialog));
        return;
    }
    BeginLevelTransition(level_id, entry);
}

/* True when the cursor hotspot lies in the formation-mode portrait rect for
   `party_slot`, accounting for the hover panel's x origin and the slot's
   mirrored even/odd layout. */
static bool IsPartyPortraitUnderCursor(unsigned int party_slot)
{
    const auto& hud = *MainGameHud();
    const auto rect =
        PartyPortraitBounds(party_slot, hud.portrait_refresh_image[party_slot] -
                                            MainGame()->layout().PortraitHoverXOrigin());
    return rect.right > 0 &&
           IsCursorInRectangle(std::max(0, rect.left), rect.top, rect.right - 1, rect.bottom - 1);
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

        if (CurrentGame().party.members[slot].has_value() && character->hp_current > 0 &&
            character->highest_condition <= W8_CONDITION_UNCONSCIOUS) {
            if (CurrentGame().combat->actingSlot() != slot ||
                CurrentGame().combat->action_delay > std::chrono::milliseconds{800}) {
                if (IsPartySlotEligible(slot)) {
                    if (combat_row->dead) {
                        status = W8_COMBAT_PORTRAIT_INELIGIBLE;
                    } else {
                        status = CharacterCanSwitchTo(
                                     slot, SelectCombatAction(slot, W8_TARGETING_CONTEXT_IN_COMBAT),
                                     false, false)
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
                MainGameHud()->repaint.combat_portraits.set(slot);
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
            if (CurrentGame().party.members[MainGameHud()->combat_slot].has_value() == 0) {
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
        } else if (!CurrentGame().party.members[party_slot].has_value()) {
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
    if (g_settings.main_ui_mode != W8_MAIN_UI_MODE_PORTRAITS &&
        MainGameHud()->portrait_refresh_pending[slot] != 0) {
        MainGameHud()->portrait_refresh_pending[slot] = 0;
        CurrentGame().monster_manager_entries[slot].portrait_refresh_pinned = false;
        MainGameHud()->portrait_refresh_image[slot] = 0;
        const auto rect = PartyPortraitBounds(slot);
        EraseRegion(rect.left - 1, rect.top, rect.right, rect.bottom - 1);
        if (auto* hud = MainGameHud()) hud->repaint.portrait(slot);
        MainGame()->edge_targets[slot]->setEnabled(true);
        DisableMainGamePortraitSlot(slot);

        if (auto* hud = MainGameHud()) hud->repaint.portrait_panel = true;
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

// A conversation that is not scripted holds the NPC dialogue panel up.
static bool NpcDialogueOpen(const Game& game)
{
    return game.activity.conversation && !game.npc_interaction.scripted_dialogue;
}

MainGameLayout MainGameLayout::Decide(const W8GameSettings& settings, bool picker,
                                      bool npc_dialogue)
{
    const W8MainUiMode preferred = settings.main_ui_mode;
    if (picker || npc_dialogue) {
        /* The interaction supplies the bottom panel, and the world keeps the viewport
           of the preferred mode. A picker shows the formation layout over radar mode. */
        MainGameLayout layout{.mode = preferred, .interaction = true, .bottom_panel = true};
        switch (preferred) {
        case W8_MAIN_UI_MODE_RADAR:
            layout.viewport = W8_VIEWPORT_MODE_FULL;
            layout.mode = W8_MAIN_UI_MODE_FORMATION;
            break;
        case W8_MAIN_UI_MODE_FORMATION:
            layout.viewport = W8_VIEWPORT_MODE_FULL_WITH_BOTTOM_PANEL;
            break;
        default:
            layout.viewport = W8_VIEWPORT_MODE_CENTERED_WITH_BOTTOM_PANEL;
            break;
        }
        if (npc_dialogue) layout.mode = W8_MAIN_UI_MODE_PORTRAITS;
        return layout;
    }
    switch (preferred) {
    case W8_MAIN_UI_MODE_RADAR:
        return {.mode = preferred, .viewport = W8_VIEWPORT_MODE_FULL};
    case W8_MAIN_UI_MODE_PORTRAITS: {
        const bool action = settings.portraits_action_panel_preference != 0;
        return {.mode = preferred,
                .formation_board = true,
                .radar_map = true,
                .bottom_panel = action,
                .viewport = action ? W8_VIEWPORT_MODE_CENTERED_WITH_BOTTOM_PANEL
                                   : W8_VIEWPORT_MODE_CENTERED_FULL_HEIGHT};
    }
    default: {
        const bool board = settings.formation_board_preference != 0;
        const bool radar = settings.formation_radar_map_preference != 0;
        const bool action = settings.formation_action_panel_preference != 0;
        return {.mode = preferred,
                .formation_board = board,
                .radar_map = radar,
                .bottom_panel = action,
                .viewport = board && radar && action ? W8_VIEWPORT_MODE_FULL_WITH_BOTTOM_PANEL
                                                     : W8_VIEWPORT_MODE_FULL};
    }
    }
}

void MainGameScreen::setPreferredMode(W8MainUiMode mode)
{
    g_settings.main_ui_mode = mode;
    refreshLayout();
}

// Raise the radar map panel, zoomed for the current fight, or release it.
void MainGameScreen::showRadarMap(bool visible)
{
    radar.setEnabled(visible);
    radar_target->setEnabled(visible);
    EnableRadarMap(visible);
    if (visible) {
        hud->radar_map_alternate = 0;
        if (game.combat.has_value()) {
            ZoomRadarMapIn();
        } else {
            ZoomRadarMapOut();
        }
    } else {
        ReleaseRadarMap();
    }
    hud->repaint.layout = true;
    hud->repaint.portrait_panel = true;
}

// Build the formation board's sprites, or release them.
void MainGameScreen::showFormationBoard(bool visible)
{
    formation.setEnabled(visible);
    if (visible) {
        hud->formation_board_alternate = 0;
        RefreshFormationBoard();
    } else {
        ReleaseFormationBoard();
    }
    hud->repaint.layout = true;
    hud->repaint.portrait_panel = true;
}

/* Set which controls take part in input under `layout` and the open interaction. */
void MainGameScreen::syncInput(const MainGameLayout& layout)
{
    const bool npc_dialogue = NpcDialogueOpen(game);
    world.setEnabled(true);
    hud_buttons.input.setEnabled(true);
    monster_list->setEnabled(hud->group_list_rows != 0);
    edges.setEnabled(layout.mode != W8_MAIN_UI_MODE_PORTRAITS);
    refreshPartySlotRegions();
    combat.setEnabled(game.combat.has_value());
    if (game.combat.has_value()) {
        DisablePortraitControls();
    } else {
        EnablePortraitAdvanceRegions();
    }
    if (layout.mode == W8_MAIN_UI_MODE_PORTRAITS) {
        DisableConditionButtons();
    } else {
        EnableConditionButtons();
    }

    // Spell and item pickers always raise the text box; otherwise it follows the bottom panel.
    if (gpSCSV || (!npc_dialogue && ItemUseDraft())) {
        RestoreSpellCastingRegions();
    } else {
        SetActionPanelRegions(layout.bottom_panel);
    }
    if (gpSCSV) gpSCSV->input.setEnabled(true);
    if (npc_dialogue) SetNpcDialogueInputEnabled(2, true);
    if (ItemUseDraft()) SetUseItemSelectInputEnabled(true);
    // A lock or trap being opened is only created after its layout is applied.
    if (lock_active && lock_interaction) lock_interaction->EnablePanels(true);
    if (trap_active && trap_interaction) trap_interaction->EnablePanelInput(true);
    formation.setEnabled(layout.formation_board);
    radar.setEnabled(layout.radar_map);

    if (party_movement_panel && !game.advancement.held) {
        UpdatePartyMovementPanel();
    } else {
        DisablePartyMovementRegions();
    }
}

void MainGameScreen::refreshLayout()
{
    const bool picker = gpSCSV || ItemUseDraft() || lock_active || trap_active;
    const auto layout = MainGameLayout::Decide(g_settings, picker, NpcDialogueOpen(game));
    const auto previous = std::exchange(applied_layout, layout);
    if (layout != previous) {
        const bool portraits = layout.mode == W8_MAIN_UI_MODE_PORTRAITS;
        SetTooltipSubject(portraits ? W8_HIGHLIGHT_TARGET : W8_HIGHLIGHT_VITALS, -1);
        if (portraits) {
            for (unsigned int slot = 0; slot < 8; ++slot) ClearPortraitRefreshSlot(slot);
        }
        if (layout.formation_board != previous.formation_board) {
            showFormationBoard(layout.formation_board);
        }
        if (layout.radar_map != previous.radar_map) showRadarMap(layout.radar_map);
        if (layout.viewport != previous.viewport) {
            const auto& rect = g_viewport_modes[static_cast<unsigned int>(layout.viewport)];
            world_view->setShape(
                SDL_Rect{rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top});
            SetViewport({rect.left, rect.top, rect.right + 1, rect.bottom + 1});
            partyCamera().bottom_panel_view =
                layout.viewport == W8_VIEWPORT_MODE_FULL_WITH_BOTTOM_PANEL ||
                layout.viewport == W8_VIEWPORT_MODE_CENTERED_WITH_BOTTOM_PANEL;
        }
        ClearUiHover();
        SetMouseCursorHotspot(0, 0);
        SyncSystemCursor();
    }
    hud->repaint.everything = true;
    hud->repaint.roof_and_spell_icons = true;
    if (game.combat.has_value()) hud->refresh_combat_panel = 1;
    syncInput(layout);
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

void MainGameScreen::refreshPartySlotRegions()
{
    for (int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        const bool available = game.party.members[slot].has_value() &&
                               !game.monster_manager_entries[slot].keyboard_menu_open;
        const bool full = applied_layout.mode == W8_MAIN_UI_MODE_PORTRAITS ||
                          hud->portrait_refresh_pending[slot] != 0;
        portraits[slot]->setEnabled(available && full);
        edge_targets[slot]->setEnabled(available && !full);
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

    const auto selected_action = CurrentGame().status.selected_character < 0
                                     ? W8Action{W8_ACTION_NONE, -1, {}, {}}
                                     : EditedAction(CurrentGame().status.selected_character);
    needed = IsSighting(CurrentGame().status.selected_character)
                 ? W8_TARGET_NEED_NONE
                 : GetTargetNeededForAction(CurrentGame().party, selected_action);
    if (ScreenPointer().targeting() == W8_TARGET_NEED_ALLY ||
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
    } else if (ScreenPointer().targeting() == W8_TARGET_NEED_CHARACTER_INDIRECT ||
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
                    if (auto* hud = MainGameHud()) hud->repaint.portrait_panel = true;
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
                    if (auto* hud = MainGameHud()) hud->repaint.portrait_panel = true;
                    if (CurrentScreenState().id != W8_SCREEN_MAIN_GAME) {
                        return UiResult::Handled;
                    }
                    if (MainGameHud() == nullptr) {
                        return UiResult::Handled;
                    }
                    if (auto* hud = MainGameHud()) hud->repaint.portraits.set();
                } else if (IsNpcDialogueCursorActive() == 0) {
                    if (targeting) {
                        if (aim_ok) {
                            if (!CurrentGame().activity.scripted_scene) {
                                if (!CanPartySlotParticipate(slot)) {
                                    AimAtTarget(
                                        CurrentGame().status.selected_character,
                                        CharacterTarget(slot, W8_TARGET_KIND_CHARACTER_INDIRECT));
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
                                    AimAtTarget(CurrentGame().status.selected_character,
                                                CharacterTarget(slot));
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
                               ScreenPointer().shownSlot() != W8_CURSOR_INVALID_TARGET ||
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
                if (auto* hud = MainGameHud()) hud->repaint.portrait(slot);
                if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME && MainGameHud() != nullptr) {
                    if (auto* hud = MainGameHud()) hud->repaint.character_action = true;
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
                            if (ScreenPointer().targeting() == W8_TARGET_NEED_NONE) {
                                RevalidateSelectedTarget(CurrentGame().status.selected_character);
                            }
                            ScreenPointer().showSlot(4);
                            return UiResult::Pass;
                        }
                        ScreenPointer().showSlot(ScreenPointer().targetingSlot(false));
                    }
                } else {
                    if (MainGameHud()->portrait_refresh_pending[slot] == 0) {
                        if (CurrentGame().party.held.item.iItemNo == -1 ||
                            ScreenPointer().shownSlot() != W8_CURSOR_INVALID_TARGET ||
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
                        if (ScreenPointer().targeting() == W8_TARGET_NEED_NONE) {

                            return UiResult::Pass;
                        }
                    }
                }
            } else {
                MainGameHud()->portrait_right_hold_armed = false;
                if (IsNpcDialogueCursorActive() == 0) {
                    SetTooltipSubject(W8_HIGHLIGHT_TARGET, -1);
                    ScreenPointer().showSlot(ScreenPointer().targetingSlot(false));
                }
                if (MainGameHud()->portrait_overlay_party_slot == slot) {
                    MainGameHud()->portrait_overlay_party_slot = -1;
                    DismissHighlightOverlay();
                    if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME &&
                        MainGameHud() != nullptr) {
                        if (auto* hud = MainGameHud()) hud->repaint.portrait_panel = true;
                        if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME &&
                            MainGameHud() != nullptr) {
                            if (auto* hud = MainGameHud()) hud->repaint.portraits.set();
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
                    if (auto* hud = MainGameHud()) hud->repaint.portrait_panel = true;
                }
            } else {
                if (event.action == UiPointerAction::Enter) {
                    if ((character->*effect) != none) {
                        SetTooltipSubject(tooltip_kind, slot);
                        if (IsLeftButtonHeld() && (character->*effect) != none) {
                            (hud->*selection) = slot;
                            if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME)
                                hud->repaint.portrait_panel = true;
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
        if (auto* hud = MainGameHud()) hud->repaint.portrait_panel = true;
        return UiResult::Handled;
    }

    if ((character->*effect) == none) {
        return UiResult::Handled;
    }
    (hud->*selection) = slot;
    if (auto* hud = MainGameHud()) hud->repaint.portrait_panel = true;
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
        if (auto* hud = MainGameHud()) hud->repaint.portrait(slot);
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
            if (auto* hud = MainGameHud()) hud->repaint.portrait_panel = true;
            return UiResult::Pass;
        }
        MainGameHud()->portrait_overlay_party_slot = -1;
        DismissHighlightOverlay();
    }
    if (auto* hud = MainGameHud()) hud->repaint.portrait_panel = true;
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
            CurrentGame().party.members[slot].has_value()) {
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
            MainGameHud()->repaint.combat_portraits.set(slot);
            target.setTooltip({});
            return UiResult::Pass;
        }
        if (event.action != UiPointerAction::Enter) {
            return UiResult::Pass;
        }
        if (CurrentGame().combat->round_active) {
            MainGameHud()->combat_action_hover_party_slot = slot;
            MainGameHud()->repaint.combat_portraits.set(slot);
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
                             Data(),
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
    const auto selected_action = CurrentGame().status.selected_character < 0
                                     ? W8Action{W8_ACTION_NONE, -1, {}, {}}
                                     : EditedAction(CurrentGame().status.selected_character);
    needed = IsSighting(CurrentGame().status.selected_character)
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
        if (!CurrentGame().activity.conversation &&
            ScreenPointer().targeting() != W8_TARGET_NEED_PLACE &&
            ScreenPointer().targeting() != W8_TARGET_NEED_CONE &&
            ScreenPointer().targeting() != W8_TARGET_NEED_ITEM &&
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
                if (CurrentGame().party.members[slot].has_value() &&
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
    if (ScreenPointer().targeting() == W8_TARGET_NEED_PLACE ||
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
    } else if (ScreenPointer().targeting() == W8_TARGET_NEED_CONE ||
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
            if (ScreenPointer().targeting() != W8_TARGET_NEED_NONE ||
                !CurrentGame().activateSelectedNativeObject())
                ActivateSelectedProp();
        }
    } else {
        W8MonsterInfo* monster_info = FindMonster(MainGameHud()->highlighted_item);
        bool assign = true;
        if (!CurrentGame().combat.has_value()) {
            if (needed != W8_TARGET_NEED_ENEMY && needed != W8_TARGET_NEED_ALLY &&
                needed != W8_TARGET_NEED_GROUP) {
                assign = false;
                if ((ScreenPointer().shownSlot() == W8_CURSOR_VALID_TARGET ||
                     (CurrentGame().party.held.item.iItemNo != -1)) &&
                    CurrentGame().status.selected_character != -1 &&
                    AssessMonsterAim(CurrentGame().status.selected_character, 2, monster_info,
                                     EditedAction(CurrentGame().status.selected_character))) {
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
                if (CurrentGame().party.members[slot].has_value() &&
                    CurrentGame().party.characters[slot].hp_current != 0) {
                    AimAtMonsterLocation(slot, MainGameHud()->highlighted_item, false);
                }
            }
            assign = false;
        }
        if (assign) {
            assert(CurrentGame().status.selected_character != -1);
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
        ScreenPointer().targeting() != W8_TARGET_NEED_PLACE &&
        ScreenPointer().targeting() != W8_TARGET_NEED_CONE &&
        ScreenPointer().targeting() != W8_TARGET_NEED_ITEM &&
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
            CurrentCombatSubjectIsValid(*MainGameHud())) {
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
    ScreenPointer().showSlot(ScreenPointer().targetingSlot(false));
}

/* Refresh the target cursor as the mouse moves over the world view. A
   highlighted monster checks targetability and the mode flags; a picked prop
   checks whether its trigger takes the in-cursor item or shows a message;
   otherwise the cursor comes from the current targeting state. */
void UpdateWorldViewCursor(const UiPointerEvent& event, W8TargetNeed target_needed)
{
    const auto edited_action = CurrentGame().status.selected_character < 0
                                   ? W8Action{W8_ACTION_NONE, -1, {}, {}}
                                   : EditedAction(CurrentGame().status.selected_character);
    const bool sighting = IsSighting(CurrentGame().status.selected_character);

    int cursor = ScreenPointer().shownSlot();
    if (cursor == W8_CURSOR_INVALID_TARGET) {
        return;
    }
    if (MainGameHud()->highlighted_item == -1) {
        if (ScreenPointer().targeting() == W8_TARGET_NEED_NONE) {
            if (MainGameHud()->selected_item != -1) {
                if (!CurrentGame().advancement.held) {
                    ScreenPointer().showSlot(5);
                    return;
                }
                ScreenPointer().showSlot(cursor);
                return;
            }
            if (CurrentGame().interactiveNativeObject()) {
                ScreenPointer().showSlot(13);
                return;
            }
            int prop_index = GetSelectedPropIndex();
            if (prop_index > -1) {
                Prop* prop = GetWorldProp(GetWorld(), prop_index);
                if (!CurrentGame().advancement.held) {
                    if (prop != 0) {
                        if (prop->TriggerRequiresItem() &&
                            (CurrentGame().party.held.item.iItemNo != -1)) {
                            ScreenPointer().showSlot(0x10);
                            return;
                        }
                        ScreenPointer().showSlot(prop->TriggerHasActionMessage() ? 13 : 5);
                        return;
                    }
                    ScreenPointer().showSlot(5);
                    return;
                }
                ScreenPointer().showSlot(cursor);
                return;
            }
        }
        cursor = 0;
    } else {
        FindMonster(MainGameHud()->highlighted_item);
        if (!AssessMonsterTarget(CurrentGame().status.selected_character,
                                 MainGameHud()->highlighted_item, edited_action, true, sighting)) {
            ScreenPointer().showSlot(cursor);
            return;
        }
        if (target_needed == W8_TARGET_NEED_NONE && !CurrentGame().combat.has_value() && !gpSCSV &&
            !ItemUseDraft() && !CurrentGame().activity.conversation && !IsLockInteractionActive() &&
            !IsTrapInteractionActive()) {
            ScreenPointer().showSlot(W8_CURSOR_VALID_TARGET);
            return;
        }
        cursor = 1;
    }
    ScreenPointer().showSlot(ScreenPointer().targetingSlot(cursor));
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
        !(CurrentGame().activity.conversation && CurrentGame().npc_interaction.scripted_dialogue)) {
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
        ScreenPointer().showSlot(W8_CURSOR_MAP_LOAD);
        g_world_cleanup_flag = true;
        UnloadSkyWorld();
        LeaveLevel();
        g_world_cleanup_flag = false;
        ScreenPointer().reset();
    }
    return true;
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
        if (auto* hud = MainGameHud()) hud->repaint.portrait(previous.slot);
    }
    if (next.slot != -1) {
        if (auto* hud = MainGameHud()) hud->repaint.portrait(next.slot);
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
        if (Monster* monster = info ? info->entity.find() : nullptr) {
            const auto bounds = monster->GetWorldAnimationBounds();
            if (bounds && traceLineOfSightToBounds(cameraLocation(), *bounds))
                MainGameHud()->highlighted_item = value;
        }
    }
    if (auto* hud = MainGameHud()) hud->repaint.monster_list = true;
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
            if (item != 0 && item->entity &&
                traceLineOfSightToBounds(cameraLocation(), GetItemWorldBounds(*item->entity))) {
                MainGameHud()->selected_item = value;
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
            auto text = FormatItemDisplayName(Data(), &item->item, true);

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
    ScreenPointer().showSlot(
        ScreenPointer().targetingSlot(CurrentCombatSubjectIsValid(*MainGameHud())));
    if (auto* hud = MainGameHud()) hud->repaint.monster_list = true;
}

/* With no modal or NPC dialogue up, draw the bottom prompt strip: the catalog
   backdrop plus the "choose action" line - the combat variant while combat
   mode is on. */
static void DrawMainGamePrompt(void)
{
    W8ControlsRect bounds;

    if (g_modal_owner == 0 && !CurrentGame().activity.conversation) {
        CatalogArtwork::Load(0xa9, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0xb1, 0x13f);
        bounds.left = 0xb1;
        bounds.top = 0x13f;
        bounds.right = 0x1cf;
        bounds.bottom = 0x153;
        W8TextBuffer buffer(&bounds, {}, g_wiz_text_font_secondary,
                            g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter, 4);
        buffer.RenderString(Data().strings[CurrentGame().combat.has_value() ? 0x780 : 0x77f],
                            g_wiz_text_font_secondary, false, GetFrameBufferSurface());
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
            (CurrentGame().activity.conversation &&
             CurrentGame().npc_interaction.scripted_dialogue)) {
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
            return false;
        }
    }
    assert(state != -1);
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
        if (auto* hud = MainGameHud()) hud->repaint.portrait_panel = true;
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
    if (!monster_info->fActive || monster_info->entity->IsDying() ||
        monster_info->hp_current == 0) {
        return;
    }
    auto dialog = std::make_unique<W8MonsterInfoDialog>(location_id);
    dialog->SetText("");
    dialog->on_close = [] {
        if (auto* hud = MainGameHud()) hud->repaint.everything = true;
    };
    OpenModal(std::move(dialog));
}

/* Open the assay dialog over the main game screen. A live modal dialog is
   parked in the pending slot so it resumes when this one closes. */
void OpenAssayDialog(const W8ItemInstance& item, int character_slot)
{
    if (g_modal_owner != 0) {
        g_pending_main_game_dialog = std::move(g_modal_owner);
    }
    auto dialog = std::make_unique<W8AssayDialog>(
        item, character_slot != -1 ? &CurrentGame().party.characters[character_slot] : nullptr,
        SDL_Point{kInfoDialogX, 0x48});
    dialog->SetText("");

    dialog->on_close = [] {
        if (auto* hud = MainGameHud()) hud->repaint.everything = true;
    };
    OpenModal(std::move(dialog));
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
    SetUiCanvasAlphaTestEnabled(false);
}

void MainGameScreen::closeOverlays()
{
    auto& layout = applied_layout;
    if (std::exchange(layout.formation_board, false)) showFormationBoard(false);
    if (std::exchange(layout.radar_map, false)) showRadarMap(false);
    if (std::exchange(layout.bottom_panel, false)) SetActionPanelRegions(false);
}

void ClearHighlightOverlayRegion(void)
{
    auto& hud = *MainGameHud();
    hud.shown_plate.reset();
    if (g_main_game_mode != W8_MAIN_GAME_HIGHLIGHT_OVERLAY) return;
    const DesignRect plate{hud.dialogue_x, static_cast<int>(hud.dialogue_y),
                           hud.dialogue_x + hud.dialogue_width,
                           static_cast<int>(hud.dialogue_y + hud.dialogue_height)};
    hud.repaint.plate_erase =
        hud.repaint.plate_erase ? UnionScreenRects(*hud.repaint.plate_erase, plate) : plate;
    if (hud.dialogue_y <
        static_cast<unsigned int>(
            g_viewport_modes[static_cast<unsigned int>(MainGame()->layout().viewport)].top))
        hud.repaint.roof_and_spell_icons = true;
    if (hud.dialogue_y + hud.dialogue_height > 0x166) hud.repaint.text_box = true;
}

void W8NpcDialogueTextController::ClearTranscriptEntries()
{
    showing_notebook = false;
    while (!text_area.m_all_lines.empty()) {
        text_area.RemoveEntry(0);
    }
    InvalidateAll();
}

/* Forget the keyword on the selected visible line and drop its line. */
void W8NpcDialogueTextController::RemoveSelectedTranscriptEntry()
{
    if (text_area.m_highlighted_entry != -1) {
        int index = text_area.GetOwningEntryIndex(text_area.m_highlighted_entry);
        if (index != -1) {
            const std::string_view text = text_area.GetEntryText(index).value();
            std::erase_if(CurrentGame().story.keywords,
                          [&](const W8DialogueTranscriptRecord& record) {
                              return CompareTextIgnoreAsciiCase(record.text, text) == 0;
                          });
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

// Finish the transfer and its consequences before presenting the result.
void ConfirmNpcTradeItem(void)
{
    auto& panel = MainGame()->dialogue;
    if (!panel.trade_selection || panel.trade_mode == W8_NPC_TRADE_NONE) return;
    auto& game = CurrentGame();
    auto* npc = game.npc_interaction.dialogue_npc.get();
    const auto selection = *panel.trade_selection;
    const int actor = game.status.selected_character;
    const int row = MainGameHud()->selected_text_lines[2];
    const auto action = panel.trade_mode == W8_NPC_TRADE_GIVE       ? W8TradeAction::Offer
                        : panel.trade_mode == W8_NPC_TRADE_SHOPLIFT ? W8TradeAction::Steal
                                                                    : W8TradeAction::Exchange;
    const auto result = CompleteNpcTrade(game, npc, actor, selection, panel.trade_quantity, action);
    if (result.theft_succeeded)
        DisplayNpcQuote(fmt::sprintf(Data().strings[0x74d], game.party.characters[actor].name,
                                     GetItemDisplayName(Data(), &selection.expected)),
                        true);
    switch (result.outcome) {
    case W8TradeOutcome::Offer:
        HandleNpcDialogueItemChoice();
        break;
    case W8TradeOutcome::Complete:
        wiz8::sound::play("Data\\Sound\\misc\\Cash Transaction.wav");
        if (result.dropped_overflow) DisplayNpcQuote(Data().strings[0x6b1], false);
        if (!result.theft_succeeded) SortNpcItems(npc);
        RebuildNpcTradeItemList(false);
        if (!result.theft_succeeded) {
            SetSelectedTextLine(row, 2);
            UpdateNpcTradeSelection(row, 0, 1);
        }
        if (result.accepted_topic) game.npc_dialogue.QueueQuote(0x15, false, false, false);
        break;
    case W8TradeOutcome::TheftFailed:
        DisplayNpcQuote(fmt::sprintf(Data().strings[0x74f], game.party.characters[actor].name),
                        false);
        break;
    case W8TradeOutcome::TheftCaught:
        game.npc_dialogue.QueueQuote(0x17, false, false, false);
        CloseNpcDialogueOptionLayout();
        ShowNpcDialogueTopicMenu();
        break;
    case W8TradeOutcome::Refused:
        if (!result.theft_succeeded) game.npc_dialogue.QueueQuote(0x11, false, false, false);
        break;
    case W8TradeOutcome::InsufficientGold:
        game.npc_dialogue.QueueQuote(0x14, false, false, false);
        break;
    case W8TradeOutcome::WantedByOwner:
        QueueCharacterEvent(&game.party.characters[selection.character], 109, 0,
                            kCharacterEventFullVolume);
        return;
    case W8TradeOutcome::InvalidSelection:
        if (action == W8TradeAction::Exchange) RebuildNpcTradeItemList(false);
        break;
    }
    RefreshNpcTradePartyGold();
    RefreshNpcTradePrice();
}

void ShowNpcTradeItemNotice(W8ItemInstance* item)
{
    W8NpcTradeMode mode = MainGame()->dialogue.trade_mode;
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
        price =
            CalculateNpcTradeStackPrice(CurrentGame().npc_interaction.dialogue_npc.get(),
                                        item->iItemNo, price_kind, stack_count, item->identified);
    } else {
        price = GetItemStackValue(item);
    }
    if (CurrentGame().party.gold < price &&
        MainGame()->dialogue.trade_mode != W8_NPC_TRADE_SHOPLIFT) {
        font_palette = 0;
    }
    mode = MainGame()->dialogue.trade_mode;
    if (mode != W8_NPC_TRADE_SELL && mode != W8_NPC_TRADE_BUY && mode != W8_NPC_TRADE_SHOPLIFT) {
        ShowNotice(font_palette, FormatItemDisplayName(Data(), item, true), 2);
        return;
    }
    MainGameHud()->text_paint_scratch = fmt::format("{:d}{}", price, Data().strings[0x797]);
    ShowNotice(font_palette, FormatItemDisplayName(Data(), item, true), 2,
               (MainGameHud()->text_box_right - MainGameHud()->text_box_left) -
                   (*MainGameHud()->text_box_font).textWidth(MainGameHud()->text_paint_scratch));
    AppendTextBoxLine(MainGameHud()->text_paint_scratch, 2);
}

void RebuildNpcTradeItemList(bool scroll_to_top)
{
    MainGame()->dialogue.trade_rows.clear();
    MainGame()->dialogue.trade_selection.reset();
    ResetEditorStatusLine(2);
    ResetNpcDialogueItemEditor();
    EnableNpcTradeFilterButtons();
    if (scroll_to_top) {
        MainGameHud()->text_lines[2] = 0;
    }
    if (MainGame()->dialogue.trade_mode == W8_NPC_TRADE_BUY ||
        MainGame()->dialogue.trade_mode == W8_NPC_TRADE_SHOPLIFT) {
        unsigned int shown = 0;
        const auto count = GetNpcItemCount(CurrentGame().npc_interaction.dialogue_npc.get());
        for (int index = 0; index < static_cast<int>(count); ++index) {
            W8NpcItemEntry* entry =
                GetNpcItemAt(CurrentGame().npc_interaction.dialogue_npc.get(), index);
            ++shown;
            if (shown > 0x15e) {
                break;
            }
            if (!NpcTradeItemAllowed(&entry->item) && entry->available_at == 0) {
                MainGame()->dialogue.trade_rows.push_back(
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
    if (MainGame()->dialogue.trade_mode == W8_NPC_TRADE_SELL) {
        unsigned char count =
            Data().items[item->iItemNo].equip_class == W8_ITEM_EQUIP_CLASS_AMMUNITION
                ? item->stack_count
                : 1;
        int price = CalculateNpcTradeStackPrice(CurrentGame().npc_interaction.dialogue_npc.get(),
                                                item->iItemNo, W8_TRADE_PRICE_PARTY_SELLS, count,
                                                item->identified);
        if (acceptable) {
            MainGameHud()->text_paint_scratch = fmt::format("{:d}{}", price, Data().strings[0x797]);
        } else {
            MainGameHud()->text_paint_scratch = "---";
        }
        ShowNotice(
            font_palette, FormatItemDisplayName(Data(), item, true), 2,
            (MainGameHud()->text_box_right - MainGameHud()->text_box_left) -
                (*MainGameHud()->text_box_font).textWidth(MainGameHud()->text_paint_scratch));
        AppendTextBoxLine(MainGameHud()->text_paint_scratch, 2);
    } else {
        ShowNotice(font_palette, FormatItemDisplayName(Data(), item, true), 2);
    }
}

void PopulateNpcTradeList(void)
{
    MainGame()->dialogue.trade_rows.clear();
    MainGame()->dialogue.trade_selection.reset();
    W8Character* trading = &CurrentGame().party.characters[CurrentGame().status.selected_character];
    unsigned int shown = 0;

    ResetEditorStatusLine(2);
    ResetNpcDialogueItemEditor();
    EnableNpcTradeFilterButtons();
    if (MainGame()->dialogue.trade_mode == W8_NPC_TRADE_GIVE) {
        MainGameHud()->text_paint_scratch =
            fmt::format("{:d}{}", CurrentGame().party.gold, Data().strings[0x797]);
        ShowNotice(
            W8_FONT_PALETTE_TEXT_BOX, Data().strings[0x72d], 2,
            (MainGameHud()->text_box_right - MainGameHud()->text_box_left) -
                (*MainGameHud()->text_box_font).textWidth(MainGameHud()->text_paint_scratch));
        AppendTextBoxLine(MainGameHud()->text_paint_scratch, 2);
        MainGame()->dialogue.trade_rows.push_back({W8TradeSource::Gold});
    }
    if (!MainGame()->dialogue.trade_pc_items) {
        for (unsigned int index = 0; index < CurrentGame().party.item_count; ++index) {
            W8ItemInstance* item = &CurrentGame().party.item_pool[index];
            bool acceptable = true;
            unsigned int font_palette;
            if (item->iItemNo == -1) {
                continue;
            }
            if (!NpcAcceptsTradeItem(CurrentGame().npc_interaction.dialogue_npc.get(), item) &&
                MainGame()->dialogue.trade_mode != W8_NPC_TRADE_GIVE) {
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
            MainGame()->dialogue.trade_rows.push_back(
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
            if (!NpcAcceptsTradeItem(CurrentGame().npc_interaction.dialogue_npc.get(), item) &&
                MainGame()->dialogue.trade_mode != W8_NPC_TRADE_GIVE) {
                font_palette = 0;
                acceptable = false;
            } else {
                font_palette = 0xf;
            }
            MainGame()->dialogue.trade_rows.push_back(
                {W8TradeSource::Backpack, CurrentGame().status.selected_character,
                 static_cast<int>(item - trading->backpack), *item});
            ShowNpcPlayerTradeItem(item, acceptable, font_palette);
        }
    }
    RepaintNpcPanel(1);
}

void OpenNpcTradeQuantityDialog(void)
{
    auto& panel = MainGame()->dialogue;
    if (!panel.trade_selection || panel.trade_selection->expected.stack_count < 2) return;
    const auto selection = *panel.trade_selection;
    const auto mode = panel.trade_mode;
    const auto npc = CurrentGame().npc_interaction.dialogue_npc;
    const int actor = CurrentGame().status.selected_character;
    const auto split_mode = mode == W8_NPC_TRADE_SELL ? W8_ITEM_SPLIT_SELL
                            : mode == W8_NPC_TRADE_BUY || mode == W8_NPC_TRADE_SHOPLIFT
                                ? W8_ITEM_SPLIT_BUY
                                : W8_ITEM_SPLIT_INVENTORY;
    auto dialog =
        std::make_unique<W8SplitItemDialog>(split_mode, &selection.expected, panel.trade_quantity,
                                            SDL_Point{kSplitDialogX, kSplitDialogY});
    dialog->SetText("");
    dialog->on_complete = [selection, mode, npc, actor](int count) {
        auto* screen = MainGame();
        if (!screen || count <= 0 || screen->dialogue.trade_mode != mode ||
            screen->dialogue.trade_selection != selection ||
            screen->game.npc_interaction.dialogue_npc != npc ||
            screen->game.status.selected_character != actor ||
            !ResolveTradeSelection(screen->game, npc.get(), selection))
            return;
        screen->dialogue.trade_quantity = count;
        UpdateNpcTradeSelection(screen->hud->selected_text_lines[2], 0, 0);
    };
    OpenModal(std::move(dialog));
}

void SelectNpcBuyMode(void)
{
    MainGame()->dialogue.trade_mode = W8_NPC_TRADE_BUY;
    UpdateNpcDialogueSubMode();
}

void RestockNpcTradeStock(void)
{
    RestockNpcInventory(CurrentGame().npc_interaction.dialogue_npc.get());
}

/* Highlight a clicked transcript keyword; clicking it again submits it to
   the dialogue input handler and clears its marked-entry palette. */

void W8NpcDialogueTextController::SelectTranscriptKeywordAtPoint(int x, int y)
{
    unsigned int hit;

    if (CurrentGame().npc_interaction.modal_dialog_open) {
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

// The game's step time when the surprise fade's current direction began.
static std::chrono::milliseconds g_surprise_fade_start;
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
    SetUiCanvasAlphaTestEnabled(false);
}

void CreateSurpriseFade(void)
{
    glm::vec4 color;

    SetFullscreenSceneLast(0);
    color = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    g_surprise_fade_node = makeColorOverlay(0x280, 0x1e0, color, true);
    positionOverlay(g_surprise_fade_node.get(), 0, 0, false);
    g_surprise_fade_node->material.blend = OverlayBlend::Fade;
    g_surprise_fade_node->color.a = 0.0;
    g_surprise_fade_start = CurrentGame().time;
    g_surprise_fade_in = true;
}

void ReverseSurpriseFade(void)
{
    if (SurpriseFadingIn()) {
        OverlayQuad* material = g_surprise_fade_node.get();
        float opacity = material->color.a;

        const auto now = CurrentGame().time;
        g_surprise_fade_in = !g_surprise_fade_in;
        const float resumed = g_surprise_fade_in ? 1.0f - opacity : opacity;
        g_surprise_fade_start =
            now + std::chrono::milliseconds(static_cast<int>(resumed * g_fade_resume_scale));
    } else {
        g_surprise_fade_node->visible = true;
        g_surprise_fade_start = CurrentGame().time;
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
    const auto now = CurrentGame().time;

    if (now - g_surprise_fade_start > std::chrono::milliseconds{500}) {
        if (!g_surprise_fade_in) {
            opacity = 0.0f;
            done = true;
        } else {
            boundary = true;
            opacity = 1.0f;
            g_surprise_fade_in = false;
            g_surprise_fade_start = now;
        }
    } else {
        opacity = static_cast<float>((now - g_surprise_fade_start).count()) * 0.002f;
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
            g_surprise_fade_start = CurrentGame().time;
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

W8Action* EditedDraft(int party_slot)
{
    if (party_slot != CurrentGame().status.selected_character) return nullptr;
    if (gpSCSV) return &gpSCSV->draft;
    return ItemUseDraft();
}

W8Action EditedAction(int party_slot)
{
    if (auto* draft = EditedDraft(party_slot)) return ParticipatingAction(party_slot, *draft);
    return SelectCombatAction(party_slot, CurrentGame().combat.has_value()
                                              ? W8_TARGETING_CONTEXT_IN_COMBAT
                                              : W8_TARGETING_CONTEXT_OUT_OF_COMBAT);
}

bool IsSighting(int party_slot)
{
    return !CurrentGame().combat.has_value() && !EditedDraft(party_slot);
}

namespace {
// The interaction controller evaluates a choice, commits its gameplay state,
// and then refreshes the presentation of that committed choice. A sighting aim
// commits without presentation.
void CommitAim(int actor, const W8Action& action, W8CombatSlot& stored_target, W8CombatSlot chosen,
               bool presented, bool remember)
{
    if (action.target != chosen) {
        stored_target = chosen;
        if (remember && chosen.iType != W8_TARGET_KIND_NONE)
            RememberActionTarget(CurrentGame().party.rows[actor].remembered, action.kind, chosen);
        if (presented) {
            RequestPartySlotRedraw(actor);
            if (actor == CurrentGame().status.selected_character) {
                if (auto* hud = MainGameHud()) {
                    hud->repaint.monster_list = true;
                    hud->repaint.character_action = true;
                }
            }
            if (auto& combat = CurrentGame().combat) combat->target_picked = false;
        }
    }
    if (chosen.iType != W8_TARGET_KIND_NONE && presented &&
        actor == CurrentGame().status.selected_character) {
        const auto edited_action = EditedAction(actor);
        const auto needed = IsSighting(actor)
                                ? W8_TARGET_NEED_NONE
                                : GetTargetNeededForAction(CurrentGame().party, edited_action);
        SetTargetingMode(TargetMatchesNeeded(&edited_action.target, needed) ? W8_TARGET_NEED_NONE
                                                                            : needed);
    }
}
} // namespace

void AimAtTarget(int actor, const W8CombatSlot& target)
{
    if (auto* draft = EditedDraft(actor)) {
        AimDraft(actor, *draft, target);
    } else {
        AimStoredAction(actor,
                        CurrentGame().combat.has_value() ? W8_TARGETING_CONTEXT_IN_COMBAT
                                                         : W8_TARGETING_CONTEXT_OUT_OF_COMBAT,
                        target);
    }
}

void AimStoredAction(int actor, W8TargetingContext stored, const W8CombatSlot& target)
{
    auto& row = CurrentGame().party.rows[actor];
    const bool committed = stored == W8_TARGETING_CONTEXT_IN_COMBAT;
    CommitAim(actor, SelectCombatAction(actor, stored),
              committed ? row.committed.target : row.executing.target, target, committed,
              committed && CurrentGame().combat.has_value());
}

void AimDraft(int actor, W8Action& draft, const W8CombatSlot& target)
{
    CommitAim(actor, ParticipatingAction(actor, draft), draft.target, target, true, false);
}

void SetTargetingMode(W8TargetNeed state)
{
    ScreenPointer().setTargeting(state);
    RequestRefreshPartyState();
    SetTargetConeEnabled(state == W8_TARGET_NEED_CONE);
}

void ConfigureSpellTargetFilter(W8SpellTargetType target_type, W8TargetNeed needed_kind,
                                W8Action* draft)
{
    W8CombatSlot target;
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
            target.iMonsterID = CurrentGame().npc_interaction.target_location_id;
            AimAtTarget(selected, target);
            needed_kind = W8_TARGET_NEED_NONE;
        } else if (g_settings.autotarget_spells != 0 && draft &&
                   RepickDraftTarget(selected, *draft)) {
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
    AimAtTarget(selected, target);
aim_done:
    SetTargetingMode(needed_kind);
}

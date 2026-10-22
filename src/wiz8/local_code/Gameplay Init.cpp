#include <algorithm>
#include <array>
#include <memory>
#include <string_view>

#include "wiz8/character_event_queue.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/fight.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/difficulty.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/FormationAndFacing.h"
#include "wiz8/local_code/GameplayInit.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_screens/OptionsScreen.h"
#include "wiz8/platform/timer.h"
#include "wiz8/render/quality.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/screen.h"
#include "wiz8/world/environment.h"
/* TU attribution follows the 2001-08-14 demo __FILE__ and link-order evidence. */

namespace {
}

/* The hand and the carried pool start empty, then take the six starting item stacks. */
Game& BeginNewGame(GameSession& games)
{
    static constexpr std::array<unsigned int, 6> starting_items = {0x14f, 0x14f, 0x15a,
                                                                   0x15a, 0x15b, 0x155};
    auto& game = games.begin();
    game.party.held.item = W8ItemInstance{.iItemNo = -1};
    std::ranges::fill(game.party.item_pool, W8ItemInstance{.iItemNo = -1});
    for (const auto id : starting_items) {
        auto item = MakeItem(Data(), CurrentGame().rng, id, true, true, true);
        item.stack_count = 1;
        AddItemToParty(game, OwnedItem(item), false, false);
    }
    game.party.gold = 500;
    const auto selected = game.party.firstSelectableSlot();
    game.status.selected_character = selected ? static_cast<int>(*selected) : -1;
    InitializePartyFormation(&game.party.formation);
    std::ranges::fill(game.party.order_slots, 0xffffffff);
    return game;
}

/* Raises three flags, optionally hands the caller's target to 0x005A9E70, then
   runs a fixed opening sequence. The two calls into 0x00482720 and 0x00482740
   share one stack cleanup, as consecutive cdecl calls do. */
void RunNewGameOpeningSequence(bool notify, std::string_view target)
{
    CurrentGame().status.game_started = true;
    if (!target.empty()) {
        CurrentGame().status.iron_man = true;
        g_options_last_save_name = target;
    }
    CurrentGame().status.difficulty = g_settings.difficulty;
    CurrentGame().party.redistribution_pending = true;
    SetGameTimeMilliseconds(0x2932e00);
    CurrentGame().status.game_time_days = 1;
    if (notify) {
        ReturnToPreviousScreen();
    }
    CreateNewGameNpcStates();
    RequestScreen({.id = W8_SCREEN_GAME_START_ROUTER});
}

void ResetTargetingState(void)
{
    unsigned int slot;

    for (slot = 0; slot < 8; ++slot) {
        ResetGameplaySlot(slot);
    }
    ResetTargetPicks();
}

// Start the portrait in its current condition's pose and schedule the first idle animation.
void ResetGameplaySlot(unsigned int slot)
{
    Game& game = CurrentGame();
    auto& record = game.monster_manager_entries[slot];
    record = {};
    record.portrait.pose = record.portrait.target_pose =
        game.party.characters[slot].highest_condition >= W8_CONDITION_ASLEEP ? 2 : 1;
    record.portrait.pose_clock = MonotonicMilliseconds();
    record.portrait.idle_clock = MonotonicMilliseconds() + game.rng.below(5000) + 5000;
    record.portrait_fx_clock = MonotonicMilliseconds();
    record.damage_splat_frame = -1;
    record.damage_splat_catalog = 0x90;
    record.effect_icon_catalog = -1;
    record.effect_icon_frame = -1;
    record.acting_portrait_pulse_clock = MonotonicMilliseconds();
}

/* The static initializer constructs the manager's entries and vector before
   this runs; the bulk reset below deliberately wipes them along with the
   neighbouring runtime state. That matches retail exactly, including the
   wiped container headers: every later use is non-virtual (Clear, GetCount,
   direct teardown of a null backing store), so no reconstruction runs. */

void ResetPartySlotRow(int slot)
{
    W8PartySlotRow* row = &CurrentGame().party.rows[slot];

    *row = {};
    CurrentGame().party.members[slot].emplace();
    row->remembered.spell = {W8_ACTION_CAST_SPELL, W8_SPELL_NONE};
    row->remembered.chooseBasic(W8_ACTION_ATTACK, -1);
    /* Retail cleared the row to zeros, which leaves a new member's committed
       action a detail-zero attack at an empty target and the remembered item
       use empty, rather than W8Action's "no action" default. */
    row->committed = {.kind = W8_ACTION_ATTACK, .detail = 0, .target = {}, .detail_block = {}};
    row->remembered.item_detail = {};
}

/* Both buffers are cleared only after both allocations succeed, so a failed
   second allocation leaves the first one live and unzeroed. */
/* Clears the settings block and writes its defaults. The constants 0, 1, 0x40
   and 0xff are each used many times over, which is why VC6 holds them in
   registers rather than spelling out immediates. */
void ResetGameplaySettings(void)
{
    g_settings = {};
    g_settings.sound_effects_volume = 0x40;
    g_settings.voice_volume = 0x40;
    g_settings.main_ui_mode = W8_MAIN_UI_MODE_FORMATION;
    g_settings.continuous_combat = 0;
    g_settings.auto_advance_character = 0;
    g_settings.tooltips_enabled = true;
    g_settings.formation_action_panel_preference = 1;
    g_settings.formation_radar_map_preference = 1;
    g_settings.formation_board_preference = 1;
    g_settings.portraits_action_panel_preference = 1;
    g_settings.field_000 = 0;
    g_settings.invert_mouse_y = 0;
    g_settings.field_03b = 0;
    g_settings.difficulty = W8_DIFFICULTY_NORMAL;
    g_settings.text_display_delay_ms = 0x9c4;
    g_settings.combat_delay_ms = 1000;
    g_settings.continuous_combat_start_delay_ms = 5000;
    g_settings.camera_rotation_mode = W8_CAMERA_ROTATION_ALL_TARGETS;
    g_settings.camera_rotation_style = W8_CAMERA_ROTATION_SMOOTH;
    g_settings.tooltip_delay_ms = 600;
    g_settings.music_volume = 0x1f;
    g_settings.footstep_volume = 0x13;
    g_settings.muted_sound_effects_volume = 0xff;
    g_settings.muted_music_volume = 0xff;
    g_settings.muted_voice_volume = 0xff;
    g_settings.field_035 = 0xff;
    g_settings.monster_movement_speed = 2.5f;
    g_settings.legacy_gamma = 1.0f;
    g_settings.pc_confirmations = 1;
    g_settings.pc_subtitles = 1;
    g_settings.mouselook_toggle = 0;
    g_settings.mouselook_smoothing = 1;
    g_settings.numeric_hit_points = 1;
    g_settings.auto_save = 0;
    g_settings.field_047 = 0;
    g_settings.monster_shadows = 1;
    g_settings.smooth_monster_animations = 1;
    g_settings.smooth_world_animations = 1;
    g_settings.skill_increase_messages = 1;
    g_settings.ctrl_right_click_info = 0;
    g_settings.autoswap_weapons = 1;
    g_settings.autotarget_spells = 1;
    g_settings.autoscroll_combat_messages = 0;
    g_settings.simplified_npc_interaction = 1;
    EnableAllRenderOptions();
}

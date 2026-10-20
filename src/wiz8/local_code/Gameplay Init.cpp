#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iterator>
#include <limits>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "asset_paths.h"
#include "compat/native.h"
#include "conditions.h"
#include "difficulty.h"
#include "layouts/main_game_screen.h"
#include "layouts/targeting.h"
#include "retail_records.h"
#include "spell_ids.h"
#include "wiz8/audio/mouth_gap.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/fact_state.h"
#include "wiz8/filesystem.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/item_instance.h"
#include "wiz8/layouts/party_slot.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/FormationAndFacing.h"
#include "wiz8/local_code/GameplayDatabase.h"
#include "wiz8/local_code/GameplayInit.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_screens/JournalScreen.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_screens/OptionsScreen.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/platform/random.h"
#include "wiz8/render/quality.h"
#include "wiz8/retail_text_records.h"
#include "wiz8/screen.h"
#include "wiz8/world/environment.h"
#include "wiz8/xstatus.h"
/* TU attribution follows the 2001-08-14 demo __FILE__ and link-order evidence. */

namespace {
}

/* NPC.DBS records carry an optional sub-list, stored after the record when its
   leading count exceeds one and its 0x9D flag is clear: a count, then that many
   six-byte elements appended to a freshly created list. */
unsigned char InitializeNpcDatabase(void)
try {
    auto handle =
        wiz8::open_file(wiz8::asset_path("Data\\Databases\\NPC.DBS"), wiz8::OpenMode::read);
    unsigned int count;
    handle->read_exact(&count, sizeof(count));
    if (count >
        (handle->size() - handle->tell()) / std::int64_t(wiz8::retail::kNpcDatabaseRecordBytes)) {
        return 0;
    }
    std::vector<W8NpcDatabaseRecord> records(count);
    for (unsigned int index = 0; index < count; ++index) {
        auto& record = records[index];
        wiz8::retail::read_exact(*handle, record);
        if (record.no_item_stock == 0 && record.version > 1) {
            unsigned int entry_count;
            handle->read_exact(&entry_count, sizeof(entry_count));
            const auto remaining = handle->size() - handle->tell();
            const auto following_records = std::int64_t(count - index - 1) *
                                           std::int64_t(wiz8::retail::kNpcDatabaseRecordBytes);
            if (remaining < following_records ||
                entry_count >
                    (remaining - following_records) / std::int64_t(sizeof(W8NpcItemStockRule)) ||
                entry_count > std::numeric_limits<int>::max()) {
                return 0;
            }
            record.item_stock_rules.resize(entry_count);
            for (auto& rule : record.item_stock_rules) handle->read_exact(&rule, sizeof(rule));
        }
    }
    g_npc_records = std::move(records);
    return 1;
} catch (const std::exception&) {
    return 0;
}

void DestroyNpcDatabase(void)
{
    g_npc_records = {};
}

unsigned char InitializeFactDatabase(void)
try {
    auto handle =
        wiz8::open_file(wiz8::asset_path("Data\\Databases\\FACT.DBS"), wiz8::OpenMode::read);
    g_fact_records =
        wiz8::retail::read_database<W8FactDatabaseRecord>(*handle, std::size(g_fact_values));
    return 1;
} catch (const std::exception&) {
    return 0;
}

void DestroyFactDatabase(void)
{
    g_fact_records = {};
}

unsigned char InitializeLevelDatabase(void)
try {
    auto handle =
        wiz8::open_file(wiz8::asset_path("Data\\Databases\\LEVELS.DBS"), wiz8::OpenMode::read);
    g_level_records = wiz8::retail::read_database<W8LevelDatabaseRecord>(*handle);
    return 1;
} catch (const std::exception&) {
    return 0;
}

void DestroyLevelDatabase(void)
{
    g_level_records = {};
}

/* Reset the status and its fixed eight-slot character banks. */
void ResetGameStatus(bool /*release*/)
{
    g_status = {};
}

/* Reset transient gameplay state and create its runtime resources at startup. */
void InitializeGameplayRuntimeObjects(void)
{
    gXStatus = {};
    gXStatus.character_event_queue = std::make_unique<W8CharacterEventQueue>();
    gXStatus.gameplay_timer = std::make_unique<W8GameTimer>(300.0f, false);
}

/* Loads Data\\Databases\\SpellTables.dbs, replacing whatever is already there.
   The header is two dwords: an allocation count and the number of rows to read.
   They are not the same number, and the allocation is sized from the first while
   the loop runs over the second, which is what the original does.

   Each row is preceded by 0x101 bytes the loader skips rather than reads. A
   failure anywhere stops the loop and drops the whole table, but the file is
   closed and the row count published either way - including on failure, where
   the count then describes a table that is no longer there. Preserved as found.

   The assertion at Spells.cpp:1908 names the table s_pSpellTable, which is why
   this body treats the global as the table itself rather than as a cursor. */
void ResetGameplayStatusBlock(void)
{
    memset(gXStatus.spell_cooldown_clocks, 0, sizeof(gXStatus.spell_cooldown_clocks));
    gXStatus.character_event_queue->DestroyAllEvents();
    gXStatus.party_moving = false;
    gXStatus.fSurprisePossible = false;
}

void DestroyGameplayObjects(void)
{
    gXStatus.character_event_queue.reset();
    for (auto& slot : gXStatus.monster_manager_entries) slot.mouth_gap = {};
    gXStatus.gameplay_timer.reset();
}

/* The new-game reset. ResetGameStatus replaces the status buffers, then this
   clears the item in hand and the carried pool, and
   grants the starting items. The pool and the id list are both walked by
   address against the symbol that follows them, not by index. */
void ResetForNewGame(void)
{
    W8ItemInstance item;
    W8ItemInstance* slot;
    unsigned int* id;
    unsigned int index;

    ResetGameStatus(true);
    ReleaseMessageStorage();
    *(&g_status.party.held.item) = W8ItemInstance{.iItemNo = -1};
    slot = g_status.party.item_pool;
    do {
        *(slot) = W8ItemInstance{.iItemNo = -1};
        ++slot;
    } while (slot < g_status.party.item_pool + 500);
    id = g_starting_item_ids;
    do {
        if (*id != 0xffffffff) {
            item = MakeItem(*id, true, true, true);
            item.stack_count = 1;
            AddItemToParty(&item, false, false);
        }
        ++id;
    } while (id < g_starting_item_ids + 6);
    g_status.party.gold = 500;
    g_status.selected_character = GetNextCharacter(1, 1, -1);
    InitializePartyFormation(&g_status.party.formation);
    for (index = 0; index < 8; ++index) {
        g_status.party.order_slots[index] = 0xffffffff;
    }
}

/* Raises three flags, optionally hands the caller's target to 0x005A9E70, then
   runs a fixed opening sequence. The two calls into 0x00482720 and 0x00482740
   share one stack cleanup, as consecutive cdecl calls do. */
void RunNewGameOpeningSequence(bool notify, std::string_view target)
{
    g_status.game_started = true;
    if (!target.empty()) {
        g_status.iron_man = true;
        g_options_last_save_name = target;
    }
    g_status.difficulty = g_settings.difficulty;
    g_status.party.redistribution_pending = true;
    SetGameTimeMilliseconds(0x2932e00);
    g_status.game_time_days = 1;
    if (notify) {
        ReturnToPreviousScreen();
    }
    ResetNpcStates();
    InitializeFactJournal();
    ResetFactions();
    ResetMainScreenStateBlock();
    RequestScreen({.id = W8_SCREEN_GAME_START_ROUTER});
}

void ResetTargetingState(void)
{
    unsigned int slot;

    for (slot = 0; slot < 8; ++slot) {
        ResetGameplaySlot(slot);
    }
    gXStatus.picked_monster = -1;
    gXStatus.picked_group = -1;
}

/* Resets one 0x118-byte slot. The tier it stores twice comes from the character
   the slot belongs to: the status block's first buffer is an array of
   W8Character at the 0x1862 stride, and the field at 0x0b01 - the same one
   party-member selection thresholds against 0x12 and 0x0f - decides between 1
   and 2 here. The five countdown clocks and the Random call share one stack
   cleanup, as consecutive cdecl calls do. */
void ResetGameplaySlot(unsigned int slot)
{
    W8MonsterManagerEntry* record = &gXStatus.monster_manager_entries[slot];
    int tier;

    *record = {};
    record->portrait_event_active = false;
    record->voice_sound_handle = nullptr;
    record->portrait.rendered_frame = -1;
    record->portrait.frame = 6;
    record->portrait.blink_closing = false;
    tier = 1;
    if (g_status.party.characters[slot].highest_condition >= W8_CONDITION_ASLEEP) {
        tier = 2;
    }
    record->portrait.pose = tier;
    record->portrait.target_pose = tier;
    record->portrait.rendered_pose = -1;
    record->portrait.pose_dirty = false;
    record->portrait.frame_dirty = false;
    record->portrait.frame_clock = w8_get_ticks() + (0);
    record->portrait.voice_remaining_ms = 0;
    record->portrait.pose_clock = w8_get_ticks() + (0);
    record->portrait.idle_clock = w8_get_ticks() + (Random(5000) + 5000);
    record->portrait_fx_clock = w8_get_ticks() + (0);
    record->damage_splat_active = false;
    record->damage_splat_death_variant = false;
    record->dead_portrait_revealed = false;
    record->damage_splat_amount = 0;
    record->damage_splat_frame = -1;
    record->damage_splat_end_frame = 0;
    record->damage_splat_catalog = 0x90;
    record->effect_icon_active = false;
    record->effect_icon_catalog = -1;
    record->effect_icon_frame = -1;
    record->effect_icon_end_frame = 0;
    record->active_character_event = 0;
    record->portrait_stats_dirty = false;
    record->cached_hp_bar = 0;
    record->cached_stamina_bar = 0;
    record->cached_hp = 0;
    record->cached_spell_bar = 0;
    record->portrait_refresh_pinned = false;
    record->auto_portrait_refresh = false;
    record->keyboard_menu_open = false;
    record->combat_portrait_dirty = false;
    record->acting_portrait_pulse = 0;
    record->level_up_ready = false;
    record->acting_portrait_pulse_clock = w8_get_ticks() + (0);
}

/* The static initializer constructs the manager's entries and vector before
   this runs; the bulk reset below deliberately wipes them along with the
   neighbouring runtime state. That matches retail exactly, including the
   wiped container headers: every later use is non-virtual (Clear, GetCount,
   direct teardown of a null backing store), so no reconstruction runs. */

void ResetPartySlotRow(int slot)
{
    W8PartySlotRow* row = &g_status.party.rows[slot];

    memset(row, 0, sizeof(W8PartySlotRow));
    row->fOccupied = true;
    row->spell = {W8_ACTION_CAST_SPELL, W8_SPELL_NONE};
    row->queued_action = W8_ACTION_NONE;
    SetSlotAction(slot, W8_ACTION_ATTACK, -1);
}

/* Both buffers are cleared only after both allocations succeed, so a failed
   second allocation leaves the first one live and unzeroed. */
/* Clears the settings block and writes its defaults. The constants 0, 1, 0x40
   and 0xff are each used many times over, which is why VC6 holds them in
   registers rather than spelling out immediates. */
void ResetGameplaySettings(void)
{
    memset(&g_settings, 0, sizeof(g_settings));
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
    DisableRenderOption(W8_RENDER_OPTION_CORRECT_BLURRED_TEXT);
}

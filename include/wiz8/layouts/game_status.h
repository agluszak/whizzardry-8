#ifndef WIZ8_LAYOUTS_GAME_STATUS_H
#define WIZ8_LAYOUTS_GAME_STATUS_H

#include <cstdint>
#include <string>

#include "wiz8/difficulty.h"

#include "wiz8/gameplay_modifiers.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/item_instance.h"
#include "wiz8/layouts/levels.h"
#include "wiz8/layouts/party_formation.h"
#include "wiz8/layouts/world.h"

#include <stddef.h>

struct W8PartySlotRow;

struct W8Character;

enum { W8_PARTY_SLOT_COUNT = 8 };

/* The status block's version and the process-owned party buffers it points
   at; the pointers are never saved. */
struct W8StatusBuffers {
    float save_version;
    W8Character* Char;
    W8PartySlotRow* XChar;
};

enum { W8_CHARACTER_SERIALIZED_SIZE = 0x1862 };

struct W8ItemSpellUsageRecord {
    std::uint32_t cast_count;
    std::uint32_t usable_cast_count;
    std::uint8_t unknown_08[8];
};

struct W8CharacterSpellUsageRecord {
    std::uint8_t unknown_00[4];
    std::uint32_t cast_count;
    std::uint32_t usable_cast_count;
    std::uint8_t unknown_0c[4];
};

struct W8GlobalStatus {
    W8StatusBuffers buffers;
    bool game_started; /* Retail offset 0x000c */
    /* Retail offset 0x000d..0x0018: the three join counters the party-add entry advances:
       the regular-member, auxiliary and total counts. */
    std::int32_t regular_member_count;
    std::int32_t auxiliary_member_count;
    std::int32_t total_member_count;
    std::uint32_t party_gold;
    std::int32_t selected_character;
    W8ItemInstance party_item_pool[500];
    std::uint32_t party_item_count;
    /* Retail offset 0x1795: text-box line cursor. */
    std::int16_t text_line_cursor;
    std::uint32_t legacy_text_box_lines[2][3];
    /* Retail offset 0x17af: the party's twelve effect slots, the same 0x11-byte records the
       monster and combat tables hold. The trailing run is opaque. */
    W8EffectSlot effect_slots[12];
    std::uint8_t unknown_187b[0x55];
    std::int32_t party_facing;
    std::uint32_t party_heading;
    std::int32_t world_clock;
    std::uint32_t world_clock_ms;
    std::uint32_t party_order_slots[8];
    std::int32_t current_level;
    /* Retail offset 0x1904..0x1a03: the 0x100-byte STAT header block. */
    std::uint8_t status_header_prefix[0xd4];
    std::uint32_t uiTurnsElapsed; /* Retail offset 0x19d8 */
    std::uint8_t status_header_suffix[0x28];
    W8LevelProgressRow level_progress[W8_LEVEL_COUNT];
    std::uint8_t unknown_2013[0x294];
    /* Retail offset 0x22a7: CamPos staged by recall when the anchor is on another level;
       LoadLevel restores it after the new world exists. */
    W8WorldCameraState pending_move_location;
    /* Retail offset 0x22e3: the party-wide modifier block the effect rebuild clears and
       refills. Its +0x4a flag is the light gate the monster-sight threshold
       pass reads. */
    W8GameplayModifierBlock party_modifiers;
    std::int32_t next_group_id;
    std::int32_t next_monster_location_id;
    std::int32_t next_world_item_id;
    std::int32_t next_trigger_id;
    bool item_in_cursor;
    W8ItemInstance item_in_hand;
    /* Retail offset 0x2367: per-slot flags the character-load path consults. */
    std::uint8_t flags[0x20];
    std::uint32_t game_time_ms;
    std::uint32_t aging_accumulator;
    /* Retail offset 0x238f: search mode toggle. Mirrors the submenu search button, slows
       party movement, and scales the monster-sight threshold while set. */
    std::uint8_t search_mode;
    /* Retail offset 0x2390: cleared by the main-game frame; HP/SP and condition updates
       skip work while it is set, and encounter culling treats it as the
       force-despawn gate. */
    bool world_suspended;
    /* Retail offset 0x2391/0x2395: session accumulators ConsumeLevelElapsedTime
       folds the level's pending elapsed times into; the aging tick's wait
       pass sums them against zero. */
    float real_elapsed;
    float frame_elapsed;
    std::uint32_t wait_state;
    std::uint32_t item_recharge_ms;
    W8PartyFormationState formation;
    std::int32_t game_time_days;
    bool iron_man;
    /* Retail offset 0x242a: world-clock stamp of the last NPC-binding reset; the event
       pass waits 0x3c ticks past it. */
    std::int32_t binding_reset_clock;
    /* Retail offset 0x242e: binding-reset grace period in effect; cleared once the sweep
       runs after the clock elapses. */
    bool binding_reset_pending;
    std::uint8_t padding_242f;
    /* Retail offset 0x2430: one-shot gate for the NPC event pass. */
    bool npc_restore_pending;
    /* Retail offset 0x2431: raised by the .nsf quote audit while it runs; ShowNotice counts
       each notice's wrapped lines under it. */
    std::uint8_t quote_audit;
    /* Retail offset 0x2432: ShowNotice sets it under quote_audit when a notice wraps
       past seven lines; the audit reports those as "Long Quote". */
    std::uint8_t long_quote;
    bool party_fatigued;
    /* Retail offset 0x2434: index of the party member the main-game selection flow is on.
       The screen reset writes 0xff. */
    std::uint8_t selected_party_member;
    /* Retail offset 0x2435: read as a gate by the main-game frame's world-cursor path. */
    std::uint8_t world_cursor_gate;
    std::uint32_t camp_tick_ms;
    /* Retail offset 0x243a: the five RPC race ids AssayDialog walks as NUM_RPC_RACES. */
    std::uint8_t rpc_races[5];
    std::uint8_t unknown_243f[5];
    /* Character creation skips the loose CHR collision check when set. */
    std::uint8_t skip_loose_character_check;
    /* Retail offset 0x2445: latched once the Trynnie2 Zulu/0x1c3 use-item action has been
       handled at a cursor node; later uses take the Mystical Shaman branch. */
    bool use_item_latch;
    bool infatuation_pending;
    W8Difficulty difficulty;
    /* Retail offset 0x244b: the save file's creation-time pair XOR-masked by SaveGame's
       two data constants; both halves are written as dwords. */
    std::uint32_t save_filetime_xor[2];
    std::string monster_name_buffer;
    /* Retail offset 0x247f: party slot selected by the Sedexus path before rpc_active
       is armed; later capture, fact and death handling reuse the same slot. */
    std::int32_t sedexus_party_slot;
    std::uint32_t stamina_tick_ms;
    std::uint8_t condition13_clock;
    /* Retail offset 0x2488: one-shot gate; when set, the next condition-change and
       condition-cleared reaction is swallowed and the flag cleared. */
    std::uint8_t skip_next_condition_reaction;
    bool rpc_active; /* Retail offset 0x2489: fact 0x14c gate */
    /* Retail offset 0x248a: armed by the long NPC reward event; the event also stamps
       0x2493 with the world clock. */
    bool fact_b8_pending;
    std::uint32_t condition13_stamp;
    std::int32_t pending_condition_party_slot;
    std::int32_t fact_b8_clock;
    bool greeting_pending;
    std::uint32_t camp_fatigue_count;
    /* Retail offset 0x249c: party slot fact 0x39 hands to RemoveCharacterCondition. */
    std::int32_t party_slot;
    /* Retail offset 0x24a0: per-spell 0x10-byte stat records; TrackItemSpellSource
       walks records[0..149] bumping usable_cast_count for spells the character
       carries and cast_count for the selected source spell. */
    W8ItemSpellUsageRecord item_spell_usage[200];
    std::uint8_t log_fact_checks;
    std::int32_t status_ints[1000];
    bool fact_88_latch;
    std::uint8_t unknown_40c2[0xc];
    /* Indexed by spell_id - 1, for the player spells 1..0x71. */
    W8CharacterSpellUsageRecord spell_usage[0x71];
    std::uint8_t unknown_47de[0x194];
    /* Retail offset 0x4972: set once the Cosmic Circle arena monsters have been spawned by
       the level-4 setup; the setup skips its work while this or world_suspended
       holds. */
    bool cc_arena_spawned;
    /* Retail offset 0x4973/0x4977: w8_get_ticks stamps. NpcScriptSavantHackDone writes the
       first; UpdateNpcEvents retires NPC 0x1b3 fifty ticks later and starts
       the second, which gates monster group 0x1b6's Bela cycle after five
       seconds. */
    std::int32_t savant_hack_tick;
    std::int32_t bela_cycle_tick;
    std::uint8_t unknown_497b[4];
    /* Retail offset 0x497f: combat difficulty band counters, indexed by EvaluateCombatDifficulty. */
    std::uint32_t combat_difficulty_counts[3];
    /* Retail offset 0x498b: NPC group event counter, cleared once the group event runs. */
    std::int32_t vi_event_stage;
    /* Retail offset 0x498f/0x4993: pending-stage flags set by FACT_QUE_ENDGAME2/3; the book
       callback queues ENDGAME2's script notice while either holds. */
    std::int32_t endgame2_queued;
    std::int32_t endgame3_queued;
    std::uint32_t text_box_lines_used[4];
    std::uint32_t text_box_lines_shown[4];
    /* Retail offset 0x49b7: world-clock stamp the 0x49bb reward event compares against. */
    std::int32_t trang_check_clock;
    bool trang_check_pending;
    bool intro_shown;
    bool flag;
    std::uint8_t padding_49be[2];
    /* Retail offset 0x49c0: set when the endgame transition starts; saves carrying either
       this or flag are filtered from the load list. */
    bool endgame_started;
    /* Retail offset 0x49c1: latched while g_dev_mode is set at teardown; persisted into
       the save slot as dev_flagged. */
    bool dev_flagged;
};



extern W8GlobalStatus g_status;


#endif

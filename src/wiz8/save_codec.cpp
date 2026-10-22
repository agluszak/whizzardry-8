#include "wiz8/save_codec.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "wiz8/checked_integer.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/records.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/difficulty.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/levels.h"
#include "wiz8/layouts/npc_dialogue.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/message_log.h"
#include "wiz8/retail_opaque_records.h"
#include "wiz8/retail_records.h"
#include "wiz8/retail_save_records.h"
#include "wiz8/retail_text_records.h"
#include "wiz8/rules/action_choices.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_formation.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/story.h"
#include "wiz8/unicode.h"

namespace wiz8::retail {
namespace {

#pragma pack(push, 1)
struct ItemSpellUsage {
    std::uint32_t cast_count;
    std::uint32_t usable_cast_count;
    std::uint8_t unknown_08[8];
};
static_assert(sizeof(ItemSpellUsage) == 0x10);
struct SpellUsage {
    std::uint8_t unknown_00[4];
    std::uint32_t cast_count;
    std::uint32_t usable_cast_count;
    std::uint8_t unknown_0c[4];
};
static_assert(sizeof(SpellUsage) == 0x10);
/* The status block's head: its version and two pointer words retail stored
   but never used after loading. */
struct StatusBuffers {
    float save_version;
    std::uint32_t characters;
    std::uint32_t party_rows;
};
static_assert(sizeof(StatusBuffers) == 0x0c);
struct LevelProgress {
    std::uint8_t visited;
    std::uint16_t combat_end_count;
    std::int16_t monster_kill_count;
    std::uint32_t experience_gained;
    std::int32_t gold_collected;
    std::int32_t sight_clock;
    std::uint8_t unknown_11[0x10];
};
static_assert(sizeof(LevelProgress) == 0x21);

struct GlobalStatus {
    StatusBuffers buffers;
    bool game_started; /* 0x000c */
    /* 0x000d..0x0018: the three join counters the party-add entry advances:
       the regular-member, auxiliary and total counts. */
    int regular_member_count;
    int auxiliary_member_count;
    int total_member_count;
    unsigned int party_gold;
    int selected_character;
    ItemInstance party_item_pool[500];
    unsigned int party_item_count;
    /* 0x1795: text-box line cursor. */
    short text_line_cursor;
    unsigned int legacy_text_box_lines[2][3];
    /* 0x17af: the party's twelve effect slots, the same 0x11-byte records the
       monster and combat tables hold. The trailing run is opaque. */
    EffectSlot effect_slots[12];
    unsigned char unknown_187b[0x55];
    int party_facing;
    unsigned int party_heading;
    int world_clock;
    unsigned int world_clock_ms;
    unsigned int party_order_slots[8];
    int current_level;
    /* 0x1904..0x1a03: the 0x100-byte STAT header block. */
    unsigned char status_header_prefix[0xd4];
    unsigned int uiTurnsElapsed; /* 0x19d8 */
    unsigned char status_header_suffix[0x28];
    LevelProgress level_progress[W8_LEVEL_COUNT];
    unsigned char unknown_2013[0x294];
    /* 0x22a7: CamPos staged by recall when the anchor is on another level;
       LoadLevel restores it after the new world exists. */
    CameraState pending_move_location;
    /* 0x22e3: the party-wide modifier block the effect rebuild clears and
       refills. Its +0x4a flag is the light gate the monster-sight threshold
       pass reads. */
    std::array<std::byte, 0x67> party_modifiers;
    int next_group_id;
    int next_monster_location_id;
    int next_world_item_id;
    int next_trigger_id;
    bool item_in_cursor;
    ItemInstance item_in_hand;
    /* 0x2367: per-slot flags the character-load path consults. */
    unsigned char flags[0x20];
    unsigned int game_time_ms;
    unsigned int aging_accumulator;
    /* 0x238f: search mode toggle. Mirrors the submenu search button, slows
       party movement, and scales the monster-sight threshold while set. */
    unsigned char search_mode;
    /* 0x2390: cleared by the main-game frame; HP/SP and condition updates
       skip work while it is set, and encounter culling treats it as the
       force-despawn gate. */
    bool world_suspended;
    /* 0x2391/0x2395: session accumulators ConsumeLevelElapsedTime
       folds the level's pending elapsed times into; the aging tick's wait
       pass sums them against zero. */
    float real_elapsed;
    float frame_elapsed;
    std::uint32_t wait_state;
    unsigned int item_recharge_ms;
    PartyFormation formation;
    int game_time_days;
    bool iron_man;
    /* 0x242a: world-clock stamp of the last NPC-binding reset; the event
       pass waits 0x3c ticks past it. */
    int binding_reset_clock;
    /* 0x242e: binding-reset grace period in effect; cleared once the sweep
       runs after the clock elapses. */
    bool binding_reset_pending;
    unsigned char padding_242f;
    /* 0x2430: one-shot gate for the NPC event pass. */
    bool npc_restore_pending;
    /* 0x2431: raised by the .nsf quote audit while it runs; ShowNotice counts
       each notice's wrapped lines under it. */
    unsigned char quote_audit;
    /* 0x2432: ShowNotice sets it under quote_audit when a notice wraps
       past seven lines; the audit reports those as "Long Quote". */
    unsigned char long_quote;
    bool party_fatigued;
    /* 0x2434: index of the party member the main-game selection flow is on.
       The screen reset writes 0xff. */
    unsigned char selected_party_member;
    /* 0x2435: read as a gate by the main-game frame's world-cursor path. */
    unsigned char world_cursor_gate;
    unsigned int camp_tick_ms;
    /* 0x243a: the five RPC race ids AssayDialog walks as NUM_RPC_RACES. */
    unsigned char rpc_races[5];
    unsigned char unknown_243f[5];
    /* Character creation skips the loose CHR collision check when set. */
    unsigned char skip_loose_character_check;
    /* 0x2445: latched once the Trynnie2 Zulu/0x1c3 use-item action has been
       handled at a cursor node; later uses take the Mystical Shaman branch. */
    bool use_item_latch;
    bool infatuation_pending;
    W8Difficulty difficulty;
    /* 0x244b: the save file's creation-time pair XOR-masked by SaveGame's
       two data constants; both halves are written as dwords. */
    unsigned int save_filetime_xor[2];
    std::array<std::byte, 44> monster_name_buffer;
    /* 0x247f: party slot selected by the Sedexus path before rpc_active
       is armed; later capture, fact and death handling reuse the same slot. */
    int sedexus_party_slot;
    unsigned int stamina_tick_ms;
    unsigned char condition13_clock;
    /* 0x2488: one-shot gate; when set, the next condition-change and
       condition-cleared reaction is swallowed and the flag cleared. */
    unsigned char skip_next_condition_reaction;
    bool rpc_active; /* 0x2489: fact 0x14c gate */
    /* 0x248a: armed by the long NPC reward event; the event also stamps
       0x2493 with the world clock. */
    bool fact_b8_pending;
    unsigned int condition13_stamp;
    int pending_condition_party_slot;
    int fact_b8_clock;
    bool greeting_pending;
    unsigned int camp_fatigue_count;
    /* 0x249c: party slot fact 0x39 hands to LiftCondition. */
    int party_slot;
    /* 0x24a0: per-spell 0x10-byte stat records; TrackItemSpellSource
       walks records[0..149] bumping usable_cast_count for spells the character
       carries and cast_count for the selected source spell. */
    ItemSpellUsage item_spell_usage[200];
    unsigned char log_fact_checks;
    int status_ints[1000];
    bool fact_88_latch;
    unsigned char unknown_40c2[0xc];
    /* Indexed by spell_id - 1, for the player spells 1..0x71. */
    SpellUsage spell_usage[0x71];
    unsigned char unknown_47de[0x194];
    /* 0x4972: set once the Cosmic Circle arena monsters have been spawned by
       the level-4 setup; the setup skips its work while this or world_suspended
       holds. */
    bool cc_arena_spawned;
    /* 0x4973/0x4977: GetTickCount stamps. NpcScriptSavantHackDone writes the
       first; UpdateNpcEvents retires NPC 0x1b3 fifty ticks later and starts
       the second, which gates monster group 0x1b6's Bela cycle after five
       seconds. */
    int savant_hack_tick;
    int bela_cycle_tick;
    unsigned char unknown_497b[4];
    /* 0x497f: combat difficulty band counters, indexed by EvaluateCombatDifficulty. */
    unsigned int combat_difficulty_counts[3];
    /* 0x498b: NPC group event counter, cleared once the group event runs. */
    int vi_event_stage;
    /* 0x498f/0x4993: pending-stage flags set by FACT_QUE_ENDGAME2/3; the book
       callback queues ENDGAME2's script notice while either holds. */
    int endgame2_queued;
    int endgame3_queued;
    unsigned int text_box_lines_used[4];
    unsigned int text_box_lines_shown[4];
    /* 0x49b7: world-clock stamp the 0x49bb reward event compares against. */
    int trang_check_clock;
    bool trang_check_pending;
    bool intro_shown;
    bool flag;
    unsigned char padding_49be[2];
    /* 0x49c0: set when the endgame transition starts; saves carrying either
       this or flag are filtered from the load list. */
    bool endgame_started;
    /* 0x49c1: retail's developer-mode latch. Never set here; kept so saves
       round-trip the byte. */
    bool dev_flagged;
};
static_assert(std::is_trivially_copyable_v<GlobalStatus>);
static_assert(offsetof(GlobalStatus, status_header_suffix) +
                      sizeof(GlobalStatus::status_header_suffix) -
                      offsetof(GlobalStatus, status_header_prefix) ==
                  0x100,
              "GlobalStatus_status_header_size");
static_assert(offsetof(GlobalStatus, party_gold) == 0x19, "GlobalStatus_party_gold_offset");
static_assert(offsetof(GlobalStatus, selected_character) == 0x1d,
              "GlobalStatus_selected_character_offset");
static_assert(offsetof(GlobalStatus, party_item_pool) == 0x21,
              "GlobalStatus_party_item_pool_offset");
static_assert(offsetof(GlobalStatus, party_item_count) == 0x1791,
              "GlobalStatus_party_item_count_offset");
static_assert(offsetof(GlobalStatus, text_line_cursor) == 0x1795,
              "GlobalStatus_text_line_cursor_offset");
static_assert(offsetof(GlobalStatus, flags) == 0x2367, "GlobalStatus_flags_offset");
static_assert(offsetof(GlobalStatus, party_facing) == 0x18d0, "GlobalStatus_party_facing_offset");
static_assert(offsetof(GlobalStatus, current_level) == 0x1900, "GlobalStatus_current_level_offset");
static_assert(offsetof(GlobalStatus, status_header_prefix) == 0x1904,
              "GlobalStatus_status_header_prefix_offset");
static_assert(offsetof(GlobalStatus, uiTurnsElapsed) == 0x19d8,
              "GlobalStatus_uiTurnsElapsed_offset");
static_assert(offsetof(GlobalStatus, status_header_suffix) == 0x19dc,
              "GlobalStatus_status_header_suffix_offset");
static_assert(offsetof(GlobalStatus, level_progress) == 0x1a04,
              "GlobalStatus_level_progress_offset");
static_assert(offsetof(GlobalStatus, iron_man) == 0x2429, "GlobalStatus_iron_man_offset");
static_assert(offsetof(GlobalStatus, world_cursor_gate) == 0x2435,
              "GlobalStatus_value_2435_offset");
static_assert(offsetof(GlobalStatus, pending_move_location) == 0x22a7,
              "GlobalStatus_pending_move_location_offset");
static_assert(offsetof(GlobalStatus, formation) == 0x23a1, "GlobalStatus_formation_offset");
static_assert(offsetof(GlobalStatus, world_suspended) == 0x2390,
              "GlobalStatus_world_suspended_offset");
static_assert(offsetof(GlobalStatus, skip_loose_character_check) == 0x2444,
              "GlobalStatus_skip_loose_character_check_offset");
static_assert(offsetof(GlobalStatus, use_item_latch) == 0x2445,
              "GlobalStatus_use_item_latch_offset");
static_assert(offsetof(GlobalStatus, selected_party_member) == 0x2434,
              "GlobalStatus_selected_party_member_offset");
static_assert(offsetof(GlobalStatus, rpc_races) == 0x243a, "GlobalStatus_rpc_races_offset");
static_assert(offsetof(GlobalStatus, monster_name_buffer) == 0x2453,
              "GlobalStatus_monster_name_buffer_offset");
static_assert(offsetof(GlobalStatus, sedexus_party_slot) == 0x247f,
              "GlobalStatus_sedexus_party_slot_offset");
static_assert(offsetof(GlobalStatus, greeting_pending) == 0x2497, "GlobalStatus_flag_2497_offset");
static_assert(offsetof(GlobalStatus, next_group_id) == 0x234a, "GlobalStatus_next_group_id_offset");
static_assert(offsetof(GlobalStatus, log_fact_checks) == 0x3120,
              "GlobalStatus_log_fact_checks_offset");
static_assert(offsetof(GlobalStatus, text_box_lines_shown) == 0x49a7,
              "GlobalStatus_text_box_lines_shown_offset");
static_assert(offsetof(GlobalStatus, intro_shown) == 0x49bc, "GlobalStatus_intro_shown_offset");
static_assert(offsetof(GlobalStatus, text_box_lines_used) == 0x4997,
              "GlobalStatus_migrated_values_offset");
static_assert(offsetof(GlobalStatus, rpc_active) == 0x2489, "GlobalStatus_flag_2489_offset");
static_assert(offsetof(GlobalStatus, status_ints) == 0x3121, "GlobalStatus_status_ints_offset");
static_assert(offsetof(GlobalStatus, fact_88_latch) == 0x40c1, "GlobalStatus_fact_88_latch_offset");
static_assert(offsetof(GlobalStatus, spell_usage) == 0x40ce, "GlobalStatus_spell_usage_offset");
static_assert(offsetof(GlobalStatus, vi_event_stage) == 0x498b, "GlobalStatus_value_498b_offset");
static_assert(offsetof(GlobalStatus, endgame2_queued) == 0x498f, "GlobalStatus_value_498f_offset");
static_assert(offsetof(GlobalStatus, endgame3_queued) == 0x4993, "GlobalStatus_value_4993_offset");
static_assert(offsetof(GlobalStatus, party_slot) == 0x249c, "GlobalStatus_party_slot_offset");
static_assert(offsetof(GlobalStatus, regular_member_count) == 0x000d,
              "GlobalStatus_regular_member_count_offset");
static_assert(offsetof(GlobalStatus, auxiliary_member_count) == 0x0011,
              "GlobalStatus_auxiliary_member_count_offset");
static_assert(offsetof(GlobalStatus, total_member_count) == 0x0015,
              "GlobalStatus_total_member_count_offset");
static_assert(offsetof(GlobalStatus, party_gold) == 0x0019, "GlobalStatus_party_gold_offset");
static_assert(sizeof(GlobalStatus) == 0x49c2, "GlobalStatus_must_be_0x49c2");

/* The STAT record. Only the leading counters and the status block are established. */
struct StatusHeader {
    float version;
    std::int32_t next_group_id;
    std::int32_t next_monster_location_id;
    std::int32_t next_world_item_id;
    std::int32_t next_trigger_id;
    std::uint8_t status_prefix[0xd4];
    std::uint32_t turns_elapsed;
    std::uint8_t status_suffix[0x28];
    std::uint8_t unknown_114[0x200];
};
static_assert(sizeof(StatusHeader) == 0x314);
static_assert(offsetof(StatusHeader, turns_elapsed) == 0x14 + 0xd4);
#pragma pack(pop)

constexpr float kStatusHeaderVersion = 2.0f;

} // namespace

template<> struct Record<W8ItemSpellUsageRecord> {
    using Disk = ItemSpellUsage;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text)
    {
        value(&runtime.cast_count,
              bytes.subspan(offsetof(Disk, cast_count), sizeof(Disk::cast_count)));
        value(&runtime.usable_cast_count,
              bytes.subspan(offsetof(Disk, usable_cast_count), sizeof(Disk::usable_cast_count)));
        value(&runtime.unknown_08,
              bytes.subspan(offsetof(Disk, unknown_08), sizeof(Disk::unknown_08)));
    }
};
template<> struct Record<W8CharacterSpellUsageRecord> {
    using Disk = SpellUsage;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text)
    {
        value(&runtime.unknown_00,
              bytes.subspan(offsetof(Disk, unknown_00), sizeof(Disk::unknown_00)));
        value(&runtime.cast_count,
              bytes.subspan(offsetof(Disk, cast_count), sizeof(Disk::cast_count)));
        value(&runtime.usable_cast_count,
              bytes.subspan(offsetof(Disk, usable_cast_count), sizeof(Disk::usable_cast_count)));
        value(&runtime.unknown_0c,
              bytes.subspan(offsetof(Disk, unknown_0c), sizeof(Disk::unknown_0c)));
    }
};
template<> struct Record<W8LevelProgressRow> {
    using Disk = LevelProgress;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text)
    {
        value(&runtime.visited, bytes.subspan(offsetof(Disk, visited), sizeof(Disk::visited)));
        value(&runtime.combat_end_count,
              bytes.subspan(offsetof(Disk, combat_end_count), sizeof(Disk::combat_end_count)));
        value(&runtime.monster_kill_count,
              bytes.subspan(offsetof(Disk, monster_kill_count), sizeof(Disk::monster_kill_count)));
        value(&runtime.experience_gained,
              bytes.subspan(offsetof(Disk, experience_gained), sizeof(Disk::experience_gained)));
        value(&runtime.gold_collected,
              bytes.subspan(offsetof(Disk, gold_collected), sizeof(Disk::gold_collected)));
        value(&runtime.sight_clock,
              bytes.subspan(offsetof(Disk, sight_clock), sizeof(Disk::sight_clock)));
        value(&runtime.unknown_11,
              bytes.subspan(offsetof(Disk, unknown_11), sizeof(Disk::unknown_11)));
    }
};
template<> struct Record<save::GameState> {
    using Disk = GlobalStatus;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text text)
    {
        value(&runtime.status.retail.save_version,
              bytes.subspan(offsetof(Disk, buffers.save_version),
                            sizeof(Disk::buffers.save_version)));
        std::uint32_t party_present = 1;
        value(&party_present, bytes.subspan(offsetof(Disk, buffers.characters), 4));
        party_present = 1;
        value(&party_present, bytes.subspan(offsetof(Disk, buffers.party_rows), 4));
        value(&runtime.status.game_started,
              bytes.subspan(offsetof(Disk, game_started), sizeof(Disk::game_started)));
        // Retail stores three counters; runtime membership has one source of truth.
        if constexpr (std::is_const_v<R>) {
            int regular = 0, auxiliary = 0;
            for (const auto& member : runtime.party.members) {
                if (!member) continue;
                if (member->npc_index == -1)
                    ++regular;
                else
                    ++auxiliary;
            }
            const int total_count = regular + auxiliary;
            value(&regular, bytes.subspan(offsetof(Disk, regular_member_count), 4));
            value(&auxiliary, bytes.subspan(offsetof(Disk, auxiliary_member_count), 4));
            value(&total_count, bytes.subspan(offsetof(Disk, total_member_count), 4));
        }
        value(&runtime.party.gold,
              bytes.subspan(offsetof(Disk, party_gold), sizeof(Disk::party_gold)));
        value(&runtime.status.selected_character,
              bytes.subspan(offsetof(Disk, selected_character), sizeof(Disk::selected_character)));
        constexpr auto pool_slots = std::extent_v<decltype(Disk::party_item_pool)>;
        if (runtime.party.item_pool.size() > pool_slots)
            throw std::runtime_error("party inventory exceeds retail save capacity");
        unsigned int count = static_cast<unsigned int>(runtime.party.item_pool.size());
        value(&count, bytes.subspan(offsetof(Disk, party_item_count), sizeof(count)));
        if constexpr (!std::is_const_v<R>) {
            if (count > pool_slots) throw std::runtime_error("invalid party inventory count");
            runtime.party.item_pool.resize(count);
        }
        const auto pool_bytes =
            bytes.subspan(offsetof(Disk, party_item_pool), sizeof(Disk::party_item_pool));
        item_run(runtime.party.item_pool, pool_bytes, value, text);
        // Only the codec handles unused disk slots. Known fields are canonical empty
        // values; unknown bytes from loaded unused records remain round-trippable.
        auto& unused = runtime.status.retail.unused_pool_unknown;
        if constexpr (!std::is_const_v<R>) unused.fill(0);
        for (std::size_t index = count; index < pool_slots; ++index) {
            const auto slot =
                pool_bytes.subspan(index * sizeof(ItemInstance), sizeof(ItemInstance));
            if constexpr (std::is_const_v<R>) {
                const W8ItemInstance empty{.unknown_08 = unused[index]};
                nested(empty, slot, value, text);
            } else {
                value(&unused[index], slot.subspan(offsetof(ItemInstance, unknown_08), 1));
            }
        }
        value(&runtime.status.text_line_cursor,
              bytes.subspan(offsetof(Disk, text_line_cursor), sizeof(Disk::text_line_cursor)));
        value(&runtime.status.retail.legacy_text_box_lines,
              bytes.subspan(offsetof(Disk, legacy_text_box_lines),
                            sizeof(Disk::legacy_text_box_lines)));
        for (std::size_t index = 0; index < std::size(runtime.status.effect_slots); ++index)
            Record<W8EffectSlot>::fields(
                runtime.status.effect_slots[index],
                bytes.subspan(offsetof(Disk, effect_slots) + index * sizeof(EffectSlot),
                              sizeof(EffectSlot)),
                value, text);
        value(&runtime.status.retail.unknown_187b,
              bytes.subspan(offsetof(Disk, unknown_187b), sizeof(Disk::unknown_187b)));
        value(&runtime.status.party_facing,
              bytes.subspan(offsetof(Disk, party_facing), sizeof(Disk::party_facing)));
        value(&runtime.status.party_heading,
              bytes.subspan(offsetof(Disk, party_heading), sizeof(Disk::party_heading)));
        value(&runtime.status.world_clock,
              bytes.subspan(offsetof(Disk, world_clock), sizeof(Disk::world_clock)));
        value(&runtime.status.world_clock_ms,
              bytes.subspan(offsetof(Disk, world_clock_ms), sizeof(Disk::world_clock_ms)));
        value(&runtime.party.order_slots,
              bytes.subspan(offsetof(Disk, party_order_slots), sizeof(Disk::party_order_slots)));
        value(&runtime.status.resume_level,
              bytes.subspan(offsetof(Disk, current_level), sizeof(Disk::current_level)));
        value(&runtime.status.retail.status_header_prefix,
              bytes.subspan(offsetof(Disk, status_header_prefix),
                            sizeof(Disk::status_header_prefix)));
        value(&runtime.status.uiTurnsElapsed,
              bytes.subspan(offsetof(Disk, uiTurnsElapsed), sizeof(Disk::uiTurnsElapsed)));
        value(&runtime.status.retail.status_header_suffix,
              bytes.subspan(offsetof(Disk, status_header_suffix),
                            sizeof(Disk::status_header_suffix)));
        for (std::size_t level = 0; level < W8_LEVEL_COUNT; ++level)
            nested(runtime.status.level_progress[level],
                   bytes.subspan(offsetof(Disk, level_progress) + level * sizeof(LevelProgress),
                                 sizeof(LevelProgress)),
                   value, text);
        value(&runtime.status.retail.unknown_2013,
              bytes.subspan(offsetof(Disk, unknown_2013), sizeof(Disk::unknown_2013)));
        nested(runtime.status.pending_move_location,
               bytes.subspan(offsetof(Disk, pending_move_location),
                             sizeof(Disk::pending_move_location)),
               value, text);
        nested(runtime.party.modifiers,
               bytes.subspan(offsetof(Disk, party_modifiers), sizeof(Disk::party_modifiers)), value,
               text);
        value(&runtime.status.next_group_id,
              bytes.subspan(offsetof(Disk, next_group_id), sizeof(Disk::next_group_id)));
        value(&runtime.status.next_monster_location_id,
              bytes.subspan(offsetof(Disk, next_monster_location_id),
                            sizeof(Disk::next_monster_location_id)));
        value(&runtime.status.next_world_item_id,
              bytes.subspan(offsetof(Disk, next_world_item_id), sizeof(Disk::next_world_item_id)));
        value(&runtime.status.next_trigger_id,
              bytes.subspan(offsetof(Disk, next_trigger_id), sizeof(Disk::next_trigger_id)));
        bool held_present = runtime.party.held.item.iItemNo != -1;
        value(&held_present,
              bytes.subspan(offsetof(Disk, item_in_cursor), sizeof(Disk::item_in_cursor)));
        nested(runtime.party.held.item,
               bytes.subspan(offsetof(Disk, item_in_hand), sizeof(Disk::item_in_hand)), value,
               text);
        value(&runtime.status.flags, bytes.subspan(offsetof(Disk, flags), sizeof(Disk::flags)));
        value(&runtime.status.game_time_ms,
              bytes.subspan(offsetof(Disk, game_time_ms), sizeof(Disk::game_time_ms)));
        value(&runtime.status.aging_accumulator,
              bytes.subspan(offsetof(Disk, aging_accumulator), sizeof(Disk::aging_accumulator)));
        byte_boolean(&runtime.status.search_mode,
                     bytes.subspan(offsetof(Disk, search_mode), sizeof(Disk::search_mode)));
        value(&runtime.status.world_suspended,
              bytes.subspan(offsetof(Disk, world_suspended), sizeof(Disk::world_suspended)));
        value(&runtime.status.real_elapsed,
              bytes.subspan(offsetof(Disk, real_elapsed), sizeof(Disk::real_elapsed)));
        value(&runtime.status.frame_elapsed,
              bytes.subspan(offsetof(Disk, frame_elapsed), sizeof(Disk::frame_elapsed)));
        enum_value<std::uint32_t>(
            &runtime.status.wait_state,
            bytes.subspan(offsetof(Disk, wait_state), sizeof(Disk::wait_state)));
        value(&runtime.status.item_recharge_ms,
              bytes.subspan(offsetof(Disk, item_recharge_ms), sizeof(Disk::item_recharge_ms)));
        nested(runtime.party.formation,
               bytes.subspan(offsetof(Disk, formation), sizeof(Disk::formation)), value, text);
        value(&runtime.status.game_time_days,
              bytes.subspan(offsetof(Disk, game_time_days), sizeof(Disk::game_time_days)));
        value(&runtime.status.iron_man,
              bytes.subspan(offsetof(Disk, iron_man), sizeof(Disk::iron_man)));
        value(
            &runtime.status.binding_reset_clock,
            bytes.subspan(offsetof(Disk, binding_reset_clock), sizeof(Disk::binding_reset_clock)));
        value(&runtime.status.binding_reset_pending,
              bytes.subspan(offsetof(Disk, binding_reset_pending),
                            sizeof(Disk::binding_reset_pending)));
        value(&runtime.status.retail.padding_242f,
              bytes.subspan(offsetof(Disk, padding_242f), sizeof(Disk::padding_242f)));
        value(
            &runtime.status.npc_restore_pending,
            bytes.subspan(offsetof(Disk, npc_restore_pending), sizeof(Disk::npc_restore_pending)));
        value(&runtime.status.quote_audit,
              bytes.subspan(offsetof(Disk, quote_audit), sizeof(Disk::quote_audit)));
        value(&runtime.status.long_quote,
              bytes.subspan(offsetof(Disk, long_quote), sizeof(Disk::long_quote)));
        value(&runtime.status.party_fatigued,
              bytes.subspan(offsetof(Disk, party_fatigued), sizeof(Disk::party_fatigued)));
        value(&runtime.status.selected_party_member,
              bytes.subspan(offsetof(Disk, selected_party_member),
                            sizeof(Disk::selected_party_member)));
        value(&runtime.status.world_cursor_gate,
              bytes.subspan(offsetof(Disk, world_cursor_gate), sizeof(Disk::world_cursor_gate)));
        value(&runtime.status.camp_tick_ms,
              bytes.subspan(offsetof(Disk, camp_tick_ms), sizeof(Disk::camp_tick_ms)));
        value(&runtime.status.rpc_races,
              bytes.subspan(offsetof(Disk, rpc_races), sizeof(Disk::rpc_races)));
        value(&runtime.status.retail.unknown_243f,
              bytes.subspan(offsetof(Disk, unknown_243f), sizeof(Disk::unknown_243f)));
        value(&runtime.status.skip_loose_character_check,
              bytes.subspan(offsetof(Disk, skip_loose_character_check),
                            sizeof(Disk::skip_loose_character_check)));
        value(&runtime.status.use_item_latch,
              bytes.subspan(offsetof(Disk, use_item_latch), sizeof(Disk::use_item_latch)));
        value(
            &runtime.status.infatuation_pending,
            bytes.subspan(offsetof(Disk, infatuation_pending), sizeof(Disk::infatuation_pending)));
        value(&runtime.status.difficulty,
              bytes.subspan(offsetof(Disk, difficulty), sizeof(Disk::difficulty)));
        value(&runtime.status.retail.save_filetime_xor,
              bytes.subspan(offsetof(Disk, save_filetime_xor), sizeof(Disk::save_filetime_xor)));
        text(runtime.status.monster_name_buffer,
             bytes.subspan(offsetof(Disk, monster_name_buffer), sizeof(Disk::monster_name_buffer)),
             Encoding::utf16);
        value(&runtime.status.sedexus_party_slot,
              bytes.subspan(offsetof(Disk, sedexus_party_slot), sizeof(Disk::sedexus_party_slot)));
        value(&runtime.status.stamina_tick_ms,
              bytes.subspan(offsetof(Disk, stamina_tick_ms), sizeof(Disk::stamina_tick_ms)));
        value(&runtime.status.condition13_clock,
              bytes.subspan(offsetof(Disk, condition13_clock), sizeof(Disk::condition13_clock)));
        byte_boolean(&runtime.status.skip_next_condition_reaction,
                     bytes.subspan(offsetof(Disk, skip_next_condition_reaction),
                                   sizeof(Disk::skip_next_condition_reaction)));
        value(&runtime.status.rpc_active,
              bytes.subspan(offsetof(Disk, rpc_active), sizeof(Disk::rpc_active)));
        value(&runtime.status.fact_b8_pending,
              bytes.subspan(offsetof(Disk, fact_b8_pending), sizeof(Disk::fact_b8_pending)));
        value(&runtime.status.condition13_stamp,
              bytes.subspan(offsetof(Disk, condition13_stamp), sizeof(Disk::condition13_stamp)));
        value(&runtime.status.pending_condition_party_slot,
              bytes.subspan(offsetof(Disk, pending_condition_party_slot),
                            sizeof(Disk::pending_condition_party_slot)));
        value(&runtime.status.fact_b8_clock,
              bytes.subspan(offsetof(Disk, fact_b8_clock), sizeof(Disk::fact_b8_clock)));
        value(&runtime.status.greeting_pending,
              bytes.subspan(offsetof(Disk, greeting_pending), sizeof(Disk::greeting_pending)));
        value(&runtime.status.camp_fatigue_count,
              bytes.subspan(offsetof(Disk, camp_fatigue_count), sizeof(Disk::camp_fatigue_count)));
        value(&runtime.status.party_slot,
              bytes.subspan(offsetof(Disk, party_slot), sizeof(Disk::party_slot)));
        for (std::size_t index = 0; index < std::size(runtime.status.item_spell_usage); ++index)
            nested(runtime.status.item_spell_usage[index],
                   bytes.subspan(offsetof(Disk, item_spell_usage) + index * sizeof(ItemSpellUsage),
                                 sizeof(ItemSpellUsage)),
                   value, text);
        value(&runtime.status.log_fact_checks,
              bytes.subspan(offsetof(Disk, log_fact_checks), sizeof(Disk::log_fact_checks)));
        value(&runtime.status.status_ints,
              bytes.subspan(offsetof(Disk, status_ints), sizeof(Disk::status_ints)));
        value(&runtime.status.fact_88_latch,
              bytes.subspan(offsetof(Disk, fact_88_latch), sizeof(Disk::fact_88_latch)));
        value(&runtime.status.retail.unknown_40c2,
              bytes.subspan(offsetof(Disk, unknown_40c2), sizeof(Disk::unknown_40c2)));
        for (std::size_t index = 0; index < std::size(runtime.status.spell_usage); ++index)
            nested(runtime.status.spell_usage[index],
                   bytes.subspan(offsetof(Disk, spell_usage) + index * sizeof(SpellUsage),
                                 sizeof(SpellUsage)),
                   value, text);
        value(&runtime.status.retail.unknown_47de,
              bytes.subspan(offsetof(Disk, unknown_47de), sizeof(Disk::unknown_47de)));
        value(&runtime.status.cc_arena_spawned,
              bytes.subspan(offsetof(Disk, cc_arena_spawned), sizeof(Disk::cc_arena_spawned)));
        value(&runtime.status.savant_hack_tick,
              bytes.subspan(offsetof(Disk, savant_hack_tick), sizeof(Disk::savant_hack_tick)));
        value(&runtime.status.bela_cycle_tick,
              bytes.subspan(offsetof(Disk, bela_cycle_tick), sizeof(Disk::bela_cycle_tick)));
        value(&runtime.status.retail.unknown_497b,
              bytes.subspan(offsetof(Disk, unknown_497b), sizeof(Disk::unknown_497b)));
        value(&runtime.status.combat_difficulty_counts,
              bytes.subspan(offsetof(Disk, combat_difficulty_counts),
                            sizeof(Disk::combat_difficulty_counts)));
        value(&runtime.status.vi_event_stage,
              bytes.subspan(offsetof(Disk, vi_event_stage), sizeof(Disk::vi_event_stage)));
        value(&runtime.status.endgame2_queued,
              bytes.subspan(offsetof(Disk, endgame2_queued), sizeof(Disk::endgame2_queued)));
        value(&runtime.status.endgame3_queued,
              bytes.subspan(offsetof(Disk, endgame3_queued), sizeof(Disk::endgame3_queued)));
        value(
            &runtime.status.text_box_lines_used,
            bytes.subspan(offsetof(Disk, text_box_lines_used), sizeof(Disk::text_box_lines_used)));
        value(&runtime.status.text_box_lines_shown,
              bytes.subspan(offsetof(Disk, text_box_lines_shown),
                            sizeof(Disk::text_box_lines_shown)));
        value(&runtime.status.trang_check_clock,
              bytes.subspan(offsetof(Disk, trang_check_clock), sizeof(Disk::trang_check_clock)));
        value(
            &runtime.status.trang_check_pending,
            bytes.subspan(offsetof(Disk, trang_check_pending), sizeof(Disk::trang_check_pending)));
        value(&runtime.status.intro_shown,
              bytes.subspan(offsetof(Disk, intro_shown), sizeof(Disk::intro_shown)));
        value(&runtime.status.flag, bytes.subspan(offsetof(Disk, flag), sizeof(Disk::flag)));
        value(&runtime.status.retail.padding_49be,
              bytes.subspan(offsetof(Disk, padding_49be), sizeof(Disk::padding_49be)));
        value(&runtime.status.endgame_started,
              bytes.subspan(offsetof(Disk, endgame_started), sizeof(Disk::endgame_started)));
        value(&runtime.status.dev_flagged,
              bytes.subspan(offsetof(Disk, dev_flagged), sizeof(Disk::dev_flagged)));
    }
};

template<> struct Record<save::LevelStatusHeader> {
    using Disk = StatusHeader;
    template<class R, class B, class Value, class Text>
    static void fields(R& runtime, B bytes, Value value, Text)
    {
        float version = kStatusHeaderVersion;
        value(&version, bytes.subspan(offsetof(Disk, version), sizeof(Disk::version)));
        if (version != kStatusHeaderVersion)
            throw std::runtime_error("unsupported level status header version");
        value(&runtime.next_group_id,
              bytes.subspan(offsetof(Disk, next_group_id), sizeof(Disk::next_group_id)));
        value(&runtime.next_monster_location_id,
              bytes.subspan(offsetof(Disk, next_monster_location_id),
                            sizeof(Disk::next_monster_location_id)));
        value(&runtime.next_world_item_id,
              bytes.subspan(offsetof(Disk, next_world_item_id), sizeof(Disk::next_world_item_id)));
        value(&runtime.next_trigger_id,
              bytes.subspan(offsetof(Disk, next_trigger_id), sizeof(Disk::next_trigger_id)));
        value(&runtime.status_prefix,
              bytes.subspan(offsetof(Disk, status_prefix), sizeof(Disk::status_prefix)));
        value(&runtime.turns_elapsed,
              bytes.subspan(offsetof(Disk, turns_elapsed), sizeof(Disk::turns_elapsed)));
        value(&runtime.status_suffix,
              bytes.subspan(offsetof(Disk, status_suffix), sizeof(Disk::status_suffix)));
        value(&runtime.unknown_tail,
              bytes.subspan(offsetof(Disk, unknown_114), sizeof(Disk::unknown_114)));
    }
};
} // namespace wiz8::retail

namespace wiz8::save {
namespace {

// A section payload, written front to back.
class Writer {
    std::vector<std::byte> out_;

public:
    void bytes(std::span<const std::byte> data)
    {
        out_.insert(out_.end(), data.begin(), data.end());
    }
    template<class T> void value(const T& field)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        bytes(std::as_bytes(std::span(&field, 1)));
    }
    void flag(bool set) { value(static_cast<std::uint8_t>(set)); }
    // A NUL-terminated text field of `width` bytes, truncated to fit.
    void text(std::string_view value, std::size_t width)
    {
        std::vector<char> field(width);
        std::ranges::copy(value.substr(0, width - 1), field.begin());
        bytes(std::as_bytes(std::span(field)));
    }
    template<class T> void record(const T& runtime) { bytes(retail::encode(runtime)); }
    // A record behind its four-byte length.
    void sized(std::span<const std::byte> record)
    {
        value(static_cast<std::uint32_t>(record.size()));
        bytes(record);
    }
    std::vector<std::byte> take() { return std::move(out_); }
};

// A section payload, read front to back. Reading past its end throws.
class Reader {
    std::span<const std::byte> rest_;

public:
    explicit Reader(std::span<const std::byte> payload) : rest_(payload) {}

    std::size_t remaining() const { return rest_.size(); }
    std::span<const std::byte> take(std::size_t size)
    {
        if (size > rest_.size()) throw std::runtime_error("truncated save section");
        const auto taken = rest_.first(size);
        rest_ = rest_.subspan(size);
        return taken;
    }
    template<class T> T value()
    {
        static_assert(std::is_trivially_copyable_v<T>);
        T field{};
        std::memcpy(&field, take(sizeof(field)).data(), sizeof(field));
        return field;
    }
    bool flag() { return value<std::uint8_t>() != 0; }
    // A NUL-terminated text field of `width` bytes.
    std::string text(std::size_t width)
    {
        const auto field = take(width);
        const auto* data = reinterpret_cast<const char*>(field.data());
        const auto* end = static_cast<const char*>(std::memchr(data, 0, width));
        if (!end) throw std::runtime_error("unterminated save text field");
        return std::string(data, end);
    }
    // A record whose writer may have stored only a prefix of today's layout.
    template<class T>
    void record(T& runtime, std::size_t size = sizeof(typename retail::Record<T>::Disk))
    {
        if (size > sizeof(typename retail::Record<T>::Disk))
            throw std::runtime_error("oversized save record");
        retail::decode(take(size), runtime);
    }
    // The bytes of a record behind its four-byte length.
    std::span<const std::byte> sized() { return take(value<std::uint32_t>()); }
};

} // namespace

std::vector<std::byte> encode(const GameState& state)
{
    Writer out;
    out.sized(retail::encode(state));
    for (const auto& character : state.party.characters) out.sized(retail::encode(character));
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        out.sized(retail::encode(
            retail::SavedPartySlot{state.party.rows[slot], state.party.members[slot]}));
    return out.take();
}

GameState decode_game_state(std::span<const std::byte> payload)
{
    Reader records(payload);
    GameState state{};
    retail::decode(records.sized(), state);
    auto& [status, party] = state;
    if (status.retail.save_version < 1.1f) {
        // Before 1.1 there were three message boxes, stored in a legacy pair.
        for (std::size_t box = 0; box < 3; ++box) {
            status.text_box_lines_used[box] = status.retail.legacy_text_box_lines[0][box];
            status.text_box_lines_shown[box] = status.retail.legacy_text_box_lines[1][box];
        }
        status.text_box_lines_used[3] = 0;
        status.text_box_lines_shown[3] = 0;
    }
    for (std::size_t box = 0; box < 4; ++box) {
        if (status.text_box_lines_used[box] > W8_TEXT_BOX_LINES ||
            status.text_box_lines_shown[box] > W8_TEXT_BOX_LINES)
            throw std::runtime_error("invalid message storage count");
    }
    for (auto& character : party.characters) {
        retail::decode(records.sized(), character);
        if (character.record_version < 2 &&
            character.original_profession == W8_PROFESSION_FIGHTER &&
            character.profession_levels[W8_PROFESSION_FIGHTER] == 0)
            character.original_profession = character.iProfession;
    }
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        retail::SavedPartySlot saved{};
        retail::decode(records.sized(), saved);
        auto& row = party.rows[slot] = saved.row;
        party.members[slot] = saved.member;
        auto& item = row.remembered.item_detail.item_use.item;
        item.character = slot;
        if (!party.members[slot] || !party.itemAt(item)) item = {};
        if (row.executing.kind == W8_ACTION_USE_ITEM) {
            row.executing.detail_block.item_use.item = item;
            row.executing.detail_block.item_use.item_id =
                row.remembered.item_detail.item_use.item_id;
        }
    }
    party.formation.reconcile(party.members);
    return state;
}

std::vector<std::byte> encode(const LevelStatusHeader& header)
{
    const std::array<std::byte, sizeof(retail::StatusHeader)> image = retail::encode(header);
    return std::vector<std::byte>(image.begin(), image.end());
}

LevelStatusHeader decode_level_status_header(std::span<const std::byte> payload)
{
    if (payload.size() < sizeof(retail::StatusHeader))
        throw std::runtime_error("truncated level status header");
    LevelStatusHeader header;
    retail::decode(payload.first(sizeof(retail::StatusHeader)), header);
    return header;
}

namespace {

/* A TEXT line on disk. Its first word counts UTF-16 units with the terminator, and the
   highlight and length fields count UTF-16 units too. The list-pointer word is unused and
   written as zero. */
#pragma pack(push, 1)
struct MessageRecord {
    std::uint32_t character_count;
    std::uint8_t font_palette;
    std::uint8_t highlight_color;
    std::uint8_t highlight_start;
    std::uint8_t highlight_stop;
    std::uint32_t clock;
    std::uint32_t saved_remaining_ms;
    std::int32_t link;
    std::int32_t length;
    std::uint32_t entries;
    std::uint8_t unknown_1c[8];
};
static_assert(sizeof(MessageRecord) == 0x24);

/* A JRNL entry on disk. Retail writes the alternate-text flag as one byte and leaves the
   three bytes after it uninitialized. */
struct JournalRecord {
    std::int32_t level;
    std::int32_t fact;
    std::uint8_t alternate_text;
    std::array<std::uint8_t, 3> unknown_09;
};
static_assert(sizeof(JournalRecord) == 12);

// A FATA standing record on disk.
struct FactionRecord {
    std::int8_t disposition_score;
    std::int32_t offense_count;
    std::uint8_t unknown_05;
    std::int32_t band_changed_clock;
    std::uint8_t encountered;
    std::array<std::uint8_t, 3> unknown_0b;
};
static_assert(sizeof(FactionRecord) == 14);
#pragma pack(pop)

constexpr std::size_t kTextFieldBytes = 0x80;
constexpr std::size_t kLocationVariableBytes = 4 + kTextFieldBytes + 4;
// Retail's NPC character block before it carried its size.
constexpr std::uint32_t kLegacyNpcCharacterBytes = 0x185c;
constexpr std::uint8_t kNpcStateVersion = 3;
constexpr std::uint8_t kTranscriptVersion = 2;
// A transcript keyword holds fewer UTF-16 units than this.
constexpr std::int32_t kTranscriptKeywordUnits = 100;
// A message line longer than this many UTF-16 units is malformed.
constexpr std::uint32_t kMessageCharacterLimit = 100000;

// UTF-16 units in the first `bytes` bytes of `line`.
std::size_t utf16_units(std::string_view line, std::size_t bytes)
{
    return text::to_utf16(line.substr(0, bytes)).size();
}

// The byte offset in `line` after `units` UTF-16 units.
std::size_t utf8_offset(std::string_view line, std::size_t units)
{
    std::size_t offset = 0, count = 0;
    while (offset < line.size() && count < units)
        count += text::next(line, offset) > 0xffff ? 2 : 1;
    return offset;
}

// A highlight bound as the retail byte of UTF-16 units, which saturates.
std::uint8_t utf16_byte(std::string_view line, std::size_t bytes)
{
    return static_cast<std::uint8_t>(std::min<std::size_t>(255, utf16_units(line, bytes)));
}

} // namespace

std::vector<std::byte> encode(const MessageBoxes& boxes)
{
    Writer out;
    out.value(std::int32_t{1});
    for (const auto& lines : boxes) {
        out.value(checked_integer<std::uint32_t>(lines.size()));
        for (const auto& line : lines) {
            const auto units = text::to_utf16(line.text).size();
            MessageRecord record{};
            // Even an empty line keeps its terminator: retail allocates the count it reads
            // and treats the result as a terminated string.
            record.character_count = checked_integer<std::uint32_t>(units + 1);
            record.font_palette = line.font_palette;
            record.highlight_color = line.highlight_color;
            record.highlight_start = utf16_byte(line.text, line.highlight_start);
            record.highlight_stop = utf16_byte(line.text, line.highlight_stop);
            record.clock = static_cast<std::uint32_t>(line.clock);
            record.saved_remaining_ms = line.saved_remaining_ms;
            record.link = line.link;
            record.length = line.length < 0
                                ? -1
                                : checked_integer<std::int32_t>(utf16_units(
                                      line.text, static_cast<std::size_t>(line.length)));
            std::ranges::copy(line.unknown_1c, record.unknown_1c);
            out.value(record);
            std::vector<std::byte> characters(std::size_t{record.character_count} * 2);
            text::to_utf16le(line.text, characters);
            out.bytes(characters);
        }
    }
    return out.take();
}

MessageBoxes decode_message_boxes(std::span<const std::byte> payload, float status_version)
{
    Reader in(payload);
    in.value<std::int32_t>(); // The format, always 1.
    MessageBoxes boxes;
    for (std::size_t box = 0; box < boxes.size(); ++box) {
        if (status_version < 1.1f && box == 3) break;
        const auto count = in.value<std::uint32_t>();
        if (count > W8_TEXT_BOX_LINES) throw std::runtime_error("invalid message line count");
        auto& lines = boxes[box];
        lines.resize(count);
        for (auto& line : lines) {
            const auto record = in.value<MessageRecord>();
            if (record.character_count > kMessageCharacterLimit)
                throw std::runtime_error("invalid message length");
            line.text = text::from_utf16le(in.take(std::size_t{record.character_count} * 2));
            line.font_palette = record.font_palette;
            line.highlight_color = record.highlight_color;
            line.highlight_start = utf8_offset(line.text, record.highlight_start);
            line.highlight_stop = utf8_offset(line.text, record.highlight_stop);
            line.clock = record.clock;
            line.saved_remaining_ms = record.saved_remaining_ms;
            line.link = record.link;
            line.length = record.length < 0
                              ? record.length
                              : checked_integer<int>(utf8_offset(
                                    line.text, static_cast<std::size_t>(record.length)));
            std::ranges::copy(record.unknown_1c, line.unknown_1c);
        }
    }
    return boxes;
}

std::vector<std::byte> encode(std::span<const W8LocationVariable> variables)
{
    Writer out;
    out.value(checked_integer<std::int32_t>(variables.size()));
    for (const auto& variable : variables) {
        out.value(std::int32_t{variable.value});
        out.text(variable.name, kTextFieldBytes);
        out.value(std::int32_t{variable.level});
    }
    return out.take();
}

std::vector<W8LocationVariable> decode_location_variables(std::span<const std::byte> payload)
{
    Reader in(payload);
    const auto count = in.value<std::int32_t>();
    if (count < 0 || static_cast<std::size_t>(count) > in.remaining() / kLocationVariableBytes)
        throw std::runtime_error("invalid location variable count");
    std::vector<W8LocationVariable> variables(static_cast<std::size_t>(count));
    for (auto& variable : variables) {
        variable.value = in.value<std::int32_t>();
        variable.name = in.text(kTextFieldBytes);
        variable.level = in.value<std::int32_t>();
    }
    return variables;
}

std::vector<std::byte> encode(const DialogueTranscript& transcript)
{
    Writer out;
    out.value(kTranscriptVersion);
    out.value(checked_integer<std::int32_t>(transcript.keywords.size()));
    for (const auto& keyword : transcript.keywords) {
        const auto units = checked_integer<std::int32_t>(text::to_utf16(keyword.text).size());
        if (units >= kTranscriptKeywordUnits)
            throw std::runtime_error("transcript keyword too long");
        std::vector<std::byte> characters((std::size_t(units) + 1) * 2);
        text::to_utf16le(keyword.text, characters);
        out.value(units);
        out.bytes(characters);
        out.value(keyword.category);
    }
    const auto view = transcript.view.value_or(DialogueTranscript::View{});
    out.value(view.category_filter);
    out.flag(view.sorted);
    return out.take();
}

DialogueTranscript decode_dialogue_transcript(std::span<const std::byte> payload)
{
    Reader in(payload);
    const auto version = in.value<std::uint8_t>();
    const auto count = in.value<std::int32_t>();
    // Each keyword is at least its length, its terminator and its category.
    if (version < 1 || version > kTranscriptVersion || count < 0 ||
        static_cast<std::size_t>(count) > in.remaining() / 7)
        throw std::runtime_error("invalid dialogue transcript");
    DialogueTranscript transcript;
    transcript.keywords.resize(static_cast<std::size_t>(count));
    for (auto& keyword : transcript.keywords) {
        const auto units = in.value<std::int32_t>();
        if (units < 0 || units >= kTranscriptKeywordUnits)
            throw std::runtime_error("invalid transcript keyword length");
        const auto characters = in.take((std::size_t(units) + 1) * 2);
        keyword.text = text::from_utf16le(characters);
        if (characters.last(2)[0] != std::byte{} || characters.last(2)[1] != std::byte{} ||
            text::to_utf16(keyword.text).size() != std::size_t(units))
            throw std::runtime_error("malformed transcript keyword");
        keyword.category = in.value<W8DialogueCategory>();
    }
    if (version > 1) {
        const auto filter = in.value<W8DialogueCategory>();
        const auto sorted = in.value<std::uint8_t>();
        if (filter < W8_DIALOGUE_CATEGORY_ALL || filter > W8_DIALOGUE_CATEGORY_MISC || sorted > 1)
            throw std::runtime_error("invalid transcript view");
        transcript.view = DialogueTranscript::View{filter, sorted != 0};
    }
    return transcript;
}

std::vector<std::byte> encode(std::span<const std::shared_ptr<W8NpcState>> npcs)
{
    Writer out;
    out.value(kNpcStateVersion);
    out.value(checked_integer<std::uint32_t>(npcs.size()));
    for (const auto& npc : npcs) {
        out.record(*npc);
        if (npc->character) out.sized(retail::encode(*npc->character));
    }
    for (const auto& npc : npcs) {
        out.value(checked_integer<std::uint32_t>(npc->items.size()));
        for (const auto& entry : npc->items) out.record(entry);
    }
    return out.take();
}

std::vector<std::unique_ptr<W8NpcState>> decode_npc_states(std::span<const std::byte> payload,
                                                           std::size_t npc_record_count)
{
    Reader in(payload);
    const auto version = in.value<std::uint8_t>();
    const auto count = in.value<std::uint32_t>();
    if (count > in.remaining() / sizeof(retail::NpcState))
        throw std::runtime_error("invalid NPC state count");
    std::vector<std::unique_ptr<W8NpcState>> npcs;
    npcs.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        retail::NpcState image{};
        std::memcpy(&image, in.take(sizeof(image)).data(), sizeof(image));
        auto npc = std::make_unique<W8NpcState>();
        retail::decode_image(image, *npc);
        const std::size_t record = npc->name_style == 0 ? index : npc->name_style;
        if (record >= npc_record_count || record > std::numeric_limits<std::uint8_t>::max())
            throw std::runtime_error("invalid NPC database index");
        npc->name_style = static_cast<std::uint8_t>(record);
        if (image.character != 0) {
            const auto size = version >= 3 ? in.value<std::uint32_t>() : kLegacyNpcCharacterBytes;
            in.record(npc->character.emplace(), size);
        }
        npcs.push_back(std::move(npc));
    }
    if (version > 1) {
        for (auto& npc : npcs) {
            const auto items = in.value<std::uint32_t>();
            if (items > in.remaining() / sizeof(retail::NpcItemEntry))
                throw std::runtime_error("invalid NPC stock count");
            npc->items.resize(items);
            for (auto& entry : npc->items) in.record(entry);
        }
    }
    return npcs;
}

std::vector<std::byte> encode(const Facts& facts)
{
    const auto bytes = std::as_bytes(std::span(facts));
    return std::vector<std::byte>(bytes.begin(), bytes.end());
}

Facts decode_facts(std::span<const std::byte> payload)
{
    Facts facts{};
    std::memcpy(facts.data(), Reader(payload).take(facts.size()).data(), facts.size());
    return facts;
}

std::vector<std::byte> encode(const Factions& factions)
{
    Writer out;
    for (const auto& row : factions.relations) out.value(row);
    for (const auto& state : factions.states) {
        out.value(FactionRecord{state.disposition_score, state.offense_count, state.unknown_05,
                                state.band_changed_clock, state.encountered, state.unknown_0b});
    }
    return out.take();
}

Factions decode_factions(std::span<const std::byte> payload)
{
    Reader in(payload);
    Factions factions;
    for (auto& row : factions.relations) row = in.value<std::remove_reference_t<decltype(row)>>();
    for (auto& state : factions.states) {
        const auto record = in.value<FactionRecord>();
        state = {record.disposition_score, record.offense_count, record.band_changed_clock,
                 record.encountered != 0,  record.unknown_05,    record.unknown_0b};
    }
    return factions;
}

std::vector<std::byte> encode(std::span<const W8JournalEntry> journal)
{
    Writer out;
    out.value(std::int32_t{1}); // The format.
    out.value(checked_integer<std::int32_t>(journal.size()));
    for (const auto& entry : journal) {
        out.value(JournalRecord{entry.level, static_cast<std::int32_t>(entry.fact),
                                entry.alternate_text, entry.unknown_09});
    }
    return out.take();
}

std::vector<W8JournalEntry> decode_journal(std::span<const std::byte> payload)
{
    Reader in(payload);
    in.value<std::int32_t>(); // The format, always 1.
    const auto count = in.value<std::int32_t>();
    if (count < 0 || static_cast<std::size_t>(count) > in.remaining() / sizeof(JournalRecord))
        throw std::runtime_error("invalid journal count");
    std::vector<W8JournalEntry> journal(static_cast<std::size_t>(count));
    for (auto& entry : journal) {
        const auto record = in.value<JournalRecord>();
        entry = {record.level, static_cast<W8FactId>(record.fact), record.alternate_text != 0,
                 record.unknown_09};
    }
    return journal;
}

/* The HYPN record lists the effect's spell, its turns left, its working source and target,
   the cast's original source and target, four flags and the effect definition. */

std::vector<std::byte> encode(const W8SpellEffectEntry& monster_control)
{
    Writer out;
    out.value(static_cast<std::int32_t>(monster_control.kind));
    out.value(std::int32_t{monster_control.turns_remaining});
    out.record(monster_control.Source);
    out.record(monster_control.target);
    out.record(monster_control.OrigSource);
    out.record(monster_control.OrigTarget);
    out.flag(monster_control.recast);
    out.flag(true); // Retail HYPN sustained flag.
    out.flag(monster_control.missiles_pending);
    out.flag(monster_control.targets_resolved);
    out.record(monster_control.definition);
    return out.take();
}

W8SpellEffectEntry decode_monster_control(std::span<const std::byte> payload)
{
    Reader in(payload);
    W8SpellEffectEntry effect{};
    effect.kind = static_cast<W8SpellId>(in.value<std::int32_t>());
    effect.turns_remaining = in.value<std::int32_t>();
    in.record(effect.Source);
    in.record(effect.target);
    in.record(effect.OrigSource);
    in.record(effect.OrigTarget);
    effect.recast = in.flag();
    in.flag(); // Retail flag: HYPN is always sustained.
    effect.missiles_pending = in.flag();
    effect.targets_resolved = in.flag();
    in.record(effect.definition);
    return effect;
}

// A CHAR section's file name field.
constexpr std::size_t kStoredCharacterNameBytes = 64;

std::vector<std::byte> encode(const StoredCharacter& stored)
{
    if (stored.file_name.empty() || stored.file_name.size() >= kStoredCharacterNameBytes)
        throw std::runtime_error("invalid stored character name");
    Writer out;
    out.text(stored.file_name, kStoredCharacterNameBytes);
    out.sized(retail::encode(stored.character));
    return out.take();
}

StoredCharacter decode_stored_character(std::span<const std::byte> payload)
{
    Reader in(payload);
    StoredCharacter stored;
    stored.file_name = in.text(kStoredCharacterNameBytes);
    const auto size = in.value<std::uint32_t>();
    in.record(stored.character, size);
    return stored;
}

} // namespace wiz8::save

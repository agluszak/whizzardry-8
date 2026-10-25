#ifndef WIZ8_LAYOUTS_GAME_STATUS_H
#define WIZ8_LAYOUTS_GAME_STATUS_H

#include <array>
#include <cstdint>

#include "wiz8/data/records.h"
#include "wiz8/difficulty.h"
#include "wiz8/layouts/levels.h"
#include "wiz8/retail_opaque_records.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_formation.h"
#include "wiz8/world/world.h"

enum W8GameWaitState : unsigned char {
    W8_GAME_WAIT_STATE_MOVING = 0,
    W8_GAME_WAIT_STATE_FAST_MOVEMENT = 1,
    W8_GAME_WAIT_STATE_INITIAL_REST = 2,
    W8_GAME_WAIT_STATE_RESTING = 3
};

struct W8ItemSpellUsageRecord {
    unsigned int cast_count = 0;
    unsigned int usable_cast_count = 0;
    std::array<unsigned char, 8> unknown_08{};

    bool operator==(const W8ItemSpellUsageRecord&) const = default;
};

struct W8CharacterSpellUsageRecord {
    std::array<unsigned char, 4> unknown_00{};
    unsigned int cast_count = 0;
    unsigned int usable_cast_count = 0;
    std::array<unsigned char, 4> unknown_0c{};

    bool operator==(const W8CharacterSpellUsageRecord&) const = default;
};

/* The game's status beside its party: clocks, story latches and counters. Saves store it
   in the GSTA section (save_codec.h); `retail` keeps the serialized bytes that are not
   game state, so a loaded status saves back unchanged. */
struct GameStatus {
    bool game_started = false;
    int selected_character = 0;
    // The text box's line cursor.
    short text_line_cursor = 0;
    /* The party's twelve effect slots, the same records the monster and combat tables
       hold. */
    std::array<W8EffectSlot, 12> effect_slots{};
    int party_facing = 0;
    unsigned int party_heading = 0;
    // Seconds modulo 2^32; elapsed-time arithmetic must also wrap unsigned.
    std::uint32_t world_clock = 0;
    unsigned int world_clock_ms = 0;
    // Decoded save destination. Live identity belongs to the primary world.
    int resume_level = -1;
    unsigned int uiTurnsElapsed = 0;
    std::array<W8LevelProgressRow, W8_LEVEL_COUNT> level_progress{};
    /* CamPos staged by recall when the anchor is on another level; Game::travelToLevel
       restores it after the new world exists. */
    CameraPose pending_move_location{};
    int next_group_id = 0;
    int next_monster_location_id = 0;
    int next_world_item_id = 0;
    int next_trigger_id = 0;
    // Per-slot flags the character-load path consults.
    std::array<unsigned char, 0x20> flags{};
    unsigned int game_time_ms = 0;
    unsigned int aging_accumulator = 0;
    /* Cleared by the main-game frame; HP/SP and condition updates skip work while it is
       set, and encounter culling treats it as the force-despawn gate. */
    bool world_suspended = false;
    /* Session accumulators ConsumeLevelElapsedTime folds the level's pending elapsed times
       into; the aging tick's wait pass sums them against zero. */
    float real_elapsed = 0;
    float frame_elapsed = 0;
    W8GameWaitState wait_state = W8_GAME_WAIT_STATE_MOVING;
    unsigned int item_recharge_ms = 0;
    int game_time_days = 0;
    bool iron_man = false;
    /* World-clock stamp of the last NPC-binding reset; the event pass waits 0x3c ticks
       past it. */
    int binding_reset_clock = 0;
    /* Binding-reset grace period in effect; cleared once the sweep runs after the clock
       elapses. */
    bool binding_reset_pending = false;
    // One-shot gate for the NPC event pass.
    bool npc_restore_pending = false;
    bool party_fatigued = false;
    // Index of the party member the main-game selection flow is on; 0xff for none.
    unsigned char selected_party_member = 0xff;
    // Read as a gate by the main-game frame's world-cursor path.
    unsigned char world_cursor_gate = 0;
    unsigned int camp_tick_ms = 0;
    // The five RPC race ids AssayDialog walks as NUM_RPC_RACES.
    std::array<unsigned char, 5> rpc_races{};
    // Character creation skips the loose CHR collision check when set.
    unsigned char skip_loose_character_check = 0;
    /* Latched once the Trynnie2 Zulu/0x1c3 use-item action has been handled at a cursor
       node; later uses take the Mystical Shaman branch. */
    bool use_item_latch = false;
    bool infatuation_pending = false;
    W8Difficulty difficulty = W8_DIFFICULTY_NOVICE;
    /* Party slot selected by the Sedexus path before rpc_active is armed; later capture,
       fact and death handling reuse the same slot. */
    int sedexus_party_slot = 0;
    unsigned int stamina_tick_ms = 0;
    unsigned char condition13_clock = 0;
    /* One-shot gate; when set, the next condition-change and condition-cleared reaction
       is swallowed and the flag cleared. */
    bool skip_next_condition_reaction = false;
    bool rpc_active = false;
    /* Armed by the long NPC reward event; the event also stamps fact_b8_clock with the
       world clock. */
    bool fact_b8_pending = false;
    unsigned int condition13_stamp = 0;
    int pending_condition_party_slot = 0;
    int fact_b8_clock = 0;
    bool greeting_pending = false;
    unsigned int camp_fatigue_count = 0;
    // Party slot fact 0x39 hands to RemoveCharacterCondition.
    int party_slot = 0;
    /* Per-spell stat records; TrackItemSpellSource walks records[0..149] bumping
       usable_cast_count for spells the character carries and cast_count for the
       selected source spell. */
    std::array<W8ItemSpellUsageRecord, 200> item_spell_usage{};
    unsigned char log_fact_checks = 0;
    std::array<int, 1000> status_ints{};
    bool fact_88_latch = false;
    // Indexed by spell_id - 1, for the player spells 1..0x71.
    std::array<W8CharacterSpellUsageRecord, 0x71> spell_usage{};
    /* Set once the Cosmic Circle arena monsters have been spawned by the level-4 setup;
       the setup skips its work while this or world_suspended holds. */
    bool cc_arena_spawned = false;
    /* MonotonicMilliseconds stamps. NpcScriptSavantHackDone writes the first;
       UpdateNpcEvents retires NPC 0x1b3 fifty ticks later and starts the second, which
       gates monster group 0x1b6's Bela cycle after five seconds. */
    int savant_hack_tick = 0;
    int bela_cycle_tick = 0;
    // Combat difficulty band counters, indexed by EvaluateCombatDifficulty.
    std::array<unsigned int, 3> combat_difficulty_counts{};
    // NPC group event counter, cleared once the group event runs.
    int vi_event_stage = 0;
    /* Pending-stage flags set by FACT_QUE_ENDGAME2/3; the book callback queues ENDGAME2's
       script notice while either holds. */
    int endgame2_queued = 0;
    int endgame3_queued = 0;
    std::array<unsigned int, 4> text_box_lines_used{};
    std::array<unsigned int, 4> text_box_lines_shown{};
    // World-clock stamp the trang reward event compares against.
    int trang_check_clock = 0;
    bool trang_check_pending = false;
    bool intro_shown = false;
    bool flag = false;
    /* Set when the endgame transition starts; saves carrying either this or flag are
       filtered from the load list. */
    bool endgame_started = false;
    /* Retail's developer-mode latch. Never set here; kept so saves round-trip the byte. */
    bool dev_flagged = false;
    wiz8::retail::GlobalStatusOpaque retail;

    bool operator==(const GameStatus&) const = default;
};

#endif

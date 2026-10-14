#pragma once

/* Disk records of the save-game sections that hold live game objects: their
   runtime types own pointers, so each has a fixed-width disk image and a
   codec. Pointer slots are presence words (see pointer_slot). */

#include "wiz8/retail_text_records.h"
#include "wiz8/item_spawning.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace wiz8::retail {

#pragma pack(push, 1)

/* The on-disk W8MonsterInfo; field meanings are documented there. */
struct MonsterInfo {
    int location_id;
    int monster_group_id;
    unsigned int monster_species;
    std::uint32_t p3D;
    std::uint32_t pCombat;
    bool fActive;
    bool fInCombat;
    W8Disposition ubDisposition;
    srVector3T<float> position;
    float derived;
    int uiHPMax;
    unsigned int hp_current;
    int stamina_max;
    int stamina;
    srVector3T<float> heard_noise_position;
    int heard_noise_radius;
    float hp_regen_rate;
    float hp_regen_accumulator;
    float stamina_regen_rate;
    float stamina_regen_accumulator;
    unsigned int uiCondition[W8_CONDITION_COUNT];
    W8Enchantment enchantments[8];
    W8Condition highest_condition;
    int condition_argument;
    W8EffectSlot effect_slots[12];
    W8GameplayModifierBlock modifiers;
    int fatigue_band;
    unsigned char pathing_cooldown;
    unsigned char attributes[W8_MONSTER_ATTR_COUNT];
    unsigned char condition_binding_mask;
    bool within_viewing_distance;
    bool fMotionless;
    float scale;
    bool death_processed;
    signed char movement_stall_ticks;
    unsigned char ai_mode;
    unsigned char unknown_256[0x30];
    W8PartyThreatRecord party_threat;
    std::uint32_t monster_visibility;
    CombatTarget Target;
    W8MonsterSummonKind summoned;
    signed char charm_strength;
    bool fMissileReleased;
    bool fSpellReleased;
    W8MonsterActionKind action_kind;
    int action_detail;
    unsigned int spell_power_level;
    unsigned char unknown_2ed[4];
    int bound_npc_index;
    int heard_noise_margin;
    unsigned int spell_points;
    W8MonsterControlState control_state;
    bool saved_mirror_x;
    unsigned char look_time;
    unsigned char pause_time;
    W8TargetSource condition_target;
    srVector3T<float> movement_watch_position;
    int elemental_summon;
    W8VisibilityRecord player_visibility;
    unsigned char unknown_379;
    bool has_projectile_origin;
    unsigned char unknown_37b;
    bool has_spell_origin;
    unsigned char unknown_37d[0xa8];
};

/* The on-disk W8MonsterGroup; field meanings are documented there. */
struct MonsterGroup {
    int group_id;
    unsigned int member_count;
    std::uint32_t monsters;
    int visible_member_count;
    int selectable_member_count;
    int active_member_count;
    int monster_id;
    srVector3T<float> centre;
    bool members_active;
    bool fInCombat;
    W8Disposition ubDisposition;
    unsigned char unknown_2b;
    bool alternate_name;
    unsigned char group_state[0x6e];
    int highlighted_member;
    int leader_location_id;
    int leader_group_id;
    int allied_group_ids[4];
    srVector3T<float> formation;
    bool encounter_registered;
    unsigned int version;
    bool engagement;
    unsigned char engagement_ticks;
    bool forced_neutral;
    int hostility_set_at;
    int spawn_time;
    bool encounter_ended;
    unsigned char unknown_d4[0x57];
};

/* The on-disk W8WorldItem; field meanings are documented there. */
struct WorldItem {
    int runtime_id;
    std::uint32_t p3D;
    bool fActive;
    W8ItemInstance item;
    srVector3T<float> position;
    unsigned char unknown_21[4];
    int entity_flags;
    unsigned int flags;
    int saved_marker;
    std::uint32_t next;
    float vertical_velocity;
    int sector_id;
    unsigned char unknown_3d[0x70];
};

/* The on-disk W8NpcState; field meanings are documented there. */
struct NpcState {
    std::uint32_t script_file;
    unsigned short spawned;
    std::uint32_t record;
    std::uint32_t items;
    int restock_clock;
    int maintenance_clock;
    int location_id;
    bool has_monster;
    unsigned char disposition;
    bool dismissed_flag;
    bool greeting_pending;
    unsigned int dismissed_timer;
    bool flag;
    bool flag0;
    unsigned char level_band;
    bool is_present;
    bool is_grouped;
    std::uint32_t character;
    signed char group_index;
    unsigned char partner_index;
    bool party_noticed;
    unsigned char name_style;
    unsigned char bound_level;
    short item_ids[40];
    int gold;
    unsigned char suspicion;
    unsigned int refusal_flags;
    int topics[5];
    char restore_entity_name[0x28];
    bool pending_restore;
    unsigned char pending_restore_level;
    unsigned char binding_unavailable;
    bool talk_cooldown_active;
    bool trade_cooldown_active;
    unsigned short trade_pool;
    int talk_cooldown_clock;
    int trade_cooldown_clock;
    unsigned char service_flags[0x14];
    unsigned char incapacitated;
    unsigned char event_pending;
    bool restored;
    int event_clock;
    unsigned char disposition_at_open;
    unsigned char healer_assist;
    unsigned char item_assist;
    short known_facts[14];
    W8Dice item_count_dice;
    bool pending_release;
    unsigned char pending_release_level;
    unsigned char restore_done;
    unsigned char item_weights[40];
};

#pragma pack(pop)

static_assert(sizeof(MonsterInfo) == 0x425);
static_assert(offsetof(MonsterInfo, modifiers) == 0x1db);
static_assert(offsetof(MonsterInfo, heard_noise_margin) == 0x2f5);
static_assert(offsetof(MonsterInfo, spell_points) == 0x2f9);
static_assert(sizeof(MonsterGroup) == 0x12b);
static_assert(sizeof(WorldItem) == 0xad);
static_assert(offsetof(WorldItem, next) == 0x31);
static_assert(sizeof(NpcState) == 0x13d);
static_assert(offsetof(NpcState, character) == 0x27);
static_assert(std::is_trivially_copyable_v<MonsterInfo> && std::is_trivially_copyable_v<MonsterGroup> &&
              std::is_trivially_copyable_v<WorldItem> && std::is_trivially_copyable_v<NpcState>);

template<> struct Record<W8MonsterInfo> {
    using Disk = MonsterInfo;
    template<class Runtime, class Bytes, class Value, class Text>
    static void fields(Runtime& runtime, Bytes bytes, Value value, Text text)
    {
        const auto field = [&](std::size_t offset, std::size_t size) { return bytes.subspan(offset, size); };
        value(&runtime.location_id, field(offsetof(Disk, location_id), sizeof(Disk::location_id)));
        value(&runtime.monster_group_id, field(offsetof(Disk, monster_group_id), sizeof(Disk::monster_group_id)));
        value(&runtime.monster_species, field(offsetof(Disk, monster_species), sizeof(Disk::monster_species)));
        pointer_slot(runtime.p3D, field(offsetof(Disk, p3D), sizeof(Disk::p3D)));
        pointer_slot(runtime.pCombat, field(offsetof(Disk, pCombat), sizeof(Disk::pCombat)));
        value(&runtime.fActive, field(offsetof(Disk, fActive), sizeof(Disk::fActive)));
        value(&runtime.fInCombat, field(offsetof(Disk, fInCombat), sizeof(Disk::fInCombat)));
        value(&runtime.ubDisposition, field(offsetof(Disk, ubDisposition), sizeof(Disk::ubDisposition)));
        value(&runtime.position, field(offsetof(Disk, position), sizeof(Disk::position)));
        value(&runtime.derived, field(offsetof(Disk, derived), sizeof(Disk::derived)));
        value(&runtime.uiHPMax, field(offsetof(Disk, uiHPMax), sizeof(Disk::uiHPMax)));
        value(&runtime.hp_current, field(offsetof(Disk, hp_current), sizeof(Disk::hp_current)));
        value(&runtime.stamina_max, field(offsetof(Disk, stamina_max), sizeof(Disk::stamina_max)));
        value(&runtime.stamina, field(offsetof(Disk, stamina), sizeof(Disk::stamina)));
        value(&runtime.heard_noise_position,
              field(offsetof(Disk, heard_noise_position), sizeof(Disk::heard_noise_position)));
        value(&runtime.heard_noise_radius, field(offsetof(Disk, heard_noise_radius), sizeof(Disk::heard_noise_radius)));
        value(&runtime.hp_regen_rate, field(offsetof(Disk, hp_regen_rate), sizeof(Disk::hp_regen_rate)));
        value(&runtime.hp_regen_accumulator,
              field(offsetof(Disk, hp_regen_accumulator), sizeof(Disk::hp_regen_accumulator)));
        value(&runtime.stamina_regen_rate, field(offsetof(Disk, stamina_regen_rate), sizeof(Disk::stamina_regen_rate)));
        value(&runtime.stamina_regen_accumulator,
              field(offsetof(Disk, stamina_regen_accumulator), sizeof(Disk::stamina_regen_accumulator)));
        value(&runtime.uiCondition, field(offsetof(Disk, uiCondition), sizeof(Disk::uiCondition)));
        value(&runtime.enchantments, field(offsetof(Disk, enchantments), sizeof(Disk::enchantments)));
        value(&runtime.highest_condition, field(offsetof(Disk, highest_condition), sizeof(Disk::highest_condition)));
        value(&runtime.condition_argument, field(offsetof(Disk, condition_argument), sizeof(Disk::condition_argument)));
        value(&runtime.effect_slots, field(offsetof(Disk, effect_slots), sizeof(Disk::effect_slots)));
        value(&runtime.modifiers, field(offsetof(Disk, modifiers), sizeof(Disk::modifiers)));
        value(&runtime.fatigue_band, field(offsetof(Disk, fatigue_band), sizeof(Disk::fatigue_band)));
        value(&runtime.pathing_cooldown, field(offsetof(Disk, pathing_cooldown), sizeof(Disk::pathing_cooldown)));
        value(&runtime.attributes, field(offsetof(Disk, attributes), sizeof(Disk::attributes)));
        value(&runtime.condition_binding_mask,
              field(offsetof(Disk, condition_binding_mask), sizeof(Disk::condition_binding_mask)));
        value(&runtime.within_viewing_distance,
              field(offsetof(Disk, within_viewing_distance), sizeof(Disk::within_viewing_distance)));
        value(&runtime.fMotionless, field(offsetof(Disk, fMotionless), sizeof(Disk::fMotionless)));
        value(&runtime.scale, field(offsetof(Disk, scale), sizeof(Disk::scale)));
        value(&runtime.death_processed, field(offsetof(Disk, death_processed), sizeof(Disk::death_processed)));
        value(&runtime.movement_stall_ticks,
              field(offsetof(Disk, movement_stall_ticks), sizeof(Disk::movement_stall_ticks)));
        value(&runtime.ai_mode, field(offsetof(Disk, ai_mode), sizeof(Disk::ai_mode)));
        value(&runtime.unknown_256, field(offsetof(Disk, unknown_256), sizeof(Disk::unknown_256)));
        value(&runtime.party_threat, field(offsetof(Disk, party_threat), sizeof(Disk::party_threat)));
        pointer_slot(runtime.monster_visibility,
                     field(offsetof(Disk, monster_visibility), sizeof(Disk::monster_visibility)));
        nested(runtime.Target, field(offsetof(Disk, Target), sizeof(Disk::Target)), value, text);
        value(&runtime.summoned, field(offsetof(Disk, summoned), sizeof(Disk::summoned)));
        value(&runtime.charm_strength, field(offsetof(Disk, charm_strength), sizeof(Disk::charm_strength)));
        value(&runtime.fMissileReleased, field(offsetof(Disk, fMissileReleased), sizeof(Disk::fMissileReleased)));
        value(&runtime.fSpellReleased, field(offsetof(Disk, fSpellReleased), sizeof(Disk::fSpellReleased)));
        value(&runtime.action_kind, field(offsetof(Disk, action_kind), sizeof(Disk::action_kind)));
        value(&runtime.action_detail, field(offsetof(Disk, action_detail), sizeof(Disk::action_detail)));
        value(&runtime.spell_power_level, field(offsetof(Disk, spell_power_level), sizeof(Disk::spell_power_level)));
        value(&runtime.unknown_2ed, field(offsetof(Disk, unknown_2ed), sizeof(Disk::unknown_2ed)));
        value(&runtime.bound_npc_index, field(offsetof(Disk, bound_npc_index), sizeof(Disk::bound_npc_index)));
        value(&runtime.heard_noise_margin, field(offsetof(Disk, heard_noise_margin), sizeof(Disk::heard_noise_margin)));
        value(&runtime.spell_points, field(offsetof(Disk, spell_points), sizeof(Disk::spell_points)));
        value(&runtime.control_state, field(offsetof(Disk, control_state), sizeof(Disk::control_state)));
        value(&runtime.saved_mirror_x, field(offsetof(Disk, saved_mirror_x), sizeof(Disk::saved_mirror_x)));
        value(&runtime.look_time, field(offsetof(Disk, look_time), sizeof(Disk::look_time)));
        value(&runtime.pause_time, field(offsetof(Disk, pause_time), sizeof(Disk::pause_time)));
        value(&runtime.condition_target, field(offsetof(Disk, condition_target), sizeof(Disk::condition_target)));
        value(&runtime.movement_watch_position,
              field(offsetof(Disk, movement_watch_position), sizeof(Disk::movement_watch_position)));
        value(&runtime.elemental_summon, field(offsetof(Disk, elemental_summon), sizeof(Disk::elemental_summon)));
        value(&runtime.player_visibility, field(offsetof(Disk, player_visibility), sizeof(Disk::player_visibility)));
        value(&runtime.unknown_379, field(offsetof(Disk, unknown_379), sizeof(Disk::unknown_379)));
        value(&runtime.has_projectile_origin,
              field(offsetof(Disk, has_projectile_origin), sizeof(Disk::has_projectile_origin)));
        value(&runtime.unknown_37b, field(offsetof(Disk, unknown_37b), sizeof(Disk::unknown_37b)));
        value(&runtime.has_spell_origin, field(offsetof(Disk, has_spell_origin), sizeof(Disk::has_spell_origin)));
        value(&runtime.unknown_37d, field(offsetof(Disk, unknown_37d), sizeof(Disk::unknown_37d)));
    }
};

template<> struct Record<W8MonsterGroup> {
    using Disk = MonsterGroup;
    template<class Runtime, class Bytes, class Value, class Text>
    static void fields(Runtime& runtime, Bytes bytes, Value value, Text text)
    {
        const auto field = [&](std::size_t offset, std::size_t size) { return bytes.subspan(offset, size); };
        value(&runtime.group_id, field(offsetof(Disk, group_id), sizeof(Disk::group_id)));
        value(&runtime.member_count, field(offsetof(Disk, member_count), sizeof(Disk::member_count)));
        presence_slot(runtime.monsters, field(offsetof(Disk, monsters), sizeof(Disk::monsters)));
        value(&runtime.visible_member_count,
              field(offsetof(Disk, visible_member_count), sizeof(Disk::visible_member_count)));
        value(&runtime.selectable_member_count,
              field(offsetof(Disk, selectable_member_count), sizeof(Disk::selectable_member_count)));
        value(&runtime.active_member_count,
              field(offsetof(Disk, active_member_count), sizeof(Disk::active_member_count)));
        value(&runtime.monster_id, field(offsetof(Disk, monster_id), sizeof(Disk::monster_id)));
        value(&runtime.centre, field(offsetof(Disk, centre), sizeof(Disk::centre)));
        value(&runtime.members_active, field(offsetof(Disk, members_active), sizeof(Disk::members_active)));
        value(&runtime.fInCombat, field(offsetof(Disk, fInCombat), sizeof(Disk::fInCombat)));
        value(&runtime.ubDisposition, field(offsetof(Disk, ubDisposition), sizeof(Disk::ubDisposition)));
        value(&runtime.unknown_2b, field(offsetof(Disk, unknown_2b), sizeof(Disk::unknown_2b)));
        value(&runtime.alternate_name, field(offsetof(Disk, alternate_name), sizeof(Disk::alternate_name)));
        value(&runtime.group_state, field(offsetof(Disk, group_state), sizeof(Disk::group_state)));
        value(&runtime.highlighted_member, field(offsetof(Disk, highlighted_member), sizeof(Disk::highlighted_member)));
        value(&runtime.leader_location_id, field(offsetof(Disk, leader_location_id), sizeof(Disk::leader_location_id)));
        value(&runtime.leader_group_id, field(offsetof(Disk, leader_group_id), sizeof(Disk::leader_group_id)));
        value(&runtime.allied_group_ids, field(offsetof(Disk, allied_group_ids), sizeof(Disk::allied_group_ids)));
        value(&runtime.formation, field(offsetof(Disk, formation), sizeof(Disk::formation)));
        value(&runtime.encounter_registered,
              field(offsetof(Disk, encounter_registered), sizeof(Disk::encounter_registered)));
        value(&runtime.version, field(offsetof(Disk, version), sizeof(Disk::version)));
        value(&runtime.engagement, field(offsetof(Disk, engagement), sizeof(Disk::engagement)));
        value(&runtime.engagement_ticks, field(offsetof(Disk, engagement_ticks), sizeof(Disk::engagement_ticks)));
        value(&runtime.forced_neutral, field(offsetof(Disk, forced_neutral), sizeof(Disk::forced_neutral)));
        value(&runtime.hostility_set_at, field(offsetof(Disk, hostility_set_at), sizeof(Disk::hostility_set_at)));
        value(&runtime.spawn_time, field(offsetof(Disk, spawn_time), sizeof(Disk::spawn_time)));
        value(&runtime.encounter_ended, field(offsetof(Disk, encounter_ended), sizeof(Disk::encounter_ended)));
        value(&runtime.unknown_d4, field(offsetof(Disk, unknown_d4), sizeof(Disk::unknown_d4)));
    }
};

template<> struct Record<W8WorldItem> {
    using Disk = WorldItem;
    template<class Runtime, class Bytes, class Value, class Text>
    static void fields(Runtime& runtime, Bytes bytes, Value value, Text text)
    {
        const auto field = [&](std::size_t offset, std::size_t size) { return bytes.subspan(offset, size); };
        value(&runtime.runtime_id, field(offsetof(Disk, runtime_id), sizeof(Disk::runtime_id)));
        pointer_slot(runtime.p3D, field(offsetof(Disk, p3D), sizeof(Disk::p3D)));
        value(&runtime.fActive, field(offsetof(Disk, fActive), sizeof(Disk::fActive)));
        value(&runtime.item, field(offsetof(Disk, item), sizeof(Disk::item)));
        value(&runtime.position, field(offsetof(Disk, position), sizeof(Disk::position)));
        value(&runtime.unknown_21, field(offsetof(Disk, unknown_21), sizeof(Disk::unknown_21)));
        value(&runtime.entity_flags, field(offsetof(Disk, entity_flags), sizeof(Disk::entity_flags)));
        value(&runtime.flags, field(offsetof(Disk, flags), sizeof(Disk::flags)));
        value(&runtime.saved_marker, field(offsetof(Disk, saved_marker), sizeof(Disk::saved_marker)));
        pointer_slot(runtime.next, field(offsetof(Disk, next), sizeof(Disk::next)));
        value(&runtime.vertical_velocity, field(offsetof(Disk, vertical_velocity), sizeof(Disk::vertical_velocity)));
        value(&runtime.sector_id, field(offsetof(Disk, sector_id), sizeof(Disk::sector_id)));
        value(&runtime.unknown_3d, field(offsetof(Disk, unknown_3d), sizeof(Disk::unknown_3d)));
    }
};

template<> struct Record<W8NpcState> {
    using Disk = NpcState;
    template<class Runtime, class Bytes, class Value, class Text>
    static void fields(Runtime& runtime, Bytes bytes, Value value, Text text)
    {
        const auto field = [&](std::size_t offset, std::size_t size) { return bytes.subspan(offset, size); };
        pointer_slot(runtime.script_file, field(offsetof(Disk, script_file), sizeof(Disk::script_file)));
        value(&runtime.spawned, field(offsetof(Disk, spawned), sizeof(Disk::spawned)));
        pointer_slot(runtime.record, field(offsetof(Disk, record), sizeof(Disk::record)));
        presence_slot(runtime.items, field(offsetof(Disk, items), sizeof(Disk::items)));
        value(&runtime.restock_clock, field(offsetof(Disk, restock_clock), sizeof(Disk::restock_clock)));
        value(&runtime.maintenance_clock, field(offsetof(Disk, maintenance_clock), sizeof(Disk::maintenance_clock)));
        value(&runtime.location_id, field(offsetof(Disk, location_id), sizeof(Disk::location_id)));
        value(&runtime.has_monster, field(offsetof(Disk, has_monster), sizeof(Disk::has_monster)));
        value(&runtime.disposition, field(offsetof(Disk, disposition), sizeof(Disk::disposition)));
        value(&runtime.dismissed_flag, field(offsetof(Disk, dismissed_flag), sizeof(Disk::dismissed_flag)));
        value(&runtime.greeting_pending, field(offsetof(Disk, greeting_pending), sizeof(Disk::greeting_pending)));
        value(&runtime.dismissed_timer, field(offsetof(Disk, dismissed_timer), sizeof(Disk::dismissed_timer)));
        value(&runtime.flag, field(offsetof(Disk, flag), sizeof(Disk::flag)));
        value(&runtime.flag0, field(offsetof(Disk, flag0), sizeof(Disk::flag0)));
        value(&runtime.level_band, field(offsetof(Disk, level_band), sizeof(Disk::level_band)));
        value(&runtime.is_present, field(offsetof(Disk, is_present), sizeof(Disk::is_present)));
        value(&runtime.is_grouped, field(offsetof(Disk, is_grouped), sizeof(Disk::is_grouped)));
        presence_slot(runtime.character, field(offsetof(Disk, character), sizeof(Disk::character)));
        value(&runtime.group_index, field(offsetof(Disk, group_index), sizeof(Disk::group_index)));
        value(&runtime.partner_index, field(offsetof(Disk, partner_index), sizeof(Disk::partner_index)));
        value(&runtime.party_noticed, field(offsetof(Disk, party_noticed), sizeof(Disk::party_noticed)));
        value(&runtime.name_style, field(offsetof(Disk, name_style), sizeof(Disk::name_style)));
        value(&runtime.bound_level, field(offsetof(Disk, bound_level), sizeof(Disk::bound_level)));
        value(&runtime.item_ids, field(offsetof(Disk, item_ids), sizeof(Disk::item_ids)));
        value(&runtime.gold, field(offsetof(Disk, gold), sizeof(Disk::gold)));
        value(&runtime.suspicion, field(offsetof(Disk, suspicion), sizeof(Disk::suspicion)));
        value(&runtime.refusal_flags, field(offsetof(Disk, refusal_flags), sizeof(Disk::refusal_flags)));
        value(&runtime.topics, field(offsetof(Disk, topics), sizeof(Disk::topics)));
        value(&runtime.restore_entity_name,
              field(offsetof(Disk, restore_entity_name), sizeof(Disk::restore_entity_name)));
        value(&runtime.pending_restore, field(offsetof(Disk, pending_restore), sizeof(Disk::pending_restore)));
        value(&runtime.pending_restore_level,
              field(offsetof(Disk, pending_restore_level), sizeof(Disk::pending_restore_level)));
        value(&runtime.binding_unavailable,
              field(offsetof(Disk, binding_unavailable), sizeof(Disk::binding_unavailable)));
        value(&runtime.talk_cooldown_active,
              field(offsetof(Disk, talk_cooldown_active), sizeof(Disk::talk_cooldown_active)));
        value(&runtime.trade_cooldown_active,
              field(offsetof(Disk, trade_cooldown_active), sizeof(Disk::trade_cooldown_active)));
        value(&runtime.trade_pool, field(offsetof(Disk, trade_pool), sizeof(Disk::trade_pool)));
        value(&runtime.talk_cooldown_clock,
              field(offsetof(Disk, talk_cooldown_clock), sizeof(Disk::talk_cooldown_clock)));
        value(&runtime.trade_cooldown_clock,
              field(offsetof(Disk, trade_cooldown_clock), sizeof(Disk::trade_cooldown_clock)));
        value(&runtime.service_flags, field(offsetof(Disk, service_flags), sizeof(Disk::service_flags)));
        value(&runtime.incapacitated, field(offsetof(Disk, incapacitated), sizeof(Disk::incapacitated)));
        value(&runtime.event_pending, field(offsetof(Disk, event_pending), sizeof(Disk::event_pending)));
        value(&runtime.restored, field(offsetof(Disk, restored), sizeof(Disk::restored)));
        value(&runtime.event_clock, field(offsetof(Disk, event_clock), sizeof(Disk::event_clock)));
        value(&runtime.disposition_at_open,
              field(offsetof(Disk, disposition_at_open), sizeof(Disk::disposition_at_open)));
        value(&runtime.healer_assist, field(offsetof(Disk, healer_assist), sizeof(Disk::healer_assist)));
        value(&runtime.item_assist, field(offsetof(Disk, item_assist), sizeof(Disk::item_assist)));
        value(&runtime.known_facts, field(offsetof(Disk, known_facts), sizeof(Disk::known_facts)));
        value(&runtime.item_count_dice, field(offsetof(Disk, item_count_dice), sizeof(Disk::item_count_dice)));
        value(&runtime.pending_release, field(offsetof(Disk, pending_release), sizeof(Disk::pending_release)));
        value(&runtime.pending_release_level,
              field(offsetof(Disk, pending_release_level), sizeof(Disk::pending_release_level)));
        value(&runtime.restore_done, field(offsetof(Disk, restore_done), sizeof(Disk::restore_done)));
        value(&runtime.item_weights, field(offsetof(Disk, item_weights), sizeof(Disk::item_weights)));
    }
};

} // namespace wiz8::retail

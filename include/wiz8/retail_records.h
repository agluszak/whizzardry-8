#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "wiz8/data/item_records.h"
#include "wiz8/entities/missile.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/retail_geometry_types.h"
#include "wiz8/rules/character.h"

namespace wiz8::retail {
// Format-defined storage only. Text fields are bytes so odd UTF-16 offsets
// cannot accidentally become unaligned char16_t or wchar_t accesses.
inline constexpr std::size_t kItemDatabaseRecordBytes = 0x10d;
inline constexpr std::size_t kSpellRuntimeRecordBytes = 0x1bf;
inline constexpr std::size_t kFactDatabaseRecordBytes = 0x1d8;
inline constexpr std::size_t kNpcCharacterTemplateBytes = 0x206;
inline constexpr std::size_t kNpcDatabaseRecordBytes = 0x309;
inline constexpr std::size_t kLevelDatabaseRecordBytes = 0xd8;
inline constexpr std::size_t kMonsterRecordBytes = 0x297;
inline constexpr std::size_t kCharacterBytes = 0x1862;
inline constexpr std::size_t kPartySlotBytes = 0x106;
inline constexpr std::size_t kMissileTableRecordBytes = 0x1e5;

#pragma pack(push, 1)
struct CameraState {
    Vec3f position;
    float pitch[6], yaw[6];
};
static_assert(sizeof(CameraState) == 0x3c);
struct TargetSource {
    std::int32_t iType;
    int iChar;
    int iMonsterID;
    Vec3f point;
    unsigned char auto_cast;
    unsigned char name_known;
    bool aim_resolved;
    bool fReflection;
    bool fBackfire;
    unsigned char point_source;
    unsigned char target_diverted;
    unsigned char spell_difficulty;
    unsigned char unknown_20;
    unsigned char item_cast;
    unsigned char unknown_22[0x12];
};
static_assert(sizeof(TargetSource) == 0x34);

struct CombatTarget {
    std::int32_t iType, iChar, iMonsterID, iGroupID;
    float point[3];
    std::uint32_t item_marker; // Saved address, never a runtime pointer/handle.
};
static_assert(sizeof(CombatTarget) == 0x20);
struct PartySlot {
    bool fOccupied;
    W8ActionKind pending_action;
    int attack_mode[4];
    std::array<std::byte, 8> pending_action_detail;
    CombatTarget target_out_of_combat;
    W8ActionKind action;
    int action_detail0;
    std::array<std::byte, 8> action_detail1;
    CombatTarget target_in_combat;
    W8ActionKind action_kind;
    int action_detail;
    int spell_id;
    std::array<std::byte, 8> spell_detail;
    CombatTarget spell_target;
    std::array<std::byte, 8> item_detail;
    CombatTarget item_target;
    int item_id;
    unsigned char item_origin;
    unsigned short item_slot;
    signed char queued_action;
    CombatTarget breath_target;
    int party_order_index;
    bool item_action_pending;
    float movement_fatigue;
    int npc_index;
    bool npc_bound;
    unsigned int pending_event_type;
    unsigned char portrait_advance;
    bool action_is_berserk;
    bool weapon_swap_pending;
};
static_assert(std::is_trivially_copyable_v<PartySlot>);
static_assert(sizeof(PartySlot) == kPartySlotBytes);
static_assert(offsetof(PartySlot, pending_action) == 0x01);
static_assert(offsetof(PartySlot, target_out_of_combat) == 0x1d);
static_assert(offsetof(PartySlot, breath_target) == 0xd1);
static_assert(offsetof(PartySlot, movement_fatigue) == 0xf6);
static_assert(offsetof(PartySlot, weapon_swap_pending) == 0x105);

struct MissileTableRecord {
    std::array<std::byte, 0x100> display_name;
    char cycle_name[0x40];
    float radius;
    W8AttackMode attack_mode;
    unsigned char unknown_148[8];
    int magnitude_base;
    bool spell_missile;
    unsigned char condition_chances[W8_ATTACK_EFFECT_COUNT];
    int weapon_sound_class;
    unsigned char unknown_169[0x7c];
};
static_assert(std::is_trivially_copyable_v<MissileTableRecord>);
static_assert(sizeof(MissileTableRecord) == kMissileTableRecordBytes);
static_assert(offsetof(MissileTableRecord, cycle_name) == 0x100);
static_assert(offsetof(MissileTableRecord, weapon_sound_class) == 0x165);

struct ItemDatabaseRecord {
    std::array<std::byte, 60> display_name;
    ItemProperties properties;
    char internal_name[0x20];
    ItemEffects effects;
    char video_object_name[0x18];
    char sound_name[0x28];
};
static_assert(std::is_trivially_copyable_v<ItemDatabaseRecord>);
static_assert(sizeof(ItemDatabaseRecord) == kItemDatabaseRecordBytes);
static_assert(offsetof(ItemDatabaseRecord, properties) == 0x3c);
static_assert(offsetof(ItemDatabaseRecord, internal_name) == 0x8d);
static_assert(offsetof(ItemDatabaseRecord, effects) == 0xad);
static_assert(offsetof(ItemDatabaseRecord, video_object_name) == 0xcd);
struct SpellRuntimeRecord {
    char database_name[64]; /* 0x000 */
    unsigned char unknown_040[4];
    /* 0x044: base duration the spell-info dialog prints. */
    int duration;
    unsigned char alchemy_spell; /* 0x048 */
    int spell_point_cost;        /* 0x049: per power level */
    /* 0x04d: per-level duration added to duration on the same line. */
    int duration_per_level;
    W8Dice effect_dice; /* 0x051 */
    unsigned char unknown_055;
    int spell_level;              /* 0x056: zero through seven */
    unsigned char wizardry_spell; /* 0x05a */
    char resource_name[64];       /* 0x05b: visual/MLS resource basename */
    std::array<std::byte, 128> display_name;     /* 0x09b */
    unsigned char unknown_11b[4];
    unsigned char divinity_spell; /* 0x11f */
    unsigned char psionics_spell; /* 0x120 */
    float effect_radius;          /* 0x121 */
    unsigned char unknown_125;
    /* 0x126: a monster may cast the spell at all. MonsterOKToCastSpell reports
       a spell without it by name and asserts. */
    unsigned char monster_castable;
    /* 0x127: radius added per power level; PopulateSpellTargetMarkers scales
       the pair as (radius_per_level * power + effect_radius) * 500. */
    float radius_per_level;
    /* 0x12b: the power class the spell-casting view copies into
       iSpellPowerClass (asserted not BAD_INDEX). It selects how
       power pips work: 2 prices a max cast, 3 is a fixed-cost spell, and
       SpellInfoDialog shows the alternate caption for 3. */
    W8SpellPowerClass power_class;
    /* 0x12f: the range category a monster casting this spell needs. */
    W8RangeCategory range_category;
    W8SpellRealm realm;            /* 0x133 */
    W8SpellTargetType target_type; /* 0x137 */
    /* 0x13b: when the spell may be cast. SpellUsableNow switches on it and its
       assertion calls it uiSpellUsableWhen with a SPELL_USAGE_COUNT of five. */
    W8SpellUsage usable_when;
    /* 0x13f: the spell has to be aimed before it can be cast. */
    unsigned char needs_aim;
    /* 0x140: the MissileTables.dbs row index the cast fires its effect
       through when missile_delivered is set. */
    int missile_index;
    /* 0x144: the spell's effect is delivered by a missile in flight, so its
       queued effect stays alive until the missile lands. Set for the ten
       projectile spells - Frost, Heal Wounds, Make Wounds, Sleep, Stamina,
       Terror, Noxious Fumes, Crush, Return to Portal and the monster Special
       Attack Cone. */
    unsigned char missile_delivered;
    unsigned char unknown_145[2];
    /* 0x147: SpellInfoDialog prints the effect-dice line when this is set. */
    int show_effect_dice;
    char sound_name[0x74]; /* 0x14b: relative to Data\Spells\Sounds */
};
static_assert(std::is_trivially_copyable_v<SpellRuntimeRecord>);
static_assert(sizeof(SpellRuntimeRecord) == 0x1bf, "SpellRuntimeRecord_size");
static_assert(offsetof(SpellRuntimeRecord, duration) == 0x044, "SpellRuntimeRecord_duration");
static_assert(offsetof(SpellRuntimeRecord, duration_per_level) == 0x04d,
              "SpellRuntimeRecord_duration_per_level");
static_assert(offsetof(SpellRuntimeRecord, show_effect_dice) == 0x147,
              "SpellRuntimeRecord_show_effect_dice");
struct FactDatabaseRecord {
    W8FactId identifier;      /* 0x000 */
    char symbolic_name[0x32]; /* 0x004 .. 0x035 */
    /* 0x036: the journal shades this fact's text when its value is true. */
    unsigned char highlight_when_true;
    /* 0x037: the visibility level the journal entry needs. */
    signed char visibility;
    /* 0x038 and 0x100: the alternate and normal journal descriptions, wide,
       selected by the fact's current value. */
    std::array<std::byte, 200> alternate_description;
    std::array<std::byte, 216> description;
};
static_assert(std::is_trivially_copyable_v<FactDatabaseRecord>);
static_assert(sizeof(FactDatabaseRecord) == 0x1d8, "FactDatabaseRecord_size_must_be_0x1d8");
struct NpcCharacterTemplate {
    std::array<std::byte, 20> name;
    std::array<std::byte, 12> name_part_2;
    unsigned char unknown_01a[0x44]; // Unidentified retail data, preserved verbatim.
    W8Profession profession;
    W8Race race;
    int table_value;
    unsigned int level;
    int attributes[7];
    int skills[0x29];
    unsigned char spells[0x72];
    unsigned short equipment_present[12];
    unsigned short equipment_ids[12];
    unsigned short backpack_present[8];
    unsigned short backpack_ids[8];
    signed char gender;
    short wanted_item_ids[3];
    unsigned char unknown_1fd[9];
};
static_assert(std::is_trivially_copyable_v<NpcCharacterTemplate>);
static_assert(sizeof(NpcCharacterTemplate) == 0x206, "NpcCharacterTemplate_size_must_be_0x206");
struct NpcDatabaseRecord {
    unsigned short
        version; /* 0x000: two in the corpus; the rule tail loads only when this exceeds 1 */
    /* 0x002: trade-pool stock count; CreateNpcRuntimeNode copies it into the
       runtime state's trade_pool. */
    short trade_pool;
    /* 0x004: the wide source name the level-entry rebinding prefixes with an
       underscore to build the NPC's trigger name. */
    std::array<std::byte, 80> source_name;
    /* 0x054: monster-bound NPC: no standalone runtime node is created, its
       state comes through the monster binding, and releasing the binding
       marks it unavailable. */
    unsigned char monster_bound;
    /* 0x055: the NPC's stock is released only while this is set. */
    unsigned char owns_stock;
    /* 0x056: merchant: still opens dialogue when the disposition band is
       hostile, and item drops go through the trade transcript layout. */
    unsigned char merchant;
    /* 0x057: the NPC carries an RPC character and can join a monster group,
       which is what makes the group index on its runtime state meaningful. */
    unsigned char has_group;
    /* 0x058: the NPC's kind. Kind twenty refuses to trade. */
    int kind;
    /* 0x05c: the disposition byte CreateNpcRuntimeNode starts the state with. */
    unsigned char disposition;
    /* 0x05d/0x05e: the signed scales the charm and talk interactions fold into
       the disposition shift; the sub-one path negates the /5 quotient. */
    signed char charm_scale;
    signed char talk_scale;
    /* 0x05f: the faction the NPC belongs to; zero leaves GetNpcDisposition on
       the record's own disposition byte. */
    unsigned char faction;
    /* 0x060: one bit per named-person alias the NPC answers to; read as one
       dword by FindNpcNameOrPlaceQuote. */
    unsigned int name_alias_mask;
    /* 0x064: one-based index into GameData::item_tables selecting the record's item
       table, copied into the runtime state. */
    int item_table_id;
    /* 0x068: one bit per service the NPC offers. */
    unsigned int service_flags;
    unsigned char unknown_06c[2];
    /* 0x06e: an allied faction; while a front-rank party member's bound NPC
       belongs to it, GetNpcDisposition pins this NPC's answer at fifty. The
       compare sign-extends it. */
    signed char allied_faction;
    /* 0x06f: the minimum average party level before this NPC's group can be
       invited. */
    unsigned char min_party_level;
    /* 0x070/0x074: the level this NPC restores its binding at and the
       FindEntityByName key the restore moves its monster to - the default
       pair copied into the runtime state's pending restore. */
    char restore_level;
    unsigned char unknown_071[3];
    char restore_entity_name[0x29];
    /* 0x09d: set when the record carries no appended item-stock
       rules; the load path reads them only while this is clear. */
    unsigned char no_item_stock;
    /* 0x09e: what the NPC is called, unless a fact substitutes another name.
       The wide RPC-character name at 0x0c4 bounds the string extent. */
    char display_name[0x26];
    NpcCharacterTemplate character; /* 0x0c4 */
    std::uint32_t item_stock_rules; /* 0x2ca: W8NpcItemStockRule* elements */
    /* 0x2ce/0x2d2: the trade price factors. CalculateTradeStackPrice adds the
       bargained adjustment to buy_price_factor when the NPC buys from the
       party and subtracts it from sell_price_factor when the party buys. */
    float buy_price_factor;
    float sell_price_factor;
    /* 0x2d6: one accepted trade-item class per bit. */
    unsigned int trade_item_class_mask;
    unsigned char unknown_2da[0x10];
    /* 0x2ea: voice-script NPC: picks the VOC_ script/sound prefix over NPC_
       and gates the normal dialogue paths. */
    unsigned char voice_script;
    /* 0x2eb: the purse the NPC carries; the party gets it when the NPC's
       monster dies. */
    int gold;
    unsigned char combat_script_notice_enabled;       /* 0x2ef: hostile-NPC combat notice */
    unsigned char allow_dismissed_departure_dialogue; /* 0x2f0: permits dismissed-NPC dialogue */
    unsigned char unknown_2f1[0x18];
};
static_assert(std::is_trivially_copyable_v<NpcDatabaseRecord>);
static_assert(sizeof(NpcDatabaseRecord) == 0x309, "NpcDatabaseRecord_size_must_be_0x309");
static_assert(offsetof(NpcDatabaseRecord, combat_script_notice_enabled) == 0x2ef,
              "NpcDatabaseRecord_combat_script_notice_offset");
static_assert(offsetof(NpcDatabaseRecord, allow_dismissed_departure_dialogue) == 0x2f0,
              "NpcDatabaseRecord_dialogue_departure_script_offset");
struct LevelDatabaseRecord {
    std::array<std::byte, 60> display_name;
    /* 0x3c..0x50: the per-level random-encounter budget parameters, all five
       read by UpdateRandomEncounterBudget and the sixth by the culling pass. */
    int maximum_random_encounters; /* 0x3c */
    int minimum_random_encounters; /* 0x40 */
    int maximum_encounter_budget;  /* 0x44 */
    int minimum_encounter_budget;  /* 0x48 */
    int encounter_budget_period;   /* 0x4c: elapsed-time divisor */
    int encounter_culling_seconds; /* 0x50 */
    /* 0x54: float divisor the surprise transition uses when scaling game time
       and monster-generator duration. */
    float gameplay_time_scale;
    unsigned char unknown_058[0x80];
};
static_assert(std::is_trivially_copyable_v<LevelDatabaseRecord>);
static_assert(offsetof(LevelDatabaseRecord, maximum_random_encounters) == 0x3c,
              "LevelDatabaseRecord_encounter_budget_offset");
static_assert(sizeof(LevelDatabaseRecord) == 0xd8, "LevelDatabaseRecord_size_must_be_0xd8");
static_assert(offsetof(LevelDatabaseRecord, gameplay_time_scale) == 0x54,
              "LevelDatabaseRecord_gameplay_time_scale_offset");
struct MonsterRecord {
    std::array<std::byte, 48> name0; /* 0x000: suffix after '#' removed at load */
    std::array<std::byte, 48> name1; /* 0x030: suffix after '#' removed at load */
    std::array<std::byte, 48> name2; /* 0x060: suffix after '#' removed at load */
    std::array<std::byte, 48> name3; /* 0x090: suffix after '#' removed at load */
    /* Cosmic Forge's Monster Editor exposes this byte as 'Can open doors'. */
    unsigned char can_open_doors;
    /* 0x0c1: rolled by the group-attack summon to size the spawned group. */
    W8Dice group_size_dice;
    W8EncounterCompanionRecord companions[2];
    /* 0x0cb: the monster's kind. The alchemy-casting rule admits kinds four,
       five and thirteen and no others, which is the only body that reads it. */
    unsigned char kind;
    /* 0x0cc: selects this monster's row in the name-prefix table, the same
       table a character indexes by sex. */
    unsigned char name_group;
    /* 0x0cd: the NPC record index this monster is bound to, fed to
       GetNpcStateByKind and compared against W8NpcState::name_style; 0xfa
       marks a record with no bound NPC. */
    unsigned char npc_kind;
    /* 0x0ce: signed stamina regeneration rate (Monster Editor: ST Regen). */
    signed char stamina_regeneration;
    /* 0x0cf: the monster's own percentage reduction on incoming damage. */
    unsigned char damage_reduction;
    /* 0x0d0: W8MonsterRecordFlag mask; retain the serialized byte width. */
    unsigned char flags;
    unsigned char attribute_values[5]; /* 0x0d1 */
    W8Dice hit_points;                 /* 0x0d6: rolled into uiHPMax/hp_current */
    W8Dice stamina_dice;               /* 0x0da: initializes maximum and current stamina */
    unsigned char unknown_0de[2];
    /* 0x0e0/0x0e1: the percentage chances the AI casts a spell or flees each
       round; a monster with a usable attack ignores them and always tries. */
    unsigned char spell_chance;
    unsigned char flee_chance;
    /* 0x0e2: the percentage chance the AI advances on the party each round;
       zero falls back to the behavior byte at +0x1c0. */
    unsigned char advance_chance;
    /* 0x0e3: Special attack selector; indexes the effect rows used by
       GroupAttacks.cpp and the corresponding display-name table. */
    unsigned char special_attack_kind;
    unsigned char initiative; /* 0x0e4: Monster Editor Initiative */
    /* 0x0e5/0x0e6: attacks and swings per round. */
    unsigned char attacks_per_round;
    unsigned char swings_per_round;
    /* 0x0e7: the monster's three attacks, named by the Combat Range.cpp
       assertions pMonsterDB->Attack[uiAttack].fHasAttack and uiAttack <
       MAX_MONSTER_ATTACKS, which is what bounds the array at three. */
    W8MonsterAttack attacks[W8_MAX_MONSTER_ATTACKS]; /* 0x0e7 */
    /* 0x14d: the ten spells the AI may cast, zero for none. */
    unsigned char spells[10];
    unsigned char attack_body_part_chances[5];
    unsigned char special_attack_cooldown;
    signed char evasion;
    /* 0x15e: Monster Editor Constitution selector; chooses the body-specific
       hit-location label row. */
    unsigned char constitution;
    /* 0x15f: the percentage of hits that land on each of the seven monster
       hit locations; the total is reported when it falls short of 100. */
    unsigned char hit_location_chances[7];
    /* 0x166/0x16d: the monster's armour per hit location and per attack mode,
       the terms TargetArmorClassAtLocation subtracts on top of evasion. */
    signed char armor_class_by_location[7];
    signed char armor_class_by_attack_mode[9];
    /* 0x176: the monster's own resistance per realm, read alongside the
       gameplay-modifier bonus wherever a character would read
       W8CharacterResistance::total. */
    unsigned char resistances[6];
    /* 0x17c: hit points gained (or lost, when negative) per game minute. */
    signed char hp_regeneration;
    unsigned char unknown_17d[4];
    unsigned int experience; /* 0x181: base experience value */
    unsigned char unknown_185[2];
    short record_id;       /* 0x187: equals the zero-based database index */
    char cycle_name[0x30]; /* 0x189: GrCycle lookup key */
    /* 0x1b9: prefer ranged actions instead of closing/backing off. */
    unsigned char prefer_ranged_actions;
    float combat_move_range; /* 0x1ba: designer-facing combat movement range */
    /* 0x1be: blocks the DEAD condition. */
    unsigned char instant_death_immune;
    /* 0x1bf/0x1c0: the Monster Editor's Combat Behaviour and Combat Morale selectors. */
    unsigned char combat_behavior;
    unsigned char combat_morale;
    /* 0x1c1: the MIPE monster list only admits records carrying -1 here, and
       stores the value itself as the selected monster index. */
    short editor_index;
    /* Cosmic Forge's Remains column consumes this as a narrow item-model
       name. The canonical rows contain creaturegoo, plantgoo or an empty
       string. It precedes the treasure entries rather than belonging to them. */
    char remains_model_name[0x30];
    /* 0x1f3: the monster's treasure table. DropMonsterLoot fires each slot
       whose count is nonzero once Random(100) stays under its chance, rolling
       the slot dice for the drop count; type 0 creates the item id directly,
       type 1 feeds the id to GenerateItemsFromTable. The gold dice lands in
       AddPartyGold. */
    W8MonsterTreasureBlock treasure;
    unsigned char attack_multiple_targets;
    /* 0x248: Monster Editor camouflage rating. */
    unsigned char camouflage0;
    unsigned char camouflage1;
    /* 0x24a: the monster cannot be targeted at all. Every sweep that gathers
       candidates drops it before any other test. */
    unsigned char untargetable;
    unsigned char unknown_24b[4];
    /* 0x24f: effective/combat monster level used by attack, resistance,
       spell and effect formulas. Distinct from the displayed level at 0x251. */
    unsigned char effective_level;
    unsigned char unknown_250;
    /* 0x251: monster level shown by MonsterInfo and related UI. */
    unsigned char display_level;
    unsigned char unknown_252;
    int model_index;           /* 0x253 */
    int alternate_model_index; /* 0x257: alternate selected value */
    int hostility_radius;      /* 0x25b: Minimal Neutrality Distance / Hostility Radius */
    std::int32_t faction_id;   /* 0x25f: W8Faction value, domain 0..20 */
    int material;              /* 0x263: Monster Editor Material selector */
    unsigned char deleted;     /* 0x267: LoadMonsterGroup skips the record */
    unsigned char significant_kill;
    unsigned char unknown_269;
    /* 0x26a: unborn monsters enter the birth/encounter lists before acting. */
    unsigned char unborn;
    unsigned int experience_override; /* 0x26b: nonzero experience override */
    /* 0x26f: the spell-point budget the monster casts out of, before its own
       runtime bonus. Zero is a data error the power-level chooser reports by
       name. */
    int sp_budget;
    unsigned char unknown_273[0x24];
};
static_assert(std::is_trivially_copyable_v<MonsterRecord>);
static_assert(sizeof(MonsterRecord) == 0x297, "MonsterRecord_size_must_be_0x297");
static_assert(offsetof(MonsterRecord, flags) == 0xd0, "MonsterRecord_flags_offset");
static_assert(offsetof(MonsterRecord, stamina_dice) == 0xda, "MonsterRecord_stamina_dice_offset");
static_assert(offsetof(MonsterRecord, remains_model_name) == 0x1c3,
              "MonsterRecord_remains_model_name_offset");
static_assert(offsetof(MonsterRecord, treasure) == 0x1f3, "MonsterRecord_treasure_offset");
static_assert(offsetof(MonsterRecord, treasure.gold_dice) == 0x243,
              "MonsterRecord_gold_dice_offset");
static_assert(offsetof(MonsterRecord, attack_body_part_chances) == 0x157,
              "MonsterRecord_attack_body_part_chances");
static_assert(offsetof(MonsterRecord, effective_level) == 0x24f, "MonsterRecord_effective_level");
static_assert(offsetof(MonsterRecord, display_level) == 0x251, "MonsterRecord_display_level");
// Skill images retain the retail stride; live skills use native alignment.
// These native value records already have the exact internal disk layout.
static_assert(sizeof(W8Enchantment) == 0x0c);
static_assert(sizeof(W8CharacterAttribute) == 0x14);
static_assert(sizeof(W8CharacterResistance) == 0x10);
struct HandAttack {
    int uiHolds;
    bool in_play;
    W8Skill weapon_skill;
    W8Skill combat_skill;
    unsigned int combined_skill;
    int attack_score;
    unsigned int attacks;
    int swings;
    int damage_bonus;
    int hit_bonus;
    int attack_bonus;
    int damage_percent;
    W8Dice damage_dice;
    unsigned short attack_flags;
    unsigned char condition_chances[W8_ATTACK_EFFECT_COUNT];
    unsigned char unknown_43[0x18];
};
static_assert(sizeof(HandAttack) == 0x5b);
struct CharacterConditionRecord {
    int level_acquired;
    int source_monster;
    bool active;
    unsigned char unknown_09[8];
};
static_assert(sizeof(CharacterConditionRecord) == 0x11);
struct EffectSlot {
    bool active;
    int effect_id;
    int amount;
    unsigned int percent;
    unsigned int duration;
};
static_assert(sizeof(EffectSlot) == 0x11);
static_assert(offsetof(EffectSlot, effect_id) == 0x01);
static_assert(offsetof(EffectSlot, amount) == 0x05);
static_assert(offsetof(EffectSlot, duration) == 0x0d);
struct ItemInstance {
    std::int32_t item_id;
    std::uint8_t stack_count;
    std::uint8_t uses_or_charges;
    std::uint8_t identified;
    std::uint8_t spell_hint;
    std::uint8_t unknown_08;
    std::uint8_t effect_used;
    std::uint8_t bind_announced;
    std::uint8_t bound;
};
static_assert(sizeof(ItemInstance) == 0x0c);
struct FormationPosition {
    std::int8_t row;
    std::int8_t old_row;
    std::int8_t column;
    std::int8_t facing;
    std::uint8_t unknown_04[8];
};
static_assert(sizeof(FormationPosition) == 0x0c);
struct PartyFormation {
    std::int8_t occupants[5][3];
    std::uint8_t row_occupants[5];
    FormationPosition positions[8];
    std::uint8_t unknown_74[0x10];
};
static_assert(sizeof(PartyFormation) == 0x84);
static_assert(offsetof(PartyFormation, positions) == 0x14);
struct CharacterSkill {
    std::uint8_t active, reset_flag;
    std::uint32_t points, level, base_level, practice_count;
    std::uint8_t improved, available;
    std::uint8_t unknown_14[0x12];
};
static_assert(sizeof(CharacterSkill) == 0x26);
static_assert(offsetof(CharacterSkill, points) == 0x02);
static_assert(offsetof(CharacterSkill, level) == 0x06);
struct Character {
    /* 0x0000: SaveCharacter stamps 1 here before writing the record, so the
       leading dword is a saved-record version rather than runtime state. */
    unsigned int record_version;
    bool fInParty; /* 0x0004 */
    /* 0x0005: the character's name, also the stem of its "%s.CHR" file. */
    std::array<std::byte, 20> name;
    std::array<std::byte, 12> name_part_2; /* 0x0019: rendered as the parenthesized name */
    unsigned char unknown_0025[0x44]; // Unidentified retail data, preserved verbatim.
    /* 0x0069 and 0x006d: the current profession and the one the character
       started in. The level band subtracts a base only while the two agree. */
    W8Profession iProfession;         /* 0x0069 */
    W8Profession original_profession; /* 0x006d */
    /* 0x0071: indexes the race resistance table. */
    W8Race iRace;
    /* 0x0075: zero is male and one is female. The quote lookup names the
       Data\Quotes\PCs files m_ or f_ from it, the item record's two-bit mask
       admits exactly one sex, and the female-only profession at index two
       forces the field to one. */
    W8Gender gender;
    /* 0x0079: the portrait/catalog index. Looked up by gender, race and
       profession together, drawn by RenderPartyPortrait, and reassigned from
       g_portrait_groups[].portraits[] when the portrait changes. */
    int portrait_index;
    /* 0x007d: cleared by DeriveCharacterPersonality in both of its branches and set to -1
       by the character rebuild; all three accesses are four-byte stores. */
    int unknown_007d;
    int personality; /* indexes the state-5 descriptor text */
    int voice;       /* selected by the character voice control */
    /* 0x0089: the experience level; averaged across occupied slots. */
    unsigned int uiExpLevel;
    int profession_levels[W8_PROFESSION_COUNT]; /* 0x008d */
    unsigned char unknown_00c9[0x14];
    /* 0x00dd: the eight-band ladder over the character's level in their
       current profession, and the base subtracted from it while they are still
       in the profession they started in. */
    int level_band;
    int level_band_base;
    W8CharacterAttribute attributes[7]; /* 0x00e5, indexed by skill_id - 0x22 */
    unsigned char unknown_0171[0x28];
    /* 0x199: the attribute points the level-up reset still owes against the
       profession's minimums. It accumulates as a negative debt and is drawn
       back down one point at a time once the pool is positive. */
    int attribute_point_deficit;
    CharacterSkill skills[0x29]; /* 0x019d, indexed by skill_id */
    unsigned char unknown_07b3[0x23a];
    /* 0x09ed..0x09f8: experience, the goal for the next level, and the goal
       the previous level had. A character is ready to advance once the first
       reaches the second. */
    unsigned int experience;
    unsigned int experience_goal;
    unsigned int experience_previous_goal;
    /* 0x09f9: this character's confirmed kills, shown on the camp stats
       page and imported from the Wiz7 record. */
    int kill_count;
    /* 0x09fd: how many times this character has died. */
    int death_count;
    /* 0x0a01: one entry per condition, holding how long it has left to run;
       W8_CONDITION_INDEFINITE means until something lifts it. The monster
       carries the identical array. */
    unsigned int uiCondition[W8_CONDITION_COUNT]; /* 0x0a01 */
    unsigned char unknown_0a51[0x14];
    W8Enchantment enchantments[8]; /* 0x0a65 */
    unsigned char unknown_0ac5[0x3c];
    /* 0x0b01: the highest currently-set condition index, rescanned from
       uiCondition[0x13] downward whenever a condition is
       lifted. Zero is either "none set" or condition zero, matching the
       monster copy at W8MonsterInfo::highest_condition. Thresholds are the
       condition ids themselves: below HOSTILE for rest/formation, below
       DEAD for ordinary party eligibility. */
    W8Condition highest_condition;
    /* 0x0b05: the highest enchantment slot still in use, recomputed by
       scanning down from the last one whenever a slot is cleared. */
    W8EnchantmentSlot enchantment_top;
    /* 0x0b09: the argument COND_POISONED carries (poison strength). */
    int condition_argument;
    /* 0x0b0d..0x0b20: the two pools with a ceiling each, plus the adjustment
       damage is booked against before hit points are recalculated. A character
       whose hp_current is zero is treated as out of the fight everywhere. */
    int uiHPMax;                  /* 0x0b0d */
    unsigned int hp_current;      /* 0x0b11 */
    int hp_adjustment;            /* 0x0b15 */
    int uiStaminaMax;             /* 0x0b19 */
    int stamina;                  /* 0x0b1d */
    unsigned int fatigue_penalty; /* 0x0b21: taken off the stamina ceiling */
    /* 0x0b25 and 0x0b45: the spell-point pools, one per spell realm. */
    int sp_max[W8_SPELL_REALM_COUNT]; /* 0x0b25 */
    unsigned char unknown_0b3d[8];
    int iSPLeft[W8_SPELL_REALM_COUNT]; /* 0x0b45 */
    unsigned char unknown_0b5d[8];
    /* 0x0b65: game minutes until the alchemist (trait MAKE_POTIONS) may brew
       again; the aging tick counts it down outside surprise, and a successful
       brew restarts it at 0x168. */
    unsigned int potion_brew_cooldown;
    /* 0x0b69, 0x0b71 and 0x0b79: the per-tick regeneration rates rebuilt from
       the pool ceilings, one each for hit points and stamina and one per spell
       realm at a two-float stride. */
    float health_regen_rate;
    /* 0x0b6d: fractional hit-point regeneration remainder carried between
       ticks; reset to zero when hit points reach the ceiling. */
    float health_regen_accumulator;
    float stamina_regen_rate;
    /* 0x0b75: fractional stamina-regeneration remainder carried between
       ticks; reset to zero when stamina reaches the ceiling. */
    float stamina_regen_accumulator;
    float spell_regen_rates[12];
    unsigned char unknown_0ba9[0x10];
    unsigned int inventory_weight;     /* 0x0bb9 */
    unsigned int party_weight_share;   /* 0x0bbd */
    unsigned int total_carried_weight; /* 0x0bc1 */
    unsigned int carrying_capacity;    /* 0x0bc5; displayed divided by 10 */
    /* 0x0bc9: the load category, zero through four, which scales what an
       action costs in fatigue. FatigueCharacter's error text calls it that. */
    W8LoadCategory load_category;
    /* 0x0bcd: one entry per spell. CanCharacterUseItem refuses a spell-source
       item whose spell already reads one here, so one is the learned state. */
    int spell_learned[136]; /* 0x0bcd */
    /* 0x0ded: indexed by skill_id. For the six realm skills it counts the
       spells known in that realm, which is what makes the skill available at
       all - LearnSpell bumps the entry and IsCharacterSkillAvailable reads it.
       LearnSpell also writes a recomputed figure into entry 36, which is a
       real skill id but not a count. */
    unsigned int skill_unlocks[0x25];
    /* 0x0e81: rebuilt by CalcInitiative from level, speed, senses, the
       initiative skill and the current load category. */
    int initiative;
    /* 0x0e85..0x0e8c: the two armor-class summaries rebuilt after the
       location values below. */
    int armor_class_total;
    int armor_class_average;
    /* 0x0e8d: thirteen armor-class components. CalcArmorClasses clears the
       whole run before applying equipment, traits, load and fatigue. */
    int armor_class_components[13];
    int armor_class_by_location[5]; /* 0x0ec1 */
    unsigned char unknown_0ed5[4];
    /* 0x0ed9: a percentage taken off incoming damage, the character's
       counterpart of the monster's own at 0x1e1. */
    int damage_reduction;
    W8CharacterResistance resistances[W8_RESISTANCE_COUNT]; /* 0x0edd */
    unsigned char flags0[0x20];
    /* 0x0f5d: the twelve worn/held slots, indexed by W8EquipSlot. Slots six
       and seven are the primary hands and eight and nine the alternate pair. */
    ItemInstance EquippedItem[12]; /* 0x0f5d */
    unsigned char flags1[0x3c];
    /* 0x1029: the eight per-character carried slots. GetOriginOfCharacterItem
       reports this array as origin zero and the equipment array as origin one. */
    ItemInstance backpack[8]; /* 0x1029 */
    unsigned char unknown_1089[0xc0];
    /* 0x1149: the two per-hand derived attack blocks. */
    HandAttack Hand[2];
    unsigned char unknown_11ff[0xb6];
    unsigned char dual_wielding; /* 0x12b5 */
    /* 0x12b6: per-monster-id detection score, one byte per record id. The
       wandering-group pass adds the group's member count while the party stays
       unaware, and the detection roll subtracts the record's effective level
       before scaling. */
    unsigned char monster_awareness[0x3e8];
    /* 0x169e: the fatigue band, zero through four, recomputed from the stamina
       fraction whenever it moves; a change re-runs the armour class pass. */
    int fatigue_band;
    /* 0x16a2: the modifier block rebuilt from the character's condition
       durations, enchantment slots and the bound-NPC penalty; folded into
       `bonus` alongside the equipment and party blocks. */
    W8GameplayModifierBlock condition_modifiers;
    /* 0x1709: the equipment bonus block accumulated from the worn items and
       folded into `bonus`. */
    W8GameplayModifierBlock equipment_bonus;
    /* 0x1770: the derived modifier block the rebuild clears and folds the
       equipment, persistent and party blocks into. */
    W8GameplayModifierBlock bonus;
    /* 0x17d7: the CamPos record cameraState writes and recall restores.
       The cross-level path copies the whole 0x3c bytes onto
       GameStatus::pending_move_location. */
    CameraState saved_location; /* 0x17d7 */
    /* 0x1813: which level that anchor belongs to. The recall compares it
       against GetLoadedLevelID() and takes a different path when they differ. */
    int saved_level;                          /* 0x1813 */
    CharacterConditionRecord conditions[4]; /* 0x1817 .. 0x185a */
    /* 0x185b: the deep-fatigue effect is already on this character, which is
       what stops FatigueCharacter re-applying it every turn. */
    bool deep_fatigue_applied;
    /* 0x185c: one cost per skill id 0x18..0x1b, read by the profession-change
       cost diff with the skill id biased down. */
    unsigned char skill_costs[4];
    unsigned char magic_bonus_pool;
    /* 0x1861: the anchor above has been set. Recall does nothing without it. */
    bool has_saved_location;
};
static_assert(std::is_trivially_copyable_v<Character>);
static_assert(sizeof(Character) == 0x1862, "Character_must_be_0x1862");
#pragma pack(pop)
} // namespace wiz8::retail

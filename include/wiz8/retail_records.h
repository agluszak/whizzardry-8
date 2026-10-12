#pragma once

#include "wiz8/layouts/character.h"
#include <cstdint>
#include "wiz8/layouts/combat_state.h"
#include "wiz8/engine_code/Missile.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/item_tables.h"
#include <array>
#include <cstddef>
#include <type_traits>

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
inline constexpr std::size_t kGlobalStatusBytes = 0x49c2;
inline constexpr std::size_t kPartySlotBytes = 0x106;
inline constexpr std::size_t kMissileTableRecordBytes = 0x1e5;

#pragma pack(push, 1)
struct CombatTarget {
    std::int32_t iType, iChar, iMonsterID, iGroupID;
    float point[3];
    std::uint32_t item_marker; // Saved address, never a runtime pointer/handle.
};
static_assert(sizeof(CombatTarget) == 0x20);
struct PartySlot {
    bool fOccupied;
    W8ActionKind pending_action;
    std::int32_t attack_mode[4];
    std::array<std::byte, 8> pending_action_detail;
    CombatTarget target_out_of_combat;
    W8ActionKind action;
    std::int32_t action_detail0;
    std::array<std::byte, 8> action_detail1;
    CombatTarget target_in_combat;
    W8ActionKind action_kind;
    std::int32_t action_detail;
    std::int32_t spell_id;
    std::array<std::byte, 8> spell_detail;
    CombatTarget spell_target;
    std::array<std::byte, 8> item_detail;
    CombatTarget item_target;
    std::int32_t item_id;
    std::uint8_t item_origin;
    std::uint16_t item_slot;
    std::int8_t queued_action;
    CombatTarget breath_target;
    std::int32_t party_order_index;
    bool item_action_pending;
    float movement_fatigue;
    std::int32_t npc_index;
    bool npc_bound;
    std::uint32_t pending_event_type;
    std::uint8_t portrait_advance;
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
    std::uint8_t unknown_148[8];
    std::int32_t magnitude_base;
    bool spell_missile;
    std::uint8_t condition_chances[W8_ATTACK_EFFECT_COUNT];
    std::int32_t weapon_sound_class;
    std::uint8_t unknown_169[0x7c];
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
    std::uint8_t unknown_040[4];
    /* 0x044: base duration the spell-info dialog prints. */
    std::int32_t duration;
    std::uint8_t alchemy_spell; /* 0x048 */
    std::int32_t spell_point_cost;        /* 0x049: per power level */
    /* 0x04d: per-level duration added to duration on the same line. */
    std::int32_t duration_per_level;
    W8Dice effect_dice; /* 0x051 */
    std::uint8_t unknown_055;
    std::int32_t spell_level;              /* 0x056: zero through seven */
    std::uint8_t wizardry_spell; /* 0x05a */
    char resource_name[64];       /* 0x05b: visual/MLS resource basename */
    std::array<std::byte, 128> display_name;     /* 0x09b */
    std::uint8_t unknown_11b[4];
    std::uint8_t divinity_spell; /* 0x11f */
    std::uint8_t psionics_spell; /* 0x120 */
    float effect_radius;          /* 0x121 */
    std::uint8_t unknown_125;
    /* 0x126: a monster may cast the spell at all. MonsterOKToCastSpell reports
       a spell without it by name and asserts. */
    std::uint8_t monster_castable;
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
    std::uint8_t needs_aim;
    /* 0x140: the MissileTables.dbs row index the cast fires its effect
       through when missile_delivered is set. */
    std::int32_t missile_index;
    /* 0x144: the spell's effect is delivered by a missile in flight, so its
       queued effect stays alive until the missile lands. Set for the ten
       projectile spells - Frost, Heal Wounds, Make Wounds, Sleep, Stamina,
       Terror, Noxious Fumes, Crush, Return to Portal and the monster Special
       Attack Cone. */
    std::uint8_t missile_delivered;
    std::uint8_t unknown_145[2];
    /* 0x147: SpellInfoDialog prints the effect-dice line when this is set. */
    std::int32_t show_effect_dice;
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
    std::uint8_t highlight_when_true;
    /* 0x037: the visibility level the journal entry needs. */
    std::int8_t visibility;
    /* 0x038 and 0x100: the alternate and normal journal descriptions, wide,
       selected by the fact's current value. */
    std::array<std::byte, 200> alternate_description;
    std::array<std::byte, 216> description;
};
static_assert(std::is_trivially_copyable_v<FactDatabaseRecord>);
static_assert(sizeof(FactDatabaseRecord) == 0x1d8, "FactDatabaseRecord_size_must_be_0x1d8");
struct NpcCharacterTemplate {
    std::array<std::byte, 20> name;       /* 0x000, record 0x0c4 */
    std::array<std::byte, 12> name_part_2; /* 0x014, record 0x0d8 */
    std::uint8_t unknown_01a[0x44]; // Unidentified retail data, preserved verbatim.
    W8Profession
        profession;     /* 0x064, record 0x128: index into profession_levels[W8_PROFESSION_COUNT] */
    W8Race race;        /* 0x068, record 0x12c */
    std::int32_t table_value;    /* 0x06c, record 0x130 */
    std::uint32_t level; /* 0x070, record 0x134: starting profession level */
    std::int32_t attributes[7];  /* 0x074, record 0x138: W8CharacterAttribute::value per attribute */
    std::int32_t skills[0x29];   /* 0x090, record 0x154: W8CharacterSkill::value_02 per skill */
    std::uint8_t
        spells[0x72]; /* 0x134, record 0x1f8: one flag per learnable spell, indexed from one */
    std::uint16_t equipment_present[12]; /* 0x1a6, record 0x26a: zero leaves the slot empty */
    std::uint16_t equipment_ids[12];     /* 0x1be, record 0x282: Items.dbs index, 0xffff empty */
    std::uint16_t backpack_present[8];   /* 0x1d6, record 0x29a */
    std::uint16_t backpack_ids[8];       /* 0x1e6, record 0x2aa */
    std::int8_t gender; /* 0x1f6, record 0x2ba: copied into the character's own sex field */
    std::int16_t wanted_item_ids[3];
    std::uint8_t unknown_1fd[9];
};
static_assert(std::is_trivially_copyable_v<NpcCharacterTemplate>);
static_assert(sizeof(NpcCharacterTemplate) == 0x206, "NpcCharacterTemplate_size_must_be_0x206");
struct NpcDatabaseRecord {
    std::uint16_t
        version; /* 0x000: two in the corpus; the rule tail loads only when this exceeds 1 */
    /* 0x002: trade-pool stock count; CreateNpcRuntimeNode copies it into the
       runtime state's trade_pool. */
    std::int16_t trade_pool;
    /* 0x004: the wide source name the level-entry rebinding prefixes with an
       underscore to build the NPC's trigger name. */
    std::array<std::byte, 80> source_name;
    /* 0x054: monster-bound NPC: no standalone runtime node is created, its
       state comes through the monster binding, and releasing the binding
       marks it unavailable. */
    std::uint8_t monster_bound;
    /* 0x055: the NPC's stock is released only while this is set. */
    std::uint8_t owns_stock;
    /* 0x056: merchant: still opens dialogue when the disposition band is
       hostile, and item drops go through the trade transcript layout. */
    std::uint8_t merchant;
    /* 0x057: the NPC carries an RPC character and can join a monster group,
       which is what makes the group index on its runtime state meaningful. */
    std::uint8_t has_group;
    /* 0x058: the NPC's kind. Kind twenty refuses to trade. */
    std::int32_t kind;
    /* 0x05c: the disposition byte CreateNpcRuntimeNode starts the state with. */
    std::uint8_t disposition;
    /* 0x05d/0x05e: the signed scales the charm and talk interactions fold into
       the disposition shift; the sub-one path negates the /5 quotient. */
    std::int8_t charm_scale;
    std::int8_t talk_scale;
    /* 0x05f: the faction the NPC belongs to; zero leaves GetNpcDisposition on
       the record's own disposition byte. */
    std::uint8_t faction;
    /* 0x060: one bit per named-person alias the NPC answers to; read as one
       dword by FindNpcNameOrPlaceQuote. */
    std::uint32_t name_alias_mask;
    /* 0x064: one-based index into g_item_tables selecting the record's item
       table, copied into the runtime state. */
    std::int32_t item_table_id;
    /* 0x068: one bit per service the NPC offers. */
    std::uint32_t service_flags;
    std::uint8_t unknown_06c[2];
    /* 0x06e: an allied faction; while a front-rank party member's bound NPC
       belongs to it, GetNpcDisposition pins this NPC's answer at fifty. The
       compare sign-extends it. */
    std::int8_t allied_faction;
    /* 0x06f: the minimum average party level before this NPC's group can be
       invited. */
    std::uint8_t min_party_level;
    /* 0x070/0x074: the level this NPC restores its binding at and the
       FindEntityByName key the restore moves its monster to - the default
       pair copied into the runtime state's pending restore. */
    char restore_level;
    std::uint8_t unknown_071[3];
    char restore_entity_name[0x29];
    /* 0x09d: set when the record carries no appended item-stock
       rules; the load path reads them only while this is clear. */
    std::uint8_t no_item_stock;
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
    std::uint32_t trade_item_class_mask;
    std::uint8_t unknown_2da[0x10];
    /* 0x2ea: voice-script NPC: picks the VOC_ script/sound prefix over NPC_
       and gates the normal dialogue paths. */
    std::uint8_t voice_script;
    /* 0x2eb: the purse the NPC carries; the party gets it when the NPC's
       monster dies. */
    std::int32_t gold;
    std::uint8_t combat_script_notice_enabled;       /* 0x2ef: hostile-NPC combat notice */
    std::uint8_t allow_dismissed_departure_dialogue; /* 0x2f0: permits dismissed-NPC dialogue */
    std::uint8_t unknown_2f1[0x18];
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
    std::int32_t maximum_random_encounters; /* 0x3c */
    std::int32_t minimum_random_encounters; /* 0x40 */
    std::int32_t maximum_encounter_budget;  /* 0x44 */
    std::int32_t minimum_encounter_budget;  /* 0x48 */
    std::int32_t encounter_budget_period;   /* 0x4c: elapsed-time divisor */
    std::int32_t encounter_culling_seconds; /* 0x50 */
    /* 0x54: float divisor the surprise transition uses when scaling game time
       and monster-generator duration. */
    float gameplay_time_scale;
    std::uint8_t unknown_058[0x80];
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
    std::uint8_t can_open_doors;
    /* 0x0c1: rolled by the group-attack summon to size the spawned group. */
    W8Dice group_size_dice;
    W8EncounterCompanionRecord companions[2];
    /* 0x0cb: the monster's kind. The alchemy-casting rule admits kinds four,
       five and thirteen and no others, which is the only body that reads it. */
    std::uint8_t kind;
    /* 0x0cc: selects this monster's row in the name-prefix table, the same
       table a character indexes by sex. */
    std::uint8_t name_group;
    /* 0x0cd: the NPC record index this monster is bound to, fed to
       GetNpcStateByKind and compared against W8NpcState::name_style; 0xfa
       marks a record with no bound NPC. */
    std::uint8_t npc_kind;
    /* 0x0ce: signed stamina regeneration rate (Monster Editor: ST Regen). */
    std::int8_t stamina_regeneration;
    /* 0x0cf: the monster's own percentage reduction on incoming damage. */
    std::uint8_t damage_reduction;
    /* 0x0d0: W8MonsterRecordFlag mask; retain the serialized byte width. */
    std::uint8_t flags;
    std::uint8_t attribute_values[5]; /* 0x0d1 */
    W8Dice hit_points;                 /* 0x0d6: rolled into uiHPMax/hp_current */
    W8Dice stamina_dice;               /* 0x0da: initializes maximum and current stamina */
    std::uint8_t unknown_0de[2];
    /* 0x0e0/0x0e1: the percentage chances the AI casts a spell or flees each
       round; a monster with a usable attack ignores them and always tries. */
    std::uint8_t spell_chance;
    std::uint8_t flee_chance;
    /* 0x0e2: the percentage chance the AI advances on the party each round;
       zero falls back to the behavior byte at +0x1c0. */
    std::uint8_t advance_chance;
    /* 0x0e3: Special attack selector; indexes the effect rows used by
       GroupAttacks.cpp and the corresponding display-name table. */
    std::uint8_t special_attack_kind;
    std::uint8_t initiative; /* 0x0e4: Monster Editor Initiative */
    /* 0x0e5/0x0e6: attacks and swings per round. */
    std::uint8_t attacks_per_round;
    std::uint8_t swings_per_round;
    /* 0x0e7: the monster's three attacks, named by the Combat Range.cpp
       assertions pMonsterDB->Attack[uiAttack].fHasAttack and uiAttack <
       MAX_MONSTER_ATTACKS, which is what bounds the array at three. */
    W8MonsterAttack attacks[W8_MAX_MONSTER_ATTACKS]; /* 0x0e7 */
    /* 0x14d: the ten spells the AI may cast, zero for none. */
    std::uint8_t spells[10];
    std::uint8_t attack_body_part_chances[5];
    std::uint8_t special_attack_cooldown;
    std::int8_t evasion;
    /* 0x15e: Monster Editor Constitution selector; chooses the body-specific
       hit-location label row. */
    std::uint8_t constitution;
    /* 0x15f: the percentage of hits that land on each of the seven monster
       hit locations; the total is reported when it falls short of 100. */
    std::uint8_t hit_location_chances[7];
    /* 0x166/0x16d: the monster's armour per hit location and per attack mode,
       the terms TargetArmorClassAtLocation subtracts on top of evasion. */
    std::int8_t armor_class_by_location[7];
    std::int8_t armor_class_by_attack_mode[9];
    /* 0x176: the monster's own resistance per realm, read alongside the
       gameplay-modifier bonus wherever a character would read
       W8CharacterResistance::total. */
    std::uint8_t resistances[6];
    /* 0x17c: hit points gained (or lost, when negative) per game minute. */
    std::int8_t hp_regeneration;
    std::uint8_t unknown_17d[4];
    std::uint32_t experience; /* 0x181: base experience value */
    std::uint8_t unknown_185[2];
    std::int16_t record_id;       /* 0x187: equals the zero-based database index */
    char cycle_name[0x30]; /* 0x189: GrCycle lookup key */
    /* 0x1b9: prefer ranged actions instead of closing/backing off. */
    std::uint8_t prefer_ranged_actions;
    float combat_move_range; /* 0x1ba: designer-facing combat movement range */
    /* 0x1be: blocks the DEAD condition. */
    std::uint8_t instant_death_immune;
    /* 0x1bf/0x1c0: the Monster Editor's Combat Behaviour and Combat Morale selectors. */
    std::uint8_t combat_behavior;
    std::uint8_t combat_morale;
    /* 0x1c1: the MIPE monster list only admits records carrying -1 here, and
       stores the value itself as the selected monster index. */
    std::int16_t editor_index;
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
    std::uint8_t attack_multiple_targets;
    /* 0x248: Monster Editor camouflage rating. */
    std::uint8_t camouflage0;
    std::uint8_t camouflage1;
    /* 0x24a: the monster cannot be targeted at all. Every sweep that gathers
       candidates drops it before any other test. */
    std::uint8_t untargetable;
    std::uint8_t unknown_24b[4];
    /* 0x24f: effective/combat monster level used by attack, resistance,
       spell and effect formulas. Distinct from the displayed level at 0x251. */
    std::uint8_t effective_level;
    std::uint8_t unknown_250;
    /* 0x251: monster level shown by MonsterInfo and related UI. */
    std::uint8_t display_level;
    std::uint8_t unknown_252;
    std::int32_t model_index;           /* 0x253 */
    std::int32_t alternate_model_index; /* 0x257: alternate selected value */
    std::int32_t hostility_radius;      /* 0x25b: Minimal Neutrality Distance / Hostility Radius */
    std::int32_t faction_id;            /* 0x25f: W8Faction value, domain 0..20 */
    std::int32_t material;              /* 0x263: Monster Editor Material selector */
    std::uint8_t deleted;     /* 0x267: LoadMonsterGroup skips the record */
    std::uint8_t significant_kill;
    std::uint8_t unknown_269;
    /* 0x26a: unborn monsters enter the birth/encounter lists before acting. */
    std::uint8_t unborn;
    std::uint32_t experience_override; /* 0x26b: nonzero experience override */
    /* 0x26f: the spell-point budget the monster casts out of, before its own
       runtime bonus. Zero is a data error the power-level chooser reports by
       name. */
    std::int32_t sp_budget;
    std::uint8_t unknown_273[0x24];
};
static_assert(std::is_trivially_copyable_v<MonsterRecord>);
static_assert(sizeof(MonsterRecord) == 0x297, "MonsterRecord_size_must_be_0x297");
static_assert(offsetof(MonsterRecord, flags) == 0xd0, "MonsterRecord_flags_offset");
static_assert(offsetof(MonsterRecord, stamina_dice) == 0xda,
              "MonsterRecord_stamina_dice_offset");
static_assert(offsetof(MonsterRecord, remains_model_name) == 0x1c3,
              "MonsterRecord_remains_model_name_offset");
static_assert(offsetof(MonsterRecord, treasure) == 0x1f3, "MonsterRecord_treasure_offset");
static_assert(offsetof(MonsterRecord, treasure.gold_dice) == 0x243,
              "MonsterRecord_gold_dice_offset");
static_assert(offsetof(MonsterRecord, attack_body_part_chances) == 0x157,
              "MonsterRecord_attack_body_part_chances");
static_assert(offsetof(MonsterRecord, effective_level) == 0x24f,
              "MonsterRecord_effective_level");
static_assert(offsetof(MonsterRecord, display_level) == 0x251, "MonsterRecord_display_level");
struct Character {
    /* 0x0000: SaveCharacter stamps 1 here before writing the record, so the
       leading dword is a saved-record version rather than runtime state. */
    std::uint32_t record_version;
    bool fInParty; /* 0x0004 */
    /* 0x0005: the character's name, also the stem of its "%s.CHR" file. */
    std::array<std::byte, 20> name;
    std::array<std::byte, 12> name_part_2; /* 0x0019: rendered as the parenthesized name */
    std::uint8_t unknown_0025[0x44]; // Unidentified retail data, preserved verbatim.
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
    std::int32_t portrait_index;
    /* 0x007d: cleared by DeriveCharacterPersonality in both of its branches and set to -1
       by the character rebuild; all three accesses are four-byte stores. */
    std::int32_t unknown_007d;
    std::int32_t personality; /* indexes the state-5 descriptor text */
    std::int32_t voice;       /* selected by the character voice control */
    /* 0x0089: the experience level; averaged across occupied slots. */
    std::uint32_t uiExpLevel;
    std::int32_t profession_levels[W8_PROFESSION_COUNT]; /* 0x008d */
    std::uint8_t unknown_00c9[0x14];
    /* 0x00dd: the eight-band ladder over the character's level in their
       current profession, and the base subtracted from it while they are still
       in the profession they started in. */
    std::int32_t level_band;
    std::int32_t level_band_base;
    W8CharacterAttribute attributes[7]; /* 0x00e5, indexed by skill_id - 0x22 */
    std::uint8_t unknown_0171[0x28];
    /* 0x199: the attribute points the level-up reset still owes against the
       profession's minimums. It accumulates as a negative debt and is drawn
       back down one point at a time once the pool is positive. */
    std::int32_t attribute_point_deficit;
    W8CharacterSkill skills[0x29]; /* 0x019d, indexed by skill_id */
    std::uint8_t unknown_07b3[0x23a];
    /* 0x09ed..0x09f8: experience, the goal for the next level, and the goal
       the previous level had. A character is ready to advance once the first
       reaches the second. */
    std::uint32_t experience;
    std::uint32_t experience_goal;
    std::uint32_t experience_previous_goal;
    /* 0x09f9: this character's confirmed kills, shown on the camp stats
       page and imported from the Wiz7 record. */
    std::int32_t kill_count;
    /* 0x09fd: how many times this character has died. */
    std::int32_t death_count;
    /* 0x0a01: one entry per condition, holding how long it has left to run;
       W8_CONDITION_INDEFINITE means until something lifts it. The monster
       carries the identical array. */
    std::uint32_t uiCondition[W8_CONDITION_COUNT]; /* 0x0a01 */
    std::uint8_t unknown_0a51[0x14];
    W8Enchantment enchantments[8]; /* 0x0a65 */
    std::uint8_t unknown_0ac5[0x3c];
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
    std::int32_t condition_argument;
    /* 0x0b0d..0x0b20: the two pools with a ceiling each, plus the adjustment
       damage is booked against before hit points are recalculated. A character
       whose hp_current is zero is treated as out of the fight everywhere. */
    std::int32_t uiHPMax;                  /* 0x0b0d */
    std::uint32_t hp_current;      /* 0x0b11 */
    std::int32_t hp_adjustment;            /* 0x0b15 */
    std::int32_t uiStaminaMax;             /* 0x0b19 */
    std::int32_t stamina;                  /* 0x0b1d */
    std::uint32_t fatigue_penalty; /* 0x0b21: taken off the stamina ceiling */
    /* 0x0b25 and 0x0b45: the spell-point pools, one per spell realm. */
    std::int32_t sp_max[W8_SPELL_REALM_COUNT]; /* 0x0b25 */
    std::uint8_t unknown_0b3d[8];
    std::int32_t iSPLeft[W8_SPELL_REALM_COUNT]; /* 0x0b45 */
    std::uint8_t unknown_0b5d[8];
    /* 0x0b65: game minutes until the alchemist (trait MAKE_POTIONS) may brew
       again; the aging tick counts it down outside surprise, and a successful
       brew restarts it at 0x168. */
    std::uint32_t potion_brew_cooldown;
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
    std::uint8_t unknown_0ba9[0x10];
    std::uint32_t inventory_weight;     /* 0x0bb9 */
    std::uint32_t party_weight_share;   /* 0x0bbd */
    std::uint32_t total_carried_weight; /* 0x0bc1 */
    std::uint32_t carrying_capacity;    /* 0x0bc5; displayed divided by 10 */
    /* 0x0bc9: the load category, zero through four, which scales what an
       action costs in fatigue. FatigueCharacter's error text calls it that. */
    W8LoadCategory load_category;
    /* 0x0bcd: one entry per spell. CanCharacterUseItem refuses a spell-source
       item whose spell already reads one here, so one is the learned state. */
    std::int32_t spell_learned[136]; /* 0x0bcd */
    /* 0x0ded: indexed by skill_id. For the six realm skills it counts the
       spells known in that realm, which is what makes the skill available at
       all - LearnSpell bumps the entry and IsCharacterSkillAvailable reads it.
       LearnSpell also writes a recomputed figure into entry 36, which is a
       real skill id but not a count. */
    std::uint32_t skill_unlocks[0x25];
    /* 0x0e81: rebuilt by CalcInitiative from level, speed, senses, the
       initiative skill and the current load category. */
    std::int32_t initiative;
    /* 0x0e85..0x0e8c: the two armor-class summaries rebuilt after the
       location values below. */
    std::int32_t armor_class_total;
    std::int32_t armor_class_average;
    /* 0x0e8d: thirteen armor-class components. CalcArmorClasses clears the
       whole run before applying equipment, traits, load and fatigue. */
    std::int32_t armor_class_components[13];
    std::int32_t armor_class_by_location[5]; /* 0x0ec1 */
    std::uint8_t unknown_0ed5[4];
    /* 0x0ed9: a percentage taken off incoming damage, the character's
       counterpart of the monster's own at 0x1e1. */
    std::int32_t damage_reduction;
    W8CharacterResistance resistances[W8_RESISTANCE_COUNT]; /* 0x0edd */
    std::uint8_t flags0[0x20];
    /* 0x0f5d: the twelve worn/held slots, indexed by W8EquipSlot. Slots six
       and seven are the primary hands and eight and nine the alternate pair. */
    W8ItemInstance EquippedItem[12]; /* 0x0f5d */
    std::uint8_t flags1[0x3c];
    /* 0x1029: the eight per-character carried slots. GetOriginOfCharacterItem
       reports this array as origin zero and the equipment array as origin one. */
    W8ItemInstance backpack[8]; /* 0x1029 */
    std::uint8_t unknown_1089[0xc0];
    /* 0x1149: the two per-hand derived attack blocks. */
    W8HandAttack Hand[2];
    std::uint8_t unknown_11ff[0xb6];
    std::uint8_t dual_wielding; /* 0x12b5 */
    /* 0x12b6: per-monster-id detection score, one byte per record id. The
       wandering-group pass adds the group's member count while the party stays
       unaware, and the detection roll subtracts the record's effective level
       before scaling. */
    std::uint8_t monster_awareness[0x3e8];
    /* 0x169e: the fatigue band, zero through four, recomputed from the stamina
       fraction whenever it moves; a change re-runs the armour class pass. */
    std::int32_t fatigue_band;
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
       W8GlobalStatus::pending_move_location. */
    W8WorldCameraState saved_location; /* 0x17d7 */
    /* 0x1813: which level that anchor belongs to. The recall compares it
       against g_status.current_level and takes a different path when they differ. */
    std::int32_t saved_level;                          /* 0x1813 */
    W8CharacterConditionRecord conditions[4]; /* 0x1817 .. 0x185a */
    /* 0x185b: the deep-fatigue effect is already on this character, which is
       what stops FatigueCharacter re-applying it every turn. */
    bool deep_fatigue_applied;
    /* 0x185c: one cost per skill id 0x18..0x1b, read by the profession-change
       cost diff with the skill id biased down. */
    std::uint8_t skill_costs[4];
    std::uint8_t magic_bonus_pool;
    /* 0x1861: the anchor above has been set. Recall does nothing without it. */
    bool has_saved_location;
};
static_assert(std::is_trivially_copyable_v<Character>);
static_assert(sizeof(Character) == 0x1862, "Character_must_be_0x1862");
/* The status block's head: its version and two pointer words retail stored
   but never used after loading. */
struct StatusBuffers {
    float save_version;
    std::uint32_t characters;
    std::uint32_t party_rows;
};
static_assert(sizeof(StatusBuffers) == 0x0c);
struct GlobalStatus {
    StatusBuffers buffers;
    bool game_started; /* 0x000c */
    /* 0x000d..0x0018: the three join counters the party-add entry advances:
       the regular-member, auxiliary and total counts. */
    std::int32_t regular_member_count;
    std::int32_t auxiliary_member_count;
    std::int32_t total_member_count;
    std::uint32_t party_gold;
    std::int32_t selected_character;
    W8ItemInstance party_item_pool[500];
    std::uint32_t party_item_count;
    /* 0x1795: text-box line cursor. */
    std::int16_t text_line_cursor;
    std::uint32_t legacy_text_box_lines[2][3];
    /* 0x17af: the party's twelve effect slots, the same 0x11-byte records the
       monster and combat tables hold. The trailing run is opaque. */
    W8EffectSlot effect_slots[12];
    std::uint8_t unknown_187b[0x55];
    std::int32_t party_facing;
    std::uint32_t party_heading;
    std::int32_t world_clock;
    std::uint32_t world_clock_ms;
    std::uint32_t party_order_slots[8];
    std::int32_t current_level;
    /* 0x1904..0x1a03: the 0x100-byte STAT header block. */
    std::uint8_t status_header_prefix[0xd4];
    std::uint32_t uiTurnsElapsed; /* 0x19d8 */
    std::uint8_t status_header_suffix[0x28];
    W8LevelProgressRow level_progress[W8_LEVEL_COUNT];
    std::uint8_t unknown_2013[0x294];
    /* 0x22a7: CamPos staged by recall when the anchor is on another level;
       LoadLevel restores it after the new world exists. */
    W8WorldCameraState pending_move_location;
    /* 0x22e3: the party-wide modifier block the effect rebuild clears and
       refills. Its +0x4a flag is the light gate the monster-sight threshold
       pass reads. */
    W8GameplayModifierBlock party_modifiers;
    std::int32_t next_group_id;
    std::int32_t next_monster_location_id;
    std::int32_t next_world_item_id;
    std::int32_t next_trigger_id;
    bool item_in_cursor;
    W8ItemInstance item_in_hand;
    /* 0x2367: per-slot flags the character-load path consults. */
    std::uint8_t flags[0x20];
    std::uint32_t game_time_ms;
    std::uint32_t aging_accumulator;
    /* 0x238f: search mode toggle. Mirrors the submenu search button, slows
       party movement, and scales the monster-sight threshold while set. */
    std::uint8_t search_mode;
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
    std::uint32_t item_recharge_ms;
    W8PartyFormationState formation;
    std::int32_t game_time_days;
    bool iron_man;
    /* 0x242a: world-clock stamp of the last NPC-binding reset; the event
       pass waits 0x3c ticks past it. */
    std::int32_t binding_reset_clock;
    /* 0x242e: binding-reset grace period in effect; cleared once the sweep
       runs after the clock elapses. */
    bool binding_reset_pending;
    std::uint8_t padding_242f;
    /* 0x2430: one-shot gate for the NPC event pass. */
    bool npc_restore_pending;
    /* 0x2431: raised by the .nsf quote audit while it runs; ShowNotice counts
       each notice's wrapped lines under it. */
    std::uint8_t quote_audit;
    /* 0x2432: ShowNotice sets it under quote_audit when a notice wraps
       past seven lines; the audit reports those as "Long Quote". */
    std::uint8_t long_quote;
    bool party_fatigued;
    /* 0x2434: index of the party member the main-game selection flow is on.
       The screen reset writes 0xff. */
    std::uint8_t selected_party_member;
    /* 0x2435: read as a gate by the main-game frame's world-cursor path. */
    std::uint8_t world_cursor_gate;
    std::uint32_t camp_tick_ms;
    /* 0x243a: the five RPC race ids AssayDialog walks as NUM_RPC_RACES. */
    std::uint8_t rpc_races[5];
    std::uint8_t unknown_243f[5];
    /* Character creation skips the loose CHR collision check when set. */
    std::uint8_t skip_loose_character_check;
    /* 0x2445: latched once the Trynnie2 Zulu/0x1c3 use-item action has been
       handled at a cursor node; later uses take the Mystical Shaman branch. */
    bool use_item_latch;
    bool infatuation_pending;
    W8Difficulty difficulty;
    /* 0x244b: the save file's creation-time pair XOR-masked by SaveGame's
       two data constants; both halves are written as dwords. */
    std::uint32_t save_filetime_xor[2];
    std::array<std::byte, 44> monster_name_buffer;
    /* 0x247f: party slot selected by the Sedexus path before rpc_active
       is armed; later capture, fact and death handling reuse the same slot. */
    std::int32_t sedexus_party_slot;
    std::uint32_t stamina_tick_ms;
    std::uint8_t condition13_clock;
    /* 0x2488: one-shot gate; when set, the next condition-change and
       condition-cleared reaction is swallowed and the flag cleared. */
    std::uint8_t skip_next_condition_reaction;
    bool rpc_active; /* 0x2489: fact 0x14c gate */
    /* 0x248a: armed by the long NPC reward event; the event also stamps
       0x2493 with the world clock. */
    bool fact_b8_pending;
    std::uint32_t condition13_stamp;
    std::int32_t pending_condition_party_slot;
    std::int32_t fact_b8_clock;
    bool greeting_pending;
    std::uint32_t camp_fatigue_count;
    /* 0x249c: party slot fact 0x39 hands to RemoveCharacterCondition. */
    std::int32_t party_slot;
    /* 0x24a0: per-spell 0x10-byte stat records; TrackItemSpellSource
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
    /* 0x4972: set once the Cosmic Circle arena monsters have been spawned by
       the level-4 setup; the setup skips its work while this or world_suspended
       holds. */
    bool cc_arena_spawned;
    /* 0x4973/0x4977: GetTickCount stamps. NpcScriptSavantHackDone writes the
       first; UpdateNpcEvents retires NPC 0x1b3 fifty ticks later and starts
       the second, which gates monster group 0x1b6's Bela cycle after five
       seconds. */
    std::int32_t savant_hack_tick;
    std::int32_t bela_cycle_tick;
    std::uint8_t unknown_497b[4];
    /* 0x497f: combat difficulty band counters, indexed by EvaluateCombatDifficulty. */
    std::uint32_t combat_difficulty_counts[3];
    /* 0x498b: NPC group event counter, cleared once the group event runs. */
    std::int32_t vi_event_stage;
    /* 0x498f/0x4993: pending-stage flags set by FACT_QUE_ENDGAME2/3; the book
       callback queues ENDGAME2's script notice while either holds. */
    std::int32_t endgame2_queued;
    std::int32_t endgame3_queued;
    std::uint32_t text_box_lines_used[4];
    std::uint32_t text_box_lines_shown[4];
    /* 0x49b7: world-clock stamp the 0x49bb reward event compares against. */
    std::int32_t trang_check_clock;
    bool trang_check_pending;
    bool intro_shown;
    bool flag;
    std::uint8_t padding_49be[2];
    /* 0x49c0: set when the endgame transition starts; saves carrying either
       this or flag are filtered from the load list. */
    bool endgame_started;
    /* 0x49c1: latched while g_dev_mode is set at teardown; persisted into
       the save slot as dev_flagged. */
    bool dev_flagged;
};
static_assert(std::is_trivially_copyable_v<GlobalStatus>);
static_assert(offsetof(GlobalStatus, status_header_suffix) + sizeof(GlobalStatus::status_header_suffix) -
              offsetof(GlobalStatus, status_header_prefix) == 0x100,
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
static_assert(offsetof(GlobalStatus, party_facing) == 0x18d0,
              "GlobalStatus_party_facing_offset");
static_assert(offsetof(GlobalStatus, current_level) == 0x1900,
              "GlobalStatus_current_level_offset");
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
static_assert(offsetof(GlobalStatus, rpc_races) == 0x243a,
              "GlobalStatus_rpc_races_offset");
static_assert(offsetof(GlobalStatus, monster_name_buffer) == 0x2453,
              "GlobalStatus_monster_name_buffer_offset");
static_assert(offsetof(GlobalStatus, sedexus_party_slot) == 0x247f,
              "GlobalStatus_sedexus_party_slot_offset");
static_assert(offsetof(GlobalStatus, greeting_pending) == 0x2497,
              "GlobalStatus_flag_2497_offset");
static_assert(offsetof(GlobalStatus, next_group_id) == 0x234a,
              "GlobalStatus_next_group_id_offset");
static_assert(offsetof(GlobalStatus, log_fact_checks) == 0x3120,
              "GlobalStatus_log_fact_checks_offset");
static_assert(offsetof(GlobalStatus, text_box_lines_shown) == 0x49a7,
              "GlobalStatus_text_box_lines_shown_offset");
static_assert(offsetof(GlobalStatus, intro_shown) == 0x49bc,
              "GlobalStatus_intro_shown_offset");
static_assert(offsetof(GlobalStatus, text_box_lines_used) == 0x4997,
              "GlobalStatus_migrated_values_offset");
static_assert(offsetof(GlobalStatus, rpc_active) == 0x2489,
              "GlobalStatus_flag_2489_offset");
static_assert(offsetof(GlobalStatus, status_ints) == 0x3121,
              "GlobalStatus_status_ints_offset");
static_assert(offsetof(GlobalStatus, fact_88_latch) == 0x40c1,
              "GlobalStatus_fact_88_latch_offset");
static_assert(offsetof(GlobalStatus, spell_usage) == 0x40ce,
              "GlobalStatus_spell_usage_offset");
static_assert(offsetof(GlobalStatus, vi_event_stage) == 0x498b,
              "GlobalStatus_value_498b_offset");
static_assert(offsetof(GlobalStatus, endgame2_queued) == 0x498f,
              "GlobalStatus_value_498f_offset");
static_assert(offsetof(GlobalStatus, endgame3_queued) == 0x4993,
              "GlobalStatus_value_4993_offset");
static_assert(offsetof(GlobalStatus, party_slot) == 0x249c,
              "GlobalStatus_party_slot_offset");
static_assert(offsetof(GlobalStatus, regular_member_count) == 0x000d,
              "GlobalStatus_regular_member_count_offset");
static_assert(offsetof(GlobalStatus, auxiliary_member_count) == 0x0011,
              "GlobalStatus_auxiliary_member_count_offset");
static_assert(offsetof(GlobalStatus, total_member_count) == 0x0015,
              "GlobalStatus_total_member_count_offset");
static_assert(offsetof(GlobalStatus, party_gold) == 0x0019, "GlobalStatus_party_gold_offset");
static_assert(sizeof(GlobalStatus) == 0x49c2, "GlobalStatus_must_be_0x49c2");
#pragma pack(pop)
} // namespace wiz8::retail

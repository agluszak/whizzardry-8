#ifndef WIZ8_LAYOUTS_GAMEPLAY_DATABASES_H
#define WIZ8_LAYOUTS_GAMEPLAY_DATABASES_H

#include <cstdint>
#include <string>
#include <vector>
struct W8NpcItemStockRule;

#include "wiz8/attack_modes.h"
#include "wiz8/spell_ids.h"
#include "wiz8/fact_state.h"
#include <stddef.h>

#include <algorithm>
#include "wiz8/dice.h"

/*
 * Runtime views of gameplay database records. Text is decoded UTF-8.
 * Offsets in comments describe the retail file, not these runtime views.
 * retail_text_records.h owns the UTF-16LE fields and disk strides.
 */

/* The fifteen professions, in the game's fixed class order. A character's
   current and original profession are one of these and index the
   per-profession tables; a generated character carries NONE until the player
   picks one. The NPC character block stores the same value. */
enum W8Profession {
    W8_PROFESSION_FIGHTER = 0,
    W8_PROFESSION_LORD = 1,
    W8_PROFESSION_VALKYRIE = 2,
    W8_PROFESSION_RANGER = 3,
    W8_PROFESSION_SAMURAI = 4,
    W8_PROFESSION_NINJA = 5,
    W8_PROFESSION_MONK = 6,
    W8_PROFESSION_ROGUE = 7,
    W8_PROFESSION_GADGETEER = 8,
    W8_PROFESSION_BARD = 9,
    W8_PROFESSION_PRIEST = 10,
    W8_PROFESSION_ALCHEMIST = 11,
    W8_PROFESSION_BISHOP = 12,
    W8_PROFESSION_PSIONIC = 13,
    W8_PROFESSION_MAGE = 14,
    W8_PROFESSION_COUNT = 15,
    W8_PROFESSION_NONE = -1
};

/* Race labels 644..659 in StringData.DAT. The first eleven are selectable
   player races; all sixteen occur
   in the race tables and NPC character records. NONE is an unselected race. */
enum W8Race {
    W8_RACE_NONE = -1,
    W8_RACE_HUMAN = 0,
    W8_RACE_ELF = 1,
    W8_RACE_DWARF = 2,
    W8_RACE_GNOME = 3,
    W8_RACE_HOBBIT = 4,
    W8_RACE_FAERIE = 5,
    W8_RACE_LIZARDMAN = 6,
    W8_RACE_DRACON = 7,
    W8_RACE_FELPURR = 8,
    W8_RACE_RAWULF = 9,
    W8_RACE_MOOK = 10,
    W8_RACE_TRYNNIE = 11,
    W8_RACE_T_RANG = 12,
    W8_RACE_UMPANI = 13,
    W8_RACE_RAPAX = 14,
    W8_RACE_ANDROID = 15,
    W8_RACE_PLAYABLE_COUNT = 11,
    W8_RACE_COUNT = 16
};


/* A character's sex. Zero is male and one is female, which is what the quote
   lookup, the item record's sex mask and the female-only profession all agree
   on. A character or template that has not been assigned one carries UNSET. */
enum W8Gender {
    W8_GENDER_MALE = 0,
    W8_GENDER_FEMALE = 1,
    W8_GENDER_COUNT = 2,
    W8_GENDER_UNSET = -1
};


enum { W8_MAX_MONSTER_ATTACKS = 3 };

/* One of a monster's three attacks. Monster.cpp walks the array with a
   0x22-byte stride and copies the missile-launch fields below into its local
   attack block. */
#pragma pack(push, 1)
struct W8MonsterAttack {
    bool fHasAttack; /* Retail offset 0x00 */
    /* Retail offset 0x01: indexes the announcement verb table; must not be
       MON_WEAPON_NAME_UNARMED. */
    std::uint8_t ubWeaponNameIndex;
    /* Retail offset 0x02: the weapon type; the announcement reports "weapon type is NONE"
       when it is zero. */
    std::uint8_t weapon_type;
    std::uint8_t range_category; /* Retail offset 0x03 */
    /* Retail offset 0x04: the attack's innate attack-score value; the monster score formula
       adds it alongside the modifier hit bonus and re-reads it inside the
       surprise repick penalty. */
    std::uint8_t attack_score;
    std::uint8_t missile_values[W8_ATTACK_EFFECT_COUNT]; /* Retail offset 0x05 */
    std::uint16_t attack_modes;                          /* Retail offset 0x15 */
    /* Retail offset 0x17: the attack's damage dice, packed; the missile path copies it
       into the attack block and the melee path rolls it. */
    W8Dice damage_dice;
    std::uint8_t missile_magnitude;
    /* Retail offset 0x1c: the attack's weapon class, handed signed to BlockedForSpecialReason
       the way a character's weapon_sound_class is. */
    std::int8_t weapon_class;
    std::int8_t missile_type; /* Retail offset 0x1d */
    std::uint8_t unknown_1e[4];
};
#pragma pack(pop) /* Retail offset 0x22 */

enum W8SpellRealm {
    W8_SPELL_REALM_NONE = -1,
    W8_SPELL_REALM_FIRE = 0,
    W8_SPELL_REALM_WATER = 1,
    W8_SPELL_REALM_AIR = 2,
    W8_SPELL_REALM_EARTH = 3,
    W8_SPELL_REALM_MENTAL = 4,
    W8_SPELL_REALM_DIVINE = 5,
    W8_SPELL_REALM_COUNT = 6
};

/* Power-pip selection and the cast loop share this signed dword domain. */
enum W8SpellPowerClass {
    W8_SPELL_POWER_CLASS_NONE = -1,
    W8_SPELL_POWER_SELECTABLE = 0,
    W8_SPELL_POWER_REPEAT_OUT_OF_COMBAT = 1,
    W8_SPELL_POWER_MAXIMUM = 2,
    W8_SPELL_POWER_FIXED = 3
};

/* The five situations a spell record can admit, numbered by the switch in
   SpellUsableNow. A spell usable at any time imposes no condition; the other
   four admit exactly one of combat, the field, camping, or a lock/trap
   interaction. */
enum W8SpellUsage {
    W8_SPELL_USABLE_ANY_TIME = 0,
    W8_SPELL_USABLE_IN_COMBAT = 1,
    W8_SPELL_USABLE_OUT_OF_COMBAT = 2,
    W8_SPELL_USABLE_WHILE_CAMPED = 3,
    W8_SPELL_USABLE_ON_LOCK_OR_TRAP = 4,
    W8_SPELL_USAGE_COUNT = 5
};

/* The range bands an attack or spell works at, and the one value that means it
   has no range at all. CalcRangeDistance is the one place the band and a
   world distance are related. */
enum W8RangeCategory {
    W8_RANGE_NONE = -1,
    W8_RANGE_TOUCH = 0,
    W8_RANGE_SHORT = 1,
    W8_RANGE_LONG = 2,
    W8_RANGE_EXTREME = 3
};

/* Which spellbooks a profession may draw on, and which a spell belongs to. The
   four flags live apart in the record rather than as one mask, so the mask is
   built from them wherever the profession table is tested. */
enum {
    W8_SPELLBOOK_NONE = 0,
    W8_SPELLBOOK_WIZARDRY = 1,
    W8_SPELLBOOK_DIVINITY = 2,
    W8_SPELLBOOK_ALCHEMY = 4,
    W8_SPELLBOOK_PSIONICS = 8
};

/* Who or what a spell is cast on. The eleven values are the spell record's
   target_type domain; the interface names them through string ids 798-808,
   which give "Caster", "One Ally", "Party", "One Enemy", "Enemy Group",
   "Cone", "Radius", "All Enemies", "Point" and "Adventuring" twice over - the
   two adventuring kinds are the carried item (Identify Item) and the lock or
   trap (Knock Knock, Divine Trap). This is not the targeting-need domain
   GetTargetNeededForSpellFriendly maps these onto. */
enum W8SpellTargetType {
    W8_TARGET_TYPE_NONE = -1,
    W8_TARGET_TYPE_CASTER = 0,
    W8_TARGET_TYPE_ALLY = 1,
    W8_TARGET_TYPE_PARTY = 2,
    W8_TARGET_TYPE_ENEMY = 3,
    W8_TARGET_TYPE_ENEMY_GROUP = 4,
    W8_TARGET_TYPE_CONE = 5,
    W8_TARGET_TYPE_RADIUS = 6,
    W8_TARGET_TYPE_ALL_ENEMIES = 7,
    W8_TARGET_TYPE_POINT = 8,
    W8_TARGET_TYPE_ITEM = 9,
    W8_TARGET_TYPE_LOCK_OR_TRAP = 10,
    W8_TARGET_TYPE_COUNT = 11
};

/* One spell, as the database holds it at run time. */
struct W8SpellRuntimeRecord {
    std::string database_name; /* Retail offset 0x000 */
    std::uint8_t unknown_040[4];
    /* Retail offset 0x044: base duration the spell-info dialog prints. */
    std::int32_t duration;
    std::uint8_t alchemy_spell; /* Retail offset 0x048 */
    std::int32_t spell_point_cost;        /* Retail offset 0x049: per power level */
    /* Retail offset 0x04d: per-level duration added to duration on the same line. */
    std::int32_t duration_per_level;
    W8Dice effect_dice; /* Retail offset 0x051 */
    std::uint8_t unknown_055;
    std::int32_t spell_level;              /* Retail offset 0x056: zero through seven */
    std::uint8_t wizardry_spell; /* Retail offset 0x05a */
    std::string resource_name;       /* Retail offset 0x05b: visual/MLS resource basename */
    std::string display_name;     /* Retail offset 0x09b */
    std::uint8_t unknown_11b[4];
    std::uint8_t divinity_spell; /* Retail offset 0x11f */
    std::uint8_t psionics_spell; /* Retail offset 0x120 */
    float effect_radius;          /* Retail offset 0x121 */
    std::uint8_t unknown_125;
    /* Retail offset 0x126: a monster may cast the spell at all. MonsterOKToCastSpell reports
       a spell without it by name and asserts. */
    std::uint8_t monster_castable;
    /* Retail offset 0x127: radius added per power level; PopulateSpellTargetMarkers scales
       the pair as (radius_per_level * power + effect_radius) * 500. */
    float radius_per_level;
    /* Retail offset 0x12b: the power class the spell-casting view copies into
       iSpellPowerClass (asserted not BAD_INDEX). It selects how
       power pips work: 2 prices a max cast, 3 is a fixed-cost spell, and
       SpellInfoDialog shows the alternate caption for 3. */
    W8SpellPowerClass power_class;
    /* Retail offset 0x12f: the range category a monster casting this spell needs. */
    W8RangeCategory range_category;
    W8SpellRealm realm;            /* Retail offset 0x133 */
    W8SpellTargetType target_type; /* Retail offset 0x137 */
    /* Retail offset 0x13b: when the spell may be cast. SpellUsableNow switches on it and its
       assertion calls it uiSpellUsableWhen with a SPELL_USAGE_COUNT of five. */
    W8SpellUsage usable_when;
    /* Retail offset 0x13f: the spell has to be aimed before it can be cast. */
    std::uint8_t needs_aim;
    /* Retail offset 0x140: the MissileTables.dbs row index the cast fires its effect
       through when missile_delivered is set. */
    std::int32_t missile_index;
    /* Retail offset 0x144: the spell's effect is delivered by a missile in flight, so its
       queued effect stays alive until the missile lands. Set for the ten
       projectile spells - Frost, Heal Wounds, Make Wounds, Sleep, Stamina,
       Terror, Noxious Fumes, Crush, Return to Portal and the monster Special
       Attack Cone. */
    std::uint8_t missile_delivered;
    std::uint8_t unknown_145[2];
    /* Retail offset 0x147: SpellInfoDialog prints the effect-dice line when this is set. */
    std::int32_t show_effect_dice;
    std::string sound_name; /* Retail offset 0x14b: relative to Data\Spells\Sounds */
};





struct W8FactDatabaseRecord {
    W8FactId identifier;      /* Retail offset 0x000 */
    std::string symbolic_name; /* Retail offset 0x004 .. 0x035 */
    /* Retail offset 0x036: the journal shades this fact's text when its value is true. */
    std::uint8_t highlight_when_true;
    /* Retail offset 0x037: the visibility level the journal entry needs. */
    std::int8_t visibility;
    /* Retail offset 0x038 and 0x100: the alternate and normal journal descriptions, wide,
       selected by the fact's current value. */
    std::string alternate_description;
    std::string description;
};



/* One optional NPC stock-rule entry appended after its database record: an
   item id, the configured quantity and a keep flag. The NPC restocks only
   while holding no more than half the quantity, and tops up by the shortfall. */
#pragma pack(push, 1)
struct W8NpcItemStockRule {
    std::int32_t item_id;              /* Retail offset 0x00: Items.dbs index */
    std::uint8_t quantity;   /* Retail offset 0x04: configured stock quantity */
    std::uint8_t persistent; /* Retail offset 0x05: retain and replenish this item */
};
#pragma pack(pop) /* Retail offset 0x06 */

/* The RPC-character block a record with has_group carries; it is expanded
   into a W8Character when the NPC joins. */
struct W8NpcCharacterTemplate {
    std::string name;       /* Retail offset 0x000, record 0x0c4 */
    std::string name_part_2; /* Retail offset 0x014, record 0x0d8 */
    std::uint8_t unknown_01a[0x44]; // Unidentified retail data, preserved verbatim.
    W8Profession
        profession;     /* Retail offset 0x064, record 0x128: index into profession_levels[W8_PROFESSION_COUNT] */
    W8Race race;        /* Retail offset 0x068, record 0x12c */
    std::int32_t table_value;    /* Retail offset 0x06c, record 0x130 */
    std::uint32_t level; /* Retail offset 0x070, record 0x134: starting profession level */
    std::int32_t attributes[7];  /* Retail offset 0x074, record 0x138: W8CharacterAttribute::value per attribute */
    std::int32_t skills[0x29];   /* Retail offset 0x090, record 0x154: W8CharacterSkill::value_02 per skill */
    std::uint8_t
        spells[0x72]; /* Retail offset 0x134, record 0x1f8: one flag per learnable spell, indexed from one */
    std::uint16_t equipment_present[12]; /* Retail offset 0x1a6, record 0x26a: zero leaves the slot empty */
    std::uint16_t equipment_ids[12];     /* Retail offset 0x1be, record 0x282: Items.dbs index, 0xffff empty */
    std::uint16_t backpack_present[8];   /* Retail offset 0x1d6, record 0x29a */
    std::uint16_t backpack_ids[8];       /* Retail offset 0x1e6, record 0x2aa */
    std::int8_t gender; /* Retail offset 0x1f6, record 0x2ba: copied into the character's own sex field */
    std::int16_t wanted_item_ids[3];
    std::uint8_t unknown_1fd[9];
};



/* One Data\Databases\NPC.DBS record. */
struct W8NpcDatabaseRecord {
    std::uint16_t
        version; /* Retail offset 0x000: two in the corpus; the rule tail loads only when this exceeds 1 */
    /* Retail offset 0x002: trade-pool stock count; CreateNpcRuntimeNode copies it into the
       runtime state's trade_pool. */
    std::int16_t trade_pool;
    /* Retail offset 0x004: the wide source name the level-entry rebinding prefixes with an
       underscore to build the NPC's trigger name. */
    std::string source_name;
    /* Retail offset 0x054: monster-bound NPC: no standalone runtime node is created, its
       state comes through the monster binding, and releasing the binding
       marks it unavailable. */
    std::uint8_t monster_bound;
    /* Retail offset 0x055: the NPC's stock is released only while this is set. */
    std::uint8_t owns_stock;
    /* Retail offset 0x056: merchant: still opens dialogue when the disposition band is
       hostile, and item drops go through the trade transcript layout. */
    std::uint8_t merchant;
    /* Retail offset 0x057: the NPC carries an RPC character and can join a monster group,
       which is what makes the group index on its runtime state meaningful. */
    bool has_group;
    /* Retail offset 0x058: the NPC's kind. Kind twenty refuses to trade. */
    std::int32_t kind;
    /* Retail offset 0x05c: the disposition byte CreateNpcRuntimeNode starts the state with. */
    std::uint8_t disposition;
    /* Retail offset 0x05d/0x05e: the signed scales the charm and talk interactions fold into
       the disposition shift; the sub-one path negates the /5 quotient. */
    std::int8_t charm_scale;
    std::int8_t talk_scale;
    /* Retail offset 0x05f: the faction the NPC belongs to; zero leaves GetNpcDisposition on
       the record's own disposition byte. */
    std::uint8_t faction;
    /* Retail offset 0x060: one bit per named-person alias the NPC answers to; read as one
       dword by FindNpcNameOrPlaceQuote. */
    std::uint32_t name_alias_mask;
    /* Retail offset 0x064: one-based index into g_item_tables selecting the record's item
       table, copied into the runtime state. */
    std::int32_t item_table_id;
    /* Retail offset 0x068: one bit per service the NPC offers. */
    std::uint32_t service_flags;
    std::uint8_t unknown_06c[2];
    /* Retail offset 0x06e: an allied faction; while a front-rank party member's bound NPC
       belongs to it, GetNpcDisposition pins this NPC's answer at fifty. The
       compare sign-extends it. */
    std::int8_t allied_faction;
    /* Retail offset 0x06f: the minimum average party level before this NPC's group can be
       invited. */
    std::uint8_t min_party_level;
    /* Retail offset 0x070/0x074: the level this NPC restores its binding at and the
       FindEntityByName key the restore moves its monster to - the default
       pair copied into the runtime state's pending restore. */
    char restore_level;
    std::uint8_t unknown_071[3];
    std::string restore_entity_name;
    /* Retail offset 0x09d: set when the record carries no appended item-stock
       rules; the load path reads them only while this is clear. */
    std::uint8_t no_item_stock;
    /* Retail offset 0x09e: what the NPC is called, unless a fact substitutes another name.
       The wide RPC-character name at 0x0c4 bounds the string extent. */
    std::string display_name;
    W8NpcCharacterTemplate character; /* Retail offset 0x0c4 */
    std::vector<W8NpcItemStockRule*>* item_stock_rules; /* Retail offset 0x2ca: W8NpcItemStockRule* elements */
    /* Retail offset 0x2ce/0x2d2: the trade price factors. CalculateTradeStackPrice adds the
       bargained adjustment to buy_price_factor when the NPC buys from the
       party and subtracts it from sell_price_factor when the party buys. */
    float buy_price_factor;
    float sell_price_factor;
    /* Retail offset 0x2d6: one accepted trade-item class per bit. */
    std::uint32_t trade_item_class_mask;
    std::uint8_t unknown_2da[0x10];
    /* Retail offset 0x2ea: voice-script NPC: picks the VOC_ script/sound prefix over NPC_
       and gates the normal dialogue paths. */
    std::uint8_t voice_script;
    /* Retail offset 0x2eb: the purse the NPC carries; the party gets it when the NPC's
       monster dies. */
    std::int32_t gold;
    bool combat_script_notice_enabled;       /* Retail offset 0x2ef: hostile-NPC combat notice */
    std::uint8_t allow_dismissed_departure_dialogue; /* Retail offset 0x2f0: permits dismissed-NPC dialogue */
    std::uint8_t unknown_2f1[0x18];
};





/* One Data\Databases\LEVELS.DBS record. The loader retains the disk row;
   all 60 canonical rows begin with a terminated UTF-16 display name. */
struct W8LevelDatabaseRecord {
    std::string display_name;
    /* Retail offset 0x3c..0x50: the per-level random-encounter budget parameters, all five
       read by UpdateRandomEncounterBudget and the sixth by the culling pass. */
    std::int32_t maximum_random_encounters; /* Retail offset 0x3c */
    std::int32_t minimum_random_encounters; /* Retail offset 0x40 */
    std::int32_t maximum_encounter_budget;  /* Retail offset 0x44 */
    std::int32_t minimum_encounter_budget;  /* Retail offset 0x48 */
    std::int32_t encounter_budget_period;   /* Retail offset 0x4c: elapsed-time divisor */
    std::int32_t encounter_culling_seconds; /* Retail offset 0x50 */
    /* Retail offset 0x54: float divisor the surprise transition uses when scaling game time
       and monster-generator duration. */
    float gameplay_time_scale;
    std::uint8_t unknown_058[0x80];
};





/* The one monster record whose alternate name is used in place of its own. */
enum { W8_MONSTER_RECORD_ALTERNATE_NAME = 397 };

/* One slot of W8MonsterRecord::treasure. type selects direct item (0) or
   item-table (1) drops; the entry only fires when count is nonzero and a
   Random(100) roll stays under chance. */
#pragma pack(push, 1)
struct W8MonsterTreasureEntry {
    std::uint8_t type;     /* Retail offset 0x00 */
    std::int16_t count;            /* Retail offset 0x01: nonzero gates the slot */
    std::uint16_t item_id; /* Retail offset 0x03: item id, or table id when type is 1 */
    std::uint8_t chance;   /* Retail offset 0x05 */
    W8Dice dice;            /* Retail offset 0x06: rolled once for the drop count */
};
#pragma pack(pop)

#pragma pack(push, 1)
struct W8MonsterTreasureBlock {
    W8MonsterTreasureEntry slots[8]; /* Retail offset 0x00 */
    W8Dice gold_dice;                /* Retail offset 0x50: rolled once into AddPartyGold */
};
#pragma pack(pop)

#pragma pack(push, 1)
struct W8EncounterCompanionRecord {
    std::int16_t species;
    std::uint8_t chance;
};
#pragma pack(pop)

/* Packed database flag byte. */
enum W8MonsterRecordFlag {
    W8_MONSTER_FLAG_NPC = 0x01,
    W8_MONSTER_FLAG_VULNERABLE_FROM_BEHIND = 0x02,
    W8_MONSTER_FLAG_BODYGUARD = 0x04,
    W8_MONSTER_FLAG_ALTERNATE_NAME = 0x10
};

struct W8MonsterRecord {
    std::string name0; /* Retail offset 0x000: suffix after '#' removed at load */
    std::string name1; /* Retail offset 0x030: suffix after '#' removed at load */
    std::string name2; /* Retail offset 0x060: suffix after '#' removed at load */
    std::string name3; /* Retail offset 0x090: suffix after '#' removed at load */
    /* Cosmic Forge's Monster Editor exposes this byte as 'Can open doors'. */
    bool can_open_doors;
    /* Retail offset 0x0c1: rolled by the group-attack summon to size the spawned group. */
    W8Dice group_size_dice;
    W8EncounterCompanionRecord companions[2];
    /* Retail offset 0x0cb: the monster's kind. The alchemy-casting rule admits kinds four,
       five and thirteen and no others, which is the only body that reads it. */
    std::uint8_t kind;
    /* Retail offset 0x0cc: selects this monster's row in the name-prefix table, the same
       table a character indexes by sex. */
    std::uint8_t name_group;
    /* Retail offset 0x0cd: the NPC record index this monster is bound to, fed to
       GetNpcStateByKind and compared against W8NpcState::name_style; 0xfa
       marks a record with no bound NPC. */
    std::uint8_t npc_kind;
    /* Retail offset 0x0ce: signed stamina regeneration rate (Monster Editor: ST Regen). */
    std::int8_t stamina_regeneration;
    /* Retail offset 0x0cf: the monster's own percentage reduction on incoming damage. */
    std::uint8_t damage_reduction;
    /* Retail offset 0x0d0: W8MonsterRecordFlag mask; retain the serialized byte width. */
    std::uint8_t flags;
    std::uint8_t attribute_values[5]; /* Retail offset 0x0d1 */
    W8Dice hit_points;                 /* Retail offset 0x0d6: rolled into uiHPMax/hp_current */
    W8Dice stamina_dice;               /* Retail offset 0x0da: initializes maximum and current stamina */
    std::uint8_t unknown_0de[2];
    /* Retail offset 0x0e0/0x0e1: the percentage chances the AI casts a spell or flees each
       round; a monster with a usable attack ignores them and always tries. */
    std::uint8_t spell_chance;
    std::uint8_t flee_chance;
    /* Retail offset 0x0e2: the percentage chance the AI advances on the party each round;
       zero falls back to the behavior byte at +0x1c0. */
    std::uint8_t advance_chance;
    /* Retail offset 0x0e3: Special attack selector; indexes the effect rows used by
       GroupAttacks.cpp and the corresponding display-name table. */
    std::uint8_t special_attack_kind;
    std::uint8_t initiative; /* Retail offset 0x0e4: Monster Editor Initiative */
    /* Retail offset 0x0e5/0x0e6: attacks and swings per round. */
    std::uint8_t attacks_per_round;
    std::uint8_t swings_per_round;
    /* Retail offset 0x0e7: the monster's three attacks, named by the Combat Range.cpp
       assertions pMonsterDB->Attack[uiAttack].fHasAttack and uiAttack <
       MAX_MONSTER_ATTACKS, which is what bounds the array at three. */
    W8MonsterAttack attacks[W8_MAX_MONSTER_ATTACKS]; /* Retail offset 0x0e7 */
    /* Retail offset 0x14d: the ten spells the AI may cast, zero for none. */
    std::uint8_t spells[10];
    std::uint8_t attack_body_part_chances[5];
    std::uint8_t special_attack_cooldown;
    std::int8_t evasion;
    /* Retail offset 0x15e: Monster Editor Constitution selector; chooses the body-specific
       hit-location label row. */
    std::uint8_t constitution;
    /* Retail offset 0x15f: the percentage of hits that land on each of the seven monster
       hit locations; the total is reported when it falls short of 100. */
    std::uint8_t hit_location_chances[7];
    /* Retail offset 0x166/0x16d: the monster's armour per hit location and per attack mode,
       the terms TargetArmorClassAtLocation subtracts on top of evasion. */
    std::int8_t armor_class_by_location[7];
    std::int8_t armor_class_by_attack_mode[9];
    /* Retail offset 0x176: the monster's own resistance per realm, read alongside the
       gameplay-modifier bonus wherever a character would read
       W8CharacterResistance::total. */
    std::uint8_t resistances[6];
    /* Retail offset 0x17c: hit points gained (or lost, when negative) per game minute. */
    std::int8_t hp_regeneration;
    std::uint8_t unknown_17d[4];
    std::uint32_t experience; /* Retail offset 0x181: base experience value */
    std::uint8_t unknown_185[2];
    std::int16_t record_id;       /* Retail offset 0x187: equals the zero-based database index */
    std::string cycle_name; /* Retail offset 0x189: GrCycle lookup key */
    /* Retail offset 0x1b9: prefer ranged actions instead of closing/backing off. */
    std::uint8_t prefer_ranged_actions;
    float combat_move_range; /* Retail offset 0x1ba: designer-facing combat movement range */
    /* Retail offset 0x1be: blocks the DEAD condition. */
    std::uint8_t instant_death_immune;
    /* Retail offset 0x1bf/0x1c0: the Monster Editor's Combat Behaviour and Combat Morale selectors. */
    std::uint8_t combat_behavior;
    std::uint8_t combat_morale;
    /* Retail offset 0x1c1: the MIPE monster list only admits records carrying -1 here, and
       stores the value itself as the selected monster index. */
    std::int16_t editor_index;
    /* Cosmic Forge's Remains column consumes this as a narrow item-model
       name. The canonical rows contain creaturegoo, plantgoo or an empty
       string. It precedes the treasure entries rather than belonging to them. */
    std::string remains_model_name;
    /* Retail offset 0x1f3: the monster's treasure table. DropMonsterLoot fires each slot
       whose count is nonzero once Random(100) stays under its chance, rolling
       the slot dice for the drop count; type 0 creates the item id directly,
       type 1 feeds the id to GenerateItemsFromTable. The gold dice lands in
       AddPartyGold. */
    W8MonsterTreasureBlock treasure;
    std::uint8_t attack_multiple_targets;
    /* Retail offset 0x248: Monster Editor camouflage rating. */
    std::uint8_t camouflage0;
    std::uint8_t camouflage1;
    /* Retail offset 0x24a: the monster cannot be targeted at all. Every sweep that gathers
       candidates drops it before any other test. */
    std::uint8_t untargetable;
    std::uint8_t unknown_24b[4];
    /* Retail offset 0x24f: effective/combat monster level used by attack, resistance,
       spell and effect formulas. Distinct from the displayed level at 0x251. */
    std::uint8_t effective_level;
    std::uint8_t unknown_250;
    /* Retail offset 0x251: monster level shown by MonsterInfo and related UI. */
    std::uint8_t display_level;
    std::uint8_t unknown_252;
    std::int32_t model_index;           /* Retail offset 0x253 */
    std::int32_t alternate_model_index; /* Retail offset 0x257: alternate selected value */
    std::int32_t hostility_radius;      /* Retail offset 0x25b: Minimal Neutrality Distance / Hostility Radius */
    std::int32_t faction_id;            /* Retail offset 0x25f: W8Faction value, domain 0..20 */
    std::int32_t material;              /* Retail offset 0x263: Monster Editor Material selector */
    std::uint8_t deleted;     /* Retail offset 0x267: LoadMonsterGroup skips the record */
    std::uint8_t significant_kill;
    std::uint8_t unknown_269;
    /* Retail offset 0x26a: unborn monsters enter the birth/encounter lists before acting. */
    std::uint8_t unborn;
    std::uint32_t experience_override; /* Retail offset 0x26b: nonzero experience override */
    /* Retail offset 0x26f: the spell-point budget the monster casts out of, before its own
       runtime bonus. Zero is a data error the power-level chooser reports by
       name. */
    std::int32_t sp_budget;
    std::uint8_t unknown_273[0x24];
};












/* Data\Databases race table rows the resistance recalculation reads. A
   resistance_index of -1 terminates a race's list; an adjustment above
   W8_RACE_ADJUSTMENT_ATTRIBUTE_BIAS names a character attribute instead of a
   flat amount. */
struct W8RaceResistanceAdjustment {
    std::int32_t resistance_index;
    std::int32_t adjustment_or_attribute;
}; /* Retail offset 0x08 */

struct W8RaceResistanceProfile {
    W8RaceResistanceAdjustment adjustments[6];
}; /* Retail offset 0x30 */

/* Runtime roots for the database records described above. GameplayDatabase.cpp
   owns their storage; consumers reach it through this one declaration surface
   instead of the gameplay-boundary quarantine. */
extern W8FactDatabaseRecord* g_fact_records;
extern W8LevelDatabaseRecord* g_level_records;
extern W8NpcDatabaseRecord* g_npc_records;
extern W8SpellRuntimeRecord* g_spell_records;
extern std::uint32_t g_spell_database_version;

#endif

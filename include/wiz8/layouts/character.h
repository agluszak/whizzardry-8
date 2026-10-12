#ifndef WIZ8_LAYOUTS_CHARACTER_H
#define WIZ8_LAYOUTS_CHARACTER_H

#include <cstdint>
#include <string>

#include "wiz8/attack_modes.h"
#include "wiz8/character_skills.h"
#include "wiz8/conditions.h"
#include "wiz8/load_category.h"
#include "surrender/srMath.h"
#include "wiz8/gameplay_modifiers.h"
#include "wiz8/layouts/item_instance.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/world.h"


/* One enchantment slot. Both a character and a monster carry eight of them,
   and both clear a slot by zeroing all three dwords at once. */
#pragma pack(push, 1)
struct W8Enchantment {
    /* Retail offset 0x00: the enchantment's power; a slot is only overwritten by a higher
       (unsigned) power. Slot three also spends it as a charge count -
       Combat Attack decrements it per hit and clears the slot at zero. */
    std::uint32_t power;
    /* Retail offset 0x04: a percentage. */
    std::uint16_t percent;
    /* Retail offset 0x06: the percent-scaled magnitude: a dice roll times the power, raised
       by percent. The stamina path drains
       it as the absorb pool. */
    std::uint16_t magnitude;
    /* Retail offset 0x08: turns left; the fatigue path consults it on slot five. */
    std::uint32_t turns;
};
#pragma pack(pop) /* Retail offset 0x0c */

/* Runtime names own UTF-8 strings. retail_text_records.h encodes the
   10/6-unit UTF-16LE fields separately and preserves the following unknown bytes. */

/* Base attributes and their effective values after equipment and effects. */
#pragma pack(push, 1)
struct W8CharacterAttribute {
    std::uint32_t base; /* Retail offset 0x00 */
    std::uint32_t effective;
    std::int32_t change_counter;
    std::uint8_t unknown_0c[8];
};
#pragma pack(pop) /* Retail offset 0x14 */

/* One skill record, indexed directly by skill id. The leading flag is set
   when a skill first becomes available. */
#pragma pack(push, 1)
struct W8CharacterSkill {
    bool active; /* Retail offset 0x00: skill slot in use */
    /* Retail offset 0x01: set by ResetSkillContribution; never read. */
    bool reset_flag;
    /* Retail offset 0x02: invested points. Spellbook selection and skill-increase notices
       read these; the spell-learning ceiling divides them by ten. */
    std::uint32_t points;
    /* Retail offset 0x06: current level after profession/modifier adjustments. Spell pricing
       and resistance recalculation read this instead of the invested points. */
    std::uint32_t level;
    /* Retail offset 0x0a: the attribute-derived base the level-up reset recomputes and the
       profession-skill assignment scales, before the spent points land on
       value_02. */
    std::uint32_t base_level;
    /* Retail offset 0x0e: successful increase rolls; every eighth one raises value_02 by one
       and resets the count - PracticeCharacterSkill's practice tally. */
    std::uint32_t practice_count;
    /* Retail offset 0x12: set when practice raises value_02; the flag the skill-increase
       notices key off. */
    bool improved;
    bool available;
    std::uint8_t unknown_14[0x12];
};
#pragma pack(pop) /* Retail offset 0x26 */

/* One resistance channel. Recalculation rebuilds `base` from scratch each time
   and then derives `total` from it, so the two are a computed pair rather than
   a stored value and a cache. */
#pragma pack(push, 1)
struct W8CharacterResistance {
    std::uint32_t base;  /* Retail offset 0x00 */
    std::uint32_t total; /* Retail offset 0x04: clamped to 100 */
    std::uint8_t unknown_08[8];
};
#pragma pack(pop) /* Retail offset 0x10 */

/* The six realms a spell point pool is kept per are W8SpellRealm's, declared
   with the spell record in wiz8/layouts/gameplay_databases.h. */

enum {
    W8_RESISTANCE_COUNT = 6,
    /* The six skills whose level feeds the matching resistance, and the one
       whose presence adds a flat bonus to every one of them. */
    W8_FIRST_RESISTANCE_SKILL = 28,

    /* A race adjustment at or below this is a flat amount; above it, it selects
       a character attribute by index biased this far. */
    W8_RACE_ADJUSTMENT_ATTRIBUTE_BIAS = 1000
};

/* One hand's derived attack block - the elements of W8Character::Hand. */
enum { HOLDS_NOTHING = 0 };
#pragma pack(push, 1)
struct W8HandAttack {
    std::int32_t uiHolds;                 /* Retail offset 0x00 */
    bool in_play;                /* Retail offset 0x04 */
    W8Skill weapon_skill;        /* Retail offset 0x05, unaligned */
    W8Skill combat_skill;        /* Retail offset 0x09 */
    std::uint32_t combined_skill; /* Retail offset 0x0d */
    std::int32_t attack_score;            /* Retail offset 0x11 */
    std::uint32_t attacks;        /* Retail offset 0x15 */
    std::int32_t swings;                  /* Retail offset 0x19 */
    std::int32_t damage_bonus;            /* Retail offset 0x1d */
    std::int32_t hit_bonus;               /* Retail offset 0x21 */
    std::int32_t attack_bonus;            /* Retail offset 0x25: attack-score term, paired with modifier attack_bonus */
    std::int32_t damage_percent; /* Retail offset 0x29: percent damage multiplier, paired with modifier damage_percent */
    W8Dice damage_dice; /* Retail offset 0x2d */
    std::uint16_t attack_flags; /* Retail offset 0x31 */
    std::uint8_t
        condition_chances[W8_ATTACK_EFFECT_COUNT]; /* Retail offset 0x33: unarmed condition probabilities */
    std::uint8_t unknown_43[0x18];
};
#pragma pack(pop) /* Retail offset 0x5b */

/* One condition record. GetConditionRecordFlag reads byte 8 of one of the
   four; the condition-0x13 fold reads record one's leading dwords as the level
   the binding was made on and the bound monster's id. */
#pragma pack(push, 1)
struct W8CharacterConditionRecord {
    std::int32_t level_acquired; /* Retail offset 0x00: level the condition attached on */
    std::int32_t source_monster; /* Retail offset 0x04: monster id/location the dependence is bound to */
    bool active;
    std::uint8_t unknown_09[8];
};
#pragma pack(pop) /* Retail offset 0x11 */

struct W8Character {
    /* Retail offset 0x0000: SaveCharacter stamps 1 here before writing the record, so the
       leading dword is a saved-record version rather than runtime state. */
    std::uint32_t record_version;
    bool fInParty; /* Retail offset 0x0004 */
    /* Retail offset 0x0005: the character's name, also the stem of its "%s.CHR" file. */
    std::string name;
    std::string name_part_2; /* Retail offset 0x0019: rendered as the parenthesized name */
    std::uint8_t unknown_0025[0x44]; // Unidentified retail data, preserved verbatim.
    /* Retail offset 0x0069 and 0x006d: the current profession and the one the character
       started in. The level band subtracts a base only while the two agree. */
    W8Profession iProfession;         /* Retail offset 0x0069 */
    W8Profession original_profession; /* Retail offset 0x006d */
    /* Retail offset 0x0071: indexes the race resistance table. */
    W8Race iRace;
    /* Retail offset 0x0075: zero is male and one is female. The quote lookup names the
       Data\Quotes\PCs files m_ or f_ from it, the item record's two-bit mask
       admits exactly one sex, and the female-only profession at index two
       forces the field to one. */
    W8Gender gender;
    /* Retail offset 0x0079: the portrait/catalog index. Looked up by gender, race and
       profession together, drawn by RenderPartyPortrait, and reassigned from
       g_portrait_groups[].portraits[] when the portrait changes. */
    std::int32_t portrait_index;
    /* Retail offset 0x007d: cleared by DeriveCharacterPersonality in both of its branches and set to -1
       by the character rebuild; all three accesses are four-byte stores. */
    std::int32_t unknown_007d;
    std::int32_t personality; /* indexes the state-5 descriptor text */
    std::int32_t voice;       /* selected by the character voice control */
    /* Retail offset 0x0089: the experience level; averaged across occupied slots. */
    std::uint32_t uiExpLevel;
    std::int32_t profession_levels[W8_PROFESSION_COUNT]; /* Retail offset 0x008d */
    std::uint8_t unknown_00c9[0x14];
    /* Retail offset 0x00dd: the eight-band ladder over the character's level in their
       current profession, and the base subtracted from it while they are still
       in the profession they started in. */
    std::int32_t level_band;
    std::int32_t level_band_base;
    W8CharacterAttribute attributes[7]; /* Retail offset 0x00e5, indexed by skill_id - 0x22 */
    std::uint8_t unknown_0171[0x28];
    /* Retail offset 0x199: the attribute points the level-up reset still owes against the
       profession's minimums. It accumulates as a negative debt and is drawn
       back down one point at a time once the pool is positive. */
    std::int32_t attribute_point_deficit;
    W8CharacterSkill skills[0x29]; /* Retail offset 0x019d, indexed by skill_id */
    std::uint8_t unknown_07b3[0x23a];
    /* Retail offset 0x09ed..0x09f8: experience, the goal for the next level, and the goal
       the previous level had. A character is ready to advance once the first
       reaches the second. */
    std::uint32_t experience;
    std::uint32_t experience_goal;
    std::uint32_t experience_previous_goal;
    /* Retail offset 0x09f9: this character's confirmed kills, shown on the camp stats
       page and imported from the Wiz7 record. */
    std::int32_t kill_count;
    /* Retail offset 0x09fd: how many times this character has died. */
    std::int32_t death_count;
    /* Retail offset 0x0a01: one entry per condition, holding how long it has left to run;
       W8_CONDITION_INDEFINITE means until something lifts it. The monster
       carries the identical array. */
    std::uint32_t uiCondition[W8_CONDITION_COUNT]; /* Retail offset 0x0a01 */
    std::uint8_t unknown_0a51[0x14];
    W8Enchantment enchantments[8]; /* Retail offset 0x0a65 */
    std::uint8_t unknown_0ac5[0x3c];
    /* Retail offset 0x0b01: the highest currently-set condition index, rescanned from
       uiCondition[0x13] downward whenever a condition is
       lifted. Zero is either "none set" or condition zero, matching the
       monster copy at W8MonsterInfo::highest_condition. Thresholds are the
       condition ids themselves: below HOSTILE for rest/formation, below
       DEAD for ordinary party eligibility. */
    W8Condition highest_condition;
    /* Retail offset 0x0b05: the highest enchantment slot still in use, recomputed by
       scanning down from the last one whenever a slot is cleared. */
    W8EnchantmentSlot enchantment_top;
    /* Retail offset 0x0b09: the argument COND_POISONED carries (poison strength). */
    std::int32_t condition_argument;
    /* Retail offset 0x0b0d..0x0b20: the two pools with a ceiling each, plus the adjustment
       damage is booked against before hit points are recalculated. A character
       whose hp_current is zero is treated as out of the fight everywhere. */
    std::int32_t uiHPMax;                  /* Retail offset 0x0b0d */
    std::uint32_t hp_current;      /* Retail offset 0x0b11 */
    std::int32_t hp_adjustment;            /* Retail offset 0x0b15 */
    std::int32_t uiStaminaMax;             /* Retail offset 0x0b19 */
    std::int32_t stamina;                  /* Retail offset 0x0b1d */
    std::uint32_t fatigue_penalty; /* Retail offset 0x0b21: taken off the stamina ceiling */
    /* Retail offset 0x0b25 and 0x0b45: the spell-point pools, one per spell realm. */
    std::int32_t sp_max[W8_SPELL_REALM_COUNT]; /* Retail offset 0x0b25 */
    std::uint8_t unknown_0b3d[8];
    std::int32_t iSPLeft[W8_SPELL_REALM_COUNT]; /* Retail offset 0x0b45 */
    std::uint8_t unknown_0b5d[8];
    /* Retail offset 0x0b65: game minutes until the alchemist (trait MAKE_POTIONS) may brew
       again; the aging tick counts it down outside surprise, and a successful
       brew restarts it at 0x168. */
    std::uint32_t potion_brew_cooldown;
    /* Retail offset 0x0b69, 0x0b71 and 0x0b79: the per-tick regeneration rates rebuilt from
       the pool ceilings, one each for hit points and stamina and one per spell
       realm at a two-float stride. */
    float health_regen_rate;
    /* Retail offset 0x0b6d: fractional hit-point regeneration remainder carried between
       ticks; reset to zero when hit points reach the ceiling. */
    float health_regen_accumulator;
    float stamina_regen_rate;
    /* Retail offset 0x0b75: fractional stamina-regeneration remainder carried between
       ticks; reset to zero when stamina reaches the ceiling. */
    float stamina_regen_accumulator;
    float spell_regen_rates[12];
    std::uint8_t unknown_0ba9[0x10];
    std::uint32_t inventory_weight;     /* Retail offset 0x0bb9 */
    std::uint32_t party_weight_share;   /* Retail offset 0x0bbd */
    std::uint32_t total_carried_weight; /* Retail offset 0x0bc1 */
    std::uint32_t carrying_capacity;    /* Retail offset 0x0bc5; displayed divided by 10 */
    /* Retail offset 0x0bc9: the load category, zero through four, which scales what an
       action costs in fatigue. FatigueCharacter's error text calls it that. */
    W8LoadCategory load_category;
    /* Retail offset 0x0bcd: one entry per spell. CanCharacterUseItem refuses a spell-source
       item whose spell already reads one here, so one is the learned state. */
    std::int32_t spell_learned[136]; /* Retail offset 0x0bcd */
    /* Retail offset 0x0ded: indexed by skill_id. For the six realm skills it counts the
       spells known in that realm, which is what makes the skill available at
       all - LearnSpell bumps the entry and IsCharacterSkillAvailable reads it.
       LearnSpell also writes a recomputed figure into entry 36, which is a
       real skill id but not a count. */
    std::uint32_t skill_unlocks[0x25];
    /* Retail offset 0x0e81: rebuilt by CalcInitiative from level, speed, senses, the
       initiative skill and the current load category. */
    std::int32_t initiative;
    /* Retail offset 0x0e85..0x0e8c: the two armor-class summaries rebuilt after the
       location values below. */
    std::int32_t armor_class_total;
    std::int32_t armor_class_average;
    /* Retail offset 0x0e8d: thirteen armor-class components. CalcArmorClasses clears the
       whole run before applying equipment, traits, load and fatigue. */
    std::int32_t armor_class_components[13];
    std::int32_t armor_class_by_location[5]; /* Retail offset 0x0ec1 */
    std::uint8_t unknown_0ed5[4];
    /* Retail offset 0x0ed9: a percentage taken off incoming damage, the character's
       counterpart of the monster's own at 0x1e1. */
    std::int32_t damage_reduction;
    W8CharacterResistance resistances[W8_RESISTANCE_COUNT]; /* Retail offset 0x0edd */
    std::uint8_t flags0[0x20];
    /* Retail offset 0x0f5d: the twelve worn/held slots, indexed by W8EquipSlot. Slots six
       and seven are the primary hands and eight and nine the alternate pair. */
    W8ItemInstance EquippedItem[12]; /* Retail offset 0x0f5d */
    std::uint8_t flags1[0x3c];
    /* Retail offset 0x1029: the eight per-character carried slots. GetOriginOfCharacterItem
       reports this array as origin zero and the equipment array as origin one. */
    W8ItemInstance backpack[8]; /* Retail offset 0x1029 */
    std::uint8_t unknown_1089[0xc0];
    /* Retail offset 0x1149: the two per-hand derived attack blocks. */
    W8HandAttack Hand[2];
    std::uint8_t unknown_11ff[0xb6];
    std::uint8_t dual_wielding; /* Retail offset 0x12b5 */
    /* Retail offset 0x12b6: per-monster-id detection score, one byte per record id. The
       wandering-group pass adds the group's member count while the party stays
       unaware, and the detection roll subtracts the record's effective level
       before scaling. */
    std::uint8_t monster_awareness[0x3e8];
    /* Retail offset 0x169e: the fatigue band, zero through four, recomputed from the stamina
       fraction whenever it moves; a change re-runs the armour class pass. */
    std::int32_t fatigue_band;
    /* Retail offset 0x16a2: the modifier block rebuilt from the character's condition
       durations, enchantment slots and the bound-NPC penalty; folded into
       `bonus` alongside the equipment and party blocks. */
    W8GameplayModifierBlock condition_modifiers;
    /* Retail offset 0x1709: the equipment bonus block accumulated from the worn items and
       folded into `bonus`. */
    W8GameplayModifierBlock equipment_bonus;
    /* Retail offset 0x1770: the derived modifier block the rebuild clears and folds the
       equipment, persistent and party blocks into. */
    W8GameplayModifierBlock bonus;
    /* Retail offset 0x17d7: the CamPos record cameraState writes and recall restores.
       The cross-level path copies the whole 0x3c bytes onto
       W8GlobalStatus::pending_move_location. */
    W8WorldCameraState saved_location; /* Retail offset 0x17d7 */
    /* Retail offset 0x1813: which level that anchor belongs to. The recall compares it
       against g_status.current_level and takes a different path when they differ. */
    std::int32_t saved_level;                          /* Retail offset 0x1813 */
    W8CharacterConditionRecord conditions[4]; /* Retail offset 0x1817 .. 0x185a */
    /* Retail offset 0x185b: the deep-fatigue effect is already on this character, which is
       what stops FatigueCharacter re-applying it every turn. */
    bool deep_fatigue_applied;
    /* Retail offset 0x185c: one cost per skill id 0x18..0x1b, read by the profession-change
       cost diff with the skill id biased down. */
    std::uint8_t skill_costs[4];
    std::uint8_t magic_bonus_pool;
    /* Retail offset 0x1861: the anchor above has been set. Recall does nothing without it. */
    bool has_saved_location;
};



enum W8SkillImportPolicy {
    W8_SKILL_IMPORT_POLICY_0 = 0,
    W8_SKILL_IMPORT_POLICY_1 = 1,
    W8_SKILL_IMPORT_PROFESSION = 2,
    W8_SKILL_IMPORT_DISABLED = 3,
};

struct W8SkillAttributes {
    std::int32_t category;
    W8SkillImportPolicy import_policy; /* Retail offset 0x04: Wiz7 skill import eligibility */
    /* Retail offset 0x08/0x0c: governing attributes averaged into base_level and
       listed in the stat-info dialog. */
    W8Attribute attribute_1;
    W8Attribute attribute_2;
};


extern W8RaceResistanceProfile g_race_resistance_profiles[];
extern std::int32_t g_profession_skill_availability[0x29][W8_PROFESSION_COUNT];
extern W8Skill g_profession_bonus_skills[W8_PROFESSION_COUNT];
extern W8SkillAttributes g_skill_attributes[0x29];

/* Profession and race trait sets consulted by CharacterHasTrait. Each entry is
   only its id list: three profession abilities, five race abilities. */
struct W8ProfessionAbilitySet {
    W8Trait ability_ids[3];
};

struct W8RaceAbilitySet {
    W8Trait ability_ids[5];
};

extern W8ProfessionAbilitySet g_profession_abilities[W8_PROFESSION_COUNT];
extern W8RaceAbilitySet g_race_abilities[W8_RACE_COUNT];
extern W8Skill g_profession_skills[W8_PROFESSION_COUNT][4];
extern std::int32_t g_profession_magic_level_offsets[W8_PROFESSION_COUNT];
extern float g_profession_hit_point_factors[W8_PROFESSION_COUNT];

struct W8PortraitDescriptor {
    std::int32_t group;
    std::int32_t unknown_004;
    std::int32_t unknown_008;
    std::int32_t render_mode;
};


extern W8PortraitDescriptor g_portrait_descriptors[80];

#endif

#pragma once

#include <array>

#include "wiz8/data/records.h"

struct GameData;
struct W8Character;

/* W8Character::armor_class_by_location and gubLocalACPercent share this order.
   The armor-class derivation maps equipment slots {Head,Torso,Legs,Feet,Hands} onto it. */
enum W8ArmorLocation {
    W8_ARMOR_LOCATION_HEAD = 0,
    W8_ARMOR_LOCATION_TORSO = 1,
    W8_ARMOR_LOCATION_LEGS = 2,
    W8_ARMOR_LOCATION_FEET = 3,
    W8_ARMOR_LOCATION_HANDS = 4,
    W8_ARMOR_LOCATION_COUNT = 5
};

/* The twelve values shown by the Armor Class Modifiers panel. The first eleven
   labels are listed in this order by the manual and line up one-for-one with
   the armor-class derivation's producers. Slot eleven is only displayed when nonzero and
   is fed exclusively by skill 0x26, Reflextion. The backing character array
   has one additional cleared-but-undisplayed entry at index twelve. */
enum W8ArmorClassComponent {
    W8_AC_COMPONENT_RACE = 0,
    W8_AC_COMPONENT_SPEED = 1,
    W8_AC_COMPONENT_STEALTH = 2,
    W8_AC_COMPONENT_SHIELD = 3,
    W8_AC_COMPONENT_MAGIC_ITEMS = 4,
    W8_AC_COMPONENT_MAGIC_SPELLS = 5,
    W8_AC_COMPONENT_VS_PENETRATION = 6,
    W8_AC_COMPONENT_ENCUMBRANCE = 7,
    W8_AC_COMPONENT_CONDITIONS = 8,
    W8_AC_COMPONENT_FATIGUE = 9,
    W8_AC_COMPONENT_DEFENSIVE_ACTION = 10,
    W8_AC_COMPONENT_REFLEXTION = 11,
    W8_AC_COMPONENT_UNUSED_12 = 12,
    W8_AC_COMPONENT_COUNT = 13
};

extern const unsigned char gubLocalACPercent[W8_ARMOR_LOCATION_COUNT];
// The armor-class penalty, in tenths, of a fatigue band.
unsigned int FatigueArmorPenalty(int fatigue_band);
// The fatigue band, zero through four, of a stamina pool; an empty ceiling is full fatigue.
int FatigueBand(int stamina, int stamina_max);

/* The inputs to a derivation that come from outside the character: the combat action
   and NPC story state. live_party.h supplies the active game's. */
struct CharacterStatRules {
    bool defensive_action = false;
    bool damaged_android = false;
};

/* The derived values an operation starts from. The resource transitions compare the new
   ceilings and weight with it; every pass of one operation reads the same baseline. */
struct CharacterBaseline {
    int hp_max = 0;
    int stamina_max = 0;
    std::array<int, W8_SPELL_REALM_COUNT> sp_max{};
    unsigned int inventory_weight = 0;
};
CharacterBaseline CaptureCharacterBaseline(const W8Character& character);

/* Derive every statistic from the character's evaluated inputs (effective attributes,
   skill levels, modifier blocks and hand kinds; see character_derivation.h), its equipment,
   learned spells, conditions, fatigue band and assigned party weight share: the pool
   ceilings, regeneration rates, damage reduction, resistances, carrying capacity, weight
   and load, initiative, attacks and armor class. Current pools are left alone. */
void DeriveCharacterStats(const GameData& data, W8Character& character, CharacterStatRules rules);

unsigned int CarryingCapacity(const W8Character& character);
unsigned int InventoryWeight(const GameData& data, const W8Character& character);

/* Resource transitions after a derivation. Each pool follows the change of its ceiling. */
// True when a changed ceiling exhausted the hit points; the caller applies death.
[[nodiscard]] bool FollowHitPointCeiling(W8Character& character, int previous_ceiling);
// The fatigue band follows the stamina left; true when the band changed.
bool FollowStaminaCeiling(W8Character& character, int previous_ceiling);
void FollowSpellPointCeilings(W8Character& character,
                              const std::array<int, W8_SPELL_REALM_COUNT>& previous_ceilings);
// The inventory weight crossed the carrying capacity.
bool BecameOverloaded(unsigned int previous_inventory_weight, const W8Character& character);

/* Zeroes the six per-realm learned-spell counters and recounts them from the
   spell_learned array. */
void RecountLearnedSpellsByRealm(const GameData& data, W8Character* character);
/* Rebuild the six realm spell-point ceilings; returns the largest computed ceiling.
   Learning a spell rebuilds them for every profession, while the derivation skips the
   professions that keep no pools of their own. */
int RebuildRealmSpellPointCeilings(const GameData& data, W8Character* character);

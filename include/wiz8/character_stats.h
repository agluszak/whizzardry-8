#pragma once

struct W8Character;

/* W8Character::armor_class_by_location and gubLocalACPercent share this order.
   CalcArmorClasses maps equipment slots {Head,Torso,Legs,Feet,Hands} onto it. */
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
   CalcArmorClasses' producers. Slot eleven is only displayed when nonzero and
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

void CalcInitiative(W8Character* character);
void CalcAttacks(W8Character* character, bool damaged_android);
void CalcArmorClasses(W8Character* character, bool defensive_action);
extern const unsigned char gubLocalACPercent[W8_ARMOR_LOCATION_COUNT];

// True means a changed ceiling exhausted the pool; the caller applies death policy.
[[nodiscard]] bool RecalculateCharacterHitPoints(W8Character* character);

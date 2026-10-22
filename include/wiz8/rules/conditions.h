#pragma once

/* Condition duration indices; the icon path maps live condition N to icon N-1. */
enum W8Condition {
    W8_CONDITION_NONE = 0,
    W8_CONDITION_DRAINED = 1,
    W8_CONDITION_DISEASED = 2,
    W8_CONDITION_IRRITATED = 3,
    W8_CONDITION_NAUSEATED = 4,
    W8_CONDITION_SLOWED = 5,
    W8_CONDITION_AFRAID = 6,
    W8_CONDITION_POISONED = 7,
    W8_CONDITION_SILENCED = 8,
    W8_CONDITION_HEXED = 9,
    W8_CONDITION_INFATUATED = 10,
    W8_CONDITION_INSANE = 11,
    W8_CONDITION_BLIND = 12,
    W8_CONDITION_TURNCOAT = 13,
    W8_CONDITION_WEBBED = 14,
    W8_CONDITION_ASLEEP = 15,
    W8_CONDITION_PARALYZED = 16,
    W8_CONDITION_UNCONSCIOUS = 17,
    W8_CONDITION_DEAD = 18,
    W8_CONDITION_MISSING = 19,
};

/* Counts and duration values are separate from condition identities. */
enum { W8_CONDITION_COUNT = 20, W8_CONDITION_CLEARABLE_COUNT = 18, W8_CONDITION_INDEFINITE = 9999 };

/* BindMonsterToCharacterDependence's DEPEND_COND_COUNT domain. Summon Elemental
   binds slot zero; the swallow attack binds slot one and applies MISSING. These
   index the separate binding records, not the condition-duration array. */
enum W8CharacterDependence {
    W8_DEPENDENCE_SUMMON = 0,
    W8_DEPENDENCE_SWALLOWED = 1,
    W8_DEPENDENCE_COUNT = 2
};

/* Enchantment N maps to monster icon N+16. Slot zero is the empty/top-scan sentinel. */
enum W8EnchantmentSlot {
    W8_ENCHANTMENT_NONE = 0,
    W8_ENCHANTMENT_DRACON_BREATH = 1,
    W8_ENCHANTMENT_GUARDIAN_ANGEL = 2,
    W8_ENCHANTMENT_RAZOR_CLOAK = 3,
    W8_ENCHANTMENT_EYE_FOR_AN_EYE = 4,
    W8_ENCHANTMENT_HASTE = 5,
    W8_ENCHANTMENT_SUPERMAN = 6,
    W8_ENCHANTMENT_BODY_OF_STONE = 7,
};

/* String ids of one condition's notices (e.g. "Poisoned", "gets poisoned",
   "get poisoned", "poison"). */
struct W8ConditionNoticeIds {
    unsigned short name;
    unsigned short singular;
    unsigned short plural;
    unsigned short noun;
};

/* Notice string ids for each condition row: the status label, the singular
   and plural "inflicted" verb phrases and the noun used by resistance and
   immunity text. Rows past W8_CONDITION_MISSING are unused placeholders. */
inline constexpr W8ConditionNoticeIds kConditionNotices[25] = {
    {0x348, 0x349, 0x34a, 0x34b}, {0x34c, 0x34d, 0x34e, 0x34f}, {0x350, 0x351, 0x352, 0x353},
    {0x354, 0x355, 0x356, 0x357}, {0x358, 0x359, 0x35a, 0x35b}, {0x35c, 0x35d, 0x35e, 0x35f},
    {0x360, 0x361, 0x362, 0x363}, {0x364, 0x365, 0x366, 0x367}, {0x368, 0x369, 0x36a, 0x36b},
    {0x36c, 0x36d, 0x36e, 0x36f}, {0x370, 0x371, 0x372, 0x373}, {0x374, 0x375, 0x376, 0x377},
    {0x378, 0x379, 0x37a, 0x37b}, {0x37c, 0x37d, 0x37e, 0x37f}, {0x380, 0x381, 0x382, 0x383},
    {0x384, 0x385, 0x386, 0x387}, {0x38c, 0x38d, 0x38e, 0x38f}, {0x388, 0x389, 0x38a, 0x38b},
    {0x390, 0x391, 0x392, 0x393}, {0x394, 0x395, 0x396, 0x397}, {0x273, 0x65d, 0x65e, 0x65f},
    {0x660, 0x661, 0x662, 0x663}, {0x664, 0x665, 0x666, 0x667}, {0x668, 0x669, 0x66a, 0x66b},
    {0x66c, 0x66d, 0x66e, 0x66f},
};

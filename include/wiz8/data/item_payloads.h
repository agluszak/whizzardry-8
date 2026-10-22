#pragma once
#include <cstddef>
#include <type_traits>

#include "wiz8/data/attack_modes.h"
#include "wiz8/data/dice.h"

enum W8ItemEquipClass : unsigned char;
enum W8ItemCategory : unsigned char;
enum W8ItemQuantityKind : unsigned char;

namespace wiz8::retail {
// Unchanged numeric portions of Items.dbs. These contain no runtime ownership
// or handles, so the same checked, trivially copyable payload is shared by the
// disk record and the native item. The string-owning record has natural layout.
#pragma pack(push, 1)
struct ItemRequirement {
    unsigned char stat_id, minimum;
};
struct ItemProperties {
    /* 0x03c: the item number the Wizardry 7 import matches imported item ids
       against (Party Import.cpp). */
    short legacy_item_number;
    W8ItemEquipClass equip_class;           /* 0x03e */
    unsigned short unidentified_name_index; /* 0x03f */
    unsigned char flags;                    /* 0x041: W8ItemFlag bits */
    W8ItemCategory category;                /* 0x042 */
    unsigned char unknown_043[3];
    signed char weapon_skill; /* 0x046: -1 when the item grants none */
    /* 0x047: the W8RangeCategory band the weapon attacks at. GetCharAttackRange
       returns it, the Assay caption names it, and paired weapons/ammunition
       must share it. */
    unsigned char range_category;
    signed char attack_damage_bonus; /* 0x048 */
    signed char attack_hit_bonus;    /* 0x049 */
    W8Dice damage_dice;              /* 0x04a */
    unsigned short attack_flags;
    /* 0x050..0x05f: the item's missile-attack modifier block; the missile
       resolver sums it byte-wise across the wielded and paired weapons into
       the fired effect definition's condition_chances. */
    unsigned char missile_values[W8_ATTACK_EFFECT_COUNT];
    /* 0x060: the item's missile bonus, summed across both weapons into the
       effect definition's value_1c. */
    unsigned char missile_magnitude;
    /* 0x061: the monster kind the weapon slays for an extra damage die,
       compared against W8MonsterRecord::kind by the character damage
       resolver; 0xff means the weapon slays nothing. */
    unsigned char slays_kind;
    signed char armor_class_bonus; /* 0x062 */
    unsigned char spell_id;        /* 0x063 */
    /* 0x064: the cast spell's power level - Assay prints "(Pwr %d)" and the
       use path consumes it as the casting power. */
    unsigned char spell_power;
    unsigned char unknown_065;
    W8ItemQuantityKind quantity_kind; /* 0x066 */
    W8Dice initial_quantity;     /* 0x067 */
    /* The stack merge path clamps quantity-kind 1 items to this byte. */
    unsigned char maximum_quantity; /* 0x06b */
    /* 0x06c..0x06e: three per-item modifier bytes the equipment fold adds to
       the derived block's own unknowns; 0x06f..0x074 are the six resistance
       bonuses it sums and clamps. */
    signed char health_regen_bonus;
    signed char stamina_regen_bonus;
    signed char spell_regen_bonus;
    signed char resistance_bonus[6]; /* 0x06f .. 0x074 */
    unsigned char property;          /* assay special-property label index */
    unsigned short profession_mask;  /* 0x076 */
    unsigned int race_mask;          /* 0x078 */
    /* 0x07c: one bit per sex; three admits either, and
       CanCharacterUseItem indexes it with the character's own field. */
    unsigned char gender_mask;
    ItemRequirement attribute_requirements[2]; /* 0x07d */
    ItemRequirement skill_requirements[2];     /* 0x081 */
    unsigned char identify_difficulty;           /* 0x085 */
    unsigned int value;                          /* 0x086 */
    unsigned short weight;                       /* 0x08a */
    unsigned char binds_on_equip;                /* 0x08c */
};
struct ItemEffects {
    /* 0x0ad: extra swings the weapon grants when the wielder starts an
       attack. */
    int swings_bonus;
    /* 0x0b1/0x0b3: the item's (index, value) modifier pairs the equipment
       fold adds to the derived block's two byte tables. 0xff is no pair. */
    signed char modifier_0b1_index;
    signed char modifier_0b1_value;
    signed char modifier_0b3_index;
    signed char modifier_0b3_value;
    unsigned char unknown_0b5[4]; /* 0x0b5 .. 0x0b8 */
    int merge_component_a;        /* 0x0b9: first item kind MergeItems combines into this one */
    int merge_component_b;        /* 0x0bd: the other kind; either order is accepted */
    /* 0x0c1: the item's material index; combat sound reads it on the struck
       item to pick the impact table's material column (0..11). */
    int material;
    /* 0x0c5: the weapon's attack sound class; combat sound bounds it against
       the 38-entry swing table and the 28 impact rows. */
    int weapon_sound_class;
    signed char merge_skill;         /* 0x0c9: skill required to create this item, -1 for none */
    unsigned char merge_skill_level; /* 0x0ca: level of merge_skill required */
    unsigned char editor_excluded;   /* hidden from the MIPE item list */
    /* 0x0cc: the missile table entry the item fires; the missile resolver
       bounds it against the missile table size. */
    signed char missile_type;
};
#pragma pack(pop)
static_assert(sizeof(ItemRequirement) == 2);
static_assert(sizeof(ItemProperties) == 0x51);
static_assert(sizeof(ItemEffects) == 0x20);
static_assert(std::is_trivially_copyable_v<ItemProperties>);
static_assert(std::is_trivially_copyable_v<ItemEffects>);
static_assert(offsetof(ItemProperties, value) == 0x4a);
static_assert(offsetof(ItemEffects, missile_type) == 0x1f);
} // namespace wiz8::retail
using W8ItemRequirement = wiz8::retail::ItemRequirement;

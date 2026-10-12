#ifndef WIZ8_GAMEPLAY_MODIFIERS_H
#define WIZ8_GAMEPLAY_MODIFIERS_H

#include <cstdint>
#include <stddef.h>

/*
 * The effect-slot and modifier records the gameplay-modifier passes accumulate through.
 *
 * A W8EffectSlot is one running effect or condition: the party keeps twelve in
 * the status block, a monster keeps twelve more, and combat keeps nine records
 * at +0x7c1. The six 0x11-byte records at combat +0x85a share storage with
 * later independently typed fields; their originating array bound is
 * unresolved. A W8GameplayModifierBlock is the 0x67-byte
 * accumulator those slots and the worn equipment fold into, one block per
 * character plus the party-wide block in the status record. Monsters keep
 * the same record at W8MonsterInfo::modifiers.
 */

#pragma pack(push, 1)

/* One 0x11-byte effect slot. The id at +0x01 is both the condition id the
   HasCondition predicates compare and the index into the effect-visual table
   the clear path drops. ApplyPartyEffectSlots scales the amount at +0x05 by
   the percentage at +0x09 before accumulating it. */
struct W8EffectSlot {
    /* Every arming and clearing site stores active, effect_id, amount and
       duration in this order and leaves percent untouched. */
    void Activate(std::int32_t effect, std::int32_t magnitude, std::uint32_t lifetime)
    {
        active = true;
        effect_id = effect;
        amount = magnitude;
        duration = lifetime;
    }
    void Reset()
    {
        active = false;
        effect_id = 0;
        amount = 0;
        duration = 0;
    }

    bool active;           /* 0x00 */
    std::int32_t effect_id;         /* 0x01 */
    std::int32_t amount;            /* 0x05: dword magnitude - ApplyCombatEffectSlot and the
                             0x3e detonation pass read and write it as one dword */
    std::uint32_t percent;  /* 0x09 */
    std::uint32_t duration; /* 0x0d: remaining lifetime, aged down in whole
                                  minutes by AgeMonsterSight */
}; /* 0x11 */
#pragma pack(pop)

static_assert(sizeof(W8EffectSlot) == 0x11, "W8EffectSlot_must_be_0x11");
static_assert(offsetof(W8EffectSlot, active) == 0x00, "W8EffectSlot_active");
static_assert(offsetof(W8EffectSlot, effect_id) == 0x01, "W8EffectSlot_effect_id");
static_assert(offsetof(W8EffectSlot, amount) == 0x05, "W8EffectSlot_amount");
static_assert(offsetof(W8EffectSlot, percent) == 0x09, "W8EffectSlot_percent");
static_assert(offsetof(W8EffectSlot, duration) == 0x0d, "W8EffectSlot_duration");

/* The 0x67-byte modifier accumulator. Its byte runs are fixed by the fold at
   0x0050F090: bytes 0x00..0x0b, 0x0c..0x12, 0x13..0x3b and 0x3c..0x41 are
   added, bytes 0x42..0x46 and 0x4a are boolean-OR'd, and bytes 0x47..0x49
   keep the larger value. Only the offsets another recovered pass names are
   labelled; the rest stay runs. */
struct W8GameplayModifierBlock {
    std::int8_t damage_bonus;   /* 0x00: added to initiative and to displayed hand damage */
    std::int8_t hit_bonus;      /* 0x01: added to the hand attack hit bonus */
    std::int8_t attack_bonus;   /* 0x02: added to the hand attack score term */
    std::int8_t damage_percent; /* 0x03: added to the hand attack damage bonus */
    std::int8_t armor_flat;     /* 0x04 */
    std::int8_t armor_matchup;  /* 0x05 */
    std::int8_t damage_reduction_adjustment; /* 0x06: added to damage reduction */
    std::int8_t resistance_bonus_all;        /* 0x07: added to every resistance */
    /* 0x08: flat damage applied once per elapsed minute. The character and
       monster aging paths both multiply it by the elapsed minute count. */
    std::uint8_t damage_per_minute;
    /* 0x09..0x0b: signed flat adjustments to the corresponding regeneration
       channels. Character aging applies them directly; monster regen rebuild
       folds the HP/stamina values into its rates. */
    std::int8_t health_regen_adjustment;
    std::int8_t stamina_regen_adjustment;
    std::int8_t spell_regen_adjustment;
    std::int8_t attribute_adjustments[7]; /* 0x0c .. 0x12 */
    std::int8_t skill_bonus[0x29];        /* 0x13 .. 0x3b: one per skill id */
    std::int8_t resistance_bonus[6];      /* 0x3c .. 0x41 */
    /* 0x42..0x44: independent +50% regeneration-rate latches. */
    std::uint8_t boost_health_regen;
    std::uint8_t boost_stamina_regen;
    std::uint8_t boost_spell_regen;
    std::uint8_t out_of_formation; /* 0x45 */
    /* 0x46: set by effect id 0x11; the secret-search pass treats every
       slot as a finder while it holds. */
    std::uint8_t detect_secrets;
    std::uint8_t light; /* 0x47: the doubled light value the sky node reads */
    std::uint8_t value; /* 0x48: max-combined, effect id 0x21 */
    /* 0x49: missile deflection chance in percent; Missile.cpp compares it
       against Random(100)+1. Max-combined, effect id 0x1a. */
    std::uint8_t missile_deflect_chance;
    /* 0x4a: set by effect id 0x2d; feeds ComputeSightThreshold's
       sight_override and gates the party-surprise pass. */
    std::uint8_t sight_override;
    /* 0x4b: armor-class adjustment; added to character component 8 and the
       monster target-armor modifier, reduced by conditions. */
    std::int8_t armor_class_adjustment;
    std::uint8_t unknown_4c[0x1b]; /* 0x4c .. 0x66 */
}; /* 0x67 */

/* The spell that fills each being effect slot; the monster side indexes
   W8MonsterInfo::effect_slots, the party side
   W8GameStatus::effect_slots. */
extern std::int32_t g_being_effect_slot_spells[12];
/* The combat-state spell per effect slot, walked against
   W8CombatState::effect_slots and the monster's combat_effects. */
extern std::int32_t g_combat_effect_slot_spells[9];
/* The same mapping for the second combat effect block, indexed against
   W8CombatState::effect_slots0 and the monster's combat_effects_2. */
extern std::int32_t g_combat_effect_slot_spells_and_cast_success[23];

#endif

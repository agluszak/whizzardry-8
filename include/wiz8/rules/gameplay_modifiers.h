#ifndef WIZ8_GAMEPLAY_MODIFIERS_H
#define WIZ8_GAMEPLAY_MODIFIERS_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <stdexcept>

#include "wiz8/data/spell_ids.h"
#include "wiz8/rules/random.h"

/*
 * The effect-slot and modifier records the gameplay-modifier passes accumulate through.
 *
 * A W8EffectSlot is one running effect or condition: the party keeps twelve in
 * the status block, a monster keeps twelve more, and combat keeps nine timed
 * effects and six radius effects in separate arrays. A W8GameplayModifierBlock is the 0x67-byte
 * accumulator those slots and the worn equipment fold into, one block per
 * character plus the party-wide block in the status record. Monsters keep
 * the same record at W8MonsterInfo::modifiers.
 */

/* One runtime effect slot. The packed retail record is 0x11 bytes. The id at +0x01 is both the condition id the
   HasCondition predicates compare and the index into the effect-visual table
   the clear path drops. ApplyPartyEffectSlots scales the amount at +0x05 by
   the percentage at +0x09 before accumulating it. */
struct W8EffectSlot {
    /* Every arming and clearing site stores active, effect_id, amount and
       duration in this order and leaves percent untouched. */
    void Activate(int effect, int magnitude, unsigned int lifetime)
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
    int effect_id;         /* 0x01 */
    int amount;            /* 0x05: dword magnitude - ApplyCombatEffectSlot and the
                             0x3e detonation pass read and write it as one dword */
    unsigned int percent;  /* 0x09 */
    unsigned int duration; /* 0x0d: remaining lifetime, aged down in whole
                                  minutes by AgeMonsterSight */

    bool operator==(const W8EffectSlot&) const = default;
}; /* 0x11 */

/* The 0x67-byte modifier accumulator. Its byte runs are fixed by the fold at
   0x0050F090: bytes 0x00..0x0b, 0x0c..0x12, 0x13..0x3b and 0x3c..0x41 are
   added, bytes 0x42..0x46 and 0x4a are boolean-OR'd, and bytes 0x47..0x49
   keep the larger value. Only the offsets another recovered pass names are
   labelled; the rest stay runs. */
struct W8GameplayModifierBlock {
    signed char damage_bonus;   /* 0x00: added to initiative and to displayed hand damage */
    signed char hit_bonus;      /* 0x01: added to the hand attack hit bonus */
    signed char attack_bonus;   /* 0x02: added to the hand attack score term */
    signed char damage_percent; /* 0x03: added to the hand attack damage bonus */
    signed char armor_flat;     /* 0x04 */
    signed char armor_matchup;  /* 0x05 */
    signed char damage_reduction_adjustment; /* 0x06: added to damage reduction */
    signed char resistance_bonus_all;        /* 0x07: added to every resistance */
    /* 0x08: flat damage applied once per elapsed minute. The character and
       monster aging paths both multiply it by the elapsed minute count. */
    unsigned char damage_per_minute;
    /* 0x09..0x0b: signed flat adjustments to the corresponding regeneration
       channels. Character aging applies them directly; monster regen rebuild
       folds the HP/stamina values into its rates. */
    signed char health_regen_adjustment;
    signed char stamina_regen_adjustment;
    signed char spell_regen_adjustment;
    signed char attribute_adjustments[7]; /* 0x0c .. 0x12 */
    signed char skill_bonus[0x29];        /* 0x13 .. 0x3b: one per skill id */
    signed char resistance_bonus[6];      /* 0x3c .. 0x41 */
    /* 0x42..0x44: independent +50% regeneration-rate latches. */
    unsigned char boost_health_regen;
    unsigned char boost_stamina_regen;
    unsigned char boost_spell_regen;
    unsigned char out_of_formation; /* 0x45 */
    /* 0x46: set by effect id 0x11; the secret-search pass treats every
       slot as a finder while it holds. */
    unsigned char detect_secrets;
    unsigned char light; /* 0x47: the doubled light value the sky node reads */
    unsigned char value; /* 0x48: max-combined, effect id 0x21 */
    /* 0x49: missile deflection chance in percent; Missile.cpp compares it
       against Random(100)+1. Max-combined, effect id 0x1a. */
    unsigned char missile_deflect_chance;
    /* 0x4a: set by effect id 0x2d; feeds ComputeSightThreshold's
       sight_override and gates the party-surprise pass. */
    unsigned char sight_override;
    /* 0x4b: armor-class adjustment; added to character component 8 and the
       monster target-armor modifier, reduced by conditions. */
    signed char armor_class_adjustment;
    unsigned char unknown_4c[0x1b]; /* 0x4c .. 0x66 */
}; /* 0x67 */

// Each mapping has the same bound as its owning effect block. Casting costs
// are independent data, never a continuation or end marker of a slot mapping.
inline constexpr std::array<W8SpellId, 12> g_being_effect_slot_spells{
    W8_SPELL_ARMORPLATE, W8_SPELL_CHAMELEON,    W8_SPELL_DETECT_SECRETS, W8_SPELL_ENCHANTED_BLADE,
    W8_SPELL_LIGHT,      W8_SPELL_MAGIC_SCREEN, W8_SPELL_MISSILE_SHIELD, W8_SPELL_SHADOW_HOUND,
    W8_SPELL_X_RAY,      W8_SPELL_NONE,         W8_SPELL_NONE,           W8_SPELL_NONE};
inline constexpr std::array<W8SpellId, 9> g_combat_effect_slot_spells{
    W8_SPELL_ARMORMELT, W8_SPELL_ACID_BOMB,   W8_SPELL_TOXIC_CLOUD,
    W8_SPELL_FIRESTORM, W8_SPELL_DEATH_CLOUD, W8_SPELL_DRAINING_CLOUD,
    W8_SPELL_NONE,      W8_SPELL_NONE,        W8_SPELL_NONE};
inline constexpr std::array<W8SpellId, 6> g_defense_effect_slot_spells{
    W8_SPELL_BLESS,        W8_SPELL_ELEMENT_SHIELD, W8_SPELL_SOUL_SHIELD,
    W8_SPELL_RING_OF_FIRE, W8_SPELL_NONE,           W8_SPELL_NONE};
inline constexpr std::array<int, 17> kSpellCastCosts{30, 40, 50, 58,  64,  70,  76,  81, 86,
                                                     90, 94, 97, 100, 102, 105, 107, 110};

inline std::size_t EffectSlotForSpell(std::span<const W8SpellId> mapping, int spell)
{
    const auto slot = std::ranges::find(mapping, spell);
    if (slot == mapping.end()) throw std::invalid_argument("spell has no effect slot");
    return slot - mapping.begin();
}

inline constexpr unsigned int W8_SPELL_POWER_MAX = 7;

inline unsigned int RollMonsterSpellPower(Rng& rng, unsigned int budget, int cost_band)
{
    const auto cost_scale = kSpellCastCosts.at(std::min(cost_band, 16));
    unsigned int power = 1;
    for (; power <= W8_SPELL_POWER_MAX; ++power) {
        const auto cost = (cost_scale * power) / 7;
        // Here budget < cost <= 110, so the old overflow/sign branches were unreachable.
        if (cost > budget && (cost - budget) * 70 / cost >= 9) break;
    }
    if (power > 1) --power;
    switch (rng.below(4)) {
    case 0:
        if (power > 2) --power;
        break;
    case 1:
        if (power < W8_SPELL_POWER_MAX) ++power;
        break;
    }
    return power;
}

// Raise a byte by `percent` percent, rounding to the nearest unit.
inline void AdjustByteByPercent(unsigned char* value, unsigned int percent)
{
    *value = static_cast<unsigned char>(((percent + 100) * *value + 50) / 100);
}

void ApplyPartyEffectSlots(std::span<const W8EffectSlot> source, W8GameplayModifierBlock* target);
void ApplyCombatEffectSlots(std::span<const W8EffectSlot> source, W8GameplayModifierBlock* target);

#endif

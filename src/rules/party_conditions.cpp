#include "wiz8/rules/party_conditions.h"

#include <optional>

#include "wiz8/data/game_data.h"
#include "wiz8/data/skills.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/character_derivation.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/inventory.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/random.h"

namespace {
// The cheat-death revival leaves a member unconscious for at least this long.
constexpr unsigned int kCheatDeathUnconsciousLimit = 7;

// The member with the lowest mental resistance, who carries the party's madness ring.
std::optional<unsigned int> LowestMentalResistanceSlot(const W8Party& party)
{
    unsigned int lowest = 999;
    std::optional<unsigned int> selected;
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        const auto& character = party.characters[slot];
        if (party.members[slot].has_value() && character.highest_condition < W8_CONDITION_DEAD &&
            character.resistances[4].total < lowest) {
            selected = slot;
            lowest = character.resistances[4].total;
        }
    }
    return selected;
}
} // namespace

int ConditionDuration(W8Condition condition, int duration)
{
    switch (condition) {
    case W8_CONDITION_DISEASED:
    case W8_CONDITION_INFATUATED:
    case W8_CONDITION_DEAD:
    case W8_CONDITION_MISSING:
        return W8_CONDITION_INDEFINITE;
    default:
        return duration;
    }
}

ConditionResistance ResistCondition(const W8Character& character, W8Condition condition,
                                    int duration)
{
    switch (condition) {
    case W8_CONDITION_AFRAID:
        if (CharacterHasTrait(&character, W8_TRAIT_FEARLESS)) return ConditionResistance::Fearless;
        [[fallthrough]];
    case W8_CONDITION_DISEASED:
    case W8_CONDITION_IRRITATED:
    case W8_CONDITION_NAUSEATED:
    case W8_CONDITION_POISONED:
    case W8_CONDITION_ASLEEP:
        if (CharacterHasTrait(&character, static_cast<W8Trait>(0x1e)))
            return ConditionResistance::Silent;
        break;
    case W8_CONDITION_INSANE:
        if (duration == W8_CONDITION_INDEFINITE) break;
        [[fallthrough]];
    case W8_CONDITION_TURNCOAT:
        if (CharacterHasTrait(&character, W8_TRAIT_MENTAL_CONDITION_IMMUNITY))
            return ConditionResistance::MentallyImmune;
        break;
    default:
        break;
    }
    return ConditionResistance::None;
}

bool InfatuationHoldsHex(const W8Character& character, int level_band)
{
    return character.uiCondition[W8_CONDITION_INFATUATED] != 0 && level_band != 9 &&
           level_band != 10;
}

bool MadnessRingHoldsInsanity(const W8Party& party, W8ItemLocation found, unsigned int slot)
{
    if (found.storage == W8ItemStorage::None || found.storage == W8ItemStorage::Hand) return false;
    const auto bearer = found.storage == W8ItemStorage::Pool ? LowestMentalResistanceSlot(party)
                                                             : std::optional{found.character};
    return bearer == slot && !FindCharacterItem(party.characters[slot],
                                                ItemWithId(kItemMadnessCharm), W8ItemReach::Worn);
}

W8Condition HighestCondition(const W8Character& character)
{
    for (int index = W8_CONDITION_COUNT - 1; index > 0; --index)
        if (character.uiCondition[index] != 0) return static_cast<W8Condition>(index);
    return W8_CONDITION_NONE;
}

W8EnchantmentSlot TopEnchantment(const W8Character& character)
{
    for (int scan = 7; scan > 0; --scan)
        if (character.enchantments[scan].turns > 0) return static_cast<W8EnchantmentSlot>(scan);
    return W8_ENCHANTMENT_NONE;
}

int AgedPoisonStrength(Rng& rng, int strength, unsigned int remaining, unsigned int minutes)
{
    // A roll covers a share that floors to zero.
    unsigned int lost = strength * minutes / remaining;
    if (lost == 0 && rng.below(100) < strength * minutes * 100 / remaining) lost = 1;
    return strength - lost;
}

bool CheatsDeath(const W8Character& character)
{
    return CharacterHasTrait(&character, W8_TRAIT_CHEAT_DEATH) &&
           character.uiCondition[W8_CONDITION_UNCONSCIOUS] < kCheatDeathUnconsciousLimit;
}

int CheatDeathUnconsciousTurns(const W8Character& character)
{
    // An unconscious stint shortened by the profession-level scale.
    return static_cast<int>(character.uiCondition[W8_CONDITION_UNCONSCIOUS]) -
           static_cast<int>(ScaleValueByProfessionLevel(&character, W8_TRAIT_CHEAT_DEATH, 5.0f)) +
           static_cast<int>(kCheatDeathUnconsciousLimit);
}

unsigned int CheatDeathHitPoints(Rng& rng, const W8Character& character, bool first_in_fight)
{
    if (first_in_fight) {
        return (rng.below(static_cast<unsigned int>(ScaleValueByProfessionLevel(
                    &character, W8_TRAIT_CHEAT_DEATH,
                    static_cast<unsigned int>(character.uiHPMax) * 0.5f))) +
                0x32) *
               character.uiHPMax / 100;
    }
    return rng.below(static_cast<unsigned int>(ScaleValueByProfessionLevel(
               &character, W8_TRAIT_CHEAT_DEATH, character.hp_current))) +
           1;
}

bool StrengthenEnchantment(Rng& rng, const GameData& data, W8Character& character,
                           W8EnchantmentSlot enchantment, int power, unsigned int turns,
                           unsigned int percent)
{
    auto& entry = character.enchantments[enchantment];
    if (entry.power >= static_cast<unsigned int>(power)) return false;
    entry.power = power;
    entry.percent = static_cast<unsigned short>(percent);
    entry.turns = turns;
    if (enchantment == W8_ENCHANTMENT_GUARDIAN_ANGEL) {
        entry.magnitude = static_cast<short>(
            RollDice(rng, data.spells[W8_SPELL_GUARDIAN_ANGEL].effect_dice) * power);
        entry.magnitude =
            static_cast<short>((static_cast<unsigned int>(entry.magnitude) * percent) / 100) +
            entry.magnitude;
    }
    character.enchantment_top = TopEnchantment(character);
    return true;
}

bool Swallowed(const W8Character& character)
{
    return character.uiCondition[W8_CONDITION_MISSING] != 0 &&
           character.conditions[W8_DEPENDENCE_SWALLOWED].active;
}

W8GameplayModifierBlock ConditionModifiers(const W8Character& character, bool npc_bound,
                                           std::optional<int> swallower_level)
{
    W8GameplayModifierBlock block{};
    ApplyConditionModifiers(&character, character.uiCondition, character.condition_argument,
                            &block);
    // A swallowed member's regeneration suffers by the swallower's level.
    if (Swallowed(character))
        block.health_regen_adjustment += swallower_level ? -1 - (*swallower_level >> 1) : -5;
    ApplyEnchantmentModifiers(character.enchantments, &block);
    // A member whose NPC is bound loses twenty from every attribute and skill.
    if (npc_bound) {
        for (auto& adjustment : block.attribute_adjustments) adjustment -= 0x14;
        for (auto& bonus : block.skill_bonus) bonus -= 0x14;
    }
    return block;
}

/* Fold the live conditions into the modifier block: each running condition id
   carries its own fixed penalty set, condition seven also adds the carried
   argument (poison strength) to the block's regen channel. */
void ApplyConditionModifiers(const W8Character* character, const unsigned int* condition_turns,
                             int condition_argument, W8GameplayModifierBlock* target)
{
    unsigned int i;

    for (unsigned int index = 0; index < W8_CONDITION_COUNT; ++index) {
        if (condition_turns[index] == 0) {
            continue;
        }
        switch (index) {
        case 3:
            target->hit_bonus -= 2;
            target->armor_class_adjustment -= 2;
            break;
        case 4:
            target->hit_bonus -= 5;
            target->armor_class_adjustment -= 4;
            break;
        case 5:
            target->attribute_adjustments[W8_ATTRIBUTE_SPEED] -= 0x32;
            break;
        case 6:
            target->hit_bonus -= 3;
            target->armor_class_adjustment -= 2;
            break;
        case W8_CONDITION_POISONED:
            target->hit_bonus -= 2;
            target->armor_class_adjustment -= 2;
            target->damage_per_minute += condition_argument;
            break;
        case 9:
            target->hit_bonus -= 5;
            for (i = 0; i < 7; ++i) {
                target->attribute_adjustments[i] -= 0x14;
            }
            for (i = 0; i < 0x29; ++i) {
                target->skill_bonus[i] -= 0x14;
            }
            break;
        case 0xb:
            target->attribute_adjustments[W8_ATTRIBUTE_INTELLIGENCE] -= 0x32;
            break;
        case 0xc:
            if (character == 0 || !CharacterHasTrait(character, W8_TRAIT_EFFECTIVE_WHILE_BLIND)) {
                target->attribute_adjustments[W8_ATTRIBUTE_SENSES] -= 0x32;
                target->out_of_formation = 1;
            } else {
                target->attribute_adjustments[W8_ATTRIBUTE_SENSES] +=
                    static_cast<signed char>(ScaleValueByProfessionLevel(
                        character, W8_TRAIT_EFFECTIVE_WHILE_BLIND, 50.0f)) -
                    0x32;
            }
            break;
        case 0xe:
            target->attribute_adjustments[W8_ATTRIBUTE_DEXTERITY] -= 0x32;
            [[fallthrough]];
        case W8_CONDITION_ASLEEP:
        case 0x10:
        case W8_CONDITION_UNCONSCIOUS:
            target->out_of_formation = 1;
            break;
        }
    }
}

/* Fold the enchantment slots into the modifier block: slot five raises one
   attribute by ten per power, slot six raises them all by five per power and
   slot seven raises the damage-reduction and armor bytes by eight and one per
   power, each scaled by the slot's percentage. */
void ApplyEnchantmentModifiers(const W8Enchantment* enchantments, W8GameplayModifierBlock* target)
{
    unsigned char amount;
    unsigned int i;

    for (unsigned int index = 0; index < 8; ++index) {
        const W8Enchantment* slot = &enchantments[index];
        if (slot->turns == 0) {
            continue;
        }
        switch (index) {
        case 5:
            amount = static_cast<unsigned char>(slot->power * 10);
            AdjustByteByPercent(&amount, slot->percent);
            target->attribute_adjustments[W8_ATTRIBUTE_SPEED] += amount;
            break;
        case 6:
            amount = static_cast<unsigned char>(slot->power * 5);
            AdjustByteByPercent(&amount, slot->percent);
            for (i = 0; i < 7; ++i) {
                target->attribute_adjustments[i] += amount;
            }
            break;
        case 7:
            amount = static_cast<unsigned char>(slot->power << 3);
            AdjustByteByPercent(&amount, slot->percent);
            target->damage_reduction_adjustment += amount;
            amount = static_cast<unsigned char>(slot->power);
            AdjustByteByPercent(&amount, slot->percent);
            target->armor_matchup += amount;
            break;
        }
    }
}

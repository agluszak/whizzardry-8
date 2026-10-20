#include "wiz8/rules/party_conditions.h"

#include <bitset>
#include <cassert>
#include <optional>

#include <fmt/format.h>
#include <fmt/printf.h>

#include "wiz8/data/game_data.h"
#include "wiz8/data/skills.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/character_derivation.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/inventory.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/party_stats.h"
#include "wiz8/rules/random.h"
#include "wiz8/rules/rule_context.h"
#include "wiz8/rules/targeting.h"

namespace {
constexpr int kItemMadnessRing = 0x243;
constexpr int kItemMadnessCharm = 0x239;
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

/* The madness ring keeps its bearer insane: carried by a member, or, from the pool, by
   whoever resists least, unless the bearer wears its counter-charm. */
bool MadnessRingHoldsInsanity(const W8Party& party, unsigned int slot)
{
    const auto found = FindPartyItem(party, ItemWithId(kItemMadnessRing), W8ItemReach::Party);
    if (found.storage == W8ItemStorage::None || found.storage == W8ItemStorage::Hand) return false;
    const auto bearer = found.storage == W8ItemStorage::Pool ? LowestMentalResistanceSlot(party)
                                                             : std::optional{found.character};
    return bearer == slot && !FindCharacterItem(party.characters[slot],
                                                ItemWithId(kItemMadnessCharm), W8ItemReach::Worn);
}

void PushSlotDisplayChanged(Events& events, unsigned int slot)
{
    events.push(PartySlotChanged{slot});
    events.push(CharacterActionChanged{});
    events.push(PartyPanelChanged{});
}

/* Rescan the member's condition countdowns from the top for its highest condition and
   let the game follow it. */
void RecomputeHighestCondition(RuleContext& rules, PartyHost& host, unsigned int slot)
{
    auto& character = rules.party.characters[slot];
    character.highest_condition = W8_CONDITION_NONE;
    for (int index = W8_CONDITION_COUNT - 1; index > 0; --index) {
        if (character.uiCondition[index] != 0) {
            character.highest_condition = static_cast<W8Condition>(index);
            break;
        }
    }
    host.standingChanged(slot);
    PushSlotDisplayChanged(rules.events, slot);
}

void RecomputeEnchantmentTop(W8Character& character)
{
    character.enchantment_top = W8_ENCHANTMENT_NONE;
    for (int scan = 7; scan > 0; --scan) {
        if (character.enchantments[scan].turns > 0) {
            character.enchantment_top = static_cast<W8EnchantmentSlot>(scan);
            break;
        }
    }
}

// Rebuild the member's condition block and settle the party.
void Resettle(RuleContext& rules, PartyHost& host, unsigned int slot)
{
    RebuildConditionModifiers(rules, host, slot);
    SettleParty(rules, host);
}

/* Clear the condition and what it alone maintained; true when it was lifted. A dying
   member's conditions are cleared without settling each time. */
bool ClearCondition(RuleContext& rules, PartyHost& host, unsigned int slot, W8Condition condition,
                    bool announce)
{
    auto& party = rules.party;
    auto& character = party.characters[slot];
    if (character.uiCondition[condition] == 0 && host.worldSuspended()) return false;
    assert(party.members[slot].has_value());
    assert(character.uiCondition[condition] != 0);
    if (condition == W8_CONDITION_HEXED) {
        const int band = host.levelBand();
        if (character.uiCondition[W8_CONDITION_INFATUATED] != 0 && band != 9 && band != 10)
            return false;
    } else if (condition == W8_CONDITION_INSANE && MadnessRingHoldsInsanity(party, slot)) {
        return false;
    }
    if (announce) {
        rules.events.push(CharacterNotice{
            slot, fmt::sprintf(rules.data.strings[0x243],
                               rules.data.strings[kConditionNotices[condition].name])});
    }
    character.uiCondition[condition] = 0;
    RecomputeHighestCondition(rules, host, slot);
    switch (condition) {
    case W8_CONDITION_POISONED:
        character.condition_argument = 0;
        break;
    case W8_CONDITION_DRAINED:
        character.hp_adjustment = 0;
        character.fatigue_penalty = 0;
        break;
    default:
        break;
    }
    host.conditionLifted(slot, condition);
    return true;
}

// Death's own consequences, after the death condition took hold.
void Die(RuleContext& rules, PartyHost& host, unsigned int slot)
{
    auto& character = rules.party.characters[slot];
    assert(rules.party.members[slot].has_value());
    ++character.death_count;
    bool lifted = false;
    for (unsigned int condition = 0; condition < W8_CONDITION_CLEARABLE_COUNT; ++condition) {
        if (condition != W8_CONDITION_INFATUATED && character.uiCondition[condition] != 0 &&
            ClearCondition(rules, host, slot, static_cast<W8Condition>(condition), false)) {
            host.recoveryReaction(slot, static_cast<W8Condition>(condition));
            lifted = true;
        }
    }
    if (lifted) Resettle(rules, host, slot);
    character.hp_current = 0;
    character.stamina = 0;
    auto& row = rules.party.rows[slot];
    row.executing.target = W8CombatSlot{W8_TARGET_KIND_NONE, -1, -1, -1, {}, {}};
    row.committed.target = W8CombatSlot{W8_TARGET_KIND_NONE, -1, -1, -1, {}, {}};
    rules.events.push(CharacterDied{slot});
    host.died(slot);
}
} // namespace

void SettleParty(RuleContext& rules, PartyHost& host,
                 std::bitset<W8_PARTY_SLOT_COUNT> changed_hands)
{
    auto& party = rules.party;
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (party.members[slot].has_value() && IsCharacterDefined(party.characters[slot]) &&
            host.unequipUnusable(slot))
            changed_hands.set(slot);
    const auto stats = UpdatePartyStats(rules.data, party, host.statRules());
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        if (!party.members[slot].has_value()) continue;
        const auto& character = party.characters[slot];
        if (changed_hands[slot] && character.fInParty && rules.game_started) {
            host.handsChanged(slot);
            rules.events.push(PartySlotChanged{slot});
        }
        if (stats.characters[slot].became_overloaded) host.overloaded(slot);
        if (character.fInParty) rules.events.push(PartySlotChanged{slot});
    }
    if (stats.load_redistributed) rules.events.push(PartyLoadChanged{});
    // Death is a condition change with consequences of its own.
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (stats.characters[slot].hp_exhausted)
            InflictCondition(rules, host, slot,
                             {.condition = W8_CONDITION_DEAD, .duration = W8_CONDITION_INDEFINITE});
}

bool CheatsDeath(const W8Character& character)
{
    return CharacterHasTrait(&character, W8_TRAIT_CHEAT_DEATH) &&
           character.uiCondition[W8_CONDITION_UNCONSCIOUS] < kCheatDeathUnconsciousLimit;
}

/* A notice, an unconscious stint shortened by the profession-level scale, and hit points
   rolled back up: from a scaled share of the maximum once per fight, from a scaled share
   of the current pool every other time. */
void ReviveByCheatingDeath(RuleContext& rules, PartyHost& host, unsigned int slot)
{
    auto& character = rules.party.characters[slot];
    rules.events.push(CharacterNotice{slot, rules.data.strings[0x173]});
    InflictCondition(
        rules, host, slot,
        {.condition = W8_CONDITION_UNCONSCIOUS,
         .duration =
             static_cast<int>(character.uiCondition[W8_CONDITION_UNCONSCIOUS]) -
             static_cast<int>(ScaleValueByProfessionLevel(&character, W8_TRAIT_CHEAT_DEATH, 5.0f)) +
             static_cast<int>(kCheatDeathUnconsciousLimit)});
    if (host.spendFirstCombatRevival(slot)) {
        character.hp_current =
            (rules.rng.below(static_cast<unsigned int>(ScaleValueByProfessionLevel(
                 &character, W8_TRAIT_CHEAT_DEATH,
                 static_cast<unsigned int>(character.uiHPMax) * 0.5f))) +
             0x32) *
            character.uiHPMax / 100;
    } else {
        character.hp_current =
            rules.rng.below(static_cast<unsigned int>(ScaleValueByProfessionLevel(
                &character, W8_TRAIT_CHEAT_DEATH, character.hp_current))) +
            1;
    }
}

bool InflictCondition(RuleContext& rules, PartyHost& host, unsigned int slot,
                      const ConditionInfliction& infliction)
{
    auto& character = rules.party.characters[slot];
    const auto condition = infliction.condition;
    int duration = infliction.duration;
    assert(rules.party.members[slot].has_value());
    assert(infliction.poison_strength == 0 || condition == W8_CONDITION_POISONED);
    if (condition == W8_CONDITION_POISONED && infliction.poison_strength == 0) return false;
    if (condition == W8_CONDITION_DEAD && CheatsDeath(character)) {
        ReviveByCheatingDeath(rules, host, slot);
        return false;
    }
    switch (condition) {
    case W8_CONDITION_DISEASED:
    case W8_CONDITION_INFATUATED:
    case W8_CONDITION_DEAD:
    case W8_CONDITION_MISSING:
        duration = W8_CONDITION_INDEFINITE;
        break;
    default:
        if (duration == 0) return false;
        break;
    }
    const auto refuse = [&](std::string text) {
        rules.events.push(CharacterNotice{slot, std::move(text)});
        return false;
    };
    if (host.worldSuspended()) {
        return refuse(fmt::sprintf(rules.data.strings[0x242],
                                   rules.data.strings[kConditionNotices[condition].name]));
    }
    switch (condition) {
    case W8_CONDITION_AFRAID:
        if (CharacterHasTrait(&character, W8_TRAIT_FEARLESS))
            return refuse(rules.data.strings[0x180]);
        [[fallthrough]];
    case W8_CONDITION_DISEASED:
    case W8_CONDITION_IRRITATED:
    case W8_CONDITION_NAUSEATED:
    case W8_CONDITION_POISONED:
    case W8_CONDITION_ASLEEP:
        if (CharacterHasTrait(&character, static_cast<W8Trait>(0x1e))) return false;
        break;
    case W8_CONDITION_INSANE:
        if (duration == W8_CONDITION_INDEFINITE) break;
        [[fallthrough]];
    case W8_CONDITION_TURNCOAT:
        if (CharacterHasTrait(&character, W8_TRAIT_MENTAL_CONDITION_IMMUNITY))
            return refuse(rules.data.strings[0x181]);
        break;
    default:
        break;
    }
    const auto old_highest = character.highest_condition;
    const unsigned int old_duration = character.uiCondition[condition];
    const bool took_hold = old_duration < static_cast<unsigned int>(duration);
    if (took_hold) {
        if (old_duration == 0) host.conditionStarted(slot, condition);
        character.uiCondition[condition] = duration;
        RecomputeHighestCondition(rules, host, slot);
    }
    if (static_cast<unsigned int>(character.condition_argument) <
        static_cast<unsigned int>(infliction.poison_strength)) {
        character.condition_argument = infliction.poison_strength;
    }
    Resettle(rules, host, slot);
    if (!took_hold) return false;
    if (condition == W8_CONDITION_DEAD) Die(rules, host, slot);
    if (condition >= W8_CONDITION_DEAD) host.incapacitated(slot);
    if (old_highest != character.highest_condition) host.conditionReaction(slot);
    if (infliction.announce) {
        if (condition == W8_CONDITION_MISSING && infliction.alternate_missing_notice) {
            rules.events.push(CharacterNotice{slot, rules.data.strings[0x1d5]});
        } else {
            rules.events.push(CharacterNotice{
                slot,
                fmt::format("{}!", rules.data.strings[kConditionNotices[condition].singular])});
        }
    }
    return true;
}

void LiftCondition(RuleContext& rules, PartyHost& host, unsigned int slot, W8Condition condition,
                   bool announce)
{
    if (!ClearCondition(rules, host, slot, condition, announce)) return;
    Resettle(rules, host, slot);
    host.recoveryReaction(slot, condition);
}

void AgeCondition(RuleContext& rules, PartyHost& host, unsigned int slot, W8Condition condition,
                  unsigned int minutes)
{
    auto& character = rules.party.characters[slot];
    assert(rules.party.members[slot].has_value());
    assert(character.uiCondition[condition] != 0);
    if (character.uiCondition[condition] <= minutes) {
        LiftCondition(rules, host, slot, condition, true);
        return;
    }
    if (condition == W8_CONDITION_POISONED) {
        // Poison bleeds its strength pro rata; a roll covers a share that floors to zero.
        const int strength = character.condition_argument;
        const unsigned int remaining = character.uiCondition[W8_CONDITION_POISONED];
        unsigned int lost = strength * minutes / remaining;
        if (lost == 0 && rules.rng.below(100) < strength * minutes * 100 / remaining) lost = 1;
        character.condition_argument = strength - lost;
    }
    character.uiCondition[condition] -= minutes;
}

void LiftConditionFromParty(RuleContext& rules, PartyHost& host, W8Condition condition)
{
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (rules.party.members[slot].has_value() &&
            rules.party.characters[slot].uiCondition[condition] != 0)
            LiftCondition(rules, host, slot, condition, true);
}

void LiftAllConditionsFromParty(RuleContext& rules, PartyHost& host)
{
    for (unsigned int condition = 0; condition < W8_CONDITION_CLEARABLE_COUNT; ++condition)
        if (condition != W8_CONDITION_INFATUATED)
            LiftConditionFromParty(rules, host, static_cast<W8Condition>(condition));
}

void Enchant(RuleContext& rules, PartyHost& host, unsigned int slot, W8EnchantmentSlot enchantment,
             int power, unsigned int turns, unsigned int percent)
{
    auto& character = rules.party.characters[slot];
    auto& entry = character.enchantments[enchantment];
    if (entry.power < static_cast<unsigned int>(power)) {
        entry.power = power;
        entry.percent = static_cast<unsigned short>(percent);
        entry.turns = turns;
        if (enchantment == W8_ENCHANTMENT_GUARDIAN_ANGEL) {
            entry.magnitude = static_cast<short>(
                RollDice(rules.rng, rules.data.spells[W8_SPELL_GUARDIAN_ANGEL].effect_dice) *
                power);
            entry.magnitude =
                static_cast<short>((static_cast<unsigned int>(entry.magnitude) * percent) / 100) +
                entry.magnitude;
        }
        RecomputeEnchantmentTop(character);
        PushSlotDisplayChanged(rules.events, slot);
        host.enchantmentChanged(slot, enchantment);
    }
    Resettle(rules, host, slot);
}

void Disenchant(RuleContext& rules, PartyHost& host, unsigned int slot,
                W8EnchantmentSlot enchantment)
{
    auto& character = rules.party.characters[slot];
    character.enchantments[enchantment] = {};
    RecomputeEnchantmentTop(character);
    PushSlotDisplayChanged(rules.events, slot);
    Resettle(rules, host, slot);
    host.enchantmentChanged(slot, enchantment);
}

void AgeEnchantment(RuleContext& rules, PartyHost& host, unsigned int slot,
                    W8EnchantmentSlot enchantment, unsigned int turns)
{
    auto& entry = rules.party.characters[slot].enchantments[enchantment];
    if (entry.turns <= turns) {
        Disenchant(rules, host, slot, enchantment);
        return;
    }
    entry.turns -= turns;
}

void RebuildConditionModifiers(RuleContext& rules, PartyHost& host, unsigned int slot)
{
    auto& character = rules.party.characters[slot];
    auto& block = character.condition_modifiers;
    block = {};
    ApplyConditionModifiers(&character, character.uiCondition, character.condition_argument,
                            &block);
    // A swallowed member's regeneration suffers by the swallower's level.
    const auto& swallowed = character.conditions[W8_DEPENDENCE_SWALLOWED];
    if (character.uiCondition[W8_CONDITION_MISSING] != 0 && swallowed.active) {
        const auto level = host.swallowerLevel(swallowed);
        block.health_regen_adjustment += level ? -1 - (*level >> 1) : -5;
    }
    ApplyEnchantmentModifiers(character.enchantments, &block);
    // A member whose NPC is bound loses twenty from every attribute and skill.
    if (const auto& member = rules.party.members[slot]; member && member->npc_bound) {
        for (auto& adjustment : block.attribute_adjustments) adjustment -= 0x14;
        for (auto& bonus : block.skill_bonus) bonus -= 0x14;
    }
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

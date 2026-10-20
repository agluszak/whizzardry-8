#include "wiz8/rules/party_stats.h"

#include <span>

#include "wiz8/data/game_data.h"
#include "wiz8/data/skills.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/character_derivation.h"
#include "wiz8/rules/character_stats.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/load_category.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_slot.h"

/* Derived statistics and shared load. The retail code at 0x004ED9D0 sits in a gap
   between Local Code\Combat.cpp (upper 0x004ED390) and Local Code\GameplayCode.cpp
   (lower 0x004EE000); it is neither a Combat tail nor a separately named unit. */

/* Calculate from resolved attributes, skills and equipment. Live equipment
   reconciliation and presentation are separate operations at the caller. */
// FUNCTION: WIZ8 0x004ed9d0
static CharacterStatsUpdate RecalculateCharacterStatsFromAttributes(const GameData& data,
                                                                    W8Character& subject,
                                                                    CharacterStatRules rules)
{
    auto* character = &subject;
    const bool hp_exhausted = RecalculateCharacterHitPoints(character);
    RecalculateCharacterStamina(character);
    RecalculateRealmSpellPoints(data, character);
    CalcArmorClasses(data, character, rules.defensive_action);
    RebuildCharacterRegenRates(character);

    character->damage_reduction = 0;
    if (CharacterHasTrait(character, W8_TRAIT_DWARF_DAMAGE_RESISTANCE)) {
        character->damage_reduction += character->attributes[W8_ATTRIBUTE_VITALITY].effective / 10;
    }
    if (CharacterHasTrait(character, W8_TRAIT_MONK_DAMAGE_RESISTANCE)) {
        character->damage_reduction += static_cast<int>(
            ScaleValueByProfessionLevel(character, W8_TRAIT_MONK_DAMAGE_RESISTANCE, 15.0f));
    }
    if (character->skills[W8_SKILL_IRON_SKIN].active) {
        character->damage_reduction += (character->skills[W8_SKILL_IRON_SKIN].level >> 2) + 5;
    }
    character->damage_reduction += character->bonus.damage_reduction_adjustment;
    RecalculateCharacterResistances(character);

    RecalculateCarryingCapacity(character);
    return {true, RecalculateInventoryWeight(data, *character), hp_exhausted};
}

namespace {
W8LoadCategory LoadCategory(unsigned int percent)
{
    if (percent < 50) return W8_LOAD_NONE;
    if (percent < 70) return W8_LOAD_LIGHT;
    if (percent < 85) return W8_LOAD_MEDIUM;
    if (percent <= 100) return W8_LOAD_HEAVY;
    return W8_LOAD_EXTREME;
}
bool UpdateCharacterLoad(W8Character& character)
{
    character.total_carried_weight = character.party_weight_share + character.inventory_weight;
    const auto previous = character.load_category;
    character.load_category =
        LoadCategory(static_cast<unsigned int>(character.total_carried_weight * 100) /
                     character.carrying_capacity);
    return previous != character.load_category;
}
void RecalculateLoadModifiers(const GameData& data, W8Character& character,
                              CharacterStatRules rules)
{
    CalcInitiative(&character);
    CalcAttacks(data, &character, rules.damaged_android);
    CalcArmorClasses(data, &character, rules.defensive_action);
}
}

static void RecalculateCharacterLoadStats(const GameData& data, W8Character& character,
                                          CharacterStatRules rules)
{
    UpdateCharacterLoad(character);
    RecalculateLoadModifiers(data, character, rules);
}

/* Carrying capacity from strength and the carrying trait. The two attributes
   are the third and first effective values, the same pair the stamina
   recomputation reads. */
bool RecalculateCarryingCapacity(W8Character* character)
{
    unsigned int previous = character->carrying_capacity;
    int base = character->attributes[W8_ATTRIBUTE_VITALITY].effective +
               character->attributes[W8_ATTRIBUTE_STRENGTH].effective * 2;
    unsigned int capacity = base * 0xc;
    if (CharacterHasTrait(character, W8_TRAIT_FAERIE_REDUCED_CARRY_CAPACITY)) {
        capacity = static_cast<unsigned int>(base * 0x18) / 3;
    }
    character->carrying_capacity = capacity;
    return previous != capacity;
}

/* Sum inventory weight and report the capacity crossing to the caller. */
// FUNCTION: WIZ8 0x004edc60
InventoryWeightChange RecalculateInventoryWeight(const GameData& data, W8Character& subject)
{
    auto* character = &subject;
    unsigned int previous = character->inventory_weight;
    character->inventory_weight = 0;
    for (auto& item : character->EquippedItem) {
        character->inventory_weight += GetItemStackWeight(data, &item);
    }
    for (auto& item : character->backpack) {
        character->inventory_weight += GetItemStackWeight(data, &item);
    }
    return {previous != character->inventory_weight,
            previous < character->carrying_capacity &&
                character->carrying_capacity < character->inventory_weight};
}

/* Split the supplied pool weight across eligible members by remaining
   capacity, retaining slot order for ties and the original half-unit rounding.
   Only the shares are assigned; load bands are the caller's to update. */
// FUNCTION: WIZ8 0x004edd20
static void DistributePartyWeight(const GameData& data, W8Party& party)
{
    int capacity[8];
    int unassigned[8];
    float load_ratio[8];
    W8Character* characters = party.characters;
    W8PartySlotRow* active = party.rows;

    unsigned int slot;
    for (slot = 0; slot < 8; ++slot) {
        W8Character* character = &characters[slot];
        character->party_weight_share = 0;
        if (active[slot].fOccupied && character->highest_condition < W8_CONDITION_DEAD) {
            capacity[slot] = character->carrying_capacity;
            unassigned[slot] = capacity[slot] - character->inventory_weight;
            load_ratio[slot] = unassigned[slot] * 100.0f / capacity[slot];
        }
    }

    unsigned int party_weight = 0;
    for (const auto& item : std::span{party.item_pool}.first(party.item_count)) {
        party_weight += GetItemStackWeight(data, &item);
    }

    for (party_weight >>= 1; party_weight != 0; --party_weight) {
        unsigned int best_slot = static_cast<unsigned int>(-1);
        float best_ratio = -999999.0f;
        for (slot = 0; slot < 8; ++slot) {
            W8Character* character = &characters[slot];
            if (active[slot].fOccupied && character->highest_condition < W8_CONDITION_DEAD &&
                load_ratio[slot] > best_ratio) {
                best_ratio = load_ratio[slot];
                best_slot = slot;
            }
        }
        if (best_slot == static_cast<unsigned int>(-1)) break;
        W8Character* character = &characters[best_slot];
        ++character->party_weight_share;
        --unassigned[best_slot];
        load_ratio[best_slot] = unassigned[best_slot] * 100.0f / capacity[best_slot];
    }
}

// Apply the load timing: false when shared weight was not redistributed now.
static bool AssignPartyLoad(const GameData& data, W8Party& party, const PartyStatRules& rules)
{
    switch (rules.load_timing) {
    case PartyLoadTiming::Disabled:
        return false;
    case PartyLoadTiming::AfterCombat:
        party.redistribution_pending = true;
        return false;
    case PartyLoadTiming::Immediate:
        break;
    }
    DistributePartyWeight(data, party);
    party.redistribution_pending = false;
    return true;
}

bool RefreshPartyLoad(const GameData& data, W8Party& party, const PartyStatRules& rules)
{
    if (!AssignPartyLoad(data, party, rules)) return false;
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (party.rows[slot].fOccupied && UpdateCharacterLoad(party.characters[slot]))
            RecalculateLoadModifiers(data, party.characters[slot], rules.characters[slot]);
    return true;
}

CharacterStatsUpdate UpdateStandaloneCharacterStats(const GameData& data, W8Character& character,
                                                    CharacterStatRules rules)
{
    if (!IsCharacterDefined(character)) return {};
    EvaluateCharacterInputs(data, character, nullptr);
    auto update = RecalculateCharacterStatsFromAttributes(data, character, rules);
    character.party_weight_share = 0;
    RecalculateCharacterLoadStats(data, character, rules);
    return update;
}

W8PartyStats UpdatePartyStats(const GameData& data, W8Party& party, const PartyStatRules& rules)
{
    W8PartyStats result;
    auto& characters = party.characters;
    // Establish every capacity and inventory weight before allocating the shared pool.
    for (unsigned slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        if (!party.rows[slot].fOccupied || !IsCharacterDefined(characters[slot])) continue;
        EvaluateCharacterInputs(data, characters[slot],
                                characters[slot].fInParty ? &party.modifiers : nullptr);
        result.characters[slot] =
            RecalculateCharacterStatsFromAttributes(data, characters[slot], rules.characters[slot]);
        if (rules.load_timing == PartyLoadTiming::Disabled) characters[slot].party_weight_share = 0;
    }
    result.load_redistributed = AssignPartyLoad(data, party, rules);
    for (unsigned slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (result.characters[slot].valid)
            RecalculateCharacterLoadStats(data, characters[slot], rules.characters[slot]);
    return result;
}

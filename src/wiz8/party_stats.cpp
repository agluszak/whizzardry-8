#include "wiz8/party_stats.h"

#include <span>

#include "conditions.h"
#include "gameplay_modifiers.h"
#include "load_category.h"
#include "wiz8/character_skills.h"
#include "wiz8/character_stats.h"
#include "wiz8/item_rules.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/party_slot.h"
#include "wiz8/local_code/GameplayTime.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/MagicEffects.h"
#include "wiz8/party.h"

/* Derived statistics and shared load. The retail code at 0x004ED9D0 sits in a gap
   between Local Code\Combat.cpp (upper 0x004ED390) and Local Code\GameplayCode.cpp
   (lower 0x004EE000); it is neither a Combat tail nor a separately named unit. */

/* Calculate from resolved attributes, skills and equipment. Live equipment
   reconciliation and presentation are separate operations at the caller. */
// FUNCTION: WIZ8 0x004ed9d0
static CharacterStatsUpdate RecalculateCharacterStatsFromAttributes(W8Character& subject,
                                                                    CharacterStatRules rules)
{
    auto* character = &subject;
    const bool hp_exhausted = RecalculateCharacterHitPoints(character);
    RecalculateCharacterStamina(character);
    RecalculateRealmSpellPoints(character);
    CalcArmorClasses(character, rules.defensive_action);
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
    return {true, RecalculateInventoryWeight(*character), hp_exhausted};
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
void RecalculateLoadModifiers(W8Character& character, CharacterStatRules rules)
{
    CalcInitiative(&character);
    CalcAttacks(&character, rules.damaged_android);
    CalcArmorClasses(&character, rules.defensive_action);
}
}

static void RecalculateCharacterLoadStats(W8Character& character, CharacterStatRules rules)
{
    UpdateCharacterLoad(character);
    RecalculateLoadModifiers(character, rules);
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
InventoryWeightChange RecalculateInventoryWeight(W8Character& subject)
{
    auto* character = &subject;
    unsigned int previous = character->inventory_weight;
    character->inventory_weight = 0;
    for (auto& item : character->EquippedItem) {
        character->inventory_weight += GetItemStackWeight(&item);
    }
    for (auto& item : character->backpack) {
        character->inventory_weight += GetItemStackWeight(&item);
    }
    return {previous != character->inventory_weight,
            previous < character->carrying_capacity &&
                character->carrying_capacity < character->inventory_weight};
}

/* Split the supplied pool weight across eligible members by remaining
   capacity, retaining slot order for ties and the original half-unit rounding.
   Only the shares are assigned; load bands are the caller's to update. */
// FUNCTION: WIZ8 0x004edd20
static void DistributePartyWeight(W8Party& party)
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
        party_weight += GetItemStackWeight(&item);
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
static bool AssignPartyLoad(W8Party& party, const PartyStatRules& rules)
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
    DistributePartyWeight(party);
    party.redistribution_pending = false;
    return true;
}

bool W8Party::refreshLoad(const PartyStatRules& rules)
{
    if (!AssignPartyLoad(*this, rules)) return false;
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (rows[slot].fOccupied && UpdateCharacterLoad(characters[slot]))
            RecalculateLoadModifiers(characters[slot], rules.characters[slot]);
    return true;
}

CharacterStatsUpdate UpdateStandaloneCharacterStats(W8Character& character,
                                                    CharacterStatRules rules)
{
    if (!IsCharacterDefined(character)) return {};
    EvaluateCharacterInputs(character, nullptr);
    auto update = RecalculateCharacterStatsFromAttributes(character, rules);
    character.party_weight_share = 0;
    RecalculateCharacterLoadStats(character, rules);
    return update;
}

W8PartyStats W8Party::updateStats(const PartyStatRules& rules)
{
    W8PartyStats result;
    // Establish every capacity and inventory weight before allocating the shared pool.
    for (unsigned slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        if (!rows[slot].fOccupied || !IsCharacterDefined(characters[slot])) continue;
        EvaluateCharacterInputs(characters[slot], characters[slot].fInParty ? &modifiers : nullptr);
        result.characters[slot] =
            RecalculateCharacterStatsFromAttributes(characters[slot], rules.characters[slot]);
        if (rules.load_timing == PartyLoadTiming::Disabled) characters[slot].party_weight_share = 0;
    }
    result.load_redistributed = AssignPartyLoad(*this, rules);
    for (unsigned slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (result.characters[slot].valid)
            RecalculateCharacterLoadStats(characters[slot], rules.characters[slot]);
    return result;
}

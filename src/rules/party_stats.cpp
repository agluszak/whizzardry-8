#include "wiz8/rules/party_stats.h"

#include <array>
#include <optional>
#include <span>

#include "wiz8/data/game_data.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/character_derivation.h"
#include "wiz8/rules/character_stats.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_slot.h"

/* Split the supplied pool weight across eligible members by remaining
   capacity, retaining slot order for ties and the original half-unit rounding.
   Capacities and inventory weights come from the members' evaluated inputs; only
   the shares are assigned. */
// FUNCTION: WIZ8 0x004edd20
static void DistributePartyWeight(const GameData& data, W8Party& party)
{
    int capacity[8];
    int unassigned[8];
    float load_ratio[8];
    W8Character* characters = party.characters;

    unsigned int slot;
    for (slot = 0; slot < 8; ++slot) {
        W8Character* character = &characters[slot];
        character->party_weight_share = 0;
        if (party.members[slot].has_value() && character->highest_condition < W8_CONDITION_DEAD) {
            capacity[slot] = static_cast<int>(CarryingCapacity(*character));
            unassigned[slot] = capacity[slot] - static_cast<int>(InventoryWeight(data, *character));
            load_ratio[slot] = unassigned[slot] * 100.0f / capacity[slot];
        }
    }

    unsigned int party_weight = 0;
    for (const auto& item : std::span{party.item_pool}.first(party.item_count)) {
        party_weight += GetItemStackWeight(data, &item);
    }

    for (party_weight >>= 1; party_weight != 0; --party_weight) {
        std::optional<unsigned> best_slot;
        float best_ratio = -999999.0f;
        for (slot = 0; slot < 8; ++slot) {
            W8Character* character = &characters[slot];
            if (party.members[slot].has_value() &&
                character->highest_condition < W8_CONDITION_DEAD && load_ratio[slot] > best_ratio) {
                best_ratio = load_ratio[slot];
                best_slot = slot;
            }
        }
        if (!best_slot) break;
        W8Character* character = &characters[*best_slot];
        ++character->party_weight_share;
        --unassigned[*best_slot];
        load_ratio[*best_slot] = unassigned[*best_slot] * 100.0f / capacity[*best_slot];
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

/* Derive, let the pools follow their new ceilings, and derive again when the fatigue band
   moved, because armor class reads it. Both passes read the same baseline. */
static CharacterStatsUpdate SettleCharacterStats(const GameData& data, W8Character& character,
                                                 CharacterStatRules rules,
                                                 const CharacterBaseline& baseline)
{
    DeriveCharacterStats(data, character, rules, baseline);
    const bool hp_exhausted = FollowHitPointCeiling(character, baseline.hp_max);
    const bool band_changed = FollowStaminaCeiling(character, baseline.stamina_max);
    FollowSpellPointCeilings(character, baseline.sp_max);
    if (band_changed) DeriveCharacterStats(data, character, rules, baseline);
    return {.valid = true,
            .became_overloaded = BecameOverloaded(baseline.inventory_weight, character),
            .hp_exhausted = hp_exhausted};
}

CharacterStatsUpdate UpdateCharacterStatsFromInputs(const GameData& data, W8Character& character,
                                                    CharacterStatRules rules)
{
    return SettleCharacterStats(data, character, rules, CaptureCharacterBaseline(character));
}

CharacterStatsUpdate UpdateStandaloneCharacterStats(const GameData& data, W8Character& character,
                                                    CharacterStatRules rules)
{
    if (!IsCharacterDefined(character)) return {};
    const auto baseline = CaptureCharacterBaseline(character);
    EvaluateCharacterInputs(data, character, nullptr);
    character.party_weight_share = 0;
    return SettleCharacterStats(data, character, rules, baseline);
}

W8PartyStats UpdatePartyStats(const GameData& data, W8Party& party, const PartyStatRules& rules)
{
    W8PartyStats result;
    std::array<CharacterBaseline, W8_PARTY_SLOT_COUNT> baselines{};
    auto& characters = party.characters;
    // Every member's inputs come before the shared pool is allocated by capacity.
    for (unsigned slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        if (!party.members[slot].has_value() || !IsCharacterDefined(characters[slot])) continue;
        baselines[slot] = CaptureCharacterBaseline(characters[slot]);
        EvaluateCharacterInputs(data, characters[slot],
                                characters[slot].fInParty ? &party.modifiers : nullptr);
        if (rules.load_timing == PartyLoadTiming::Disabled) characters[slot].party_weight_share = 0;
        result.characters[slot].valid = true;
    }
    result.load_redistributed = AssignPartyLoad(data, party, rules);
    for (unsigned slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (result.characters[slot].valid)
            result.characters[slot] = SettleCharacterStats(data, characters[slot],
                                                           rules.characters[slot], baselines[slot]);
    return result;
}

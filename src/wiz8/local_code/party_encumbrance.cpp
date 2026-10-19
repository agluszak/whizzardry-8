#include "wiz8/local_code/party_encumbrance.h"

#include <optional>
#include <span>
#include <stdexcept>

#include "conditions.h"
#include "gameplay_modifiers.h"
#include "layouts/gameplay_databases.h"
#include "load_category.h"
#include "wiz8/character_skills.h"
#include "wiz8/character_stat_rules.h"
#include "wiz8/character_stats.h"
#include "wiz8/item_rules.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/party_slot.h"
#include "wiz8/local_code/GameplayTime.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/MagicEffects.h"
#include "wiz8/party.h"
#include "wiz8/party_stat_rules.h"

/* Party encumbrance redistribution. Live query: 0x004ED9D0 is a gap between
   Local Code\Combat.cpp (upper 0x004ED390) and Local Code\GameplayCode.cpp
   (lower 0x004EE000). Not a Combat tail and not an invisible named unit. */

/* Calculate from resolved attributes, skills and equipment. Live equipment
   reconciliation and presentation are separate operations at the caller. */
// FUNCTION: WIZ8 0x004ed9d0
static CharacterStatsUpdate RecalculateCharacterStatsFromAttributes(W8Character& subject,
                                                                    CharacterStatRules rules)
{
    auto* character = &subject;
    if (character->iProfession >= W8_PROFESSION_COUNT || character->iRace >= W8_RACE_COUNT ||
        character->gender == W8_GENDER_UNSET) {
        return {};
    }
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

    const bool capacity_changed = RecalculateCarryingCapacity(character);
    return {true, capacity_changed, RecalculateInventoryWeight(*character), hp_exhausted};
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
   capacity, retaining slot order for ties and the original half-unit rounding. */
// FUNCTION: WIZ8 0x004edd20
static void DistributePartyWeight(W8Party& party, const PartyStatRules& rules)
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

    for (slot = 0; slot < 8; ++slot) {
        if (!active[slot].fOccupied) {
            continue;
        }
        W8Character* character = &characters[slot];
        if (UpdateCharacterLoad(*character))
            RecalculateLoadModifiers(*character, rules.characters[slot]);
    }
}

std::optional<unsigned int> W8Party::slotOf(const W8Character& character) const
{
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (&characters[slot] == &character) return slot;
    return {};
}

bool W8Party::refreshLoad(const PartyStatRules& rules)
{
    if (rules.load_timing == PartyLoadTiming::Disabled) return false;
    if (rules.load_timing == PartyLoadTiming::AfterCombat) {
        redistribution_pending = true;
        return false;
    }
    DistributePartyWeight(*this, rules);
    redistribution_pending = false;
    return true;
}

CharacterStatsUpdate UpdateStandaloneCharacterStats(W8Character& character,
                                                    CharacterStatRules rules)
{
    EvaluateCharacterInputs(character, nullptr);
    auto update = RecalculateCharacterStatsFromAttributes(character, rules);
    if (!update.valid) return update;
    character.party_weight_share = 0;
    RecalculateCharacterLoadStats(character, rules);
    return update;
}

CharacterStatsUpdate W8Party::updateCharacterStats(unsigned int slot, const PartyStatRules& rules)
{
    if (slot >= W8_PARTY_SLOT_COUNT) throw std::out_of_range("invalid party slot");
    auto& character = characters[slot];
    const auto character_rules = rules.characters[slot];
    EvaluateCharacterInputs(character, character.fInParty ? &modifiers : nullptr);
    auto update = RecalculateCharacterStatsFromAttributes(character, character_rules);
    if (!update.valid) return update;
    if (rules.load_timing == PartyLoadTiming::Disabled) {
        character.party_weight_share = 0;
    } else if (update.capacity_changed || update.weight.changed) {
        update.party_load_changed = refreshLoad(rules);
    }
    RecalculateCharacterLoadStats(character, character_rules);
    return update;
}

std::array<CharacterStatsUpdate, W8_PARTY_SLOT_COUNT>
W8Party::updateStats(const PartyStatRules& rules)
{
    std::array<CharacterStatsUpdate, W8_PARTY_SLOT_COUNT> updates{};
    // Establish every capacity and inventory weight before allocating the shared pool.
    for (unsigned slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        if (!rows[slot].fOccupied) continue;
        EvaluateCharacterInputs(characters[slot], characters[slot].fInParty ? &modifiers : nullptr);
        updates[slot] =
            RecalculateCharacterStatsFromAttributes(characters[slot], rules.characters[slot]);
        if (rules.load_timing == PartyLoadTiming::Disabled) characters[slot].party_weight_share = 0;
    }
    const bool load_changed = refreshLoad(rules);
    for (unsigned slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        if (!updates[slot].valid) continue;
        updates[slot].party_load_changed = load_changed;
        RecalculateCharacterLoadStats(characters[slot], rules.characters[slot]);
    }
    return updates;
}

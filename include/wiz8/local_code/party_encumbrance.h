#pragma once

#include "wiz8/character_stat_rules.h"

struct W8Character;
struct InventoryWeightChange {
    bool changed = false;
    bool became_overloaded = false;
};
struct CharacterStatsUpdate {
    bool valid = false;
    bool capacity_changed = false;
    InventoryWeightChange weight;
    bool hp_exhausted = false;
    bool party_load_changed = false;
};
InventoryWeightChange RecalculateInventoryWeight(W8Character& character);
bool RecalculateCarryingCapacity(W8Character* character);
// Evaluate the character's own equipment and invested attributes/skills, then apply
// resource and load changes without live reconciliation.
// Standalone calculation has no party capability, regardless of saved membership flags.
CharacterStatsUpdate UpdateStandaloneCharacterStats(W8Character& character,
                                                    CharacterStatRules rules);

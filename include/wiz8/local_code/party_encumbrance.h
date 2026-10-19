#pragma once

#include "wiz8/character_stat_rules.h"

struct W8Character;
struct InventoryWeightChange {
    bool changed = false;
    bool became_overloaded = false;
};
// Derived values are written to the character; transitions they imply are reported.
struct CharacterStatsUpdate {
    bool valid = false;
    InventoryWeightChange weight;
    // A lowered hit-point ceiling exhausted the current pool.
    bool hp_exhausted = false;
};
InventoryWeightChange RecalculateInventoryWeight(W8Character& character);
bool RecalculateCarryingCapacity(W8Character* character);
// A character outside any party: no party modifiers and no shared load.
CharacterStatsUpdate UpdateStandaloneCharacterStats(W8Character& character,
                                                    CharacterStatRules rules);

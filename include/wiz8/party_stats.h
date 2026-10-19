#pragma once

#include <array>

#include "wiz8/layouts/party_slot.h"

/* Derived character statistics and the party's shared load. The calculations take
   the inputs that come from combat and NPC story state explicitly; live_party.h
   supplies the active session's. */

struct W8Character;

struct CharacterStatRules {
    bool defensive_action = false;
    bool damaged_android = false;
};
// Shared weight is redistributed now, after combat, or not at all before the game starts.
enum class PartyLoadTiming { Disabled, Immediate, AfterCombat };
struct PartyStatRules {
    PartyLoadTiming load_timing = PartyLoadTiming::Immediate;
    std::array<CharacterStatRules, W8_PARTY_SLOT_COUNT> characters{};
};

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

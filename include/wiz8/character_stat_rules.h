#pragma once

// Inputs supplied by the operation that owns combat and NPC story state.
// Calculations never resolve an actor against the live party.
struct CharacterStatRules {
    bool defensive_action = false;
    bool damaged_android = false;
};

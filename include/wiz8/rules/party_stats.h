#pragma once

#include <array>
#include <span>

#include "wiz8/rules/character_stats.h"
#include "wiz8/rules/party_slot.h"

/* Derived character statistics and the party's shared load. Every recalculation goes
   through these: inputs are evaluated, every statistic is derived once from them
   (character_stats.h), and the resource transitions that follow are applied or reported.
   live_party.h supplies the active game's inputs and applies what is reported. */

struct GameData;
struct W8Character;
struct W8ItemInstance;
struct W8Party;

// Shared weight is redistributed now, after combat, or not at all before the game starts.
enum class PartyLoadTiming { Disabled, Immediate, AfterCombat };
struct PartyStatRules {
    PartyLoadTiming load_timing = PartyLoadTiming::Immediate;
    std::array<CharacterStatRules, W8_PARTY_SLOT_COUNT> characters{};
};

/* The transitions a recalculation implies. Pools have followed their ceilings; death and
   the overload notice belong to the caller. */
struct CharacterStatsUpdate {
    bool valid = false;
    bool became_overloaded = false;
    // A lowered hit-point ceiling exhausted the current pool.
    bool hp_exhausted = false;
};
struct W8PartyStats {
    std::array<CharacterStatsUpdate, W8_PARTY_SLOT_COUNT> characters{};
    bool load_redistributed = false;
};
// Recalculate every occupied member from its equipment, attributes and conditions, with the
// shared load assigned between evaluating the members' inputs and deriving their statistics.
// `pool` is the shared pool whose weight the members carry.
W8PartyStats UpdatePartyStats(const GameData& data, W8Party& party,
                              std::span<const W8ItemInstance> pool, const PartyStatRules& rules);
// A character outside any party: no party modifiers and no shared load.
CharacterStatsUpdate UpdateStandaloneCharacterStats(const GameData& data, W8Character& character,
                                                    CharacterStatRules rules);
/* Recalculate from the effective attributes and skill levels the character holds, without
   evaluating them again; the creation editor owns them while it edits them. The party
   weight share is kept. */
CharacterStatsUpdate UpdateCharacterStatsFromInputs(const GameData& data, W8Character& character,
                                                    CharacterStatRules rules);

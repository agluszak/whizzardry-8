#pragma once

#include <array>

#include "wiz8/character_stat_rules.h"
#include "wiz8/layouts/party_slot.h"

enum class PartyLoadTiming { Disabled, Immediate, AfterCombat };
struct PartyStatRules {
    PartyLoadTiming load_timing = PartyLoadTiming::Immediate;
    std::array<CharacterStatRules, W8_PARTY_SLOT_COUNT> characters{};
};

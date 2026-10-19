#pragma once

#include <array>
#include <optional>

#include "wiz8/gameplay_modifiers.h"
#include "wiz8/item_location.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/item_instance.h"
#include "wiz8/layouts/party_formation.h"
#include "wiz8/layouts/party_slot.h"
#include "wiz8/local_code/party_encumbrance.h"

// Character and action storage have the same lifetime as their party.
struct PartyStatRules;
struct CharacterStatsUpdate;

struct W8HeldItem {
    W8ItemInstance item{.iItemNo = -1};
    W8ItemLocation origin{};
    int actor = -1;
};

struct W8Party {
    W8PartyFormationState formation{};
    unsigned int order_slots[W8_PARTY_SLOT_COUNT]{};
    unsigned int gold = 0;
    W8GameplayModifierBlock modifiers{};
    W8ItemInstance item_pool[500]{};
    unsigned int item_count = 0;
    W8HeldItem held{};
    W8ItemLocation locateItem(const W8ItemInstance* item) const;
    W8ItemInstance* itemAt(W8ItemLocation location);
    const W8ItemInstance* itemAt(W8ItemLocation location) const;
    std::optional<unsigned int> slotOf(const W8Character& character) const;
    bool refreshLoad(const PartyStatRules& rules);
    std::array<CharacterStatsUpdate, W8_PARTY_SLOT_COUNT> updateStats(const PartyStatRules& rules);
    CharacterStatsUpdate updateCharacterStats(unsigned int slot, const PartyStatRules& rules);

    bool redistribution_pending = false;
    std::optional<int> companion_inventory;
    W8Character characters[W8_PARTY_SLOT_COUNT]{};
    W8PartySlotRow rows[W8_PARTY_SLOT_COUNT]{};
};

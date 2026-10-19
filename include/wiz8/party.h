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

struct PartyStatRules;

struct W8HeldItem {
    W8ItemInstance item{.iItemNo = -1};
    W8ItemLocation origin{};
    int actor = -1;
};

// Derived values written by a recalculation, and the transitions they imply.
// Applying those transitions (death, notices) belongs to the caller.
struct W8PartyStats {
    std::array<CharacterStatsUpdate, W8_PARTY_SLOT_COUNT> characters{};
    bool load_redistributed = false;
};

// Character and action storage have the same lifetime as their party.
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

    // Storage only: no legality checks and no gameplay consequences. An emptied pool
    // entry closes up; an emptied hand forgets where its item came from.
    W8ItemInstance take(W8ItemLocation location);
    void exchange(W8ItemLocation first, W8ItemLocation second, int actor);
    bool insertIntoPool(unsigned int index, W8ItemInstance item);

    // Recalculate every occupied member from its equipment, attributes and conditions,
    // then the shared load.
    W8PartyStats updateStats(const PartyStatRules& rules);
    bool refreshLoad(const PartyStatRules& rules);

    bool redistribution_pending = false;
    std::optional<int> companion_inventory;
    W8Character characters[W8_PARTY_SLOT_COUNT]{};
    W8PartySlotRow rows[W8_PARTY_SLOT_COUNT]{};
};

#pragma once

#include <array>
#include <optional>

#include "wiz8/rules/character.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/party_formation.h"
#include "wiz8/rules/party_slot.h"

struct W8HeldItem {
    W8ItemInstance item{.iItemNo = -1};
    W8ItemLocation origin{};
    int actor = -1;
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

    W8ItemInstance* itemAt(W8ItemLocation location);
    const W8ItemInstance* itemAt(W8ItemLocation location) const;
    std::optional<unsigned int> slotOf(const W8Character& character) const;
    // The NPC state index of the slot's member; -1 for an empty slot or a created character.
    int npcIndex(unsigned int slot) const;
    /* The slot's place in the marching order; an empty slot reads as the first place, as a
       cleared retail row did. */
    int orderIndexOf(unsigned int slot) const;

    // Storage only: no legality checks and no gameplay consequences. An emptied pool
    // entry closes up; an emptied hand forgets where its item came from.
    W8ItemInstance take(W8ItemLocation location);
    void exchange(W8ItemLocation first, W8ItemLocation second, int actor);
    bool insertIntoPool(unsigned int index, W8ItemInstance item);

    bool redistribution_pending = false;
    std::optional<int> companion_inventory;
    W8Character characters[W8_PARTY_SLOT_COUNT]{};
    // The members by slot; an empty slot has none.
    std::array<std::optional<W8PartyMember>, W8_PARTY_SLOT_COUNT> members{};
    W8PartySlotRow rows[W8_PARTY_SLOT_COUNT]{};
};

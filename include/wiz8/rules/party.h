#pragma once

#include <array>
#include <optional>

#include "wiz8/rules/character.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/party_formation.h"
#include "wiz8/rules/party_slot.h"

// Character and action storage have the same lifetime as their party.
struct W8Party {
    W8PartyFormationState formation{};
    unsigned int order_slots[W8_PARTY_SLOT_COUNT]{};
    unsigned int gold = 0;
    W8GameplayModifierBlock modifiers{};

    std::optional<unsigned int> slotOf(const W8Character& character) const;
    // The NPC state index of the slot's member; -1 for an empty slot or a created character.
    int npcIndex(unsigned int slot) const;
    /* The slot's place in the marching order; an empty slot reads as the first place, as a
       cleared retail row did. */
    int orderIndexOf(unsigned int slot) const;

    std::optional<int> companion_inventory;
    W8Character characters[W8_PARTY_SLOT_COUNT]{};
    // The members by slot; an empty slot has none.
    std::array<std::optional<W8PartyMember>, W8_PARTY_SLOT_COUNT> members{};
    W8PartySlotRow rows[W8_PARTY_SLOT_COUNT]{};
};

// Take gold from the party, clamping at zero.
void SpendPartyGold(W8Party& party, unsigned int amount);

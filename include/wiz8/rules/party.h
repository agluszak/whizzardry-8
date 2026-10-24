#pragma once

#include <array>
#include <optional>

#include "wiz8/data/faction_ids.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/party_formation.h"
#include "wiz8/rules/party_slot.h"

class Rng;

// Character and action storage have the same lifetime as their party.
struct W8Party {
    W8PartyFormationState formation{};
    unsigned int order_slots[W8_PARTY_SLOT_COUNT]{};
    unsigned int gold = 0;
    W8GameplayModifierBlock modifiers{};

    W8Disposition dispositionToward(unsigned int slot, W8Disposition other) const;
    W8Disposition dispositionBetween(unsigned int first, unsigned int second) const;
    bool canAct(unsigned int slot) const;
    W8ActionKind commitAction(unsigned int slot, W8Action action);
    void beginDefensiveAction(unsigned int slot, W8ActionKind action);
    bool canAttack(unsigned int slot) const;
    bool participates(unsigned int slot) const;
    W8Action availableAction(unsigned int slot, W8Action action) const;
    W8Action action(unsigned int slot, W8TargetingContext stored) const;
    bool aimAction(unsigned int slot, W8TargetingContext stored, const W8CombatSlot& target,
                   bool remember);

    bool canChangeFormation(unsigned int slot) const;
    // Apply legal placement without changing the proposal or producing presentation.
    void applyFormation(const W8PartyFormationState& proposal);

    std::optional<unsigned int> slotOf(const W8Character& character) const;
    // The NPC state index of the slot's member; -1 for an empty slot or a created character.
    int npcIndex(unsigned int slot) const;
    unsigned int averageLevel() const;
    /* The slot's place in the marching order; an empty slot reads as the first place, as a
       cleared retail row did. */
    int orderIndexOf(unsigned int slot) const;
    /* A random slot other than `excluded_slot` whose member is not of `excluded_gender`
       (-1 for any), alive unless `require_primary` is 2 and awake unless
       `require_secondary` is 2. A requirement of 1 is waived when nobody meets it; -1 when
       nobody qualifies. Draws from `rng`. */
    int randomSlot(Rng& rng, int require_primary, int require_secondary, int excluded_slot,
                   signed char excluded_gender) const;

    // Living members, including incapacitated ones. No draw when at most one qualifies.
    std::optional<unsigned int> randomLivingSlot(Rng& rng, int excluded_slot = -1,
                                                 unsigned int first_slot = 0) const;

    std::optional<int> companion_inventory;
    W8Character characters[W8_PARTY_SLOT_COUNT]{};
    // The members by slot; an empty slot has none.
    std::array<std::optional<W8PartyMember>, W8_PARTY_SLOT_COUNT> members{};
    W8PartySlotRow rows[W8_PARTY_SLOT_COUNT]{};
};

// Take gold from the party, clamping at zero.
void SpendPartyGold(W8Party& party, unsigned int amount);

#pragma once

#include "wiz8/rules/action_choices.h"

enum { W8_PARTY_SLOT_COUNT = 8 };

/* A member's place in the party, present for an occupied slot only. */
struct W8PartyMember {
    // The member's place in the marching order: the index of its entry in W8Party::order_slots.
    int order_index = 0;
    /* For a recruited NPC, the index of its W8NpcState in the story's NPC states
       (GetNpcState); -1 for a created character. */
    int npc_index = -1;
    /* The NPC is bound to this slot: set when the binding restores, cleared by the level-entry
       reset. Gates the RPC banter event clock and the bonded-attribute penalty. */
    bool npc_bound = false;
    /* Walking fatigue not yet charged: distance-scaled, and every 2500 units become real
       fatigue (FatigueCharacter). */
    float movement_fatigue = 0;
    // The member's level-up has been announced, so the portrait offers it.
    bool level_up_announced = false;
};

/* A party slot's action state, which the party keeps for every slot whether or not a member
   holds it. */
struct W8PartySlotRow {
    // The player's standing choices, the action committed for this turn, and the action being
    // carried out; the three change independently.
    ActionChoices remembered;
    W8Action committed;
    ActionInProgress executing;
    /* A weapon-set swap is queued; the autoswap pass consumes it through `Inventory::swapWeaponSets`
       and clears it. */
    bool weapon_swap_pending;
    // The character event last queued for the slot's portrait.
    unsigned int pending_event_type;
};

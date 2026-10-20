#pragma once

#include "wiz8/rules/action_choices.h"

enum { W8_PARTY_SLOT_COUNT = 8 };

/* One party slot row. */
struct W8PartySlotRow {
    bool fOccupied;
    // The player's standing choices, the action committed for this turn, and the action
    // being carried out; the three change independently.
    ActionChoices remembered;
    W8Action committed;
    ActionInProgress executing;
    /* Historical offset 0x0f1: the slot's place in the marching order, the index of its entry
       in W8Party::order_slots. */
    int party_order_index;
    /* Historical offset 0x0f5: a camp-screen item action is pending on the slot; blocks
       weapon autoswap until resolved. */
    bool item_action_pending;
    /* Historical offset 0x0f6: distance-scaled fatigue accumulator; every 2500 units convert
       into real fatigue via FatigueCharacter. */
    float movement_fatigue;
    /* Historical offset 0x0fa: for a recruited NPC's slot, the index of its W8NpcState in
       the story's NPC states (GetNpcState); -1 for a created character. */
    int npc_index;
    /* Historical offset 0x0fe: cleared by the level-entry NPC-binding reset. */
    /* Historical offset 0xfe: an NPC is bound to this party slot; set when the binding
       restores, cleared by the level-entry reset. Gates the RPC banter
       event clock and the bonded-attribute penalty. */
    bool npc_bound;
    unsigned int pending_event_type;
    /* Historical offset 0x103: portrait advance is only allowed while this is set. */
    unsigned char portrait_advance;
    /* Historical offset 0x105: a weapon-set swap is queued; the autoswap pass consumes it
       through SwapWeaponSetSlots and clears it. */
    bool weapon_swap_pending;
};

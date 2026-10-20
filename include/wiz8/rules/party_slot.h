#pragma once

#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/targeting.h"

enum { W8_PARTY_SLOT_COUNT = 8 };

/* One party slot row. */
struct W8PartySlotRow {
    bool fOccupied;
    W8ActionKind pending_action;
    int attack_mode[4];
    /* Historical offset 0x15: the pending action's own two-word block, the same shape a chosen
       action carries. SelectCombatAction returns it for the out-of-combat
       context; its item member is restored from the saved item reference. */
    W8ActionDetailBlock pending_action_detail;
    W8CombatSlot target_out_of_combat;
    // The accepted action for this turn, independent of picker and remembered choices.
    W8Action committed_action;
    W8ActionKind action_kind;
    int action_detail;
    // Last accepted spell; editing a picker never changes this choice.
    W8Action spell{W8_ACTION_CAST_SPELL, 0};
    /* The item-use two-word detail block: the use kind plus the item. */
    W8ActionDetailBlock item_detail{.item_use = {-1, {}}};
    W8CombatSlot item_target;
    int item_id;
    /* Historical offset 0x0d0: the non-melee W8_ACTION_* code ChooseAction stored for the slot,
       -1 when none. */
    W8ActionKind queued_action = W8_ACTION_NONE;
    /* Historical offset 0x0d1: context-five breath target. */
    W8CombatSlot breath_target;
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
       g_npc_states (GetNpcState); -1 for a created character. */
    int npc_index;
    /* Historical offset 0x0fe: cleared by the level-entry NPC-binding reset. */
    /* Historical offset 0xfe: an NPC is bound to this party slot; set when the binding
       restores, cleared by the level-entry reset. Gates the RPC banter
       event clock and the bonded-attribute penalty. */
    bool npc_bound;
    unsigned int pending_event_type;
    /* Historical offset 0x103: portrait advance is only allowed while this is set. */
    unsigned char portrait_advance;
    bool action_is_berserk;
    /* Historical offset 0x105: a weapon-set swap is queued; the autoswap pass consumes it
       through SwapWeaponSetSlots and clears it. */
    bool weapon_swap_pending;
};

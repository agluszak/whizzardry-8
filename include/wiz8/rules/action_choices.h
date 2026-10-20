#pragma once

#include "wiz8/rules/targeting.h"

/* The choices a party member keeps from turn to turn: the basic combat action,
   the last accepted spell, the item use and the breath target. Picking a new
   action for one turn does not change them. */
struct ActionChoices {
    // Attack, berserk, defend or protect, with its detail word.
    W8ActionKind basic_kind;
    int basic_detail;
    /* The player chose berserk. The basic choice falls back to attack while the
       member cannot berserk and returns to berserk when they can again. */
    bool wants_berserk;
    // Last accepted spell; editing a picker never changes this choice.
    W8Action spell{W8_ACTION_CAST_SPELL, 0};
    // The item-use two-word detail block: the use kind plus the item.
    W8ActionDetailBlock item_detail{.item_use = {-1, {}}};
    W8CombatSlot item_target;
    // The last committed action other than a basic action or a move, which the repeat key repeats.
    W8ActionKind repeat_action = W8_ACTION_NONE;
    W8CombatSlot breath_target;

    W8Action basic() const { return {basic_kind, basic_detail}; }
    void chooseBasic(W8ActionKind kind, int detail)
    {
        basic_kind = kind;
        basic_detail = detail;
        wants_berserk = kind == W8_ACTION_BERSERK;
    }
};

/* The action being carried out: out of combat the chosen action itself, in combat
   the committed action copied when the member's turn comes. */
struct ActionInProgress {
    W8ActionKind kind;
    // Per hand; [0] is also the action's detail word.
    int attack_mode[4];
    /* The action's own two-word block, the same shape a chosen action carries. Its
       item member is restored from the saved item reference. */
    W8ActionDetailBlock detail_block;
    W8CombatSlot target;

    // Begin an action; no hand has chosen its own attack mode yet.
    void start(W8ActionKind next, int detail)
    {
        kind = next;
        attack_mode[0] = detail;
        attack_mode[1] = -1;
    }
    W8Action action() const { return {kind, attack_mode[0], target, detail_block}; }
};

/* A basic berserk choice follows the member's eligibility: it becomes an attack
   while the member cannot berserk, and a remembered berserk returns when they can.
   A committed action of the replaced kind changes with it. */
void ReconcileBerserkChoice(ActionChoices& remembered, W8Action& committed, bool can_berserk);

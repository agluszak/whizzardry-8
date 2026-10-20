#include "wiz8/rules/action_choices.h"

void ReconcileBerserkChoice(ActionChoices& remembered, W8Action& committed, bool can_berserk)
{
    const auto follow = [&](W8ActionKind from, W8ActionKind to) {
        remembered.basic_kind = to;
        if (committed.kind == from) committed.kind = to;
    };
    if (remembered.basic_kind == W8_ACTION_BERSERK && !can_berserk)
        follow(W8_ACTION_BERSERK, W8_ACTION_ATTACK);
    else if (remembered.basic_kind == W8_ACTION_ATTACK && remembered.wants_berserk && can_berserk)
        follow(W8_ACTION_ATTACK, W8_ACTION_BERSERK);
}

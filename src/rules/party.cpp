#include "wiz8/rules/party.h"

#include <optional>

#include "wiz8/rules/character.h"
#include "wiz8/rules/party_slot.h"

std::optional<unsigned int> W8Party::slotOf(const W8Character& character) const
{
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (&characters[slot] == &character) return slot;
    return {};
}

int W8Party::npcIndex(unsigned int slot) const
{
    return members[slot] ? members[slot]->npc_index : -1;
}

int W8Party::orderIndexOf(unsigned int slot) const
{
    return members[slot] ? members[slot]->order_index : 0;
}

void SpendPartyGold(W8Party& party, unsigned int amount)
{
    party.gold = amount > party.gold ? 0 : party.gold - amount;
}

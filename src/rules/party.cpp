#include "wiz8/rules/party.h"

#include <optional>

#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/random.h"

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

int W8Party::randomSlot(Rng& rng, int require_primary, int require_secondary, int excluded_slot,
                        signed char excluded_gender) const
{
    int skip;
    unsigned int slot;
    unsigned int scanned;
    bool matched;
    const W8Character* character;

retry:
    skip = rng.below(8);
    scanned = 0;
    slot = 0;
    do {
        matched = false;
        if (members[slot].has_value() && static_cast<int>(slot) != excluded_slot) {
            character = &characters[slot];
            if ((character->hp_current > 0 && character->highest_condition < W8_CONDITION_DEAD) ||
                require_primary == 2) {
                if (excluded_gender == -1 || excluded_gender != character->gender) {
                    if (character->highest_condition < W8_CONDITION_ASLEEP ||
                        require_secondary == 2) {
                        matched = true;
                        if (skip == 0) {
                            return slot;
                        }
                        skip--;
                        scanned = 0;
                    }
                }
            }
        }
        slot++;
        if (slot == 8) {
            slot = 0;
        }
        scanned++;
    } while (scanned <= 8);

    if (matched) {
        return slot;
    }
    if (require_secondary == 1) {
        require_secondary = 2;
        goto retry;
    }
    if (require_primary == 1) {
        require_primary = 2;
        goto retry;
    }
    return -1;
}

unsigned int W8Party::averageLevel() const
{
    unsigned int total = 0;
    unsigned int count = 0;
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        if (!members[slot]) continue;
        total += characters[slot].uiExpLevel;
        ++count;
    }
    return count ? total / count : 0;
}

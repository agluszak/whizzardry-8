#include "wiz8/rules/party.h"

#include <array>
#include <cassert>
#include <optional>

#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/party_formation.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/random.h"
#include "wiz8/rules/targeting.h"

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

std::optional<unsigned int> W8Party::randomLivingSlot(Rng& rng, int excluded_slot,
                                                      unsigned int first_slot) const
{
    std::array<unsigned int, W8_PARTY_SLOT_COUNT> eligible;
    unsigned int count = 0;
    for (unsigned int slot = first_slot; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        const auto& character = characters[slot];
        if (members[slot] && static_cast<int>(slot) != excluded_slot && character.hp_current != 0 &&
            character.highest_condition < W8_CONDITION_DEAD)
            eligible[count++] = slot;
    }
    if (count == 0) return std::nullopt;
    return eligible[count == 1 ? 0 : rng.below(count)];
}

bool W8Party::canChangeFormation(unsigned int slot) const
{
    return slot < W8_PARTY_SLOT_COUNT && members[slot] && characters[slot].hp_current > 0 &&
           characters[slot].highest_condition < W8_CONDITION_TURNCOAT;
}

void W8Party::applyFormation(const W8PartyFormationState& proposal)
{
    W8PartyFormationState accepted;
    accepted.positions = formation.positions;
    accepted.unknown_74 = formation.unknown_74;
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        if (!canChangeFormation(slot)) continue;
        const auto cell = proposal.cellOf(slot);
        const bool moved = proposal.rowOf(slot) != formation.rowOf(slot);
        accepted.setPosition(slot, cell, false, moved);
    }
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        if (!members[slot] || canChangeFormation(slot) ||
            characters[slot].highest_condition >= W8_CONDITION_DEAD)
            continue;
        const auto previous = formation.cellOf(slot);
        assert(previous);
        if (accepted.rowOccupants(previous->row) < W8_FORMATION_COLUMNS) {
            auto cell = *previous;
            if (accepted.occupant(cell.row, cell.column)) {
                cell.column = 0;
                while (accepted.occupant(cell.row, cell.column)) ++cell.column;
            }
            accepted.setPosition(slot, cell, false, false);
            continue;
        }
        bool placed = false;
        for (int row : {4, 2, 3}) {
            for (int column = 0; column < W8_FORMATION_COLUMNS; ++column) {
                if (accepted.occupant(row, column)) continue;
                accepted.setPosition(slot, W8FormationCell{row, column}, false, true);
                placed = true;
                break;
            }
            if (placed) break;
        }
        assert(placed);
    }
    for (int row = 0; row < W8_FORMATION_ROWS; ++row) accepted.compactRow(row);
    formation = accepted;
}

bool W8Party::participates(unsigned int slot) const
{
    return slot < W8_PARTY_SLOT_COUNT && members[slot] && characters[slot].hp_current != 0 &&
           characters[slot].highest_condition < W8_CONDITION_DEAD;
}
W8Action W8Party::availableAction(unsigned int slot, W8Action action) const
{
    if (!participates(slot) && action.kind != W8_ACTION_WALK && action.kind != W8_ACTION_RUN)
        return {};
    return action;
}
W8Action W8Party::action(unsigned int slot, W8TargetingContext stored) const
{
    const auto& row = rows[slot];
    return availableAction(slot, stored == W8_TARGETING_CONTEXT_IN_COMBAT ? row.committed
                                                                          : row.executing.action());
}
bool W8Party::aimAction(unsigned int slot, W8TargetingContext stored, const W8CombatSlot& target,
                        bool remember)
{
    const auto selected = action(slot, stored);
    if (selected.target == target) return false;
    auto& row = rows[slot];
    (stored == W8_TARGETING_CONTEXT_IN_COMBAT ? row.committed.target : row.executing.target) =
        target;
    if (remember && target.iType != W8_TARGET_KIND_NONE) {
        switch (selected.kind) {
        case W8_ACTION_CAST_SPELL:
            row.remembered.spell.target = target;
            break;
        case W8_ACTION_USE_ITEM:
            row.remembered.item_target = target;
            break;
        case W8_ACTION_BREATHE:
            row.remembered.breath_target = target;
            break;
        default:
            break;
        }
    }
    return true;
}

bool W8Party::canAttack(unsigned int slot) const
{
    return slot < W8_PARTY_SLOT_COUNT && members[slot] && characters[slot].hp_current != 0 &&
           characters[slot].highest_condition <= W8_CONDITION_INSANE &&
           characters[slot].Hand[0].in_play;
}

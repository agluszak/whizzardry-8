#include "wiz8/rules/party.h"

#include <array>
#include <cassert>
#include <optional>

#include <fmt/printf.h>

#include "wiz8/data/game_data.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/party_formation.h"
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

void W8Party::acceptFormation(W8PartyFormationState& proposal, const GameData& data, Events& events,
                              bool announce_moves)
{
    Events::Operation outcomes{events};
    W8PartyFormationState accepted;
    accepted.positions = formation.positions;
    accepted.unknown_74 = formation.unknown_74;
    const auto notice = [&](unsigned int slot, unsigned int text, int row) {
        events.push(
            CharacterNotice{slot, fmt::sprintf(data.strings[text], kFormationRowNames[row])});
    };
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        if (!canChangeFormation(slot)) continue;
        const auto cell = proposal.cellOf(slot);
        const bool moved = proposal.rowOf(slot) != formation.rowOf(slot);
        accepted.setPosition(slot, cell, false, moved);
        if (moved && cell && announce_moves) notice(slot, 0x24c, cell->row);
    }
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        if (!members[slot] || canChangeFormation(slot) ||
            characters[slot].highest_condition >= W8_CONDITION_DEAD)
            continue;
        const auto previous = formation.cellOf(slot);
        assert(previous);
        const bool moved = proposal.rowOf(slot) != previous->row;
        if (moved) events.push(CharacterNotice{slot, data.strings[0x248]});
        if (accepted.rowOccupants(previous->row) < W8_FORMATION_COLUMNS) {
            auto cell = *previous;
            if (accepted.occupant(cell.row, cell.column)) {
                cell.column = 0;
                while (accepted.occupant(cell.row, cell.column)) ++cell.column;
            }
            accepted.setPosition(slot, cell, false, false);
            if (moved) notice(slot, 0x249, cell.row);
            continue;
        }
        bool placed = false;
        for (int row : {4, 2, 3}) {
            for (int column = 0; column < W8_FORMATION_COLUMNS; ++column) {
                if (accepted.occupant(row, column)) continue;
                accepted.setPosition(slot, W8FormationCell{row, column}, false, true);
                notice(slot, 0x24a, row);
                placed = true;
                break;
            }
            if (placed) break;
        }
        assert(placed);
    }
    for (int row = 0; row < W8_FORMATION_ROWS; ++row) accepted.compactRow(row);
    formation = proposal = accepted;
    events.push(FormationChanged{});
}

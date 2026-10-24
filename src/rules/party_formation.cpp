#include "wiz8/rules/party_formation.h"

#include <algorithm>
#include <cassert>
#include <optional>
#include <span>
#include <stdexcept>

#include "wiz8/rules/party_slot.h"

namespace {
/* The way a row faces: the front and center rows face forward, the others
   face their own side. */
signed char RowFacing(int row)
{
    return row == 4 ? 0 : static_cast<signed char>(row);
}
}

std::optional<int> W8PartyFormationState::occupant(int row, int column) const
{
    const signed char slot = cells[row][column];
    return slot == -1 ? std::nullopt : std::optional<int>(slot);
}

std::optional<W8FormationCell> W8PartyFormationState::cellOf(int slot) const
{
    for (int row = 0; row < W8_FORMATION_ROWS; ++row)
        for (int column = 0; column < W8_FORMATION_COLUMNS; ++column)
            if (cells[row][column] == slot) return W8FormationCell{row, column};
    return std::nullopt;
}

int W8PartyFormationState::rowOf(int slot) const
{
    const auto cell = cellOf(slot);
    return cell ? cell->row : -1;
}

int W8PartyFormationState::rowOccupants(int row) const
{
    return static_cast<int>(
        std::ranges::count_if(cells[row], [](signed char slot) { return slot != -1; }));
}

void W8PartyFormationState::compactRow(int row)
{
    switch (rowOccupants(row)) {
    case 1:
        for (int column = 0; column < W8_FORMATION_COLUMNS; ++column) {
            if (const auto slot = occupant(row, column)) {
                setPosition(*slot, W8FormationCell{row, 0}, false, true);
                return;
            }
        }
        break;
    case 2: {
        int last_free = -1;
        for (int column = W8_FORMATION_COLUMNS - 1; column >= 0; --column) {
            const auto slot = occupant(row, column);
            if (!slot) {
                last_free = column;
            } else if (last_free != -1) {
                setPosition(*slot, W8FormationCell{row, last_free}, false, true);
                last_free = column;
            }
        }
        break;
    }
    }
}

// FUNCTION: WIZ8 0x00554bd0
void W8PartyFormationState::setPosition(int slot, std::optional<W8FormationCell> cell, bool detach,
                                        bool update_facing)
{
    const auto old_cell = cellOf(slot);
    if (old_cell) cells[old_cell->row][old_cell->column] = -1;
    if (cell) {
        auto& destination = cells[cell->row][cell->column];
        if (destination != -1) {
            throw std::logic_error("A formation member entered an occupied cell");
        }
        destination = static_cast<signed char>(slot);
        if (update_facing) positions[slot].facing = RowFacing(cell->row);
    }
    if (detach && old_cell) {
        compactRow(old_cell->row);
    }
}

void W8PartyFormationState::swap(int slot_a, int slot_b)
{
    const auto cell_a = cellOf(slot_a);
    const auto cell_b = cellOf(slot_b);
    if (cell_a) cells[cell_a->row][cell_a->column] = static_cast<signed char>(slot_b);
    if (cell_b) cells[cell_b->row][cell_b->column] = static_cast<signed char>(slot_a);
    if (cell_a) positions[slot_b].facing = RowFacing(cell_a->row);
    if (cell_b) positions[slot_a].facing = RowFacing(cell_b->row);
}

void W8PartyFormationState::reconcile(std::span<const std::optional<W8PartyMember>, 8> members)
{
    for (int slot = 0; slot < 8; ++slot)
        if (!members[slot] && cellOf(slot)) setPosition(slot, std::nullopt, true, true);
}

bool W8PartyFormationState::seatInRow(int slot, int row)
{
    const int count = rowOccupants(row);
    if (count == W8_FORMATION_COLUMNS) return false;
    int column = 0;
    if (count == 1) {
        const auto leader = occupant(row, 0);
        assert(leader);
        setPosition(*leader, W8FormationCell{row, 1}, false, true);
        column = 2;
    } else {
        assert(!occupant(row, 0));
    }
    setPosition(slot, W8FormationCell{row, column}, true, true);
    return true;
}

void W8PartyFormationState::placeCharacter(int slot)
{
    setPosition(slot, std::nullopt, false, false);
    for (int row : {0, 4, 2})
        if (seatInRow(slot, row)) return;
    throw std::logic_error("A party formation has no free seat");
}

bool W8PartyFormationState::swapOrRestore(int slot, int other)
{
    assert(slot != other);
    if (cellOf(other)) {
        swap(slot, other);
        return true;
    }
    const int row = positions[other].revival_row;
    return row == -1 || seatInRow(slot, row);
}

void W8PartyFormationState::updateLiveness(int slot, bool dead)
{
    const int row = rowOf(slot);
    auto& history = positions[slot].revival_row;
    if (dead) {
        if (row != -1) {
            history = static_cast<signed char>(row);
            setPosition(slot, std::nullopt, true, true);
        }
        return;
    }
    if (row != -1) return;
    if (history == -1 || !seatInRow(slot, history)) placeCharacter(slot);
    history = -1;
}

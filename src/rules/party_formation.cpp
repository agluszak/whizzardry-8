#include "wiz8/rules/party_formation.h"

#include <algorithm>
#include <iterator>
#include <span>
#include <stdexcept>

#include "wiz8/rules/party_slot.h"

W8PartyFormationState::W8PartyFormationState()
    : bOccupantChar{}, ubQuadrantOccupants{}, positions{}, unknown_74{}
{
    for (auto& row : bOccupantChar) std::fill(std::begin(row), std::end(row), -1);
    for (auto& position : positions) {
        position.bQuadrant = -1;
        position.bOldQuadrant = -1;
        position.bQuadrantSlot = -1;
    }
}

void W8PartyFormationState::compactRow(unsigned char row)
{
    signed char last_free = -1;
    signed char column;
    int slot;

    switch (ubQuadrantOccupants[row]) {
    case 1:
        for (column = 0; column < 3; ++column) {
            slot = bOccupantChar[row][column];
            if (slot != -1) {
                setPosition(slot, row, 0, false, true);
                return;
            }
        }
        break;
    case 2:
        for (column = 2; column >= 0; --column) {
            slot = bOccupantChar[row][column];
            if (slot == -1) {
                last_free = column;
            } else if (last_free != -1) {
                setPosition(slot, row, last_free, false, true);
                last_free = column;
            }
        }
        break;
    }
}

// FUNCTION: WIZ8 0x00554bd0
void W8PartyFormationState::setPosition(int slot, signed char new_row, signed char new_column,
                                        bool detach, bool update_facing)
{
    W8PartyFormationPosition* position = &positions[slot];
    signed char old_row = position->bQuadrant;
    signed char old_column = position->bQuadrantSlot;

    if (old_row != -1) {
        signed char* occupant = &bOccupantChar[old_row][old_column];
        if (*occupant == -1) {
            throw std::logic_error("A formation member left an empty cell");
        }
        if (ubQuadrantOccupants[old_row] == 0) {
            throw std::logic_error("A formation member left an empty row");
        }
        *occupant = -1;
        --ubQuadrantOccupants[old_row];
    }
    position->bQuadrant = new_row;
    position->bQuadrantSlot = new_column;
    if (new_row != -1) {
        signed char* occupant = &bOccupantChar[new_row][new_column];
        if (*occupant != -1) {
            throw std::logic_error("A formation member entered an occupied cell");
        }
        if (ubQuadrantOccupants[new_row] >= 3) {
            throw std::logic_error("A formation member entered a full row");
        }
        *occupant = static_cast<signed char>(slot);
        ++ubQuadrantOccupants[new_row];
        if (update_facing) {
            switch (new_row) {
            case 0:
            case 4:
                position->facing = 0;
                break;
            case 1:
                position->facing = 1;
                break;
            case 2:
                position->facing = 2;
                break;
            case 3:
                position->facing = 3;
                break;
            }
        }
    }
    if (detach && old_row != -1) {
        compactRow(old_row);
    }
}

void W8PartyFormationState::reconcile(std::span<const W8PartySlotRow, 8> rows)
{
    for (int slot = 0; slot < 8; ++slot)
        if (!rows[slot].fOccupied && positions[slot].bQuadrant != -1 &&
            positions[slot].bQuadrantSlot != -1)
            setPosition(slot, -1, -1, true, true);
}

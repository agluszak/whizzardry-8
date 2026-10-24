#ifndef WIZ8_LAYOUTS_PARTY_FORMATION_H
#define WIZ8_LAYOUTS_PARTY_FORMATION_H

#include <array>
#include <optional>
#include <span>

#include "wiz8/rules/party_slot.h"

enum { W8_FORMATION_ROWS = 5, W8_FORMATION_COLUMNS = 3 };

struct W8FormationCell {
    int row;
    int column;

    bool operator==(const W8FormationCell&) const = default;
};

/* A slot's formation state other than where it stands; the cell grid is the
   only record of placement. */
struct W8PartyFormationPosition {
    /* The row remembered while the slot is unseated by death; the revival
       path re-seats into it and then clears it. */
    signed char revival_row = -1;
    signed char facing = 0;
    /* Saved bytes with no known meaning, kept for round trips. */
    std::array<unsigned char, 8> unknown_04{};

    bool operator==(const W8PartyFormationPosition&) const = default;
};

inline constexpr std::array<const char*, 5> kFormationRowNames = {"Front", "Right", "Rear", "Left",
                                                                  "Center"};

struct W8PartyFormationState {
    /* The party slot seated in a cell, if any. */
    std::optional<int> occupant(int row, int column) const;
    std::optional<W8FormationCell> cellOf(int slot) const;
    /* The row the slot stands in, -1 while unseated. */
    int rowOf(int slot) const;
    int rowOccupants(int row) const;

    /* Seat the slot in a cell, or unseat it. `detach` re-packs the row it
       left and `update_facing` turns it the way its new row faces. */
    void setPosition(int slot, std::optional<W8FormationCell> cell, bool detach,
                     bool update_facing);
    // Complete seating operations own row packing and facing together.
    bool seatInRow(int slot, int row);
    void placeCharacter(int slot);
    void updateLiveness(int slot, bool dead);
    /* Exchange two slots' cells; each seated slot faces the way its new row does. */
    void swap(int slot_a, int slot_b);
    /* A lone occupant moves to column 0, two pack toward column 2. */
    void compactRow(int row);
    // Unseat every slot that has no member.
    void reconcile(std::span<const std::optional<W8PartyMember>, 8> members);

    /* The party slot seated in each row's cells, -1 for an empty cell. */
    std::array<std::array<signed char, W8_FORMATION_COLUMNS>, W8_FORMATION_ROWS> cells = [] {
        std::array<std::array<signed char, W8_FORMATION_COLUMNS>, W8_FORMATION_ROWS> empty;
        for (auto& row : empty) row.fill(-1);
        return empty;
    }();
    std::array<W8PartyFormationPosition, 8> positions{};
    std::array<unsigned char, 0x10> unknown_74{};

    bool operator==(const W8PartyFormationState&) const = default;
};

#endif

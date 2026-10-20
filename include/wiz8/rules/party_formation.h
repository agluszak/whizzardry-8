#ifndef WIZ8_LAYOUTS_PARTY_FORMATION_H
#define WIZ8_LAYOUTS_PARTY_FORMATION_H

#include <optional>
#include <span>

#include "wiz8/rules/party_slot.h"

struct W8PartyFormationPosition {
    /* The row the slot stands in, -1 while unseated. */
    signed char bQuadrant;
    /* The quadrant remembered while the slot is unseated by death; the
       revival path in UpdateFormationSlotState re-seats into it and then
       clears it. */
    signed char bOldQuadrant;
    signed char bQuadrantSlot;
    signed char facing;
    unsigned char unknown_04[8];

    bool operator==(const W8PartyFormationPosition&) const = default;
};

struct W8PartyFormationState {
    W8PartyFormationState();
    void setPosition(int slot, signed char row, signed char column, bool detach,
                     bool update_facing);
    void compactRow(unsigned char row);
    // Unseat every slot that has no member.
    void reconcile(std::span<const std::optional<W8PartyMember>, 8> members);

    signed char bOccupantChar[5][3];
    unsigned char ubQuadrantOccupants[5]; /* 0x0f */
    W8PartyFormationPosition positions[8];
    unsigned char unknown_74[0x10];

    bool operator==(const W8PartyFormationState&) const = default;
};

#endif

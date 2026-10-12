#ifndef WIZ8_LAYOUTS_PARTY_FORMATION_H
#define WIZ8_LAYOUTS_PARTY_FORMATION_H

#include <cstdint>
#include <stddef.h>

#pragma pack(push, 1)

struct W8PartyFormationPosition {
    /* The row the slot stands in, -1 while unseated. */
    std::int8_t bQuadrant;
    /* The quadrant remembered while the slot is unseated by death; the
       revival path in UpdateFormationSlotState re-seats into it and then
       clears it. */
    std::int8_t bOldQuadrant;
    std::int8_t bQuadrantSlot;
    std::int8_t facing;
    std::uint8_t unknown_04[8];
};

struct W8PartyFormationState {
    std::int8_t bOccupantChar[5][3];
    std::uint8_t ubQuadrantOccupants[5]; /* 0x0f */
    W8PartyFormationPosition positions[8];
    std::uint8_t unknown_74[0x10];
};

#pragma pack(pop)

static_assert(sizeof(W8PartyFormationPosition) == 0x0c, "W8PartyFormationPosition_must_be_0x0c");
static_assert(sizeof(W8PartyFormationState) == 0x84, "W8PartyFormationState_must_be_0x84");

#endif

#pragma once


#include <cstdint>
// Dice also occur at odd offsets inside packed database records.
#pragma pack(push, 1)
struct W8Dice {
    /* Smallest and largest results of `count` d`sides` plus `base`. */
    std::int32_t Minimum() const
    {
        return base + count;
    }
    std::int32_t Maximum() const
    {
        return base + count * sides;
    }

    std::int16_t base;
    std::uint8_t count;
    std::uint8_t sides;
};
#pragma pack(pop)

static_assert(sizeof(W8Dice) == 4, "W8Dice_must_be_4");

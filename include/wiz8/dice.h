#pragma once

#ifdef WIZ8_NATIVE
// Dice also occur at odd offsets inside packed database records.
#pragma pack(push, 1)
#endif
struct W8Dice {
    /* Smallest and largest results of `count` d`sides` plus `base`. */
    int Minimum() const
    {
        return base + count;
    }
    int Maximum() const
    {
        return base + count * sides;
    }

    short base;
    unsigned char count;
    unsigned char sides;
};
#ifdef WIZ8_NATIVE
#pragma pack(pop)
#endif

static_assert(sizeof(W8Dice) == 4, "W8Dice_must_be_4");

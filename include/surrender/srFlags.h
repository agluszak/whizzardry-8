#pragma once


#include <cstdint>
template <class Enum> class srFlags {
public:
    srFlags();
    explicit srFlags(std::uint32_t bits) : value(bits) {}

    void set(std::int32_t bit, std::int32_t on);

    std::uint32_t value;
};

template <class Enum> srFlags<Enum>::srFlags() : value(0) {}

/* Set or clear one flag bit. */
template <class Enum> void srFlags<Enum>::set(std::int32_t bit, std::int32_t on)
{
    if (on != 0) {
        value |= 1 << bit;
        return;
    }
    value &= ~(1u << bit);
}


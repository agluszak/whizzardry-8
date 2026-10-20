#pragma once

#include <cstdint>
#include <source_location>
#include <vector>

#include "wiz8/data/dice.h"

/* A random stream: the MSVC 32-bit linear congruential sequence with 15-bit draws, which
   retail's rand() produced. The game's stream is `Game::rng`; presentation has its own
   (`PresentationRng()`), so what is drawn or played never moves a game draw. */
class Rng {
public:
    // One draw as a trace records it: the range asked for (zero for a raw 15-bit draw).
    struct Draw {
        std::uint32_t range = 0;
        std::uint32_t value = 0;
        std::source_location where;
    };

    // Retail's rand() starts at one until it is seeded.
    constexpr Rng() = default;
    constexpr explicit Rng(std::uint32_t value) : state_(value) {}

    constexpr void seed(std::uint32_t value) { state_ = value; }

    // A raw draw in [0, 0x7fff].
    std::uint32_t next(std::source_location where = std::source_location::current())
    {
        return record(0, step(), where);
    }

    // A draw in [0, range); an empty range returns zero without consuming a draw.
    std::uint32_t below(std::uint32_t range,
                        std::source_location where = std::source_location::current())
    {
        if (range == 0) return 0;
        const auto value =
            static_cast<std::uint32_t>(std::uint64_t{step()} * range / 0x7fff % range);
        return record(range, value, where);
    }

    // A percentage roll. It consumes a draw even when the chance is certain.
    bool chance(std::uint32_t percent, std::source_location where = std::source_location::current())
    {
        return below(100, where) < percent;
    }

    /* Appends every following draw to `log`, or stops recording when null. Determinism
       tests use it to catch a change that reorders, adds or drops draws. */
    void trace(std::vector<Draw>* log) { trace_ = log; }

private:
    constexpr std::uint32_t step()
    {
        state_ = state_ * 214013u + 2531011u;
        return (state_ >> 16) & 0x7fff;
    }

    std::uint32_t record(std::uint32_t range, std::uint32_t value, std::source_location where)
    {
        if (trace_) trace_->push_back({range, value, where});
        return value;
    }

    std::uint32_t state_ = 1;
    std::vector<Draw>* trace_ = nullptr;
};

// `count` d`sides` plus `base`, one draw per die.
inline int RollDice(Rng& rng, const W8Dice& dice)
{
    int result = dice.base;
    for (unsigned int roll = 0; roll < dice.count; ++roll)
        result += static_cast<int>(rng.below(dice.sides) + 1);
    return result;
}

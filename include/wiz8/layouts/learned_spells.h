#pragma once

#include <cstdint>
#include <stddef.h>

/* Shared learned-spell state embedded in camp UI. */
struct W8LearnedSpellState {
    std::int32_t spell_ids_by_realm[6][40];
    std::int32_t scroll[6];
    std::int32_t learned_total;
};

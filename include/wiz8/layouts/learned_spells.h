#pragma once

#include <stddef.h>

/* Shared learned-spell state embedded in camp UI. */
struct W8LearnedSpellState {
    int spell_ids_by_realm[6][40];
    int scroll[6];
    int learned_total;
};

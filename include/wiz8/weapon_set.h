#pragma once
#include <array>
struct W8ItemInstance;

// Borrowed proposed equipment; evaluation never moves the live item records.
struct W8WeaponSwapPlan {
    std::array<bool, 2> swap_hands{};
    std::array<const W8ItemInstance*, 2> weapons{};
};

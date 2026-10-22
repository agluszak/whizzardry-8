#pragma once
#include <optional>
#include <utility>

#include "wiz8/combat/combat.h"
#include "wiz8/game.h"

// The current game's fight replaced for a test, and restored afterwards.
struct ScopedCombat {
    std::optional<Combat> saved = std::exchange(CurrentGame().combat, std::nullopt);
    // A fight in progress, or none.
    explicit ScopedCombat(bool fighting = false)
    {
        if (fighting) CurrentGame().combat.emplace();
    }
    ~ScopedCombat() { CurrentGame().combat = std::move(saved); }
    ScopedCombat(const ScopedCombat&) = delete;
    ScopedCombat& operator=(const ScopedCombat&) = delete;
};

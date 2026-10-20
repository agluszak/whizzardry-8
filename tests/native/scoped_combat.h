#pragma once
#include <memory>
#include <utility>

#include "wiz8/game.h"
#include "wiz8/layouts/combat_state.h"

// A combat state for the current game, restored afterwards.
struct ScopedCombat {
    W8CombatLifecycle saved = std::move(CurrentGame().combat);
    explicit ScopedCombat(bool active = false)
    {
        auto& combat = CurrentGame().combat;
        combat.state_ = std::make_unique<W8CombatState>();
        combat.phase_ = active ? W8CombatLifecycle::Phase::Active : W8CombatLifecycle::Phase::Idle;
    }
    ~ScopedCombat() { CurrentGame().combat = std::move(saved); }
    void BeginRestoration() { CurrentGame().combat.phase_ = W8CombatLifecycle::Phase::Restoring; }
};

#pragma once
#include <memory>
#include <utility>

#include "wiz8/layouts/combat_state.h"

struct ScopedCombat {
    W8CombatLifecycle saved = std::move(g_combat);
    explicit ScopedCombat(bool active = false)
    {
        g_combat.state_ = std::make_unique<W8CombatState>();
        g_combat.phase_ =
            active ? W8CombatLifecycle::Phase::Active : W8CombatLifecycle::Phase::Idle;
    }
    ~ScopedCombat() { g_combat = std::move(saved); }
    void BeginRestoration() { g_combat.phase_ = W8CombatLifecycle::Phase::Restoring; }
};

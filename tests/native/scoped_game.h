#pragma once

#include "wiz8/game.h"

// A game in progress for the scope's duration.
struct ScopedGame {
    Game& game = BeginGame();
    ScopedGame() = default;
    ScopedGame(const ScopedGame&) = delete;
    ScopedGame& operator=(const ScopedGame&) = delete;
    ~ScopedGame() { EndGame(); }
};

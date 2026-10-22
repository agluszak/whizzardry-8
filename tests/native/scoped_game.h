#pragma once

#include "wiz8/game.h"

// A game in progress for the scope's duration, in a session of its own.
struct ScopedGame {
    GameSession session;
    Game& game = session.begin();
    ScopedGame() = default;
    ScopedGame(const ScopedGame&) = delete;
    ScopedGame& operator=(const ScopedGame&) = delete;
};

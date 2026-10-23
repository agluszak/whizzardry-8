#pragma once

#include "wiz8/data/game_data.h"
#include "wiz8/game.h"

/* A game in progress for the scope's duration, in a session of its own, reading `data`,
   which the caller keeps alive longer; by default it reads no data. */
struct ScopedGame {
    explicit ScopedGame(const GameData& data = NoData()) : session(data) {}
    ScopedGame(const ScopedGame&) = delete;
    ScopedGame& operator=(const ScopedGame&) = delete;
    static const GameData& NoData()
    {
        static const GameData empty;
        return empty;
    }
    GameSession session;
    Game& game = session.begin();
};

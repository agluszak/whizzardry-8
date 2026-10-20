#pragma once

#include <utility>

#include "wiz8/data/game_data.h"
#include "wiz8/loaded_game_data.h"

// Owns and installs a test's game data while it runs, then reinstalls the previous data.
class ScopedGameData {
public:
    explicit ScopedGameData(GameData data)
        : data_(std::move(data)), previous_(InstallGameData(&data_))
    {
    }
    ScopedGameData(const ScopedGameData&) = delete;
    ScopedGameData& operator=(const ScopedGameData&) = delete;
    ScopedGameData(ScopedGameData&&) = delete;
    ScopedGameData& operator=(ScopedGameData&&) = delete;
    ~ScopedGameData() { InstallGameData(previous_); }

private:
    GameData data_;
    const GameData* previous_;
};

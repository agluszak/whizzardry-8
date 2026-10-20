#pragma once

#include <utility>

#include "wiz8/data/game_data.h"
#include "wiz8/loaded_game_data.h"

// Installs a test's game data while it runs and restores the previously installed data.
class ScopedGameData {
public:
    explicit ScopedGameData(GameData data) : previous_(InstallGameData(std::move(data))) {}
    ScopedGameData(const ScopedGameData&) = delete;
    ScopedGameData& operator=(const ScopedGameData&) = delete;
    ScopedGameData(ScopedGameData&&) = delete;
    ScopedGameData& operator=(ScopedGameData&&) = delete;
    ~ScopedGameData() { InstallGameData(std::move(previous_)); }

private:
    GameData previous_;
};

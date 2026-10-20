#include "wiz8/game.h"

#include <optional>
#include <stdexcept>

namespace {
std::optional<Game> current_game;
}

// Finish speech first: its voices complete into the queue and the characters it names.
Game::~Game()
{
    speech.DestroyAllEvents();
    for (auto& entry : monster_manager_entries) entry.mouth_gap = {};
}

Game& CurrentGame()
{
    if (!current_game) throw std::logic_error("No game is in progress");
    return *current_game;
}

bool GameInProgress()
{
    return current_game.has_value();
}

bool GameStarted()
{
    return current_game && current_game->status.game_started;
}

Game& BeginGame()
{
    current_game.reset();
    return current_game.emplace();
}

void EndGame()
{
    current_game.reset();
}

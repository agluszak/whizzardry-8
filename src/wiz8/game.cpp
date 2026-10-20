#include "wiz8/game.h"

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

#include "wiz8/combat/entity_signals.h"
#include "wiz8/rules/events.h"
#include "wiz8/world/world.h"

namespace {
std::optional<Game> current_game;
std::function<void(const Event&)> game_event_observer;
}

Level::Level() = default;

// The sky goes before the level it is drawn behind, each leaving its slot first.
Level::~Level()
{
    replaceSky(nullptr);
    replacePrimary(nullptr);
}

std::unique_ptr<World> Level::replacePrimary(std::unique_ptr<World> world)
{
    if (world) world->simulation = &CombatEntitySignals();
    return std::exchange(primary, std::move(world));
}

std::unique_ptr<World> Level::replaceSky(std::unique_ptr<World> world)
{
    return std::exchange(sky, std::move(world));
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
    auto& game = current_game.emplace();
    game.events.observe(game_event_observer);
    return game;
}

void EndGame()
{
    current_game.reset();
}

void SetGameEventObserver(std::function<void(const Event&)> observer)
{
    game_event_observer = std::move(observer);
    if (current_game) current_game->events.observe(game_event_observer);
}

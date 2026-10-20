#include "wiz8/game.h"

#include <cstdint>
#include <ctime>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

#include "wiz8/combat/entity_signals.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/random.h"
#include "wiz8/script_context.h"
#include "wiz8/world/world.h"

namespace {
std::optional<Game> current_game;
// The stream the next game continues while no game is in progress.
Rng idle_stream(static_cast<std::uint32_t>(std::time(nullptr)));
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
    EndGame();
    auto& game = current_game.emplace();
    game.rng = idle_stream;
    game.events.observe(game_event_observer);
    return game;
}

void EndGame()
{
    if (current_game) idle_stream = current_game->rng;
    current_game.reset();
}

void SeedGameRandom(std::uint32_t seed)
{
    (current_game ? current_game->rng : idle_stream).seed(seed);
}

void SetGameEventObserver(std::function<void(const Event&)> observer)
{
    game_event_observer = std::move(observer);
    if (current_game) current_game->events.observe(game_event_observer);
}

ScriptContext MakeScriptContext(Game& game)
{
    return {.party = game.party,
            .story = game.story,
            .status = game.status,
            .activity = game.activity,
            .combat = game.combat,
            .events = game.events,
            .world = game.level.primary.get(),
            .game = game};
}

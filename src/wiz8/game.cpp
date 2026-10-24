#include "wiz8/game.h"

#include <cstdint>
#include <ctime>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

#include "wiz8/audio/soundman.h"
#include "wiz8/combat/entity_signals.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/random.h"
#include "wiz8/script_context.h"
#include "wiz8/world/world.h"

namespace {
// The session `CurrentGame()` reads.
GameSession* current_session = nullptr;
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

World& Level::loaded() const
{
    if (!primary) throw std::logic_error("a level screen was requested with no level loaded");
    return *primary;
}

std::unique_ptr<World> Level::replaceSky(std::unique_ptr<World> world)
{
    return std::exchange(sky, std::move(world));
}

// NPC voice belongs to its screen presentation; the Al-Sedexus ambience stops with the game.
Game::~Game()
{
    for (auto& entry : monster_manager_entries) entry.mouth_gap = {};
    wiz8::sound::stop(sedexus_scene.moaning);
}

GameSession::GameSession(const GameData& data)
    : data_(data), next_stream_(static_cast<std::uint32_t>(std::time(nullptr)))
{
    if (current_session) throw std::logic_error("A game session already exists");
    current_session = this;
}

// Destruction discards the game without departure bookkeeping; callers end it explicitly.
GameSession::~GameSession()
{
    discard();
    current_session = nullptr;
}

Game& GameSession::begin()
{
    // Allocate the next owner before departure. Its address remains stable when
    // published, and failure to construct it leaves the outgoing game intact.
    auto candidate = std::make_unique<Game>(data_);
    candidate->native_object_file = native_object_file;
    end();
    candidate->rng = next_stream_;
    game_ = std::move(candidate);
    game_->events.observe(observer_);
    return *game_;
}

void GameSession::load(SavedGame saved, std::function_ref<void()> release_presentation)
{
    // Ending combat can still present outcomes through the outgoing screen.
    if (game_ && game_->combat) game_->endCombat(true);
    release_presentation();
    try {
        auto& game = begin();
        std::move(saved).install(game);
        if (!game.enterSavedLevel(game.status.resume_level))
            throw std::runtime_error("cannot construct saved level");
    } catch (...) {
        // Never leave a partially entered replacement available to screens.
        end();
        throw;
    }
}

void GameSession::end()
{
    if (!game_) return;
    game_->leaveLevel();
    discard();
}

void GameSession::discard()
{
    if (!game_) return;
    next_stream_ = game_->rng;
    game_.reset();
}

void GameSession::seed(std::uint32_t seed)
{
    (game_ ? game_->rng : next_stream_).seed(seed);
}

void GameSession::observe(std::function<void(const Event&)> observer)
{
    observer_ = std::move(observer);
    if (game_) game_->events.observe(observer_);
}

Game& CurrentGame()
{
    Game* game = current_session ? current_session->game() : nullptr;
    if (!game) throw std::logic_error("No game is in progress");
    return *game;
}

bool GameInProgress()
{
    return current_session && current_session->game();
}

bool GameStarted()
{
    return GameInProgress() && CurrentGame().status.game_started;
}

ScriptContext MakeScriptContext(Game& game)
{
    return {.party = game.party,
            .inventory = game.inventory,
            .story = game.story,
            .status = game.status,
            .activity = game.activity,
            .combat = game.combat,
            .events = game.events,
            .world = game.level.primary.get(),
            .game = game};
}

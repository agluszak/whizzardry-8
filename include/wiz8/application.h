#pragma once

#include <memory>
#include <span>
#include <string>
#include <string_view>

extern "C" {
extern bool gfProgramIsRunning;
extern bool gfApplicationActive;
extern bool gfGameInitialized;

union SDL_Event;
void HandleGameEvent(const SDL_Event& event);
bool PumpGameEvents(bool wait = false);
[[noreturn]] void ShutdownWithErrorBox(std::string_view message);
}

struct GameData;
class GameSession;

namespace wiz8 {
/* The composition root: the platform, the game data, the session's games and the
   screens, which the router hands the session. */
class Application {
public:
    explicit Application(std::span<const std::string_view> arguments = {});
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    void run();
    GameSession& games() { return *games_; }

private:
    void shutdown() noexcept;
    bool sdl_initialized_ = false;
    bool game_started_ = false;
    // Loaded before the games and declared before them, which read it, so it outlives them.
    std::unique_ptr<const GameData> data_;
    std::unique_ptr<GameSession> games_;
};
}

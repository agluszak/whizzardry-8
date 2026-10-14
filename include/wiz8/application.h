#include <string>
#include <string_view>
#pragma once

#include <span>

extern "C" {
extern bool gfProgramIsRunning;
extern bool gfLoadAtStartup;
extern std::string gzStringDataOverride;
extern bool gfUsingBoundsChecker;
extern bool gfCapturingVideo;
extern bool gfApplicationActive;
extern bool gfGameInitialized;
extern bool gfIgnoreMessages;
extern std::string gzErrorMsg;

union SDL_Event;
void HandleGameEvent(const SDL_Event& event);
bool PumpGameEvents(bool wait = false);
[[noreturn]] void ShutdownWithErrorBox(std::string_view message);
}

namespace wiz8 {
class Application {
public:
    explicit Application(std::span<const std::string_view> arguments = {});
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    void run();

private:
    void shutdown() noexcept;
    bool sdl_initialized_ = false;
    bool game_started_ = false;
    size_t initialized_screens_ = 0;
};
} // namespace wiz8

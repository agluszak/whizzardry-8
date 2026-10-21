#include "wiz8/application.h"

#include <cstddef>
#include <exception>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_stdinc.h>

#include "asset_paths.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/data/game_data.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/filesystem.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/game_init.h"
#include "wiz8/render/frame.h"
#include "wiz8/screen.h"

namespace {

struct LaunchOptions {
    bool sound = true;
    bool inspector = false;
    bool load_at_startup = false;
    bool windowed = false;
    std::string string_data;
    std::string native_object;
};

LaunchOptions ParseArguments(std::span<const std::string_view> arguments)
{
    LaunchOptions options;
    for (size_t index = 0; index < arguments.size(); ++index) {
        const auto separator = arguments[index].find('=');
        const std::string option(arguments[index].substr(0, separator));
        if (SDL_strcasecmp(option.c_str(), "/NOSOUND") == 0)
            options.sound = false;
        else if (SDL_strcasecmp(option.c_str(), "/INSPECTOR") == 0)
            options.inspector = true;
        else if (SDL_strcasecmp(option.c_str(), "/LOAD") == 0)
            options.load_at_startup = true;
        else if (SDL_strcasecmp(option.c_str(), "/WINDOW") == 0)
            options.windowed = true;
        else if (SDL_strcasecmp(option.c_str(), "/STRINGDATA") == 0 ||
                 SDL_strcasecmp(option.c_str(), "/NATIVEOBJECT") == 0) {
            const auto value = separator != std::string_view::npos
                                   ? arguments[index].substr(separator + 1)
                               : ++index < arguments.size() ? arguments[index]
                                                            : std::string_view{};
            if (value.empty()) throw std::invalid_argument(option + " requires a filename");
            if (SDL_strcasecmp(option.c_str(), "/NATIVEOBJECT") == 0)
                options.native_object = value;
            else
                options.string_data = value;
        }
    }
    return options;
}
}

wiz8::Application::Application(std::span<const std::string_view> arguments)
{
    if (gfGameInitialized) throw std::logic_error("The game is already running");
    try {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            const std::string failure(SDL_GetError());
            SDL_Quit();
            throw std::runtime_error(failure);
        }
        sdl_initialized_ = true;
        for (const auto* directory :
             {"config", "logs", "saves/characters", "saves/npcs", "saves/import"})
            wiz8::create_directory(wiz8::user_path(directory));
        const auto options = ParseArguments(arguments);
        if (options.inspector) VideoInspectorEnable();
        if (options.windowed) VideoFullScreen(false);
        if (!InitializeVideoManager()) throw std::runtime_error("Video initialization failed");
        if (!wiz8::sound::initialize(options.sound && !SetupDisablesAudio()))
            throw std::runtime_error("Sound initialization failed");
        data_ = std::make_unique<GameData>();
        games_ = std::make_unique<GameSession>();
        games_->native_object_file = wiz8::path_from_utf8(options.native_object);
        game_started_ = true;
        if (!InitializeGame(*data_, *games_, options.load_at_startup, options.string_data))
            throw std::runtime_error("Game initialization failed");
        gfGameInitialized = gfApplicationActive = gfProgramIsRunning = true;
    } catch (...) {
        shutdown();
        throw;
    }
}

wiz8::Application::~Application()
{
    shutdown();
}

void wiz8::Application::run()
{
    while (gfProgramIsRunning) {
        PumpGameEvents(!gfApplicationActive);
        if (!gfProgramIsRunning) break;
        if (gfApplicationActive) {
            GameLoop();
        }
    }
}

void wiz8::Application::shutdown() noexcept
{
    if (!sdl_initialized_) return;
    gfProgramIsRunning = false;
    const bool complete = gfGameInitialized;
    if (complete) {
        try {
            GameloopExit();
        } catch (const std::exception& failure) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Leaving game: %s", failure.what());
        }
    }
    gfGameInitialized = false;
    wiz8::sound::shutdown();
    if (game_started_) {
        try {
            ShutdownGame(*games_, complete);
        } catch (const std::exception& failure) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Game shutdown: %s", failure.what());
        }
    }
    ReleaseCatalogArtwork();
    ShutdownButtonImageManager();
    ReleaseMenuFonts();
    ShutdownVideoManager();
    SDL_ShowCursor();
    gfApplicationActive = false;
    SDL_Quit();
    sdl_initialized_ = false;
}

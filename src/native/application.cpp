#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_stdinc.h>
#include <stdexcept>
#include <cstddef>
#include <exception>
#include <span>
#include <string>
#include "wiz8/fonts.h"
#include "wiz8/catalog_artwork.h"
#include <string_view>

#include "wiz8/application.h"
#include "wiz8/engine_code/Video2.h"
#include "input.h"
#include "Font.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "soundman.h"
#include "random.h"
#include "wiz8/filesystem.h"
#include "wiz8/game_init.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/engine_code/Octree.h"

namespace {

struct LaunchOptions {
    bool sound = true;
    bool inspector = false;
    bool load_at_startup = false;
    bool windowed = false;
    bool bounds_checker = false;
    bool capture = false;
    bool no_octree = false;
    std::string video_config;
    std::string string_data;
};

LaunchOptions ParseArguments(std::span<const std::string_view> arguments)
{
    LaunchOptions options;
    for (size_t index = 0; index < arguments.size(); ++index) {
        const auto separator = arguments[index].find('=');
        const std::string option(arguments[index].substr(0, separator));
        if (SDL_strcasecmp(option.c_str(), "/NOSOUND") == 0) {
            options.sound = false;
        } else if (SDL_strcasecmp(option.c_str(), "/INSPECTOR") == 0) {
            options.inspector = true;
        } else if (SDL_strcasecmp(option.c_str(), "/LOAD") == 0) {
            options.load_at_startup = true;
        } else if (SDL_strcasecmp(option.c_str(), "/WINDOW") == 0) {
            options.windowed = true;
        } else if (SDL_strcasecmp(option.c_str(), "/BC") == 0) {
            options.bounds_checker = true;
        } else if (SDL_strcasecmp(option.c_str(), "/CAPTURE") == 0) {
            options.capture = true;
        } else if (SDL_strcasecmp(option.c_str(), "/NOOCT") == 0) {
            options.no_octree = true;
        } else if (SDL_strcasecmp(option.c_str(), "/VIDEOCFG") == 0 ||
                   SDL_strcasecmp(option.c_str(), "/STRINGDATA") == 0) {
            const auto value = separator != std::string_view::npos
                                   ? arguments[index].substr(separator + 1)
                               : ++index < arguments.size() ? arguments[index]
                                                            : std::string_view{};
            if (value.empty()) {
                throw std::invalid_argument(option + " requires a filename");
            }
            if (SDL_strcasecmp(option.c_str(), "/VIDEOCFG") == 0) {
                options.video_config = value;
            } else {
                options.string_data = value;
            }
        }
    }
    return options;
}
} // namespace

wiz8::Application::Application(std::span<const std::string_view> arguments)
{
    if (gfGameInitialized) {
        throw std::logic_error("The game is already running");
    }
    try {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            const std::string failure(SDL_GetError());
            SDL_Quit();
            throw std::runtime_error(failure);
        }
        sdl_initialized_ = true;
        gfIgnoreMessages = false;
        gzErrorMsg.clear();
        const auto options = ParseArguments(arguments);
        gfLoadAtStartup = options.load_at_startup;
        gfUsingBoundsChecker = options.bounds_checker;
        gfCapturingVideo = options.capture;
        gzStringDataOverride = options.string_data;
        if (options.inspector) {
            VideoInspectorEnable();
        }
        if (options.windowed) {
            VideoFullScreen(false);
        }
        if (options.no_octree) {
            NoOct();
        }
        if (!options.video_config.empty()) {
            VideoSetConfigFile(options.video_config.c_str());
        }
        wiz8::refresh_asset_archives();
        if (!InitializeInputManager()) {
            throw std::runtime_error("Input initialization failed");
        }
        if (!InitializeVideoManager()) {
            throw std::runtime_error("Video initialization failed");
        }
        ConfigureFontRendering(CreateEnglishTransTable());
        if (!wiz8::sound::initialize(options.sound && !SetupDisablesAudio())) {
            throw std::runtime_error("Sound initialization failed");
        }
        InitializeRandom();
        game_started_ = true;
        if (!InitializeGame(initialized_screens_)) {
            throw std::runtime_error("Game initialization failed");
        }
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
        if (!gfProgramIsRunning) {
            break;
        }
        if (gfApplicationActive) {
            GameLoop();
            GetInputState().received = false;
        }
    }
}

void wiz8::Application::shutdown() noexcept
{
    if (!sdl_initialized_) {
        return;
    }
    gfProgramIsRunning = false;
    const bool complete = gfGameInitialized;
    if (complete) {
        try {
            bool release_screens = true;
#ifdef _DEBUG
            release_screens = !gfIgnoreMessages;
#endif
            GameloopExit(release_screens);
        } catch (const std::exception& failure) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Leaving game: %s", failure.what());
        }
    }
    gfGameInitialized = false;
    wiz8::sound::shutdown();
    if (game_started_) {
        try {
            ShutdownGame(initialized_screens_, complete);
        } catch (const std::exception& failure) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Game shutdown: %s", failure.what());
        }
    }
    ReleaseCatalogArtwork();
    ShutdownButtonImageManager();
    ReleaseMenuFonts();
    ResetFontRendering();
    ShutdownVideoManager();
    wiz8::clear_asset_archives();
    SDL_ShowCursor();
    VideoDumpMemoryLeaks();
    gfApplicationActive = false;
    SDL_Quit();
    sdl_initialized_ = false;
}

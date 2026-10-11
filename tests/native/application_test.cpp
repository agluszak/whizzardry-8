#include <catch2/catch_test_macros.hpp>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_stdinc.h>
#include <array>
#include <filesystem>
#include <stdexcept>
#include <span>
#include <string>
#include <string_view>

#include "wiz8/application.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/asset_paths.h"
#include "temporary_directory.h"
#include "scoped_asset_roots.h"
#include "wiz8/regions.h"


static void CheckStopped()
{
    REQUIRE(SDL_WasInit(0) == 0);
    REQUIRE((!gfProgramIsRunning && !gfGameInitialized && !gfApplicationActive));
    REQUIRE(ghWindow == nullptr);
    REQUIRE(LiveDynamicRegionCount() == 0);
}

TEST_CASE("application_test")
{
    ScopedTemporaryDirectory directory{"wiz8-application"};
    auto roots = w8_native::path_roots();
    roots.assets = (directory.path / "missing-assets").string();
    roots.user = directory.path.string();
    ScopedAssetRoots scoped_roots{roots};

    for (const auto option : {"/VIDEOCFG", "/STRINGDATA", "/VIDEOCFG="}) {
        const std::array<std::string_view, 1> arguments{option};
        bool rejected = false;
        try {
            wiz8::Application application(arguments);
        } catch (const std::invalid_argument& failure) {
            rejected = std::string_view(failure.what()).find("requires a filename") !=
                std::string_view::npos;
        }
        REQUIRE(rejected);
        CheckStopped();
    }

    const std::array<std::string_view, 4> arguments{
        "/STRINGDATA=string data.wiz", "/window", "/vIdEoCfG", "C:\\missing video.cfg"};
    for (int attempt = 0; attempt < 2; ++attempt) {
        bool failed = false;
        try {
            wiz8::Application application(arguments);
        } catch (const std::runtime_error&) {
            failed = true;
        }
        REQUIRE(failed);
        REQUIRE(gzStringDataOverride == "string data.wiz");
        REQUIRE(std::string_view(VideoGetConfigFile()) == "C:\\missing video.cfg");
        CheckStopped();
    }

    REQUIRE(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SDL_VIDEODRIVER",
                                     "wiz8-invalid-driver", true));
    bool failed = false;
    try {
        wiz8::Application application;
    } catch (const std::runtime_error&) {
        failed = true;
    }
    REQUIRE(failed);
    CheckStopped();
    REQUIRE(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SDL_VIDEODRIVER", "dummy", true));

    bool reported = false;
    try {
        ShutdownWithErrorBox("native initialization failure");
    } catch (const std::runtime_error& failure) {
        reported = std::string_view(failure.what()) == "native initialization failure";
    }
    REQUIRE((reported && gfIgnoreMessages));
    CheckStopped();


}

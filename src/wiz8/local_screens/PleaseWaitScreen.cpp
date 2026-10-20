#include "wiz8/local_screens/PleaseWaitScreen.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <SDL3/SDL_log.h>
#include <fmt/format.h>

#include "asset_paths.h"
#include "layouts/levels.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/fact_state.h"
#include "wiz8/filesystem.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/save_screenshot.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/music_playlist.h"
#include "wiz8/platform/timer.h"
#include "wiz8/render/frame.h"
#include "wiz8/screen.h"
#include "wiz8/world/level_session.h"

namespace {
class PleaseWaitScreen final : public Screen {
public:
    explicit PleaseWaitScreen(GameSession& games) : games(games) {}
    bool enter(const ScreenRequest& request) override;
    bool resume(const ScreenRequest& request) override;
    void frame() override;
    bool leave(ScreenDeparture departure) override;
    /* Advance the caption animation during a synchronous load. */
    void animate();

private:
    void draw();

    // A load begins a game in it.
    GameSession& games;
    std::string caption;
    W8PleaseWaitMode mode = W8_PLEASE_WAIT_NEW_GAME;
    int parameter = 0;
    int parameter_2 = 0;
    std::string name;
    std::string previous_save_name;
    std::optional<W8SaveScreenshot> save_screenshot;
    std::uint64_t entered_tick = 0;
    int caption_y = 0;
};
} // namespace

/* Engine Code\Levels.cpp owns this with C++ linkage. */

/* One video-object id per level, the backdrop the Please Wait
   screen shows while that level loads. 0x00605820 indexes the level's name in
   the string list. Both are bounded by 0x2F, with 0xE4 as the backdrop the
   frame handler falls back to. */
static int g_level_backdrops[W8_LEVEL_COUNT] = {
    0x1bc, 0x1bd, 0x1be, 0x1bf, 0x1c0, 0x1c2, 0x1c1, 0x1c3, 0x1c4, 0x1c5, 0x1c6, 0xe4,
    0x1c7, 0x1c8, 0x1c9, 0x1ca, 0x1cb, 0x1cc, 0x1cd, 0x1ce, 0x1cf, 0x1d0, 0x1d1, 0xe4,
    0x1d2, 0x1d3, 0x1d4, 0x1d5, 0xe4,  0x1d6, 0xe4,  0x1d7, 0x1d8, 0x1d9, 0xe4,  0xe4,
    0x1da, 0x1db, 0x1dc, 0xe4,  0,     0,     0,     0,     0,     0,     0,
};
/* The path the five assertions carry. */

/* New-game mode seeds the starting facts and deletes the running save; load and
   save modes carry a name; level-change mode carries two parameters. */
bool PleaseWaitScreen::enter(const ScreenRequest& request)
{
    mode = std::get<W8PleaseWaitMode>(request.mode);
    switch (mode) {
    case W8_PLEASE_WAIT_NEW_GAME:
        SeedNewGameFacts(CurrentGame().status);
        parameter = SelectNewGameStartLevel();
        break;
    case W8_PLEASE_WAIT_LOAD_GAME:
        parameter = request.parameter;
        name = request.name;
        /* The loaded game replaces the one in progress, so that game's screens go first:
           a suspended main game leaves its level as it is discarded. */
        DiscardSuspendedScreensAbove(W8_SCREEN_MAIN_MENU);
        break;
    case W8_PLEASE_WAIT_SAVE_GAME:
        parameter = GetLoadedLevelID();
        name = request.name;
        previous_save_name = request.previous_save_name;
        save_screenshot = request.save_screenshot;
        break;
    case W8_PLEASE_WAIT_CHANGE_LEVEL:
        parameter = request.parameter;
        parameter_2 = request.parameter_2;
        break;
    }
    return resume(request);
}

bool PleaseWaitScreen::resume(const ScreenRequest&)
{
    SetUiCanvasAlphaTestEnabled(false);
    DisableCursorScene();
    caption_y = 0;
    entered_tick = MonotonicMilliseconds();
    return true;
}

/* Draw the loading screen's backdrop, progress frame and caption. */
void PleaseWaitScreen::draw()
{
    int backdrop =
        static_cast<unsigned int>(parameter) < W8_LEVEL_COUNT ? g_level_backdrops[parameter] : 0xe4;
    CatalogArtwork::Load(backdrop, 0).draw(GetFrameBufferSurface(), 0, 0, 0);
    CatalogArtwork::Load(0x1de, 0).draw(GetFrameBufferSurface(), 0, 0, 0x1be);
    DrawText(GetFrameBufferSurface(), g_level_load_font, {0x6a, 0x1c7}, caption);
    CatalogArtwork::Load(0x1dd, 0).draw(GetFrameBufferSurface(), caption_y, 0, 0x185);
    ResetTransientRenderScenes();
}

void PleaseWaitScreen::frame()
{
    switch (mode) {
    case W8_PLEASE_WAIT_NEW_GAME:
        caption = Data().strings[0x6ef];
        break;
    case W8_PLEASE_WAIT_LOAD_GAME: {
        const auto& strings = Data().strings;
        caption = name.starts_with("Quick") ? strings[0x6f1] : strings[0x6f0];
    } break;
    case W8_PLEASE_WAIT_SAVE_GAME:
        caption = Data().strings[0x6f2].c_str();
        break;
    case W8_PLEASE_WAIT_CHANGE_LEVEL:
        if (static_cast<unsigned int>(parameter) < W8_LEVEL_COUNT) {
            caption = fmt::format("{} {}...", Data().strings[0x6f3],
                                  Data().strings[g_level_name_indices[parameter]]);
        } else if (parameter == 0x38) {
            caption = "Entering default level...";
        } else {
            caption = fmt::format("Entering test level {:c}...", parameter + 2);
        }
    }

    draw();
    RenderFrame();
    RenderFrame();
    DisableCursorScene();

    switch (mode) {
    case W8_PLEASE_WAIT_NEW_GAME:
        int level;
        int entrance;
        ChooseNewGameStartLocation(&level, &entrance);
        if (!LoadLevel(level, entrance, false))
            throw std::runtime_error(fmt::format("cannot load level {}", level));
        PostNewGameLoad();

        break;
    case W8_PLEASE_WAIT_LOAD_GAME:
        if (!LoadGame(games, name))
            throw std::runtime_error(fmt::format("cannot load saved game {}", name));
        if (!LoadLevel(CurrentGame().status.resume_level, -1, true))
            throw std::runtime_error(
                fmt::format("cannot load level {}", CurrentGame().status.resume_level));

        break;
    case W8_PLEASE_WAIT_SAVE_GAME: {
        const auto& screenshot = save_screenshot;
        const bool saved = SaveGame(name, screenshot ? &*screenshot : nullptr);
        // Do not destroy the previous save until the new one has been committed.
        if (saved && !previous_save_name.empty() && previous_save_name != name) {
            const std::string previous_path =
                fmt::format("saves\\{}.{}", previous_save_name, g_save_extension);
            try {
                wiz8::remove_file(wiz8::user_path(previous_path));
            } catch (const std::exception& error) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Saved %s but could not remove previous save %s: %s", name.c_str(),
                            previous_path.c_str(), error.what());
            }
        }
        ShowNotice(W8_FONT_PALETTE_BEIGE, saved ? Data().strings[0x6f4] : Data().strings[0x6f6]);
        // Keep the saving notice up for at least half a second, without spinning.
        if (const auto shown = MonotonicMilliseconds() - entered_tick; shown < 500)
            std::this_thread::sleep_for(std::chrono::milliseconds(500 - shown));
        break;
    }
    case W8_PLEASE_WAIT_CHANGE_LEVEL:
        if (!ReloadLevelPreservingCamera(parameter, parameter_2))
            throw std::runtime_error(fmt::format("cannot load level {}", parameter));
        AutoSaveIfAllowed(true);
    }

    assert(GetWorld());
    ReturnToPreviousScreen();
    if (mode == W8_PLEASE_WAIT_LOAD_GAME && CurrentGame().status.intro_shown) {
        RequestScreen({.id = W8_SCREEN_INTRO, .mode = W8_INTRO_VIDEO_VIRGIN});
        return;
    }
    if (PendingScreen() != W8_SCREEN_MAIN_GAME) {
        RequestScreen({.id = W8_SCREEN_MAIN_GAME});
    }
}

bool PleaseWaitScreen::leave(ScreenDeparture)
{
    EnableCursorScene();
    return true;
}

void PleaseWaitScreen::animate()
{
    const std::uint64_t tick = MonotonicMilliseconds();
    if (tick - entered_tick > 499) {
        caption_y = (caption_y + 1) % 0x18;
        entered_tick = tick;
        CatalogArtwork::Load(0x1dd, 0).drawAndInvalidate(GetFrameBufferSurface(), caption_y, 0,
                                                         0x185);
        RenderFrame();
    }
}

void UpdatePleaseWaitLoadFrame(void)
{
    wiz8::sound::service();
    ServiceMusicPlaylist();
    if (CurrentScreenState().id == W8_SCREEN_PLEASE_WAIT) {
        static_cast<PleaseWaitScreen*>(CurrentScreen())->animate();
    }
}

std::unique_ptr<Screen> CreatePleaseWaitScreen(GameSession& games)
{
    return std::make_unique<PleaseWaitScreen>(games);
}

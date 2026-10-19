#include "wiz8/local_screens/PleaseWaitScreen.h"

#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <SDL3/SDL_log.h>
#include <fmt/format.h>

#include "Font.h"
#include "asset_paths.h"
#include "compat/native.h"
#include "layouts/levels.h"
#include "soundman.h"
#include "surrender/srDebug.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/fact_state.h"
#include "wiz8/filesystem.h"
#include "wiz8/fonts.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/save_screenshot.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_screens/IntroScreen.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/music_playlist.h"

/* The screen's descriptor. The entry handler mallocs it, clears it and fills the
   tail from the screen-state record it was entered with; the frame handler reads
   the tail and writes the caption; the leave releases it. */
struct W8LevelLoadDescriptor {
    std::string caption; /* 0x000, written by strcpy and sprintf */
    W8PleaseWaitMode mode; /* 0x0f0, the screen state's own mode */
    int parameter;
    int parameter_2;
    std::string name; /* 0x0fd, bounded only by the next field */
    std::string previous_save_name;
    std::optional<W8SaveScreenshot> save_screenshot;
    std::uint64_t entered_tick;
    int caption_y;
};

// GLOBAL: WIZ8 0x0069B7C0
Font g_level_load_font;
// GLOBAL: WIZ8 0x0069B7C8
std::unique_ptr<W8LevelLoadDescriptor> g_load_descriptor;

/* Engine Code\Levels.cpp owns this with C++ linkage. */

/* One video-object id per level, the backdrop the Please Wait
   screen shows while that level loads. 0x00605820 indexes the level's name in
   the string list. Both are bounded by 0x2F, with 0xE4 as the backdrop the
   frame handler falls back to. */
// GLOBAL: WIZ8 0x0064bf8c
static int g_level_backdrops[W8_LEVEL_COUNT] = {
    0x1bc, 0x1bd, 0x1be, 0x1bf, 0x1c0, 0x1c2, 0x1c1, 0x1c3, 0x1c4, 0x1c5, 0x1c6, 0xe4,
    0x1c7, 0x1c8, 0x1c9, 0x1ca, 0x1cb, 0x1cc, 0x1cd, 0x1ce, 0x1cf, 0x1d0, 0x1d1, 0xe4,
    0x1d2, 0x1d3, 0x1d4, 0x1d5, 0xe4,  0x1d6, 0xe4,  0x1d7, 0x1d8, 0x1d9, 0xe4,  0xe4,
    0x1da, 0x1db, 0x1dc, 0xe4,  0,     0,     0,     0,     0,     0,     0,
};
/* The path the five assertions carry. */
#define PLEASE_WAIT_SCREEN_CPP __FILE__

bool PleaseWaitScreenInitialize(void)
{
    return g_level_load_font.load("Data\\Level Load\\levelload_font.sti");
}

/* The entry handler. It builds the descriptor once and fills it from the
   screen-state record it was entered with. New-game mode clears the fact state
   and deletes the running save; load and save modes carry a name; level-change
   mode carries two parameters.

   The handler accesses the shared descriptor through g_load_descriptor. */
bool PleaseWaitScreenEnter(void)
{
    if (!g_load_descriptor) {
        g_load_descriptor = std::make_unique<W8LevelLoadDescriptor>();
        g_load_descriptor->mode = std::get<W8PleaseWaitMode>(g_current_screen_state.mode);
        switch (g_load_descriptor->mode) {
        case W8_PLEASE_WAIT_NEW_GAME:
            InitializeFactState();
            g_load_descriptor->parameter = SelectNewGameStartLevel();
            ReleaseMessageStorage();
            wiz8::remove_file(wiz8::user_path("saves\\CurrentGame.SAV"));
            break;
        case W8_PLEASE_WAIT_LOAD_GAME:
            g_load_descriptor->parameter = g_current_screen_state.parameter;
            g_load_descriptor->name = g_current_screen_state.name;
            break;
        case W8_PLEASE_WAIT_SAVE_GAME:
            g_load_descriptor->parameter = GetLoadedLevelID();
            g_load_descriptor->name = g_current_screen_state.name;
            g_load_descriptor->previous_save_name = g_current_screen_state.previous_save_name;
            g_load_descriptor->save_screenshot = g_current_screen_state.save_screenshot;
            break;
        case W8_PLEASE_WAIT_CHANGE_LEVEL:
            g_load_descriptor->parameter = g_current_screen_state.parameter;
            g_load_descriptor->parameter_2 = g_current_screen_state.parameter_2;
            break;
        }
    }
    SetPrimarySurfaceTextureHint2Enabled(false);
    DisableCursorScene();
    g_load_descriptor->caption_y = 0;
    g_load_descriptor->entered_tick = w8_get_ticks();
    return 1;
}

/* Draw the loading screen's backdrop, progress frame and caption. Retail
   expands this helper at both frame-handler call sites. */
static void DrawPleaseWaitScreen()
{
    int backdrop = static_cast<unsigned int>(g_load_descriptor->parameter) < W8_LEVEL_COUNT
                       ? g_level_backdrops[g_load_descriptor->parameter]
                       : 0xe4;
    CatalogArtwork::Load(backdrop, 0).draw(GetFrameBufferSurface(), 0, 0, 0);
    CatalogArtwork::Load(0x1de, 0).draw(GetFrameBufferSurface(), 0, 0, 0x1be);
    DrawText(GetFrameBufferSurface(), g_level_load_font, {0x6a, 0x1c7}, g_load_descriptor->caption);
    CatalogArtwork::Load(0x1dd, 0).draw(GetFrameBufferSurface(), g_load_descriptor->caption_y, 0,
                                        0x185);
    ResetTransientRenderScenes();
}

void PleaseWaitScreenFrame(void)
{
    switch (g_load_descriptor->mode) {
    case W8_PLEASE_WAIT_NEW_GAME:
        g_load_descriptor->caption = g_localized_strings[0x6ef];
        break;
    case W8_PLEASE_WAIT_LOAD_GAME: {
        const auto& strings = g_localized_strings;
        g_load_descriptor->caption =
            g_load_descriptor->name.starts_with("Quick") ? strings[0x6f1] : strings[0x6f0];
    } break;
    case W8_PLEASE_WAIT_SAVE_GAME:
        g_load_descriptor->caption = g_localized_strings[0x6f2].c_str();
        break;
    case W8_PLEASE_WAIT_CHANGE_LEVEL:
        if (static_cast<unsigned int>(g_load_descriptor->parameter) < W8_LEVEL_COUNT) {
            g_load_descriptor->caption = fmt::format(
                "{} {}...", g_localized_strings[0x6f3],
                g_localized_strings[g_level_name_indices[g_load_descriptor->parameter]]);
        } else if (g_load_descriptor->parameter == 0x38) {
            g_load_descriptor->caption = "Entering default level...";
        } else {
            g_load_descriptor->caption =
                fmt::format("Entering test level {:c}...", g_load_descriptor->parameter + 2);
        }
    }

    DrawPleaseWaitScreen();
    RenderFrame();
    RenderFrame();
    DisableCursorScene();

    switch (g_load_descriptor->mode) {
    case W8_PLEASE_WAIT_NEW_GAME:
        ResetLiveSessionForLoad();
        int level;
        int entrance;
        ChooseNewGameStartLocation(&level, &entrance);
        if (!LoadLevel(level, entrance, false)) {
            srAssertFail("fVerify", PLEASE_WAIT_SCREEN_CPP, 279, 0);
        }
        PostNewGameLoad();

        break;
    case W8_PLEASE_WAIT_LOAD_GAME:
        if (!LoadGame(g_load_descriptor->name)) {
            srAssertFail("fVerify", PLEASE_WAIT_SCREEN_CPP, 293, 0);
        }
        if (!LoadLevel(g_status.resume_level, -1, true)) {
            srAssertFail("fVerify", PLEASE_WAIT_SCREEN_CPP, 296, 0);
        }

        break;
    case W8_PLEASE_WAIT_SAVE_GAME: {
        const auto& screenshot = g_load_descriptor->save_screenshot;
        const bool saved = SaveGame(g_load_descriptor->name, screenshot ? &*screenshot : nullptr);
        // Do not destroy the previous save until the new one has been committed.
        if (saved && !g_load_descriptor->previous_save_name.empty() &&
            g_load_descriptor->previous_save_name != g_load_descriptor->name) {
            const std::string previous_path = fmt::format(
                "saves\\{}.{}", g_load_descriptor->previous_save_name, g_save_extension);
            try {
                wiz8::remove_file(wiz8::user_path(previous_path));
            } catch (const std::exception& error) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Saved %s but could not remove previous save %s: %s",
                            g_load_descriptor->name.c_str(), previous_path.c_str(), error.what());
            }
        }
        ShowNotice(W8_FONT_PALETTE_BEIGE,
                   saved ? g_localized_strings[0x6f4] : g_localized_strings[0x6f6]);
        // Keep the saving notice up for at least half a second, without spinning.
        if (const auto shown = w8_get_ticks() - g_load_descriptor->entered_tick; shown < 500)
            std::this_thread::sleep_for(std::chrono::milliseconds(500 - shown));
        break;
    }
    case W8_PLEASE_WAIT_CHANGE_LEVEL:
        if (!ReloadLevelPreservingCamera(g_load_descriptor->parameter,
                                         g_load_descriptor->parameter_2)) {
            srAssertFail("fVerify", PLEASE_WAIT_SCREEN_CPP, 320, 0);
        }
        AutoSaveIfAllowed(true);
    }

    if (!GetWorld()) {
        srAssertFail("GetWorld()", PLEASE_WAIT_SCREEN_CPP, 332, 0);
    }
    RequestScreenTransition();
    if (g_load_descriptor->mode == W8_PLEASE_WAIT_LOAD_GAME && g_status.intro_shown) {
        SetIntroVideoIndex(W8_INTRO_VIDEO_VIRGIN);
        g_pending_screen_state.id = W8_SCREEN_INTRO;
        return;
    }
    if (GetPendingScreenState() != 7) {
        g_pending_screen_state.id = W8_SCREEN_MAIN_GAME;
    }
}

/* The leave. The leaving pass releases the descriptor the entry handler
   allocated; the ordinary pass only tears the frame down. Both passes run the
   teardown, which is why the release sits under the flag rather than the whole
   body. */
bool PleaseWaitScreenLeave(int leaving)
{
    if (leaving) {
        g_load_descriptor.reset();
    }
    EnableCursorScene();
    return 1;
}

/* Synchronous texture loading does not run GameLoop, so service audio here
   and keep the animated please-wait glyph moving. */
void UpdatePleaseWaitLoadFrame(void)
{
    std::uint64_t tick;

    wiz8::sound::service();
    ServiceMusicPlaylist();
    tick = w8_get_ticks();
    if (tick - g_load_descriptor->entered_tick > 499) {
        g_load_descriptor->caption_y = (g_load_descriptor->caption_y + 1) % 0x18;
        g_load_descriptor->entered_tick = tick;
        CatalogArtwork::Load(0x1dd, 0).drawAndInvalidate(GetFrameBufferSurface(),
                                                         g_load_descriptor->caption_y, 0, 0x185);
        RenderFrame();
    }
}

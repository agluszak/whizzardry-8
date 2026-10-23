#include "wiz8/game_init.h"

#include <cstring>
#include <optional>

#include "wiz8/application.h"
#include "wiz8/canvas/shading.h"
#include "wiz8/combat/sounds.h"
#include "wiz8/data/game_data.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/music_playlist.h"
#include "wiz8/render/frame.h"
#include "wiz8/screen.h"
#include "wiz8/ui_input.h"

bool InitializeGame(const GameData& data, GameSession& games, bool load_at_startup)
{
    InstallGameData(&data);
    const auto& framebuffer = GetFrameBufferSurface();
    memset(framebuffer.pixels, 0, framebuffer.pitch * framebuffer.h);
    LoadGameConfiguration();
    ResetScreenRouting(games);
    games.observe(PresentGameEvent);
    SetShadeTablePercent((float)0.66);
    if (!InitializeMenuFonts()) {
        return false;
    }
    InitializeButtonImageManager();
    InitializeUiHelp();
    if (!VerifyDataSubdirs()) {
        return false;
    }
    if (const auto save = load_at_startup ? FindStartupQuickSave() : std::nullopt) {
        RequestScreen({.id = W8_SCREEN_PLEASE_WAIT,
                       .mode = W8_PLEASE_WAIT_LOAD_GAME,
                       .parameter = GetSaveGameLevel(*save),
                       .name = *save});
    } else {
        RequestScreen({.id = W8_SCREEN_INTRO, .mode = W8_INTRO_VIDEO_SIRTECH});
    }
    if (!LoadHitSoundDatabase()) {
        ShutdownWithErrorBox("Could not load hit sound database!");
    }
    if (!InitializeMusicPlaylist()) {
        return false;
    }
    g_texture_cache_enabled = true;
    return true;
}

void ShutdownGame(GameSession& games, bool save_configuration)
{
    games.end();
    ReleaseHitSoundDatabase();
    g_font_state_palettes = {};
    ResetScreenRouting(games);
    InstallGameData(nullptr);
    if (save_configuration) SaveGameConfiguration();
}

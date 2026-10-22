#include "wiz8/game_init.h"

#include <cstring>
#include <exception>
#include <optional>
#include <string>
#include <string_view>

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
#include "wiz8/version.h"

/* The data bring-up gate the native application initializes last. It stamps
   the version string, opens the string data, then walks every database loader
   in turn, abandoning the sequence the moment one fails. */
bool InitializeGame(GameData& data, GameSession& games, bool load_at_startup,
                    const std::string& string_data_override)
{
    const auto version = FormatVersionBanner(true, true, true);

    try {
        data = LoadGameData(!string_data_override.empty() ? std::string_view(string_data_override)
                                                          : "Data\\Strings\\StringData.DAT");
    } catch (const std::exception&) {
        return false;
    }
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
        return false;
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

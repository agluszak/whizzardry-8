#include "wiz8/game_init.h"

#include <cstring>
#include <optional>
#include <string>

#include "wiz8/application.h"
#include "wiz8/canvas/shading.h"
#include "wiz8/cursor.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/entities/missile.h"
#include "wiz8/fonts.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/CombatSound.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/GameplayDatabase.h"
#include "wiz8/local_code/GameplayInit.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/monster_generators.h"
#include "wiz8/music_playlist.h"
#include "wiz8/render/frame.h"
#include "wiz8/ui_input.h"
#include "wiz8/version.h"

/* Original translation unit is not established by the Arnika/Gameloop source anchors. */

/*
 * The data bring-up gate the native application initializes last. It stamps the version
 * string, opens the string data, then walks every database
 * loader in turn, abandoning the sequence the moment one fails. Its callees
 * are mostly unidentified and carry address-derived names; the ones already
 * recovered elsewhere keep theirs.
 */

// FUNCTION: WIZ8 0x004e2f40
unsigned char InitializeGame(size_t& initialized_screens, bool load_at_startup,
                             const std::string& string_data_override)
{
    const auto version = FormatVersionBanner(true, true, true);

    LoadLocalizedStrings(!string_data_override.empty() ? string_data_override.c_str()
                                                       : "Data\\Strings\\StringData.DAT");
    const auto& framebuffer = GetFrameBufferSurface();
    memset(framebuffer.pixels, 0, framebuffer.pitch * framebuffer.h);
    LoadGameConfiguration();
    ResetScreenRouting();
    for (int screen = 0; screen < W8_SCREEN_COUNT; ++screen) {
        initialized_screens = screen + 1;
        const auto initialize = g_screen_handlers[screen].initialize;
        if (initialize && !initialize()) {
            return 0;
        }
    }
    SetShadeTablePercent((float)0.66);
    if (!InitializeMenuFonts()) {
        return 0;
    }
    InitializeButtonImageManager();
    InitializeUiHelp();
    ResetGameStatus(false);
    InitializeGameplayRuntimeObjects();
    UpdateHeldItemCursor();
    if (!VerifyDataSubdirs()) {
        return 0;
    }
    if (!InitializeItemDatabase()) {
        return 0;
    }
    if (!InitializeItemTables()) {
        return 0;
    }
    if (!LoadMonsterDatabase(0)) {
        return 0;
    }
    if (!InitializeNpcDatabase()) {
        return 0;
    }
    if (!InitializeFactDatabase()) {
        return 0;
    }
    if (!InitializeLevelDatabase()) {
        return 0;
    }
    if (const auto save = load_at_startup ? FindStartupQuickSave() : std::nullopt) {
        RequestScreen({.id = W8_SCREEN_PLEASE_WAIT,
                       .mode = W8_PLEASE_WAIT_LOAD_GAME,
                       .parameter = GetSaveGameLevel(*save),
                       .name = *save});
    } else {
        RequestScreen({.id = W8_SCREEN_INTRO});
    }
    InitializeEncounterTables();
    if (!LoadMissileDatabase()) {
        ShutdownWithErrorBox("Could not load missile database!");
    }
    if (!InitializeSpellDatabase()) {
        ShutdownWithErrorBox("Could not load spell database!");
    }
    if (!LoadHitSoundDatabase()) {
        ShutdownWithErrorBox("Could not load hit sound database!");
    }
    ReleaseMessageStorage();
    if (!InitializeMusicPlaylist()) {
        return 0;
    }
    g_texture_cache_enabled = true;
    return 1;
}

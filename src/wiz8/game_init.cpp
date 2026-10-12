#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "wiz8/version.h"
#include "wiz8/music_playlist.h"
#include "wiz8/cursor.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/engine_code/Spells.h"
#include "wiz8/local_code/GameplayDatabase.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/monster_generators.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/game_init.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/fonts.h"
#include "wiz8/ui_input.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/CombatSound.h"
#include "wiz8/local_code/GameplayInit.h"
#include "wiz8/engine_code/Missile.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "shading.h"
#include "wiz8/application.h"

/* Original translation unit is not established by the Arnika/Gameloop source anchors. */

/*
 * The data bring-up gate the native application initializes last. It stamps the version
 * string, opens the archives and string data, then walks every database
 * loader in turn, abandoning the sequence the moment one fails. Its callees
 * are mostly unidentified and carry address-derived names; the ones already
 * recovered elsewhere keep theirs.
 */

// FUNCTION: WIZ8 0x004e2f40
unsigned char InitializeGame(size_t& initialized_screens)
{
    const auto version = FormatVersionBanner(true, true, true);
    bool ok;

    LoadLocalizedStrings(!gzStringDataOverride.empty() ? gzStringDataOverride.c_str()
                                              : "Data\\Strings\\StringData.DAT");
    const auto& framebuffer = GetFrameBufferSurface();
    memset(framebuffer.pixels, 0, framebuffer.pitch * framebuffer.h);
    LoadGameConfiguration();
    g_current_screen_state.id = W8_SCREEN_NONE;
    g_pending_screen_state.id = W8_SCREEN_NONE;
    g_screen_return_stack.clear();
    for (int screen = 0; screen < W8_SCREEN_COUNT; ++screen) {
        initialized_screens = screen + 1;
        if (!g_screen_handlers[screen].initialize()) {
            return 0;
        }
    }
    SetShadeTablePercent((float)0.66);
    if (!InitializeMenuFonts()) {
        return 0;
    }
    InitializeButtonImageManager();
    InitializeRegionHelpState();
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
    InitializeItemVideoObjects();
    SetPendingScreenState(W8_SCREEN_INTRO);
    g_status.current_level = -1;
    if (const auto save = gfLoadAtStartup ? FindStartupQuickSave() : std::nullopt) {
        g_pending_screen_state.name = *save;
        g_pending_screen_state.mode = 1;
        g_pending_screen_state.parameter = GetSaveGameLevel(g_pending_screen_state.name);
        SetPendingScreenState(W8_SCREEN_PLEASE_WAIT);
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
    ok = static_cast<unsigned char>(0x4000000 < GetTotalPhysicalMemory());
    g_texture_cache_enabled = ok;
    return 1;
}

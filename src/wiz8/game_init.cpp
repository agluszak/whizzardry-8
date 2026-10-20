#include "wiz8/game_init.h"

#include <cstring>
#include <optional>
#include <string>

#include "wiz8/application.h"
#include "wiz8/canvas/shading.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/cursor.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/entities/missile.h"
#include "wiz8/fonts.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/CombatSound.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/GameplayDatabase.h"
#include "wiz8/local_code/GameplayInit.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/location_variables.h"
#include "wiz8/monster_generators.h"
#include "wiz8/music_playlist.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/render/frame.h"
#include "wiz8/screen.h"
#include "wiz8/ui_input.h"
#include "wiz8/version.h"
#include "wiz8/xstatus.h"

/* The data bring-up gate the native application initializes last. It stamps
   the version string, opens the string data, then walks every database loader
   in turn, abandoning the sequence the moment one fails. */
unsigned char InitializeGame(bool load_at_startup, const std::string& string_data_override)
{
    const auto version = FormatVersionBanner(true, true, true);

    LoadLocalizedStrings(!string_data_override.empty() ? string_data_override.c_str()
                                                       : "Data\\Strings\\StringData.DAT");
    const auto& framebuffer = GetFrameBufferSurface();
    memset(framebuffer.pixels, 0, framebuffer.pitch * framebuffer.h);
    LoadGameConfiguration();
    ResetScreenRouting();
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
        RequestScreen({.id = W8_SCREEN_INTRO, .mode = W8_INTRO_VIDEO_SIRTECH});
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

void ShutdownGame(bool save_configuration)
{
    if (gXStatus.character_event_queue) gXStatus.character_event_queue->DestroyAllEvents();
    ReleaseHitSoundDatabase();
    ReleaseGenericItemNames();
    ReleaseNpcStates();
    UnloadEncounterTables();
    ReleaseSpellDatabase();
    DestroyItemDatabase();
    DestroyItemTables();
    DestroyNpcDatabase();
    DestroyFactDatabase();
    DestroyLevelDatabase();
    g_font_state_palettes = {};
    ResetScreenRouting();
    ClearLocationVariables();
    DestroyGameplayObjects();
    ClearNpcMessageQueue();
    ClearNpcDialogueTranscript();
    ReleaseMessageStorage();
    FreeStringTable();
    g_status.party = {};
    if (save_configuration) SaveGameConfiguration();
}

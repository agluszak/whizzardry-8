#include <SDL3/SDL_log.h>
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-04, 2026-10-06, 2026-10-07, 2026-10-09.
   Distributed under the accompanying SFI Source Code license agreement. */
#include "Types.h"
#include "compat/kernel32.h"
#include "compat/video.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "sgp.h"
#include "vobject.h"
#include "Font.h"
#include "FileMan.h"
#include "input.h"
#include "random.h"
#include "wiz8/game_init.h"
#include "wiz8/local_code/Gameloop.h"
#include "soundman.h"
#include "Button System.h"
#include "mousesystem.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/GameData.h"

#include "input.h"


// Prototype Declarations

// Should the game immediately load the quick save at startup?
// GLOBAL: WIZ8 0x006505a0
BOOLEAN gfLoadAtStartup = FALSE;
// GLOBAL: WIZ8 0x006505a1
BOOLEAN gfUsingBoundsChecker = FALSE;
// GLOBAL: WIZ8 0x006505a4
CHAR8* gzStringDataOverride = NULL;
// GLOBAL: WIZ8 0x006505a8
BOOLEAN gfCapturingVideo = FALSE;

// GLOBAL: WIZ8 0x006f062c
HINSTANCE ghInstance;

// Global Variable Declarations
#ifdef WINDOWED_MODE
RECT rcWindow;
#endif

// moved from header file: 24mar98:HJH
// GLOBAL: WIZ8 0x006f0624
UINT32 giStartMem;

// GLOBAL: WIZ8 0x006f0620

// GLOBAL: WIZ8 0x006f0630
BOOLEAN gfApplicationActive;
// GLOBAL: WIZ8 0x006f0628
BOOLEAN gfProgramIsRunning;
// GLOBAL: WIZ8 0x006505a9
BOOLEAN gfGameInitialized = FALSE;
// GLOBAL: WIZ8 0x006505aa
BOOLEAN gfDontUseDDBlits = FALSE;

// There were TWO of them??!?! -- DB
//CHAR8		gzCommandLine[ 100 ];
CHAR8 gzCommandLine[100]; // Command line given

// GLOBAL: WIZ8 0x006505ac
CHAR8 gzErrorMsg[2048] = "";
// GLOBAL: WIZ8 0x00650dac
BOOLEAN gfIgnoreMessages = FALSE;

// GLOBAL VARIBLE, SET TO DEFAULT BUT CAN BE CHANGED BY THE GAME IF INIT FILE READ
// GLOBAL: WIZ8 0x005ff450
UINT8 gbPixelDepth = PIXEL_DEPTH;


// FUNCTION: WIZ8 0x00401570
BOOLEAN InitializeStandardGamingPlatform(HINSTANCE hInstance, int sCommandShow)
{
    FontTranslationTable* pFontTable;

    // now required by all (even JA2) in order to call ShutdownSGP
    atexit(SGPExit);

    // For rendering DLLs etc.

    // Second, read in settings
    GetRuntimeSettings();

    // Now start up everything else.

    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Initializing Memory Manager");
    // Initialize the Memory Manager
    if (InitializeMemoryManager() == FALSE) { // We were unable to initialize the memory manager
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", "FAILED : Initializing Memory Manager");
        return FALSE;
    }

    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Initializing File Manager");
    // Initialize the File Manager
    if (InitializeFileManager(NULL) == FALSE) { // We were unable to initialize the file manager
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", "FAILED : Initializing File Manager");
        return FALSE;
    }

    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Initializing Input Manager");
    // Initialize the Input Manager
    if (InitializeInputManager() == FALSE) { // We were unable to initialize the input manager
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", "FAILED : Initializing Input Manager");
        return FALSE;
    }

    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Initializing Video Manager");
    // Initialize DirectDraw (DirectX 2)
    if (InitializeVideoManager(hInstance, (UINT16)sCommandShow) ==
        FALSE) { // We were unable to initialize the video manager
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", "FAILED : Initializing Video Manager");
        return FALSE;
    }

    // Initialize Video Object Manager
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Initializing Video Object Manager");
    if (!InitializeVideoObjectManager()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", "FAILED : Initializing Video Object Manager");
        return FALSE;
    }

    // Initialize Video Surface Manager
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Initializing Video Surface Manager");
    if (!InitializeVideoSurfaceManager()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", "FAILED : Initializing Video Surface Manager");
        return FALSE;
    }

    // Make sure we start up our local clock (in milliseconds)
    // We don't need to check for a return value here since so far its always TRUE
    InitializeClockManager(); // must initialize after VideoManager, 'cause it uses ghWindow

    // Create font translation table (store in temp structure)
    pFontTable = CreateEnglishTransTable();
    if (pFontTable == NULL) {
        return (FALSE);
    }

    // Initialize Font Manager
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Initializing the Font Manager");
    // Init the manager and copy the TransTable stuff into it.
    if (!InitializeFontManager(8, pFontTable)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", "FAILED : Initializing Font Manager");
        return FALSE;
    }
    // Don't need this thing anymore, so get rid of it (but don't de-alloc the contents)
    MemFree(pFontTable);

    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Initializing Sound Manager");
    // Initialize the Sound Manager (DirectSound)
    if (InitializeSoundManager() == FALSE) { // We were unable to initialize the sound manager
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", "FAILED : Initializing Sound Manager");
        return FALSE;
    }

    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Initializing Random");
    // Initialize random number generator
    InitializeRandom(); // no Shutdown

    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%s", "Initializing Game Manager");
    // Initialize the Game
    if (InitializeGame() == FALSE) { // We were unable to initialize the game
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", "FAILED : Initializing Game Manager");
        return FALSE;
    }

    // Register mouse wheel message

    gfGameInitialized = TRUE;

    return TRUE;
}

void ShutdownStandardGamingPlatform(void)
{
    // GLOBAL: WIZ8 0x00650db4
    static BOOLEAN Reenter = FALSE;
    // Prevent multiple reentry into this function

    if (Reenter == FALSE) {
        Reenter = TRUE;
    } else {
        return;
    }
    // Shut down the different components of the SGP

    if (gfGameInitialized) {
        ShutdownGame();
    }

    ShutdownButtonSystem();
    MSYS_Shutdown();

    ShutdownSoundManager();

    DestroyEnglishTransTable(); // has to go before ShutdownFontManager()
    ShutdownFontManager();

    ShutdownClockManager(); // must shutdown before VideoManager, 'cause it uses ghWindow

#ifdef SGP_VIDEO_DEBUGGING
    PerformVideoInfoDumpIntoFile("SGPVideoShutdownDump.txt", FALSE);
#endif

    ShutdownVideoSurfaceManager();
    ShutdownVideoObjectManager();
    ShutdownVideoManager();

    ShutdownInputManager();
    ShutdownFileManager();

#ifdef EXTREME_MEMORY_DEBUGGING
    DumpMemoryInfoIntoFile("ExtremeMemoryDump.txt", FALSE);
#endif

    ShutdownMemoryManager(); // must go last, for MemDebugCounter to work right...
}


//Do not place code in between WinMain and Handled WinMain

// FUNCTION: WIZ8 0x004017f0
void SGPExit(void)
{
    // GLOBAL: WIZ8 0x00650db5
    static BOOLEAN fAlreadyExiting = FALSE;
    BOOLEAN fUnloadScreens = TRUE;

    // helps prevent heap crashes when multiple assertions occur and call us
    if (fAlreadyExiting) {
        return;
    }

    fAlreadyExiting = TRUE;
    gfProgramIsRunning = FALSE;

    // Wizardry only
    if (gfGameInitialized) {
// ARM: if in DEBUG mode & we've ShutdownWithErrorBox, don't unload screens and release data structs to permit easier debugging
#ifdef _DEBUG
        if (gfIgnoreMessages) {
            fUnloadScreens = FALSE;
        }
#endif
        GameloopExit(fUnloadScreens);
    }

    /* Movie voices belong to the current screen and must leave the engine first. */
    ShutdownSoundManager();
    ShutdownStandardGamingPlatform();
    W8VideoShowCursor(TRUE);
    if (gzErrorMsg[0])
        fprintf(stderr, "%s\n", gzErrorMsg);
    VideoDumpMemoryLeaks();
}

// FUNCTION: WIZ8 0x004018c0
void GetRuntimeSettings()
{
    gbPixelDepth = W8ReadProfileInt("sgp.ini", "SGP", "PIXEL_DEPTH", PIXEL_DEPTH);
}

// FUNCTION: WIZ8 0x00401920
void ShutdownWithErrorBox(const CHAR8* pcMessage)
{
    strncpy(gzErrorMsg, pcMessage, 2047);
    gzErrorMsg[2047] = '\0';
    gfIgnoreMessages = TRUE;

    fprintf(stderr, "%s\n", gzErrorMsg);
    // Release packed-pointer users before exit destroys the native handle table.
    SGPExit();
    exit(1);
}

// FUNCTION: WIZ8 0x00401950
void ProcessCommandLine(CHAR8* pCommandLine)
{
    CHAR8 cSeparators[] = "\t =";
    CHAR8 *pCopy = NULL, *pToken;

    pCopy = (CHAR8*)MemAlloc(strlen(pCommandLine) + 1);

    Assert(pCopy);
    if (!pCopy)
        return;

    memcpy(pCopy, pCommandLine, strlen(pCommandLine) + 1);

    pToken = strtok(pCopy, cSeparators);
    while (pToken) {
        if (!_strnicmp(pToken, "/NOSOUND", 8)) {
            SoundEnableSound(FALSE);
        } else if (!_strnicmp(pToken, "/INSPECTOR", 10)) {
            VideoInspectorEnable();
        } else if (!_strnicmp(pToken, "/VIDEOCFG", 9)) {
            pToken = strtok(NULL, cSeparators);
            VideoSetConfigFile(pToken);
        } else if (!_strnicmp(pToken, "/LOAD", 5)) {
            gfLoadAtStartup = TRUE;
        } else if (!_strnicmp(pToken, "/WINDOW", 7)) {
            VideoFullScreen(FALSE);
        } else if (!_strnicmp(pToken, "/BC", 7)) {
            gfUsingBoundsChecker = TRUE;
        } else if (!_strnicmp(pToken, "/CAPTURE", 7)) {
            gfCapturingVideo = TRUE;
        } else if (!_strnicmp(pToken, "/NOOCT", 6)) {
            NoOct();
        } else if (!_strnicmp(pToken, "/STRINGDATA", 11)) {
            pToken = strtok(NULL, cSeparators);
            gzStringDataOverride = (CHAR8*)MemAlloc(strlen(pToken) + 1);
            strcpy(gzStringDataOverride, pToken);
        }

        pToken = strtok(NULL, cSeparators);
    }

    MemFree(pCopy);
}

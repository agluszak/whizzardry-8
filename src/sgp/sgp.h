/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-04, 2026-10-06, 2026-10-07, 2026-10-09.
   Distributed under the accompanying SFI Source Code license agreement. */
#ifndef __SGP_
#define __SGP_

#include "Types.h"
#include "timer.h"

#include "Video2.h"

#include "input.h"
#include "MemMan.h"
#include "FileMan.h"
#include "DbMan.h"
#include "soundman.h"
#include "line.h"
#include "Font.h"
#include "english.h"
#include "Mutex Manager.h"
#include "vobject.h"
#include "random.h"
#include "shading.h"

#ifdef __cplusplus
extern "C" {
#endif

extern BOOLEAN gfProgramIsRunning; // Turn this to FALSE to exit program
extern UINT32 giStartMem;
extern CHAR8 gzCommandLine[100]; // Command line given
extern UINT8 gbPixelDepth;       // GLOBAL RUN-TIME SETTINGS
extern BOOLEAN gfDontUseDDBlits; // GLOBAL FOR USE OF DD BLITTING

extern BOOLEAN gfLoadAtStartup;
extern CHAR8* gzStringDataOverride;
extern BOOLEAN gfUsingBoundsChecker;
extern BOOLEAN gfCapturingVideo;

extern HINSTANCE ghInstance;
extern BOOLEAN gfApplicationActive;
extern BOOLEAN gfGameInitialized;
extern BOOLEAN gfIgnoreMessages;
extern CHAR8 gzErrorMsg[2048];

union SDL_Event;
void HandleGameEvent(const SDL_Event& event);
bool PumpGameEvents(bool wait = false);
BOOLEAN InitializeStandardGamingPlatform(HINSTANCE instance, int show_command);
void ShutdownStandardGamingPlatform(void);
void ProcessCommandLine(CHAR8* command_line);
void GetRuntimeSettings(void);

// function prototypes
void SGPExit(void);
void ShutdownWithErrorBox(const CHAR8* pcMessage);

#ifdef __cplusplus
}
#endif

#endif

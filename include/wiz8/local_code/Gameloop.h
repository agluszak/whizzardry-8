#ifndef WIZ8_LOCAL_CODE_GAMELOOP_H
#define WIZ8_LOCAL_CODE_GAMELOOP_H

#include <cstddef>
#include <vector>

#include "wiz8/layouts/screen_state.h"

void ShutdownGame(size_t initialized_screens, bool save_configuration);
void GameLoop(void);
void GameloopExit();

/* Local Code\Gameloop.cpp's screen-state machine storage. */
extern W8ScreenStateHandlers g_screen_handlers[W8_SCREEN_COUNT];
enum class ScreenDeparture { Suspend, Discard };
void ResetScreenRouting();
void RequestScreen(W8ScreenStateRuntime request,
                   ScreenDeparture departure = ScreenDeparture::Suspend);
void ReturnToPreviousScreen();
const W8ScreenStateRuntime& CurrentScreenState();
W8ScreenId PreviousScreen();
W8ScreenId SuspendedScreen();
W8ScreenId RequestedScreen();
std::size_t SuspendedScreenCount();
bool IsScreenTransitionPending();
W8ScreenId GetPendingScreenState();

#endif

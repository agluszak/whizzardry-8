#pragma once

#include <memory>

#include "wiz8/screen.h"

class GameSession;

/* Performs the new-game, load, save or level-change transition the request
   names behind a level backdrop and caption. */
std::unique_ptr<Screen> CreatePleaseWaitScreen(GameSession& games);
/* Synchronous loading does not run GameLoop: keep audio and the animated
   caption moving while the Please Wait screen is current. */
void UpdatePleaseWaitLoadFrame(void);

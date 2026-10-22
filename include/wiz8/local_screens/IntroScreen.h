#pragma once

#include <memory>

#include "wiz8/screen.h"

/* Plays the film named by the request's W8IntroVideo mode, then routes to the
   main menu, a new game or the ending. */
std::unique_ptr<Screen> CreateIntroScreen();
void ContinueAfterDarkEndingVideo(void);

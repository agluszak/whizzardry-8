#pragma once

#include "compat/compiler.h"

void ContinueAfterDarkEndingVideo(void);
bool IntroScreenEnter(void);
void IntroScreenFrame(void);
bool IntroScreenLeave(int leaving);
void SetIntroVideoIndex(w8_ulong value);
/* Which intro video the intro screen shows next; the router and
   the menu paths select it through SetIntroVideoIndex. */
extern w8_ulong g_intro_video_index;

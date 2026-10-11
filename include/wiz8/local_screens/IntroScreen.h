#pragma once

#include "compat/compiler.h"

class W8BinkVideo;

extern W8BinkVideo* gpVideo;

void ContinueAfterDarkEndingVideo(void);
unsigned char IntroScreenEnter(void);
void IntroScreenFrame(void);
unsigned char IntroScreenLeave(int leaving);
void SetIntroVideoIndex(w8_ulong value);
/* Which intro video the intro screen shows next; the router and
   the menu paths select it through SetIntroVideoIndex. */
extern w8_ulong g_intro_video_index;

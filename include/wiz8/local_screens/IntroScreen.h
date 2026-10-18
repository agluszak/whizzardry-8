#pragma once

#include "compat/compiler.h"

/* The seven intro films, in the same order as the filename table. */
enum W8IntroVideo : w8_ulong {
    W8_INTRO_VIDEO_WIZARDRY = 0,
    W8_INTRO_VIDEO_UNALIGNED = 1,
    W8_INTRO_VIDEO_UMPANI = 2,
    W8_INTRO_VIDEO_TRANG = 3,
    W8_INTRO_VIDEO_VIRGIN = 4,
    W8_INTRO_VIDEO_DARK_ENDING = 5,
    W8_INTRO_VIDEO_SIRTECH = 6
};

void ContinueAfterDarkEndingVideo(void);
bool IntroScreenEnter(void);
void IntroScreenFrame(void);
bool IntroScreenLeave(int leaving);
void SetIntroVideoIndex(W8IntroVideo value);
/* Which intro video the intro screen shows next; the router and
   the menu paths select it through SetIntroVideoIndex. */
extern W8IntroVideo g_intro_video_index;

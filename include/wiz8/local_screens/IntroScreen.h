#pragma once

#include <cstdint>
#include "compat/compiler.h"

class W8BinkVideo;

extern W8BinkVideo* gpVideo;

void ContinueAfterDarkEndingVideo(void);
std::uint8_t IntroScreenEnter(void);
void IntroScreenFrame(void);
std::uint8_t IntroScreenLeave(std::int32_t leaving);
void SetIntroVideoIndex(std::uint32_t value);
/* Which intro video the intro screen shows next; the router and
   the menu paths select it through SetIntroVideoIndex. */
extern std::uint32_t g_intro_video_index;

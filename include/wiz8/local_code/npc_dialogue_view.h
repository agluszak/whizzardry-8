#pragma once

#include <string_view>

#include "wiz8/message_box.h"

/* Dialogue presentation: quote bubbles, portrait messages and voiced
   subquotes. NPC Scripting.cpp decides what is said; this decides how. */
void ShowNpcScriptNoticeBubble(std::string_view text);
void ShowNpcPortraitMessage(std::string_view text, W8QuoteNotice notice = {});
bool IsNpcVoicePlaying(void);
/* Stop the voice and drop whatever is on screen without reporting it. */
void ResetNpcPresentation(void);

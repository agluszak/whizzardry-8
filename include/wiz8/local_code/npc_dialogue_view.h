#pragma once

#include <string_view>

#include "wiz8/message_box.h"

/* Dialogue presentation: quote bubbles, portrait messages and voiced
   subquotes. NPC Scripting.cpp decides what is said; this decides how. */
class Game;
void ShowNpcPortraitMessage(Game& game, std::string_view text, W8QuoteNotice notice = {});

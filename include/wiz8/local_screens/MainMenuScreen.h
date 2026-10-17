#pragma once

#include <string>
#include <string_view>

#include <SDL3/SDL_surface.h>

extern bool g_main_menu_has_save_games;
extern bool g_main_menu_redraw;
extern bool g_main_menu_warning_shown;
extern std::string g_pending_main_menu_message;

unsigned char DrawMainMenuItem(short item, short state);
unsigned char MainMenuScreenEnter(void);
void MainMenuScreenFrame(void);
unsigned char MainMenuScreenLeave(int leaving);

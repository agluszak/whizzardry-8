struct SGPVSurface;
#pragma once

#include <string>
#include <string_view>

extern bool g_main_menu_has_save_games;
extern bool g_main_menu_redraw;
extern bool g_main_menu_warning_shown;
extern bool g_main_menu_overlay_enabled;
extern SGPVSurface* g_main_menu_overlay_surface;
extern std::string g_pending_main_menu_message;

unsigned char DrawMainMenuItem(short item, short state);
void SetMainMenuMessage(std::string_view message);
unsigned char MainMenuScreenInitialize(void);
unsigned char MainMenuScreenEnter(void);
void MainMenuScreenFrame(void);
unsigned char MainMenuScreenLeave(int leaving);

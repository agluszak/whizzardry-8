#pragma once

#include <string>
#include <string_view>

#include "input.h"

struct W8Region;

class W8MessageDialogBase;

extern unsigned short g_main_menu_selected_item;
extern bool g_main_menu_has_save_games;
extern bool g_main_menu_redraw;
extern bool g_main_menu_warning_shown;
extern bool g_main_menu_overlay_enabled;
extern unsigned int g_main_menu_overlay_surface;
extern unsigned int g_main_menu_hover_region;
extern W8MessageDialogBase* g_main_menu_dialog;
extern std::string g_pending_main_menu_message;

unsigned char DrawMainMenuItem(short item, short state);
void SetMainMenuMessage(std::string_view message);
unsigned char MainMenuIntroduction(const InputAtom* event, W8Region* region);
unsigned char MainMenuNewGame(const InputAtom* event, W8Region* region);
unsigned char MainMenuLoadGame(const InputAtom* event, W8Region* region);
unsigned char MainMenuCredits(const InputAtom* event, W8Region* region);
unsigned char MainMenuOptions(const InputAtom* event, W8Region* region);
unsigned char MainMenuExit(const InputAtom* event, W8Region* region);
unsigned char MainMenuScreenInitialize(void);
unsigned char MainMenuScreenEnter(void);
void MainMenuScreenFrame(void);
unsigned char MainMenuScreenLeave(int leaving);

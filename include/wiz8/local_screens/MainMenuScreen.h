#pragma once

#include <cstdint>
#include <SDL3/SDL_surface.h>
#include <string>
#include <string_view>

extern bool g_main_menu_has_save_games;
extern bool g_main_menu_redraw;
extern bool g_main_menu_warning_shown;
extern std::string g_pending_main_menu_message;

std::uint8_t DrawMainMenuItem(std::int16_t item, std::int16_t state);
void SetMainMenuMessage(std::string_view message);
std::uint8_t MainMenuScreenEnter(void);
void MainMenuScreenFrame(void);
std::uint8_t MainMenuScreenLeave(std::int32_t leaving);

#pragma once

#include <cstdint>
#include <string_view>

#include <SDL3/SDL_keycode.h>

// Retail virtual-key numbers belong only to saved bindings and MGSKeyboard.ini.
SDL_Keycode KeyFromRetailBinding(std::uint16_t key);
std::uint16_t KeyToRetailBinding(SDL_Keycode key);
SDL_Keycode KeyFromBindingName(std::string_view name);

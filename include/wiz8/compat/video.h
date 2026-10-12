#pragma once
#include <cstdint>
#include <SDL3/SDL_rect.h>

struct SDL_Window;
class srDD;

SDL_Window* W8CreateGameWindow(std::int32_t width, std::int32_t height, bool fullscreen);
void W8DestroyGameWindow(SDL_Window* window);
bool W8ConfigureGameWindow(SDL_Window* window, bool fullscreen, std::int32_t width, std::int32_t height);
srDD* W8CreateNativeRenderDevice();
/* Fills rect with the game's fixed 640x480 client space; real window scaling
   happens in the SDL input adapters. */
bool W8VideoGetClientRect(SDL_Window* window, SDL_Rect* rect);
std::uint32_t W8TotalPhysicalMemory();
bool W8HasEnoughSaveSpace();

std::int32_t W8ReadProfileInt(const char* path, const char* section, const char* key, std::int32_t fallback);

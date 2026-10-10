#pragma once
#include "Types.h"
#include "compat/surfaces.h"
struct SDL_Window;
class srDD;
class srGERD;
SDL_Window* W8CreateGameWindow(int width, int height, bool fullscreen);
void W8DestroyGameWindow(SDL_Window* window);
bool W8ConfigureGameWindow(SDL_Window* window, bool fullscreen, int width, int height);
srDD* W8CreateNativeRenderDevice();
/* Fills rect with the game's fixed 640x480 client space; real window scaling
   happens in the SDL input adapters. */
bool W8VideoGetClientRect(SDL_Window* window, SGPRect* rect);
unsigned int W8TotalPhysicalMemory();
bool W8HasEnoughSaveSpace();

int W8ReadProfileInt(const char* path, const char* section, const char* key, int fallback);

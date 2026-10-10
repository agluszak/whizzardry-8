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
bool W8VideoGetClientRect(SDL_Window* window, SGPRect* rect);
bool W8VideoGetWindowRect(SDL_Window* window, SGPRect* rect);
bool W8VideoClientToScreen(SDL_Window* window, SGPPoint* point);
bool W8VideoWarpMouse(SDL_Window* window, int x, int y);
bool W8VideoShowCursor(bool visible);
bool W8VideoShowWindow(SDL_Window* window, int command);
bool W8VideoRaiseWindow(SDL_Window* window);
bool W8VideoCloseWindow(SDL_Window* window);
unsigned int W8TotalPhysicalMemory();
bool W8HasEnoughSaveSpace();

int W8ReadProfileInt(const char* path, const char* section, const char* key, int fallback);

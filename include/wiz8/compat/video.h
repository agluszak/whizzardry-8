#pragma once
#include "compat/kernel32.h"
#include "compat/surfaces.h"
class srDD;
class srGERD;
HWND W8CreateGameWindow(int width, int height, bool fullscreen);
void W8DestroyGameWindow(HWND window);
bool W8ConfigureGameWindow(HWND window, bool fullscreen, int width, int height);
srDD* W8CreateNativeRenderDevice();
BOOL W8VideoGetClientRect(HWND window, RECT* rect);
BOOL W8VideoGetWindowRect(HWND window, RECT* rect);
BOOL W8VideoClientToScreen(HWND window, POINT* point);
BOOL W8VideoWarpMouse(HWND window, int x, int y);
BOOL W8VideoShowCursor(BOOL visible);
BOOL W8VideoShowWindow(HWND window, int command);
BOOL W8VideoRaiseWindow(HWND window);
BOOL W8VideoCloseWindow(HWND window);
unsigned int W8TotalPhysicalMemory();
bool W8HasEnoughSaveSpace();

int W8ReadProfileInt(const char* path, const char* section, const char* key, int fallback);

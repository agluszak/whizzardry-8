#pragma once
#include "compat/kernel32.h"
#include <SDL3/SDL.h>

void SetInputWindow(SDL_Window* window);
void HandleInputEvent(const SDL_Event& event);
void GetGameMousePosition(POINT* point);
bool SetGameCursorRect(const RECT* rect);
bool WarpGameMouse(SDL_Window* window, int x, int y);

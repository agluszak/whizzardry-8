#pragma once
#include "Types.h"
#include <SDL3/SDL.h>

void SetInputWindow(SDL_Window* window);
void HandleInputEvent(const SDL_Event& event);
void GetGameMousePosition(SGPPoint* point);
bool SetGameCursorRect(const SGPRect* rect);
bool WarpGameMouse(SDL_Window* window, int x, int y);

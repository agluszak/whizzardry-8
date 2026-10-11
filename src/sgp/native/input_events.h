#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_video.h>

void SetInputWindow(SDL_Window* window);
void HandleInputEvent(const SDL_Event& event);
void GetGameMousePosition(SDL_Point* point);
bool SetGameCursorRect(const SDL_Rect* rect);
bool WarpGameMouse(SDL_Window* window, int x, int y);

void SetNativeTextInputEnabled(bool enabled);
bool NativeTextInputEnabled();

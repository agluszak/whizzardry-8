#pragma once

#include "compat/platform.h"

struct SDL_Window;
namespace w8_native
{
using WindowProcedure = LRESULT (*)(HWND, UINT, WPARAM, LPARAM);
/* Call on SDL's main thread, before using the message APIs. Detach before
   destroying the SDL window. All messages/timer callbacks run on this thread. */
HWND attach_window(SDL_Window* window, WindowProcedure procedure, int width, int height);
void detach_window(HWND window);
void post_quit(int code);
bool warp_mouse(HWND window, int x, int y);
} // namespace w8_native

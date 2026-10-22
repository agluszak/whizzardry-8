#pragma once

struct SDL_Window;

/* The game's resizable 4:3 window, at least 640x480. Creation registers it as
   the input window and destruction unregisters it. */
SDL_Window* CreateGameWindow(int width, int height, bool fullscreen);
void DestroyGameWindow(SDL_Window* window);
bool ConfigureGameWindow(SDL_Window* window, bool fullscreen, int width, int height);

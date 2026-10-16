#pragma once

struct SDL_Window;

SDL_Window* W8CreateGameWindow(int width, int height, bool fullscreen);
void W8DestroyGameWindow(SDL_Window* window);
bool W8ConfigureGameWindow(SDL_Window* window, bool fullscreen, int width, int height);
bool W8HasEnoughSaveSpace();

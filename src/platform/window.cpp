
#include "wiz8/platform/window.h"

#include <algorithm>

#include <SDL3/SDL_video.h>

#include "wiz8/platform/input.h"

SDL_Window* CreateGameWindow(int width, int height, bool fullscreen)
{
    SDL_Window* window =
        SDL_CreateWindow("Whizzardry 8", std::max(width, 640), std::max(height, 480),
                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) return nullptr;
    SDL_SetWindowMinimumSize(window, 640, 480);
    SDL_SetWindowAspectRatio(window, 4.f / 3.f, 4.f / 3.f);
    if (!ConfigureGameWindow(window, fullscreen, width, height)) {
        SDL_DestroyWindow(window);
        return nullptr;
    }
    SetInputWindow(window);
    return window;
}
void DestroyGameWindow(SDL_Window* window)
{
    if (!window) return;
    SetInputWindow(nullptr);
    SDL_DestroyWindow(window);
}
bool ConfigureGameWindow(SDL_Window* window, bool fullscreen, int width, int height)
{
    return window && width >= 640 && height >= 480 && SDL_SetWindowSize(window, width, height) &&
           SDL_SetWindowFullscreen(window, fullscreen) && SDL_SyncWindow(window);
}

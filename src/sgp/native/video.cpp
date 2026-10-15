#include "compat/video.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

#include <SDL3/SDL_video.h>
#include <wiz8/asset_paths.h>
#include <wiz8/filesystem.h>

#include "input.h"
#include "surrender/srDD_SDLGPU.h"

SDL_Window* W8CreateGameWindow(int width, int height, bool fullscreen)
{
    SDL_Window* window = SDL_CreateWindow("Whizzardry 8", std::max(width, 640),
                                          std::max(height, 480), SDL_WINDOW_RESIZABLE);
    if (!window) return nullptr;
    SDL_SetWindowMinimumSize(window, 640, 480);
    SDL_SetWindowAspectRatio(window, 4.f / 3.f, 4.f / 3.f);
    if (!W8ConfigureGameWindow(window, fullscreen, width, height)) {
        SDL_DestroyWindow(window);
        return nullptr;
    }
    SetInputWindow(window);
    return window;
}
void W8DestroyGameWindow(SDL_Window* window)
{
    if (!window) return;
    SetInputWindow(nullptr);
    SDL_DestroyWindow(window);
}
bool W8ConfigureGameWindow(SDL_Window* window, bool fullscreen, int width, int height)
{
    return window && width >= 640 && height >= 480 && SDL_SetWindowSize(window, width, height) &&
           SDL_SetWindowFullscreen(window, fullscreen) && SDL_SyncWindow(window);
}
srDD* W8CreateNativeRenderDevice()
{
    return srCreateSDLGPUDevice();
}
bool W8HasEnoughSaveSpace()
{
    std::error_code error;
    auto root = wiz8::path_from_utf8(w8_native::path_roots().user);
    while (!root.empty()) {
        const bool exists = std::filesystem::exists(root, error);
        if (error) return false;
        if (exists) break;
        const auto parent = root.parent_path();
        if (parent == root) return false;
        root = parent;
    }
    if (root.empty()) return false;
    if (!std::filesystem::is_directory(root, error) || error) return false;
    const auto space = std::filesystem::space(root, error);
    return !error && space.available != static_cast<std::uintmax_t>(-1) &&
           space.available >= 0x10000000;
}

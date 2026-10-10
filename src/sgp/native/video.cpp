#include "compat/video.h"
#include "native/input_events.h"
#include <wiz8/asset_paths.h>
#include <wiz8/filesystem.h>
#include "surrender/srDD_SDLGPU.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <climits>
#include <filesystem>
#include <sstream>
namespace
{
SDL_Window* native(SDL_Window* window) { return reinterpret_cast<SDL_Window*>(window); }
} // namespace
SDL_Window* W8CreateGameWindow(int width, int height, bool fullscreen)
{
    SDL_Window* window = SDL_CreateWindow("Whizzardry 8", std::max(width, 640),
                                          std::max(height, 480), SDL_WINDOW_RESIZABLE);
    if (!window)
        return nullptr;
    SDL_SetWindowMinimumSize(window, 640, 480);
    SDL_SetWindowAspectRatio(window, 4.f / 3.f, 4.f / 3.f);
    SDL_Window* handle = reinterpret_cast<SDL_Window*>(window);
    if (!W8ConfigureGameWindow(handle, fullscreen, width, height))
    {
        SDL_DestroyWindow(window);
        return nullptr;
    }
    SetInputWindow(window);
    return handle;
}
void W8DestroyGameWindow(SDL_Window* window)
{
    if (!window)
        return;
    SetInputWindow(nullptr);
    SDL_DestroyWindow(native(window));
}
bool W8ConfigureGameWindow(SDL_Window* window, bool fullscreen, int width, int height)
{
    return window && width >= 640 && height >= 480 &&
           SDL_SetWindowSize(native(window), width, height) &&
           SDL_SetWindowFullscreen(native(window), fullscreen) && SDL_SyncWindow(native(window));
}
srDD* W8CreateNativeRenderDevice() { return srCreateSDLGPUDevice(); }
bool W8VideoGetClientRect(SDL_Window* window, SGPRect* rect)
{
    if (!window || !rect)
        return FALSE;
    /* Recovered cursor/viewport logic operates in the 640x480 design space.
       SDL event and warp adapters perform the actual window scaling. */
    *rect = {0, 0, 640, 480};
    return TRUE;
}
bool W8VideoClientToScreen(SDL_Window* window, SGPPoint* point) { return window && point; }
bool W8VideoGetWindowRect(SDL_Window* window, SGPRect* rect)
{
    if (!window || !rect)
        return FALSE;
    int x, y, width, height;
    if (!SDL_GetWindowPosition(native(window), &x, &y) ||
        !SDL_GetWindowSize(native(window), &width, &height))
        return FALSE;
    *rect = {x, y, x + width, y + height};
    return TRUE;
}
bool W8VideoWarpMouse(SDL_Window* window, int x, int y) { return WarpGameMouse(native(window), x, y); }
bool W8VideoShowCursor(bool visible) { return visible ? SDL_ShowCursor() : SDL_HideCursor(); }
bool W8VideoShowWindow(SDL_Window* window, int command)
{
    if (!window)
        return FALSE;
    if (command == 6)
        return SDL_MinimizeWindow(native(window));
    return SDL_ShowWindow(native(window)) && SDL_RestoreWindow(native(window));
}
bool W8VideoRaiseWindow(SDL_Window* window) { return window && SDL_RaiseWindow(native(window)); }
bool W8VideoCloseWindow(SDL_Window* window) { return window && SDL_HideWindow(native(window)); }
unsigned int W8TotalPhysicalMemory()
{
    const int megabytes = SDL_GetSystemRAM();
    if (megabytes <= 0)
        return 0;
    const uint64_t bytes = uint64_t(megabytes) * 1024 * 1024;
    return std::min<uint64_t>(bytes, UINT_MAX);
}
bool W8HasEnoughSaveSpace()
{
    std::error_code error;
    auto root = wiz8::path_from_utf8(w8_native::path_roots().user);
    while (!root.empty())
    {
        const bool exists = std::filesystem::exists(root, error);
        if (error)
            return false;
        if (exists)
            break;
        const auto parent = root.parent_path();
        if (parent == root)
            return false;
        root = parent;
    }
    if (root.empty())
        return false;
    if (!std::filesystem::is_directory(root, error) || error)
        return false;
    const auto space = std::filesystem::space(root, error);
    return !error && space.available != static_cast<std::uintmax_t>(-1) && space.available >= 0x10000000;
}

int W8ReadProfileInt(const char* path, const char* section, const char* key, int fallback)
{
    std::istringstream input;
    try {
        auto file = wiz8::open_file(path);
        if (file->size() > 1024 * 1024) return fallback;
        std::string text(static_cast<std::size_t>(file->size()), '\0');
        if (file->read(text.data(), text.size()).bytes != text.size()) return fallback;
        input.str(text);
    } catch (const std::exception&) { return fallback; }
    std::string line, current;
    auto trim = [](std::string value)
    {
        auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return std::string();
        return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
    };
    while (std::getline(input, line))
    {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;
        if (line[0] == '[')
        {
            auto end = line.find(']');
            if (end != std::string::npos)
                current = trim(line.substr(1, end - 1));
            continue;
        }
        auto separator = line.find('=');
        if (separator == std::string::npos || stricmp(current.c_str(), section))
            continue;
        auto name = trim(line.substr(0, separator));
        if (!stricmp(name.c_str(), key))
            return atoi(line.c_str() + separator + 1);
    }
    return fallback;
}

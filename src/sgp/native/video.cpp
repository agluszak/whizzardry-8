#include "compat/video.h"
#include "compat/platform.h"
#include "native/input_events.h"
#include "platform_paths.h"
#include "surrender/srDD_SDLGPU.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <climits>
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#include "platform_fs_windows.h"
#else
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif
namespace
{
SDL_Window* native(HWND window) { return reinterpret_cast<SDL_Window*>(window); }
} // namespace
HWND W8CreateGameWindow(int width, int height, bool fullscreen)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
        return nullptr;
    SDL_Window* window = SDL_CreateWindow("Whizzardry 8", std::max(width, 640),
                                          std::max(height, 480), SDL_WINDOW_RESIZABLE);
    if (!window)
        return nullptr;
    SDL_SetWindowMinimumSize(window, 640, 480);
    SDL_SetWindowAspectRatio(window, 4.f / 3.f, 4.f / 3.f);
    HWND handle = reinterpret_cast<HWND>(window);
    if (!W8ConfigureGameWindow(handle, fullscreen, width, height))
    {
        SDL_DestroyWindow(window);
        return nullptr;
    }
    SetInputWindow(window);
    return handle;
}
void W8DestroyGameWindow(HWND window)
{
    if (!window)
        return;
    SetInputWindow(nullptr);
    SDL_DestroyWindow(native(window));
}
bool W8ConfigureGameWindow(HWND window, bool fullscreen, int width, int height)
{
    return window && width >= 640 && height >= 480 &&
           SDL_SetWindowSize(native(window), width, height) &&
           SDL_SetWindowFullscreen(native(window), fullscreen) && SDL_SyncWindow(native(window));
}
srDD* W8CreateNativeRenderDevice() { return srCreateSDLGPUDevice(); }
BOOL W8VideoGetClientRect(HWND window, RECT* rect)
{
    if (!window || !rect)
        return FALSE;
    /* Recovered cursor/viewport logic operates in the 640x480 design space.
       SDL event and warp adapters perform the actual window scaling. */
    *rect = {0, 0, 640, 480};
    return TRUE;
}
BOOL W8VideoClientToScreen(HWND window, POINT* point) { return window && point; }
BOOL W8VideoGetWindowRect(HWND window, RECT* rect)
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
BOOL W8VideoWarpMouse(HWND window, int x, int y) { return WarpGameMouse(native(window), x, y); }
BOOL W8VideoShowCursor(BOOL visible) { return visible ? SDL_ShowCursor() : SDL_HideCursor(); }
BOOL W8VideoShowWindow(HWND window, int command)
{
    if (!window)
        return FALSE;
    if (command == 6)
        return SDL_MinimizeWindow(native(window));
    return SDL_ShowWindow(native(window)) && SDL_RestoreWindow(native(window));
}
BOOL W8VideoRaiseWindow(HWND window) { return window && SDL_RaiseWindow(native(window)); }
BOOL W8VideoCloseWindow(HWND window) { return window && SDL_HideWindow(native(window)); }
unsigned int W8TotalPhysicalMemory()
{
#if defined(_WIN32)
    uint64_t bytes = uint64_t(SDL_GetSystemRAM()) * 1024 * 1024;
#elif defined(__APPLE__)
    uint64_t bytes = 0;
    size_t size = sizeof(bytes);
    if (sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0))
        return 0;
#else
    long pages = sysconf(_SC_PHYS_PAGES), page_size = sysconf(_SC_PAGESIZE);
    if (pages < 0 || page_size < 0)
        return 0;
    uint64_t bytes = uint64_t(pages) * page_size;
#endif
    return std::min<uint64_t>(bytes, UINT_MAX);
}
int W8UsedPageFileBytes()
{
#if defined(_WIN32)
    return std::min<uint64_t>(w8_native::committed_memory(), INT_MAX);
#elif defined(__APPLE__)
    xsw_usage usage{};
    size_t size = sizeof(usage);
    if (sysctlbyname("vm.swapusage", &usage, &size, nullptr, 0))
        return 0;
    return std::min<uint64_t>(usage.xsu_used, INT_MAX);
#else
    std::ifstream memory("/proc/meminfo");
    std::string name, rest;
    uint64_t amount;
    while (memory >> name >> amount)
    {
        std::getline(memory, rest);
        if (name == "Committed_AS:")
            return std::min<uint64_t>(amount * 1024, INT_MAX);
    }
    return 0;
#endif
}
bool W8HasEnoughSaveSpace()
{
    std::error_code error;
    auto root = std::filesystem::path(w8_native::path_roots().user);
    while (!root.empty() && !std::filesystem::exists(root, error))
        root = root.parent_path();
    auto space = std::filesystem::space(root, error);
    return !error && space.available >= 0x10000000;
}

int W8ReadProfileInt(const char* path, const char* section, const char* key, int fallback)
{
    std::ifstream input(w8_native::read_path(path));
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

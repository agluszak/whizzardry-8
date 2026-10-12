#include <cstdint>
#include <SDL3/SDL_video.h>

#include "surrender/srWindow.h"

/* Native window handles are SDL_Window pointers carried as unsigned long. */
namespace {
SDL_Window* nativeWindow(std::uintptr_t handle)
{
    return reinterpret_cast<SDL_Window*>(handle);
}
} // namespace

std::int32_t srWindow::getWidth(std::uintptr_t handle)
{
    std::int32_t width = 0;
    if (isWindow(handle) == 0 || !SDL_GetWindowSizeInPixels(nativeWindow(handle), &width, 0)) {
        return 0;
    }
    return width;
}

std::int32_t srWindow::getHeight(std::uintptr_t handle)
{
    std::int32_t height = 0;
    if (isWindow(handle) == 0 || !SDL_GetWindowSizeInPixels(nativeWindow(handle), 0, &height)) {
        return 0;
    }
    return height;
}

bool srWindow::isWindow(std::uintptr_t handle)
{
    return handle != 0;
}

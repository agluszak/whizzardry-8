#include "surrender/srWindow.h"

#include <SDL3/SDL.h>

/* Native window handles are SDL_Window pointers carried as unsigned long. */
namespace {
SDL_Window* nativeWindow(w8_ulong_ptr handle)
{
    return reinterpret_cast<SDL_Window*>(handle);
}
} // namespace

w8_long srWindow::getWidth(w8_ulong_ptr handle)
{
    int width = 0;
    if (isWindow(handle) == 0 || !SDL_GetWindowSizeInPixels(nativeWindow(handle), &width, 0)) {
        return 0;
    }
    return width;
}

w8_long srWindow::getHeight(w8_ulong_ptr handle)
{
    int height = 0;
    if (isWindow(handle) == 0 || !SDL_GetWindowSizeInPixels(nativeWindow(handle), 0, &height)) {
        return 0;
    }
    return height;
}

int srWindow::isWindow(w8_ulong_ptr handle)
{
    return handle != 0;
}

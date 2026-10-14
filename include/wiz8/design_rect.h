#pragma once

#include <SDL3/SDL_rect.h>

// Bounds in the 640x480 design space; right and bottom are exclusive.
struct DesignRect {
    int left;
    int top;
    int right;
    int bottom;
    bool operator==(const DesignRect&) const = default;
};

// Scale origin and extent separately, preserving the renderer's integer rounding.
inline SDL_Rect ToSurfaceRect(const DesignRect& rect, int width, int height)
{
    return {width * rect.left / 640, height * rect.top / 480, width * (rect.right - rect.left) / 640,
            height * (rect.bottom - rect.top) / 480};
}

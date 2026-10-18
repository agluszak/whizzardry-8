#pragma once

#include <SDL3/SDL_rect.h>

/* An edge rectangle, as the retail layout tables store them. Runtime widget
   geometry is an SDL_Rect; convert at the table boundary. */
struct W8ControlsRect {
    int left;
    int top;
    int right;
    int bottom;
};

inline W8ControlsRect ToEdges(const SDL_Rect& rect)
{
    return {rect.x, rect.y, rect.x + rect.w, rect.y + rect.h};
}

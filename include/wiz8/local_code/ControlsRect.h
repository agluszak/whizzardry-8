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

inline SDL_Rect ToSDLRect(const W8ControlsRect& edges)
{
    return {edges.left, edges.top, edges.right - edges.left, edges.bottom - edges.top};
}

inline W8ControlsRect ToEdges(const SDL_Rect& rect)
{
    return {rect.x, rect.y, rect.x + rect.w, rect.y + rect.h};
}

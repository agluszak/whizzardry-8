#pragma once

#include "wiz8/ui_layer.h"
#include "wiz8/button_interaction.h"
#include "wiz8/catalog_artwork.h"
#include <array>
#include <functional>

// A scrollbar owns three input targets and paints its retail artwork directly.
class W8RangeControl {
public:
    W8RangeControl(int left, int top, int right, int bottom);
    void SetRange(int first, int second);
    void SetValue(int value);
    void Decrement() { AdjustValue(1); }
    void Increment() { AdjustValue(-1); }
    void AdjustValue(int steps);
    void SetEnabled(bool enabled);
    void SetActive(bool active);
    void SetBounds(SDL_Rect bounds);
    void InvalidateAll() { m_dirty = true; }
    void Redraw(SDL_Surface& target, bool full_redraw);
    int m_minimum = 0;
    int m_maximum = 1;
    int m_value = 0;
    bool m_enabled = false;
    std::function<void(W8RangeControl*)> on_change;

private:
    UiResult HandlePointer(int part, const UiPointerEvent& event);
    UiLayer m_input;
    CatalogArtwork m_artwork = CatalogArtwork::Load(0x86);
    std::array<HitTarget*, 3> m_targets;
    std::array<ButtonInteraction, 2> m_arrows;
    SDL_Rect m_bounds{};
    SDL_Rect m_track{};
    int m_thumb_height = 0;
    int m_travel = 0;
    int m_drag_y = 0;
    float m_position = 0;
    bool m_active = false;
    bool m_dirty = true;
    bool m_dragging = false;
    bool m_hovered = false;
};

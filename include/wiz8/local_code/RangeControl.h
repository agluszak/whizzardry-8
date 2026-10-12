#pragma once

#include <cstdint>
#include "wiz8/local_code/Controls.h"
#include "wiz8/local_code/TextControl.h"

/* Shared declaration owner; implementation remains in Local Code\Controls.cpp. */

class W8RangeControl;

// VTABLE: WIZ8 0x005ed6fc
class W8RangeButton : public W8TextControl {
public:
    W8RangeButton(Controls* panel, std::uint32_t region, std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom,
                  std::optional<CatalogArtwork> image_artwork, std::int32_t normal_sprite, std::int32_t pressed_sprite,
                  std::int32_t alternate_normal_sprite, std::int32_t alternate_pressed_sprite, std::int32_t disabled_sprite,
                  std::int16_t direction, W8RangeControl* range);

    virtual void OnLeftButtonDown(std::int32_t event) override;
    virtual void ActivatePrimary(std::int32_t event) override;
    virtual void AdjustValue(std::int32_t steps) override;

protected:
    std::int16_t m_direction; /* zero decrements */
    std::uint16_t pad_ba;
    W8RangeControl* m_range;
};

class W8VerticalRangeThumb;

class W8RangeListener {
public:
    virtual void OnRangeChanged(W8RangeControl* control) = 0;
};

/* Common base of the camp screen's three range listeners (item, spell-realm
   and stats): each owns one W8RangeControl. */
class W8CampRangeListener : public W8RangeListener {
public:
    /* Re-invalidates the range control when the visible pool
       changed and always repaints it. */
    void UpdateRange(bool range_changed);
    W8RangeControl* m_range;
};

// VTABLE: WIZ8 0x005ed74c
class W8RangeControl : public Controls {
public:
    friend class W8VerticalRangeThumb;

    W8RangeControl(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom);
    ~W8RangeControl();

    void SetRange(std::int32_t first, std::int32_t second);
    void SetValue(std::int32_t value);
    void Decrement();
    void Increment();
    void SetRangeEnabled(bool enabled);

    void AdjustValue(std::int32_t steps)
    {
        for (; steps > 0; --steps)
            Decrement();
        for (; steps < 0; ++steps)
            Increment();
    }

    std::int32_t m_minimum;
    std::int32_t m_maximum;
    std::int32_t m_value;
    W8Widget* m_decrement;
    W8Widget* m_increment;
    W8VerticalRangeThumb* m_thumb;
    W8RangeListener* m_listener;
    bool m_enabled; /* binary enable gating Decrement/Increment */
};

class W8HorizontalRangeThumb;

class W8HorizontalRangeThumbListener {
public:
    virtual void OnDrag(W8HorizontalRangeThumb* thumb) = 0;
    virtual void OnDragEnd(W8HorizontalRangeThumb* thumb) = 0;
};

// VTABLE: WIZ8 0x005ed66c
class W8HorizontalRangeThumb : public W8Widget {
public:
    W8HorizontalRangeThumb(Controls* panel, std::uint32_t region, std::int32_t left, std::int32_t top,
                           CatalogArtwork artwork, std::int32_t background_sprite,
                           std::int32_t normal_thumb_sprite, std::int32_t hovered_thumb_sprite,
                           std::int32_t disabled_thumb_sprite);
    virtual void Redraw(bool full_redraw) override;
    void UpdatePixelPosition();
    virtual void OnLeftButtonDown(std::int32_t event) override;
    virtual void OnLeftButtonUp(std::int32_t event) override;
    virtual void OnMouseEnter(std::int32_t event) override;
    virtual void OnMouseLeave(std::int32_t event) override;
    virtual void OnMouseMove(std::int32_t event) override;

protected:
    CatalogArtwork m_artwork;
    std::int32_t m_backgroundSprite;
    std::int32_t m_normalThumbSprite;
    std::int32_t m_hoveredThumbSprite;
    std::int32_t m_disabledThumbSprite;
    std::int32_t m_trackLength;
    std::int32_t m_thumbWidth;
    std::int32_t m_pixelPosition;
    std::int32_t m_dragCoordinate;
    bool m_hovered;  /* cursor over the thumb */
    bool m_dragging; /* thumb drag in progress */
    std::uint8_t pad_5e[2];

public:
    /* OptionsScreen.cpp sets these bounds and attaches the listener directly. */
    float m_minimumPosition;
    float m_maximumPosition;
    float m_position;
    W8HorizontalRangeThumbListener* m_listener;

protected:
    void ClampPositionAndInvalidate();
};

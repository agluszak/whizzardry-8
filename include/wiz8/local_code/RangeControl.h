#pragma once

#include "wiz8/local_code/Controls.h"
#include "wiz8/local_code/TextControl.h"

#include <memory>

/* Shared declaration owner; implementation remains in Local Code\Controls.cpp. */

class W8RangeControl;

// VTABLE: WIZ8 0x005ed6fc
class W8RangeButton : public W8TextControl {
public:
    W8RangeButton(unsigned int region, int left, int top, int right, int bottom,
                  std::optional<CatalogArtwork> image_artwork, W8SpriteStates sprites,
                  short direction, W8RangeControl* range);

    virtual void OnLeftButtonDown(int event) override;
    virtual void ActivatePrimary(int event) override;
    virtual void AdjustValue(int steps) override;

protected:
    short m_direction; /* zero decrements */
    unsigned short pad_ba;
    W8RangeControl* m_range;
};

class W8VerticalRangeThumb;

class W8RangeListener {
public:
    virtual void OnRangeChanged(W8RangeControl* control) = 0;
};

// VTABLE: WIZ8 0x005ed74c
class W8RangeControl : public Controls {
public:
    friend class W8VerticalRangeThumb;

    W8RangeControl(int left, int top, int right, int bottom);
    ~W8RangeControl();

    void SetRange(int first, int second);
    void SetValue(int value);
    void Decrement();
    void Increment();
    /* A disabled range ignores steps and greys its buttons and thumb. */
    void SetEnabled(bool enabled) override;

    void AdjustValue(int steps)
    {
        for (; steps > 0; --steps)
            Decrement();
        for (; steps < 0; ++steps)
            Increment();
    }

    int m_minimum;
    int m_maximum;
    int m_value;
    W8Widget* m_decrement;
    W8Widget* m_increment;
    W8VerticalRangeThumb* m_thumb;
    W8RangeListener* m_listener;
};

/* Common base of the camp screen's three range listeners (item, spell-realm
   and stats): each owns one active W8RangeControl and listens to it. */
class W8CampRangeListener : public W8RangeListener {
public:
    explicit W8CampRangeListener(const W8ControlsRect& bounds);
    /* Re-invalidates the range control when the visible pool
       changed and always repaints it. */
    void UpdateRange(bool range_changed);
    std::unique_ptr<W8RangeControl> m_range;
};

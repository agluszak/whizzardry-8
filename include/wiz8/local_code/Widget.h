#pragma once

#include <cstdint>
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/regions.h"

/* Shared declaration owner; implementation remains in Local Code\Controls.cpp. */

typedef void (*W8ControlCallback)();
struct Controls;
struct W8Region;

// VTABLE: WIZ8 0x005ed5bc
class W8Widget {
public:
    friend struct Controls;

    W8Widget()
        : m_enabled(1), m_active(0), m_dirty(0), m_left(0), m_top(0), m_right(0), m_bottom(0),
          m_region(-1), m_pPanel(0), m_primaryActivationCallback(0), m_leftButtonDownCallback(0),
          m_secondaryActivationCallback(0), m_rightButtonDownCallback(0),
          m_leftDoubleClickCallback(0)
    {
    }
    W8Widget(Controls* owner, std::uint32_t region, std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom);

    W8Widget(const W8Widget&) = delete;
    W8Widget& operator=(const W8Widget&) = delete;

    // Transfer a panel-owned child; detach with Controls::TakeControl instead.
    void SetPanel(Controls& panel);
    void SetRegion(std::uint32_t region);
    void Invalidate(bool immediate);
    void SetActive(bool active);
    void EnableRegionHelp(std::int32_t help_text_id);
    void DisableRegionHelp();

    virtual ~W8Widget();

    virtual void SetEnabled(bool enabled);
    /* Default hooks; Redraw's flag is the panel's dirty state. */
    // FUNCTION: WIZ8 0x005b1be0
    virtual void Redraw(bool) {}
    virtual void SetBounds(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom);
    virtual void SetBoundsFromRect(const W8ControlsRect* bounds);
    virtual void AddLayoutFlags(std::uint32_t) {}
    virtual void SetAlternateTextEnabled(bool) {}
    virtual void OnMouseEnter(std::int32_t) {}
    virtual void OnMouseLeave(std::int32_t) {}
    virtual void OnMouseMove(std::int32_t) {}
    virtual void AdjustValue(std::int32_t) {}
    virtual void OnLeftButtonDown(std::int32_t) {}
    virtual void OnRightButtonDown(std::int32_t) {}
    virtual void OnLeftButtonUp(std::int32_t) {}
    virtual void OnRightButtonUp(std::int32_t) {}
    virtual void OnLeftButtonDoubleClick(std::int32_t) {}
    virtual void ActivatePrimary(std::int32_t) {}
    virtual void ActivateSecondary(std::int32_t) {}

protected:
    bool OwnsRegion() const;
    void ReleaseRegion();
    void UpdateRegionBounds(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom);

public:
    /* Read from outside the class by Local Screens\RCSCommon.cpp, which is what
       keeps the three flags reachable rather than protected. */
    bool m_enabled; /* interaction and enabled appearance */
    bool m_active;  /* panel participation and region input */
    bool m_dirty;   /* pending widget redraw */
    std::uint8_t pad_007;
    /* The widget's rectangle, relative to the owner's origin. The
       constructor adds the origin to all four before handing them to the
       region, which is what makes right and bottom edges rather than a size. */
    std::int32_t m_left;
    std::int32_t m_top;
    std::int32_t m_right;
    std::int32_t m_bottom;
    std::int32_t m_region; /* handed to DisableRegionInput unless -1 */
    Controls* m_pPanel;
    W8ControlCallback m_primaryActivationCallback; /* invoked by text-control activation */
    W8ControlCallback m_leftButtonDownCallback;
    W8ControlCallback m_secondaryActivationCallback;
    W8ControlCallback m_rightButtonDownCallback;
    W8ControlCallback m_leftDoubleClickCallback;
    OwnedRegion m_owned_region;
};

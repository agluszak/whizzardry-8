#pragma once

#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/regions.h"

/* Shared declaration owner; implementation remains in Local Code\Controls.cpp. */

typedef void (*W8ControlCallback)();
struct Controls;
struct W8Region;

// VTABLE: WIZ8 0x005ed5bc
class W8Widget : public W8RegionHandler {
public:
    friend struct Controls;

    W8Widget()
        : m_enabled(1), m_active(0), m_dirty(0), m_region(-1), m_pPanel(0), m_primaryActivationCallback(0), m_leftButtonDownCallback(0),
          m_secondaryActivationCallback(0), m_rightButtonDownCallback(0),
          m_leftDoubleClickCallback(0)
    {
    }
    W8Widget(unsigned int region, int left, int top, int right, int bottom);

    W8Widget(const W8Widget&) = delete;
    W8Widget& operator=(const W8Widget&) = delete;

    void SetRegion(unsigned int region);
    void Invalidate(bool immediate);
    void SetActive(bool active);
    void EnableRegionHelp(int help_text_id);
    void DisableRegionHelp();

    virtual ~W8Widget();
    int HandleRegionInput(W8Region& region, const InputEvent& input) override;

    virtual void SetEnabled(bool enabled);
    /* Default hooks; Redraw's flag is the panel's dirty state. */
    // FUNCTION: WIZ8 0x005b1be0
    virtual void Redraw(bool) {}
    /* The widget's rectangle relative to its panel, and on screen. A widget
       without a panel sits at the screen origin. */
    const SDL_Rect& Bounds() const { return m_rect; }
    SDL_Rect ScreenBounds() const;
    /* A screen point in this widget's own coordinates. */
    SDL_Point ToLocal(SDL_Point screen) const;
    /* The one way to move a widget: hit testing and layout follow. */
    void SetBounds(const SDL_Rect& bounds);
    virtual void AddLayoutFlags(unsigned int) {}
    virtual void SetAlternateTextEnabled(bool) {}
    virtual void OnMouseEnter(int) {}
    virtual void OnMouseLeave(int) {}
    virtual void OnMouseMove(int) {}
    virtual void AdjustValue(int) {}
    virtual void OnLeftButtonDown(int) {}
    virtual void OnRightButtonDown(int) {}
    virtual void OnLeftButtonUp(int) {}
    virtual void OnRightButtonUp(int) {}
    virtual void OnLeftButtonDoubleClick(int) {}
    virtual void ActivatePrimary(int) {}
    virtual void ActivateSecondary(int) {}

protected:
    /* Called after the widget's rectangle or its panel's origin changed,
       including attachment to a panel. Overrides that derive screen
       coordinates from the panel origin must call the base. */
    virtual void OnGeometryChanged();
    bool OwnsRegion() const;
    void ReleaseRegion();
    SDL_Point PanelOrigin() const;

public:
    /* Read from outside the class by Local Screens\RCSCommon.cpp, which is what
       keeps the three flags reachable rather than protected. */
    bool m_enabled; /* interaction and enabled appearance */
    bool m_active;  /* panel participation and region input */
    bool m_dirty;   /* pending widget redraw */
    unsigned char pad_007;
    int m_region; /* handed to DisableRegionInput unless -1 */
    Controls* m_pPanel;
    W8ControlCallback m_primaryActivationCallback; /* invoked by text-control activation */
    W8ControlCallback m_leftButtonDownCallback;
    W8ControlCallback m_secondaryActivationCallback;
    W8ControlCallback m_rightButtonDownCallback;
    W8ControlCallback m_leftDoubleClickCallback;
    OwnedRegion m_owned_region;

private:
    void UpdateRegionBounds();

    SDL_Rect m_rect{}; /* relative to the panel origin */
};

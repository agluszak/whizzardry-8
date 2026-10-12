#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "compat/compiler.h"
#include "wiz8/button_interaction.h"
#include "local_code/ControlsRect.h"
#include "wiz8/local_code/Widget.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/catalog_artwork.h"

struct SpriteSheet;

/* Shared declaration owner; implementation remains in Local Code\Controls.cpp. */

extern const unsigned int g_W8TextControlLayoutToggle;
extern const unsigned int g_W8TextControlLayoutTextBesideImage;
extern const unsigned int g_W8TextControlLayoutImageLeft;
extern const unsigned int g_W8TextControlLayoutStayLatched;
extern const unsigned int g_W8TextControlLayoutLatchedImage;
extern const unsigned int g_W8TextControlLayoutImageAtOrigin;
extern const unsigned int g_W8TextControlSilentHover;

enum W8TextControlInputFlag {
    W8_TEXT_CONTROL_SILENT = 0x20u,
    W8_TEXT_CONTROL_ACTIVATE_WHILE_HELD = 0x100u
};

/* Which sprite of a control's image shows in each visual state; -1 shows
   none. The alternate pair is used while alternate text is enabled. */
struct W8SpriteStates {
    int normal = -1;
    int pressed = -1;
    int alternate_normal = -1;
    int alternate_pressed = -1;
    int disabled = -1;
};

// VTABLE: WIZ8 0x005ed604
class W8TextControl : public W8Widget {
public:
    // VTABLE: WIZ8 0x005ed664
    class Listener {
    public:
        virtual void OnPrimary(W8TextControl* control) = 0;
        virtual void OnSecondary(W8TextControl*) {}
    };

    W8TextControl();
    // A right or bottom of zero takes the edge from the normal sprite's size.
    W8TextControl(unsigned int region, int left, int top, int right, int bottom,
                  std::optional<CatalogArtwork> image_artwork = std::nullopt,
                  W8SpriteStates sprites = {});
    void SetImage(CatalogArtwork artwork);
    void SetItemImage(int item_id);
    void ClearImage();

    bool MeasureText();
    SDL_Point ImageOrigin();
    void Invalidate(bool immediate);
    virtual void SetEnabled(bool enabled) override;
    virtual void Redraw(SDL_Surface& target, bool full_redraw) override;
    /* Centre is panel-relative, like the initial sprite bounds. */
    void SetCircularHitArea(SDL_Point center, int radius);
    virtual void AddLayoutFlags(unsigned int flags) override;
    virtual void SetAlternateTextEnabled(bool enabled) override;
    void RemoveLayoutFlags(unsigned int flags);
    /* Held down, and latched on (a toggle, tab or radio entry that is set). */
    bool IsPressed() const { return m_interaction.pressed; }
    bool IsSelected() const { return m_interaction.selected; }
    virtual void EnableSecondaryState(bool immediate);
    virtual void DisableSecondaryState(bool immediate);
    virtual void OnMouseEnter(int event) override;
    virtual void OnMouseLeave(int event) override;
    virtual void OnLeftButtonDown(int event) override;
    virtual void OnRightButtonDown(int event) override;
    virtual void OnLeftButtonUp(int event) override;
    virtual void OnRightButtonUp(int event) override;
    virtual void OnLeftButtonDoubleClick(int event) override;
    virtual void ActivatePrimary(int event) override;
    virtual void ActivateSecondary(int event) override;
    /* Lays text out in this panel-relative rectangle instead of the widget's. */
    void UpdateTextBounds(int left, int top, int right, int bottom);

protected:
    ButtonInteraction m_interaction;
    ButtonPolicy InteractionPolicy() const;

public:
    unsigned int m_layoutFlags;  /* layout and input behavior masks */
    bool m_alternateTextEnabled; /* alternate text-selection flag */
    std::shared_ptr<const SpriteSheet> m_imageSprites;
    int m_imageYOffset;
    int m_normalSprite;
    int m_pressedSprite;
    int m_alternatePressedSprite;
    int m_alternateNormalSprite;
    int m_disabledSprite;
    short m_measured_w; /* -1 until measured */
    short m_measured_h;

public:
    W8TextBuffer m_textBuffer; /* complete typed subobject */
    int m_pressedTextOffset;
    /* Several owning panels install their listener immediately after
       construction; the pointer is the shared callback attachment point. */
    Listener* m_listener;

private:
    std::optional<W8ControlsRect> m_textBounds; /* set by UpdateTextBounds */
    struct CircleHitArea {
        SDL_Point center; // Widget-local, so it follows moves and reparenting.
        int radius;
    };
    std::optional<CircleHitArea> m_circleHitArea;

protected:
    void OnGeometryChanged() override;
    W8ControlsRect TextBounds();
    void UpdateTextLayout();
    void NotifyPrimaryActivation();
    void NotifySecondaryActivation();
};

// VTABLE: WIZ8 0x005ed758
class W8HelpTextControl : public W8TextControl {
public:
    W8HelpTextControl(unsigned int region, int left, int top, int right, int bottom);

    void SetRegionHelp(std::string_view text);
    virtual void OnMouseEnter(int event) override;
    virtual void OnLeftButtonDown(int event) override;
    virtual void OnRightButtonDown(int event) override;
    virtual void OnLeftButtonUp(int event) override;
    virtual void OnRightButtonUp(int event) override;
    virtual void OnLeftButtonDoubleClick(int event) override;

protected:
    std::string m_regionHelp;
};

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "compat/compiler.h"
#include "local_code/ControlsRect.h"
#include "wiz8/local_code/Widget.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/catalog_artwork.h"

struct SpriteSheet;

/* Shared declaration owner; implementation remains in Local Code\Controls.cpp. */

extern const std::uint32_t g_W8TextControlStatePressed;
extern const std::uint32_t g_W8TextControlStateSecondary;
extern const std::uint32_t g_W8TextControlLayoutToggle;
extern const std::uint32_t g_W8TextControlLayoutTextBesideImage;
extern const std::uint32_t g_W8TextControlLayoutImageLeft;
extern const std::uint32_t g_W8TextControlLayoutStayLatched;
extern const std::uint32_t g_W8TextControlLayoutLatchedImage;
extern const std::uint32_t g_W8TextControlLayoutImageAtOrigin;
extern const std::uint32_t g_W8TextControlSilentHover;

enum W8TextControlInputFlag {
    W8_TEXT_CONTROL_SILENT = 0x20u,
    W8_TEXT_CONTROL_ACTIVATE_WHILE_HELD = 0x100u
};
enum { W8_TEXT_CONTROL_ACTIVATED_WHILE_HELD = 0x04u };

// VTABLE: WIZ8 0x005ed604
class W8TextControl : public W8Widget {
public:
    friend class W8ControlSelection;

    // VTABLE: WIZ8 0x005ed664
    class Listener {
    public:
        virtual void OnPrimary(W8TextControl* control) = 0;
        virtual void OnSecondary(W8TextControl*) {}
    };

    W8TextControl();
    W8TextControl(Controls* panel, std::uint32_t region, std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom,
                  std::optional<CatalogArtwork> image_artwork, std::int32_t normal_sprite, std::int32_t pressed_sprite,
                  std::int32_t alternate_normal_sprite, std::int32_t alternate_pressed_sprite, std::int32_t disabled_sprite);

    void SetImage(CatalogArtwork artwork);
    void SetItemImage(std::int32_t item_id);
    void ClearImage();

    bool MeasureText();
    void GetTextOrigin(std::int32_t* px, std::int32_t* py);
    void Invalidate(bool immediate);
    virtual void SetEnabled(bool enabled) override;
    virtual void Redraw(bool full_redraw) override;
    void SetFlaggedRegionBounds(std::int32_t left, std::int32_t top, std::int32_t right);
    virtual void AddLayoutFlags(std::uint32_t flags) override;
    virtual void SetAlternateTextEnabled(bool enabled) override;
    void RemoveLayoutFlags(std::uint32_t flags);
    virtual void EnableSecondaryState(bool immediate);
    virtual void DisableSecondaryState(bool immediate);
    virtual void OnMouseEnter(std::int32_t event) override;
    virtual void OnMouseLeave(std::int32_t event) override;
    virtual void OnLeftButtonDown(std::int32_t event) override;
    virtual void OnRightButtonDown(std::int32_t event) override;
    virtual void OnLeftButtonUp(std::int32_t event) override;
    virtual void OnRightButtonUp(std::int32_t event) override;
    virtual void OnLeftButtonDoubleClick(std::int32_t event) override;
    virtual void ActivatePrimary(std::int32_t event) override;
    virtual void ActivateSecondary(std::int32_t event) override;
    void UpdateTextBounds(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom);
    virtual void SetBounds(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom) override;
    virtual void SetBoundsFromRect(const W8ControlsRect* bounds) override;

public:
    /* The state-5 controller persists two option bits by directly masking the
       controls' state words.  This is observed storage access, not an accessor
       API inferred for convenience. */
    std::uint32_t m_stateFlags;   /* paired state masks */
    std::uint32_t m_layoutFlags;  /* layout and input behavior masks */
    bool m_alternateTextEnabled; /* alternate text-selection flag */
    std::shared_ptr<const SpriteSheet> m_imageSprites;
    std::int32_t m_imageYOffset;
    std::int32_t m_normalSprite;
    std::int32_t m_pressedSprite;
    std::int32_t m_alternatePressedSprite;
    std::int32_t m_alternateNormalSprite;
    std::int32_t m_disabledSprite;
    std::int16_t m_measured_w; /* -1 until measured */
    std::int16_t m_measured_h;

public:
    W8TextBuffer m_textBuffer; /* complete typed subobject */
    std::int32_t m_pressedTextOffset;
    /* Several owning panels install their listener immediately after
       construction; the pointer is the shared callback attachment point. */
    Listener* m_listener;

protected:
    void UpdateTextLayout();
    void NotifyPrimaryActivation();
    void NotifySecondaryActivation();
};

// VTABLE: WIZ8 0x005ed758
class W8HelpTextControl : public W8TextControl {
public:
    W8HelpTextControl(Controls* panel, std::uint32_t region, std::int32_t left, std::int32_t top, std::int32_t right,
                      std::int32_t bottom);

    void SetRegionHelp(std::string_view text);
    virtual void OnMouseEnter(std::int32_t event) override;
    virtual void OnLeftButtonDown(std::int32_t event) override;
    virtual void OnRightButtonDown(std::int32_t event) override;
    virtual void OnLeftButtonUp(std::int32_t event) override;
    virtual void OnRightButtonUp(std::int32_t event) override;
    virtual void OnLeftButtonDoubleClick(std::int32_t event) override;

protected:
    std::string m_regionHelp;
};

/* Separate panel-owned arrays retain their slots until each control has been
   destroyed. Panel storage and region sets are released by the caller. */
inline void DestroyTextControls(W8TextControl** controls, std::int32_t count)
{
    for (std::int32_t index = 0; index < count; ++index) {
        if (controls[index] != 0) {
            delete controls[index];
            controls[index] = 0;
        }
    }
}

#pragma once

#include "Font.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/local_code/Widget.h"
#include "wiz8/regions.h"

#include <cstdint>
#include <functional>
#include <string_view>

/* Tooltip string-table sentinel; independent of the five frame indices. */
enum { W8_DIALOG_BUTTON_NO_TOOLTIP = -1 };

class W8DialogBase;
class W8ButtonWidget;

/* Callbacks stored through Configure and dispatched with the button as the subject. */
using W8ButtonCallback = std::function<void(W8ButtonWidget*)>;
enum class DialogButtonActivation { Press, Release };

/* Sprite or generic-frame push button. Draws through ButtonAppearance and
   receives input through the W8Widget region dispatch. A button may be a
   plain member (its region lives in the shared control set) or a child of a
   Controls panel. */
// VTABLE: WIZ8 0x005efa98
class W8ButtonWidget : public W8Widget {
public:
    W8ButtonWidget();
    explicit W8ButtonWidget(Controls& owner);
    ~W8ButtonWidget() override;
    void Draw();
    void Redraw(bool) override { Draw(); }
    void Reset();
    bool IsConfigured() const { return OwnsRegion(); }
    void SetPosition(std::int32_t x, std::int32_t y);
    void Resize(std::int32_t width, std::int32_t height);
    void SetText(std::string_view text);
    void SetTextOffsets(std::int32_t x, std::int32_t y, bool shift);
    void SetMultiColor(bool enabled);
    std::int32_t GetWidth() const { return m_appearance.bounds.w; }
    std::int32_t GetHeight() const { return m_appearance.bounds.h; }
    std::int32_t GetX() const { return m_appearance.bounds.x; }
    std::int32_t GetY() const { return m_appearance.bounds.y; }
    SDL_Point GetMousePosition() const { return GetRegionMousePosition(); }
    bool IsLeftButtonHeld() const
    {
        return OwnsRegion() && (Region().flags & W8_REGION_LEFT_BUTTON_HELD) != 0;
    }
    SDL_Rect GetBounds() const { return m_appearance.bounds; }
    void SetEnabled(bool enabled) override;
    void UpdateEnabledState(bool enabled)
    {
        if (!enabled) {
            SetEnabled(false);
            m_dirty = true;
        } else if (!IsEnabled()) {
            SetEnabled(true);
            m_dirty = true;
        }
    }
    bool IsEnabled() const { return OwnsRegion() && m_visual.enabled; }
    void SetPressed(bool pressed);
    bool IsPressed() const { return m_visual.pressed; }
    void SetVisible(bool visible);
    /* Installs the tooltip string on the mouse region (when one is configured
       and tooltips are on), or clears it. */
    void SetTooltipEnabled(bool enabled);
    std::int32_t GetUserData() const { return m_user_data; }
    /* Load the frame images, configure the owned region, and store the
       per-button callbacks, left-toggle flag, priority and tooltip index. */
    bool Configure(const char* image_path, std::int32_t gray_frame, std::int32_t off_normal_frame,
                   std::int32_t off_hover_frame, std::int32_t on_normal_frame, std::int32_t on_hover_frame,
                   W8ButtonCallback left_callback, W8ButtonCallback move_callback,
                   bool left_toggles, std::int32_t priority, std::int32_t tooltip_index,
                   W8ButtonCallback right_callback, W8ButtonCallback double_click_callback);
    bool Configure(std::shared_ptr<const SpriteSheet> sprites, std::int32_t gray_frame,
                   std::int32_t off_normal_frame, std::int32_t off_hover_frame, std::int32_t on_normal_frame,
                   std::int32_t on_hover_frame, W8ButtonCallback left_callback,
                   W8ButtonCallback move_callback, bool left_toggles, std::int32_t priority,
                   std::int32_t tooltip_index, W8ButtonCallback right_callback,
                   W8ButtonCallback double_click_callback);
    bool ConfigureIcon(const char* image_path, std::int32_t disabled_frame, std::int32_t normal_frame, std::int32_t priority,
                       std::int32_t tooltip_index)
    {
        return Configure(image_path, disabled_frame, normal_frame, BUTTON_NO_IMAGE, normal_frame,
                         BUTTON_NO_IMAGE, BUTTON_NO_CALLBACK, BUTTON_NO_CALLBACK, false, priority,
                         tooltip_index, BUTTON_NO_CALLBACK, BUTTON_NO_CALLBACK);
    }

    /* Configure a text button with an owner-local numeric callback payload. */
    bool ConfigureTextButton(std::string_view text, Font& font, std::int16_t fore_color,
                             std::int16_t shadow_color, std::int16_t x, std::int16_t y, std::int16_t width, std::int16_t height,
                             W8ButtonCallback left_callback, std::int32_t user_data,
                             GenericButtonImage image = {}, std::int32_t priority = REGION_PRIORITY_HIGHEST);
    /* Store the tooltip string-table index and, when tooltips are
       enabled, push its text onto the mouse region. */
    void SetTooltipIndex(std::int32_t tooltip_index);

    void OnMouseEnter(std::int32_t) override;
    void OnMouseLeave(std::int32_t) override;
    void OnMouseMove(std::int32_t) override;
    void OnLeftButtonDown(std::int32_t) override;
    void OnLeftButtonUp(std::int32_t) override;
    void OnLeftButtonDoubleClick(std::int32_t) override;
    void ActivatePrimary(std::int32_t) override;
    void OnRightButtonDown(std::int32_t) override;
    void OnRightButtonUp(std::int32_t) override;
    void ActivateSecondary(std::int32_t) override;

private:
    ButtonAppearance m_appearance;
    ButtonVisualState m_visual;
    SDL_MouseButtonFlags m_armed_buttons = 0;
    SDL_MouseButtonFlags m_repeated_buttons = 0;
    std::int32_t m_user_data = 0;
    W8Region& Region() const { return g_regions[m_region]; }
    void ConfigureRegion(std::int32_t priority);
    void ApplyRegionBounds();
    void HandleMouseEvent(std::int32_t reason);
    std::int32_t m_tooltip_index; /* g_localized_strings index, or -1 */
    W8ButtonCallback m_left_callback;
    W8ButtonCallback m_right_callback;
    W8ButtonCallback m_move_callback;
    W8ButtonCallback m_double_click_callback;
    bool m_left_toggles; /* left down xors CLICKED_ON */

public:
    /* Raised by the dialog factories on their item-row buttons; right
       down then toggles CLICKED_ON the same way left-toggle does. */
    bool m_right_toggles;
private:
    bool silent;       /* suppresses all button sounds */
    bool hover_silent; /* suppresses hover/exit sounds only */

public:
    /* Spinner repeats activate while held; a repeated press does not activate again on release. */
    bool m_repeat_enabled;
    DialogButtonActivation m_activation = DialogButtonActivation::Release;
    /* Set by the dialog factories to the owning dialog; the per-button
       dispatch callback reads it back when a stored callback needs it. */
    W8DialogBase* m_owner;

private:
    /* Snapshot of the live-dialog count; dispatch ignores events when a
       nested dialog has changed that count. */
    std::int32_t m_live_dialog_count;
};

#pragma once

#include "Font.h"
#include "wiz8/button_interaction.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/ui_layer.h"

#include <memory>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <functional>
#include <string_view>

/* Tooltip string-table sentinel; independent of the five frame indices. */
enum { W8_DIALOG_BUTTON_NO_TOOLTIP = -1 };

class W8DialogBase;
class UiLayer;
class W8ButtonWidget;

/* Callbacks stored through Configure and dispatched with the button as the subject. */
using W8ButtonCallback = std::function<void(W8ButtonWidget*)>;

/* Ordinary sprite or generic-frame button. Its layer owns input registration;
   this control owns behavior and appearance without a visual parent tree. */
// VTABLE: WIZ8 0x005efa98
class W8ButtonWidget {
public:
    W8ButtonWidget();
    explicit W8ButtonWidget(UiLayer& layer);
    ~W8ButtonWidget();
    void Draw(SDL_Surface& target);
    void Reset();
    bool IsConfigured() const { return m_target != nullptr; }
    const SDL_Rect& Bounds() const { return m_bounds; }
    bool m_dirty = true;
    void SetPosition(int x, int y);
    void Resize(int width, int height);
    void SetText(std::string_view text);
    void SetMultiColor(bool enabled);
    SDL_Point GetMousePosition() const;
    bool IsLeftButtonHeld() const
    {
        return m_target && (m_target->capturedButtons() & SDL_BUTTON_LMASK) != 0;
    }
    void SetEnabled(bool enabled);
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
    bool IsEnabled() const { return m_target && m_enabled; }
    void SetPressed(bool pressed);
    bool IsPressed() const { return m_interaction.pressed; }
    void SetVisible(bool visible);
    bool IsVisible() const { return m_visible; }
    void SetImage(ButtonImage image);
    void SetCircularHitArea(SDL_Point center, int radius);
    void SetTooltip(std::function<std::string()> text);
    std::function<void(bool)> on_hover;
    /* Installs the tooltip string on the input target (when one is configured
       and tooltips are on), or clears it. */
    void SetTooltipEnabled(bool enabled);
    int GetUserData() const { return m_user_data; }
    /* Load the frame images, configure the input target, and store the
       per-button callbacks, left-toggle flag, priority and tooltip index. */
    bool Configure(const char* image_path, int gray_frame, int off_normal_frame,
                   int off_hover_frame, int on_normal_frame, int on_hover_frame,
                   W8ButtonCallback left_callback, W8ButtonCallback move_callback,
                   bool left_toggles, int priority, int tooltip_index,
                   W8ButtonCallback right_callback, W8ButtonCallback double_click_callback);
    bool Configure(std::shared_ptr<const SpriteSheet> sprites, int gray_frame,
                   int off_normal_frame, int off_hover_frame, int on_normal_frame,
                   int on_hover_frame, W8ButtonCallback left_callback,
                   W8ButtonCallback move_callback, bool left_toggles, int priority,
                   int tooltip_index, W8ButtonCallback right_callback,
                   W8ButtonCallback double_click_callback);
    bool ConfigureIcon(const char* image_path, int disabled_frame, int normal_frame, int priority,
                       int tooltip_index)
    {
        return Configure(image_path, disabled_frame, normal_frame, BUTTON_NO_IMAGE, normal_frame,
                         BUTTON_NO_IMAGE, BUTTON_NO_CALLBACK, BUTTON_NO_CALLBACK, false, priority,
                         tooltip_index, BUTTON_NO_CALLBACK, BUTTON_NO_CALLBACK);
    }

    /* Configure a text button with an owner-local numeric callback payload. */
    bool ConfigureTextButton(std::string_view text, Font& font, short fore_color,
                             short shadow_color, short x, short y, short width, short height,
                             W8ButtonCallback left_callback, int user_data,
                             GenericButtonImage image = {}, int priority = 127);
    /* Store the tooltip string-table index and, when tooltips are
       enabled, push its text onto the input target. */
    void SetTooltipIndex(int tooltip_index);

private:
    std::unique_ptr<UiLayer> m_owned_layer;
    UiLayer* m_layer;
    HitTarget* m_target = nullptr;
    SDL_Rect m_bounds{};
    bool m_enabled = true;
    bool m_visible = true;
    void SetBounds(SDL_Rect bounds);
    UiResult HandlePointer(const UiPointerEvent& event);
    ButtonAppearance m_appearance;
    ButtonInteraction m_interaction;
    int m_user_data = 0;
    void ConfigureInput(int priority);
    int m_tooltip_index = W8_DIALOG_BUTTON_NO_TOOLTIP; /* g_localized_strings index, or -1 */
    W8ButtonCallback m_left_callback;
    W8ButtonCallback m_right_callback;
    W8ButtonCallback m_move_callback;
    W8ButtonCallback m_double_click_callback;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

public:
    ButtonPolicy policy;
    /* Set by the dialog factories to the owning dialog; the per-button
       dispatch callback reads it back when a stored callback needs it. */
    W8DialogBase* m_owner = nullptr;

};

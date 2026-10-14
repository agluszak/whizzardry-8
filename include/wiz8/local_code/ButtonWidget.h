#pragma once

#include "Font.h"
#include "wiz8/button_interaction.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"

#include <memory>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <functional>
#include <string_view>
#include <utility>

/* Tooltip string-table sentinel; independent of the five frame indices. */
enum { W8_DIALOG_BUTTON_NO_TOOLTIP = -1 };

class UiLayer;
class W8ButtonWidget;

/* Callbacks stored through Configure and dispatched with the button as the subject. */
using W8ButtonCallback = std::function<void(W8ButtonWidget*)>;

/* The sprite frame of each visual state; BUTTON_NO_IMAGE where a state has none. */
struct ButtonFrames {
    int disabled, normal, hover, pressed, pressed_hover;
};

/* What a button does. Callbacks capture whatever they act on. */
struct ButtonSetup {
    W8ButtonCallback on_click;
    W8ButtonCallback on_right_click;
    W8ButtonCallback on_move;
    W8ButtonCallback on_double_click;
    bool toggles = false; /* a left press latches the button */
    int priority = UI_PRIORITY_HIGHEST;
    int tooltip = W8_DIALOG_BUTTON_NO_TOOLTIP; /* g_localized_strings index */
};

/* Ordinary sprite or generic-frame button. Its layer owns input registration;
   this control owns behavior and appearance without a visual parent tree. */
// VTABLE: WIZ8 0x005efa98
class W8ButtonWidget {
public:
    explicit W8ButtonWidget(UiLayer& layer);
    ~W8ButtonWidget();
    void Draw(SDL_Surface& target);
    void Reset();
    bool IsConfigured() const
    {
        return m_target != nullptr;
    }
    const SDL_Rect& Bounds() const
    {
        return m_bounds;
    }
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
    bool IsEnabled() const
    {
        return m_target && m_enabled;
    }
    void SetPressed(bool pressed);
    bool IsPressed() const
    {
        return m_interaction.pressed;
    }
    void SetVisible(bool visible);
    bool IsVisible() const
    {
        return m_visible;
    }
    void SetImage(ButtonImage image);
    void SetCircularHitArea(SDL_Point center, int radius);
    void SetTooltip(std::function<std::string()> text);
    std::function<void(bool)> on_hover;
    /* Installs the tooltip string on the input target (when one is configured
       and tooltips are on), or clears it. */
    void SetTooltipEnabled(bool enabled);
    /* Load the frame images and configure the input target. */
    bool Configure(const char* image_path, ButtonFrames frames, ButtonSetup setup = {});
    bool Configure(std::shared_ptr<const SpriteSheet> sprites, ButtonFrames frames, ButtonSetup setup = {});
    /* A button with only disabled and normal artwork. */
    bool ConfigureIcon(const char* image_path, int disabled_frame, int normal_frame, ButtonSetup setup = {})
    {
        return Configure(image_path, {disabled_frame, normal_frame, BUTTON_NO_IMAGE, normal_frame, BUTTON_NO_IMAGE},
                         std::move(setup));
    }
    /* A generic-frame button showing `text`. It never toggles. */
    bool ConfigureTextButton(std::string_view text, Font& font, short fore_color, short shadow_color, SDL_Rect bounds,
                             ButtonSetup setup = {}, GenericButtonImage image = {});
    /* Store the tooltip string-table index and, when tooltips are
       enabled, push its text onto the input target. */
    void SetTooltipIndex(int tooltip_index);

private:
    UiLayer& m_layer;
    HitTarget* m_target = nullptr;
    SDL_Rect m_bounds{};
    bool m_enabled = true;
    bool m_visible = true;
    void SetBounds(SDL_Rect bounds);
    UiResult HandlePointer(const UiPointerEvent& event);
    ButtonAppearance m_appearance;
    ButtonInteraction m_interaction;
    void ConfigureInput();
    ButtonSetup m_setup;
    int m_handling_pointer = 0;

public:
    ButtonPolicy policy;
};

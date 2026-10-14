#pragma once

#include "Font.h"
#include "wiz8/button_interaction.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/ui_layer.h"
#include "wiz8/ui_input.h"

#include <memory>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <functional>
#include <string_view>

/* Tooltip string-table sentinel; independent of the five frame indices. */
enum { W8_DIALOG_BUTTON_NO_TOOLTIP = -1 };

class UiLayer;
class W8ButtonWidget;

/* Callbacks stored through Configure and dispatched with the button as the subject. */
using W8ButtonCallback = std::function<void(W8ButtonWidget*)>;

// A complete configuration. Reconfiguration replaces every callback (including
// hover), policy, tooltip and artwork mode; failed image loads leave it unchanged.
struct ButtonSetup {
    W8ButtonCallback on_click = {};
    W8ButtonCallback on_right_click = {};
    W8ButtonCallback on_move = {};
    W8ButtonCallback on_double_click = {};
    std::function<void(bool)> on_hover = {};
    ButtonPolicy policy{};
    int tooltip = W8_DIALOG_BUTTON_NO_TOOLTIP;
    int priority = UI_PRIORITY_HIGHEST;
    bool menu_artwork = false;
};

/* Ordinary sprite or generic-frame button. Its layer owns input registration;
   this control owns behavior and appearance without a visual parent tree. */
// VTABLE: WIZ8 0x005efa98
class W8ButtonWidget {
public:
    explicit W8ButtonWidget(UiLayer& layer);
    ~W8ButtonWidget();
    W8ButtonWidget(const W8ButtonWidget&) = delete;
    W8ButtonWidget& operator=(const W8ButtonWidget&) = delete;
    // Paint only, including clean buttons. Never commits an on-screen update.
    void Draw(SDL_Surface& target) const;
    // Commit a dirty (or forced) framebuffer update. Auxiliary targets are
    // painted unconditionally without invalidation or changing screen dirtiness.
    // The owner restores backing artwork before requesting a forced repaint.
    void DrawAndInvalidate(SDL_Surface& target, bool force = false);
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
    /* Installs the tooltip string on the input target (when one is configured
       and tooltips are on), or clears it. */
    void SetTooltipEnabled(bool enabled);
    bool Configure(ButtonImage image, ButtonSetup setup = {});
    bool Configure(ButtonAppearance appearance, SDL_Rect bounds, ButtonSetup setup = {});

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
    void ConfigureInput(ButtonSetup setup);
    ButtonSetup m_setup;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

public:
    ButtonPolicy& Policy() { return m_setup.policy; }
};

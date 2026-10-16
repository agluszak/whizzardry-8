#include "wiz8/local_code/ButtonWidget.h"

#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>

#include "wiz8/assertions.h"
#include "wiz8/button_interaction.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"

// FUNCTION: WIZ8 0x005db4e0
void W8ButtonWidget::Draw(SDL_Surface& target) const
{
    if (m_target && m_visible) {
        const ButtonVisualState visual{.enabled = m_enabled,
                                       .pressed = m_interaction.pressed,
                                       .hovered = m_target->hovered(),
                                       .menu_artwork = m_setup.menu_artwork};
        DrawButtonAppearance(target, m_appearance, Bounds(), visual);
    }
}

void W8ButtonWidget::Refresh(bool force)
{
    if (!m_dirty && !force) {
        return;
    }
    if (m_target && m_visible) {
        Draw(GetFrameBufferSurface());
        InvalidateRegion(Bounds());
    }
    m_dirty = false;
}

void W8ButtonWidget::SetBounds(SDL_Rect bounds)
{
    m_bounds = bounds;
    if (m_target) m_target->setShape(SDL_Rect{bounds.x, bounds.y, bounds.w + 1, bounds.h + 1});
}

SDL_Point W8ButtonWidget::GetMousePosition() const
{
    return UiPointerPosition();
}

// FUNCTION: WIZ8 0x005db550
void W8ButtonWidget::SetPosition(int x, int y)
{
    if (Bounds().x == x && Bounds().y == y) return;
    SetBounds({x, y, Bounds().w, Bounds().h});
    m_dirty = true;
}

void W8ButtonWidget::Resize(int width, int height)
{
    SetBounds({Bounds().x, Bounds().y, std::max(width, 4), std::max(height, 3)});
    m_dirty = true;
}

void W8ButtonWidget::SetText(std::string_view text)
{
    m_appearance.string = text;
    m_dirty = true;
}

void W8ButtonWidget::SetMultiColor(bool enabled)
{
    m_appearance.fMultiColor = enabled;
    m_dirty = true;
}

// FUNCTION: WIZ8 0x005db8d0
void W8ButtonWidget::SetEnabled(bool enabled)
{
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    if (!enabled) {
        m_interaction.cancel(m_setup.policy.primary_toggle != ButtonToggle::None ||
                             m_setup.policy.secondary_toggle != ButtonToggle::None);
    }
    m_dirty = true;
}

// FUNCTION: WIZ8 0x005db950
void W8ButtonWidget::SetPressed(bool pressed)
{
    if (m_setup.policy.primary_toggle == ButtonToggle::Release ||
        m_setup.policy.primary_toggle == ButtonToggle::StayLatched) {
        m_interaction.selected = pressed;
    }
    if (m_interaction.pressed != pressed) {
        m_interaction.pressed = pressed;
        m_dirty = true;
    }
}

// FUNCTION: WIZ8 0x005db9d0
void W8ButtonWidget::SetVisible(bool visible)
{
    if (!m_target || m_visible == visible) return;
    m_visible = visible;
    m_target->setEnabled(visible);
    if (!visible) {
        m_interaction.cancel(m_setup.policy.primary_toggle != ButtonToggle::None ||
                             m_setup.policy.secondary_toggle != ButtonToggle::None);
    }
    m_dirty = true;
    const auto mouse = UiPointerPosition();
    UpdateUiPointer(mouse.x, mouse.y);
}

void W8ButtonWidget::SetImage(ButtonImage image)
{
    m_appearance.quick_image = std::move(image);
    m_dirty = true;
}

void W8ButtonWidget::SetCircularHitArea(SDL_Point center, int radius)
{
    m_target->setShape(UiCircle{center, radius});
}

void W8ButtonWidget::SetTooltip(std::function<std::string()> text)
{
    m_target->setTooltip(std::move(text));
}

// FUNCTION: WIZ8 0x005dba70
void W8ButtonWidget::SetTooltipEnabled(bool enabled)
{
    if (!m_target) return;
    const bool show_tooltip =
        enabled && m_setup.tooltip != W8_DIALOG_BUTTON_NO_TOOLTIP && g_settings.tooltips_enabled;
    if (show_tooltip)
        m_target->setTooltip([index = m_setup.tooltip] { return g_localized_strings[index]; });
    else
        m_target->setTooltip({});
}

// FUNCTION: WIZ8 0x005dbac0
void W8ButtonWidget::SetTooltipIndex(int tooltip_index)
{
    m_setup.tooltip = tooltip_index;
    SetTooltipEnabled(true);
}

// FUNCTION: WIZ8 0x005db1b0
W8ButtonWidget::W8ButtonWidget(UiLayer& layer) : m_layer(layer) {}

W8ButtonWidget::~W8ButtonWidget()
{
    W8_ASSERT(m_handling_pointer == 0);
    if (m_target) {
        m_layer.remove(*m_target);
    }
}

void W8ButtonWidget::Reset()
{
    if (m_target) {
        m_layer.remove(*m_target);
    }
    m_target = nullptr;
    m_appearance = {};
    SetBounds({});
    m_enabled = true;
    m_interaction = {};
    m_setup = {};
    m_dirty = true;
}

void W8ButtonWidget::ConfigureInput(ButtonSetup setup)
{
    if (m_target) {
        m_layer.remove(*m_target);
    }
    m_setup = std::move(setup);
    m_target = &m_layer.add(
        SDL_Rect{m_bounds.x, m_bounds.y, m_bounds.w + 1, m_bounds.h + 1},
        [this](const UiPointerEvent& event) { return HandlePointer(event); }, m_setup.priority);
    m_visible = true;
    m_enabled = true;
    m_interaction = {};
    m_dirty = true;
    SetTooltipEnabled(true);
}

bool W8ButtonWidget::Configure(ButtonImage image, ButtonSetup setup)
{
    if (!image) {
        return false;
    }
    const SDL_Rect bounds{0, 0, static_cast<int>(image->MaxWidth),
                          static_cast<int>(image->MaxHeight)};
    m_appearance = ButtonAppearance{.quick_image = std::move(image)};
    SetBounds(bounds);
    ConfigureInput(std::move(setup));
    return true;
}

bool W8ButtonWidget::Configure(ButtonAppearance appearance, SDL_Rect bounds, ButtonSetup setup)
{
    m_appearance = std::move(appearance);
    bounds.w = std::max(bounds.w, 4);
    bounds.h = std::max(bounds.h, 3);
    SetBounds(bounds);
    ConfigureInput(std::move(setup));
    return true;
}

UiResult W8ButtonWidget::HandlePointer(const UiPointerEvent& event)
{
    if (!m_setup.on_click && !m_setup.on_right_click && !m_setup.on_move &&
        !m_setup.on_double_click && !m_setup.on_hover) {
        return UiResult::Handled;
    }
    // Reconfiguration may replace a callback while it runs, so dispatch copies
    // it. Destruction is deferred by the owner until this dispatch returns.
    ++m_handling_pointer;
    // NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): local scope guard
    const struct Handling {
        int& depth;
        ~Handling() { --depth; }
    } handling{m_handling_pointer};
    if (event.action == UiPointerAction::Leave) {
        m_interaction.leave(m_setup.policy);
        m_dirty = true;
        if (const auto hover = m_setup.on_hover) {
            hover(false);
        }
        return UiResult::Handled;
    }
    if (const auto hover = m_setup.on_hover; event.action == UiPointerAction::Enter && hover) {
        hover(true);
    }
    if (!m_enabled) return UiResult::Handled;
    const auto mask = event.button == UiPointerButton::Right ? SDL_BUTTON_RMASK : SDL_BUTTON_LMASK;
    const auto callback =
        event.button == UiPointerButton::Right ? m_setup.on_right_click : m_setup.on_click;
    ButtonResponse response;
    const bool was_pressed = m_interaction.pressed;
    switch (event.action) {
    case UiPointerAction::Enter:
        PlayButtonSound(UiPointerAction::Enter);
        m_dirty = true;
        return UiResult::Handled;
    case UiPointerAction::Move:
        if (const auto move = m_setup.on_move) {
            move(this);
        }
        return UiResult::Handled;
    case UiPointerAction::Down:
        response = m_interaction.press(mask, m_setup.policy);
        PlayButtonSound(UiPointerAction::Down);
        break;
    case UiPointerAction::Up:
        if (!event.inside) {
            if (event.captured_buttons & mask) PlayButtonSound(UiPointerAction::Up);
            m_interaction.leave(m_setup.policy);
            m_dirty = true;
            return UiResult::Handled;
        }
        if (!(event.captured_buttons & mask)) return UiResult::Handled;
        response = m_interaction.release(mask, m_setup.policy);
        if (!response.repeated_release ||
            m_setup.policy.input_rule != ButtonInputRule::PrimaryPressedState)
            PlayButtonSound(UiPointerAction::Up);
        break;
    case UiPointerAction::Repeat:
        if (!(event.captured_buttons & mask)) return UiResult::Handled;
        response = m_interaction.repeat(mask, m_setup.policy);
        break;
    default:
        return UiResult::Handled;
    }
    if (was_pressed != m_interaction.pressed) m_dirty = true;
    const bool double_click =
        event.action == UiPointerAction::Down && event.button == UiPointerButton::Left &&
        event.clicks >= 2 &&
        (m_setup.policy.primary_toggle == ButtonToggle::None || m_interaction.pressed);
    const auto double_click_callback = double_click ? m_setup.on_double_click : W8ButtonCallback{};
    if (response.activate && event.action == UiPointerAction::Repeat)
        PlayButtonSound(UiPointerAction::Up);
    if (response.activate && callback) callback(this);
    if (double_click_callback) {
        double_click_callback(this);
    }
    return UiResult::Handled;
}

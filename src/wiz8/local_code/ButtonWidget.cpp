#include <SDL3/SDL_surface.h>
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/ui_layer.h"
#include "wiz8/button_interaction.h"

#include "Font.h"
#include "image_assets.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/ui_input.h"

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>

// FUNCTION: WIZ8 0x005db4e0
void W8ButtonWidget::Draw(SDL_Surface& target)
{
    if (m_dirty && m_target && m_visible) {
        const ButtonVisualState visual{
            .enabled = m_enabled, .pressed = m_interaction.pressed,
            .hovered = m_target->hovered(), .menu_artwork = policy.controller_activation};
        DrawButtonAppearance(target, m_appearance, Bounds(), visual);
        const auto bounds = Bounds();
        InvalidateRegion(bounds.x, bounds.y, bounds.x + bounds.w, bounds.y + bounds.h);
        m_dirty = false;
    }
}

void W8ButtonWidget::SetBounds(SDL_Rect bounds)
{
    m_bounds = bounds;
    if (m_target)
        m_target->setShape(SDL_Rect{bounds.x, bounds.y, bounds.w + 1, bounds.h + 1});
}

SDL_Point W8ButtonWidget::GetMousePosition() const { return UiPointerPosition(); }

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
        m_interaction.cancel(policy.primary_toggle != ButtonToggle::None || policy.secondary_toggle != ButtonToggle::None);
    }
    m_dirty = true;
}

// FUNCTION: WIZ8 0x005db950
void W8ButtonWidget::SetPressed(bool pressed)
{
    if (policy.primary_toggle == ButtonToggle::Release ||
        policy.primary_toggle == ButtonToggle::StayLatched) m_interaction.selected = pressed;
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
    if (!visible) m_interaction.cancel(policy.primary_toggle != ButtonToggle::None || policy.secondary_toggle != ButtonToggle::None);
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
        enabled && m_tooltip_index != W8_DIALOG_BUTTON_NO_TOOLTIP && g_settings.tooltips_enabled;
    if (show_tooltip)
        m_target->setTooltip([index = m_tooltip_index] { return g_localized_strings[index]; });
    else
        m_target->setTooltip({});
}

// FUNCTION: WIZ8 0x005dbac0
void W8ButtonWidget::SetTooltipIndex(int tooltip_index)
{
    m_tooltip_index = tooltip_index;
    SetTooltipEnabled(true);
}

// FUNCTION: WIZ8 0x005db1b0
W8ButtonWidget::W8ButtonWidget()
    : m_owned_layer(new UiLayer()), m_layer(m_owned_layer.get()) {}

W8ButtonWidget::W8ButtonWidget(UiLayer& layer) : m_layer(&layer) {}

W8ButtonWidget::~W8ButtonWidget()
{
    if (m_target) m_layer->remove(*m_target);
}

void W8ButtonWidget::Reset()
{
    if (m_target) m_layer->remove(*m_target);
    m_target = nullptr;
    m_appearance = {};
    SetBounds({});
    m_enabled = true;
    m_interaction = {};
    m_user_data = 0;
    m_left_callback = m_right_callback = m_move_callback = m_double_click_callback = nullptr;
    policy = {};
    m_tooltip_index = W8_DIALOG_BUTTON_NO_TOOLTIP;
    m_owner = nullptr;
    m_dirty = true;
}

void W8ButtonWidget::ConfigureInput(int priority)
{
    if (m_target) m_layer->remove(*m_target);
    m_target = &m_layer->add(SDL_Rect{m_bounds.x, m_bounds.y, m_bounds.w + 1, m_bounds.h + 1},
        [this](const UiPointerEvent& event) { return HandlePointer(event); }, priority);
    m_visible = true;
    m_enabled = true;
    m_interaction = {};
    m_dirty = true;
    SetTooltipEnabled(true);
}

// FUNCTION: WIZ8 0x005DB350
bool W8ButtonWidget::ConfigureTextButton(std::string_view text, Font& font, short fore_color,
                                       short shadow_color, short x, short y, short width,
                                       short height, W8ButtonCallback left_callback,
                                       int user_data, GenericButtonImage image, int priority)
{
    m_appearance = {};
    m_appearance.generic_image = image ? std::move(image) : DefaultGenericButtonImage();
    m_appearance.string = text;
    m_appearance.usFont = &font;
    m_appearance.sForeColor = fore_color;
    m_appearance.sShadowColor = shadow_color;
    SetBounds({x, y, std::max<int>(width, 4), std::max<int>(height, 3)});
    m_user_data = user_data;
    m_left_callback = left_callback;
    m_right_callback = m_move_callback = m_double_click_callback = nullptr;
    policy = {};
    m_tooltip_index = W8_DIALOG_BUTTON_NO_TOOLTIP;
    ConfigureInput(priority);
    return true;
}

// FUNCTION: WIZ8 0x005db3e0
bool W8ButtonWidget::Configure(const char* image_path, int gray_frame, int off_normal_frame,
                              int off_hover_frame, int on_normal_frame, int on_hover_frame,
                              W8ButtonCallback left_callback,
                              W8ButtonCallback move_callback, bool left_toggles,
                              int priority, int tooltip_index,
                              W8ButtonCallback right_callback,
                              W8ButtonCallback double_click_callback)
{
    return Configure(g_image_assets.sprites(image_path), gray_frame, off_normal_frame,
                     off_hover_frame, on_normal_frame, on_hover_frame, std::move(left_callback),
                     std::move(move_callback), left_toggles, priority, tooltip_index,
                     std::move(right_callback), std::move(double_click_callback));
}

bool W8ButtonWidget::Configure(std::shared_ptr<const SpriteSheet> sprites, int gray_frame,
                               int off_normal_frame, int off_hover_frame, int on_normal_frame,
                               int on_hover_frame, W8ButtonCallback left_callback,
                               W8ButtonCallback move_callback, bool left_toggles,
                               int priority, int tooltip_index,
                               W8ButtonCallback right_callback,
                               W8ButtonCallback double_click_callback)
{
    auto image = ButtonGraphics::Load(std::move(sprites), gray_frame, off_normal_frame,
                                      off_hover_frame, on_normal_frame, on_hover_frame);
    if (!image) return false;
    m_appearance = {};
    SetBounds({0, 0, static_cast<int>(image->MaxWidth),
                   static_cast<int>(image->MaxHeight)});
    m_appearance.quick_image = std::move(image);
    m_left_callback = left_callback;
    m_right_callback = right_callback;
    m_move_callback = move_callback;
    m_double_click_callback = double_click_callback;
    policy.primary_toggle = left_toggles ? ButtonToggle::Press : ButtonToggle::None;
    m_tooltip_index = tooltip_index;
    ConfigureInput(priority);
    return true;
}

UiResult W8ButtonWidget::HandlePointer(const UiPointerEvent& event)
{
    if (!m_left_callback && !m_right_callback && !m_move_callback && !m_double_click_callback)
        return UiResult::Handled;
    // Text-style controls still use the temporary backend's ordinary sounds.
    const bool backend_sound = event.action == UiPointerAction::Enter ||
        event.action == UiPointerAction::Leave || event.action == UiPointerAction::Down ||
        (event.action == UiPointerAction::Up && event.captured_buttons != 0);
    if (policy.controller_activation && !m_enabled && backend_sound)
        PushButtonSoundScheme(0, true);
    if (event.action == UiPointerAction::Leave) {
        if (m_interaction.leave(policy) && policy.controller_activation && policy.repeat)
            PushButtonSoundScheme(0, true);
        m_dirty = true;
        if (on_hover) on_hover(false);
        if (m_enabled && !policy.controller_activation) PlayButtonSound(1);
        return UiResult::Handled;
    }
    if (event.action == UiPointerAction::Enter && on_hover) on_hover(true);
    if (!m_enabled) return UiResult::Handled;
    const auto mask = event.button == UiPointerButton::Right ? SDL_BUTTON_RMASK : SDL_BUTTON_LMASK;
    const auto callback = event.button == UiPointerButton::Right ? m_right_callback : m_left_callback;
    ButtonResponse response;
    const bool was_pressed = m_interaction.pressed;
    switch (event.action) {
    case UiPointerAction::Enter:
        m_dirty = true;
        if (!policy.controller_activation) PlayButtonSound(0);
        return UiResult::Handled;
    case UiPointerAction::Move:
        if (const auto move = m_move_callback) move(this);
        return UiResult::Handled;
    case UiPointerAction::Down:
        response = m_interaction.press(mask, policy);
        break;
    case UiPointerAction::Up:
        if (!event.inside) {
            m_interaction.leave(policy);
            m_dirty = true;
            return UiResult::Handled;
        }
        if (!(event.captured_buttons & mask)) return UiResult::Handled;
        response = m_interaction.release(mask, policy);
        if (response.repeated_release && policy.controller_activation)
            PushButtonSoundScheme(0, true);
        break;
    case UiPointerAction::Repeat:
        if (!(event.captured_buttons & mask)) return UiResult::Handled;
        response = m_interaction.repeat(mask, policy);
        break;
    default: return UiResult::Handled;
    }
    if (was_pressed != m_interaction.pressed) m_dirty = true;
    if (response.press_sound && !policy.controller_activation) PlayButtonSound(2);
    if (response.activate) {
        if (callback) callback(this);
        if (!policy.controller_activation || event.action == UiPointerAction::Repeat)
            PlayButtonSound(3);
    }
    if (event.action == UiPointerAction::Down && event.button == UiPointerButton::Left &&
        event.clicks >= 2 && (policy.primary_toggle == ButtonToggle::None || m_interaction.pressed)) {
        if (const auto double_click = m_double_click_callback) double_click(this);
    }
    return UiResult::Handled;
}

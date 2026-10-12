#include "wiz8/local_code/ButtonWidget.h"

#include "Font.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Controls.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/regions.h"

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

// FUNCTION: WIZ8 0x005db4e0
void W8ButtonWidget::Draw()
{
    if (m_dirty && OwnsRegion() && (Region().flags & W8_REGION_INPUT_DISABLED) == 0) {
        m_visual.hovered = (Region().flags & W8_REGION_MOUSE_ENTER) != 0 ||
                           g_hover_region_index == static_cast<unsigned int>(m_region);
        DrawButtonAppearance(GetFrameBufferSurface(), m_appearance, ScreenBounds(), m_visual);
        const auto bounds = ScreenBounds();
        InvalidateRegion(bounds.x, bounds.y, bounds.x + bounds.w, bounds.y + bounds.h);
        m_dirty = false;
    }
}

// FUNCTION: WIZ8 0x005db550
void W8ButtonWidget::SetPosition(int x, int y)
{
    if (m_left == x && m_top == y) return;
    SetButtonRect({x, y, GetWidth(), GetHeight()});
    m_dirty = true;
}

void W8ButtonWidget::Resize(int width, int height)
{
    SetButtonRect({m_left, m_top, std::max(width, 4), std::max(height, 3)});
    m_dirty = true;
}

void W8ButtonWidget::SetText(std::string_view text)
{
    m_appearance.string = text;
    m_dirty = true;
}

void W8ButtonWidget::SetTextOffsets(int x, int y, bool shift)
{
    m_appearance.bTextXOffset = x;
    m_appearance.bTextYOffset = y;
    m_appearance.fShiftText = shift;
    m_dirty = true;
}

void W8ButtonWidget::SetMultiColor(bool enabled)
{
    m_appearance.fMultiColor = enabled;
    m_dirty = true;
}

void W8ButtonWidget::SetButtonRect(const SDL_Rect& bounds)
{
    m_left = bounds.x;
    m_top = bounds.y;
    m_right = bounds.x + bounds.w;
    m_bottom = bounds.y + bounds.h;
    UpdateRegionBounds(m_left, m_top, m_right, m_bottom);
}

SDL_Rect W8ButtonWidget::ScreenBounds() const
{
    const auto origin = PanelOrigin();
    return {m_left + origin.x, m_top + origin.y, GetWidth(), GetHeight()};
}

// FUNCTION: WIZ8 0x005db8d0
void W8ButtonWidget::SetEnabled(bool enabled)
{
    m_enabled = enabled;
    if (m_visual.enabled == enabled) return;
    m_visual.enabled = enabled;
    if (!enabled) {
        m_armed_buttons = m_repeated_buttons = 0;
        if (!m_left_toggles && !m_right_toggles) m_visual.pressed = false;
    }
    m_dirty = true;
}

// FUNCTION: WIZ8 0x005db950
void W8ButtonWidget::SetPressed(bool pressed)
{
    if (m_visual.pressed != pressed) {
        m_visual.pressed = pressed;
        m_dirty = true;
    }
}

// FUNCTION: WIZ8 0x005db9d0
void W8ButtonWidget::SetVisible(bool visible)
{
    if (!OwnsRegion()) return;
    const bool currently_visible = (Region().flags & W8_REGION_INPUT_DISABLED) == 0;
    if (visible == currently_visible) return;
    m_active = visible;
    if (visible) {
        Region().SetInputEnabled(true);
    } else {
        Region().SetInputEnabled(false);
        m_armed_buttons = m_repeated_buttons = 0;
        if (!m_left_toggles && !m_right_toggles) m_visual.pressed = false;
    }
    m_dirty = true;
    const auto mouse = GetRegionMousePosition();
    UpdateRegionMousePosition(mouse.x, mouse.y);
}

// FUNCTION: WIZ8 0x005dba70
void W8ButtonWidget::SetTooltipEnabled(bool enabled)
{
    if (!OwnsRegion()) return;
    const bool show_tooltip =
        enabled && m_tooltip_index != W8_DIALOG_BUTTON_NO_TOOLTIP && g_settings.tooltips_enabled;
    Region().help_text = show_tooltip ? g_localized_strings[m_tooltip_index] : std::string{};
    Region().SetHelp(show_tooltip,
                  show_tooltip ? m_tooltip_index : W8_DIALOG_BUTTON_NO_TOOLTIP);
}

// FUNCTION: WIZ8 0x005dbac0
void W8ButtonWidget::SetTooltipIndex(int tooltip_index)
{
    m_tooltip_index = tooltip_index;
    SetTooltipEnabled(true);
}

// FUNCTION: WIZ8 0x005db1b0
W8ButtonWidget::W8ButtonWidget()
    : m_tooltip_index(W8_DIALOG_BUTTON_NO_TOOLTIP), m_left_callback(nullptr),
      m_right_callback(nullptr), m_move_callback(nullptr), m_double_click_callback(nullptr),
      m_left_toggles(false), m_right_toggles(false), silent(false),
      hover_silent(false), m_repeat_enabled(false), m_owner(nullptr),
      m_layer(TopLiveDialog())
{
    m_dirty = true;
}


// FUNCTION: WIZ8 0x005db260
W8ButtonWidget::~W8ButtonWidget() = default;

void W8ButtonWidget::Reset()
{
    ReleaseRegion();
    m_appearance = {};
    SetButtonRect({});
    m_visual = {};
    m_armed_buttons = m_repeated_buttons = 0;
    m_user_data = 0;
    m_left_callback = m_right_callback = m_move_callback = m_double_click_callback = nullptr;
    m_left_toggles = m_right_toggles = m_repeat_enabled = false;
    m_activation = DialogButtonActivation::Release;
    m_tooltip_index = W8_DIALOG_BUTTON_NO_TOOLTIP;
    m_owner = nullptr;
    m_dirty = true;
}

void W8ButtonWidget::ConfigureRegion(int priority)
{
    ReleaseRegion();
    const RegionTemplate region_template{W8_REGION_RECTANGLE, 0, 0, 0, 0,
                                         nullptr, static_cast<std::uint16_t>(priority), false, -1};
    const auto set = m_pPanel && m_pPanel->RegionSet() != 0 ? m_pPanel->RegionSet()
                                                                 : ControlRegionSetIndex();
    OwnedRegion allocated_region(set, region_template, priority);
    m_active = true;
    SetRegion(allocated_region.index());
    m_owned_region = std::move(allocated_region);
    SetButtonRect(GetBounds());
    m_visual = {};
    m_armed_buttons = m_repeated_buttons = 0;
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
    SetButtonRect({x, y, std::max<int>(width, 4), std::max<int>(height, 3)});
    m_user_data = user_data;
    m_left_callback = left_callback;
    m_right_callback = m_move_callback = m_double_click_callback = nullptr;
    m_left_toggles = m_right_toggles = m_repeat_enabled = false;
    m_activation = DialogButtonActivation::Release;
    m_tooltip_index = W8_DIALOG_BUTTON_NO_TOOLTIP;
    ConfigureRegion(priority);
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
    auto image = ButtonGraphics::Load(image_path, gray_frame, off_normal_frame,
                                     off_hover_frame, on_normal_frame, on_hover_frame);
    if (!image) return false;
    m_appearance = {};
    SetButtonRect({0, 0, static_cast<int>(image->MaxWidth), static_cast<int>(image->MaxHeight)});
    m_appearance.quick_image = std::move(image);
    m_left_callback = left_callback;
    m_right_callback = right_callback;
    m_move_callback = move_callback;
    m_double_click_callback = double_click_callback;
    m_left_toggles = left_toggles;
    m_tooltip_index = tooltip_index;
    ConfigureRegion(priority);
    return true;
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
    SetButtonRect({0, 0, static_cast<int>(image->MaxWidth),
                   static_cast<int>(image->MaxHeight)});
    m_appearance.quick_image = std::move(image);
    m_left_callback = left_callback;
    m_right_callback = right_callback;
    m_move_callback = move_callback;
    m_double_click_callback = double_click_callback;
    m_left_toggles = left_toggles;
    m_tooltip_index = tooltip_index;
    ConfigureRegion(priority);
    return true;
}

// FUNCTION: WIZ8 0x005db620
void W8ButtonWidget::OnMouseEnter(int) { HandleButtonEvent(ButtonEvent::Enter, 0); }
void W8ButtonWidget::OnMouseLeave(int) { HandleButtonEvent(ButtonEvent::Leave, 0); }
void W8ButtonWidget::OnMouseMove(int) { HandleButtonEvent(ButtonEvent::Move, 0); }
void W8ButtonWidget::OnLeftButtonDown(int) { HandleButtonEvent(ButtonEvent::Press, SDL_BUTTON_LMASK); }
void W8ButtonWidget::OnLeftButtonUp(int) { HandleButtonEvent(ButtonEvent::Release, SDL_BUTTON_LMASK); }
void W8ButtonWidget::OnLeftButtonDoubleClick(int)
{
    HandleButtonEvent(ButtonEvent::DoubleClick, SDL_BUTTON_LMASK);
}
void W8ButtonWidget::ActivatePrimary(int) { HandleButtonEvent(ButtonEvent::Repeat, SDL_BUTTON_LMASK); }
void W8ButtonWidget::OnRightButtonDown(int) { HandleButtonEvent(ButtonEvent::Press, SDL_BUTTON_RMASK); }
void W8ButtonWidget::OnRightButtonUp(int) { HandleButtonEvent(ButtonEvent::Release, SDL_BUTTON_RMASK); }
void W8ButtonWidget::ActivateSecondary(int) { HandleButtonEvent(ButtonEvent::Repeat, SDL_BUTTON_RMASK); }

/* Press arms the button (or flips a toggle); release of an armed button
   activates it unless it was held into repeat or activates on press. */
void W8ButtonWidget::HandleButtonEvent(ButtonEvent event, SDL_MouseButtonFlags mask)
{
    if (m_layer != TopLiveDialog()) return;
    if (!m_left_callback && !m_right_callback && !m_move_callback && !m_double_click_callback) return;
    if (event == ButtonEvent::Leave) {
        m_armed_buttons = m_repeated_buttons = 0;
        if (!m_left_toggles) SetPressed(false);
        m_dirty = true;
        if (m_visual.enabled && !silent && !hover_silent) PlayButtonSound(1);
        return;
    }
    if (!m_visual.enabled) return;
    switch (event) {
    case ButtonEvent::Enter:
        m_dirty = true;
        if (!silent && !hover_silent) PlayButtonSound(0);
        return;
    case ButtonEvent::DoubleClick:
        if (const auto callback = m_double_click_callback;
            callback && (!m_left_toggles || m_visual.pressed)) {
            callback(this);
        }
        return;
    case ButtonEvent::Move:
        if (const auto callback = m_move_callback) callback(this);
        return;
    default:
        break;
    }
    const bool right = mask == SDL_BUTTON_RMASK;
    const bool toggles = right ? m_right_toggles : m_left_toggles;
    const auto callback = right ? m_right_callback : m_left_callback;
    // Callbacks may destroy the owning dialog and this control.
    const bool play_sound = !silent;
    switch (event) {
    case ButtonEvent::Press:
        if (toggles) {
            SetPressed(!m_visual.pressed);
            break;
        }
        if (m_armed_buttons & mask) return;
        m_armed_buttons |= mask;
        SetPressed(true);
        if (play_sound) PlayButtonSound(2);
        if (m_activation != DialogButtonActivation::Press) return;
        break;
    case ButtonEvent::Repeat:
        if (!m_repeat_enabled || toggles || !(m_armed_buttons & mask)) return;
        m_repeated_buttons |= mask;
        break;
    case ButtonEvent::Release: {
        if (toggles || !(m_armed_buttons & mask)) return;
        m_armed_buttons &= ~mask;
        SetPressed(m_armed_buttons != 0);
        const bool repeated = (m_repeated_buttons & mask) != 0;
        m_repeated_buttons &= ~mask;
        if (repeated || m_activation == DialogButtonActivation::Press) return;
        break;
    }
    default:
        return;
    }
    if (callback) callback(this);
    if (play_sound) PlayButtonSound(3);
}

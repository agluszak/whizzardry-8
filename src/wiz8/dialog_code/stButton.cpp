#include "Font.h"
#include <cstdint>
#include <SDL3/SDL_mouse.h>
#include <memory>
#include <string_view>
#include <utility>
#include <algorithm>
#include <SDL3/SDL_rect.h>

#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/dialog_code/DialogButton.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/regions.h"
#include "input.h"
// FUNCTION: WIZ8 0x005db4e0
void W8DialogButton::Draw()
{
    if (m_dirty && m_region &&
        (m_region.region().flags & W8_REGION_INPUT_DISABLED) == 0) {
        m_visual.hovered = (m_region.region().flags & W8_REGION_MOUSE_ENTER) != 0 ||
                           g_hover_region_index == m_region.index();
        DrawButtonAppearance(GetFrameBufferSurface(), m_appearance, m_visual);
        const auto& bounds = m_appearance.bounds;
        InvalidateRegion(bounds.x, bounds.y, bounds.x + bounds.w, bounds.y + bounds.h, 0);
        m_dirty = false;
    }
}

// FUNCTION: WIZ8 0x005db550
void W8DialogButton::SetPosition(int x, int y)
{
    auto& bounds = m_appearance.bounds;
    if (bounds.x == x && bounds.y == y) return;
    bounds.x = x;
    bounds.y = y;
    if (m_region) {
        SetRegionBounds(m_region.index(), static_cast<unsigned short>(x),
                        static_cast<unsigned short>(y),
                        static_cast<unsigned short>(x + bounds.w),
                        static_cast<unsigned short>(y + bounds.h));
    }
    m_dirty = true;
}

void W8DialogButton::Resize(int width, int height)
{
    auto& bounds = m_appearance.bounds;
    bounds.w = std::max(width, 4);
    bounds.h = std::max(height, 3);
    if (m_region) {
        SetRegionBounds(m_region.index(), static_cast<unsigned short>(bounds.x),
                        static_cast<unsigned short>(bounds.y),
                        static_cast<unsigned short>(bounds.x + bounds.w),
                        static_cast<unsigned short>(bounds.y + bounds.h));
    }
    m_dirty = true;
}

void W8DialogButton::SetText(std::string_view text)
{
    m_appearance.string = text;
    m_dirty = true;
}

void W8DialogButton::SetTextOffsets(int x, int y, bool shift)
{
    m_appearance.bTextXOffset = x;
    m_appearance.bTextYOffset = y;
    m_appearance.fShiftText = shift;
    m_dirty = true;
}

void W8DialogButton::SetMultiColor(bool enabled)
{
    m_appearance.fMultiColor = enabled;
    m_dirty = true;
}

// FUNCTION: WIZ8 0x005db5a0
int W8DialogButton::GetWidth() { return m_appearance.bounds.w; }

// FUNCTION: WIZ8 0x005db5c0
int W8DialogButton::GetHeight() { return m_appearance.bounds.h; }

// FUNCTION: WIZ8 0x005db5e0
int W8DialogButton::GetX() { return m_appearance.bounds.x; }

// FUNCTION: WIZ8 0x005db600
int W8DialogButton::GetY() { return m_appearance.bounds.y; }

// FUNCTION: WIZ8 0x005db8d0
void W8DialogButton::SetEnabled(bool enabled)
{
    if (m_visual.enabled == enabled) return;
    m_visual.enabled = enabled;
    if (!enabled) {
        m_armed_buttons = m_repeated_buttons = 0;
        if (!m_left_toggles && !m_right_toggles) m_visual.pressed = false;
    }
    m_dirty = true;
}

// FUNCTION: WIZ8 0x005db920
bool W8DialogButton::IsEnabled()
{
    return static_cast<bool>(m_region) && m_visual.enabled;
}

// FUNCTION: WIZ8 0x005db950
void W8DialogButton::SetPressed(bool pressed)
{
    if (m_visual.pressed != pressed) {
        m_visual.pressed = pressed;
        m_dirty = true;
    }
}

// FUNCTION: WIZ8 0x005db9a0
unsigned char W8DialogButton::IsPressed() { return m_visual.pressed; }

// FUNCTION: WIZ8 0x005db9d0
void W8DialogButton::SetVisible(bool visible)
{
    if (!m_region) return;
    const bool currently_visible = (m_region.region().flags & W8_REGION_INPUT_DISABLED) == 0;
    if (visible == currently_visible) return;
    if (visible) {
        EnableRegionInput(m_region.index());
    } else {
        DisableRegionInput(m_region.index());
        m_armed_buttons = m_repeated_buttons = 0;
        if (!m_left_toggles && !m_right_toggles) m_visual.pressed = false;
    }
    m_dirty = true;
    const auto mouse = GetRegionMousePosition();
    UpdateRegionMousePosition(mouse.x, mouse.y);
}

// FUNCTION: WIZ8 0x005dba70
void W8DialogButton::SetTooltipEnabled(bool enabled)
{
    if (!m_region) return;
    const bool show_tooltip =
        enabled && m_tooltip_index != W8_DIALOG_BUTTON_NO_TOOLTIP && g_settings.tooltips_enabled;
    m_region.region().help_text = show_tooltip ? g_localized_strings[m_tooltip_index] : std::string{};
    SetRegionHelp(m_region.index(), show_tooltip,
                  show_tooltip ? m_tooltip_index : W8_DIALOG_BUTTON_NO_TOOLTIP);
}

// FUNCTION: WIZ8 0x005dbac0
void W8DialogButton::SetTooltipIndex(int tooltip_index)
{
    m_tooltip_index = tooltip_index;
    SetTooltipEnabled(true);
}

// FUNCTION: WIZ8 0x005dbaf0
int W8DialogButton::GetUserData() { return m_user_data; }

// FUNCTION: WIZ8 0x005db1b0
W8DialogButton::W8DialogButton()
    : m_tooltip_index(W8_DIALOG_BUTTON_NO_TOOLTIP), m_left_callback(nullptr),
      m_right_callback(nullptr), m_move_callback(nullptr), m_double_click_callback(nullptr),
      m_left_toggles(false), m_right_toggles(false), m_dirty(true), silent(false),
      hover_silent(false), m_repeat_enabled(false), m_owner(nullptr),
      m_live_dialog_count(g_live_dialog_count)
{
}

// FUNCTION: WIZ8 0x005db260
W8DialogButton::~W8DialogButton() = default;

void W8DialogButton::Reset()
{
    m_region.reset();
    m_appearance = {};
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

void W8DialogButton::ConfigureRegion(int priority)
{
    const auto& bounds = m_appearance.bounds;
    m_region = OwnedRegion(ControlRegionSetIndex(), priority, *this);
    auto& region = m_region.region();
    region.flags |= W8_REGION_RECTANGLE;
    SetRegionBounds(m_region.index(), static_cast<unsigned short>(bounds.x),
                    static_cast<unsigned short>(bounds.y),
                    static_cast<unsigned short>(bounds.x + bounds.w),
                    static_cast<unsigned short>(bounds.y + bounds.h));
    m_visual = {};
    m_armed_buttons = m_repeated_buttons = 0;
    m_dirty = true;
    SetTooltipEnabled(true);
}

// FUNCTION: WIZ8 0x005DB350
bool W8DialogButton::ConfigureTextButton(std::string_view text, Font& font, short fore_color,
                                       short shadow_color, short x, short y, short width,
                                       short height, W8DialogButtonCallback left_callback,
                                       int user_data, GenericButtonImage image, int priority)
{
    m_appearance = {};
    m_appearance.generic_image = image ? std::move(image) : DefaultGenericButtonImage();
    m_appearance.string = text;
    m_appearance.usFont = &font;
    m_appearance.sForeColor = fore_color;
    m_appearance.sShadowColor = shadow_color;
    m_appearance.bounds = {x, y, std::max<int>(width, 4), std::max<int>(height, 3)};
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
bool W8DialogButton::Configure(const char* image_path, int gray_frame, int off_normal_frame,
                              int off_hover_frame, int on_normal_frame, int on_hover_frame,
                              W8DialogButtonCallback left_callback,
                              W8DialogButtonCallback move_callback, bool left_toggles,
                              int priority, int tooltip_index,
                              W8DialogButtonCallback right_callback,
                              W8DialogButtonCallback double_click_callback)
{
    auto image = ButtonGraphics::Load(image_path, gray_frame, off_normal_frame,
                                     off_hover_frame, on_normal_frame, on_hover_frame);
    if (!image) return false;
    m_appearance = {};
    m_appearance.bounds = {0, 0, static_cast<int>(image->MaxWidth), static_cast<int>(image->MaxHeight)};
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

bool W8DialogButton::Configure(std::shared_ptr<const SpriteSheet> sprites, int gray_frame,
                               int off_normal_frame, int off_hover_frame, int on_normal_frame,
                               int on_hover_frame, W8DialogButtonCallback left_callback,
                               W8DialogButtonCallback move_callback, bool left_toggles,
                               int priority, int tooltip_index,
                               W8DialogButtonCallback right_callback,
                               W8DialogButtonCallback double_click_callback)
{
    auto image = ButtonGraphics::Load(std::move(sprites), gray_frame, off_normal_frame,
                                      off_hover_frame, on_normal_frame, on_hover_frame);
    if (!image) return false;
    m_appearance = {};
    m_appearance.bounds = {0, 0, static_cast<int>(image->MaxWidth),
                           static_cast<int>(image->MaxHeight)};
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
int W8DialogButton::HandleRegionInput(W8Region& region, const InputEvent& input)
{
    const auto reason = RegionReasonForInput(region, input);
    if (reason == REGION_REASON_NONE) return input.type == InputEventType::LeftButtonDoubleClick;
    HandleMouseEvent(reason);
    return 1;
}

void W8DialogButton::HandleMouseEvent(std::int32_t reason)
{
    if (m_live_dialog_count != g_live_dialog_count) return;
    if (!m_left_callback && !m_right_callback && !m_move_callback && !m_double_click_callback) return;
    if (reason & REGION_REASON_LOST_MOUSE) {
        m_armed_buttons = m_repeated_buttons = 0;
        if (!m_left_toggles) SetPressed(false);
        m_dirty = true;
        if (m_visual.enabled && !silent && !hover_silent) PlayButtonSound(1);
        return;
    }
    if (!m_visual.enabled) return;
    if (reason & REGION_REASON_GAIN_MOUSE) {
        m_dirty = true;
        if (!silent && !hover_silent) PlayButtonSound(0);
        return;
    }
    if ((reason & REGION_REASON_LBUTTON_DOUBLECLICK) && m_double_click_callback) {
        const auto callback = m_double_click_callback;
        if (!m_left_toggles || m_visual.pressed) callback(this);
        return;
    }
    if (reason & REGION_REASON_MOVE) {
        const auto callback = m_move_callback;
        if (callback) callback(this);
        return;
    }
    const bool right = (reason & (REGION_REASON_RBUTTON_DWN |
        REGION_REASON_RBUTTON_UP | REGION_REASON_RBUTTON_REPEAT)) != 0;
    const auto mask = right ? SDL_BUTTON_RMASK : SDL_BUTTON_LMASK;
    const auto down = right ? REGION_REASON_RBUTTON_DWN : REGION_REASON_LBUTTON_DWN;
    const auto up = right ? REGION_REASON_RBUTTON_UP : REGION_REASON_LBUTTON_UP;
    const auto repeat = right ? REGION_REASON_RBUTTON_REPEAT : REGION_REASON_LBUTTON_REPEAT;
    const bool toggles = right ? m_right_toggles : m_left_toggles;
    const auto callback = right ? m_right_callback : m_left_callback;
    // Callbacks may destroy the owning dialog and this control.
    const bool play_sound = !silent;
    if (reason & down) {
        if (toggles) SetPressed(!m_visual.pressed);
        else {
            if (m_armed_buttons & mask) return;
            m_armed_buttons |= mask;
            SetPressed(true);
            if (play_sound) PlayButtonSound(2);
            if (m_activation != DialogButtonActivation::Press) return;
        }
    } else if (reason & repeat) {
        if (!m_repeat_enabled || toggles || !(m_armed_buttons & mask)) return;
        m_repeated_buttons |= mask;
    } else if (reason & up) {
        if (toggles || !(m_armed_buttons & mask)) return;
        m_armed_buttons &= ~mask;
        SetPressed(m_armed_buttons != 0);
        const bool repeated = (m_repeated_buttons & mask) != 0;
        m_repeated_buttons &= ~mask;
        if (repeated || m_activation == DialogButtonActivation::Press) return;
    } else return;
    if (callback) callback(this);
    if (play_sound) PlayButtonSound(3);
}

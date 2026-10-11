#include "Font.h"
#include <string_view>
#include <utility>
#include <vector>
#include <SDL3/SDL_rect.h>

#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/dialog_code/DialogButton.h"
#include "wiz8/dialog_code/ButtonUserData.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/sr_api.h"
#include "Button System.h"
#include "wiz8/regions.h"
#include "Types.h"

bool UpdateDialogArrowState(GUI_BUTTON* button, INT32 reason)
{
    if (reason & BUTTON_REASON_LBUTTON_DWN) {
        if (!(button->uiFlags & BUTTON_CLICKED_ON)) {
            button->uiFlags |= BUTTON_CLICKED_ON;
            return true;
        }
    } else if (reason & BUTTON_REASON_LBUTTON_UP) {
        if (button->uiFlags & BUTTON_CLICKED_ON) {
            button->uiFlags &= ~BUTTON_CLICKED_ON;
            return true;
        }
    } else if (reason & BUTTON_REASON_GAIN_MOUSE) {
        return true;
    } else if (reason & BUTTON_REASON_LOST_MOUSE) {
        return true;
    }
    return false;
}

// FUNCTION: WIZ8 0x005db4e0
void W8DialogButton::Draw()
{
    if (m_dirty && m_button_handle) {
        int left = GetButtonX(m_button_handle);
        int top = GetButtonY(m_button_handle);
        DrawButton(m_button_handle);
        InvalidateRegion(left, top, left + GetButtonWidth(m_button_handle),
                         top + GetButtonHeight(m_button_handle), 0);
        m_dirty = false;
    }
}

// FUNCTION: WIZ8 0x005db550
void W8DialogButton::SetPosition(int x, int y)
{
    if (m_button_handle &&
        (x != GetButtonX(m_button_handle) || y != GetButtonY(m_button_handle))) {
        SetButtonPosition(m_button_handle, static_cast<short>(x), static_cast<short>(y));
        m_dirty = true;
    }
}

// FUNCTION: WIZ8 0x005db5a0
int W8DialogButton::GetWidth()
{
    return m_button_handle ? GetButtonWidth(m_button_handle) : 0;
}

// FUNCTION: WIZ8 0x005db5c0
int W8DialogButton::GetHeight()
{
    return m_button_handle ? GetButtonHeight(m_button_handle) : 0;
}

// FUNCTION: WIZ8 0x005db5e0
int W8DialogButton::GetX()
{
    return m_button_handle ? GetButtonX(m_button_handle) : 0;
}

// FUNCTION: WIZ8 0x005db600
int W8DialogButton::GetY()
{
    return m_button_handle ? GetButtonY(m_button_handle) : 0;
}

// FUNCTION: WIZ8 0x005db8d0
void W8DialogButton::SetEnabled(bool enabled)
{
    GUI_BUTTON* button = m_button_handle.get();
    m_enabled = enabled;
    if (button) {
        if (enabled) {
            if (!(button->uiFlags & BUTTON_ENABLED)) {
                button->uiFlags |= BUTTON_ENABLED;
                m_dirty = true;
            }
        } else if (button->uiFlags & BUTTON_ENABLED) {
            button->uiFlags &= ~BUTTON_ENABLED;
            m_dirty = true;
        }
    }
}

// FUNCTION: WIZ8 0x005db920
bool W8DialogButton::IsEnabled()
{
    GUI_BUTTON* button = m_button_handle.get();
    return button ? (button->uiFlags & BUTTON_ENABLED) != 0 : false;
}

// FUNCTION: WIZ8 0x005db950
void W8DialogButton::SetPressed(bool pressed)
{
    GUI_BUTTON* button = m_button_handle.get();
    if (button) {
        if (pressed) {
            if (!(button->uiFlags & BUTTON_CLICKED_ON)) {
                button->uiFlags |= BUTTON_CLICKED_ON;
                m_dirty = true;
            }
        } else if (button->uiFlags & BUTTON_CLICKED_ON) {
            button->uiFlags &= ~BUTTON_CLICKED_ON;
            m_dirty = true;
        }
    }
}

// FUNCTION: WIZ8 0x005db9a0
unsigned char W8DialogButton::IsPressed()
{
    return static_cast<unsigned char>(m_button_handle.get()->uiFlags & BUTTON_CLICKED_ON);
}

// FUNCTION: WIZ8 0x005db9d0
void W8DialogButton::SetVisible(bool visible)
{
    if (!m_button_handle) return;
    const bool currently_visible =
        (g_regions[m_button_handle->region_index].flags & W8_REGION_INPUT_DISABLED) == 0;
    if (visible) {
        if (!currently_visible) {
            ShowButton(m_button_handle);
        }
    } else if (currently_visible) {
        HideButton(m_button_handle);
    }
    SDL_Point mouse;
    SGPMouseGetPos(&mouse);
    UpdateRegionMousePosition(mouse.x, mouse.y);
}

// FUNCTION: WIZ8 0x005dba70
void W8DialogButton::SetTooltipEnabled(bool enabled)
{
    if (enabled) {
        if (m_tooltip_index != W8_DIALOG_BUTTON_NO_TOOLTIP && g_settings.tooltips_enabled) {
            SetButtonHelpText(m_button_handle,

                                  g_localized_strings[m_tooltip_index]);
        }
    } else {
        SetButtonHelpText(m_button_handle, {});
    }
}

// FUNCTION: WIZ8 0x005dbac0
void W8DialogButton::SetTooltipIndex(int tooltip_index)
{
    if (m_button_handle) {
        m_tooltip_index = tooltip_index;
        if (g_settings.tooltips_enabled) {
            SetButtonHelpText(m_button_handle,

                                  g_localized_strings[tooltip_index]);
        }
    }
}

// FUNCTION: WIZ8 0x005dbaf0
int W8DialogButton::GetUserData()
{
    return m_button_handle->UserData[1];
}

// FUNCTION: WIZ8 0x005db1b0
W8DialogButton::W8DialogButton()
{
    m_left_callback = 0;
    m_right_callback = 0;
    m_move_callback = 0;
    m_double_click_callback = 0;
    m_latched_press_bits = 0;
    m_enabled = true;
    m_left_toggles = false;
    m_right_toggles = false;
    m_tooltip_index = W8_DIALOG_BUTTON_NO_TOOLTIP;
    m_dirty = true;
    silent = false;
    hover_silent = false;
    m_fires_on_press = false;
    press_armed = false;
    m_owner = 0;
    m_live_dialog_count = g_live_dialog_count;
}

// FUNCTION: WIZ8 0x005db260
W8DialogButton::~W8DialogButton() = default;

#define STBUTTON_CPP "C:\\Projects\\Wizardry 8\\Dialog Code\\stButton.cpp"

// FUNCTION: WIZ8 0x005DB350
bool W8DialogButton::ConfigureTextButton(std::string_view text, Font& font, short fore_color,
                                         short shadow_color, short x, short y, short width,
                                         short height, W8DialogButtonCallback left_callback,
                                         int user_data)
{
    auto handle = CreateTextButton(text, font, fore_color, shadow_color, {},
                                    x, y, width, height, BUTTON_NO_TOGGLE, BUTTON_PRIORITY_HIGHEST,
                                    DialogButtonCallback, DialogButtonCallback);
    m_button_handle = handle;
    if (handle) {
        SetButtonUserDataPointer(handle, this);
        SetButtonUserData(m_button_handle, 1, user_data);
        m_left_callback = left_callback;
        m_left_toggles = false;
        m_dirty = true;
        return true;
    }
    return false;
}

// FUNCTION: WIZ8 0x005db3e0
bool W8DialogButton::Configure(const char* image_path, int gray_frame, int off_normal_frame,
                               int off_hover_frame, int on_normal_frame, int on_hover_frame,
                               W8DialogButtonCallback left_callback,
                               W8DialogButtonCallback move_callback, bool left_toggles,
                               short priority, int tooltip_index,
                               W8DialogButtonCallback right_callback,
                               W8DialogButtonCallback double_click_callback)
{
    auto image = ButtonGraphics::Load(image_path, gray_frame, off_normal_frame,
                                      off_hover_frame, on_normal_frame, on_hover_frame);
    if (!image) return false;
    auto button = QuickCreateButton(std::move(image), 0, 0, BUTTON_NO_TOGGLE, priority,
                                    DialogButtonCallback, DialogButtonCallback);
    if (!button) return false;
    m_button_handle = std::move(button);
    SetButtonUserDataPointer(m_button_handle, this);
    m_move_callback = move_callback;
    m_left_callback = left_callback;
    m_right_callback = right_callback;
    m_double_click_callback = double_click_callback;
    m_left_toggles = left_toggles;
    m_tooltip_index = tooltip_index;
    if (tooltip_index != W8_DIALOG_BUTTON_NO_TOOLTIP && g_settings.tooltips_enabled) {
        SetButtonHelpText(m_button_handle, g_localized_strings[tooltip_index]);
    }
    m_dirty = true;
    return true;
}

// FUNCTION: WIZ8 0x005db620
void DialogButtonCallback(GUI_BUTTON* button, INT32 reason)
{
    if (button == 0) {
        srAssertFail("pButton", STBUTTON_CPP, 0x197, 0);
    }
    W8DialogButton* self = GetButtonUserDataPointer<W8DialogButton>(button);
    const bool silent = self->silent; // A click callback may destroy the dialog.
    const auto left_callback = self->m_left_callback;
    const auto right_callback = self->m_right_callback;
    const auto move_callback = self->m_move_callback;
    if (left_callback == 0 && right_callback == 0 && move_callback == 0) {
        return;
    }
    if (self->m_live_dialog_count != g_live_dialog_count) {
        return;
    }
    if ((reason & BUTTON_REASON_LBUTTON_DOUBLECLICK) != 0 &&
        self->m_double_click_callback != 0) {
        if (self->m_left_toggles && (button->uiFlags & BUTTON_CLICKED_ON) == 0) {
            return;
        }
        self->m_double_click_callback(self);
        return;
    }

    if (reason & BUTTON_REASON_LBUTTON_DWN) {
        if (!self->m_left_toggles) {
            if (button->uiFlags & BUTTON_CLICKED_ON) return;
            button->uiFlags |= BUTTON_CLICKED_ON;
            self->m_dirty = true;
            if (self->silent) return;
            PlayButtonSound(2);
            return;
        }
        button->uiFlags ^= BUTTON_CLICKED_ON;
        self->m_dirty = true;
        self->m_latched_press_bits =
            static_cast<unsigned char>(button->uiFlags & BUTTON_CLICKED_ON);
        if (left_callback) left_callback(self);
    } else if (reason & BUTTON_REASON_LBUTTON_UP) {
        if (self->m_left_toggles) {
            return;
        }
        if ((button->uiFlags & BUTTON_CLICKED_ON) == 0) {
            return;
        }
        button->uiFlags &= ~BUTTON_CLICKED_ON;
        self->m_dirty = true;
        if (self->m_fires_on_press && self->press_armed) {
            self->press_armed = false;
            return;
        }
        if (left_callback) left_callback(self);
    } else if (reason & BUTTON_REASON_LBUTTON_REPEAT) {
        if (!self->m_fires_on_press || self->m_left_toggles ||
            (button->uiFlags & BUTTON_CLICKED_ON) == 0) {
            return;
        }
        self->press_armed = true;
        if (left_callback) left_callback(self);
    } else if (reason & BUTTON_REASON_RBUTTON_DWN) {
        if (!self->m_right_toggles) {
            if (button->uiFlags & BUTTON_CLICKED_ON) return;
            button->uiFlags |= BUTTON_CLICKED_ON;
            self->m_dirty = true;
            if (self->silent) return;
            PlayButtonSound(2);
            return;
        }
        button->uiFlags ^= BUTTON_CLICKED_ON;
        self->m_dirty = true;
        self->m_latched_press_bits =
            static_cast<unsigned char>(button->uiFlags & BUTTON_CLICKED_ON);
        if (right_callback) right_callback(self);
    } else if (reason & BUTTON_REASON_RBUTTON_UP) {
        if (self->m_right_toggles ||
            (button->uiFlags & BUTTON_CLICKED_ON) == 0) {
            return;
        }
        button->uiFlags &= ~BUTTON_CLICKED_ON;
        self->m_dirty = true;
        if (self->m_fires_on_press && self->press_armed) {
            self->press_armed = false;
            return;
        }
        if (right_callback) right_callback(self);
    } else if (reason & BUTTON_REASON_RBUTTON_REPEAT) {
        if (!self->m_fires_on_press || self->m_right_toggles ||
            (button->uiFlags & BUTTON_CLICKED_ON) == 0) {
            return;
        }
        self->press_armed = true;
        if (right_callback) right_callback(self);
    } else if (reason & BUTTON_REASON_GAIN_MOUSE) {
        self->m_dirty = true;
        if (self->hover_silent || self->silent) return;
        PlayButtonSound(0);
        return;
    } else if (reason & BUTTON_REASON_LOST_MOUSE) {
        self->press_armed = false;
        self->m_dirty = true;
        if (!self->m_left_toggles) self->SetPressed(false);
        if (self->hover_silent || self->silent) return;
        PlayButtonSound(1);
        return;
    } else if (reason & BUTTON_REASON_MOVE) {
        if (move_callback) move_callback(self);
        return;
    } else {
        return;
    }
    if (!silent) PlayButtonSound(3);
}

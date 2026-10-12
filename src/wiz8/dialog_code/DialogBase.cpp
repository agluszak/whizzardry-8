#include <utility>
#include <SDL3/SDL_rect.h>
#include <string>
#include <string_view>
#include <vector>

#include "wiz8/engine_code/Video2.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/dialog_code/DialogTextArea.h"
#include "wiz8/regions.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "input.h"
#include "wiz8/dialog_code/ButtonAppearance.h"

// GLOBAL: WIZ8 0x0069CA28
static std::vector<W8DialogBase*> g_live_dialogs;

RegionSetRef CurrentInputLayer()
{
    return CaptureRegionSet(g_live_dialogs.empty() ? ControlRegionSetIndex()
                                                   : g_live_dialogs.back()->RegionSet());
}

unsigned int InputLayerSet(const RegionSetRef& layer)
{
    return RegionSetLive(layer) ? layer.index : ControlRegionSetIndex();
}

void W8DialogFrame::Configure(ButtonAppearance appearance, const SDL_Rect& bounds,
                              unsigned int set)
{
    m_appearance = std::move(appearance);
    SetBounds(bounds);
    ClaimRegion(REGION_PRIORITY_BACKGROUND, set);
}

void W8DialogFrame::Reset()
{
    ReleaseRegion();
    m_appearance = {};
    SetBounds({});
}

void W8DialogFrame::Draw(SDL_Surface& target) const
{
    DrawButtonAppearance(target, m_appearance, ScreenBounds(), {});
}

void W8DialogBase::CreateCloseButton(W8ButtonWidget& button, int left, int top)
{
    button.Configure("Data\\Dialogs\\popup_confirmationbuttons.sti", 3, 0, 1, 4, 2,
                     DialogCloseButtonCallback, BUTTON_NO_CALLBACK, false, REGION_PRIORITY_HIGHEST,
                     W8_DIALOG_BUTTON_NO_TOOLTIP, BUTTON_NO_CALLBACK, BUTTON_NO_CALLBACK);
    button.SetPosition(m_x + left, m_y + top);
    button.m_owner = this;
}

void W8DialogBase::ScrollTextArea(W8DialogTextArea& area, int first_visible_line, int left, int top,
                                  int width, int height)
{
    area.SetFirstVisibleLine(first_visible_line);
    left += m_x;
    top += m_y;
    int right = left + width;
    int bottom = top + height;
    InvalidateRegion(left, top, right, bottom);
    CatalogArtwork::Load(0x1b6, 0).drawRect(GetFrameBufferSurface(), SDL_Rect{0, 0, (right) - (left), (bottom) - (top)}, left, top);
    area.m_dirty = true;
}

// FUNCTION: WIZ8 0x005dc7a0
W8DialogBase::W8DialogBase()
{
    m_dirty_flags = 0;
    m_error = 0;
    m_font = g_dialog_interface_font;
    m_foreground = g_dialog_font_foreground;
    m_background = g_dialog_font_background;
    m_x = -1;
    m_y = -1;
    m_width = -1;
    m_height = -1;
    m_background_flags = 0;
    m_field_4c = 0;
    m_initialized = false;
    m_keep_open = true;
    m_destroy_callback = 0;
    m_destroy_callback_context = 0;
    m_region_set.Enable();
    g_live_dialogs.push_back(this);
    m_right_button_down = false;
}

// FUNCTION: WIZ8 0x005dc860
W8DialogBase::~W8DialogBase()
{
    EndModalInput();
    if (m_destroy_callback) {
        m_destroy_callback(this);
    }
    DestroyControls();
    std::erase(g_live_dialogs, this);
}

// FUNCTION: WIZ8 0x005dc890
void W8DialogBase::Draw()
{
    if ((m_dirty_flags & W8_DIALOG_DIRTY_REDRAW) == 0) {
        return;
    }
    if (!m_initialized) {
        CreateControls();
    }
    if (m_error == 0 && m_frame.IsConfigured()) {
        if (m_width != m_frame.Bounds().w || m_height != m_frame.Bounds().h) {
            m_frame.SetBounds({m_x, m_y, m_width, m_height});
        }
        m_frame.Draw(GetFrameBufferSurface());
        InvalidateRegion(m_x, m_y, m_x + m_width, m_y + m_height);
    }
    m_dirty_flags &= ~W8_DIALOG_DIRTY_REDRAW;
}

// FUNCTION: WIZ8 0x005dc940
void W8DialogBase::SetText(std::string_view text)
{
    m_text.assign(text);
    m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
}

// FUNCTION: WIZ8 0x005dc9c0
void W8DialogBase::SetOrigin(int x, int y)
{
    m_x = x;
    m_y = y;
    if (m_frame.IsConfigured()) {
        m_frame.SetBounds({x, y, m_frame.Bounds().w, m_frame.Bounds().h});
    }
    m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
}

// FUNCTION: WIZ8 0x005dc9f0
void W8DialogBase::SetExtent(int width, int height)
{
    if (m_width != width || m_height != height) {
        if (m_initialized && m_width > 0 && m_height > 0) {
            ClearSurfaceRect(m_x, m_y, m_x + m_width + 1, m_y + m_height + 1);
            InvalidateRegion(m_x, m_y, m_x + m_width + 1, m_y + m_height + 1);
        }
        m_width = width;
        m_height = height;
        m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
    }
}

// FUNCTION: WIZ8 0x005dca70
void W8DialogBase::SetBackground(const char* path, int flags)
{
    m_background_path = path ? path : "";
    m_background_flags = flags;
    m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
}

// FUNCTION: WIZ8 0x005dcaf0
int W8DialogBase::CreateControls()
{
    if (m_x < 0 || m_y < 0) {
        return m_error = 1;
    }
    if (m_width < 0 || m_height < 0) {
        return m_error = 3;
    }
    if (!m_font) {
        return m_error = 2;
    }
    if (m_background_path.empty()) {
        return m_error = 4;
    }
    if (!m_border) {
        m_border = LoadGenericButtonImages({}, "Data\\Dialogs\\DialogBorder.STI", {}, "Data\\Dialogs\\DialogBorder.STI", {}, m_background_path, static_cast<short>(m_background_flags), 0, 0);
        if (!m_border) {
            return m_error = 4;
        }
    }
    m_frame.Configure({.generic_image = m_border}, {m_x, m_y, m_width, m_height}, RegionSet());
    m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
    m_initialized = true;
    return 0;
}

// FUNCTION: WIZ8 0x005dcc30
void W8DialogBase::DestroyControls()
{
    EndModalInput();
    m_frame.Reset();
    m_border.reset();
    m_text.clear();
    m_background_path.clear();
    m_background_flags = 0;
    if (m_initialized && m_x >= 0 && m_y >= 0 && m_width >= 0 && m_height >= 0) {
        ClearSurfaceRect(m_x, m_y, m_x + m_width + 1, m_y + m_height + 1);
        InvalidateRegion(m_x, m_y, m_x + m_width + 1, m_y + m_height + 1);
    }
    m_initialized = false;
}

void W8DialogBase::BeginModalInput()
{
    if (!m_input_scope) {
        m_input_scope.emplace(RegionSet());
    }
}

void W8DialogBase::EndModalInput()
{
    m_input_scope.reset();
}

// FUNCTION: WIZ8 0x005dcce0
bool W8DialogBase::ProcessInput()
{
    BeginModalInput();
    PumpUiInput({
        .consumed =
            [this](const InputEvent& input) {
                if (input.type == InputEventType::RightButtonDown) {
                    OnRightButtonDown();
                } else if (input.type == InputEventType::RightButtonUp) {
                    OnRightButtonUp();
                }
                return InputRoute::Handled;
            },
        .unhandled =
            [this](const InputEvent& input) {
                if (input.type == InputEventType::MouseWheel) {
                    OnMouseWheel(input.wheel);
                } else if (input.type == InputEventType::KeyDown && input.key == VK_ESCAPE) {
                    m_keep_open = false;
                }
                return InputRoute::Handled;
            },
    });
    if (!m_keep_open) EndModalInput();
    return m_keep_open;
}

// FUNCTION: WIZ8 0x005d6eb0
void DialogCloseButtonCallback(W8ButtonWidget* button)
{
    W8DialogBase* dialog = button->m_owner;
    if (dialog != 0) {
        dialog->m_keep_open = false;
    }
}

// FUNCTION: WIZ8 0x005d6fa0
W8DialogKind W8DialogBase::GetDialogType()
{
    return W8_DIALOG_BASIC;
}

/* Retail ICF shares this one-argument no-op with the retained widget Redraw
   body at 0x005B1BE0; this override has no separate retail address marker. */
void W8DialogBase::OnNumericInputChanged(int) {}

// FUNCTION: WIZ8 0x005ad270
void W8DialogBase::OnRightButtonDown()
{
    m_right_button_down = true;
}

// FUNCTION: WIZ8 0x005b1bf0
void W8DialogBase::OnRightButtonUp() {}

/* Retail ICF shares this no-op with the retained body at 0x005B1BE0. */
void W8DialogBase::OnMouseWheel(int) {}

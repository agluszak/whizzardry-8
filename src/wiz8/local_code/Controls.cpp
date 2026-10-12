#include <cstdint>
#include <SDL3/SDL_surface.h>
#include <tuple>
#include <SDL3/SDL_rect.h>
#include <algorithm>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>
#include <array>
#include <cstring>
#include <string>

#include "image.h"
#include "wiz8/containers.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_code/Widget.h"
#include "wiz8/local_code/TextControl.h"
#include "wiz8/local_code/RangeControl.h"
#include "wiz8/local_code/ControlSelection.h"
#include "wiz8/float_constants.h"
#include "wiz8/fonts.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/Controls.h"
#include "wiz8/regions.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/sr_api.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/item_video_object_vector.h"
#include "Font.h"
#include "input.h"
#include "layouts/main_game_screen.h"


std::uint8_t DispatchButtonRegionEvent(const InputEvent* event, W8Region* region,
                                               W8Widget* button)
{
    switch (event->type) {
    case InputEventType::LeftButtonDown:
    case InputEventType::LeftButtonRepeat:
        region->flags |= W8_REGION_LEFT_BUTTON_HELD;
        button->OnLeftButtonDown(0);
        return 1;
    case InputEventType::LeftButtonUp:
        region->flags &= ~W8_REGION_LEFT_BUTTON_HELD;
        button->OnLeftButtonUp(0);
        return 1;
    case InputEventType::MouseMove:
        if ((region->flags & W8_REGION_MOUSE_LEAVE) != 0) {
            button->OnMouseLeave(0);
            return 1;
        }
        if ((region->flags & W8_REGION_MOUSE_ENTER) != 0) {
            button->OnMouseEnter(0);
            return 1;
        }
        break;
    default:
        break;
    }
    return 0;
}

/* Original Local Code\Controls.cpp translation unit. Keep these implementations
   together and preserve their order. Retail assertions identify the panel region set
   (line 399) and the widget's m_pPanel (line 1849); other role names are recovered. */

// GLOBAL: WIZ8 0x005ED548
extern const std::uint32_t g_W8TextBufferAlignLeft = 0x01;
// GLOBAL: WIZ8 0x005ED54C
extern const std::uint32_t g_W8TextBufferAlignCenter = 0x02;
// GLOBAL: WIZ8 0x005ED550
extern const std::uint32_t g_W8TextBufferAlignRight = 0x04;
// GLOBAL: WIZ8 0x005ED554
extern const std::uint32_t g_W8TextBufferAlignMiddle = 0x08;
// GLOBAL: WIZ8 0x005ED558
extern const std::uint32_t g_W8TextBufferAlignTop = 0x10;
// GLOBAL: WIZ8 0x005ED55C
extern const std::uint32_t g_W8TextBufferAlignBottom = 0x20;
// GLOBAL: WIZ8 0x005ED560
extern const std::uint32_t g_W8TextBufferNoWrap = 0x40;
// GLOBAL: WIZ8 0x005ED56C
extern const std::uint32_t g_W8TextControlStatePressed = 0x01;
// GLOBAL: WIZ8 0x005ED570
extern const std::uint32_t g_W8TextControlStateSecondary = 0x02;
// GLOBAL: WIZ8 0x005ED578
extern const std::uint32_t g_W8TextControlLayoutToggle = 0x01;
// GLOBAL: WIZ8 0x005ED57C
extern const std::uint32_t g_W8TextControlLayoutTextBesideImage = 0x02;
// GLOBAL: WIZ8 0x005ED580
extern const std::uint32_t g_W8TextControlLayoutImageLeft = 0x04;
// GLOBAL: WIZ8 0x005ED584
extern const std::uint32_t g_W8TextControlLayoutStayLatched = 0x08;
// GLOBAL: WIZ8 0x005ED588
extern const std::uint32_t g_W8TextControlLayoutLatchedImage = 0x10;
// GLOBAL: WIZ8 0x005ed594
extern const std::uint32_t g_W8TextControlLayoutImageAtOrigin = 0x80;

/* The default constructor. Everything the seven-argument one takes from its
   caller, this one zeroes or sets to -1. */
// FUNCTION: WIZ8 0x004f2c30
Controls::Controls()
{
    m_backgroundImage = -1;
    m_fEnabled = false;
    m_fDirty = false;
    m_fLayoutDirty = false;
    m_bounds.left = 0;
    m_bounds.top = 0;
    m_bounds.right = 0;
    m_bounds.bottom = 0;
    m_dirtyRect.left = -1;
    m_fWholeAreaDirty = true;
}

// FUNCTION: WIZ8 0x004f2ca0
Controls::Controls(std::int32_t left, std::int32_t top, std::int32_t right_bound, std::int32_t bottom_bound,
                   std::optional<CatalogArtwork> background, std::int32_t background_image)
{
    m_bounds.left = left;
    m_bounds.right = right_bound;
    m_fEnabled = false;
    m_fDirty = false;
    m_fLayoutDirty = false;
    m_background = std::move(background);
    m_backgroundImage = background_image;
    m_bounds.top = top;
    m_bounds.bottom = bottom_bound;
    m_dirtyRect.left = -1;
    m_fWholeAreaDirty = true;
}

// FUNCTION: WIZ8 0x004f2d30
Controls::~Controls()
{
    DestroyAllControls();
    DestroyRegionSet(m_regionSet);
}

/* 0x00562A50 takes the redraw-request mask the panel raises. */
// GLOBAL: WIZ8 0x0060CC74
char g_W8TextSeparator[] = " ";
// GLOBAL: WIZ8 0x00617C88
/* Newlines are hard breaks: text can carry them, and a previous layout pass
   leaves its own wraps in the buffer. Measuring one as a glyph is an error. */
static char g_W8TextBreakCharacters[] = " \n";

// GLOBAL: WIZ8 0x005ebb38
const float g_float_one = 1.0f;

// GLOBAL: WIZ8 0x005ebc7c
const float g_float_half = 0.5f;

// GLOBAL: WIZ8 0x005ebcd8
const float g_float_thirty_five_hundredths = 0.35f;

// FUNCTION: WIZ8 0x004f30f0
void Controls::EnableRegionSet(bool enable)
{
    if (m_regionSet == 0) return;
    if (enable) {
        RegionSetEnable(m_regionSet);
    } else {
        RegionSetDisable(m_regionSet);
    }
}

bool W8Widget::OwnsRegion() const
{
    return m_region >= 0 && static_cast<std::uint32_t>(m_region) < g_regions.size() &&
           g_regions[m_region].widget == this;
}

static constexpr RegionTemplate g_widget_region_template{
    W8_REGION_RECTANGLE, 0, 0, 0, 0, DispatchControlRegionEvent, 0, false, -1};

// FUNCTION: WIZ8 0x004f40f0
void W8Widget::SetActive(bool active)
{
    m_active = active;
    if (OwnsRegion()) {
        if (active) EnableRegionInput(m_region);
        else DisableRegionInput(m_region);
    }
}

void W8Widget::ReleaseRegion()
{
    if (OwnsRegion()) {
        UnbindWidgetRegion(static_cast<std::uint32_t>(m_region), this, &m_owned_region);
    }
    m_owned_region.reset();
    m_region = -1;
}

// FUNCTION: WIZ8 0x004f3f10
W8Widget::~W8Widget()
{
    ReleaseRegion();
    if (m_pPanel) m_pPanel->TakeControl(this).release();
}

void W8Widget::UpdateRegionBounds(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom)
{
    if (!OwnsRegion() || !m_pPanel) return;
    SetRegionBounds(m_region,
                    static_cast<std::uint16_t>(static_cast<std::int16_t>(left) +
                                                static_cast<std::int16_t>(m_pPanel->m_bounds.left)),
                    static_cast<std::uint16_t>(static_cast<std::int16_t>(top) +
                                                static_cast<std::int16_t>(m_pPanel->m_bounds.top)),
                    static_cast<std::uint16_t>(static_cast<std::int16_t>(right) +
                                                static_cast<std::int16_t>(m_pPanel->m_bounds.left)),
                    static_cast<std::uint16_t>(static_cast<std::int16_t>(bottom) +
                                                static_cast<std::int16_t>(m_pPanel->m_bounds.top)));
}

/* Move a widget between panels, preserving its existing region when it has
   one and allocating a region from the new panel otherwise. */
// FUNCTION: WIZ8 0x004f3f30
void W8Widget::SetPanel(Controls& new_panel)
{
    auto* panel = &new_panel;
    if (panel == m_pPanel) return;
    // Allocate before unlinking so failure leaves the old owner intact.
    if (panel->m_controls.size() == panel->m_controls.capacity())
        panel->m_controls.reserve(std::max(size_t{8}, panel->m_controls.capacity() * 2));
    // A widget's region lives in its panel's set; moving panels moves the region.
    const bool needs_region =
        panel->m_regionSet != 0 &&
        (m_region == -1 || (OwnsRegion() && m_owned_region.set() != panel->m_regionSet));
    OwnedRegion allocated_region;
    if (needs_region) {
        allocated_region = OwnedRegion(panel->m_regionSet, g_widget_region_template);
    }
    const auto region = needs_region ? allocated_region.index() : 0;
    if (m_pPanel) {
        auto& children = m_pPanel->m_controls;
        const auto found = std::ranges::find(children, this, &std::unique_ptr<W8Widget>::get);
        auto owned = std::move(*found);
        children.erase(found);
        panel->m_controls.push_back(std::move(owned));
    } else {
        panel->m_controls.emplace_back(this);
    }
    m_pPanel = panel;
    if (needs_region) {
        SetRegion(region);
        m_owned_region = std::move(allocated_region);
    } else if (m_region != -1) {
        SetRegion(m_region);
    }
}

// FUNCTION: WIZ8 0x004f3dd0
W8Widget::W8Widget(Controls* owner, std::uint32_t region, std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom)
    : W8Widget()
{
    m_left = left;
    m_top = top;
    m_right = right;
    m_bottom = bottom;
    m_region = static_cast<std::int32_t>(region);
    if (owner) SetPanel(*owner);
}

// Dynamic regions bind directly to a widget. Removing or moving another
// child cannot change the event target.
// FUNCTION: WIZ8 0x004F3140
std::uint8_t DispatchControlRegionEvent(const InputEvent* event, W8Region* region)
{
    W8Widget* widget = region->widget;
    if (!widget) return 0;
    const auto reason = event->type;
    switch (reason) {
    case InputEventType::LeftButtonDown:
        region->flags |= W8_REGION_LEFT_BUTTON_HELD;
        widget->OnLeftButtonDown(0);
        return 1;
    case InputEventType::LeftButtonUp:
        if ((region->flags & W8_REGION_LEFT_BUTTON_HELD) == 0) {
            return 1;
        }
        region->flags &= ~W8_REGION_LEFT_BUTTON_HELD;
        widget->OnLeftButtonUp(0);
        return 1;
    case InputEventType::LeftButtonDoubleClick:
        widget->OnLeftButtonDoubleClick(0);
        return 1;
    case InputEventType::LeftButtonRepeat:
        if ((region->flags & W8_REGION_LEFT_BUTTON_HELD) == 0) {
            return 1;
        }
        widget->ActivatePrimary(0);
        return 1;
    case InputEventType::RightButtonDown:
        region->flags |= W8_REGION_RIGHT_BUTTON_HELD;
        widget->OnRightButtonDown(0);
        return 1;
    case InputEventType::RightButtonUp:
        if ((region->flags & W8_REGION_RIGHT_BUTTON_HELD) == 0) {
            return 1;
        }
        region->flags &= ~W8_REGION_RIGHT_BUTTON_HELD;
        widget->OnRightButtonUp(0);
        return 1;
    case InputEventType::RightButtonRepeat:
        if ((region->flags & W8_REGION_RIGHT_BUTTON_HELD) == 0) {
            return 1;
        }
        widget->ActivateSecondary(0);
        return 1;
    case InputEventType::MouseMove:
        if ((region->flags & W8_REGION_MOUSE_LEAVE) != 0) {
            widget->OnMouseLeave(0);
        } else if ((region->flags & W8_REGION_MOUSE_ENTER) != 0) {
            widget->OnMouseEnter(0);
        } else {
            widget->OnMouseMove(region->flags & W8_REGION_LEFT_BUTTON_HELD);
        }
        return 1;
    case InputEventType::MouseWheel:
        widget->AdjustValue(event->wheel);
        return 1;
    default:
        break;
    }
    return 0;
}

// FUNCTION: WIZ8 0x004f4020
void W8Widget::SetRegion(std::uint32_t region)
{
    if (m_region != static_cast<std::int32_t>(region)) ReleaseRegion();
    m_region = static_cast<std::int32_t>(region);
    if (m_region == -1) return;
    g_regions[m_region].widget = this;
    ++g_regions[m_region].binding_generation;
    UpdateRegionBounds(m_left, m_top, m_right, m_bottom);
    SetActive(m_active);
}

/*
 * The class at vtable 0x005ED5B8. It owns a wide-string buffer at +0x34, which
 * is the one field the encodings name for themselves: the destructor frees it
 * and 0x004F33A0 fills it with strcpy. Everything else the constructor touches
 * is positional.
 */

// FUNCTION: WIZ8 0x004f3480
W8TextBuffer::~W8TextBuffer() = default;

// FUNCTION: WIZ8 0x004f3310
W8TextBuffer::W8TextBuffer()
{
    m_buffer.clear();
    m_font = 0;
    m_lineCount = 0;
    m_geometryDirty = false;
    m_layoutMode = 10;
    m_maxLineWidth = 0;
    m_lineHeight = 0;
    m_renderMode = 4;
    m_alternateRenderer = 0;
    m_fontStateIndex = -1;
    m_highlighted = false;
    m_layoutBounds.left = 0;
    m_layoutBounds.top = 0;
    m_layoutBounds.right = 0;
    m_layoutBounds.bottom = 0;
    m_pendingBounds.left = 0;
    m_pendingBounds.top = 0;
    m_pendingBounds.right = 0;
    m_pendingBounds.bottom = 0;
}

// FUNCTION: WIZ8 0x004f34d0
void W8TextBuffer::SetText(std::string_view text, Font& font)
{
    m_font = &font;
    m_text.assign(text);
    UpdateLayout();
    m_geometryDirty = true;
}

// FUNCTION: WIZ8 0x004f33a0
W8TextBuffer::W8TextBuffer(const W8ControlsRect* bounds, std::string_view text, Font& font,
                           std::uint32_t layout_mode, std::int32_t render_mode)
{
    m_buffer.clear();
    m_font = 0;
    m_lineCount = 0;
    m_maxLineWidth = 0;
    m_geometryDirty = false;
    m_lineHeight = 0;
    m_alternateRenderer = 0;
    SetLayoutMode(layout_mode);
    m_renderMode = render_mode;
    m_fontStateIndex = -1;
    m_highlighted = false;
    m_layoutBounds = *bounds;
    m_pendingBounds = *bounds;
    SetText(text, font);
}

// FUNCTION: WIZ8 0x004f34a0
void W8TextBuffer::SetLayoutMode(std::uint32_t layout_mode)
{
    m_layoutMode = layout_mode;
    if ((m_layoutMode & 7) == 0) {
        m_layoutMode |= 2;
    }
    if ((m_layoutMode & 0x38) == 0) {
        m_layoutMode |= 8;
    }
}

// FUNCTION: WIZ8 0x004f3540
void W8TextBuffer::SetLayoutBounds(const W8ControlsRect* bounds, bool copy_pending,
                                   bool update_layout)
{
    m_layoutBounds = *bounds;
    if (copy_pending) {
        m_pendingBounds = *bounds;
    }
    if (update_layout && !m_buffer.empty()) {
        UpdateLayout();
    }
    m_geometryDirty = true;
}

// FUNCTION: WIZ8 0x004f35b0
void W8TextBuffer::UpdateLayout()
{
    m_buffer = m_text;
    std::uint32_t available_width = m_layoutBounds.right - m_layoutBounds.left;
    char* line = m_buffer.data();
    std::uint32_t accumulated_width = 0;
    char* previous_break = 0;
    std::int16_t separator_width = m_font->textWidth(const_cast<char*>(g_W8TextSeparator));

    m_lineCount = 1;
    if ((m_layoutMode & 0x40) == 0) {
        m_maxLineWidth = 0;
        size_t span = strcspn(line, g_W8TextBreakCharacters);
        char* break_at = line + span;
        while (*break_at != '\0') {
            const char separator = *break_at;
            *break_at = '\0';
            std::int16_t word_width = m_font->textWidth(line);
            if (static_cast<std::uint32_t>(static_cast<std::int32_t>(word_width) + accumulated_width) <
                available_width) {
                accumulated_width +=
                    static_cast<std::int32_t>(separator_width) + static_cast<std::int32_t>(word_width);
                previous_break = break_at;
            } else {
                if (previous_break != 0) {
                    *previous_break = '\n';
                }
                std::uint32_t completed_width =
                    accumulated_width - static_cast<std::int32_t>(separator_width);
                if (m_maxLineWidth < completed_width) {
                    m_maxLineWidth = completed_width;
                }
                accumulated_width =
                    static_cast<std::int32_t>(separator_width) + static_cast<std::int32_t>(word_width);
                previous_break = 0;
                ++m_lineCount;
            }
            if (separator == '\n') {
                std::uint32_t completed_width =
                    accumulated_width - static_cast<std::int32_t>(separator_width);
                if (m_maxLineWidth < completed_width) {
                    m_maxLineWidth = completed_width;
                }
                accumulated_width = 0;
                previous_break = 0;
                ++m_lineCount;
            }
            line += span + 1;
            *break_at = separator;
            span = strcspn(line, g_W8TextBreakCharacters);
            break_at = line + span;
        }
        std::int16_t final_width = m_font->textWidth(line);
        std::uint32_t total_width = static_cast<std::int32_t>(final_width) + accumulated_width;
        if (available_width <= total_width) {
            if (previous_break != 0) {
                *previous_break = '\n';
            }
            if (m_maxLineWidth < accumulated_width) {
                m_maxLineWidth = accumulated_width;
            }
            total_width = static_cast<std::int32_t>(separator_width) + static_cast<std::int32_t>(final_width);
            ++m_lineCount;
        }
        if (m_maxLineWidth < total_width) {
            m_maxLineWidth = total_width;
        }
        return;
    }
    m_maxLineWidth = m_buffer.size();
}

/* Resolves one measured line against the horizontal alignment flags. */
// FUNCTION: WIZ8 0x004f3c00
std::int32_t W8TextBuffer::GetHorizontalPosition(std::int32_t width)
{
    if ((m_layoutMode & 2) != 0) {
        std::int32_t inset = (m_layoutBounds.right - m_layoutBounds.left - width) / 2;
        if (inset < 0) {
            inset = 0;
        }
        return m_layoutBounds.left + inset;
    }
    if ((m_layoutMode & 4) == 0) {
        return m_layoutBounds.left;
    }
    std::int32_t position = m_layoutBounds.right - width;
    if (position <= m_layoutBounds.left) {
        return m_layoutBounds.left;
    }
    return position;
}

/* Resolves the first baseline from the line count, line height and vertical
   alignment flags, including the font's own height inside a larger override. */
// FUNCTION: WIZ8 0x004f3c50
std::int32_t W8TextBuffer::GetVerticalPosition()
{
    std::uint32_t line_height = m_lineHeight;
    std::uint32_t font_height = m_font->height();
    std::uint32_t first_line_inset;
    if (line_height == 0) {
        line_height = font_height;
        first_line_inset = 0;
    } else {
        first_line_inset = (line_height - font_height + 1) >> 1;
    }

    if ((m_layoutMode & 0x10) != 0) {
        return m_layoutBounds.top + first_line_inset;
    }
    std::int32_t free_height = (m_layoutBounds.bottom - m_layoutBounds.top) - m_lineCount * line_height;
    if (free_height < 0) {
        free_height = 0;
    }
    if ((m_layoutMode & 0x20) != 0) {
        return m_layoutBounds.top + free_height + first_line_inset;
    }
    return m_layoutBounds.top + free_height / 2 + first_line_inset;
}

/* Rejects an override smaller than the active font's natural line height. */
// FUNCTION: WIZ8 0x004f3cf0
void W8TextBuffer::SetLineHeight(std::uint32_t height)
{
    if (height < m_font->height()) {
        m_lineHeight = 0;
        return;
    }
    m_lineHeight = height;
}

/* Fills the current text rectangle on the standard UI surface and queues the
   same rectangle for composition. */
// FUNCTION: WIZ8 0x004f3d50
void W8TextBuffer::FillBounds(std::int32_t colour)
{
    const SDL_Rect area{m_layoutBounds.left, m_layoutBounds.top, m_layoutBounds.right - m_layoutBounds.left,
                        m_layoutBounds.bottom - m_layoutBounds.top};
    SDL_FillSurfaceRect(&GetFrameBufferSurface(), &area, colour);
    InvalidateRegion(m_layoutBounds.left, m_layoutBounds.top, m_layoutBounds.right,
                     m_layoutBounds.bottom, 0);
}

/* Draw each newline-delimited view without modifying the owned text. */
// FUNCTION: WIZ8 0x004f3710
void W8TextBuffer::RenderText(SDL_Surface& target, std::int32_t x_offset, std::int32_t y_offset, bool force)
{
    std::string_view remaining = m_buffer;
    if (m_buffer.empty() || (!force && !m_geometryDirty)) {
        return;
    }

    m_font->setShade(m_renderMode);
    const auto previous_state = m_font->palette();
    if (m_fontStateIndex != -1) {
        m_font->setPalette(g_font_state_palettes[m_fontStateIndex]);
    }
    {
        SDL_Rect clip{m_pendingBounds.left, m_pendingBounds.top,
                      m_pendingBounds.right - m_pendingBounds.left,
                      m_pendingBounds.bottom - m_pendingBounds.top};
        ScopedClipRect clipping(target, clip);

        std::int32_t y = GetVerticalPosition();
        for (;;) {
            const auto end = remaining.find('\n');
            const auto line = remaining.substr(0, end);

            const std::int32_t x = GetHorizontalPosition(m_font->textWidth(line));
            DrawText(target, *m_font, {x + x_offset, y + y_offset}, line,
                     {.mono = m_alternateRenderer != 0});

            if (end == std::string_view::npos)
                break;
            y += GetLineHeight();
            if (m_layoutBounds.bottom <= y)
                break;
            remaining.remove_prefix(end + 1);
        }

        m_font->setPalette(previous_state);
    }
    m_geometryDirty = false;
}

/* The state-5 option and summary panels use the simpler surface renderer. It
   shares the buffer's layout and palette state but prints directly to the
   selected target, then restores the full-screen clip. */
// FUNCTION: WIZ8 0x004f39b0
void W8TextBuffer::RenderToTarget(std::int32_t offset, bool force, SDL_Surface& target)
{
    std::string_view remaining = m_buffer;
    if (m_buffer.empty() || (!force && !m_geometryDirty)) {
        return;
    }

    m_font->setShade(m_renderMode);
    SDL_Rect clip{m_pendingBounds.left, m_pendingBounds.top,
                  m_pendingBounds.right - m_pendingBounds.left,
                  m_pendingBounds.bottom - m_pendingBounds.top};
    ScopedClipRect clipping(target, clip);
    const auto previous_state = m_font->palette();
    if (m_fontStateIndex != -1) {
        m_font->setPalette(g_font_state_palettes[m_fontStateIndex]);
    }
    if (m_highlighted) {
        m_font->setPalette(g_font_state_palettes[W8_FONT_PALETTE_GREEN]);
    }

    std::int32_t y = GetVerticalPosition();
    for (;;) {
        const auto end = remaining.find('\n');
        const auto line = remaining.substr(0, end);

        const std::int32_t x = GetHorizontalPosition(m_font->textWidth(line));
        DrawText(target, *m_font, {x + offset, y + offset}, line);

        if (end == std::string_view::npos)
            break;
        y += GetLineHeight();
        if (m_layoutBounds.bottom <= y)
            break;
        remaining.remove_prefix(end + 1);
    }

    m_font->setPalette(previous_state);
    InvalidateRegion(m_layoutBounds.left, m_layoutBounds.top, m_layoutBounds.right,
                     m_layoutBounds.bottom, 0);
    m_geometryDirty = false;
}

/* Copies the owned text into caller storage. The caller supplies the capacity;
   the canonical method performs the same unbounded wide-string copy. */
// FUNCTION: WIZ8 0x004f3990
// FUNCTION: WIZ8 0x004f3d30
std::uint32_t W8TextBuffer::GetLineHeight()
{
    std::uint32_t height = m_lineHeight;
    if (height == 0) {
        height = m_font->height();
    }
    return height;
}

/* Concrete controls share the widget dirty flag, panel invalidation and
   frame redraw request. Text controls also invalidate their text geometry. */
// FUNCTION: WIZ8 0x004f40a0
void W8Widget::Invalidate(bool immediate)
{
    if (m_pPanel != 0) {
        m_dirty = true;
        if (immediate) {
            m_pPanel->Invalidate(0);
            RequestRedraw(W8_MAIN_REDRAW_FRAME);
            return;
        }
        m_pPanel->InvalidateLayout();
        RequestRedraw(W8_MAIN_REDRAW_FRAME);
    }
}

// FUNCTION: WIZ8 0x004f4650
void W8TextControl::Invalidate(bool immediate)
{
    W8Widget::Invalidate(immediate);
    m_textBuffer.SetGeometryDirty();
}

/* The empty text control used as a base by controls that finish their setup
   later. */
// FUNCTION: WIZ8 0x004f4160
W8TextControl::W8TextControl()
{
    m_stateFlags = 0;
    m_layoutFlags = 0;
    m_alternateTextEnabled = false;
    m_imageYOffset = -1;
    m_normalSprite = -1;
    m_pressedSprite = -1;
    m_alternatePressedSprite = -1;
    m_alternateNormalSprite = -1;
    m_disabledSprite = -1;
    m_pressedTextOffset = 1;
    m_listener = 0;
    m_textBuffer.MarkGeometryDirty(10);
}

void W8TextControl::SetImage(CatalogArtwork artwork)
{
    m_imageSprites = artwork.spriteSheet();
    m_imageYOffset = artwork.imageOffset();
    m_measured_w = -1;
    m_measured_h = -1;
    m_normalSprite = 0;
    m_pressedSprite = 0;
}

void W8TextControl::SetItemImage(std::int32_t item_id)
{
    m_imageSprites = g_item_video_objects.SharedSprites(item_id);
    m_imageYOffset = 0;
    m_measured_w = -1;
    m_measured_h = -1;
    m_normalSprite = 0;
    m_pressedSprite = 0;
}

void W8TextControl::ClearImage()
{
    m_imageSprites.reset();
    m_imageYOffset = -1;
    m_measured_w = -1;
    m_measured_h = -1;
    m_normalSprite = -1;
    m_pressedSprite = -1;
}

/* Refresh the cached extent from the preferred text handle, falling back to
   the alternate handle. */
// FUNCTION: WIZ8 0x004F4800
bool W8TextControl::MeasureText()
{
    std::int32_t handle;

    if (m_imageSprites &&
        ((handle = m_normalSprite) != -1 || (handle = m_pressedSprite) != -1)) {
        std::tie(m_measured_w, m_measured_h) =
            GetSpriteSize(*m_imageSprites, m_imageYOffset + handle);
        return true;
    }
    m_measured_w = -1;
    m_measured_h = -1;
    return false;
}

/* The 182-caller text-control constructor. The first six arguments construct
   the reviewed widget base, while the implicit W8TextBuffer constructor owns
   the second EH state. The image/sprite arguments are named for the fields
   their direct stores initialize; note the alternate normal sprite precedes
   the alternate pressed sprite in the argument list. Retail retains only this
   constructor and the default one, so call-site argument patterns do not
   establish overloads or default arguments. */
// FUNCTION: WIZ8 0x004f4250
W8TextControl::W8TextControl(Controls* panel, std::uint32_t region, std::int32_t left, std::int32_t top, std::int32_t right,
                             std::int32_t bottom, std::optional<CatalogArtwork> image_artwork, std::int32_t normal_sprite,
                             std::int32_t pressed_sprite, std::int32_t alternate_normal_sprite,
                             std::int32_t alternate_pressed_sprite, std::int32_t disabled_sprite)
    : W8Widget(panel, region, left, top, right, bottom)
{
    m_pressedSprite = pressed_sprite;
    m_alternatePressedSprite = alternate_pressed_sprite;
    m_alternateNormalSprite = alternate_normal_sprite;
    m_stateFlags = 0;
    m_alternateTextEnabled = false;
    m_normalSprite = normal_sprite;
    m_disabledSprite = disabled_sprite;
    m_layoutFlags = 0;
    m_listener = 0;
    m_pressedTextOffset = 1;
    m_imageYOffset = -1;
    if (image_artwork) {
        m_imageSprites = image_artwork->spriteSheet();
        m_imageYOffset = image_artwork->imageOffset();
    }

    MeasureText();

    if (right == 0 || bottom == 0) {
        if (right == 0) {
            right = left + m_measured_w;
        }
        if (bottom == 0) {
            bottom = top + m_measured_h;
        }
        SetBounds(left, top, right, bottom);
    }

    m_textBuffer.SetLayoutBounds(panel->m_bounds.left + left, panel->m_bounds.top + top,
                                 panel->m_bounds.left + right, panel->m_bounds.top + bottom);
    if (m_textBuffer.HasBuffer()) {
        m_textBuffer.UpdateLayout();
    }
    m_textBuffer.MarkGeometryDirty(10);
}

/* Where the text should be drawn: the panel origin plus either the widget's
   corner or an alignment computed from its cached measured extent. */
// FUNCTION: WIZ8 0x004f4850
void W8TextControl::GetTextOrigin(std::int32_t* px, std::int32_t* py)
{
    if (m_pPanel == 0) {
        srAssertFail("m_pPanel != NULL", "C:\\Projects\\Wizardry 8\\Local Code\\Controls.cpp",
                     0x739, 0);
    }
    *px = m_pPanel->m_bounds.left;
    *py = m_pPanel->m_bounds.top;
    if (((m_measured_w == -1 || m_measured_h == -1) && !MeasureText()) ||
        (m_layoutFlags & g_W8TextControlLayoutImageAtOrigin) != 0) {
        *px += m_left;
        *py += m_top;
        return;
    }
    *px += (m_layoutFlags & g_W8TextControlLayoutImageLeft) != 0 ? m_left : m_right - m_measured_w;
    *py += ((m_bottom - m_measured_h) - m_top) / 2 + m_top;
}

// FUNCTION: WIZ8 0x004f4990
void W8TextControl::Redraw(bool full_redraw)
{
    if (!m_active || m_pPanel == 0) {
        return;
    }

    std::int32_t text_state = 0;
    if ((m_stateFlags & g_W8TextControlStatePressed) != 0 &&
        (m_layoutFlags & g_W8TextControlLayoutTextBesideImage) == 0) {
        text_state = m_pressedTextOffset;
    }

    if (!full_redraw && !m_dirty) {
        if (m_textBuffer.HasBuffer()) {
            m_textBuffer.RenderToTarget(text_state, false, GetFrameBufferSurface());
        }
        return;
    }

    if (!m_imageSprites) {
        if (m_textBuffer.HasBuffer()) {
            m_textBuffer.RenderToTarget(text_state, full_redraw, GetFrameBufferSurface());
        }
        return;
    }

    std::int32_t sprite;
    if (!m_enabled) {
        sprite = m_disabledSprite;
        if (sprite == -1) {
            if (m_textBuffer.HasBuffer()) {
                m_textBuffer.RenderToTarget(text_state, full_redraw, GetFrameBufferSurface());
            }
            const std::int32_t left = m_pPanel->m_bounds.left + m_left;
            const std::int32_t top = m_pPanel->m_bounds.top + m_top;
            ShadowRect(GetFrameBufferSurface(),
                       {left, top, m_right - m_left + 1, m_bottom - m_top + 1});
            m_dirty = false;
            return;
        }
    } else if ((m_stateFlags & g_W8TextControlStatePressed) == 0) {
        sprite = m_alternateTextEnabled && m_alternateNormalSprite != -1 ? m_alternateNormalSprite
                                                                         : m_normalSprite;
    } else {
        sprite = m_alternateTextEnabled && m_alternatePressedSprite != -1 ? m_alternatePressedSprite
                                                                          : m_pressedSprite;
    }

    std::int32_t x;
    std::int32_t y;
    GetTextOrigin(&x, &y);
    if (sprite != -1) {
        if (m_alternateTextEnabled) {
            std::int16_t width;
            std::int16_t height;
            std::tie(width, height) =
                GetSpriteSize(*m_imageSprites, m_imageYOffset + sprite);
            std::int32_t x_inset = m_left - m_right + static_cast<std::uint16_t>(width);
            std::int32_t y_inset = m_top - m_bottom + static_cast<std::uint16_t>(height);
            if (x_inset > 0) {
                x += x_inset / 2;
            }
            if (y_inset > 0) {
                y += y_inset / 2;
            }
        }
        DrawSpriteAndInvalidate(GetFrameBufferSurface(), *m_imageSprites,
                                m_imageYOffset + sprite, x, y);
    }

    if (m_textBuffer.HasBuffer()) {
        m_textBuffer.RenderToTarget(text_state, full_redraw, GetFrameBufferSurface());
    }
    m_dirty = false;
}

// FUNCTION: WIZ8 0x004f4460
void W8TextControl::SetBoundsFromRect(const W8ControlsRect* bounds)
{
    SetBounds(bounds->left, bounds->top, bounds->right, bounds->bottom);
    if (m_region != -1 && m_pPanel != 0) {
        UpdateRegionBounds(bounds->left, bounds->top, bounds->right, bounds->bottom);
    }
}

void W8TextControl::UpdateTextLayout()
{
    std::int16_t measured_width;
    std::int16_t measured_height;
    std::int32_t absolute_left = m_pPanel->m_bounds.left + m_left;
    std::int32_t absolute_top = m_pPanel->m_bounds.top + m_top;
    std::int32_t absolute_right = m_pPanel->m_bounds.left + m_right;
    std::int32_t absolute_bottom = m_pPanel->m_bounds.top + m_bottom;
    if (m_imageSprites) {
        std::tie(measured_width, measured_height) =
            GetSpriteSize(*m_imageSprites, m_imageYOffset + m_normalSprite);
        if ((m_layoutFlags & g_W8TextControlLayoutImageLeft) != 0) {
            absolute_left += 2 + static_cast<std::uint16_t>(measured_width);
        } else {
            absolute_right -= 2 + static_cast<std::uint16_t>(measured_width);
        }
    }
    m_textBuffer.SetLayoutBounds(absolute_left, absolute_top, absolute_right, absolute_bottom);
    if (m_textBuffer.HasBuffer()) {
        m_textBuffer.UpdateLayout();
    }
    m_textBuffer.MarkGeometryDirty(9);
}

// FUNCTION: WIZ8 0x004f44d0
void W8TextControl::SetBounds(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom)
{
    W8Widget::SetBounds(left, top, right, bottom);
    if (m_pPanel != 0) {
        if (m_region != -1) {
            UpdateRegionBounds(left, top, right, bottom);
        }
        if ((m_layoutFlags & g_W8TextControlLayoutTextBesideImage) != 0) {
            UpdateTextLayout();
        }
    }
}

// FUNCTION: WIZ8 0x004f4600
void W8TextControl::SetFlaggedRegionBounds(std::int32_t left, std::int32_t top, std::int32_t right)
{
    if (m_region != -1 && m_pPanel != 0 && RegionHasFlags(m_region, 2)) {
        SetRegionBounds(m_region, m_pPanel->m_bounds.left + left, m_pPanel->m_bounds.top + top,
                        right, 0);
    }
}

// FUNCTION: WIZ8 0x004f46a0
void W8TextControl::AddLayoutFlags(std::uint32_t flags)
{
    m_layoutFlags |= flags;
    if (m_pPanel != 0 && (m_layoutFlags & g_W8TextControlLayoutTextBesideImage) != 0) {
        UpdateTextLayout();
    }
}

// FUNCTION: WIZ8 0x004f4780
void W8TextControl::RemoveLayoutFlags(std::uint32_t flags)
{
    if ((flags & g_W8TextControlLayoutTextBesideImage) != 0 && m_pPanel != 0) {
        m_textBuffer.SetLayoutBounds(
            m_pPanel->m_bounds.left + m_left, m_pPanel->m_bounds.top + m_top,
            m_pPanel->m_bounds.left + m_right, m_pPanel->m_bounds.top + m_bottom);
        if (m_textBuffer.HasBuffer()) {
            m_textBuffer.UpdateLayout();
        }
        m_textBuffer.MarkGeometryDirty(10);
    }
    m_layoutFlags &= ~flags;
}

// FUNCTION: WIZ8 0x004f4c40
void W8TextControl::EnableSecondaryState(bool immediate)
{
    if ((m_layoutFlags & g_W8TextControlLayoutToggle) != 0 &&
        (m_stateFlags & g_W8TextControlStateSecondary) == 0) {
        m_stateFlags |= g_W8TextControlStatePressed;
        m_stateFlags |= g_W8TextControlStateSecondary;
        Invalidate(immediate);
    }
}

// FUNCTION: WIZ8 0x004f4cb0
void W8TextControl::DisableSecondaryState(bool immediate)
{
    if ((m_layoutFlags & g_W8TextControlLayoutToggle) != 0 &&
        (m_stateFlags & g_W8TextControlStateSecondary) != 0) {
        m_stateFlags &= ~g_W8TextControlStatePressed;
        m_stateFlags &= ~g_W8TextControlStateSecondary;
        Invalidate(immediate);
    }
}

// FUNCTION: WIZ8 0x004f4d30
void W8TextControl::OnMouseEnter(std::int32_t event)
{
    if (!m_active) {
        return;
    }
    if (!m_enabled) {
        PushButtonSoundScheme(0, true);
        SetAlternateTextEnabled(true);
        return;
    }
    if ((m_layoutFlags & (W8_TEXT_CONTROL_SILENT | g_W8TextControlSilentHover)) != 0) {
        PushButtonSoundScheme(0, true);
    }

    if ((m_stateFlags & g_W8TextControlStatePressed) == 0) {
        if (m_alternateNormalSprite == -1) {
            return;
        }
    } else if (m_alternatePressedSprite == -1) {
        return;
    }

    SetAlternateTextEnabled(true);
    Invalidate(static_cast<std::uint8_t>(event));
}

// FUNCTION: WIZ8 0x004f4e00
void W8TextControl::OnMouseLeave(std::int32_t event)
{
    if (!m_active) {
        return;
    }
    if (!m_enabled) {
        PushButtonSoundScheme(0, true);
        if ((m_layoutFlags & g_W8TextControlLayoutToggle) == 0) {
            m_stateFlags &= ~g_W8TextControlStatePressed;
        }
        SetAlternateTextEnabled(false);
        return;
    }
    if ((m_layoutFlags & (W8_TEXT_CONTROL_SILENT | g_W8TextControlSilentHover)) != 0) {
        PushButtonSoundScheme(0, true);
    }
    if ((m_layoutFlags & W8_TEXT_CONTROL_ACTIVATE_WHILE_HELD) != 0 &&
        (m_stateFlags & W8_TEXT_CONTROL_ACTIVATED_WHILE_HELD) != 0) {
        PushButtonSoundScheme(0, true);
        m_stateFlags &= ~W8_TEXT_CONTROL_ACTIVATED_WHILE_HELD;
    }

    if ((m_stateFlags & g_W8TextControlStatePressed) == 0) {
        if (m_alternateNormalSprite == -1) {
            return;
        }
        SetAlternateTextEnabled(false);
        Invalidate(static_cast<std::uint8_t>(event));
        return;
    }

    if (m_alternatePressedSprite != -1 ||
        (m_alternateNormalSprite != -1 &&
         (m_layoutFlags & g_W8TextControlLayoutLatchedImage) != 0)) {
        SetAlternateTextEnabled(false);
        Invalidate(static_cast<std::uint8_t>(event));
    }
    if ((m_stateFlags & g_W8TextControlStateSecondary) != 0) {
        return;
    }
    m_stateFlags &= ~g_W8TextControlStatePressed;
    Invalidate(static_cast<std::uint8_t>(event));
}

void W8TextControl::NotifyPrimaryActivation()
{
    if (m_listener != 0) {
        m_listener->OnPrimary(this);
    }
    if (m_primaryActivationCallback != 0) {
        m_primaryActivationCallback();
    }
}

void W8TextControl::NotifySecondaryActivation()
{
    if (m_listener != 0) {
        m_listener->OnSecondary(this);
    }
    if (m_secondaryActivationCallback != 0) {
        m_secondaryActivationCallback();
    }
}

// FUNCTION: WIZ8 0x004f4f70
void W8TextControl::OnLeftButtonDown(std::int32_t event)
{
    if (!m_active) {
        if (m_enabled) {
            return;
        }
        PushButtonSoundScheme(0, true);
        return;
    }
    if (!m_enabled) {
        PushButtonSoundScheme(0, true);
        return;
    }
    if ((m_layoutFlags & W8_TEXT_CONTROL_SILENT) != 0) {
        PushButtonSoundScheme(0, true);
    }

    if ((m_layoutFlags & g_W8TextControlLayoutToggle) == 0) {
        m_stateFlags |= g_W8TextControlStatePressed;
        if (m_imageSprites) {
            Invalidate(static_cast<std::uint8_t>(event));
        }
    } else if ((m_stateFlags & g_W8TextControlStatePressed) == 0) {
        m_stateFlags |= g_W8TextControlStatePressed;
        if ((m_layoutFlags & g_W8TextControlLayoutLatchedImage) == 0) {
            Invalidate(static_cast<std::uint8_t>(event));
        }
    }

    m_textBuffer.SetGeometryDirty();
    if (m_leftButtonDownCallback != 0) {
        m_leftButtonDownCallback();
    }
}

// FUNCTION: WIZ8 0x004f5070
void W8TextControl::OnRightButtonDown(std::int32_t)
{
    if ((m_active && m_enabled)) {
        if ((m_layoutFlags & W8_TEXT_CONTROL_SILENT) != 0) {
            PushButtonSoundScheme(0, true);
        }
        if (m_rightButtonDownCallback != 0) {
            m_rightButtonDownCallback();
        }
        return;
    }
    if (!m_active && m_enabled) {
        return;
    }
    PushButtonSoundScheme(0, true);
}

// FUNCTION: WIZ8 0x004f50c0
void W8TextControl::OnLeftButtonUp(std::int32_t event)
{
    if (!m_active) {
        return;
    }
    if (!m_enabled) {
        PushButtonSoundScheme(0, true);
        if ((m_layoutFlags & g_W8TextControlLayoutToggle) == 0) {
            m_stateFlags &= ~g_W8TextControlStatePressed;
        }
        return;
    }
    if ((m_stateFlags & g_W8TextControlStatePressed) == 0) {
        return;
    }
    if ((m_layoutFlags & W8_TEXT_CONTROL_SILENT) != 0) {
        PushButtonSoundScheme(0, true);
    }

    if ((m_layoutFlags & g_W8TextControlLayoutToggle) == 0) {
        m_stateFlags &= ~g_W8TextControlStatePressed;
        Invalidate(static_cast<std::uint8_t>(event));
    } else if ((m_stateFlags & g_W8TextControlStateSecondary) == 0) {
        m_stateFlags |= g_W8TextControlStatePressed;
        m_stateFlags |= g_W8TextControlStateSecondary;
        if ((m_layoutFlags & g_W8TextControlLayoutLatchedImage) != 0) {
            Invalidate(static_cast<std::uint8_t>(event));
        }
    } else if ((m_layoutFlags & g_W8TextControlLayoutStayLatched) == 0) {
        m_stateFlags &= ~g_W8TextControlStatePressed;
        m_stateFlags &= ~g_W8TextControlStateSecondary;
        Invalidate(static_cast<std::uint8_t>(event));
    }

    m_textBuffer.SetGeometryDirty();
    if ((m_layoutFlags & W8_TEXT_CONTROL_ACTIVATE_WHILE_HELD) != 0 &&
        (m_stateFlags & W8_TEXT_CONTROL_ACTIVATED_WHILE_HELD) != 0) {
        m_stateFlags &= ~W8_TEXT_CONTROL_ACTIVATED_WHILE_HELD;
        PushButtonSoundScheme(0, true);
        return;
    }
    NotifyPrimaryActivation();
}

// FUNCTION: WIZ8 0x004f5290
void W8TextControl::OnRightButtonUp(std::int32_t)
{
    if (!m_active) {
        if (m_enabled) {
            return;
        }
        PushButtonSoundScheme(0, true);
        return;
    }
    if (!m_enabled) {
        PushButtonSoundScheme(0, true);
        return;
    }
    if ((m_layoutFlags & W8_TEXT_CONTROL_SILENT) != 0) {
        PushButtonSoundScheme(0, true);
    }
    if ((m_layoutFlags & W8_TEXT_CONTROL_ACTIVATE_WHILE_HELD) != 0 &&
        (m_stateFlags & W8_TEXT_CONTROL_ACTIVATED_WHILE_HELD) != 0) {
        m_stateFlags &= ~W8_TEXT_CONTROL_ACTIVATED_WHILE_HELD;
        PushButtonSoundScheme(0, true);
        return;
    }
    NotifySecondaryActivation();
}

// FUNCTION: WIZ8 0x004f5230
void W8TextControl::ActivatePrimary(std::int32_t)
{
    if ((m_layoutFlags & W8_TEXT_CONTROL_ACTIVATE_WHILE_HELD) != 0 && m_active && m_enabled &&
        (m_stateFlags & g_W8TextControlStatePressed) != 0) {
        m_stateFlags |= W8_TEXT_CONTROL_ACTIVATED_WHILE_HELD;
        if ((m_layoutFlags & W8_TEXT_CONTROL_SILENT) == 0) {
            PlayButtonSound(3);
        }
        NotifyPrimaryActivation();
    }
}

// FUNCTION: WIZ8 0x004f5310
void W8TextControl::OnLeftButtonDoubleClick(std::int32_t)
{
    if (m_active && m_enabled) {
        if ((m_layoutFlags & W8_TEXT_CONTROL_SILENT) != 0) {
            PushButtonSoundScheme(0, true);
        }
        if ((m_layoutFlags & g_W8TextControlLayoutToggle) != 0 &&
            (m_stateFlags & g_W8TextControlStateSecondary) == 0) {
            return;
        }
        if (m_leftDoubleClickCallback != 0) {
            m_leftDoubleClickCallback();
        }
        return;
    }
    if (!m_active && m_enabled) {
        return;
    }
    PushButtonSoundScheme(0, true);
}

// FUNCTION: WIZ8 0x004f5360
void W8TextControl::ActivateSecondary(std::int32_t)
{
    if ((m_layoutFlags & W8_TEXT_CONTROL_ACTIVATE_WHILE_HELD) != 0 && m_active && m_enabled) {
        m_stateFlags |= W8_TEXT_CONTROL_ACTIVATED_WHILE_HELD;
        if ((m_layoutFlags & W8_TEXT_CONTROL_SILENT) == 0) {
            PlayButtonSound(3);
        }
        NotifySecondaryActivation();
    }
}

// FUNCTION: WIZ8 0x004f53b0
void W8TextControl::UpdateTextBounds(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom)
{
    W8ControlsRect absolute = {m_pPanel->m_bounds.left + left, m_pPanel->m_bounds.top + top,
                               m_pPanel->m_bounds.left + right, m_pPanel->m_bounds.top + bottom};
    m_textBuffer.SetLayoutBounds(&absolute, true, false);
    if (m_textBuffer.HasBuffer()) {
        m_textBuffer.UpdateLayout();
    }
    m_textBuffer.SetGeometryDirty();
}

// FUNCTION: WIZ8 0x004f5410
void W8TextControl::SetEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!enabled) {
        if (m_disabledSprite != -1) {
            m_textBuffer.SetRenderMode(7);
        }
    } else if (m_disabledSprite != -1) {
        m_textBuffer.SetRenderMode(4);
    }
}

// FUNCTION: WIZ8 0x004f69a0
void W8TextControl::SetAlternateTextEnabled(bool enabled)
{
    m_alternateTextEnabled = enabled;
}

// FUNCTION: WIZ8 0x004f65e0
W8HelpTextControl::W8HelpTextControl(Controls* panel, std::uint32_t region, std::int32_t left, std::int32_t top,
                                     std::int32_t right, std::int32_t bottom)
    : W8TextControl(panel, region, left, top, right, bottom, std::nullopt, -1, -1, -1, -1, -1)
{
    m_regionHelp.clear();
}

// VTABLE: WIZ8 0x005ed6b4
class W8VerticalRangeThumb : public W8Widget {
public:
    W8VerticalRangeThumb(W8RangeControl* range, std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom,
                         CatalogArtwork artwork, std::int32_t normal_sprite, std::int32_t hovered_sprite,
                         std::int32_t disabled_sprite);
    /* Retail ICF folds this onto W8HorizontalRangeThumb's deleting destructor
       at 0x004f69b0. */

    virtual void Redraw(bool full_redraw) override;
    void AdjustValue(std::int32_t steps) override;
    virtual void OnMouseEnter(std::int32_t event) override;
    virtual void OnMouseLeave(std::int32_t event) override;
    virtual void OnLeftButtonDown(std::int32_t event) override;
    virtual void OnLeftButtonUp(std::int32_t event) override;
    virtual void OnMouseMove(std::int32_t event) override;
    void SetRangePosition(float position)
    {
        m_position = position;
        ClampPositionAndInvalidate();
    }

protected:
    CatalogArtwork m_artwork;
    std::int32_t m_normalSprite;
    std::int32_t m_hoveredSprite;
    std::int32_t m_disabledSprite;
    std::int32_t m_drawOffsetX;
    std::int32_t m_pixelPosition;
    std::int32_t m_thumbHeight;
    std::int32_t m_trackLength;
    std::int32_t m_dragCoordinate;
    bool m_hovered;  /* cursor over the thumb */
    bool m_dragging; /* thumb drag in progress */
    std::uint8_t pad_5e[2];
    float m_minimumPosition;
    float m_maximumPosition;
    float m_position;
    W8RangeControl* m_range;

    void ClampPositionAndInvalidate();
    void SynchronizeRangeValue();
};

W8RangeButton::W8RangeButton(Controls* panel, std::uint32_t region, std::int32_t left, std::int32_t top, std::int32_t right,
                             std::int32_t bottom, std::optional<CatalogArtwork> image_artwork,
                             std::int32_t normal_sprite,
                             std::int32_t pressed_sprite, std::int32_t alternate_normal_sprite,
                             std::int32_t alternate_pressed_sprite, std::int32_t disabled_sprite, std::int16_t direction,
                             W8RangeControl* range)
    : W8TextControl(panel, region, left, top, right, bottom, std::move(image_artwork),
                    normal_sprite, pressed_sprite, alternate_normal_sprite,
                    alternate_pressed_sprite, disabled_sprite),
      m_direction(direction), m_range(range)
{
}

/* The range panel owns two text-derived step buttons and one vertical thumb.
   Retail construction passes five stack arguments and proves all three
   allocation sizes, the ordered child construction, and the four EH cleanup
   states. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshadow-field"
/* Controls stores panel bounds as `right`/`bottom`; this constructor forwards
   those same names to the base. That is original constructor style, not a
   second inherited field. */
// FUNCTION: WIZ8 0x004f61f0
W8RangeControl::W8RangeControl(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom)
    : Controls(left, top, right, bottom, std::nullopt, -1), m_minimum(0), m_maximum(1), m_value(0),
      m_listener(0)
{
    std::int32_t height = bottom - top;

    CreateRegionSet();

    m_decrement =
        new W8RangeButton(this, 0xffffffff, 0, 0, 0x10, 0x10, CatalogArtwork::Load(0x86), 0, 2, 1, 2, 3, 0, this);
    m_increment = new W8RangeButton(this, 0xffffffff, 0, height - 0x10, 0x10, height, CatalogArtwork::Load(0x86), 8,
                                    10, 9, 10, 0xb, 1, this);
    m_thumb = new W8VerticalRangeThumb(this, 0, 0x10, 0x10, height - 0x10,
                                       CatalogArtwork::Load(0x86), 4, 5, -1);
    m_enabled = false;
}
#pragma clang diagnostic pop

// The panel owns all three registered widgets. Its base destructor releases them.
W8RangeControl::~W8RangeControl() = default;

// FUNCTION: WIZ8 0x004f6440
void W8RangeControl::SetRange(std::int32_t first, std::int32_t second)
{
    if (first < second) {
        m_minimum = first;
        m_maximum = second;
    } else {
        m_maximum = first;
        m_minimum = second;
    }
    SetValue(m_value);
}

// FUNCTION: WIZ8 0x004f6470
void W8RangeControl::SetValue(std::int32_t value)
{
    m_value = value;
    if (value < m_minimum) {
        m_value = m_minimum;
    } else if (m_maximum < value) {
        m_value = m_maximum;
    }

    float position;
    if (m_value == m_minimum) {
        position = g_float_zero;
    } else if (m_value == m_maximum) {
        position = g_float_one;
    } else {
        position = ((m_value - m_minimum) + g_float_half) / ((m_maximum - m_minimum) + 1);
    }
    m_thumb->SetRangePosition(position);
}

// FUNCTION: WIZ8 0x004f6540
void W8RangeControl::Decrement()
{
    if (m_enabled) {
        SetValue(m_value - 1);
        if (m_listener != 0) {
            m_listener->OnRangeChanged(this);
        }
    }
}

// FUNCTION: WIZ8 0x004f6570
void W8RangeControl::Increment()
{
    if (m_enabled) {
        SetValue(m_value + 1);
        if (m_listener != 0) {
            m_listener->OnRangeChanged(this);
        }
    }
}

// FUNCTION: WIZ8 0x004f65a0
void W8RangeControl::SetRangeEnabled(bool enabled)
{
    m_enabled = enabled;
    m_decrement->SetEnabled(enabled);
    m_increment->SetEnabled(enabled & m_enabled);
    m_thumb->SetEnabled(enabled);
}

/* Both thumb orientations use the same normalized track coordinate. */
static std::int32_t ClampThumbToTrack(float& position, float minimum, float maximum, std::int32_t track_length)
{
    if (position < minimum)
        position = minimum;
    if (maximum < position)
        position = maximum;
    return static_cast<std::int32_t>(((position - minimum) / (maximum - minimum)) * track_length);
}

void W8VerticalRangeThumb::ClampPositionAndInvalidate()
{
    m_pixelPosition = ClampThumbToTrack(m_position, m_minimumPosition, m_maximumPosition,
                                        m_trackLength);
    Invalidate(false);
}

void W8VerticalRangeThumb::SynchronizeRangeValue()
{
    std::int32_t value = static_cast<std::int32_t>(m_range->m_thumb->m_position *
                                 (m_range->m_maximum - m_range->m_minimum + 1)) +
                m_range->m_minimum;
    if (m_range->m_maximum < value) {
        value = m_range->m_maximum;
    }
    if (value != m_range->m_value) {
        W8RangeListener* listener = m_range->m_listener;
        m_range->m_value = value;
        if (listener != 0) {
            listener->OnRangeChanged(m_range);
        }
    }
}

// FUNCTION: WIZ8 0x004f5b20
W8VerticalRangeThumb::W8VerticalRangeThumb(W8RangeControl* range, std::int32_t left, std::int32_t top, std::int32_t right,
                                           std::int32_t bottom, CatalogArtwork artwork, std::int32_t normal_sprite,
                                           std::int32_t hovered_sprite, std::int32_t disabled_sprite)
    : W8Widget(range, 0xffffffff, left, top, 0, bottom), m_artwork(std::move(artwork))
{
    std::int16_t width;
    std::int16_t height;

    m_hoveredSprite = hovered_sprite;
    m_disabledSprite = disabled_sprite;
    m_normalSprite = normal_sprite;
    m_hovered = false;
    m_dragging = false;
    m_minimumPosition = 0.0f;
    m_maximumPosition = 1.0f;
    m_position = 0.0f;
    const SDL_Point sprite_size = m_artwork.size(normal_sprite);
    width = sprite_size.x;
    height = sprite_size.y;
    if (right - left < static_cast<std::uint16_t>(width)) {
        m_right = m_left + static_cast<std::uint16_t>(width);
        m_drawOffsetX = 0;
    } else {
        m_right = right;
        m_drawOffsetX = right - static_cast<std::uint16_t>(width) - left;
    }
    SetRegion(m_region);
    m_thumbHeight = static_cast<std::uint16_t>(height);
    m_trackLength = bottom - static_cast<std::uint16_t>(height) - top;
    m_range = range;
}

// FUNCTION: WIZ8 0x004f5c00
void W8VerticalRangeThumb::OnLeftButtonDown(std::int32_t event)
{
    PushButtonSoundScheme(0, true);
    if (m_enabled) {
        SDL_Point cursor;
        SGPMouseGetPos(&cursor);
        std::int32_t y = cursor.y - m_pPanel->m_bounds.top - m_top;
        if (!m_hovered) {
            m_hovered = true;
            m_position = ((y - m_thumbHeight / 2) / static_cast<float>(m_trackLength)) *
                             (m_maximumPosition - m_minimumPosition) +
                         m_minimumPosition;
            ClampPositionAndInvalidate();
            SynchronizeRangeValue();
        }
        m_dragCoordinate = y;
        m_dragging = true;
        ActivateDialogRegion(m_region);
    }
}

// FUNCTION: WIZ8 0x004f5d30
void W8VerticalRangeThumb::OnLeftButtonUp(std::int32_t event)
{
    PushButtonSoundScheme(0, true);
    if (m_enabled && m_dragging) {
        m_dragging = false;
        ClearActiveRegionIfMatches(m_region);
    }
}

// FUNCTION: WIZ8 0x004f5d70
void W8VerticalRangeThumb::OnMouseMove(std::int32_t event)
{
    if (!m_enabled) {
        return;
    }

    SDL_Point cursor;
    SGPMouseGetPos(&cursor);
    std::int32_t y = cursor.y - m_pPanel->m_bounds.top - m_top;
    if (m_dragging) {
        std::int32_t half_height = m_thumbHeight / 2;
        if (y <= half_height) {
            m_position = 0.0f;
        } else if (m_trackLength + half_height <= y) {
            m_position = 1.0f;
        } else {
            std::int32_t delta = y - m_dragCoordinate;
            m_dragCoordinate = y;
            m_position += (delta / static_cast<float>(m_trackLength)) *
                          (m_maximumPosition - m_minimumPosition);
        }
        ClampPositionAndInvalidate();
        SynchronizeRangeValue();
        return;
    }

    bool hovered = (m_pixelPosition <= y && y <= m_pixelPosition + m_thumbHeight);
    if (hovered != m_hovered) {
        Invalidate(false);
    }
    m_hovered = hovered;
}

// FUNCTION: WIZ8 0x004f5ef0
void W8VerticalRangeThumb::AdjustValue(std::int32_t steps)
{
    m_range->AdjustValue(steps);
}

// FUNCTION: WIZ8 0x004f5f60
void W8VerticalRangeThumb::Redraw(bool full_redraw)
{
    if (!m_active || (!full_redraw && !m_dirty)) {
        return;
    }
    std::int32_t left = m_pPanel->m_bounds.left + m_left;
    std::int32_t top = m_pPanel->m_bounds.top + m_top;
    std::int32_t right = m_pPanel->m_bounds.left + m_right;
    std::int32_t bottom = m_pPanel->m_bounds.top + m_bottom;
    InvalidateRegion(left, top, right, bottom, 0);
    const SDL_Rect area{left, top, right - left, bottom - top};
    SDL_FillSurfaceRect(&GetFrameBufferSurface(), &area, 0x8000);

    std::int32_t sprite;
    if (!m_enabled) {
        sprite = m_disabledSprite;
        if (sprite == -1) {
            return;
        }
    } else if (!m_hovered || (sprite = m_hoveredSprite) == -1) {
        sprite = m_normalSprite;
    }
    m_artwork.draw(GetFrameBufferSurface(), sprite,
                   m_pPanel->m_bounds.left + m_drawOffsetX + m_left,
                   m_pPanel->m_bounds.top + m_pixelPosition + m_top);
}

// FUNCTION: WIZ8 0x004f6050
void W8RangeButton::OnLeftButtonDown(std::int32_t event)
{
    W8TextControl::OnLeftButtonDown(event);
    if (m_active && m_enabled) {
        if (m_direction == 0) {
            m_range->Decrement();
        } else {
            m_range->Increment();
        }
    }
}

// FUNCTION: WIZ8 0x004f60c0
void W8RangeButton::ActivatePrimary(std::int32_t event)
{
    W8TextControl::ActivatePrimary(event);
    if (m_active && m_enabled) {
        if (m_direction == 0) {
            m_range->Decrement();
        } else {
            m_range->Increment();
        }
    }
}

// FUNCTION: WIZ8 0x004f6180
void W8RangeButton::AdjustValue(std::int32_t steps)
{
    m_range->AdjustValue(steps);
}

// FUNCTION: WIZ8 0x004f6680
void W8HelpTextControl::SetRegionHelp(std::string_view text)
{
    if (!text.empty()) {
        m_regionHelp = text;
    }
}

// FUNCTION: WIZ8 0x004f66b0
void W8HelpTextControl::OnMouseEnter(std::int32_t event)
{
    PushButtonSoundScheme(0, true);
    W8TextControl::OnMouseEnter(event);
    if (m_regionHelp.size() > 1 && m_region != -1) {
        ::SetRegionHelpText(m_regionHelp);
        ::EnableRegionHelp(m_region);
        return;
    }
    if (m_region != -1) {
        ::DisableRegionHelp(m_region);
    }
}

// FUNCTION: WIZ8 0x005b7cb0
void W8HelpTextControl::OnLeftButtonDown(std::int32_t event)
{
    PushButtonSoundScheme(0, true);
    W8TextControl::OnLeftButtonDown(event);
}

// FUNCTION: WIZ8 0x004f6720
void W8HelpTextControl::OnRightButtonDown(std::int32_t event)
{
    if (m_secondaryActivationCallback == 0) {
        PushButtonSoundScheme(0, true);
    }
    W8TextControl::OnRightButtonDown(event);
}

// FUNCTION: WIZ8 0x005b7cd0
void W8HelpTextControl::OnLeftButtonUp(std::int32_t event)
{
    PushButtonSoundScheme(0, true);
    W8TextControl::OnLeftButtonUp(event);
}

// FUNCTION: WIZ8 0x004f6780
void W8HelpTextControl::OnRightButtonUp(std::int32_t event)
{
    if (m_secondaryActivationCallback == 0) {
        PushButtonSoundScheme(0, true);
    }
    W8TextControl::OnRightButtonUp(event);
}

// FUNCTION: WIZ8 0x004f6810
void W8HelpTextControl::OnLeftButtonDoubleClick(std::int32_t event)
{
    PushButtonSoundScheme(0, true);
    W8TextControl::OnLeftButtonDoubleClick(event);
}

/* The vtable at 0x005ED66C is the horizontal draggable range thumb. Its
   constructor measures the normal sprite for the widget bounds, then measures
   the movable thumb sprite and retains the remaining horizontal travel at
   +0x4c. The interaction methods independently prove that geometry: cursor X
   is converted through +0x4c into the normalized float range +0x60..+0x68. */

void W8HorizontalRangeThumb::ClampPositionAndInvalidate()
{
    m_pixelPosition = ClampThumbToTrack(m_position, m_minimumPosition, m_maximumPosition,
                                        m_trackLength);
    Invalidate(false);
}

// FUNCTION: WIZ8 0x004f5620
W8HorizontalRangeThumb::W8HorizontalRangeThumb(Controls* panel, std::uint32_t region, std::int32_t left,
                                               std::int32_t top, CatalogArtwork artwork,
                                               std::int32_t background_sprite, std::int32_t normal_thumb_sprite,
                                               std::int32_t hovered_thumb_sprite, std::int32_t disabled_thumb_sprite)
    : W8Widget(panel, region, left, top, 0, 0), m_artwork(std::move(artwork))
{
    std::int16_t width;
    std::int16_t height;

    m_normalThumbSprite = normal_thumb_sprite;
    m_hoveredThumbSprite = hovered_thumb_sprite;
    m_disabledThumbSprite = disabled_thumb_sprite;
    m_backgroundSprite = background_sprite;
    m_pixelPosition = 0;
    m_hovered = false;
    m_dragging = false;
    m_minimumPosition = 0.0f;
    m_maximumPosition = 1.0f;
    m_position = 0.0f;
    m_listener = 0;
    const SDL_Point background_size = m_artwork.size(background_sprite);
    width = background_size.x;
    height = background_size.y;
    m_right = static_cast<std::uint16_t>(width) + m_left;
    m_bottom = m_top + static_cast<std::uint16_t>(height);
    SetRegion(m_region);
    const SDL_Point thumb_size = m_artwork.size(m_normalThumbSprite);
    width = thumb_size.x;
    height = thumb_size.y;
    m_thumbWidth = static_cast<std::uint16_t>(width);
    m_trackLength = (m_right - m_left) - static_cast<std::uint16_t>(width);
}

// FUNCTION: WIZ8 0x004f5710
void W8HorizontalRangeThumb::UpdatePixelPosition()
{
    ClampPositionAndInvalidate();
}

// FUNCTION: WIZ8 0x004f5780
void W8HorizontalRangeThumb::OnLeftButtonDown(std::int32_t event)
{
    PushButtonSoundScheme(0, true);
    if (m_enabled) {
        SDL_Point cursor;
        SGPMouseGetPos(&cursor);
        std::int32_t x = cursor.x - m_pPanel->m_bounds.left - m_left;
        if (!m_hovered) {
            m_hovered = true;
            m_position = ((x - m_thumbWidth / 2) / static_cast<float>(m_trackLength)) *
                             (m_maximumPosition - m_minimumPosition) +
                         m_minimumPosition;
            ClampPositionAndInvalidate();
            if (m_listener != 0) {
                m_listener->OnDrag(this);
            }
        }
        m_dragCoordinate = x;
        m_dragging = true;
        ActivateDialogRegion(m_region);
    }
}

// FUNCTION: WIZ8 0x004f5880
void W8HorizontalRangeThumb::OnLeftButtonUp(std::int32_t event)
{
    PushButtonSoundScheme(0, true);
    if (m_enabled && m_dragging) {
        m_dragging = false;
        ClearActiveRegionIfMatches(m_region);
        if (m_listener != 0) {
            m_listener->OnDragEnd(this);
        }
    }
}

// FUNCTION: WIZ8 0x004f58c0
void W8HorizontalRangeThumb::OnMouseEnter(std::int32_t)
{
    PushButtonSoundScheme(0, true);
}

/* Retail ICF-folds these two hover hooks onto the horizontal thumb methods
   at 0x004f58c0 / 0x004f58d0. */
void W8VerticalRangeThumb::OnMouseEnter(std::int32_t)
{
    PushButtonSoundScheme(0, true);
}

void W8VerticalRangeThumb::OnMouseLeave(std::int32_t event)
{
    PushButtonSoundScheme(0, true);
    if (m_hovered && !m_dragging) {
        m_hovered = false;
        Invalidate(static_cast<std::uint8_t>(event) != 0);
    }
}

// FUNCTION: WIZ8 0x004f58d0
void W8HorizontalRangeThumb::OnMouseLeave(std::int32_t event)
{
    PushButtonSoundScheme(0, true);
    if (m_hovered && !m_dragging) {
        m_hovered = false;
        Invalidate(static_cast<std::uint8_t>(event) != 0);
    }
}

// FUNCTION: WIZ8 0x004f5940
void W8HorizontalRangeThumb::OnMouseMove(std::int32_t event)
{
    if (!m_enabled) {
        return;
    }

    SDL_Point cursor;
    SGPMouseGetPos(&cursor);
    std::int32_t x = cursor.x - m_pPanel->m_bounds.left - m_left;
    if (m_dragging) {
        if (x < 0 || m_trackLength + m_thumbWidth / 2 < x) {
            return;
        }
        std::int32_t delta = x - m_dragCoordinate;
        m_dragCoordinate = x;
        m_position +=
            (delta / static_cast<float>(m_trackLength)) * (m_maximumPosition - m_minimumPosition);
        ClampPositionAndInvalidate();
        if (m_listener != 0) {
            m_listener->OnDrag(this);
        }
        return;
    }

    bool hovered = (m_pixelPosition <= x && x <= m_pixelPosition + m_thumbWidth);
    if (m_hovered != hovered && m_pPanel != 0) {
        Invalidate(false);
    }
    m_hovered = hovered;
}

// FUNCTION: WIZ8 0x004f5a80
void W8HorizontalRangeThumb::Redraw(bool full_redraw)
{
    if (!m_active || (!full_redraw && !m_dirty)) {
        return;
    }

    std::int32_t x = m_pPanel->m_bounds.left + m_left;
    std::int32_t y = m_pPanel->m_bounds.top + m_top;
    m_artwork.drawAndInvalidate(GetFrameBufferSurface(), m_backgroundSprite, x, y);

    std::int32_t sprite;
    if (!m_enabled && m_disabledThumbSprite != -1) {
        sprite = m_disabledThumbSprite;
    } else if (m_hovered && m_hoveredThumbSprite != -1) {
        sprite = m_hoveredThumbSprite;
    } else {
        sprite = m_normalThumbSprite;
    }
    m_artwork.draw(GetFrameBufferSurface(), sprite, x + m_pixelPosition, y);
}

/* Enables or disables the whole panel: the panel's own flag, then every child's,
   and each child's region follows - mode 4 restores the disabled region and
   clearing the mode bits re-arms it. */
// FUNCTION: WIZ8 0x004f2d50
void Controls::SetEnabled(bool enable)
{
    std::int32_t index;

    m_fEnabled = enable;
    for (index = 0; index < static_cast<std::int32_t>(m_controls.size()); ++index) {
        W8Widget* control = ControlAt(index);

        control->SetActive(enable);
    }
}

void Controls::AdoptControl(std::unique_ptr<W8Widget> control)
{
    if (!control) return;
    control->SetPanel(*this);
    control.release(); // SetPanel transferred ownership to this panel.
}

// FUNCTION: WIZ8 0x004f2da0
std::unique_ptr<W8Widget> Controls::TakeControl(W8Widget* control)
{
    const auto found = std::ranges::find(m_controls, control, &std::unique_ptr<W8Widget>::get);
    if (found == m_controls.end()) return {};
    auto owned = std::move(*found);
    m_controls.erase(found);
    owned->ReleaseRegion();
    owned->m_pPanel = nullptr;
    return owned;
}

// FUNCTION: WIZ8 0x004f2df0
void Controls::DestroyAllControls()
{
    // Unlink before invoking destructors: they may destroy another child.
    while (!m_controls.empty()) {
        auto child = std::move(m_controls.back());
        m_controls.pop_back();
    }
}

/* Adds a rectangle to the panel's pending redraw. A null rectangle means the
   whole panel, and the first rectangle after a flush - recognised by a left
   edge of -1 - is copied rather than unioned. */
// FUNCTION: WIZ8 0x004f2e50
void Controls::Invalidate(const W8ControlsRect* rect)
{
    m_fDirty = true;
    if (!rect) {
        m_fWholeAreaDirty = true;
        RequestRedraw(W8_MAIN_REDRAW_FRAME);
        return;
    }

    m_fWholeAreaDirty = false;
    if (m_dirtyRect.left == -1) {
        m_dirtyRect = *rect;
    } else {
        const SDL_Rect dirty{m_dirtyRect.left, m_dirtyRect.top,
                             m_dirtyRect.right - m_dirtyRect.left,
                             m_dirtyRect.bottom - m_dirtyRect.top};
        const SDL_Rect requested{rect->left, rect->top,
                                 rect->right - rect->left, rect->bottom - rect->top};
        SDL_Rect combined{};
        SDL_GetRectUnion(&dirty, &requested, &combined);
        m_dirtyRect = {combined.x, combined.y, combined.x + combined.w,
                       combined.y + combined.h};
    }
    RequestRedraw(W8_MAIN_REDRAW_FRAME);
}

// FUNCTION: WIZ8 0x004f2f00
void Controls::InvalidateLayout()
{
    m_fLayoutDirty = true;
    RequestRedraw(W8_MAIN_REDRAW_FRAME);
}

void Controls::RedrawControls(bool full_redraw)
{
    if (full_redraw) {
        if (m_fWholeAreaDirty) {
            if (m_background) {
                m_background->invalidate(m_backgroundImage, m_bounds.left, m_bounds.top, 2);
            }
        } else {
            InvalidateRegion(m_dirtyRect.left, m_dirtyRect.top, m_dirtyRect.right,
                             m_dirtyRect.bottom, 2);
        }
        m_fDirty = false;
        m_dirtyRect.left = -1;
    }
    RedrawChildren(full_redraw);
}

/* Flushes pending panel drawing, then asks each enabled child to redraw. A
   full panel request uses the target-backed path when one exists; a bounded
   request uses the accumulated rectangle. */
// FUNCTION: WIZ8 0x004f2f10
void Controls::Redraw()
{
    bool redrawn = false;

    if (!m_fEnabled) {
        return;
    }
    if (m_fDirty) {
        if (m_background) {
            m_background->draw(GetFrameBufferSurface(), m_backgroundImage, m_bounds.left,
                               m_bounds.top);
        }
        redrawn = true;
    } else if (!m_fLayoutDirty) {
        return;
    }
    RedrawControls(redrawn);
}

/* Replaces the panel bounds and forwards each child's existing relative
   rectangle through virtual slot three so derived widgets can respond. */
// FUNCTION: WIZ8 0x004f3010
void Controls::SetBounds(std::int32_t left, std::int32_t top, std::int32_t new_right, std::int32_t new_bottom)
{
    std::int32_t index;

    m_bounds.left = left;
    m_bounds.top = top;
    m_bounds.right = new_right;
    m_bounds.bottom = new_bottom;
    for (index = 0; index < static_cast<std::int32_t>(m_controls.size()); ++index) {
        ControlAt(index)->SetBounds(ControlAt(index)->m_left, ControlAt(index)->m_top,
                                    ControlAt(index)->m_right, ControlAt(index)->m_bottom);
    }
}

// FUNCTION: WIZ8 0x004f30c0
void Controls::CreateRegionSet()
{
    if (m_regionSet == 0) {
        m_regionSet = ::CreateRegionSet();
    } else {
        ResetRegionSet(m_regionSet);
    }
}

// FUNCTION: WIZ8 0x004f4120
void W8Widget::EnableRegionHelp(std::int32_t help_text_id)
{
    if (OwnsRegion()) {
        SetRegionHelp(m_region, true, help_text_id);
    }
}

// FUNCTION: WIZ8 0x004f4140
void W8Widget::DisableRegionHelp()
{
    if (OwnsRegion()) {
        SetRegionHelp(m_region, false, -1);
    }
}

// FUNCTION: WIZ8 0x004f6950
void W8Widget::SetEnabled(bool enabled)
{
    m_enabled = enabled;
}

// FUNCTION: WIZ8 0x004f6980
void W8Widget::SetBounds(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom)
{
    m_left = left;
    m_top = top;
    m_right = right;
    m_bottom = bottom;
}

// FUNCTION: WIZ8 0x004f6960
void W8Widget::SetBoundsFromRect(const W8ControlsRect* bounds)
{
    W8Widget::SetBounds(bounds->left, bounds->top, bounds->right, bounds->bottom);
}

/* Selection controller: the listener occupies +0, the text-control pointer
   vector +0x10. Controls.cpp:2679 names m_lsButtons and checks iSelected against
   its count. The selected index is stored at +0x0c. */

// FUNCTION: WIZ8 0x004f5450
W8ControlSelection::W8ControlSelection()
    : m_value_4(0), m_value_8(0), m_selectedIndex(-1), m_selectionListener(0)
{
}

// FUNCTION: WIZ8 0x004f54b0
std::int32_t W8ControlSelection::AddEntry(W8TextControl* entry)
{
    entry->AddLayoutFlags(0x19);
    entry->m_listener = this;
    const auto index = static_cast<std::int32_t>(m_lsButtons.size());
    m_lsButtons.push_back(entry);
    return index;
}

/*
 * Moves the selection. Tells the outgoing entry it lost the selection and the
 * incoming one that it gained it, then reports the change.
 *
 * Both lookups go through the vector's bounds-checked GetAt, which returns the
 * base pointer rather than an offset one when the index is out of range - so an
 * index past the end quietly addresses element zero instead of failing. The
 * assert at the top is the only thing that would have caught it, and it does
 * not return.
 */
// FUNCTION: WIZ8 0x004f5540
void W8ControlSelection::SetSelected(std::int32_t iSelected)
{
    std::int32_t previous;

    if (iSelected >= static_cast<std::int32_t>(m_lsButtons.size())) {
        srAssertFail("iSelected < m_lsButtons.Length()",
                     "C:\\Projects\\Wizardry 8\\Local Code\\Controls.cpp", 0xa77, 0);
    }
    previous = m_selectedIndex;
    if (previous == iSelected) {
        return;
    }
    if (previous != -1) {
        m_lsButtons[previous]->DisableSecondaryState(false);
    }
    m_selectedIndex = iSelected;
    if (iSelected != -1) {
        m_lsButtons[iSelected]->EnableSecondaryState(false);
    }
    if (m_selectionListener != 0) {
        m_selectionListener->OnSelectionChanged(this, m_selectedIndex);
    }
}

// FUNCTION: WIZ8 0x004f55c0
void W8ControlSelection::OnPrimary(W8TextControl* entry)
{
    std::int32_t iIndex;

    iIndex = wiz8::index_of(m_lsButtons, entry);
    if (iIndex == -1) {
        srAssertFail("iIndex != -1", "C:\\Projects\\Wizardry 8\\Local Code\\Controls.cpp", 0xa9e,
                     0);
    }
    SetSelected(iIndex);
}

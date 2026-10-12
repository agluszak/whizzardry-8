#include "wiz8/ui_layer.h"
#include <SDL3/SDL_surface.h>
#include <tuple>
#include <SDL3/SDL_rect.h>
#include <algorithm>
#include <cassert>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>
#include <array>
#include <cstring>
#include <string>

#include "image.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_code/Widget.h"
#include "wiz8/local_code/TextControl.h"
#include "wiz8/button_interaction.h"
#include <SDL3/SDL_mouse.h>
#include "wiz8/local_code/RangeControl.h"
#include "wiz8/cursor.h"
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


unsigned char DispatchButtonRegionEvent(const InputEvent* event, W8Region* region,
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
extern const unsigned int g_W8TextBufferAlignLeft = 0x01;
// GLOBAL: WIZ8 0x005ED54C
extern const unsigned int g_W8TextBufferAlignCenter = 0x02;
// GLOBAL: WIZ8 0x005ED550
extern const unsigned int g_W8TextBufferAlignRight = 0x04;
// GLOBAL: WIZ8 0x005ED554
extern const unsigned int g_W8TextBufferAlignMiddle = 0x08;
// GLOBAL: WIZ8 0x005ED558
extern const unsigned int g_W8TextBufferAlignTop = 0x10;
// GLOBAL: WIZ8 0x005ED55C
extern const unsigned int g_W8TextBufferAlignBottom = 0x20;
// GLOBAL: WIZ8 0x005ED560
extern const unsigned int g_W8TextBufferNoWrap = 0x40;
// GLOBAL: WIZ8 0x005ED578
extern const unsigned int g_W8TextControlLayoutToggle = 0x01;
// GLOBAL: WIZ8 0x005ED57C
extern const unsigned int g_W8TextControlLayoutTextBesideImage = 0x02;
// GLOBAL: WIZ8 0x005ED580
extern const unsigned int g_W8TextControlLayoutImageLeft = 0x04;
// GLOBAL: WIZ8 0x005ED584
extern const unsigned int g_W8TextControlLayoutStayLatched = 0x08;
// GLOBAL: WIZ8 0x005ED588
extern const unsigned int g_W8TextControlLayoutLatchedImage = 0x10;
// GLOBAL: WIZ8 0x005ed594
extern const unsigned int g_W8TextControlLayoutImageAtOrigin = 0x80;

/* The default constructor. Everything the seven-argument one takes from its
   caller, this one zeroes or sets to -1. */
// FUNCTION: WIZ8 0x004f2c30
Controls::Controls()
{
    m_backgroundImage = -1;
}

// FUNCTION: WIZ8 0x004f2ca0
/* A zero right or bottom edge leaves that extent empty until the panel is
   sized, typically from its background artwork. */
Controls::Controls(int left, int top, int right_bound, int bottom_bound,
                   std::optional<CatalogArtwork> background, int background_image)
    : W8Widget(-1u, left, top, std::max(left, right_bound), std::max(top, bottom_bound)),
      m_background(std::move(background)), m_backgroundImage(background_image)
{
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
        g_region_sets[m_regionSet].enabled = 1;
    } else {
        g_region_sets[m_regionSet].enabled = 0;
    }
}

bool W8Widget::OwnsRegion() const
{
    return m_region >= 0 && static_cast<unsigned int>(m_region) < g_regions.size() &&
           g_regions[m_region].owner == this;
}

static constexpr RegionTemplate g_widget_region_template{
    W8_REGION_RECTANGLE, 0, 0, 0, 0, nullptr, 0, false, -1};

// FUNCTION: WIZ8 0x004f40f0
void W8Widget::SetActive(bool active)
{
    m_active = active;
    if (OwnsRegion()) {
        if (active) g_regions[m_region].SetInputEnabled(true);
        else g_regions[m_region].SetInputEnabled(false);
    }
}

void W8Widget::ReleaseRegion()
{
    if (OwnsRegion()) {
        UnbindWidgetRegion(static_cast<unsigned int>(m_region), this, &m_owned_region);
    }
    m_owned_region.reset();
    m_region = -1;
}

// FUNCTION: WIZ8 0x004f3f10
W8Widget::~W8Widget()
{
    ReleaseRegion();
}

/* Widget geometry is relative to its panel; a widget without a panel sits
   at the screen origin. */
SDL_Point W8Widget::PanelOrigin() const
{
    if (!m_pPanel) return {0, 0};
    const auto panel = m_pPanel->ScreenBounds();
    return {panel.x, panel.y};
}

SDL_Rect W8Widget::ScreenBounds() const
{
    const auto origin = PanelOrigin();
    return {m_rect.x + origin.x, m_rect.y + origin.y, m_rect.w, m_rect.h};
}

SDL_Point W8Widget::ToLocal(SDL_Point screen) const
{
    const auto bounds = ScreenBounds();
    return {screen.x - bounds.x, screen.y - bounds.y};
}

void W8Widget::UpdateRegionBounds()
{
    if (!OwnsRegion()) return;
    const auto bounds = ScreenBounds();
    g_regions[m_region].SetBounds(bounds.x, bounds.y, bounds.x + bounds.w, bounds.y + bounds.h);
}

// FUNCTION: WIZ8 0x004f3dd0
W8Widget::W8Widget(unsigned int region, int left, int top, int right, int bottom)
    : W8Widget()
{
    m_rect = {left, top, right - left, bottom - top};
    m_region = static_cast<int>(region);
}

// Widget regions dispatch straight to their widget. Removing or moving another
// child cannot change the event target.
// FUNCTION: WIZ8 0x004F3140
int W8Widget::HandleRegionInput(W8Region& region, const InputEvent& input)
{
    const auto reason = input.type;
    switch (reason) {
    case InputEventType::LeftButtonDown:
        region.flags |= W8_REGION_LEFT_BUTTON_HELD;
        OnLeftButtonDown(0);
        return 1;
    case InputEventType::LeftButtonUp:
        if ((region.flags & W8_REGION_LEFT_BUTTON_HELD) == 0) {
            return 1;
        }
        region.flags &= ~W8_REGION_LEFT_BUTTON_HELD;
        OnLeftButtonUp(0);
        return 1;
    case InputEventType::LeftButtonDoubleClick:
        OnLeftButtonDoubleClick(0);
        return 1;
    case InputEventType::LeftButtonRepeat:
        if ((region.flags & W8_REGION_LEFT_BUTTON_HELD) == 0) {
            return 1;
        }
        ActivatePrimary(0);
        return 1;
    case InputEventType::RightButtonDown:
        region.flags |= W8_REGION_RIGHT_BUTTON_HELD;
        OnRightButtonDown(0);
        return 1;
    case InputEventType::RightButtonUp:
        if ((region.flags & W8_REGION_RIGHT_BUTTON_HELD) == 0) {
            return 1;
        }
        region.flags &= ~W8_REGION_RIGHT_BUTTON_HELD;
        OnRightButtonUp(0);
        return 1;
    case InputEventType::RightButtonRepeat:
        if ((region.flags & W8_REGION_RIGHT_BUTTON_HELD) == 0) {
            return 1;
        }
        ActivateSecondary(0);
        return 1;
    case InputEventType::MouseMove:
        if ((region.flags & W8_REGION_MOUSE_LEAVE) != 0) {
            OnMouseLeave(0);
        } else if ((region.flags & W8_REGION_MOUSE_ENTER) != 0) {
            OnMouseEnter(0);
        } else {
            OnMouseMove(region.flags & W8_REGION_LEFT_BUTTON_HELD);
        }
        return 1;
    case InputEventType::MouseWheel:
        AdjustValue(input.wheel);
        return 1;
    default:
        break;
    }
    return 0;
}

/* Gives an adopted widget its hit region in the set its panel registers in;
   a catalog region the widget was built with is bound in place. */
void W8Widget::BindRegion(unsigned int set)
{
    const bool needs_region =
        set != 0 && (m_region == -1 || (OwnsRegion() && m_owned_region.set() != set));
    if (!needs_region) {
        if (m_region != -1) SetRegion(m_region);
        return;
    }
    // Allocate before binding, so a failure leaves no half-registered region.
    OwnedRegion allocated(set, g_widget_region_template);
    SetRegion(allocated.index());
    m_owned_region = std::move(allocated);
}

void W8Widget::UnbindRegion()
{
    ReleaseRegion();
}

/* An active hit region at the given priority, in the panel tree's set when
   the widget is in one, else in the given set. */
void W8Widget::SetRegion(unsigned int region)
{
    if (m_region != static_cast<int>(region)) ReleaseRegion();
    m_region = static_cast<int>(region);
    if (m_region == -1) return;
    auto& bound = g_regions[m_region];
    bound.owner = this;
    // A screen template's callback keeps routing the region's input; it
    // reaches this widget through the screen's own records.
    if (!bound.template_callback) bound.handler = this;
    ++bound.binding_generation;
    UpdateRegionBounds();
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
                           unsigned int layout_mode, int render_mode)
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
void W8TextBuffer::SetLayoutMode(unsigned int layout_mode)
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
    unsigned int available_width = m_layoutBounds.right - m_layoutBounds.left;
    char* line = m_buffer.data();
    unsigned int accumulated_width = 0;
    char* previous_break = 0;
    short separator_width = m_font->textWidth(const_cast<char*>(g_W8TextSeparator));

    m_lineCount = 1;
    if ((m_layoutMode & 0x40) == 0) {
        m_maxLineWidth = 0;
        size_t span = strcspn(line, g_W8TextBreakCharacters);
        char* break_at = line + span;
        while (*break_at != '\0') {
            const char separator = *break_at;
            *break_at = '\0';
            short word_width = m_font->textWidth(line);
            if (static_cast<unsigned int>(static_cast<int>(word_width) + accumulated_width) <
                available_width) {
                accumulated_width +=
                    static_cast<int>(separator_width) + static_cast<int>(word_width);
                previous_break = break_at;
            } else {
                if (previous_break != 0) {
                    *previous_break = '\n';
                }
                unsigned int completed_width =
                    accumulated_width - static_cast<int>(separator_width);
                if (m_maxLineWidth < completed_width) {
                    m_maxLineWidth = completed_width;
                }
                accumulated_width =
                    static_cast<int>(separator_width) + static_cast<int>(word_width);
                previous_break = 0;
                ++m_lineCount;
            }
            if (separator == '\n') {
                unsigned int completed_width =
                    accumulated_width - static_cast<int>(separator_width);
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
        short final_width = m_font->textWidth(line);
        unsigned int total_width = static_cast<int>(final_width) + accumulated_width;
        if (available_width <= total_width) {
            if (previous_break != 0) {
                *previous_break = '\n';
            }
            if (m_maxLineWidth < accumulated_width) {
                m_maxLineWidth = accumulated_width;
            }
            total_width = static_cast<int>(separator_width) + static_cast<int>(final_width);
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
int W8TextBuffer::GetHorizontalPosition(int width)
{
    if ((m_layoutMode & 2) != 0) {
        int inset = (m_layoutBounds.right - m_layoutBounds.left - width) / 2;
        if (inset < 0) {
            inset = 0;
        }
        return m_layoutBounds.left + inset;
    }
    if ((m_layoutMode & 4) == 0) {
        return m_layoutBounds.left;
    }
    int position = m_layoutBounds.right - width;
    if (position <= m_layoutBounds.left) {
        return m_layoutBounds.left;
    }
    return position;
}

/* Resolves the first baseline from the line count, line height and vertical
   alignment flags, including the font's own height inside a larger override. */
// FUNCTION: WIZ8 0x004f3c50
int W8TextBuffer::GetVerticalPosition()
{
    unsigned int line_height = m_lineHeight;
    unsigned int font_height = m_font->height();
    unsigned int first_line_inset;
    if (line_height == 0) {
        line_height = font_height;
        first_line_inset = 0;
    } else {
        first_line_inset = (line_height - font_height + 1) >> 1;
    }

    if ((m_layoutMode & 0x10) != 0) {
        return m_layoutBounds.top + first_line_inset;
    }
    int free_height = (m_layoutBounds.bottom - m_layoutBounds.top) - m_lineCount * line_height;
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
void W8TextBuffer::SetLineHeight(unsigned int height)
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
void W8TextBuffer::FillBounds(int colour)
{
    const SDL_Rect area{m_layoutBounds.left, m_layoutBounds.top, m_layoutBounds.right - m_layoutBounds.left,
                        m_layoutBounds.bottom - m_layoutBounds.top};
    SDL_FillSurfaceRect(&GetFrameBufferSurface(), &area, colour);
    InvalidateRegion(m_layoutBounds.left, m_layoutBounds.top, m_layoutBounds.right,
                     m_layoutBounds.bottom);
}

/* Draw each newline-delimited view without modifying the owned text. */
// FUNCTION: WIZ8 0x004f3710
void W8TextBuffer::RenderText(SDL_Surface& target, int x_offset, int y_offset, bool force)
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

        int y = GetVerticalPosition();
        for (;;) {
            const auto end = remaining.find('\n');
            const auto line = remaining.substr(0, end);

            const int x = GetHorizontalPosition(m_font->textWidth(line));
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
void W8TextBuffer::RenderToTarget(int offset, bool force, SDL_Surface& target)
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

    int y = GetVerticalPosition();
    for (;;) {
        const auto end = remaining.find('\n');
        const auto line = remaining.substr(0, end);

        const int x = GetHorizontalPosition(m_font->textWidth(line));
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
                     m_layoutBounds.bottom);
    m_geometryDirty = false;
}

/* Copies the owned text into caller storage. The caller supplies the capacity;
   the canonical method performs the same unbounded wide-string copy. */
// FUNCTION: WIZ8 0x004f3990
// FUNCTION: WIZ8 0x004f3d30
unsigned int W8TextBuffer::GetLineHeight()
{
    unsigned int height = m_lineHeight;
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
            m_pPanel->InvalidateAll();
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

void W8TextControl::SetItemImage(int item_id)
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
    int handle;

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
// Text layout needs the panel origin, so it is resolved when the panel adopts
// the control (OnGeometryChanged), not here.
W8TextControl::W8TextControl(unsigned int region, int left, int top, int right, int bottom,
                             std::optional<CatalogArtwork> image_artwork, W8SpriteStates sprites)
    : W8Widget(region, left, top, right, bottom)
{
    m_pressedSprite = sprites.pressed;
    m_alternatePressedSprite = sprites.alternate_pressed;
    m_alternateNormalSprite = sprites.alternate_normal;
    m_alternateTextEnabled = false;
    m_normalSprite = sprites.normal;
    m_disabledSprite = sprites.disabled;
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
        SetBounds({left, top, right == 0 ? m_measured_w : right - left,
                   bottom == 0 ? m_measured_h : bottom - top});
    }
    m_textBuffer.MarkGeometryDirty(10);
}

/* Where the image is drawn on screen: the widget's corner, or the image
   right- (or left-) aligned and vertically centred by its measured extent. */
// FUNCTION: WIZ8 0x004f4850
SDL_Point W8TextControl::ImageOrigin()
{
    const auto bounds = ScreenBounds();
    if (((m_measured_w == -1 || m_measured_h == -1) && !MeasureText()) ||
        (m_layoutFlags & g_W8TextControlLayoutImageAtOrigin) != 0) {
        return {bounds.x, bounds.y};
    }
    const int x = (m_layoutFlags & g_W8TextControlLayoutImageLeft) != 0
                      ? bounds.x
                      : bounds.x + bounds.w - m_measured_w;
    return {x, bounds.y + (bounds.h - m_measured_h) / 2};
}

// FUNCTION: WIZ8 0x004f4990
void W8TextControl::Redraw(SDL_Surface& target, bool full_redraw)
{
    if (!m_active || m_pPanel == 0) {
        return;
    }

    int text_state = 0;
    if (m_interaction.pressed &&
        (m_layoutFlags & g_W8TextControlLayoutTextBesideImage) == 0) {
        text_state = m_pressedTextOffset;
    }

    if (!full_redraw && !m_dirty) {
        if (m_textBuffer.HasBuffer()) {
            m_textBuffer.RenderToTarget(text_state, false, target);
        }
        return;
    }

    if (!m_imageSprites) {
        if (m_textBuffer.HasBuffer()) {
            m_textBuffer.RenderToTarget(text_state, full_redraw, target);
        }
        return;
    }

    int sprite;
    if (!m_enabled) {
        sprite = m_disabledSprite;
        if (sprite == -1) {
            if (m_textBuffer.HasBuffer()) {
                m_textBuffer.RenderToTarget(text_state, full_redraw, target);
            }
            const auto bounds = ScreenBounds();
            ShadowRect(target, {bounds.x, bounds.y, bounds.w + 1, bounds.h + 1});
            m_dirty = false;
            return;
        }
    } else if (!m_interaction.pressed) {
        sprite = m_alternateTextEnabled && m_alternateNormalSprite != -1 ? m_alternateNormalSprite
                                                                         : m_normalSprite;
    } else {
        sprite = m_alternateTextEnabled && m_alternatePressedSprite != -1 ? m_alternatePressedSprite
                                                                          : m_pressedSprite;
    }

    auto [x, y] = ImageOrigin();
    if (sprite != -1) {
        if (m_alternateTextEnabled) {
            const auto [width, height] = GetSpriteSize(*m_imageSprites, m_imageYOffset + sprite);
            int x_inset = static_cast<unsigned short>(width) - Bounds().w;
            int y_inset = static_cast<unsigned short>(height) - Bounds().h;
            if (x_inset > 0) {
                x += x_inset / 2;
            }
            if (y_inset > 0) {
                y += y_inset / 2;
            }
        }
        DrawSpriteAndInvalidate(target, *m_imageSprites,
                                m_imageYOffset + sprite, x, y);
    }

    if (m_textBuffer.HasBuffer()) {
        m_textBuffer.RenderToTarget(text_state, full_redraw, target);
    }
    m_dirty = false;
}

/* The text rectangle in panel coordinates: an explicit one from
   UpdateTextBounds, else the widget's own, less the image when the text sits
   beside it. */
W8ControlsRect W8TextControl::TextBounds()
{
    if (m_textBounds) {
        return *m_textBounds;
    }
    const auto& rect = Bounds();
    W8ControlsRect bounds{rect.x, rect.y, rect.x + rect.w, rect.y + rect.h};
    if ((m_layoutFlags & g_W8TextControlLayoutTextBesideImage) != 0 && m_imageSprites) {
        const auto [width, height] =
            GetSpriteSize(*m_imageSprites, m_imageYOffset + m_normalSprite);
        if ((m_layoutFlags & g_W8TextControlLayoutImageLeft) != 0) {
            bounds.left += 2 + static_cast<unsigned short>(width);
        } else {
            bounds.right -= 2 + static_cast<unsigned short>(width);
        }
    }
    return bounds;
}

void W8TextControl::UpdateTextLayout()
{
    if (m_pPanel == 0) {
        return;
    }
    const auto origin = PanelOrigin();
    const auto bounds = TextBounds();
    m_textBuffer.SetLayoutBounds(origin.x + bounds.left, origin.y + bounds.top,
                                 origin.x + bounds.right, origin.y + bounds.bottom);
    if (m_textBuffer.HasBuffer()) {
        m_textBuffer.UpdateLayout();
    }
    m_textBuffer.SetGeometryDirty();
}

// FUNCTION: WIZ8 0x004f44d0
void W8TextControl::OnGeometryChanged()
{
    W8Widget::OnGeometryChanged();
    if (m_circleHitArea && OwnsRegion()) {
        const auto bounds = ScreenBounds();
        auto& region = g_regions[m_region];
        region.flags = (region.flags & ~(W8_REGION_RECTANGLE | W8_REGION_CIRCLE)) |
                       W8_REGION_CIRCLE;
        region.SetBounds(bounds.x + m_circleHitArea->center.x,
                         bounds.y + m_circleHitArea->center.y, m_circleHitArea->radius, 0);
    }
    UpdateTextLayout();
}

// FUNCTION: WIZ8 0x004f4600
void W8TextControl::SetCircularHitArea(SDL_Point center, int radius)
{
    m_circleHitArea = CircleHitArea{{center.x - Bounds().x, center.y - Bounds().y}, radius};
    OnGeometryChanged();
}

// FUNCTION: WIZ8 0x004f46a0
void W8TextControl::AddLayoutFlags(unsigned int flags)
{
    m_layoutFlags |= flags;
    if ((m_layoutFlags & g_W8TextControlLayoutTextBesideImage) != 0) {
        m_textBounds.reset();
        UpdateTextLayout();
        m_textBuffer.MarkGeometryDirty(9);
    }
}

// FUNCTION: WIZ8 0x004f4780
void W8TextControl::RemoveLayoutFlags(unsigned int flags)
{
    m_layoutFlags &= ~flags;
    if ((flags & g_W8TextControlLayoutTextBesideImage) != 0) {
        m_textBounds.reset();
        UpdateTextLayout();
        m_textBuffer.MarkGeometryDirty(10);
    }
}

// FUNCTION: WIZ8 0x004f4c40
void W8TextControl::EnableSecondaryState(bool immediate)
{
    if ((m_layoutFlags & g_W8TextControlLayoutToggle) != 0 &&
        !m_interaction.selected) {
        m_interaction.pressed = true;
        m_interaction.selected = true;
        Invalidate(immediate);
    }
}

// FUNCTION: WIZ8 0x004f4cb0
void W8TextControl::DisableSecondaryState(bool immediate)
{
    if ((m_layoutFlags & g_W8TextControlLayoutToggle) != 0 &&
        m_interaction.selected) {
        m_interaction.pressed = false;
        m_interaction.selected = false;
        Invalidate(immediate);
    }
}

// FUNCTION: WIZ8 0x004f4d30
void W8TextControl::OnMouseEnter(int event)
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

    if (!m_interaction.pressed) {
        if (m_alternateNormalSprite == -1) {
            return;
        }
    } else if (m_alternatePressedSprite == -1) {
        return;
    }

    SetAlternateTextEnabled(true);
    Invalidate(static_cast<unsigned char>(event));
}

// FUNCTION: WIZ8 0x004f4e00
void W8TextControl::OnMouseLeave(int event)
{
    if (!m_active) {
        return;
    }
    if (!m_enabled) {
        PushButtonSoundScheme(0, true);
        if ((m_layoutFlags & g_W8TextControlLayoutToggle) == 0) {
            m_interaction.pressed = false;
        }
        SetAlternateTextEnabled(false);
        return;
    }
    if ((m_layoutFlags & (W8_TEXT_CONTROL_SILENT | g_W8TextControlSilentHover)) != 0) {
        PushButtonSoundScheme(0, true);
    }
    const bool was_pressed = m_interaction.pressed;
    const bool repeated = m_interaction.leave(InteractionPolicy());
    if ((m_layoutFlags & W8_TEXT_CONTROL_ACTIVATE_WHILE_HELD) != 0 && repeated)
        PushButtonSoundScheme(0, true);

    if (!was_pressed) {
        if (m_alternateNormalSprite == -1) {
            return;
        }
        SetAlternateTextEnabled(false);
        Invalidate(static_cast<unsigned char>(event));
        return;
    }

    if (m_alternatePressedSprite != -1 ||
        (m_alternateNormalSprite != -1 &&
         (m_layoutFlags & g_W8TextControlLayoutLatchedImage) != 0)) {
        SetAlternateTextEnabled(false);
        Invalidate(static_cast<unsigned char>(event));
    }
    if (m_interaction.selected) {
        return;
    }
    Invalidate(static_cast<unsigned char>(event));
}

void W8TextControl::NotifyPrimaryActivation()
{
    if (m_listener != 0) {
        m_listener->OnPrimary(this);
    }
    if (m_onPrimary != 0) {
        m_onPrimary();
    }
}

void W8TextControl::NotifySecondaryActivation()
{
    if (m_listener != 0) {
        m_listener->OnSecondary(this);
    }
    if (m_onSecondary != 0) {
        m_onSecondary();
    }
}

// FUNCTION: WIZ8 0x004f4f70
void W8TextControl::OnLeftButtonDown(int event)
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

    const bool was_pressed = m_interaction.pressed;
    m_interaction.press(SDL_BUTTON_LMASK, InteractionPolicy());
    if ((m_layoutFlags & g_W8TextControlLayoutToggle) == 0) {
        if (m_imageSprites) {
            Invalidate(static_cast<unsigned char>(event));
        }
    } else if (!was_pressed) {
        if ((m_layoutFlags & g_W8TextControlLayoutLatchedImage) == 0) {
            Invalidate(static_cast<unsigned char>(event));
        }
    }

    m_textBuffer.SetGeometryDirty();
    if (m_onLeftButtonDown != 0) {
        m_onLeftButtonDown();
    }
}

// FUNCTION: WIZ8 0x004f5070
void W8TextControl::OnRightButtonDown(int)
{
    if ((m_active && m_enabled)) {
        m_interaction.press(SDL_BUTTON_RMASK, InteractionPolicy());
        if ((m_layoutFlags & W8_TEXT_CONTROL_SILENT) != 0) {
            PushButtonSoundScheme(0, true);
        }
        if (m_onRightButtonDown != 0) {
            m_onRightButtonDown();
        }
        return;
    }
    if (!m_active && m_enabled) {
        return;
    }
    PushButtonSoundScheme(0, true);
}

// FUNCTION: WIZ8 0x004f50c0
void W8TextControl::OnLeftButtonUp(int event)
{
    if (!m_active) {
        return;
    }
    if (!m_enabled) {
        PushButtonSoundScheme(0, true);
        if ((m_layoutFlags & g_W8TextControlLayoutToggle) == 0) {
            m_interaction.pressed = false;
        }
        return;
    }
    if (!m_interaction.pressed) {
        return;
    }
    if ((m_layoutFlags & W8_TEXT_CONTROL_SILENT) != 0) {
        PushButtonSoundScheme(0, true);
    }

    const bool was_selected = m_interaction.selected;
    const auto response = m_interaction.release(SDL_BUTTON_LMASK, InteractionPolicy());
    if ((m_layoutFlags & g_W8TextControlLayoutToggle) == 0 ||
        (!was_selected && (m_layoutFlags & g_W8TextControlLayoutLatchedImage) != 0) ||
        (was_selected && (m_layoutFlags & g_W8TextControlLayoutStayLatched) == 0)) {
        Invalidate(static_cast<unsigned char>(event));
    }

    m_textBuffer.SetGeometryDirty();
    if (response.repeated_release) {
        PushButtonSoundScheme(0, true);
        return;
    }
    NotifyPrimaryActivation();
}

// FUNCTION: WIZ8 0x004f5290
void W8TextControl::OnRightButtonUp(int)
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
    const auto response = m_interaction.release(SDL_BUTTON_RMASK, InteractionPolicy());
    if (response.repeated_release) {
        PushButtonSoundScheme(0, true);
        return;
    }
    NotifySecondaryActivation();
}

// FUNCTION: WIZ8 0x004f5230
void W8TextControl::ActivatePrimary(int)
{
    if (m_active && m_enabled &&
        m_interaction.repeat(SDL_BUTTON_LMASK, InteractionPolicy()).activate) {
        if ((m_layoutFlags & W8_TEXT_CONTROL_SILENT) == 0) {
            PlayButtonSound(3);
        }
        NotifyPrimaryActivation();
    }
}

// FUNCTION: WIZ8 0x004f5310
void W8TextControl::OnLeftButtonDoubleClick(int)
{
    if (m_active && m_enabled) {
        if ((m_layoutFlags & W8_TEXT_CONTROL_SILENT) != 0) {
            PushButtonSoundScheme(0, true);
        }
        if ((m_layoutFlags & g_W8TextControlLayoutToggle) != 0 &&
            !m_interaction.selected) {
            return;
        }
        if (m_onDoubleClick != 0) {
            m_onDoubleClick();
        }
        return;
    }
    if (!m_active && m_enabled) {
        return;
    }
    PushButtonSoundScheme(0, true);
}

// FUNCTION: WIZ8 0x004f5360
void W8TextControl::ActivateSecondary(int)
{
    if (m_active && m_enabled &&
        m_interaction.repeat(SDL_BUTTON_RMASK, InteractionPolicy()).activate) {
        if ((m_layoutFlags & W8_TEXT_CONTROL_SILENT) == 0) {
            PlayButtonSound(3);
        }
        NotifySecondaryActivation();
    }
}

// FUNCTION: WIZ8 0x004f53b0
void W8TextControl::UpdateTextBounds(int left, int top, int right, int bottom)
{
    m_textBounds = W8ControlsRect{left, top, right, bottom};
    UpdateTextLayout();
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
W8HelpTextControl::W8HelpTextControl(unsigned int region, int left, int top, int right,
                                     int bottom)
    : W8TextControl(region, left, top, right, bottom)
{
    m_regionHelp.clear();
}

W8RangeControl::W8RangeControl(int left, int top, int right, int bottom)
{
    for (int part = 0; part < 3; ++part)
        m_targets[part] = &m_input.add(SDL_Rect{0, 0, 1, 1},
            [this, part](const UiPointerEvent& event) { return HandlePointer(part, event); });
    SetBounds({left, top, right - left, bottom - top});
    SetActive(false);
}

void W8RangeControl::SetBounds(SDL_Rect bounds)
{
    m_bounds = bounds;
    const auto thumb = m_artwork.size(4);
    m_thumb_height = thumb.y;
    m_track = {bounds.x, bounds.y + 16, std::max(16, thumb.x), bounds.h - 32};
    m_travel = std::max(1, m_track.h - thumb.y);
    m_targets[0]->setShape(SDL_Rect{bounds.x, bounds.y, 17, 17});
    m_targets[1]->setShape(SDL_Rect{bounds.x, bounds.y + bounds.h - 16, 17, 17});
    m_targets[2]->setShape(SDL_Rect{m_track.x, m_track.y, m_track.w + 1, m_track.h + 1});
    m_dirty = true;
}

void W8RangeControl::SetRange(int first, int second)
{
    m_minimum = std::min(first, second);
    m_maximum = std::max(first, second);
    SetValue(m_value);
}

void W8RangeControl::SetValue(int value)
{
    m_value = std::clamp(value, m_minimum, m_maximum);
    m_position = m_value == m_minimum ? 0.f : m_value == m_maximum ? 1.f :
        (m_value - m_minimum + .5f) / (m_maximum - m_minimum + 1);
    m_dirty = true;
}

void W8RangeControl::AdjustValue(int steps)
{
    if (!m_enabled) return;
    // Notify each step: existing controllers may update the range in response.
    while (steps != 0) {
        const int direction = steps > 0 ? 1 : -1;
        SetValue(m_value - direction);
        if (on_change) on_change(this);
        steps -= direction;
    }
}

void W8RangeControl::SetEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!enabled) {
        m_dragging = m_hovered = false;
        for (auto* target : m_targets) target->releaseCapture();
        for (auto& arrow : m_arrows) arrow.cancel(false);
    }
    m_dirty = true;
}

void W8RangeControl::SetActive(bool active)
{
    m_active = active;
    if (!active) {
        m_dragging = m_hovered = false;
        for (auto& arrow : m_arrows) arrow.cancel(false);
    }
    m_input.setEnabled(active);
    m_dirty = true;
}

UiResult W8RangeControl::HandlePointer(int part, const UiPointerEvent& event)
{
    m_dirty = true;
    if (!m_enabled || part == 2) PushButtonSoundScheme(0, true);
    if (!m_enabled) return UiResult::Handled;
    if (event.action == UiPointerAction::Wheel) {
        AdjustValue(event.wheel);
        return UiResult::Handled;
    }
    if (part < 2) {
        const ButtonPolicy policy{.activation = ButtonActivation::Press, .repeat = true,
                                  .controller_activation = true};
        auto& press = m_arrows[part];
        if (event.action == UiPointerAction::Leave) press.leave(policy);
        if (event.button != UiPointerButton::Left) return UiResult::Handled;
        bool activate = false;
        if (event.action == UiPointerAction::Down)
            activate = press.press(SDL_BUTTON_LMASK, policy).activate;
        else if (event.action == UiPointerAction::Repeat)
            activate = press.repeat(SDL_BUTTON_LMASK, policy).activate;
        else if (event.action == UiPointerAction::Up) press.release(SDL_BUTTON_LMASK, policy);
        if (activate) AdjustValue(part == 0 ? 1 : -1);
        return UiResult::Handled;
    }
    const int y = event.position.y - g_cursor_hotspot_y - m_track.y;
    bool changed = false;
    if (event.action == UiPointerAction::Down && event.button == UiPointerButton::Left) {
        if (!m_hovered) {
            m_position = (y - m_thumb_height / 2) / static_cast<float>(m_travel);
            changed = true;
        }
        m_hovered = m_dragging = true;
        m_drag_y = y;
    } else if (event.action == UiPointerAction::Up && event.button == UiPointerButton::Left) {
        m_dragging = false;
        m_targets[2]->releaseCapture();
    } else if (event.action == UiPointerAction::Leave && !m_dragging) m_hovered = false;
    else if (event.action == UiPointerAction::Move) {
        if (m_dragging) {
            if (y <= m_thumb_height / 2) m_position = 0;
            else if (y >= m_travel + m_thumb_height / 2) m_position = 1;
            else {
                m_position += (y - m_drag_y) / static_cast<float>(m_travel);
                m_drag_y = y;
            }
            changed = true;
        } else {
            const int pixel = static_cast<int>(m_position * m_travel);
            m_hovered = pixel <= y && y <= pixel + m_thumb_height;
        }
    }
    if (changed) {
        m_position = std::clamp(m_position, 0.f, 1.f);
        const int value = std::min(m_maximum, m_minimum +
            static_cast<int>(m_position * (m_maximum - m_minimum + 1)));
        if (value != m_value) {
            m_value = value;
            if (on_change) on_change(this);
        }
    }
    return UiResult::Handled;
}

void W8RangeControl::Redraw(SDL_Surface& target, bool full_redraw)
{
    if (!m_active || (!full_redraw && !m_dirty)) return;
    m_dirty = false;
    for (int part = 0; part < 2; ++part) {
        const int frame = part * 8 + (!m_enabled ? 3 : m_arrows[part].pressed ? 2 :
                                    m_targets[part]->hovered() ? 1 : 0);
        const auto size = m_artwork.size(frame);
        m_artwork.drawAndInvalidate(target, frame, m_bounds.x + 16 - size.x,
            m_bounds.y + (part ? m_bounds.h - 16 : 0) + (16 - size.y) / 2);
    }
    SDL_FillSurfaceRect(&target, &m_track, 0x8000);
    InvalidateRegion(m_track.x, m_track.y, m_track.x + m_track.w, m_track.y + m_track.h);
    if (m_enabled) m_artwork.draw(target, m_hovered ? 5 : 4,
        m_track.x + m_track.w - m_artwork.size(4).x,
        m_track.y + static_cast<int>(m_position * m_travel));
}

// FUNCTION: WIZ8 0x004f6680
void W8HelpTextControl::SetRegionHelp(std::string_view text)
{
    if (!text.empty()) {
        m_regionHelp = text;
    }
}

// FUNCTION: WIZ8 0x004f66b0
void W8HelpTextControl::OnMouseEnter(int event)
{
    PushButtonSoundScheme(0, true);
    W8TextControl::OnMouseEnter(event);
    if (m_regionHelp.size() > 1 && m_region != -1) {
        ::SetRegionHelpText(m_regionHelp);
        g_regions[m_region].help_enabled = true;
        return;
    }
    if (m_region != -1) {
        g_regions[m_region].help_enabled = false;
    }
}

// FUNCTION: WIZ8 0x005b7cb0
void W8HelpTextControl::OnLeftButtonDown(int event)
{
    PushButtonSoundScheme(0, true);
    W8TextControl::OnLeftButtonDown(event);
}

// FUNCTION: WIZ8 0x004f6720
void W8HelpTextControl::OnRightButtonDown(int event)
{
    if (m_onSecondary == 0) {
        PushButtonSoundScheme(0, true);
    }
    W8TextControl::OnRightButtonDown(event);
}

// FUNCTION: WIZ8 0x005b7cd0
void W8HelpTextControl::OnLeftButtonUp(int event)
{
    PushButtonSoundScheme(0, true);
    W8TextControl::OnLeftButtonUp(event);
}

// FUNCTION: WIZ8 0x004f6780
void W8HelpTextControl::OnRightButtonUp(int event)
{
    if (m_onSecondary == 0) {
        PushButtonSoundScheme(0, true);
    }
    W8TextControl::OnRightButtonUp(event);
}

// FUNCTION: WIZ8 0x004f6810
void W8HelpTextControl::OnLeftButtonDoubleClick(int event)
{
    PushButtonSoundScheme(0, true);
    W8TextControl::OnLeftButtonDoubleClick(event);
}

/* Enables or disables the whole panel: the panel's own flag, then every child's,
   and each child's region follows - mode 4 restores the disabled region and
   clearing the mode bits re-arms it. */
// FUNCTION: WIZ8 0x004f2d50
void Controls::SetActive(bool enable)
{
    int index;

    W8Widget::SetActive(enable);
    for (index = 0; index < static_cast<int>(m_controls.size()); ++index) {
        W8Widget* control = ControlAt(index);

        control->SetActive(enable);
    }
}

void Controls::AddControl(std::unique_ptr<W8Widget> control)
{
    if (!control) {
        return;
    }
    // A widget must not already belong to another panel.
    assert(control->m_pPanel == nullptr);

    // Reserve before preparation so committing ownership cannot allocate.
    if (m_controls.size() == m_controls.capacity()) {
        m_controls.reserve(m_controls.empty() ? 1 : m_controls.size() * 2);
    }
    control->m_pPanel = this;
    try {
        control->BindRegion(ChildRegionSet());
        // Layout needs the parent, but ownership is committed only after it succeeds.
        control->OnGeometryChanged();
        control->SetActive(m_active);
        control->m_dirty = true;
    } catch (...) {
        control->UnbindRegion();
        control->m_pPanel = nullptr;
        throw;
    }
    m_controls.push_back(std::move(control));
    InvalidateLayout();
}

// FUNCTION: WIZ8 0x004f2da0
std::unique_ptr<W8Widget> Controls::TakeControl(W8Widget* control)
{
    const auto found = std::ranges::find(m_controls, control, &std::unique_ptr<W8Widget>::get);
    if (found == m_controls.end()) return {};
    auto owned = std::move(*found);
    m_controls.erase(found);
    owned->UnbindRegion();
    owned->m_pPanel = nullptr;
    return owned;
}

void Controls::RemoveControl(W8Widget* control)
{
    auto removed = TakeControl(control);
}

// FUNCTION: WIZ8 0x004f2df0
void Controls::DestroyAllControls()
{
    // Unlink before invoking destructors: they may destroy another child.
    while (!m_controls.empty()) {
        RemoveControl(m_controls.back().get());
    }
}

/* Pending panel repaint: the whole panel, or the union of the requested
   areas. A bounded request never narrows a pending whole-panel one. */
// FUNCTION: WIZ8 0x004f2e50
void Controls::InvalidateAll()
{
    m_dirtyRect.reset();
    m_dirty = true;
    if (m_pPanel) m_pPanel->InvalidateLayout();
    RequestRedraw(W8_MAIN_REDRAW_FRAME);
}

void Controls::InvalidateArea(const W8ControlsRect& rect)
{
    if (!m_dirty) {
        m_dirtyRect = rect;
    } else if (m_dirtyRect) {
        const SDL_Rect dirty = ToSDLRect(*m_dirtyRect);
        const SDL_Rect requested = ToSDLRect(rect);
        SDL_Rect combined{};
        SDL_GetRectUnion(&dirty, &requested, &combined);
        m_dirtyRect = W8ControlsRect{combined.x, combined.y, combined.x + combined.w,
                                     combined.y + combined.h};
    }
    m_dirty = true;
    if (m_pPanel) m_pPanel->InvalidateLayout();
    RequestRedraw(W8_MAIN_REDRAW_FRAME);
}

// FUNCTION: WIZ8 0x004f2f00
/* Some child needs repainting; ancestors must visit this panel to reach it. */
void Controls::InvalidateLayout()
{
    m_childrenDirty = true;
    if (m_pPanel) m_pPanel->InvalidateLayout();
    RequestRedraw(W8_MAIN_REDRAW_FRAME);
}

void Controls::RedrawControls(SDL_Surface& target, bool full_redraw)
{
    if (full_redraw) {
        if (!m_dirtyRect) {
            if (m_background) {
                m_background->invalidate(m_backgroundImage, ScreenBounds().x, ScreenBounds().y);
            }
        } else {
            InvalidateRegion(m_dirtyRect->left, m_dirtyRect->top, m_dirtyRect->right,
                             m_dirtyRect->bottom);
        }
        m_dirty = false;
        m_dirtyRect.reset();
    }
    RedrawChildren(target, full_redraw);
}

/* Flushes pending panel drawing, then asks each enabled child to redraw. A
   full panel request uses the target-backed path when one exists; a bounded
   request uses the accumulated rectangle. A forced redraw (from a parent
   repainting over this panel) repaints all of it. */
// FUNCTION: WIZ8 0x004f2f10
void Controls::Redraw(SDL_Surface& target, bool full_redraw)
{
    bool redrawn = false;

    if (!m_active) {
        return;
    }
    if (full_redraw) {
        m_dirty = true;
        m_dirtyRect.reset();
    }
    if (m_dirty) {
        if (m_background) {
            m_background->draw(target, m_backgroundImage, ScreenBounds().x,
                               ScreenBounds().y);
        }
        redrawn = true;
    } else if (!m_childrenDirty) {
        return;
    }
    RedrawControls(target, redrawn);
}

/* Children keep their relative rectangles and re-derive their screen
   geometry from the panel's new origin. */
// FUNCTION: WIZ8 0x004f3010
void Controls::OnGeometryChanged()
{
    W8Widget::OnGeometryChanged();
    for (const auto& control : m_controls) {
        control->OnGeometryChanged();
    }
}

unsigned int Controls::ChildRegionSet() const
{
    if (m_regionSet != 0) return m_regionSet;
    return m_pPanel ? m_pPanel->ChildRegionSet() : 0;
}

/* A panel is never a hit target itself. Without a set of its own, its
   children register in (and leave) the set of the panel it joins. */
void Controls::BindRegion(unsigned int set)
{
    if (m_regionSet != 0) set = m_regionSet;
    for (const auto& control : m_controls) {
        control->BindRegion(set);
    }
}

void Controls::UnbindRegion()
{
    for (const auto& control : m_controls) {
        control->UnbindRegion();
    }
}

// FUNCTION: WIZ8 0x004f30c0
void Controls::CreateRegionSet()
{
    // Existing descendants must leave their old bindings before the set resets.
    // This also migrates children that previously inherited an ancestor's set.
    const auto set = m_regionSet != 0 ? m_regionSet : ::CreateRegionSet();
    UnbindRegion();
    m_regionSet = set;
    ResetRegionSet(m_regionSet);
    BindRegion(m_regionSet);
}

// FUNCTION: WIZ8 0x004f4120
void W8Widget::EnableRegionHelp(int help_text_id)
{
    if (OwnsRegion()) {
        g_regions[m_region].SetHelp(true, help_text_id);
    }
}

// FUNCTION: WIZ8 0x004f4140
void W8Widget::DisableRegionHelp()
{
    if (OwnsRegion()) {
        g_regions[m_region].SetHelp(false, -1);
    }
}

// FUNCTION: WIZ8 0x004f6950
void W8Widget::SetEnabled(bool enabled)
{
    m_enabled = enabled;
}

// FUNCTION: WIZ8 0x004f6980
void W8Widget::SetBounds(const SDL_Rect& bounds)
{
    m_rect = bounds;
    OnGeometryChanged();
}

void W8Widget::OnGeometryChanged()
{
    UpdateRegionBounds();
}

void W8ControlSelection::AddEntry(W8TextControl* entry)
{
    entry->AddLayoutFlags(g_W8TextControlLayoutToggle | g_W8TextControlLayoutStayLatched |
                          g_W8TextControlLayoutLatchedImage);
    const auto index = static_cast<int>(m_lsButtons.size());
    entry->m_onPrimary = [this, index] { SetSelected(index); };
    m_lsButtons.push_back(entry);
}

void W8ControlSelection::SetSelected(int selected, bool notify)
{
    if (selected >= static_cast<int>(m_lsButtons.size()))
        srAssertFail("selected < m_lsButtons.size()", __FILE__, __LINE__, 0);
    if (selected == m_selectedIndex) return;
    if (m_selectedIndex != -1) m_lsButtons[m_selectedIndex]->DisableSecondaryState(false);
    m_selectedIndex = selected;
    if (selected != -1) m_lsButtons[selected]->EnableSecondaryState(false);
    if (notify && on_selection) on_selection(selected);
}

ButtonPolicy W8TextControl::InteractionPolicy() const
{
    ButtonToggle toggle = ButtonToggle::None;
    if (m_layoutFlags & g_W8TextControlLayoutToggle)
        toggle = (m_layoutFlags & g_W8TextControlLayoutStayLatched)
                     ? ButtonToggle::StayLatched : ButtonToggle::Release;
    return {.primary_toggle = toggle,
            .repeat = (m_layoutFlags & W8_TEXT_CONTROL_ACTIVATE_WHILE_HELD) != 0,
            .controller_activation = true};
}

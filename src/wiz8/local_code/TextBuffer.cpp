#include "wiz8/local_code/TextBuffer.h"
#include "Font.h"
#include "image.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/float_constants.h"
#include "wiz8/fonts.h"
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>

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
char g_W8TextSeparator[] = " ";
static char g_W8TextBreakCharacters[] = " \n";
const float g_float_one = 1.0f;
const float g_float_half = 0.5f;
const float g_float_thirty_five_hundredths = 0.35f;

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

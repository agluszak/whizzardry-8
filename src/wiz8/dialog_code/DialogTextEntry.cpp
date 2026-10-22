#include "wiz8/dialog_code/DialogTextEntry.h"

#include <array>
#include <string>
#include <string_view>

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>

#include "wiz8/canvas/Font.h"
#include "wiz8/canvas/image.h"
#include "wiz8/checked_integer.h"
#include "wiz8/fonts.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/render/frame.h"

W8DialogTextEntry::W8DialogTextEntry(std::string_view prefix, std::string_view text,
                                     unsigned int prefix_palette, unsigned int text_palette,
                                     const W8ControlsRect* bounds, Font& font)
{
    m_prefix_palette = prefix_palette;
    m_font = &font;
    m_lineCount = 0;
    m_geometryDirty = true;
    m_text_palette = text_palette;
    SetClipBounds(*bounds);
    SetLayoutBounds(bounds, true);
    SetLayoutMode(g_W8TextBufferAlignMiddle | g_W8TextBufferAlignLeft);
    m_prefix_length = prefix.empty() ? 0 : wiz8::checked_integer<int>(prefix.size() + 2);
    m_text = prefix.empty() ? std::string(text) : fmt::format("{}: {}", prefix, text);
    UpdateLayout();
}

int W8DialogTextEntry::DrawLine(SDL_Surface& target, std::string_view line, int prefix_remaining,
                                int y)
{
    int x = GetHorizontalPosition(m_font->textWidth(line));
    if (prefix_remaining > 0) {
        if (prefix_remaining < static_cast<int>(line.size())) {
            const auto prefix = line.substr(0, prefix_remaining);
            DrawText(target, *m_font, {x, y}, prefix);
            x += m_font->textWidth(prefix);
            m_font->setPalette(m_text_palette < 15 ? g_font_state_palettes[m_text_palette]
                                                   : g_wiz_text_font_secondary.glyphs->colors);
            line.remove_prefix(prefix_remaining);
        }
        prefix_remaining -= line.size();
    }
    DrawText(target, *m_font, {x, y}, line);
    return prefix_remaining;
}

void W8DialogTextEntry::Paint(SDL_Surface& target)
{
    int prefix_remaining = m_prefix_length;
    if (m_buffer.empty()) {
        return;
    }
    std::string_view remaining = m_buffer;
    auto palette = g_wiz_text_font_secondary.glyphs->colors;
    if (m_prefix_length == 0 && m_text_palette < 15)
        palette = g_font_state_palettes[m_text_palette];
    else if (m_prefix_length != 0 && m_prefix_palette < 15)
        palette = g_font_state_palettes[m_prefix_palette];
    const auto previous_palette = m_font->palette();
    m_font->setPalette(palette);
    SDL_Rect clip{m_clipBounds.left, m_clipBounds.top, m_clipBounds.right - m_clipBounds.left,
                  m_clipBounds.bottom - m_clipBounds.top};
    ScopedClipRect clipping(target, clip);
    int y = GetVerticalPosition();
    for (;;) {
        const auto end = remaining.find('\n');
        prefix_remaining = DrawLine(target, remaining.substr(0, end), prefix_remaining, y);
        if (end == std::string_view::npos) break;
        y += GetLineHeight();
        if (m_layoutBounds.bottom <= y) break;
        remaining.remove_prefix(end + 1);
    }
    m_font->setPalette(previous_palette);
}

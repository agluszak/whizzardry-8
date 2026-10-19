#include "wiz8/dialog_code/DialogTextEntry.h"

#include <array>
#include <string>
#include <string_view>

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>

#include "Font.h"
#include "himage.h"
#include "image.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/fonts.h"
#include "wiz8/layouts/npc_dialogue.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"

/* Retail initializer 0x005D1010 copies the Controls layout constant. */
// GLOBAL: WIZ8 0x0069c5d0
unsigned int g_dialog_text_layout_mask = g_W8TextBufferNoWrap;

// FUNCTION: WIZ8 0x005d1050
W8DialogTextEntry::W8DialogTextEntry(std::string_view prefix, std::string_view text,
                                     unsigned int prefix_palette, unsigned int text_palette,
                                     const W8ControlsRect* bounds, Font& font,
                                     W8DialogueCategory category, unsigned int layout_mode,
                                     unsigned char shorten)
{
    m_prefix_palette = prefix_palette;
    m_font = &font;
    m_lineCount = 0;
    m_geometryDirty = true;
    m_text_palette = text_palette;
    m_marked = false;
    m_category = category;
    SetClipBounds(*bounds);
    SetLayoutBounds(bounds, true);
    SetLayoutMode(layout_mode);
    m_shorten_mask = shorten;
    m_prefix_length = prefix.empty() ? 0 : prefix.size() + 2;
    m_text = prefix.empty() ? std::string(text) : fmt::format("{}: {}", prefix, text);
    UpdateLayout();
}

int W8DialogTextEntry::DrawLine(SDL_Surface& target, std::string_view line, int prefix_remaining,
                                int y, bool selected)
{
    int x = GetHorizontalPosition(m_font->textWidth(line));
    if (prefix_remaining > 0) {
        if (prefix_remaining < static_cast<int>(line.size())) {
            const auto prefix = line.substr(0, prefix_remaining);
            DrawText(target, *m_font, {x, y}, prefix);
            x += m_font->textWidth(prefix);
            if (!selected) {
                m_font->setPalette(m_text_palette < 15 ? g_font_state_palettes[m_text_palette]
                                                       : g_wiz_text_font_secondary.glyphs->colors);
            }
            line.remove_prefix(prefix_remaining);
        }
        prefix_remaining -= line.size();
    }
    DrawText(target, *m_font, {x, y}, line);
    return prefix_remaining;
}

// FUNCTION: WIZ8 0x005d1170

void W8DialogTextEntry::Paint(SDL_Surface& target, bool selected, bool highlighted)
{
    int width = m_layoutBounds.right - m_layoutBounds.left;
    int prefix_remaining = m_prefix_length;
    if (m_buffer.empty()) {
        return;
    }
    std::string shortened;
    std::string_view remaining = m_buffer;
    if (m_shorten_mask) {
        shortened = ShortenTextToWidth(m_buffer, width - 5, *m_font);
        remaining = shortened;
    }
    Palette16 palette = g_font_state_palettes[W8_FONT_PALETTE_YELLOW];
    if (!highlighted) {
        if (m_marked) {
            palette = g_font_state_palettes[W8_FONT_PALETTE_GREEN];
        } else if (selected) {
            palette = g_font_state_palettes[W8_FONT_PALETTE_WHITE];
        } else {
            palette = g_wiz_text_font_secondary.glyphs->colors;
            if (m_prefix_length == 0) {
                if (m_text_palette < 15) {
                    palette = g_font_state_palettes[m_text_palette];
                }
            } else if (m_prefix_palette < 15) {
                palette = g_font_state_palettes[m_prefix_palette];
            }
        }
    }
    const auto previous_palette = m_font->palette();
    m_font->setPalette(palette);
    SDL_Rect clip{m_clipBounds.left, m_clipBounds.top, m_clipBounds.right - m_clipBounds.left,
                  m_clipBounds.bottom - m_clipBounds.top};
    ScopedClipRect clipping(target, clip);
    int y = GetVerticalPosition();
    for (;;) {
        const auto end = remaining.find('\n');
        prefix_remaining =
            DrawLine(target, remaining.substr(0, end), prefix_remaining, y, selected);
        if (end == std::string_view::npos) break;
        y += GetLineHeight();
        if (m_layoutBounds.bottom <= y) break;
        remaining.remove_prefix(end + 1);
    }
    m_font->setPalette(previous_palette);
}

// FUNCTION: WIZ8 0x005d14b0

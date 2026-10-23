#include "wiz8/dialog_code/DialogTextArea.h"

#include <memory>
#include <string_view>
#include <utility>

#include <SDL3/SDL_surface.h>

#include "wiz8/canvas/Font.h"
#include "wiz8/local_code/ControlsRect.h"

void W8DialogTextArea::Configure(const W8ControlsRect* bounds, Font& font)
{
    m_bounds = *bounds;
    m_font = &font;
    for (const auto& entry : m_entries) entry->SetClipBounds(m_bounds);
    m_dirty = true;
}

bool W8DialogTextArea::TakeDirty()
{
    return std::exchange(m_dirty, false);
}

void W8DialogTextArea::Paint(SDL_Surface& target)
{
    const int height = GetLineHeight();
    auto bounds = m_bounds;
    bounds.top -= m_first_visible_line * height;
    for (const auto& entry : m_entries) {
        bounds.bottom = bounds.top + entry->m_lineCount * height;
        entry->SetLayoutBounds(&bounds, false);
        entry->Paint(target);
        bounds.top = bounds.bottom + m_entry_spacing_lines * height;
    }
}

void W8DialogTextArea::SetFirstVisibleLine(int line)
{
    if (line >= 0 && line < GetTotalLineCount() && line != m_first_visible_line) {
        m_first_visible_line = line;
        m_dirty = true;
    }
}

int W8DialogTextArea::GetTotalLineCount()
{
    int total = 0;
    for (const auto& entry : m_entries) total += entry->m_lineCount + m_entry_spacing_lines;
    return m_entries.empty() ? 0 : total - m_entry_spacing_lines;
}

unsigned int W8DialogTextArea::GetLineHeight()
{
    return m_font->height();
}

void W8DialogTextArea::SetEntrySpacing(int lines)
{
    m_entry_spacing_lines = lines;
    m_dirty = true;
}

void W8DialogTextArea::AddEntry(std::string_view prefix, std::string_view text,
                                unsigned int prefix_palette, unsigned int text_palette)
{
    m_entries.push_back(std::make_unique<W8DialogTextEntry>(prefix, text, prefix_palette,
                                                            text_palette, &m_bounds, *m_font));
    m_first_visible_line = 0;
    m_dirty = true;
}

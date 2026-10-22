#include "wiz8/dialog_code/DialogTextArea.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include <SDL3/SDL_surface.h>

#include "dialog_code/DialogTextEntry.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/layouts/npc_dialogue.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/unicode.h"

/* Reconstructed logical owner; original translation-unit identity is unproven.
   Live query: 0x005D14D0/0x005D1640 sit in the gap between DialogInterface.cpp
   (upper 0x005CF580) and stMessageDialog.cpp (lower 0x005D2800). Retail emits a second one-slot table at 0x005EF89C
   holding the duplicate sdd emission 0x005D2560; the compared table at
   0x005EF898 (marked in the header) holds 0x005D2590, so only that copy
   pairs to the recompiled sdd. */

/* Behavior masks for contained text entries: single-line layout, pointer
   selection and shortening to the available width. */
extern const unsigned int g_W8DialogTextAreaNoWrap = 0x01;
extern const unsigned int g_W8DialogTextAreaPointSelection = 0x02;
extern const unsigned int g_W8DialogTextAreaShortenText = 0x04;

void W8DialogTextArea::SetFirstVisibleEntry(unsigned int index)
{
    if (!m_all_lines.empty() && index < m_visible_lines.size() &&
        (static_cast<unsigned int>(m_first_visible_entry) != index || m_first_visible_line != 0)) {
        m_first_visible_entry = index;
        m_first_visible_line = 0;
        m_relayout_needed = true;
        m_dirty = true;
    }
}

void W8DialogTextArea::Configure(const W8ControlsRect* bounds, Font& font, unsigned int flags)
{
    m_bounds = *bounds;
    m_dirty = m_relayout_needed = true;
    m_behavior_flags = flags;
    m_font = &font;
    for (const auto& entry : m_all_lines) {
        entry->m_clipBounds = m_bounds;
    }
}

bool W8DialogTextArea::TakeDirty()
{
    return std::exchange(m_dirty, false);
}

void W8DialogTextArea::Paint(SDL_Surface& target)
{
    unsigned int font_height = GetLineHeight();
    {
        W8ControlsRect bounds;
        if (m_relayout_needed) {
            bounds.left = m_bounds.left;
            bounds.right = m_bounds.right;
            bounds.top = m_bounds.top - m_first_visible_line * font_height;
        }
        for (int index = m_first_visible_entry; index < static_cast<int>(m_visible_lines.size());
             ++index) {
            if (m_relayout_needed) {
                unsigned int height = GetLineHeight();
                bounds.bottom = bounds.top + VisibleEntry(index)->m_lineCount * height;
                VisibleEntry(index)->SetLayoutBounds(&bounds, false);
                bounds.top = bounds.bottom + m_entry_spacing_lines * font_height;
            }
            VisibleEntry(index)->Paint(target, index == m_selected_visible_entry,
                                       index == m_highlighted_entry);
        }
        m_relayout_needed = false;
    }
}

void W8DialogTextArea::SetFirstVisibleLine(int requested_line)
{
    if (requested_line < 0) return;
    for (unsigned int index = 0; index < m_visible_lines.size(); ++index) {
        const int lines = VisibleEntry(index)->m_lineCount + m_entry_spacing_lines;
        if (requested_line < lines) {
            if (m_first_visible_entry != static_cast<int>(index) ||
                m_first_visible_line != requested_line) {
                m_first_visible_entry = index;
                m_first_visible_line = requested_line;
                m_dirty = m_relayout_needed = true;
            }
            return;
        }
        requested_line -= lines;
    }
}

int W8DialogTextArea::GetTotalLineCount()
{
    int total = 0;
    for (int index : m_visible_lines)
        total += m_all_lines[index]->m_lineCount + m_entry_spacing_lines;
    return m_visible_lines.empty() ? 0 : total - m_entry_spacing_lines;
}

unsigned int W8DialogTextArea::GetLineHeight()
{
    if (!m_font) return static_cast<unsigned int>(-1);
    unsigned int height = m_line_height_override;
    if (height == static_cast<unsigned int>(-1)) height = m_font->height();
    return height;
}

void W8DialogTextArea::SetEntrySpacing(int lines)
{
    if (m_font) {
        m_dirty = m_relayout_needed = true;
        m_entry_spacing_lines = lines;
    }
}

void W8DialogTextArea::SetLineHeight(unsigned int height)
{
    m_line_height_override = height;
    m_relayout_needed = true;
    m_dirty = true;
    for (auto& entry : m_all_lines) entry->SetLineHeight(height);
}

bool W8DialogTextArea::SelectEntry(int index)
{
    if (index < 0 || index >= static_cast<int>(m_visible_lines.size()) ||
        index == m_selected_visible_entry)
        return false;
    m_selected_visible_entry = index;
    m_dirty = true;
    return true;
}

bool W8DialogTextArea::ClearSelection()
{
    if (std::exchange(m_selected_visible_entry, -1) == -1) return false;
    m_dirty = true;
    return true;
}

std::optional<std::string_view> W8DialogTextArea::GetEntryText(unsigned int index) const
{
    if (index >= m_all_lines.size()) return std::nullopt;
    return m_all_lines[index]->GetText();
}

/* Hit testing and selection share the visible-line mapping. Only the first
   entry can start partway through its wrapped lines. */
unsigned int W8DialogTextArea::FindVisibleEntryAtLine(int target)
{
    if (target < 0) return static_cast<unsigned int>(-1);
    target += m_first_visible_line;
    for (unsigned int index = m_first_visible_entry; index < m_visible_lines.size(); ++index) {
        const int lines = VisibleEntry(index)->m_lineCount + m_entry_spacing_lines;
        if (target < lines) return index;
        target -= lines;
    }
    return static_cast<unsigned int>(-1);
}

unsigned int W8DialogTextArea::HitTestEntry(int, int y)
{
    int line_height = GetLineHeight();
    int target = (y - m_bounds.top) / line_height;
    return FindVisibleEntryAtLine(target);
}

bool W8DialogTextArea::UpdateSelectionFromPoint(int x, int y)
{
    if (!(m_behavior_flags & g_W8DialogTextAreaPointSelection)) return false;
    const unsigned int index = HitTestEntry(x, y);
    return index == static_cast<unsigned int>(-1) ? ClearSelection() : SelectEntry(index);
}

bool W8DialogTextArea::ClearPointSelection()
{
    return (m_behavior_flags & g_W8DialogTextAreaPointSelection) ? ClearSelection() : 0;
}

std::optional<std::string_view> W8DialogTextArea::GetVisibleEntryText(unsigned int index) const
{
    if (index >= m_visible_lines.size()) return std::nullopt;
    return VisibleEntry(index)->GetText();
}

bool W8DialogTextArea::HighlightVisibleEntry(int index)
{
    if (index < 0 || index >= static_cast<int>(m_visible_lines.size()) ||
        index == m_highlighted_entry)
        return false;
    m_highlighted_entry = index;
    m_dirty = true;
    return true;
}

bool W8DialogTextArea::ClearEntryHighlight()
{
    if (std::exchange(m_highlighted_entry, -1) == -1) return false;
    m_dirty = true;
    return true;
}

W8DialogTextEntry* W8DialogTextArea::GetEntry(unsigned int index)
{
    if (index >= m_all_lines.size()) return 0;
    return m_all_lines[index].get();
}

int W8DialogTextArea::GetOwningEntryIndex(int visible_index)
{
    return visible_index >= 0 && visible_index < static_cast<int>(m_visible_lines.size())
               ? m_visible_lines[visible_index]
               : -1;
}

void W8DialogTextArea::SetEntryMarked(int index, bool state)
{
    W8DialogTextEntry* entry = m_all_lines[index].get();
    if (entry->m_marked != state) {
        entry->m_marked = state;
        m_dirty = true;
    }
}

int W8DialogTextArea::AddEntry(std::string_view prefix, std::string_view text,
                               unsigned int prefix_palette, unsigned int text_palette,
                               unsigned char category)
{
    unsigned int layout_mode = g_W8TextBufferAlignMiddle | g_W8TextBufferAlignLeft;
    if (m_behavior_flags & g_W8DialogTextAreaNoWrap) {
        layout_mode |= g_dialog_text_layout_mask;
    }
    auto entry = std::make_unique<W8DialogTextEntry>(
        prefix, text, prefix_palette, text_palette, &m_bounds, *m_font,
        static_cast<W8DialogueCategory>(category), layout_mode,
        m_behavior_flags & g_W8DialogTextAreaShortenText);
    if (m_line_height_override != -1) entry->SetLineHeight(m_line_height_override);
    int index = static_cast<int>(m_all_lines.size());
    m_all_lines.push_back(std::move(entry));
    RebuildVisibleEntries();
    return index;
}

void W8DialogTextArea::RemoveEntry(unsigned int index)
{
    if (index < m_all_lines.size()) {
        m_all_lines.erase(m_all_lines.begin() + index);
        RebuildVisibleEntries();
    }
}

void W8DialogTextArea::RebuildVisibleEntries()
{
    ClearEntryHighlight();
    ClearSelection();
    m_visible_lines.clear();
    for (int index = 0; index < static_cast<int>(m_all_lines.size()); ++index)
        if (m_category_filter == W8_DIALOGUE_CATEGORY_ALL ||
            m_all_lines[index]->m_category == m_category_filter)
            m_visible_lines.push_back(index);
    if (m_sorted)
        std::stable_sort(m_visible_lines.begin(), m_visible_lines.end(), [this](int a, int b) {
            return CompareTextIgnoreAsciiCase(m_all_lines[a]->GetText(),
                                              m_all_lines[b]->GetText()) < 0;
        });
    m_first_visible_entry = m_first_visible_line = 0;
    m_dirty = m_relayout_needed = true;
}

void W8DialogTextArea::SetCategoryFilter(W8DialogueCategory category)
{
    m_category_filter = category;
    RebuildVisibleEntries();
}

void W8DialogTextArea::SetSorted(bool sorted)
{
    m_sorted = sorted;
    RebuildVisibleEntries();
}

bool W8DialogTextArea::ScrollDown(bool check_only)
{
    int height = m_bounds.bottom - m_bounds.top;
    int visible_line = 1;
    for (unsigned int index = m_first_visible_entry; index < m_visible_lines.size(); ++index) {
        for (int line = index == static_cast<unsigned int>(m_first_visible_entry)
                            ? m_first_visible_line
                            : 0;
             line < VisibleEntry(index)->m_lineCount + m_entry_spacing_lines;
             ++line, ++visible_line) {
            unsigned int line_height = GetLineHeight();
            if (static_cast<int>(line_height * visible_line) > height) {
                W8TextBuffer* text = VisibleEntry(m_first_visible_entry);
                if (m_first_visible_line < text->m_lineCount - 1 + m_entry_spacing_lines) {
                    if (!check_only) ++m_first_visible_line;
                } else {
                    if (static_cast<unsigned int>(m_first_visible_entry) >=
                        m_visible_lines.size() - 1) {
                        return false;
                    }
                    if (!check_only) {
                        ++m_first_visible_entry;
                        m_first_visible_line = 0;
                    }
                }
                if (!check_only) {
                    m_dirty = true;
                    m_relayout_needed = true;
                }
                return true;
            }
        }
    }
    return false;
}

bool W8DialogTextArea::ScrollUp(bool check_only)
{
    if (m_all_lines.empty() || (m_first_visible_entry == 0 && m_first_visible_line == 0)) {
        return false;
    }
    if (!check_only) {
        if (m_first_visible_line != 0) {
            --m_first_visible_line;
        } else {
            --m_first_visible_entry;
            W8TextBuffer* text = VisibleEntry(m_first_visible_entry);
            if (text->m_lineCount + m_entry_spacing_lines > 1) {
                m_first_visible_line = text->m_lineCount - 1 + m_entry_spacing_lines;
            }
        }
        m_dirty = true;
        m_relayout_needed = true;
    }
    return true;
}

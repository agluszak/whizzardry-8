#include "wiz8/dialog_code/DialogTextArea.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include <SDL3/SDL_surface.h>

#include "Font.h"
#include "dialog_code/DialogTextEntry.h"
#include "wiz8/layouts/npc_dialogue.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/unicode.h"

/* Reconstructed logical owner; original translation-unit identity is unproven.
   Live query: 0x005D14D0/0x005D1640 sit in the gap between DialogInterface.cpp
   (upper 0x005CF580) and stMessageDialog.cpp (lower 0x005D2800). Retail
   Dialog Code\stListBox.cpp's hull is 0x005CCE70-0x005CD1E0: the six SGP
   button callbacks of W8ListBoxDialog, now defined in stListBox.cpp. The
   intervening helpers (0x005CC650/0x005CCB80/0x005CD2B0) are not in that hull,
   and neither are these. Retail emits a second one-slot table at 0x005EF89C
   holding the duplicate sdd emission 0x005D2560; the compared table at
   0x005EF898 (marked in the header) holds 0x005D2590, so only that copy
   pairs to the recompiled sdd. */

/* Behavior masks for contained text entries: single-line layout, pointer
   selection and shortening to the available width. */
// GLOBAL: WIZ8 0x005EF888
extern const unsigned int g_W8DialogTextAreaNoWrap = 0x01;
// GLOBAL: WIZ8 0x005EF88C
extern const unsigned int g_W8DialogTextAreaPointSelection = 0x02;
// GLOBAL: WIZ8 0x005EF890
extern const unsigned int g_W8DialogTextAreaShortenText = 0x04;

// FUNCTION: WIZ8 0x005d1ab0
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

// FUNCTION: WIZ8 0x005d1640
void W8DialogTextArea::Configure(const W8ControlsRect* bounds, Font& font, unsigned int flags)
{
    m_bounds = *bounds;
    m_layout_initialized = true;
    m_dirty = m_relayout_needed = true;
    m_behavior_flags = flags;
    m_font = &font;
    for (const auto& entry : m_all_lines) {
        entry->m_clipBounds = m_bounds;
    }
}

// FUNCTION: WIZ8 0x005d1900

bool W8DialogTextArea::TakeDirty()
{
    return std::exchange(m_dirty, false);
}

void W8DialogTextArea::Paint(SDL_Surface& target)
{
    const auto font_height = GetLineHeight();
    if (!font_height) return;
    {
        W8ControlsRect bounds;
        if (m_relayout_needed) {
            bounds.left = m_bounds.left;
            bounds.right = m_bounds.right;
            bounds.top = m_bounds.top - m_first_visible_line * *font_height;
        }
        for (int index = m_first_visible_entry; index < static_cast<int>(m_visible_lines.size());
             ++index) {
            if (m_relayout_needed) {
                const unsigned int height = *font_height;
                bounds.bottom = bounds.top + VisibleEntry(index)->m_lineCount * height;
                VisibleEntry(index)->SetLayoutBounds(&bounds, false);
                bounds.top = bounds.bottom + m_entry_spacing_lines * *font_height;
            }
            VisibleEntry(index)->Paint(target, index == m_selected_visible_entry,
                                       index == m_highlighted_entry);
        }
        m_relayout_needed = false;
    }
}

// FUNCTION: WIZ8 0x005d1a30
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

// FUNCTION: WIZ8 0x005d1cb0
int W8DialogTextArea::GetTotalLineCount()
{
    int total = 0;
    for (int index : m_visible_lines)
        total += m_all_lines[index]->m_lineCount + m_entry_spacing_lines;
    return m_visible_lines.empty() ? 0 : total - m_entry_spacing_lines;
}

// FUNCTION: WIZ8 0x005d1d30
std::optional<unsigned int> W8DialogTextArea::GetLineHeight()
{
    if (!m_layout_initialized) return std::nullopt;
    return m_line_height_override ? *m_line_height_override : m_font->height();
}

// FUNCTION: WIZ8 0x005d1d60
void W8DialogTextArea::SetEntrySpacing(int lines)
{
    if (m_layout_initialized) {
        m_dirty = m_relayout_needed = true;
        m_entry_spacing_lines = lines;
    }
}

// FUNCTION: WIZ8 0x005d1d90
void W8DialogTextArea::SetLineHeight(unsigned int height)
{
    m_line_height_override = height;
    m_relayout_needed = true;
    m_dirty = true;
    for (auto& entry : m_all_lines) entry->SetLineHeight(height);
}

// FUNCTION: WIZ8 0x005d1e80
bool W8DialogTextArea::SelectEntry(int index)
{
    if (index < 0 || index >= static_cast<int>(m_visible_lines.size()) ||
        index == m_selected_visible_entry)
        return false;
    m_selected_visible_entry = index;
    m_dirty = true;
    return true;
}

// FUNCTION: WIZ8 0x005d1ed0
bool W8DialogTextArea::ClearSelection()
{
    if (!m_selected_visible_entry) return false;
    m_selected_visible_entry.reset();
    m_dirty = true;
    return true;
}

// FUNCTION: WIZ8 0x005d2120
std::optional<std::string_view> W8DialogTextArea::GetEntryText(unsigned int index) const
{
    if (index >= m_all_lines.size()) return std::nullopt;
    return m_all_lines[index]->GetText();
}

/* Hit testing and selection share the visible-line mapping. Only the first
   entry can start partway through its wrapped lines. */
std::optional<unsigned int> W8DialogTextArea::FindVisibleEntryAtLine(int target)
{
    if (target < 0) return std::nullopt;
    target += m_first_visible_line;
    for (unsigned int index = m_first_visible_entry; index < m_visible_lines.size(); ++index) {
        const int lines = VisibleEntry(index)->m_lineCount + m_entry_spacing_lines;
        if (target < lines) return index;
        target -= lines;
    }
    return std::nullopt;
}

// FUNCTION: WIZ8 0x005d1dd0
std::optional<unsigned int> W8DialogTextArea::HitTestEntry(int, int y)
{
    const auto line_height = GetLineHeight();
    if (!line_height) return std::nullopt;
    int target = (y - m_bounds.top) / static_cast<int>(*line_height);
    return FindVisibleEntryAtLine(target);
}

// FUNCTION: WIZ8 0x005d1f20
bool W8DialogTextArea::UpdateSelectionFromPoint(int x, int y)
{
    if (!(m_behavior_flags & g_W8DialogTextAreaPointSelection)) return false;
    const auto index = HitTestEntry(x, y);
    return index ? SelectEntry(*index) : ClearSelection();
}

// FUNCTION: WIZ8 0x005d20a0
bool W8DialogTextArea::ClearPointSelection()
{
    return (m_behavior_flags & g_W8DialogTextAreaPointSelection) ? ClearSelection() : 0;
}

// FUNCTION: WIZ8 0x005d20f0
std::optional<std::string_view> W8DialogTextArea::GetVisibleEntryText(unsigned int index) const
{
    if (index >= m_visible_lines.size()) return std::nullopt;
    return VisibleEntry(index)->GetText();
}

// FUNCTION: WIZ8 0x005d2150
bool W8DialogTextArea::HighlightVisibleEntry(int index)
{
    if (index < 0 || index >= static_cast<int>(m_visible_lines.size()) ||
        index == m_highlighted_entry)
        return false;
    m_highlighted_entry = index;
    m_dirty = true;
    return true;
}

// FUNCTION: WIZ8 0x005d21a0
bool W8DialogTextArea::ClearEntryHighlight()
{
    if (!m_highlighted_entry) return false;
    m_highlighted_entry.reset();
    m_dirty = true;
    return true;
}

// FUNCTION: WIZ8 0x005d2420
W8DialogTextEntry* W8DialogTextArea::GetEntry(unsigned int index)
{
    if (index >= m_all_lines.size()) return 0;
    return m_all_lines[index].get();
}

// FUNCTION: WIZ8 0x005d2440
std::optional<int> W8DialogTextArea::GetOwningEntryIndex(int visible_index)
{
    return visible_index >= 0 && visible_index < static_cast<int>(m_visible_lines.size())
               ? std::optional<int>(m_visible_lines[visible_index])
               : std::nullopt;
}

// FUNCTION: WIZ8 0x005d24a0
void W8DialogTextArea::SetEntryMarked(int index, bool state)
{
    W8DialogTextEntry* entry = m_all_lines[index].get();
    if (entry->m_marked != state) {
        entry->m_marked = state;
        m_dirty = true;
    }
}

// FUNCTION: WIZ8 0x005d16c0
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
    if (m_line_height_override) entry->SetLineHeight(*m_line_height_override);
    int index = static_cast<int>(m_all_lines.size());
    m_all_lines.push_back(std::move(entry));
    RebuildVisibleEntries();
    return index;
}

// FUNCTION: WIZ8 0x005d1820
void W8DialogTextArea::RemoveEntry(unsigned int index)
{
    if (index < m_all_lines.size()) {
        m_all_lines.erase(m_all_lines.begin() + index);
        RebuildVisibleEntries();
    }
}

// FUNCTION: WIZ8 0x005d21f0
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

// FUNCTION: WIZ8 0x005d2400
void W8DialogTextArea::SetCategoryFilter(W8DialogueCategory category)
{
    m_category_filter = category;
    RebuildVisibleEntries();
}

// FUNCTION: WIZ8 0x005d2480
void W8DialogTextArea::SetSorted(bool sorted)
{
    m_sorted = sorted;
    RebuildVisibleEntries();
}

// FUNCTION: WIZ8 0x005d1ae0
bool W8DialogTextArea::ScrollDown(bool check_only)
{
    int height = m_bounds.bottom - m_bounds.top;
    int visible_line = 1;
    const auto line_height = GetLineHeight();
    if (!line_height) return false;
    for (unsigned int index = m_first_visible_entry; index < m_visible_lines.size(); ++index) {
        for (int line = index == static_cast<unsigned int>(m_first_visible_entry)
                            ? m_first_visible_line
                            : 0;
             line < VisibleEntry(index)->m_lineCount + m_entry_spacing_lines;
             ++line, ++visible_line) {
            if (static_cast<int>(*line_height * visible_line) > height) {
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

// FUNCTION: WIZ8 0x005d1c00
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

// FUNCTION: WIZ8 0x005d14d0
W8DialogTextArea::W8DialogTextArea()
{
    int invalid = -1;
    m_line_height_override.reset();
    m_selected_visible_entry.reset();
    m_highlighted_entry.reset();
    m_category_filter = W8_DIALOGUE_CATEGORY_ALL;
    m_first_visible_entry = 0;
    m_first_visible_line = 0;
    m_font = 0;
    m_layout_initialized = false;
    m_dirty = false;
    m_entry_spacing_lines = 0;
    m_behavior_flags = 0;
    m_relayout_needed = false;
    m_sorted = false;
}

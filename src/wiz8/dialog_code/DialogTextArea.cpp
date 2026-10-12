#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "wiz8/unicode.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/dialog_code/DialogTextArea.h"
#include "Font.h"
#include "dialog_code/DialogTextEntry.h"

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
extern const std::uint32_t g_W8DialogTextAreaNoWrap = 0x01;
// GLOBAL: WIZ8 0x005EF88C
extern const std::uint32_t g_W8DialogTextAreaPointSelection = 0x02;
// GLOBAL: WIZ8 0x005EF890
extern const std::uint32_t g_W8DialogTextAreaShortenText = 0x04;

// FUNCTION: WIZ8 0x005d1ab0
void W8DialogTextArea::SetFirstVisibleEntry(std::uint32_t index)
{
    if (!m_all_lines.empty() && index <= m_all_lines.size() &&
        (static_cast<std::uint32_t>(m_first_visible_entry) != index || m_first_visible_line != 0)) {
        m_first_visible_entry = index;
        m_first_visible_line = 0;
        m_relayout_needed = true;
        m_dirty = true;
    }
}

// FUNCTION: WIZ8 0x005d1640
void W8DialogTextArea::Configure(const W8ControlsRect* bounds, Font& font, std::uint32_t flags)
{
    m_bounds = *bounds;
    m_layout_initialized = true;
    m_relayout_needed = true;
    m_behavior_flags = flags;
    m_font = &font;
    for (const auto& entry : m_all_lines) {
        entry->m_pendingBounds = m_bounds;
    }
}

// FUNCTION: WIZ8 0x005d1900
void W8DialogTextArea::Draw(bool force)
{
    std::uint32_t font_height = m_font->height();
    if (force || m_dirty || m_selection_dirty) {
        W8ControlsRect bounds;
        if (m_relayout_needed) {
            bounds.left = m_bounds.left;
            bounds.right = m_bounds.right;
            bounds.top = m_bounds.top - m_first_visible_line * font_height;
        }
        for (std::int32_t index = m_first_visible_entry; index < static_cast<std::int32_t>(m_visible_lines.size());
             ++index) {
            if (m_relayout_needed) {
                std::uint32_t height = GetLineHeight();
                bounds.bottom = bounds.top + m_visible_lines[index]->m_lineCount * height;
                m_visible_lines[index]->SetLayoutBounds(&bounds, false, false);
                bounds.top = bounds.bottom + m_entry_spacing;
            }
            m_visible_lines[index]->Draw(force || m_dirty);
        }
        m_relayout_needed = false;
        m_dirty = false;
        m_selection_dirty = false;
    }
}

// FUNCTION: WIZ8 0x005d1a30
void W8DialogTextArea::SetFirstVisibleLine(std::int32_t requested_line)
{
    std::int32_t position = 0;
    std::uint32_t font_height = m_font->height();
    for (std::uint32_t index = 0; index < m_visible_lines.size(); ++index) {
        for (std::uint32_t line = 0;
             line < m_visible_lines[index]->m_lineCount +
                        static_cast<std::uint32_t>(m_entry_spacing) / font_height;
             ++line, ++position) {
            if (position == requested_line) {
                if (static_cast<std::uint32_t>(m_first_visible_entry) == index &&
                    static_cast<std::uint32_t>(m_first_visible_line) == line)
                    return;
                m_first_visible_entry = index;
                m_first_visible_line = line;
                m_relayout_needed = true;
                m_dirty = true;
                return;
            }
        }
    }
}

// FUNCTION: WIZ8 0x005d1cb0
std::int32_t W8DialogTextArea::GetTotalLineCount()
{
    std::int32_t total = 0;
    for (std::int32_t index = 0; index < static_cast<std::int32_t>(m_visible_lines.size()); ++index) {
        std::int32_t lines = m_visible_lines[index]->m_lineCount;
        total += lines + static_cast<std::uint32_t>(m_entry_spacing) / m_font->height();
    }
    if (total != 0) {
        return total - static_cast<std::uint32_t>(m_entry_spacing) / m_font->height();
    }
    return 0;
}

// FUNCTION: WIZ8 0x005d1d30
std::uint32_t W8DialogTextArea::GetLineHeight()
{
    if (!m_layout_initialized)
        return static_cast<std::uint32_t>(-1);
    std::uint32_t height = m_line_height_override;
    if (height == static_cast<std::uint32_t>(-1))
        height = m_font->height();
    return height;
}

// FUNCTION: WIZ8 0x005d1d60
void W8DialogTextArea::SetEntrySpacing(std::int32_t lines)
{
    if (m_layout_initialized) {
        std::uint32_t font_height = m_font->height();
        m_relayout_needed = true;
        m_entry_spacing = font_height * lines;
    }
}

// FUNCTION: WIZ8 0x005d1d90
void W8DialogTextArea::SetLineHeight(std::uint32_t height)
{
    m_line_height_override = height;
    m_relayout_needed = true;
    /* Retail uses the visible count with the owning list, not visible entries. */
    for (std::int32_t index = 0; index < static_cast<std::int32_t>(m_visible_lines.size()); ++index) {
        m_all_lines[index]->SetLineHeight(m_line_height_override);
    }
}

// FUNCTION: WIZ8 0x005d1e80
bool W8DialogTextArea::SelectEntry(std::int32_t index)
{
    if (!m_visible_lines.empty() && !m_visible_lines[index]->m_selected) {
        m_visible_lines[index]->SetSelected(true);
        m_selected_visible_entry = index;
        m_selection_dirty = true;
        return true;
    }
    return false;
}

// FUNCTION: WIZ8 0x005d1ed0
bool W8DialogTextArea::ClearSelection()
{
    if (m_visible_lines.empty()) {
        m_selected_visible_entry = -1;
    } else if (m_selected_visible_entry != -1) {
        m_visible_lines[m_selected_visible_entry]->SetSelected(false);
        m_selection_dirty = true;
        m_selected_visible_entry = -1;
        return true;
    }
    return false;
}

// FUNCTION: WIZ8 0x005d2120
std::optional<std::string_view> W8DialogTextArea::GetEntryText(std::uint32_t index) const
{
    if (index >= m_all_lines.size())
        return std::nullopt;
    return m_all_lines[index]->GetText();
}

/* Hit testing and point selection traverse the same visible entry lines.
   Retail applies the first visible line to every entry, including spacing. */
std::uint32_t W8DialogTextArea::FindVisibleEntryAtLine(std::uint32_t target, std::uint32_t spacing)
{
    std::uint32_t position = 0;
    for (std::uint32_t index = m_first_visible_entry; index < m_visible_lines.size(); ++index) {
        for (std::uint32_t line = m_first_visible_line;
             line < m_visible_lines[index]->m_lineCount + spacing; ++line, ++position) {
            if (position == target)
                return index;
        }
    }
    return static_cast<std::uint32_t>(-1);
}

// FUNCTION: WIZ8 0x005d1dd0
std::uint32_t W8DialogTextArea::HitTestEntry(std::int32_t, std::int32_t y)
{
    std::uint32_t font_height = m_font->height();
    std::uint32_t spacing = static_cast<std::uint32_t>(m_entry_spacing) / font_height;
    std::int32_t line_height = GetLineHeight();
    std::int32_t target = (y - m_bounds.top) / line_height;
    return FindVisibleEntryAtLine(target, spacing);
}

// FUNCTION: WIZ8 0x005d1f20
bool W8DialogTextArea::UpdateSelectionFromPoint(std::int32_t, std::int32_t y)
{
    std::uint32_t font_height = m_font->height();
    std::uint32_t spacing = static_cast<std::uint32_t>(m_entry_spacing) / font_height;
    if (!(m_behavior_flags & g_W8DialogTextAreaPointSelection))
        return false;
    std::int32_t line_height = GetLineHeight();
    std::uint32_t target = (y - m_bounds.top) / line_height;
    if (target == static_cast<std::uint32_t>(m_selected_visible_entry))
        return false;
    bool changed = ClearSelection();
    std::uint32_t index = FindVisibleEntryAtLine(target, spacing);
    if (index != static_cast<std::uint32_t>(-1))
        return SelectEntry(index);
    return changed;
}

// FUNCTION: WIZ8 0x005d20a0
bool W8DialogTextArea::ClearPointSelection()
{
    return (m_behavior_flags & g_W8DialogTextAreaPointSelection) ? ClearSelection() : 0;
}

// FUNCTION: WIZ8 0x005d20f0
std::optional<std::string_view> W8DialogTextArea::GetVisibleEntryText(std::uint32_t index) const
{
    if (index >= m_visible_lines.size())
        return std::nullopt;
    return m_visible_lines[index]->GetText();
}

// FUNCTION: WIZ8 0x005d2150
bool W8DialogTextArea::HighlightVisibleEntry(std::int32_t index)
{
    if (!m_visible_lines.empty() && !m_visible_lines[index]->m_entry_highlighted) {
        W8DialogTextEntry* entry = m_visible_lines[index];
        if (!entry->m_entry_highlighted) {
            entry->m_entry_highlighted = true;
            entry->SetGeometryDirty();
        }
        m_highlighted_entry = index;
        m_selection_dirty = true;
        return true;
    }
    return false;
}

// FUNCTION: WIZ8 0x005d21a0
bool W8DialogTextArea::ClearEntryHighlight()
{
    if (m_visible_lines.empty()) {
        m_highlighted_entry = -1;
    } else if (m_highlighted_entry != -1) {
        W8DialogTextEntry* entry = m_visible_lines[m_highlighted_entry];
        if (entry->m_entry_highlighted) {
            entry->m_entry_highlighted = false;
            entry->SetGeometryDirty();
        }
        m_selection_dirty = true;
        m_highlighted_entry = -1;
        return true;
    }
    return false;
}

// FUNCTION: WIZ8 0x005d2420
W8DialogTextEntry* W8DialogTextArea::GetEntry(std::uint32_t index)
{
    if (index >= m_all_lines.size())
        return 0;
    return m_all_lines[index].get();
}

// FUNCTION: WIZ8 0x005d2440
std::int32_t W8DialogTextArea::GetOwningEntryIndex(std::int32_t visible_index)
{
    const auto* visible = m_visible_lines[visible_index];
    for (std::int32_t index = 0; index < static_cast<std::int32_t>(m_all_lines.size()); ++index) {
        if (m_all_lines[index].get() == visible)
            return index;
    }
    return -1;
}

// FUNCTION: WIZ8 0x005d24a0
void W8DialogTextArea::SetEntryMarked(std::int32_t index, bool state)
{
    W8DialogTextEntry* entry = m_all_lines[index].get();
    if (entry->m_marked != state) {
        entry->m_marked = state;
        entry->SetGeometryDirty();
    }
    m_selection_dirty = true;
}

// FUNCTION: WIZ8 0x005d16c0
std::int32_t W8DialogTextArea::AddEntry(std::string_view prefix, std::string_view text,
                               std::uint32_t prefix_palette, std::uint32_t text_palette,
                               std::uint8_t category)
{
    std::uint32_t layout_mode = g_W8TextBufferAlignMiddle | g_W8TextBufferAlignLeft;
    if (m_behavior_flags & g_W8DialogTextAreaNoWrap) {
        layout_mode |= g_dialog_text_layout_mask;
    }
    auto entry = std::make_unique<W8DialogTextEntry>(
        prefix, text, prefix_palette, text_palette, &m_bounds, *m_font, category, layout_mode,
        m_behavior_flags & g_W8DialogTextAreaShortenText);
    if (m_line_height_override != -1)
        entry->SetLineHeight(m_line_height_override);
    std::int32_t index = static_cast<std::int32_t>(m_all_lines.size());
    m_all_lines.push_back(std::move(entry));
    m_relayout_needed = true;
    RebuildVisibleEntries();
    return index;
}

// FUNCTION: WIZ8 0x005d1820
void W8DialogTextArea::RemoveEntry(std::uint32_t index)
{
    if (!m_all_lines.empty() && index < m_all_lines.size()) {
        if (m_all_lines[index]->m_entry_highlighted)
            m_highlighted_entry = -1;
        if (m_all_lines[index]->m_selected)
            m_selected_visible_entry = -1;
        m_all_lines.erase(m_all_lines.begin() + index);
        if (m_first_visible_entry != 0 &&
            m_all_lines.size() <= static_cast<std::uint32_t>(m_first_visible_entry) &&
            !m_all_lines.empty()) {
            SetFirstVisibleEntry(static_cast<std::int32_t>(m_all_lines.size()) - 1);
        }
        m_relayout_needed = true;
        m_dirty = true;
        RebuildVisibleEntries();
    }
}

// FUNCTION: WIZ8 0x005d21f0
void W8DialogTextArea::RebuildVisibleEntries()
{
    ClearEntryHighlight();
    ClearSelection();
    m_visible_lines.clear();
    for (std::int32_t index = 0; index < static_cast<std::int32_t>(m_all_lines.size()); ++index) {
        if (m_all_lines[index]->m_category == static_cast<std::uint8_t>(m_category_filter) ||
            m_category_filter == -1) {
            if (!m_sorted) {
                m_visible_lines.push_back(m_all_lines[index].get());
            } else {
                const std::string_view text = *GetEntryText(index);
                std::int32_t position;
                for (position = 0; position < static_cast<std::int32_t>(m_visible_lines.size());
                     ++position) {
                    if (CompareTextIgnoreAsciiCase(text, *GetVisibleEntryText(position)) < 0)
                        break;
                }
                if (position == static_cast<std::int32_t>(m_visible_lines.size())) {
                    m_visible_lines.push_back(m_all_lines[index].get());
                } else {
                    m_visible_lines.insert(m_visible_lines.begin() + position, m_all_lines[index].get());
                }
            }
        }
    }
    m_first_visible_entry = 0;
    m_first_visible_line = 0;
}

// FUNCTION: WIZ8 0x005d2400
void W8DialogTextArea::SetCategoryFilter(std::int8_t category)
{
    m_category_filter = category;
    m_selection_dirty = true;
    m_relayout_needed = true;
    RebuildVisibleEntries();
}

// FUNCTION: WIZ8 0x005d2480
void W8DialogTextArea::SetSorted(bool sorted)
{
    m_sorted = sorted;
    m_selection_dirty = true;
    m_relayout_needed = true;
    RebuildVisibleEntries();
}

// FUNCTION: WIZ8 0x005d1ae0
bool W8DialogTextArea::ScrollDown(bool check_only)
{
    std::uint32_t spacing = static_cast<std::uint32_t>(m_entry_spacing) / m_font->height();
    std::int32_t height = m_bounds.bottom - m_bounds.top;
    std::int32_t visible_line = 1;
    for (std::uint32_t index = m_first_visible_entry; index < m_visible_lines.size(); ++index) {
        for (std::uint32_t line = m_first_visible_line;
             line < m_visible_lines[index]->m_lineCount + spacing; ++line, ++visible_line) {
            std::uint32_t line_height = GetLineHeight();
            if (static_cast<std::int32_t>(line_height * visible_line) > height) {
                W8TextBuffer* text = m_visible_lines[m_first_visible_entry];
                if (static_cast<std::uint32_t>(m_first_visible_line) <
                    text->m_lineCount - 1 + spacing) {
                    if (!check_only)
                        ++m_first_visible_line;
                } else {
                    if (static_cast<std::uint32_t>(m_first_visible_entry) >=
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
    std::uint32_t spacing = static_cast<std::uint32_t>(m_entry_spacing) / m_font->height();
    if (m_all_lines.empty() || (m_first_visible_entry == 0 && m_first_visible_line == 0)) {
        return false;
    }
    if (!check_only) {
        if (m_first_visible_line != 0) {
            --m_first_visible_line;
        } else {
            --m_first_visible_entry;
            W8TextBuffer* text = m_visible_lines[m_first_visible_entry];
            if (text->m_lineCount + spacing > 1) {
                m_first_visible_line = text->m_lineCount - 1 + spacing;
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
    std::int32_t invalid;

    invalid = -1;
    m_line_height_override = invalid;
    m_selected_visible_entry = invalid;
    m_highlighted_entry = invalid;
    m_category_filter = invalid;
    m_first_visible_entry = 0;
    m_first_visible_line = 0;
    m_font = 0;
    m_layout_initialized = false;
    m_dirty = false;
    m_selection_dirty = false;
    m_entry_spacing = 0;
    m_behavior_flags = 0;
    m_relayout_needed = false;
    m_sorted = false;
}

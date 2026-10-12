#include <cstdint>
#include "Font.h"
#pragma once

#include <memory>
#include <optional>
#include <vector>
#include <string>
#include <string_view>

#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/dialog_code/DialogTextEntry.h"
#include "compat/compiler.h"

extern const std::uint32_t g_W8DialogTextAreaNoWrap;
extern const std::uint32_t g_W8DialogTextAreaPointSelection;
extern const std::uint32_t g_W8DialogTextAreaShortenText;

/* Two instances of this pointer-vector specialization are embedded in
   W8DialogTextArea. */

/* Nonpolymorphic scrolling-text helper contained by dialogs, not a widget or
   text-buffer base. Both vectors belong to this object; only all_lines owns
   the entries. visible_lines references entries selected/ordered from it. */
class W8DialogTextArea {
    friend class W8NpcDialogueScrollWidget;
    friend class W8NpcDialogueTextController;

public:
    W8DialogTextArea();
    ~W8DialogTextArea() = default;
    void Configure(const W8ControlsRect* bounds, Font& font, std::uint32_t flags);
    void Draw(bool force);
    void SetFirstVisibleLine(std::int32_t line);
    std::int32_t GetTotalLineCount();
    std::uint32_t GetLineHeight();
    void SetEntrySpacing(std::int32_t lines);
    void SetLineHeight(std::uint32_t height);
    bool SelectEntry(std::int32_t index);
    bool ClearSelection();
    std::optional<std::string_view> GetEntryText(std::uint32_t index) const;
    std::uint32_t HitTestEntry(std::int32_t x, std::int32_t y);
    bool UpdateSelectionFromPoint(std::int32_t x, std::int32_t y);
    bool ClearPointSelection();
    std::optional<std::string_view> GetVisibleEntryText(std::uint32_t index) const;
    bool HighlightVisibleEntry(std::int32_t index);
    bool ClearEntryHighlight();
    W8DialogTextEntry* GetEntry(std::uint32_t index);
    std::int32_t GetOwningEntryIndex(std::int32_t visible_index);
    void SetEntryMarked(std::int32_t index, bool state);
    std::int32_t AddEntry(std::string_view prefix, std::string_view text, std::uint32_t prefix_palette,
                 std::uint32_t text_palette, std::uint8_t category);
    void RemoveEntry(std::uint32_t index);
    void RebuildVisibleEntries();
    void SetCategoryFilter(std::int8_t category);
    void SetSorted(bool sorted);
    bool ScrollDown(bool check_only);
    bool ScrollUp(bool check_only);
    void SetFirstVisibleEntry(std::uint32_t index);

private:
    std::uint32_t FindVisibleEntryAtLine(std::uint32_t target, std::uint32_t spacing);

    W8ControlsRect m_bounds; /* passed to entry construction */
    Font* m_font;
    std::int32_t m_first_visible_entry;
    std::int32_t m_first_visible_line;
    std::vector<std::unique_ptr<W8DialogTextEntry>> m_all_lines; /* owns entries */
    std::vector<W8DialogTextEntry*> m_visible_lines; /* non-owning view */
    bool m_layout_initialized;

public:
    /* Owning dialogs raise this before Draw, the same way they dirty the
       contained button and scrollbar. */
    bool m_dirty;

private:
    bool m_selection_dirty; /* repaint after a selection change */
    std::uint8_t unknown_03f;
    std::int32_t m_entry_spacing;
    std::int32_t m_behavior_flags;
    std::int32_t m_line_height_override;
    std::int32_t m_selected_visible_entry;
    std::int32_t m_highlighted_entry; /* visible entry index, -1 when clear */
    bool m_relayout_needed;
    std::int8_t m_category_filter;
    bool m_sorted;
    std::uint8_t unknown_057;
};

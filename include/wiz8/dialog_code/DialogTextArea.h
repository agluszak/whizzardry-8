#include "wiz8/canvas/Font.h"
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "wiz8/dialog_code/DialogTextEntry.h"
#include "wiz8/local_code/ControlsRect.h"

extern const unsigned int g_W8DialogTextAreaNoWrap;
extern const unsigned int g_W8DialogTextAreaPointSelection;
extern const unsigned int g_W8DialogTextAreaShortenText;

/* Two instances of this pointer-vector specialization are embedded in
   W8DialogTextArea. */

/* Nonpolymorphic scrolling-text helper contained by dialogs, not a widget or
   text-buffer base. Both vectors belong to this object; only all_lines owns
   the entries. visible_lines references entries selected/ordered from it. */
class W8DialogTextArea {
    friend class W8NpcDialogueTextController;

public:
    W8DialogTextArea();
    ~W8DialogTextArea() = default;
    void Configure(const W8ControlsRect* bounds, Font& font, unsigned int flags);
    void Paint(SDL_Surface& target);
    bool TakeDirty();
    void SetFirstVisibleLine(int line);
    int GetTotalLineCount();
    unsigned int GetLineHeight();
    void SetEntrySpacing(int lines);
    void SetLineHeight(unsigned int height);
    bool SelectEntry(int index);
    bool ClearSelection();
    std::optional<std::string_view> GetEntryText(unsigned int index) const;
    unsigned int HitTestEntry(int x, int y);
    bool UpdateSelectionFromPoint(int x, int y);
    bool ClearPointSelection();
    std::optional<std::string_view> GetVisibleEntryText(unsigned int index) const;
    bool HighlightVisibleEntry(int index);
    bool ClearEntryHighlight();
    W8DialogTextEntry* GetEntry(unsigned int index);
    int GetOwningEntryIndex(int visible_index);
    void SetEntryMarked(int index, bool state);
    int AddEntry(std::string_view prefix, std::string_view text, unsigned int prefix_palette,
                 unsigned int text_palette, unsigned char category);
    void RemoveEntry(unsigned int index);
    void RebuildVisibleEntries();
    void SetCategoryFilter(W8DialogueCategory category);
    void SetSorted(bool sorted);
    bool ScrollDown(bool check_only);
    bool ScrollUp(bool check_only);
    void SetFirstVisibleEntry(unsigned int index);

private:
    W8DialogTextEntry* VisibleEntry(int index) const
    {
        return m_all_lines[m_visible_lines[index]].get();
    }
    unsigned int FindVisibleEntryAtLine(int target);

    W8ControlsRect m_bounds; /* passed to entry construction */
    Font* m_font;
    int m_first_visible_entry;
    int m_first_visible_line;
    std::vector<std::unique_ptr<W8DialogTextEntry>> m_all_lines; /* owns entries */
    std::vector<int> m_visible_lines; /* indices into the owning entries */
    bool m_layout_initialized;

public:
    /* Owning dialogs raise this before Draw, the same way they dirty the
       contained button and scrollbar. */
    bool m_dirty;

private:
    int m_entry_spacing_lines;
    int m_behavior_flags;
    int m_line_height_override;
    int m_selected_visible_entry;
    int m_highlighted_entry; /* visible entry index, -1 when clear */
    bool m_relayout_needed;
    W8DialogueCategory m_category_filter = W8_DIALOGUE_CATEGORY_ALL;
    bool m_sorted;
};

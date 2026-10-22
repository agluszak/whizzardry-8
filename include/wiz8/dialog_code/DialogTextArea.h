#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "wiz8/canvas/Font.h"
#include "wiz8/dialog_code/DialogTextEntry.h"
#include "wiz8/local_code/ControlsRect.h"

// Owned wrapped paragraphs for information dialogs. Notebook filtering and
// selection belong to its view, not this append-only text layout.
class W8DialogTextArea {
public:
    void Configure(const W8ControlsRect* bounds, Font& font);
    void Paint(SDL_Surface& target);
    bool TakeDirty();
    void SetFirstVisibleLine(int line);
    int GetTotalLineCount();
    unsigned int GetLineHeight();
    void SetEntrySpacing(int lines);
    void AddEntry(std::string_view prefix, std::string_view text, unsigned int prefix_palette,
                  unsigned int text_palette);
    bool m_dirty = false;

private:
    W8ControlsRect m_bounds{};
    Font* m_font = nullptr;
    int m_first_visible_line = 0;
    int m_entry_spacing_lines = 0;
    std::vector<std::unique_ptr<W8DialogTextEntry>> m_entries;
};

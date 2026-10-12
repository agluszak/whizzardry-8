#pragma once

#include <cstdint>
#include "Font.h"

#include <string_view>

#include "wiz8/local_code/TextBuffer.h"
#include "compat/compiler.h"

struct W8ControlsRect;

extern std::uint32_t g_dialog_text_layout_mask;

// VTABLE: WIZ8 0x005ef894
class W8DialogTextEntry : public W8TextBuffer {
public:
    W8DialogTextEntry(std::string_view prefix, std::string_view text, std::uint32_t prefix_palette,
                      std::uint32_t text_palette, const W8ControlsRect* bounds, Font& font,
                      std::uint8_t category, std::uint32_t layout_mode, std::uint8_t shorten);
    void Draw(bool force);
    void SetSelected(bool selected);

private:
    std::int32_t DrawLine(std::string_view line, std::int32_t prefix_remaining, std::int32_t y);
    friend class W8DialogTextArea;
    /* The controller reads m_category when it snapshots the transcript. */
    friend class W8NpcDialogueTextController;
    std::uint32_t m_prefix_palette;
    std::uint32_t m_text_palette;
    std::int32_t m_prefix_length; /* includes ": " */
    bool m_selected;
    bool m_entry_highlighted;     /* highlighted keyword palette */
    std::uint8_t m_category;     /* text-area filter key */
    std::uint8_t m_shorten_mask; /* raw shortening bit from text-area behavior flags */
    bool m_marked;                /* marked transcript entry palette */
};

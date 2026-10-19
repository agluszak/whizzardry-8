#pragma once

#include <string_view>

#include "compat/compiler.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/layouts/npc_dialogue.h"
#include "wiz8/local_code/TextBuffer.h"

struct W8ControlsRect;

extern unsigned int g_dialog_text_layout_mask;

class W8DialogTextEntry : public W8TextBuffer {
public:
    W8DialogTextEntry(std::string_view prefix, std::string_view text, unsigned int prefix_palette,
                      unsigned int text_palette, const W8ControlsRect* bounds, Font& font,
                      W8DialogueCategory category, unsigned int layout_mode, unsigned char shorten);
    void Paint(SDL_Surface& target, bool selected, bool highlighted);

private:
    int DrawLine(SDL_Surface& target, std::string_view line, int prefix_remaining, int y,
                 bool selected);
    friend class W8DialogTextArea;
    /* The controller reads m_category when it snapshots the transcript. */
    friend class W8NpcDialogueTextController;
    unsigned int m_prefix_palette;
    unsigned int m_text_palette;
    int m_prefix_length; /* includes ": " */
    W8DialogueCategory m_category;
    unsigned char m_shorten_mask; /* raw shortening bit from text-area behavior flags */
    bool m_marked;                /* marked transcript entry palette */
};

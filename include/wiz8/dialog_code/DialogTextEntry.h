#pragma once

#include <string_view>

#include "wiz8/canvas/Font.h"
#include "wiz8/local_code/TextBuffer.h"

struct W8ControlsRect;

class W8DialogTextEntry : public W8TextBuffer {
public:
    W8DialogTextEntry(std::string_view prefix, std::string_view text, unsigned int prefix_palette,
                      unsigned int text_palette, const W8ControlsRect* bounds, Font& font);
    void Paint(SDL_Surface& target);

private:
    int DrawLine(SDL_Surface& target, std::string_view line, int prefix_remaining, int y);
    unsigned int m_prefix_palette;
    unsigned int m_text_palette;
    int m_prefix_length; /* includes ": " */
};

#pragma once

#include <optional>
#include <string>

#include "input.h"
#include "compat/compiler.h"

namespace wiz8 { class File; }

struct W8CreditLine {
    unsigned int flags;
    int pixel_width;
    int line_height;
    std::string primary;
    std::string secondary;
};
W8_ABI_ASSERT(sizeof(W8CreditLine) == 0x14, "W8CreditLine_size");

std::optional<std::string> ReadRetailTextLine(wiz8::File* handle);
unsigned char CreditsScreenEnter(void);
void CreditsScreenFrame(void);
unsigned char CreditsScreenLeave(int leaving);
/* Full-screen dismiss on left-up or right-up. */
unsigned char CreditsBackgroundRegionEvent(const InputAtom* event, struct W8Region* region);

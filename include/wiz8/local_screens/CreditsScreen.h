#pragma once

#include <optional>
#include <string>

#include "compat/compiler.h"

namespace wiz8 {
class File;
}

struct W8CreditLine {
    unsigned int flags;
    int pixel_width;
    int line_height;
    std::string primary;
    std::string secondary;
};

std::optional<std::string> ReadRetailTextLine(wiz8::File* handle);
unsigned char CreditsScreenEnter(void);
void CreditsScreenFrame(void);
unsigned char CreditsScreenLeave(int leaving);

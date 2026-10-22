#pragma once

#include <memory>
#include <optional>
#include <string>

#include "wiz8/screen.h"

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
/* Scrolls Data\Options\Credits.txt over the credits backdrop. */
std::unique_ptr<Screen> CreateCreditsScreen();

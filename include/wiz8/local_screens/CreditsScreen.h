#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "compat/compiler.h"

namespace wiz8 { class File; }

struct W8CreditLine {
    std::uint32_t flags;
    std::int32_t pixel_width;
    std::int32_t line_height;
    std::string primary;
    std::string secondary;
};

std::optional<std::string> ReadRetailTextLine(wiz8::File* handle);
std::uint8_t CreditsScreenEnter(void);
void CreditsScreenFrame(void);
std::uint8_t CreditsScreenLeave(std::int32_t leaving);

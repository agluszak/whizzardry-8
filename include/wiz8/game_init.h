#pragma once

#include <cstddef>
#include <string>

unsigned char InitializeGame(size_t& initialized_screens, bool load_at_startup,
                             const std::string& string_data_override);

#pragma once

#include <cstdint>
#include "wiz8/filesystem.h"

bool ReadTextLine(wiz8::File* handle, char* destination, std::int32_t capacity, bool& more);

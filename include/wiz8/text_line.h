#pragma once

#include "wiz8/filesystem.h"

bool ReadTextLine(wiz8::File* handle, char* destination, int capacity, bool& more);

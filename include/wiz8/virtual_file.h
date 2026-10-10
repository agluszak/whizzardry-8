#pragma once

#include "wiz8/filesystem.h"

unsigned char ReadTextLine(wiz8::File* handle, char* destination, int capacity, unsigned char* more);

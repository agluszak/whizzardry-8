#pragma once

#include "native.h"

#define SR_DLL_IMPORT


#define W8_PRINTF(format_index, first_argument) \
    __attribute__((format(printf, format_index, first_argument)))



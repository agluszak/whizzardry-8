#pragma once

#include <stddef.h>
#include <stdint.h>
#include "native.h"

#define SR_DLL_IMPORT

typedef int32_t w8_long;
typedef uint32_t w8_ulong;
typedef uintptr_t w8_ulong_ptr;

#define W8_PRINTF(format_index, first_argument)                                                    \
    __attribute__((format(printf, format_index, first_argument)))

#ifndef __cplusplus
#include <assert.h>
#endif

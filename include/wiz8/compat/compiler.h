#pragma once

#include <stddef.h>
#include <stdint.h>
#include "native.h"

#define SR_DLL_IMPORT
#define SR_DLL_EXPORT

typedef int32_t w8_long;
typedef uint32_t w8_ulong;
typedef uintptr_t w8_ulong_ptr;

#define W8_PRINTF(format_index, first_argument) \
    __attribute__((format(printf, format_index, first_argument)))

#ifndef __cplusplus
#include <assert.h>
#endif

/* Historical runtime-layout assertions are not native format contracts. */
#define W8_ABI_ASSERT(condition, message) static_assert(1, message)

#ifdef __cplusplus
#define W8_ASSERT_BASE_OFFSET(derived, base, member, offset) \
    W8_ABI_ASSERT(offsetof(derived, member) - offsetof(base, member) == (offset), \
                  #derived " " #base " base must stay at " #offset)
#define W8_ASSERT_BASE_END(derived, base, member, offset) \
    W8_ABI_ASSERT(offsetof(derived, member) - sizeof(base) == (offset), \
                  #derived " " #base " base must stay at " #offset)
#define W8_ASSERT_BASE_TAIL(derived, base, offset) \
    W8_ABI_ASSERT(sizeof(derived) - sizeof(base) == (offset), \
                  #derived " " #base " base must stay at " #offset)
#endif

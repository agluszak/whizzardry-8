#pragma once

#include <stddef.h>

#if defined(WIZ8_NATIVE)
#include "native.h"
#endif

/* The original code's `long` is 32 bits wide.  The legacy lanes keep the
   exact spelling (and therefore mangled names); LP64 native targets need an
   explicitly 32-bit type.  tools/native_long_rewrite.py applies the spelling. */
#if defined(WIZ8_NATIVE)
#include <stdint.h>
typedef int32_t w8_long;
typedef uint32_t w8_ulong;
/* An integer that carries a pointer, where retail stored addresses in a long. */
typedef uintptr_t w8_ulong_ptr;
#else
typedef long w8_long;
typedef unsigned long w8_ulong;
typedef unsigned long w8_ulong_ptr;
#endif

/* printf-style checking for the native compiler. */
#if defined(WIZ8_NATIVE)
#define W8_PRINTF(format_index, first_argument)                                                    \
    __attribute__((format(printf, format_index, first_argument)))
#else
#define W8_PRINTF(format_index, first_argument)
#endif

// Keep modern structural annotations visible to the lint compiler while
// making them syntax-neutral for the C++98 matching toolchain.
#if !defined(WIZ8_CLANG_LINT) && !defined(WIZ8_NATIVE)
#define override
#endif

/* Keep source-level layout contracts in standard static_assert form while
   retaining the VC6 matching compiler.  The line-numbered typedef makes each
   assertion independent even when several appear in one scope.  C TUs keep the
   shim under lint too: C11 static_assert needs <assert.h>, which the pinned
   VC98 headers do not provide. */
#if (!defined(WIZ8_CLANG_LINT) && !defined(WIZ8_NATIVE)) || !defined(__cplusplus)
#define WIZ8_COMPAT_JOIN_INNER(left, right) left##right
#define WIZ8_COMPAT_JOIN(left, right) WIZ8_COMPAT_JOIN_INNER(left, right)
#define static_assert(condition, message)                                                          \
    typedef char WIZ8_COMPAT_JOIN(wiz8_static_assertion_at_line_, __LINE__)[(condition) ? 1 : -1]
#endif

/* VC6 treats wchar_t as an unsigned-short typedef.  Clang normally makes it
   a distinct built-in type even for a Windows target; the lint lane disables
   that built-in and recreates the legacy ABI spelling before VC6 headers are
   parsed. */
#if defined(WIZ8_CLANG_LINT) && !defined(_WCHAR_T_DEFINED)
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif

/* Layout of the shipped 32-bit MSVC executables.  These contracts hold for the
   matching and clang-cl lanes; native LP64/arm64 objects legitimately differ.
   On-disk and wire formats use plain static_assert, which every lane checks. */
#if defined(WIZ8_NATIVE)
#define W8_ABI_ASSERT(condition, message) static_assert(1, message)
#else
#define W8_ABI_ASSERT(condition, message) static_assert(condition, message)
#endif

// Fixed base offsets are checked through member offsets or object extents.
#ifdef __cplusplus
#define W8_ASSERT_BASE_OFFSET(derived, base, member, offset)                                       \
    W8_ABI_ASSERT(offsetof(derived, member) - offsetof(base, member) == (offset),                  \
                  #derived " " #base " base must stay at " #offset)
#define W8_ASSERT_BASE_END(derived, base, member, offset)                                          \
    W8_ABI_ASSERT(offsetof(derived, member) - sizeof(base) == (offset),                            \
                  #derived " " #base " base must stay at " #offset)
#define W8_ASSERT_BASE_TAIL(derived, base, offset)                                                 \
    W8_ABI_ASSERT(sizeof(derived) - sizeof(base) == (offset),                                      \
                  #derived " " #base " base must stay at " #offset)
#endif

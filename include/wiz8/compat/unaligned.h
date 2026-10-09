#pragma once

/* Scalar pointers escaped from packed retail records lose the containing
   record's alignment. Keep their access alignment explicit in native Clang;
   these aliases retain the scalar type, width and legacy mangled signatures.
   They do not change record layouts or allocation sizes. */
#if defined(WIZ8_NATIVE)
typedef int w8_unaligned_int __attribute__((aligned(1)));
typedef unsigned int w8_unaligned_uint __attribute__((aligned(1)));
typedef short w8_unaligned_short __attribute__((aligned(1)));
typedef float w8_unaligned_float __attribute__((aligned(1)));
#else
typedef int w8_unaligned_int;
typedef unsigned int w8_unaligned_uint;
typedef short w8_unaligned_short;
typedef float w8_unaligned_float;
#endif

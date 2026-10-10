#pragma once

#include <span>

#include "srMath.h"

class srARGB;

using SRBYTE = unsigned char;
using SRLONG = w8_long;
using SRDWORD = w8_ulong;
using srVector2 = srVector2T<float>;
using srVector3 = srVector3T<float>;
using srVector4 = srVector4T<float>;
using srMatrix4 = srMatrix4T<float>;

// Linear input arrays contain at least destination.size() elements. Indexed
// operations require destination.size() >= indices.size(); their source/remap
// arrays cover the referenced indices. minMax requires a nonempty source.
// Partially overlapping input/output ranges are not supported.
// Matrix multiplication requires output distinct from both inputs.
namespace srMath {
void bitwiseAnd(std::span<SRDWORD> destination, const SRDWORD* source, SRDWORD constant);
void bitwiseOr(std::span<SRDWORD> destination, const SRDWORD* source, SRDWORD constant);
void bitwiseAnd(std::span<SRDWORD> destination, const SRDWORD* source_0, const SRDWORD* source_1);
void bitwiseOr(std::span<SRDWORD> destination, const SRDWORD* source_0, const SRDWORD* source_1);
int isEqual(std::span<const SRDWORD> source_0, const SRDWORD* source_1);
int isEqual(std::span<const SRDWORD> source, SRDWORD constant);
void copyIndexed(std::span<SRDWORD> destination, const SRDWORD* source,
                 std::span<const SRDWORD> indices);
void axpy(std::span<float> destination, float add_constant, float multiply_constant,
          const float* multiply_source);
void axpy(std::span<float> destination, const float* add_source, float multiply_constant,
          const float* multiply_source);
void axpy(std::span<float> destination, float add_constant, const float* scale_source,
          const float* multiply_source);
void axpy(std::span<float> destination, const float* add_source, const float* scale_source,
          const float* multiply_source);
void axpy(std::span<float> destination, float add_constant, float scale, const float* scale_source,
          const float* multiply_source);
void axpy(std::span<float> destination, const float* add_source, float scale,
          const float* scale_source, const float* multiply_source);
void add(std::span<float> destination, float constant, const float* source);
void add(std::span<float> destination, const float* source_0, const float* source_1);
void sub(std::span<float> destination, float constant, const float* source);
void sub(std::span<float> destination, const float* source_0, const float* source_1);
void mul(std::span<float> destination, float constant, const float* source);
void mul(std::span<float> destination, const float* source_0, const float* source_1);
void mul(std::span<float> destination, float constant, const float* source_0,
         const float* source_1);
void lerp(std::span<float> destination, const float* target, const float* source, float constant);
void clampMin(std::span<float> destination, const float* source, float minimum);
void clampUnit(std::span<float> destination, const float* source);
void mulIndexed(std::span<float> destination, float constant, const float* indexed_source,
                std::span<const SRDWORD> indices);
void mulIndexed(std::span<float> destination, const float* linear_source,
                const float* indexed_source, std::span<const SRDWORD> indices);
void minMax(std::span<const float> source, float& minimum, float& maximum);
int isZero(std::span<const float> source);
void invPoly(std::span<float> destination, const float* source, const srVector3& poly);
void neg(std::span<float> destination, const float* source);
void copyIndexed(std::span<srVector2> destination, const srVector2* source,
                 std::span<const SRDWORD> indices);
void minMax(std::span<const srVector3> source, srVector3& minimum, srVector3& maximum);
void copy(std::span<srVector3> destination, const srVector4* source);
void add(std::span<srVector3> destination, const srVector3& constant,
         const srVector3* vector_source);
void sub(std::span<srVector3> destination, const srVector3& constant,
         const srVector3* vector_source);
void mul(std::span<srVector3> destination, const srVector3& constant,
         const srVector3* vector_source);
void add(std::span<srVector3> destination, const srVector3& constant, const float* float_source);
void sub(std::span<srVector3> destination, const srVector3& constant, const float* float_source);
void mul(std::span<srVector3> destination, const srVector3& constant, const float* float_source);
void add(std::span<srVector3> destination, const srVector3* vector_source,
         const float* float_source);
void sub(std::span<srVector3> destination, const srVector3* vector_source,
         const float* float_source);
void mul(std::span<srVector3> destination, const srVector3* vector_source,
         const float* float_source);
void sub(std::span<srVector3> destination, const float* float_source,
         const srVector3* vector_source);
void length(std::span<float> destination, const srVector3* vectors);
void dot(std::span<float> destination, const srVector3& constant, const srVector3* vectors);
void dot(std::span<float> destination, const srVector4& constant, const srVector3* vectors);
void dot(std::span<float> destination, const srVector3* vectors_0, const srVector3* vectors_1);
void normalize(std::span<srVector3> destination, const srVector3* vectors, float length);
void mulIndexed(std::span<srVector3> destination, const srVector3& constant,
                const srVector3* indexed_source, std::span<const SRDWORD> indices);
void mulIndexed(std::span<srVector3> destination, const srVector3* linear_source,
                const srVector3* indexed_source, std::span<const SRDWORD> indices);
void copyIndexed(std::span<srVector3> destination, const srVector2* source,
                 std::span<const SRDWORD> indices);
void copyIndexed(std::span<srVector3> destination, const srVector3* source,
                 std::span<const SRDWORD> indices);
void copyIndexed(std::span<srVector3> destination, const srVector4* source,
                 std::span<const SRDWORD> indices);
void transform(std::span<srVector3> destination, const srVector3* vectors, const srMatrix4& matrix);
void dir(std::span<srVector3> destination, float* lengths, const srVector3* source);
void dir(std::span<srVector3> destination, float* lengths, const srVector4* source);
void copy(std::span<srVector4> destination, const srVector3* source_0, const float* source_1);
void copy(std::span<srVector4> destination, const srVector3* source, float constant);
void add(std::span<srVector4> destination, const srVector4& constant,
         const srVector4* vector_source);
void mul(std::span<srVector4> destination, const srVector4& constant,
         const srVector4* vector_source);
void sub(std::span<srVector4> destination, const srVector4& constant,
         const srVector4* vector_source);
void add(std::span<srVector4> destination, const srVector4& constant, const float* float_source);
void mul(std::span<srVector4> destination, const srVector4& constant, const float* float_source);
void sub(std::span<srVector4> destination, const srVector4& constant, const float* float_source);
void add(std::span<srVector4> destination, const srVector4* vector_source,
         const float* float_source);
void mul(std::span<srVector4> destination, const srVector4* vector_source,
         const float* float_source);
void sub(std::span<srVector4> destination, const srVector4* vector_source,
         const float* float_source);
void sub(std::span<srVector4> destination, const float* float_source,
         const srVector4* vector_source);
void length(std::span<float> destination, const srVector4* vectors);
void dot(std::span<float> destination, const srVector4& constant, const srVector4* vectors);
void dotIndexed(std::span<float> destination, const srVector4& constant, const srVector4* vectors,
                std::span<const SRDWORD> indices);
void dot(std::span<float> destination, const srVector4* vectors_0, const srVector4* vectors_1);
void normalize(std::span<srVector4> destination, const srVector4* vectors, float length);
void mulIndexed(std::span<srVector4> destination, const srVector4& constant,
                const srVector4* indexed_source, std::span<const SRDWORD> indices);
void mulIndexed(std::span<srVector4> destination, const srVector4* linear_source,
                const srVector4* indexed_source, std::span<const SRDWORD> indices);
void copyIndexed(std::span<srVector4> destination, const srARGB* source,
                 std::span<const SRDWORD> indices);
void copyIndexed(std::span<srVector4> destination, const srVector4* source,
                 std::span<const SRDWORD> indices);
void copyIndexed(std::span<srVector4> destination, const srVector3* source,
                 std::span<const SRDWORD> indices);
void copyIndexed(std::span<srVector4> destination, const srVector2* source,
                 std::span<const SRDWORD> indices);
void transformOrtho(std::span<srVector4> destination, const srVector4* source,
                    const srMatrix4& matrix);
void transformPerspective(std::span<srVector4> destination, const srVector4* source,
                          const srMatrix4& matrix);
void transform(std::span<srVector4> destination, const srVector4* vectors, const srMatrix4& matrix);
void transform(std::span<srVector4> destination, const srVector3* vectors, const srMatrix4& matrix);
void transformIndexed(std::span<srVector3> destination, const srVector3* source,
                      std::span<const SRDWORD> indices, const srMatrix4& matrix);
void transformIndexed(std::span<srVector4> destination, const srVector3* source,
                      std::span<const SRDWORD> indices, const srMatrix4& matrix);
void axpy(std::span<srVector4> destination, const srVector4& add_constant,
          const srVector4& multiply_constant, const float* multiply_source);
void axpy(std::span<srVector4> destination, const srVector4* add_source,
          const srVector4& multiply_constant, const float* multiply_source);
void axpy(std::span<srVector4> destination, const srVector4& add_constant,
          const srVector4* multiply_vectors, const float* multiply_source);
void axpy(std::span<srVector4> destination, const srVector4* add_source,
          const srVector4* multiply_vectors, const float* multiply_source);
void axpy(std::span<srVector4> destination, const srVector4& add_constant,
          const srVector4& multiply_constant, const float* multiply_source_0,
          const float* multiply_source_1);
void axpy(std::span<srVector4> destination, const srVector4* add_source,
          const srVector4& multiply_constant, const float* multiply_source_0,
          const float* multiply_source_1);
void copyW(std::span<srVector4> destination, float constant);
void copyW(std::span<srVector4> destination, const float* source);
void copyW(std::span<float> destination, const srVector4* source);
void minMax(std::span<const srVector4> source, srVector4& minimum, srVector4& maximum);
void mul(srMatrix4& destination, const srMatrix4& source_0, const srMatrix4& source_1);
void mul(std::span<srMatrix4> destination, const srMatrix4* source_0, const srMatrix4* source_1);
int srTestBoundingBox(const srMatrix4& matrix, const srVector3& minimum, const srVector3& maximum);
void srSpecularPow(std::span<float> destination, const float* source, float exponent);
void srCopyIndexedRemap(std::span<srVector3i> destination, const srVector3i* source,
                        std::span<const SRDWORD> indices, const SRDWORD* remap);
void srSetIndexed(SRBYTE* destination, const srVector3i* source, std::span<const SRDWORD> indices);
SRDWORD srCollectNonZero(std::span<SRDWORD> destination, const SRBYTE* source);
void srRemapInverse(SRDWORD* destination, std::span<const SRDWORD> map);
SRDWORD srCullNoClip(std::span<SRDWORD> destination, const srVector4& constant,
                     const srVector4* vectors);
void srGetClipFlags(std::span<SRBYTE> destination, const srVector4* source);
} // namespace srMath

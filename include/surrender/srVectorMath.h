#pragma once

#include <span>

#include "srMath.h"

class srARGB;

using SRBYTE = unsigned char;
using SRDWORD = w8_ulong;
using srVector2 = srVector2T<float>;
using srVector3 = srVector3T<float>;
using srVector4 = srVector4T<float>;
using srMatrix4 = srMatrix4T<float>;

// Linear input spans and auxiliary outputs cover destination.size(). Indexed
// operations require destination.size() >= indices.size(); their source/remap
// arrays cover the referenced indices. minMax requires a nonempty source.
// Exact in-place elementwise operations are supported; partially overlapping
// input/output ranges are not.
// Matrix multiplication requires output distinct from both inputs.
namespace srMath {
void axpy(std::span<float> destination, std::span<const float> add_source,
          std::span<const float> scale_source, std::span<const float> multiply_source);
void add(std::span<float> destination, float constant, std::span<const float> source);
void sub(std::span<float> destination, float constant, std::span<const float> source);
void sub(std::span<srVector3> destination, std::span<const srVector3> source_0,
         std::span<const srVector3> source_1);
void mul(std::span<float> destination, float constant, std::span<const float> source);
void mul(std::span<float> destination, std::span<const float> source_0,
         std::span<const float> source_1);
void lerp(std::span<srVector3> destination, std::span<const srVector3> target,
          std::span<const srVector3> source, float constant);
void clampMin(std::span<float> destination, std::span<const float> source, float minimum);
void clampUnit(std::span<float> destination, std::span<const float> source);
void clampUnit(std::span<srVector4> destination, std::span<const srVector4> source);
void mulIndexed(std::span<float> destination, std::span<const float> linear_source,
                const float* indexed_source, std::span<const SRDWORD> indices);
bool isZero(std::span<const float> source);
void invPoly(std::span<float> destination, std::span<const float> source, const srVector3& poly);
void neg(std::span<float> destination, std::span<const float> source);
void copyIndexed(std::span<srVector2> destination, const srVector2* source,
                 std::span<const SRDWORD> indices);
void minMax(std::span<const srVector3> source, srVector3& minimum, srVector3& maximum);
void copy(std::span<srVector3> destination, std::span<const srVector4> source);
void add(std::span<srVector3> destination, const srVector3& constant,
         std::span<const srVector3> vector_source);
void sub(std::span<srVector3> destination, const srVector3& constant,
         std::span<const srVector3> vector_source);
void mul(std::span<srVector3> destination, const srVector3& constant,
         std::span<const srVector3> vector_source);
void mul(std::span<srVector3> destination, float constant, std::span<const srVector3> source);
void mul(std::span<srVector3> destination, std::span<const srVector3> vector_source,
         std::span<const float> float_source);
void length(std::span<float> destination, std::span<const srVector3> vectors);
void dot(std::span<float> destination, const srVector3& constant,
         std::span<const srVector3> vectors);
void dot(std::span<float> destination, const srVector4& constant,
         std::span<const srVector3> vectors);
void dot(std::span<float> destination, std::span<const srVector3> vectors_0,
         std::span<const srVector3> vectors_1);
void normalize(std::span<srVector3> destination, std::span<const srVector3> vectors, float length);
void copyIndexed(std::span<srVector3> destination, const srVector3* source,
                 std::span<const SRDWORD> indices);
void transform(std::span<srVector3> destination, std::span<const srVector3> vectors,
               const srMatrix4& matrix);
void dir(std::span<srVector3> destination, std::span<float> lengths,
         std::span<const srVector3> source);
void dir(std::span<srVector3> destination, std::span<float> lengths,
         std::span<const srVector4> source);
void add(std::span<srVector4> destination, const srVector4& constant,
         std::span<const srVector4> vector_source);
void mul(std::span<srVector4> destination, std::span<const srVector4> vector_source,
         std::span<const float> float_source);
void dot(std::span<float> destination, const srVector4& constant,
         std::span<const srVector4> vectors);
void dotIndexed(std::span<float> destination, const srVector4& constant, const srVector4* vectors,
                std::span<const SRDWORD> indices);
void mulIndexed(std::span<srVector4> destination, const srVector4& constant,
                const srVector4* indexed_source, std::span<const SRDWORD> indices);
void mulIndexed(std::span<srVector4> destination, std::span<const srVector4> linear_source,
                const srVector4* indexed_source, std::span<const SRDWORD> indices);
void copyIndexed(std::span<srVector4> destination, const srARGB* source,
                 std::span<const SRDWORD> indices);
void copyIndexed(std::span<srVector4> destination, const srVector4* source,
                 std::span<const SRDWORD> indices);
void copyIndexed(std::span<srVector4> destination, const srVector3* source,
                 std::span<const SRDWORD> indices);
void transformOrtho(std::span<srVector4> destination, std::span<const srVector4> source,
                    const srMatrix4& matrix);
void transformPerspective(std::span<srVector4> destination, std::span<const srVector4> source,
                          const srMatrix4& matrix);
void transform(std::span<srVector4> destination, std::span<const srVector4> vectors,
               const srMatrix4& matrix);
void transform(std::span<srVector4> destination, std::span<const srVector3> vectors,
               const srMatrix4& matrix);
void transformIndexed(std::span<srVector3> destination, const srVector3* source,
                      std::span<const SRDWORD> indices, const srMatrix4& matrix);
void transformIndexed(std::span<srVector4> destination, const srVector3* source,
                      std::span<const SRDWORD> indices, const srMatrix4& matrix);
void axpy(std::span<srVector4> destination, std::span<const srVector4> add_source,
          const srVector4& multiply_constant, std::span<const float> multiply_source);
void axpy(std::span<srVector4> destination, std::span<const srVector4> add_source,
          const srVector4& multiply_constant, std::span<const float> multiply_source_0,
          std::span<const float> multiply_source_1);
void copyW(std::span<srVector4> destination, float constant);
void copyW(std::span<srVector4> destination, std::span<const float> source);
void mul(srMatrix4& destination, const srMatrix4& source_0, const srMatrix4& source_1);
bool srTestBoundingBox(const srMatrix4& matrix, const srVector3& minimum, const srVector3& maximum);
void srSpecularPow(std::span<float> destination, std::span<const float> source, float exponent);
void srCopyIndexedRemap(std::span<srVector3i> destination, const srVector3i* source,
                        std::span<const SRDWORD> indices, const SRDWORD* remap);
void srSetIndexed(SRBYTE* destination, const srVector3i* source, std::span<const SRDWORD> indices);
SRDWORD srCollectNonZero(std::span<SRDWORD> destination, std::span<const SRBYTE> source);
void srRemapInverse(SRDWORD* destination, std::span<const SRDWORD> map);
SRDWORD srCullNoClip(std::span<SRDWORD> destination, const srVector4& constant,
                     std::span<const srVector4> vectors);
void srGetClipFlags(std::span<SRBYTE> destination, std::span<const srVector4> source);
} // namespace srMath

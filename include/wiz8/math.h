#pragma once

#include <cmath>
#include <cstdint>
#include <span>
#include <type_traits>

#include <fenv.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

/* Float-to-int through the FPU's current rounding mode (round to nearest, not truncation). */
inline w8_long roundedInt32(double value)
{
    const double rounded = rint(value);
    // Retail FISTP stores a signed dword. LP64 lrint instead has a 64-bit
    // range and narrowing its integer-indefinite result can produce zero.
    if (!(rounded >= -2147483648.0 && rounded <= 2147483647.0)) {
        feraiseexcept(FE_INVALID);
        return (-2147483647 - 1);
    }
    return static_cast<w8_long>(rounded);
}

inline w8_long roundedInt32(float value)
{
    return roundedInt32(static_cast<double>(value));
}

template<glm::length_t N, class T, glm::qualifier Q>
glm::vec<N, T, Q> normalizedOrZero(const glm::vec<N, T, Q>& value)
{
    return glm::dot(value, value) == T(0) ? value : glm::normalize(value);
}
template<glm::length_t N, class T, glm::qualifier Q> bool isFinite(const glm::vec<N, T, Q>& value)
{
    return !glm::any(glm::isnan(value)) && !glm::any(glm::isinf(value));
}

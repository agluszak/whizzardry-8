#pragma once

#include <algorithm>
#include <span>
#include "glm/common.hpp"

namespace srChannelMath {
inline void scale(std::span<float> values, float factor)
{
    if (values.empty() || factor == 1.0f) return;
    if (factor == 0.0f) std::ranges::fill(values, 0.0f);
    else for (float& value : values) value = factor * value;
}

inline void clamp(std::span<float> values)
{
    for (float& value : values) value = glm::clamp(value, 0.0f, 1.0f);
}

// Keep normalization, saturation, and amplitude as separate passes: combining
// the factors would change values outside the range and zero/NaN behavior.
inline void normalize(std::span<float> values, float normalization, float amplitude)
{
    scale(values, normalization);
    clamp(values);
    scale(values, amplitude);
}
}

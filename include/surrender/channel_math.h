#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <span>

#include "glm/common.hpp"

namespace srChannelMath {
inline void scale(std::span<float> values, float factor)
{
    if (values.empty() || factor == 1.0f) return;
    if (factor == 0.0f)
        std::ranges::fill(values, 0.0f);
    else
        for (float& value : values) value = factor * value;
}

inline bool allZero(std::span<const float> values)
{
    return std::ranges::all_of(values, [](float value) { return value == 0.0f; });
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

enum class RampDirection { increasing, decreasing };

inline void distanceRamp(std::span<float> destination, std::span<const float> distances,
                         float origin, float normalization, float amplitude,
                         RampDirection direction)
{
    assert(distances.size() >= destination.size());
    for (std::size_t i = 0; i < destination.size(); ++i)
        destination[i] = direction == RampDirection::increasing
                             ? (origin == 0 ? distances[i] : -origin + distances[i])
                             : (origin == 0 ? -distances[i] : origin - distances[i]);
    normalize(destination, normalization, amplitude);
}

inline void multiply(std::span<float> destination, std::span<const float> factors)
{
    assert(factors.size() >= destination.size());
    for (std::size_t i = 0; i < destination.size(); ++i) destination[i] *= factors[i];
}

inline void add(std::span<float> values, float offset)
{
    if (offset != 0)
        for (float& value : values) value = offset + value;
}

}

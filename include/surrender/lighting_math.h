#pragma once

#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include "glm/common.hpp"
#include "glm/geometric.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"

namespace srLightingMath {
enum class ZeroLengthDirection { preserve, divide };
struct DirectionDistance {
    glm::vec3 direction;
    float distance;
};

inline DirectionDistance
directionAndDistance(const glm::vec3& vector,
                     ZeroLengthDirection zero = ZeroLengthDirection::preserve)
{
    const float distance = glm::length(vector);
    return {distance == 0 && zero == ZeroLengthDirection::preserve ? vector : vector / distance,
            distance};
}

// Keep the native light exponent ceiling; standard power retains faint values
// and supports sub-unit exponents without indexing a recovered approximation table.
inline void power(std::span<float> destination, std::span<const float> source, float exponent)
{
    assert(source.size() >= destination.size());
    exponent = std::min(exponent, 127.0f);
    std::transform(source.begin(), source.begin() + destination.size(), destination.begin(),
                   [exponent](float value) { return std::pow(value, exponent); });
}

enum class DotRange { nonnegative, unit };

// One direction broadcasts a directional light; otherwise use one per normal.
inline void normalDots(std::span<float> destination, std::span<const glm::vec3> normals,
                       std::span<const glm::vec3> directions, DotRange range)
{
    assert(normals.size() >= destination.size());
    assert(directions.size() == 1 || directions.size() >= destination.size());
    for (std::size_t i = 0; i < destination.size(); ++i) {
        const float dot = glm::dot(normals[i], directions[directions.size() == 1 ? 0 : i]);
        destination[i] = range == DotRange::unit ? glm::clamp(dot, 0.0f, 1.0f)
                         : dot <= 0              ? 0.0f
                                                 : dot;
    }
}

// Keep attenuation and angular weights separate to retain highlight rounding.
template <glm::length_t Components>
inline void accumulateLight(std::span<glm::vec<Components, float>> destination,
                            const glm::vec<Components, float>& color,
                            std::span<const float> weights = {},
                            std::span<const float> attenuation = {})
{
    assert(weights.empty() || weights.size() >= destination.size());
    assert(attenuation.empty() || attenuation.size() >= destination.size());
    for (std::size_t i = 0; i < destination.size(); ++i) {
        glm::vec<Components, float> contribution = color;
        if (!attenuation.empty()) {
            contribution *= attenuation[i];
        }
        if (!weights.empty()) {
            contribution *= weights[i];
        }
        destination[i] = contribution + destination[i];
    }
}

} // namespace srLightingMath

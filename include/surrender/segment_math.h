#pragma once

#include "glm/geometric.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"

namespace srSegmentMath {
template<glm::length_t Dimensions> struct Projection {
    glm::vec<Dimensions, float> point;
    float fraction;
    float distance;
};

// Share the segment projection in two and three dimensions. Explicit ordered
// comparisons retain the endpoint fallback for a degenerate segment or NaN.
template<glm::length_t Dimensions>
Projection<Dimensions> project(const glm::vec<Dimensions, float>& point,
                               const glm::vec<Dimensions, float>& from,
                               const glm::vec<Dimensions, float>& to)
{
    const auto direction = to - from;
    const auto offset = point - from;
    const float fraction = glm::dot(offset, direction) / glm::dot(direction, direction);
    auto remaining = offset;
    if (fraction > 0) {
        if (fraction <= 1) {
            remaining -= direction * fraction;
        } else {
            remaining = point - to;
        }
    }
    if (!(fraction >= 0)) {
        return {from, 0, glm::length(remaining)};
    }
    if (fraction > 1) {
        return {to, 1, glm::length(remaining)};
    }
    return {from + direction * fraction, fraction, glm::length(remaining)};
}
} // namespace srSegmentMath

#pragma once

#include "glm/ext/vector_float3.hpp"
#include "glm/geometric.hpp"
#include "surrender/srBoundingBox.h"

namespace srSegmentMath {
/* Match collision-query expansion: the margin is at least the motion length.
   Keep subtraction/addition order at each end to retain boundary rounding. */
inline srBoundingBox sweptBounds(const glm::vec3& origin, const glm::vec3& delta, float extent)
{
    const float length = glm::length(delta);
    if (length > extent) {
        extent = length;
    }
    srBoundingBox bounds;
    for (int axis = 0; axis < 3; ++axis) {
        if (delta[axis] > 0) {
            bounds.minimum[axis] = origin[axis] - extent;
            bounds.maximum[axis] = extent + origin[axis] + delta[axis];
        } else {
            bounds.minimum[axis] = origin[axis] - extent + delta[axis];
            bounds.maximum[axis] = extent + origin[axis];
        }
    }
    return bounds;
}
} // namespace srSegmentMath

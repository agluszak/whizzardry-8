#pragma once

#include "surrender/srMath.h"
#include <cmath>

struct DirectionAngles {
    float yaw;
    float pitch;
};

// Forward is +Z; positive pitch turns down about +X.
inline DirectionAngles directionAngles(const glm::vec3& direction)
{
    if (!isFinite(direction)) {
        return {};
    }
    return {std::atan2(direction.x, direction.z),
            std::atan2(-direction.y, glm::length(glm::vec2(direction.x, direction.z)))};
}

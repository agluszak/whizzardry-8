#pragma once

#include <cmath>
#include <optional>
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/float_constants.h"

namespace wiz8::placement {
// Grid coordinates are forward and lateral distances measured in cells.
struct Grid {
    glm::vec2 forward;
    glm::vec2 lateral;

    Grid(float angle, float spacing)
    {
        const float cosine = spacing * std::cos(static_cast<double>(angle));
        const float sine = spacing * std::sin(static_cast<double>(angle));
        forward = {sine, cosine};
        lateral = {-cosine, sine};
    }

    glm::vec3 candidate(const glm::vec3& source, const glm::vec2& cell) const
    {
        const auto offset = forward * cell.x + lateral * cell.y;
        return {source.x + offset.x, source.y + g_world_scale, source.z + offset.y};
    }
};

inline std::optional<glm::vec3> grounded(const glm::vec3& candidate, float ground,
                                        bool allow_large_drop)
{
    const float height = std::abs(candidate.y - ground);
    if (!(height >= 1 && height <= g_navigator_vertical_snap_limit &&
          (allow_large_drop || height <= g_double_twenty_five_hundred)))
        return std::nullopt;
    return glm::vec3(candidate.x, ground, candidate.z);
}
} // namespace wiz8::placement

#pragma once

#include <array>
#include <optional>

#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "surrender/srMath.h"

// A set neighbor bit is open; mask zero denotes an entirely open neighborhood.
inline std::optional<glm::vec3> slideDirection(unsigned mask, const glm::vec3& delta)
{
    if (mask == 0) return std::nullopt;
    unsigned wanted = delta.x > 0 ? 0x0e : delta.x < 0 ? 0xe0 : 0;
    wanted |= delta.z > 0 ? 0x83 : delta.z < 0 ? 0x38 : 0;
    const auto blocked = ~mask & wanted;
    if (blocked == 0) return std::nullopt;
    constexpr float diagonal = 0.7071070075035095f;
    static const std::array<glm::vec2, 8> directions{{{0, -1},
                                                      {-diagonal, -diagonal},
                                                      {-1, 0},
                                                      {-diagonal, diagonal},
                                                      {0, 1},
                                                      {diagonal, diagonal},
                                                      {1, 0},
                                                      {diagonal, -diagonal}}};
    glm::vec2 sum(0);
    for (unsigned bit = 0; bit < directions.size(); ++bit)
        if (blocked & (1u << bit)) sum += directions[bit];
    const auto direction = normalizedOrZero(sum);
    return glm::vec3(direction.x, 0, direction.y);
}

#pragma once

#include <cstdint>

#include "wiz8/math.h"

// CPU visibility algorithms use the renderer's homogeneous clip coordinates.
namespace clipMath {
bool testBoundingBox(const glm::mat4& matrix, const glm::vec3& minimum, const glm::vec3& maximum);
std::uint32_t collectOutside(std::span<std::uint32_t> destination, const glm::vec4& plane,
                             std::span<const glm::vec4> vertices);
void clipFlags(std::span<std::uint8_t> destination, std::span<const glm::vec4> vertices);
} // namespace clipMath

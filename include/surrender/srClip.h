#pragma once

#include "srMath.h"

// CPU visibility algorithms use the renderer's homogeneous clip coordinates.
namespace srRender {
bool testBoundingBox(const glm::mat4& matrix, const glm::vec3& minimum, const glm::vec3& maximum);
SRDWORD collectOutside(std::span<SRDWORD> destination, const glm::vec4& plane, std::span<const glm::vec4> vertices);
void clipFlags(std::span<SRBYTE> destination, std::span<const glm::vec4> vertices);
} // namespace srRender

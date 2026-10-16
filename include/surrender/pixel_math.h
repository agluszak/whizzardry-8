#pragma once

#include <cstdint>

#include "glm/ext/vector_uint4.hpp"

namespace srPixels {

// Runtime channel order is RGBA; the packed word remains 0xAARRGGBB.
inline glm::uvec4 channels(std::uint32_t pixel)
{
    return {(pixel >> 16) & 255, (pixel >> 8) & 255, pixel & 255, pixel >> 24};
}

inline std::uint32_t pack(const glm::uvec4& color)
{
    return color.r << 16 | color.g << 8 | color.b | color.a << 24;
}

} // namespace srPixels

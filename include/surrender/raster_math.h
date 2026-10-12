#pragma once

#include <cstdint>
#include "glm/common.hpp"
#include "glm/ext/vector_int2.hpp"
#include <SDL3/SDL_rect.h>
#include <algorithm>
#include <optional>

// Axis-aligned lines include their first pixel and exclude their last endpoint.
inline std::optional<SDL_Rect> axisLineRectangle(std::int32_t fixed, std::int32_t begin, std::int32_t end,
                                                bool vertical, const glm::ivec2& dimensions)
{
    const std::int32_t fixed_limit = vertical ? dimensions.x : dimensions.y;
    if (fixed < 0 || fixed >= fixed_limit) return std::nullopt;
    const std::int32_t variable_limit = vertical ? dimensions.y : dimensions.x;
    const auto interval = glm::clamp(glm::ivec2(std::min(begin, end), std::max(begin, end)),
                                     0, variable_limit);
    if (interval.x >= interval.y) return std::nullopt;
    const auto count = interval.y - interval.x;
    return vertical ? SDL_Rect{fixed, interval.x, 1, count}
                    : SDL_Rect{interval.x, fixed, count, 1};
}

#pragma once

#include <algorithm>
#include <cmath>
#include <ranges>

#include "glm/geometric.hpp"

// Accumulate in the center's scalar precision; padding belongs to the caller.
template<class Point, std::ranges::input_range Range>
inline typename Point::value_type boundingRadius(Range&& points, const Point& center)
{
    using Scalar = typename Point::value_type;
    Scalar maximum = 0;
    for (const auto& point : points) {
        const auto offset = Point(point) - center;
        maximum = std::max(maximum, glm::dot(offset, offset));
    }
    return std::sqrt(maximum);
}

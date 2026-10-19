#pragma once

#include <array>
#include <span>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/math.h"

struct BoundingBox {
    glm::vec3 minimum{0};
    glm::vec3 maximum{0};

    void include(const glm::vec3& point)
    {
        minimum = glm::min(minimum, point);
        maximum = glm::max(maximum, point);
    }

    void include(const BoundingBox& box)
    {
        include(box.minimum);
        include(box.maximum);
    }

    bool contains(const glm::vec3& point) const
    {
        return glm::all(glm::greaterThanEqual(point, minimum)) &&
               glm::all(glm::lessThanEqual(point, maximum));
    }

    // Octree point ownership excludes the maximum face.
    bool containsHalfOpen(const glm::vec3& point) const
    {
        return glm::all(glm::greaterThanEqual(point, minimum)) &&
               glm::all(glm::lessThan(point, maximum));
    }

    bool overlaps(const BoundingBox& box) const
    {
        return glm::all(glm::greaterThanEqual(maximum, box.minimum)) &&
               glm::all(glm::greaterThanEqual(box.maximum, minimum));
    }

    bool intersectsSphere(const glm::vec3& center, float radius) const
    {
        if (!(radius >= 0)) return false;
        const auto offset = center - glm::clamp(center, minimum, maximum);
        return glm::dot(offset, offset) <= radius * radius;
    }

    glm::vec3 center() const { return (minimum + maximum) * 0.5f; }

    glm::vec3 corner(unsigned index) const
    {
        return glm::mix(minimum, maximum, glm::bvec3(index & 4, index & 2, index & 1));
    }

    // Visibility probes test the midpoint first, then these ordered corners.
    std::array<glm::vec3, 9> visibilitySamples() const
    {
        return {center(),  corner(7), corner(0), corner(3), corner(2),
                corner(6), corner(5), corner(1), corner(4)};
    }

    template<class TransformPoint>
    BoundingBox transformedBy(TransformPoint transform) const
    {
        const glm::vec3 first = transform(minimum);
        BoundingBox result{first, first};
        for (unsigned corner = 1; corner < 8; ++corner) {
            result.include(transform(this->corner(corner)));
        }
        return result;
    }

    BoundingBox transformed(const glm::mat4& matrix) const
    {
        return transformedBy(
            [&](const glm::vec3& point) { return glm::vec3(matrix * glm::vec4(point, 1)); });
    }
};

template<class Point>
inline BoundingBox boundingBox(std::span<const Point> points)
{
    if (points.empty()) return {};
    const auto coordinates = [](const Point& point) {
        return glm::vec3(point.x, point.y, point.z);
    };
    const auto first = coordinates(points.front());
    BoundingBox result{first, first};
    for (const auto& point : points.subspan(1)) result.include(coordinates(point));
    return result;
}

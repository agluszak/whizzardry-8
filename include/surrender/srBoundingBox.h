#pragma once

#include "srMath.h"
#include <span>

struct srBoundingBox {
    glm::vec3 minimum{0};
    glm::vec3 maximum{0};

    void include(const glm::vec3& point)
    {
        minimum = glm::min(minimum, point);
        maximum = glm::max(maximum, point);
    }

    void include(const srBoundingBox& box)
    {
        include(box.minimum);
        include(box.maximum);
    }

    bool contains(const glm::vec3& point) const
    {
        return glm::all(glm::greaterThanEqual(point, minimum)) &&
               glm::all(glm::lessThanEqual(point, maximum));
    }

    bool overlaps(const srBoundingBox& box) const
    {
        return glm::all(glm::greaterThanEqual(maximum, box.minimum)) &&
               glm::all(glm::greaterThanEqual(box.maximum, minimum));
    }

    glm::vec3 center() const
    {
        return (minimum + maximum) * 0.5f;
    }

    srBoundingBox transformed(const glm::mat4& matrix) const
    {
        const glm::vec3 first(matrix * glm::vec4(minimum, 1));
        srBoundingBox result{first, first};
        for (unsigned corner = 1; corner < 8; ++corner) {
            const auto point =
                glm::mix(minimum, maximum, glm::bvec3(corner & 4, corner & 2, corner & 1));
            result.include(glm::vec3(matrix * glm::vec4(point, 1)));
        }
        return result;
    }
};

inline srBoundingBox boundingBox(std::span<const srVector3> points)
{
    if (points.empty())
        return {};
    const auto first = points.front().glmValue();
    srBoundingBox result{first, first};
    for (const auto& point : points.subspan(1))
        result.include(point.glmValue());
    return result;
}

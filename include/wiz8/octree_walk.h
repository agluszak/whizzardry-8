#pragma once

#include <cstdint>
#include "glm/ext/vector_int3.hpp"
#include "glm/common.hpp"
#include "glm/vector_relational.hpp"

// Inclusive x/y/z traversal; query callers choose whether to clip the range.
// Break at the endpoint before incrementing, including at INT_MAX.
template<class Visit>
void visitOctreeCellBox(const glm::ivec3& first, const glm::ivec3& last, Visit visit)
{
    if (glm::any(glm::greaterThan(first, last))) return;
    glm::ivec3 cell = first;
    for (;;) {
        cell.y = first.y;
        for (;;) {
            cell.z = first.z;
            for (;;) {
                visit(cell);
                if (cell.z == last.z) break;
                ++cell.z;
            }
            if (cell.y == last.y) break;
            ++cell.y;
        }
        if (cell.x == last.x) break;
        ++cell.x;
    }
}

template<class Visit>
void visitClippedOctreeCellBox(const glm::ivec3& first, const glm::ivec3& last,
                               const glm::ivec3& dimensions, Visit visit)
{
    visitOctreeCellBox(glm::max(first, glm::ivec3(0)),
                       glm::min(last, dimensions - 1), visit);
}

/* Runtime state for the fixed-point 3D Bresenham walk. Axis ties retain the
   first axis; ties between the errors visit minor_axis1 before minor_axis0. */
struct W8OctreeWalk {
    glm::ivec3 cell;
    glm::ivec3 step;
    std::int32_t major_axis;
    std::int32_t minor_axis0;
    std::int32_t minor_axis1;
    std::int32_t count;
    std::int32_t error_delta0;
    std::int32_t error0;
    std::int32_t error_reset0;
    std::int32_t error_delta1;
    std::int32_t error1;
    std::int32_t error_reset1;

    /* Return false from visit to stop at the first blocking cell. A copy of
       the cursor keeps the walk reusable and callers' starting cells intact. */
    template<class Visit>
    void visitCells(Visit visit) const
    {
        auto current = cell;
        std::int32_t error_a = error0, error_b = error1;
        for (std::int32_t index = 0; index < count; ++index) {
            if (!visit(current)) return;
            const auto step_minor = [&](std::int32_t axis, std::int32_t& error, std::int32_t reset) {
                current[axis] += step[axis];
                error += reset;
                return visit(current);
            };
            if (error_a < error_b) {
                if (error_a < 0) {
                    if (!step_minor(minor_axis0, error_a, error_reset0)) return;
                    if (error_b < 0 && !step_minor(minor_axis1, error_b, error_reset1)) return;
                }
            } else if (error_b < 0) {
                if (!step_minor(minor_axis1, error_b, error_reset1)) return;
                if (error_a < 0 && !step_minor(minor_axis0, error_a, error_reset0)) return;
            }
            current[major_axis] += step[major_axis];
            error_a -= error_delta0;
            error_b -= error_delta1;
        }
    }
};

// Adjacent queries probe their quantized endpoints directly. Longer queries
// lazily construct the fixed-point walk and can stop at the first blocked cell.
template<class MakeWalk, class Visit>
void visitOctreeSegment(const glm::ivec3& first, const glm::ivec3& last,
                        MakeWalk makeWalk, Visit visit)
{
    const auto delta = glm::abs(last - first);
    const auto distance = delta.x + delta.y + delta.z;
    if (distance < 2) {
        if (visit(first, true) && distance != 0) visit(last, true);
    } else {
        makeWalk().visitCells([&](const glm::ivec3& cell) { return visit(cell, false); });
    }
}

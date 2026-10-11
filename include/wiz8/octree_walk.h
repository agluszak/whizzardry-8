#pragma once

#include "glm/ext/vector_int3.hpp"
#include "glm/common.hpp"

/* Runtime state for the fixed-point 3D Bresenham walk. Axis ties retain the
   first axis; ties between the errors visit minor_axis1 before minor_axis0. */
struct W8OctreeWalk {
    glm::ivec3 cell;
    glm::ivec3 step;
    int major_axis;
    int minor_axis0;
    int minor_axis1;
    int count;
    int error_delta0;
    int error0;
    int error_reset0;
    int error_delta1;
    int error1;
    int error_reset1;

    /* Return false from visit to stop at the first blocking cell. A copy of
       the cursor keeps the walk reusable and callers' starting cells intact. */
    template<class Visit>
    void visitCells(Visit visit) const
    {
        auto current = cell;
        int error_a = error0, error_b = error1;
        for (int index = 0; index < count; ++index) {
            if (!visit(current)) return;
            const auto step_minor = [&](int axis, int& error, int reset) {
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

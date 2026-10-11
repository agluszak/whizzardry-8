#include <algorithm>
#include <vector>

#include "surrender/srTriangulator.h"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_int3.hpp"

// FUNCTION: SURRENDER 0x1003bea0
srTriangulator::srTriangulator(glm::vec2* points, int count)
    : nodes(std::max(count, 0)), remaining(count), points(points)
{
    // Circular links from SURRENDER 0x1003c1e0. Storage never changes during clipping.
    for (int i = 0; i < count; ++i) {
        nodes[i].index = i;
        nodes[i].prev = &nodes[(i + count - 1) % count];
        nodes[i].next = &nodes[(i + 1) % count];
    }
    if (!nodes.empty()) current = nodes.data();
}

// FUNCTION: SURRENDER 0x1003c0e0
glm::ivec3 srTriangulator::next()
{
    if (remaining > 3) {
        const auto* start = current;
        while (!satisfyConstraints(current)) {
            current = current->next;
            if (current == start)
                return {-1, 0, 0};
        }
        auto* previous = current->prev;
        auto* next = current->next;
        const glm::ivec3 result(previous->index, current->index, next->index);
        next->prev = previous;
        previous->next = next;
        --remaining;
        current = previous;
        return result;
    }
    if (remaining == 3) {
        remaining = 0;
        return {current->index, current->next->index, current->next->next->index};
    }
    return {-1, 0, 0};
}

// FUNCTION: SURRENDER 0x1003bfa0
int srTriangulator::satisfyConstraints(const Node* vertex)
{
    glm::vec2 a = points[vertex->prev->index];
    glm::vec2 b = points[vertex->index];
    glm::vec2 c = points[vertex->next->index];
    if ((c.y - b.y) * (a.x - b.x) - (a.y - b.y) * (c.x - b.x) >= 0.0) {
        return 0;
    }
    Node* node = vertex->next->next;
    while (!isInsideTriangle(points[node->index], a, b, c)) {
        node = node->next;
        if (node == vertex->prev) {
            return 1;
        }
    }
    return 0;
}

// FUNCTION: SURRENDER 0x1003bee0
int srTriangulator::sameSide(const glm::vec2& first_point, const glm::vec2& second_point,
                             const glm::vec2& a, const glm::vec2& b)
{
    float first = (first_point.x - a.x) * (b.y - a.y) - (first_point.y - a.y) * (b.x - a.x);
    float second = (second_point.x - a.x) * (b.y - a.y) - (second_point.y - a.y) * (b.x - a.x);
    return first * second >= 0.0f;
}

// FUNCTION: SURRENDER 0x1003bf40
int srTriangulator::isInsideTriangle(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b,
                                     const glm::vec2& c)
{
    return sameSide(p, a, b, c) && sameSide(p, b, a, c) && sameSide(p, c, a, b);
}

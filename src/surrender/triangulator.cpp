#include <cstdint>
#include "surrender/triangle_projection.h"
#include <array>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <span>
#include <optional>
#include <vector>

#include "surrender/srTriangulator.h"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_int3.hpp"

// FUNCTION: SURRENDER 0x1003bea0
srTriangulator::srTriangulator(std::span<const glm::vec2> points)
    : remaining(0), points(points)
{
    if (points.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        throw std::length_error("Too many triangulation points");
    remaining = static_cast<std::int32_t>(points.size());
    nodes.resize(points.size());
    const std::int32_t count = remaining;
    // Circular links from SURRENDER 0x1003c1e0. Storage never changes during clipping.
    for (std::int32_t i = 0; i < count; ++i) {
        nodes[i].index = i;
        nodes[i].prev = &nodes[(i + count - 1) % count];
        nodes[i].next = &nodes[(i + 1) % count];
    }
    if (!nodes.empty()) current = nodes.data();
}

// FUNCTION: SURRENDER 0x1003c0e0
std::optional<glm::ivec3> srTriangulator::next()
{
    if (remaining > 3) {
        const auto* start = current;
        while (!satisfyConstraints(current)) {
            current = current->next;
            if (current == start)
                return std::nullopt;
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
        return glm::ivec3(current->index, current->next->index, current->next->next->index);
    }
    return std::nullopt;
}

// FUNCTION: SURRENDER 0x1003bfa0
std::int32_t srTriangulator::satisfyConstraints(const Node* vertex)
{
    glm::vec2 a = points[vertex->prev->index];
    glm::vec2 b = points[vertex->index];
    glm::vec2 c = points[vertex->next->index];
    if (srTriangleProjection::orientedArea(a, b, c) <= 0) {
        return 0;
    }
    Node* node = vertex->next->next;
    while (!srTriangleProjection::containsClosed(std::array{a, b, c}, points[node->index])) {
        node = node->next;
        if (node == vertex->prev) {
            return 1;
        }
    }
    return 0;
}

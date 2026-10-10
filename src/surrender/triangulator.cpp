#include "surrender/srTriangulator.h"

#include <algorithm>

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
srVector3i srTriangulator::next()
{
    srVector3i result;
    if (remaining > 3) {
        Node* start = current;
        while (!satisfyConstraints(current)) {
            current = current->next;
            if (current == start) {
                result.x = -1;
                result.y = 0;
                result.z = 0;
                return result;
            }
        }
        Node* previous = current->prev;
        Node* next_node = current->next;
        result.x = previous->index;
        result.y = current->index;
        result.z = next_node->index;
        // Unlink the ear (SURRENDER 0x1003c270), without erasing its storage.
        next_node->prev = previous;
        previous->next = next_node;
        --remaining;
        current = previous;
        return result;
    }
    if (remaining == 3) {
        Node* next_node = current->next;
        result.x = current->index;
        result.y = next_node->index;
        result.z = next_node->next->index;
        remaining = 0;
        return result;
    }
    result.x = -1;
    result.y = 0;
    result.z = 0;
    return result;
}

// FUNCTION: SURRENDER 0x1003bfa0
int srTriangulator::satisfyConstraints(const Node* vertex)
{
    glm::vec2 a = points[vertex->prev->index];
    glm::vec2 b = points[vertex->index];
    glm::vec2 c = points[vertex->next->index];
    if ((c.y - b.y) * (a.x - b.x) - (a.y - b.y) * (c.x - b.x) <= 0.0) {
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
    return first * second > 0.0f;
}

// FUNCTION: SURRENDER 0x1003bf40
int srTriangulator::isInsideTriangle(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b,
                                     const glm::vec2& c)
{
    return sameSide(p, a, b, c) && sameSide(p, b, a, c) && sameSide(p, c, a, b);
}

#pragma once

#include "srMath.h"
#include <vector>

class srTriangulator {
public:
    srTriangulator(glm::vec2* points, int count);
    srTriangulator(const srTriangulator&) = delete;
    srTriangulator& operator=(const srTriangulator&) = delete;

    srVector3i next();

private:
    struct Node {
        w8_long index;
        Node* next;
        Node* prev;
    };

    int sameSide(const glm::vec2& first_point, const glm::vec2& second_point, const glm::vec2& a,
                 const glm::vec2& b);
    int isInsideTriangle(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b,
                         const glm::vec2& c);
    int satisfyConstraints(const Node* vertex);

    // Sized once: clipping only unlinks nodes, preserving every node address.
    std::vector<Node> nodes;
    Node* current = nullptr;
    int remaining;
    const glm::vec2* points;
};


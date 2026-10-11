#pragma once

#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_int3.hpp"
#include <vector>
#include <optional>
#include <span>

class srTriangulator {
public:
    explicit srTriangulator(std::span<const glm::vec2> points);
    srTriangulator(const srTriangulator&) = delete;
    srTriangulator& operator=(const srTriangulator&) = delete;

    std::optional<glm::ivec3> next();

private:
    struct Node {
        int index;
        Node* next;
        Node* prev;
    };

    int satisfyConstraints(const Node* vertex);

    // Sized once: clipping only unlinks nodes, preserving every node address.
    std::vector<Node> nodes;
    Node* current = nullptr;
    int remaining;
    std::span<const glm::vec2> points;
};


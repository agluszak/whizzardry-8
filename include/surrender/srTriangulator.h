#pragma once

#include "srMath.h"
#include <vector>

class srTriangulator {
public:
    srTriangulator(srVector2T<float>* points, int count);
    srTriangulator(const srTriangulator&) = delete;
    srTriangulator& operator=(const srTriangulator&) = delete;

    srVector3i next();

private:
    struct Node {
        w8_long index;
        Node* next;
        Node* prev;
    };
    W8_ABI_ASSERT(sizeof(Node) == 0xc, "CircularList_Node_must_be_0xc");

    int sameSide(const srVector2T<float>& first_point, const srVector2T<float>& second_point,
                 const srVector2T<float>& a, const srVector2T<float>& b);
    int isInsideTriangle(const srVector2T<float>& p, const srVector2T<float>& a,
                         const srVector2T<float>& b, const srVector2T<float>& c);
    int satisfyConstraints(const Node* vertex);

    // Sized once: clipping only unlinks nodes, preserving every node address.
    std::vector<Node> nodes;
    Node* current = nullptr;
    int remaining;
    const srVector2T<float>* points;
};

W8_ABI_ASSERT((sizeof(srTriangulator) == 0x10), "srTriangulator_must_be_0x10");

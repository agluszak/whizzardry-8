#pragma once

#include "wiz8/engine_code/OctBuildTree.h"
#include "wiz8/engine_code/OctPreTree.h"

// Both triangle inserters visit overlapping octants in x/y/z order. The callers
// retain their own depth limits, overlap inputs, node ownership, and leaf actions.
template<class Overlaps, class CreateNode, class InsertChild>
bool InsertBuildSurfaceChildren(W8OctSpatialState* parent, W8OctSpatialState& child,
                                float extent, Overlaps overlaps, CreateNode create_node,
                                InsertChild insert_child)
{
    bool inserted = false;
    int octant = 0;
    for (int x = 0; x != 2; ++x) {
        for (int y = 0; y != 2; ++y) {
            for (int z = 0; z != 2; ++z, ++octant) {
                child.m_minimum.x = parent->m_minimum.x + x * extent;
                child.m_maximum.x = child.m_minimum.x + extent;
                child.m_minimum.y = parent->m_minimum.y + y * extent;
                child.m_maximum.y = child.m_minimum.y + extent;
                child.m_minimum.z = parent->m_minimum.z + z * extent;
                child.m_maximum.z = child.m_minimum.z + extent;
                if (!overlaps(child)) continue;
                auto*& node = parent->m_root->children[octant];
                if (!node) node = create_node();
                child.m_root = node;
                if (insert_child(child)) inserted = true;
            }
        }
    }
    return inserted;
}

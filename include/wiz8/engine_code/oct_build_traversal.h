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
    for (unsigned octant = 0; octant < 8; ++octant) {
        child.setChildBounds(*parent, octant, extent);
        if (!overlaps(child)) continue;
        auto*& node = parent->m_root->children[octant];
        if (!node) node = create_node();
        child.m_root = node;
        if (insert_child(child)) inserted = true;
    }
    return inserted;
}

// Existing-child walks borrow the parent's nodes and keep x/y/z visitation order.
template<class Visit>
void visitBuildChildren(const W8OctSpatialState& parent, W8OctSpatialState& child, Visit visit)
{
    for (unsigned octant = 0; octant < 8; ++octant) {
        if (auto* node = parent.m_root->children[octant]) {
            child.setChildBounds(parent, octant, child.m_extent);
            child.m_root = node;
            visit(child, octant);
        }
    }
}

#include <ostream>

#include "glm/common.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/geometric.hpp"
#include "glm/matrix.hpp"
#include "surrender/srBounder.h"
#include "surrender/srBoundingBox.h"
#include "surrender/srGERD.h"
#include "surrender/srNode.h"
#include "surrender/srTypeRegistry.h"
#include "surrender/stream_scope.h"

// FUNCTION: SURRENDER 0x1004a5a0
srBounder::srBounder(srNode* parent)
    : srClassSupport<srBounder, srNode, 0x1600>(static_cast<srNode*>(0))
{
    bound_mode = BOUND_MODE_DYNAMIC;
    bounds.state = 2;
    if (parent != 0) {
        setParent(parent, 0);
    }
    setNotify(NOTIFY_BOUNDS_DIRTY);
}

// FUNCTION: SURRENDER 0x1004b1a0
srBounder::e_boundMode srBounder::getBoundMode() const
{
    return bound_mode;
}

// FUNCTION: SURRENDER 0x1004b160
void srBounder::setBoundMode(e_boundMode mode)
{
    if (testNotify(NOTIFY_BOUNDS_DIRTY)) {
        updateBounds();
    }
    bound_mode = mode;
}

// FUNCTION: SURRENDER 0x1004b120
void srBounder::setBounds(const BoundInfo& bounds)
{
    if (bound_mode == BOUND_MODE_DYNAMIC) {
        if (testNotify(NOTIFY_BOUNDS_DIRTY)) {
            updateBounds();
        }
    }
    this->bounds = bounds;
}

// FUNCTION: SURRENDER 0x1004b1b0
void srBounder::checkBounds()
{
    if (bound_mode == BOUND_MODE_DYNAMIC) {
        if (testNotify(NOTIFY_BOUNDS_DIRTY)) {
            updateBounds();
        }
    }
}

// FUNCTION: SURRENDER 0x1004aa00
void srBounder::forceUpdateBounds()
{
    e_boundMode mode;

    setNotify(NOTIFY_BOUNDS_DIRTY);
    mode = bound_mode;
    bound_mode = BOUND_MODE_DYNAMIC;
    updateBounds();
    bound_mode = mode;
}

// FUNCTION: SURRENDER 0x1004A690
srBounder& srBounder::operator=(const srBounder& other)
{
    if (this != &other) {
        srNode::operator=(other);
        bound_mode = other.bound_mode;
        bounds = other.bounds;
        setNotify(NOTIFY_BOUNDS_DIRTY);
    }
    return *this;
}

// FUNCTION: SURRENDER 0x1004A6E0
void srBounder::process(const ProcessInfo& info, e_processType type) {}

// FUNCTION: SURRENDER 0x1004A6F0
void srBounder::traverse(TraverseInfo& info)
{
    if (next_sibling_ != 0) {
        next_sibling_->traverse(info);
    }
    if (testFlag(FLAG_TERMINATE) == 0) {
        srGERD* renderer = info.renderer;
        if (testFlag(FLAG_DISABLE) == 0 && renderer != 0 && first_child_ != 0) {
            if (bound_mode == BOUND_MODE_DYNAMIC && testNotify(NOTIFY_BOUNDS_DIRTY) != 0) {
                updateBounds();
            }
            if (bounds.state == 0) {
                return;
            }
            if (bounds.state == 1) {
                const auto transform = scopedWorldSpaceMatrix(*renderer);
                if (renderer->testBoundingSphere(bounds.center, bounds.radius) ==
                    srGERD::VISIBILITY_OUTSIDE) {
                    return;
                }
                if (renderer->testBoundingBox(bounds.minimum, bounds.maximum) ==
                    srGERD::VISIBILITY_OUTSIDE) {
                    return;
                }
            }
        }
        if (first_child_ != 0) {
            first_child_->traverse(info);
        }
    }
}

// FUNCTION: SURRENDER 0x1004A800
void srBounder::updateBounds()
{
    if (testNotify(NOTIFY_BOUNDS_DIRTY) != 0 && bound_mode == BOUND_MODE_DYNAMIC) {
        bounds.minimum = glm::vec3(0);
        bounds.maximum = glm::vec3(0);
        bounds.center = glm::vec3(0);
        bounds.radius = 0.0f;
        bounds.state = 0;
        glm::mat4 world;
        world = getWorldSpaceMatrix<glm::mat4>();
        inverse_world = glm::inverse(world);
        for (srNode* child = first_child_; child != 0; child = child->getNext()) {
            getChildBoundingBox(child);
        }
        if (bounds.state == 1) {
            bounds.center = (bounds.maximum + bounds.minimum) * 0.5f;
            bounds.radius = glm::distance(bounds.minimum, bounds.center);
        }
    }
    clearNotify(NOTIFY_BOUNDS_DIRTY);
}

// FUNCTION: SURRENDER 0x1004AC60
void srBounder::getChildBoundingBox(srNode* node)
{
    node->updateBounds();
    if (bounds.state != 2) {
        const auto child_bounds = node->getLocalBounds();
        if (child_bounds.state == 1) {
            auto world = node->getWorldSpaceMatrix<glm::mat4>();
            const srBoundingBox child_box{child_bounds.minimum, child_bounds.maximum};
            const auto transformed = child_box.transformed(inverse_world * world);
            if (bounds.state == 0) {
                bounds.minimum = transformed.minimum;
                bounds.maximum = transformed.maximum;
            } else {
                bounds.minimum = glm::min(bounds.minimum, transformed.minimum);
                bounds.maximum = glm::max(bounds.maximum, transformed.maximum);
            }
            bounds.state = 1;
        } else if (child_bounds.state == 2) {
            bounds.state = 2;
            return;
        }
    }
    for (srNode* child = node->getChild(); child != 0; child = child->getNext()) {
        getChildBoundingBox(child);
    }
}

// FUNCTION: SURRENDER 0x1004AA30
void srBounder::dump(std::ostream& stream)
{
    srNode::dump(stream);
    const srDumpStreamScope formatting(stream);
    stream.width(0x20);
    stream << "  Bound mode: ";
    stream << (bound_mode == 1 ? "static\n" : "dynamic\n");
    if (testNotify(NOTIFY_BOUNDS_DIRTY) != 0) {
        stream << "Bounds not calculated.\n";
    } else if (bounds.state == 0) {
        stream << "Bounding area empty\n";
    } else if (bounds.state == 2) {
        stream << "Bounds contain an infinite-size child (VOLUME_INFINITE) - bounder "
                  "deactivated\n";
    } else {
        stream.width(0x20);
        stream << "  Bounding box min: ";
        stream << '{' << bounds.minimum.x << ',' << bounds.minimum.y << ',' << bounds.minimum.z
               << '}' << '\n';
        stream.width(0x20);
        stream << "  Bounding box max: ";
        stream << '{' << bounds.maximum.x << ',' << bounds.maximum.y << ',' << bounds.maximum.z
               << '}' << '\n';
        stream.width(0x20);
        stream << "  Bounding sphere origin: ";
        stream << '{' << bounds.center.x << ',' << bounds.center.y << ',' << bounds.center.z << '}'
               << '\n';
        stream.width(0x20);
        stream << "  Bounding sphere radius: " << bounds.radius << '\n';
    }
}

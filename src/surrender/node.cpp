#include <cmath>
#include <cstdint>

#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include "compat/compiler.h"
#include "glm/fwd.hpp"
#include "glm/geometric.hpp"
#include "glm/matrix.hpp"
#include "surrender/model_view_scope.h"
#include "surrender/srClass.h"
#include "surrender/srGERD.h"
#include "surrender/srMath.h"
#include "surrender/srNode.h"

// The node repair step orthonormalizes rows, preserving its zero-row behavior.
static void orthonormalizeRotation(glm::dmat3& rotation)
{
    auto rows = glm::transpose(rotation);
    for (int row = 0; row < 3; ++row) {
        for (int earlier = 0; earlier < row; ++earlier)
            rows[row] -= rows[earlier] * glm::dot(rows[row], rows[earlier]);
        rows[row] = glm::normalize(rows[row]);
    }
    rotation = glm::transpose(rows);
}

void srNode::notifyChildren(std::uint32_t notifications)
{
    /* Only the bits this node has not seen yet propagate; the new set is
       re-masked at every recursion level. */
    w8_ulong pending = ~this->notifications & notifications;
    if (pending != 0) {
        this->notifications |= pending;
        for (srNode* child = first_child_; child != 0; child = child->next_sibling_) {
            child->notifyChildren(pending);
        }
    }
}

int srNode::testNotify(e_notify notification) const
{
    return (notifications & (1 << notification)) != 0;
}

void srNode::traverse(TraverseInfo& info)
{
    if (next_sibling_ != 0) {
        next_sibling_->traverse(info);
    }
    if ((flags & (1 << FLAG_TERMINATE)) == 0 && first_child_ != 0) {
        first_child_->traverse(info);
    }
}

void srNode::traverseEffect(TraverseInfo& info)
{
    traverseEffect(info, [] { return EffectTraversal::active; });
}

void srNode::process(const ProcessInfo& info, e_processType type) {}

srNode::BoundInfo srNode::getLocalBounds()
{
    return {glm::vec3(0), glm::vec3(0), glm::vec3(0), 0.0f, testFlag(FLAG_GLOBAL) != 0 ? 2 : 0};
}

srNode& srNode::operator=(const srNode& other)
{
    if (this != &other) {
        srClass::operator=(other);
        flags = other.flags;
        location = other.location;
        scale = other.scale;
        rotation = other.rotation;
        setWSDirty();
    }
    return *this;
}

srNode::srNode(srNode* parent)
{
    world_transform0 = glm::dmat4x3(1);
    world_transform1 = glm::mat4x3(1);
    next_sibling_ = 0;
    previous_sibling_ = 0;
    parent_ = 0;
    first_child_ = 0;
    location = glm::dvec3(0);
    rotation = glm::dmat3(1);
    scale = glm::dvec3(1);
    notifications = 0;
    if (parent) setParent(parent, 0);
}

// Links borrow; detach surviving children before destroying this node's owners.
srNode::~srNode()
{
    unlink();
    while (first_child_) first_child_->setParent(nullptr, 0);
}

void srNode::eraseOwnedChild(const srNode* child)
{
    std::erase_if(owned_children_, [child](const auto& owned) { return owned.get() == child; });
}

// Reparenting preserves world placement when requested, independent of ownership.
int srNode::setParent(srNode* parent, int preserve_world_transform)
{
    if (parent == this) {
        return 0;
    }
    if (parent != 0 && isParentOf(*parent)) {
        return 0;
    }
    if (preserve_world_transform != 0) {
        rotation = getWorldSpaceRotation<glm::dmat3>();
        location = getWorldSpaceLocation();
        scale = getWorldSpaceScale();
    }
    unlink();
    if (parent != nullptr) {
        parent_ = parent;
        next_sibling_ = parent->first_child_;
        if (next_sibling_ != 0) {
            next_sibling_->previous_sibling_ = this;
        }
        parent->first_child_ = this;
        if (preserve_world_transform != 0) {
            const auto [parent_rotation, parent_location, parent_scale] =
                parent->getWorldSpaceCoordinates();
            rotation = glm::transpose(parent_rotation) * rotation;
            orthonormalizeRotation(rotation);
            const auto inverse_scale = 1.0 / parent_scale;
            location =
                glm::transpose(parent_rotation) * ((location - parent_location) * inverse_scale);
            scale *= inverse_scale;
        }
    }
    setWSDirty();
    if (testNotify(NOTIFY_TRANSFORM_DIRTY) != 0) {
        updateTransformation();
    }
    return 1;
}

w8_long srNode::getChildCount() const
{
    w8_long count = 0;
    for (srNode* child = first_child_; child != 0; child = child->next_sibling_) {
        count += 1 + child->getChildCount();
    }
    return count;
}

void srNode::unlink()
{
    if (previous_sibling_ != 0) {
        previous_sibling_->next_sibling_ = next_sibling_;
    }
    if (next_sibling_ != 0) {
        next_sibling_->previous_sibling_ = previous_sibling_;
    }
    if (parent_ != 0 && parent_->first_child_ == this) {
        parent_->first_child_ = next_sibling_;
    }
    previous_sibling_ = 0;
    next_sibling_ = 0;
    parent_ = 0;
    setWSDirty();
}

int srNode::isChildOf(const srNode& node) const
{
    const srNode* current = this;
    while (current != 0) {
        if (current == &node) {
            return 1;
        }
        current = current->parent_;
    }
    return 0;
}

int srNode::isParentOf(const srNode& node) const
{
    const srNode* target = &node;
    if (target == 0) {
        return 0;
    }
    return target->isChildOf(*this);
}

srNode* srNode::getChild() const
{
    return first_child_;
}

srNode* srNode::getNext() const
{
    return next_sibling_;
}

void srNode::setScale(double scale)
{
    setScale(glm::dvec3(scale));
}

void srNode::checkTransformation() const
{
    if (testNotify(NOTIFY_TRANSFORM_DIRTY) != 0) {
        updateTransformation();
    }
}

void srNode::setFlag(e_flag flag)
{
    flags |= 1 << flag;
    if (flag == FLAG_IGNORE_TRANSFORM) {
        setWSDirty();
    }
}

void srNode::clearFlag(e_flag flag)
{
    flags &= ~(1 << flag);
    if (flag == FLAG_IGNORE_TRANSFORM) {
        setWSDirty();
    }
}

int srNode::testFlag(e_flag flag) const
{
    return (flags & (1 << flag)) != 0;
}

void srNode::setWSDirty()
{
    notifyChildren(1u << NOTIFY_TRANSFORM_DIRTY);
}

void srNode::setRotation(const glm::dmat3& rotation)
{
    this->rotation = rotation;
    setWSDirty();
}

void srNode::setRotation(const glm::mat3& rotation)
{
    setRotation(glm::dmat3(rotation));
}

// Compose local X, Y and Z rotations in that order.
void srNode::setRotation(double x, double y, double z)
{
    rotation = glm::mat3_cast(glm::angleAxis(x, glm::dvec3(1, 0, 0)) *
                              glm::angleAxis(y, glm::dvec3(0, 1, 0)) *
                              glm::angleAxis(z, glm::dvec3(0, 0, 1)));
    setWSDirty();
}

void srNode::setRotation(double amount, const glm::dvec3& direction)
{
    const glm::dvec3 axis = normalizedOrZero(direction);
    rotation =
        glm::dot(axis, axis) == 0 ? glm::dmat3(1) : glm::mat3_cast(glm::angleAxis(amount, axis));
    setWSDirty();
}

void srNode::setRotation(const glm::dvec3& direction, double amount)
{
    const double yaw = std::atan2(direction.x, direction.z);
    const double pitch = -std::atan2(direction.y, std::hypot(direction.x, direction.z));
    rotation = glm::mat3_cast(glm::angleAxis(yaw, glm::dvec3(0, 1, 0)) *
                              glm::angleAxis(pitch, glm::dvec3(1, 0, 0)) *
                              glm::angleAxis(amount, glm::dvec3(0, 0, 1)));
    setWSDirty();
}

void srNode::setRotation(const glm::dvec3& first, const glm::dvec3& second, double amount)
{
    setRotation(first - second, amount);
}

double srNode::getLocationX() const
{
    return location.x;
}

double srNode::getLocationY() const
{
    return location.y;
}

double srNode::getLocationZ() const
{
    return location.z;
}

glm::dvec3 srNode::getLocation() const
{
    return location;
}

glm::dvec3 srNode::getScale() const
{
    return scale;
}

void srNode::offsetLocation(const glm::dvec3& offset)
{
    if (offset.x != 0.0 || offset.y != 0.0 || offset.z != 0.0) {
        location += offset;
        setWSDirty();
    }
}

void srNode::offsetLocation(double x, double y, double z)
{
    offsetLocation(glm::dvec3(x, y, z));
}

void srNode::setLocation(const glm::dvec3& location)
{
    if (!(location == this->location)) {
        this->location = location;
        setWSDirty();
    }
}

void srNode::setLocation(double x, double y, double z)
{
    setLocation(glm::dvec3(x, y, z));
}

void srNode::setScale(const glm::dvec3& scale)
{
    if (!(this->scale == scale)) {
        this->scale = scale;
        setWSDirty();
    }
}

glm::dvec3 srNode::getWorldSpaceLocation() const
{
    checkTransformation();
    return world_transform0[3];
}

glm::dvec3 srNode::getWorldSpaceDOF() const
{
    checkTransformation();
    return normalizedOrZero(world_transform0[2]);
}

glm::dvec3 srNode::getWorldSpaceScale() const
{
    checkTransformation();
    return {glm::length(world_transform0[0]), glm::length(world_transform0[1]),
            glm::length(world_transform0[2])};
}

void srNode::updateTransformation() const
{
    notifications &= ~(1u << NOTIFY_TRANSFORM_DIRTY);
    glm::dmat4 world(1);
    if (!testFlag(FLAG_IGNORE_TRANSFORM)) {
        world = glm::scale(glm::dmat4(rotation), scale);
        world[3] = glm::dvec4(location, 1);
    }
    if (parent_) {
        parent_->checkTransformation();
        world = glm::dmat4(parent_->world_transform0) * world;
    }
    world_transform0 = glm::dmat4x3(world);
    world_transform1 = glm::mat4x3(world);
}

srModelViewScope srNode::scopedWorldSpaceMatrix(srGERD& renderer) const
{
    checkTransformation();
    srModelViewScope transform(renderer);
    renderer.multMatrix(glm::mat4(world_transform1));
    return transform;
}

void srNode::move(const glm::dvec3& offset)
{
    if (offset.x != 0.0 || offset.y != 0.0 || offset.z != 0.0) {
        location += rotation * offset;
        setWSDirty();
    }
}

void srNode::rotate(const glm::dmat3& rotation)
{
    glm::dmat3 identity;
    identity = glm::dmat3(1);
    if (!(rotation == identity)) {
        (this->rotation) = (this->rotation) * rotation;
        setWSDirty();
    }
}

void srNode::rotate(double angle, const glm::dvec3& axis)
{
    const glm::dvec3 direction = normalizedOrZero(axis);
    if (angle != 0 && glm::dot(direction, direction) != 0) {
        rotation *= glm::mat3_cast(glm::angleAxis(angle, direction));
        setWSDirty();
    }
}

void srNode::rotateX(double angle)
{
    rotate(angle, glm::dvec3(1, 0, 0));
}

void srNode::setWorldSpaceRotation(const glm::dmat3& rotation)
{
    this->rotation = rotation;
    if (parent_) {
        auto parent_world = parent_->getWorldSpaceMatrix<glm::dmat4>();
        this->rotation = glm::inverse(glm::dmat3(parent_world)) * rotation;
        for (int column = 0; column < 3; ++column)
            this->rotation[column] = normalizedOrZero(this->rotation[column]);
    }
    setWSDirty();
}

void srNode::setWorldSpaceMatrix(const glm::dmat4& matrix)
{
    glm::dmat4 local = matrix;
    if (parent_) {
        auto parent_world = parent_->getWorldSpaceMatrix<glm::dmat4>();
        local = glm::inverse(parent_world) * matrix;
    }
    location = glm::dvec3(local[3]);
    rotation = glm::dmat3(local);
    scale =
        glm::dvec3(glm::length(rotation[0]), glm::length(rotation[1]), glm::length(rotation[2]));
    for (int column = 0; column < 3; ++column) rotation[column] /= scale[column];
    setWSDirty();
}

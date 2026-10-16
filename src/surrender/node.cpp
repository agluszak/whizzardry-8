#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include <string_view>

#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include "compat/compiler.h"
#include "glm/fwd.hpp"
#include "glm/geometric.hpp"
#include "glm/matrix.hpp"
#include "surrender/model_view_scope.h"
#include "surrender/srClass.h"
#include "surrender/srCore.h"
#include "surrender/srFlags.h"
#include "surrender/srGERD.h"
#include "surrender/srMath.h"
#include "surrender/srNode.h"

// GLOBAL: SURRENDER 0x100A49E0
std::recursive_mutex srNode::sceneGraphCSect;

// GLOBAL: SURRENDER 0x100A49FC
std::atomic<w8_long> srNode::sceneGraphLockCount{0};

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

// FUNCTION: SURRENDER 0x10050340
void srNode::lockSceneGraph()
{
    sceneGraphCSect.lock();
    ++sceneGraphLockCount;
}

// FUNCTION: SURRENDER 0x10050360
void srNode::unlockSceneGraph()
{
    --sceneGraphLockCount;
    sceneGraphCSect.unlock();
}

// FUNCTION: SURRENDER 0x10050380
int srNode::isSceneGraphLocked()
{
    return sceneGraphLockCount.load() != 0;
}

// FUNCTION: SURRENDER 0x10050390
void srNode::notifyDependent()
{
    const srFlags<e_notify> notifications(0xffffffff);
    notifyParents(notifications);
    notifyChildren(notifications);
}

// FUNCTION: SURRENDER 0x100503C0
void srNode::notifyChildren(const srFlags<e_notify>& notifications)
{
    /* Only the bits this node has not seen yet propagate; the new set is
       re-masked at every recursion level. */
    w8_ulong pending = ~this->notifications.value & notifications.value;
    if (pending != 0) {
        this->notifications.value |= pending;
        for (srNode* child = first_child_; child != 0; child = child->next_sibling_) {
            child->notifyChildren(srFlags<e_notify>(pending));
        }
    }
}

// FUNCTION: SURRENDER 0x10050410
void srNode::notifyParents(const srFlags<e_notify>& notifications)
{
    w8_ulong pending = ~this->notifications.value & notifications.value;
    if (pending != 0) {
        this->notifications.value |= pending;
        if (parent_ != 0) {
            parent_->notifyParents(srFlags<e_notify>(pending));
        }
    }
}

// FUNCTION: SURRENDER 0x10050450
void srNode::setNotify(e_notify notification)
{
    notifications.value |= 1 << notification;
}

// FUNCTION: SURRENDER 0x10050470
void srNode::clearNotify(e_notify notification)
{
    notifications.value &= ~(1 << notification);
}

// FUNCTION: SURRENDER 0x10050490
int srNode::testNotify(e_notify notification) const
{
    return (notifications.value & (1 << notification)) != 0;
}

// FUNCTION: SURRENDER 0x100504B0
void srNode::traverse(TraverseInfo& info)
{
    if (next_sibling_ != 0) {
        next_sibling_->traverse(info);
    }
    if ((flags.value & (1 << FLAG_TERMINATE)) == 0 && first_child_ != 0) {
        first_child_->traverse(info);
    }
}

void srNode::traverseEffect(TraverseInfo& info)
{
    traverseEffect(info, [] { return EffectTraversal::active; });
}

// FUNCTION: SURRENDER 0x100504F0
void srNode::process(const ProcessInfo& info, e_processType type) {}

// FUNCTION: SURRENDER 0x10050500
void srNode::updateBounds()
{
    clearNotify(NOTIFY_BOUNDS_DIRTY);
}

// FUNCTION: SURRENDER 0x10050510
srNode::BoundInfo srNode::getLocalBounds()
{
    return {glm::vec3(0), glm::vec3(0), glm::vec3(0), 0.0f, testFlag(FLAG_GLOBAL) != 0 ? 2 : 0};
}

// FUNCTION: SURRENDER 0x100505E0
srNode& srNode::operator=(const srNode& other)
{
    if (this != &other) {
        std::lock_guard access(sceneGraphCSect);
        srClass::operator=(other);
        flags.value = other.flags.value;
        location = other.location;
        scale = other.scale;
        rotation = other.rotation;
        notifyDependent();
    }
    return *this;
}

// FUNCTION: SURRENDER 0x10050690
int srNode::processSignal(w8_ulong signal, void* value)
{
    return 1;
}

// FUNCTION: SURRENDER 0x100506A0
void srNode::signalInternal(w8_ulong signal, void* value)
{
    /* Depth-first walk: at each level the sibling chain is recursed first,
       then the first-child chain is iterated while processSignal keeps
       returning nonzero. */
    srNode* node = this;
    do {
        if (node->next_sibling_ != 0) {
            node->next_sibling_->signalInternal(signal, value);
        }
        if (node->processSignal(signal, value) == 0) {
            return;
        }
        node = node->first_child_;
    } while (node != 0);
}

// FUNCTION: SURRENDER 0x100506E0
void srNode::signal(w8_ulong signal, void* value)
{
    if (processSignal(signal, value) != 0 && first_child_ != 0) {
        first_child_->signalInternal(signal, value);
    }
}

// FUNCTION: SURRENDER 0x10050710
w8_long srNode::getHierarchyLevel() const
{
    w8_long level = 0;
    for (srNode* node = parent_; node != 0; node = node->parent_) {
        ++level;
    }
    return level;
}

// FUNCTION: SURRENDER 0x10050C10
srNode::srNode(srNode* parent, Ownership ownership) : parent_owns_(ownership == Ownership::parent)
{
    std::lock_guard access(sceneGraphCSect);
    world_transform0 = glm::dmat4x3(1);
    world_transform1 = glm::mat4x3(1);
    next_sibling_ = 0;
    previous_sibling_ = 0;
    parent_ = 0;
    first_child_ = 0;
    location = glm::dvec3(0);
    rotation = glm::dmat3(1);
    scale = glm::dvec3(1);
    notifications.value = 0;
    if (parent || parent_owns_) setParent(parent, 0);
}

/* Destroy owned children; independently owned render members are detached. */
// FUNCTION: SURRENDER 0x10050E20
srNode::~srNode()
{
    std::lock_guard access(sceneGraphCSect);
    unlink();
    while (first_child_ != nullptr) {
        if (first_child_->parent_owns_) {
            delete first_child_;
        } else {
            first_child_->detach();
        }
    }
}

/* Reparenting under the scene-graph lock; preserve_world_transform decomposes
   the current world transform into the local slots before unlinking, then
   rebuilds them against the new parent's inverse basis (orthonormalized by
   Gram-Schmidt) so the node's world placement survives the move. */
// FUNCTION: SURRENDER 0x10050F00
int srNode::setParent(srNode* parent, int preserve_world_transform)
{
    std::lock_guard access(sceneGraphCSect);
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
    if (parent != 0 || ((parent = srCore.getRootNode()) != this && parent != 0)) {
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
    notifyDependent();
    setWSDirty();
    if (testNotify(NOTIFY_TRANSFORM_DIRTY) != 0) {
        updateTransformation();
    }
    return 1;
}

void srNode::detach()
{
    std::lock_guard access(sceneGraphCSect);
    unlink();
    parent_owns_ = false;
    setWSDirty();
}

// FUNCTION: SURRENDER 0x10050B70
w8_long srNode::getChildCount() const
{
    w8_long count = 0;
    for (srNode* child = first_child_; child != 0; child = child->next_sibling_) {
        count += 1 + child->getChildCount();
    }
    return count;
}

// FUNCTION: SURRENDER 0x10050BA0
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
    notifyDependent();
}

// FUNCTION: SURRENDER 0x10051370
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

// FUNCTION: SURRENDER 0x100513A0
int srNode::isParentOf(const srNode& node) const
{
    const srNode* target = &node;
    if (target == 0) {
        return 0;
    }
    return target->isChildOf(*this);
}

// FUNCTION: SURRENDER 0x10051A20
srNode* srNode::getChild() const
{
    return first_child_;
}

// FUNCTION: SURRENDER 0x10051A30
srNode* srNode::getNext() const
{
    return next_sibling_;
}

// FUNCTION: SURRENDER 0x10051A50
srNode* srNode::getPrev() const
{
    return previous_sibling_;
}

// FUNCTION: SURRENDER 0x10051A60
void srNode::setScale(double scale)
{
    setScale(glm::dvec3(scale));
}

// FUNCTION: SURRENDER 0x10051A90
void srNode::checkTransformation() const
{
    if (testNotify(NOTIFY_TRANSFORM_DIRTY) != 0) {
        updateTransformation();
    }
}

// FUNCTION: SURRENDER 0x10051C50
void srNode::setFlag(e_flag flag)
{
    flags.value |= 1 << flag;
    if (flag == FLAG_IGNORE_TRANSFORM) {
        setWSDirty();
    }
}

// FUNCTION: SURRENDER 0x10051C80
void srNode::clearFlag(e_flag flag)
{
    flags.value &= ~(1 << flag);
    if (flag == FLAG_IGNORE_TRANSFORM) {
        setWSDirty();
    }
}

// FUNCTION: SURRENDER 0x10051CB0
int srNode::testFlag(e_flag flag) const
{
    return (flags.value & (1 << flag)) != 0;
}

// FUNCTION: SURRENDER 0x10051D40
void srNode::setWSDirty()
{
    /* The dirty fan-out to parents and children can rewrite the notification word, so the
       positional bit is saved first and restored after. */
    int bounds_dirty = notifications.value & (1 << NOTIFY_BOUNDS_DIRTY);
    notifyParents(srFlags<e_notify>(1u << NOTIFY_BOUNDS_DIRTY));
    notifyChildren(srFlags<e_notify>(1u << NOTIFY_TRANSFORM_DIRTY));
    notifications.set(NOTIFY_BOUNDS_DIRTY, bounds_dirty);
}

// FUNCTION: SURRENDER 0x10051DB0
double srNode::getDistance(const srNode& node) const
{
    return glm::distance(getWorldSpaceLocation(), node.getWorldSpaceLocation());
}

// FUNCTION: SURRENDER 0x100536B0
void srNode::setRotation(const glm::dmat3& rotation)
{
    this->rotation = rotation;
    setWSDirty();
}

// FUNCTION: SURRENDER 0x100536D0
void srNode::setRotation(const glm::mat3& rotation)
{
    setRotation(glm::dmat3(rotation));
}

// Compose local X, Y and Z rotations in that order.
// FUNCTION: SURRENDER 0x10053740
void srNode::setRotation(double x, double y, double z)
{
    rotation = glm::mat3_cast(glm::angleAxis(x, glm::dvec3(1, 0, 0)) *
                              glm::angleAxis(y, glm::dvec3(0, 1, 0)) *
                              glm::angleAxis(z, glm::dvec3(0, 0, 1)));
    setWSDirty();
}

// FUNCTION: SURRENDER 0x10052750
void srNode::setRotation(double amount, const glm::dvec3& direction)
{
    const glm::dvec3 axis = normalizedOrZero(direction);
    rotation =
        glm::dot(axis, axis) == 0 ? glm::dmat3(1) : glm::mat3_cast(glm::angleAxis(amount, axis));
    setWSDirty();
}

// FUNCTION: SURRENDER 0x100528C0
void srNode::setRotation(const glm::dvec3& direction, double amount)
{
    const double yaw = std::atan2(direction.x, direction.z);
    const double pitch = -std::atan2(direction.y, std::hypot(direction.x, direction.z));
    rotation = glm::mat3_cast(glm::angleAxis(yaw, glm::dvec3(0, 1, 0)) *
                              glm::angleAxis(pitch, glm::dvec3(1, 0, 0)) *
                              glm::angleAxis(amount, glm::dvec3(0, 0, 1)));
    setWSDirty();
}

// FUNCTION: SURRENDER 0x10052B20
void srNode::setRotation(const glm::dvec3& first, const glm::dvec3& second, double amount)
{
    setRotation(first - second, amount);
}

// FUNCTION: SURRENDER 0x10053D70
double srNode::getLocationX() const
{
    return location.x;
}

// FUNCTION: SURRENDER 0x10053D80
double srNode::getLocationY() const
{
    return location.y;
}

// FUNCTION: SURRENDER 0x10053D90
double srNode::getLocationZ() const
{
    return location.z;
}

// FUNCTION: SURRENDER 0x10053DA0
glm::dvec3 srNode::getLocation() const
{
    return location;
}

// FUNCTION: SURRENDER 0x10053E90
glm::dvec3 srNode::getScale() const
{
    return scale;
}

// FUNCTION: SURRENDER 0x10053EB0
void srNode::offsetLocation(const glm::dvec3& offset)
{
    if (offset.x != 0.0 || offset.y != 0.0 || offset.z != 0.0) {
        location += offset;
        setWSDirty();
    }
}

// FUNCTION: SURRENDER 0x10053F10
void srNode::offsetLocation(double x, double y, double z)
{
    offsetLocation(glm::dvec3(x, y, z));
}

// FUNCTION: SURRENDER 0x10054790
void srNode::setLocation(const glm::dvec3& location)
{
    if (!(location == this->location)) {
        this->location = location;
        setWSDirty();
    }
}

// FUNCTION: SURRENDER 0x100547E0
void srNode::setLocation(double x, double y, double z)
{
    setLocation(glm::dvec3(x, y, z));
}

void srNode::setLocationComponent(unsigned component, double value)
{
    if (location[component] != value) {
        location[component] = value;
        setWSDirty();
    }
}

// FUNCTION: SURRENDER 0x10054840
void srNode::setLocationX(double x)
{
    setLocationComponent(0, x);
}

// FUNCTION: SURRENDER 0x10054870
void srNode::setLocationY(double y)
{
    setLocationComponent(1, y);
}

// FUNCTION: SURRENDER 0x100548A0
void srNode::setLocationZ(double z)
{
    setLocationComponent(2, z);
}

// FUNCTION: SURRENDER 0x100548D0
void srNode::setScale(const glm::dvec3& scale)
{
    if (!(this->scale == scale)) {
        this->scale = scale;
        setWSDirty();
    }
}

// FUNCTION: SURRENDER 0x10054F70
glm::dvec3 srNode::getWorldSpaceLocation() const
{
    checkTransformation();
    return world_transform0[3];
}

// FUNCTION: SURRENDER 0x10055210
glm::dvec3 srNode::getWorldSpaceDOF() const
{
    checkTransformation();
    return normalizedOrZero(world_transform0[2]);
}

// FUNCTION: SURRENDER 0x100552B0
glm::dvec3 srNode::getWorldSpaceScale() const
{
    checkTransformation();
    return {glm::length(world_transform0[0]), glm::length(world_transform0[1]),
            glm::length(world_transform0[2])};
}

// FUNCTION: SURRENDER 0x10054920
void srNode::updateTransformation() const
{
    notifications.value &= ~(1u << NOTIFY_TRANSFORM_DIRTY);
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

// FUNCTION: SURRENDER 0x10051E10
void srNode::setWorldSpaceLocation(const glm::dvec3& location)
{
    if (parent_ == 0) {
        this->location = location;
    } else {
        auto parent_world = parent_->getWorldSpaceMatrix<glm::dmat4>();
        glm::dmat4 inverse;
        inverse = glm::inverse(parent_world);
        this->location = glm::dvec3(inverse * glm::dvec4(location, 1));
    }
    setWSDirty();
}

// FUNCTION: SURRENDER 0x100553A0
srModelViewScope srNode::scopedWorldSpaceMatrix(srGERD& renderer) const
{
    checkTransformation();
    srModelViewScope transform(renderer);
    renderer.multMatrix(glm::mat4(world_transform1));
    return transform;
}

// FUNCTION: SURRENDER 0x10050A30
std::string srNode::getFullPath() const
{
    std::string path;
    if (parent_ != 0) {
        parent_->getFullPathInternal(path);
    }
    path += getName();
    return path;
}

// FUNCTION: SURRENDER 0x100509B0
void srNode::getFullPathInternal(std::string& path) const
{
    if (parent_ != 0) {
        parent_->getFullPathInternal(path);
    }
    path += getName();
    path += '/';
}

// FUNCTION: SURRENDER 0x10050930
srNode* srNode::findParent(std::string_view name) const
{
    if (parent_ != 0) {
        return parent_->findParentInternal(name);
    }
    return 0;
}

// FUNCTION: SURRENDER 0x10050890
srNode* srNode::findParentInternal(std::string_view name)
{
    srNode* node = this;
    while (name != node->getName()) {
        node = node->parent_;
        if (node == 0) {
            return 0;
        }
    }
    return node;
}

// FUNCTION: SURRENDER 0x10053A20
void srNode::move(const glm::dvec3& offset)
{
    if (offset.x != 0.0 || offset.y != 0.0 || offset.z != 0.0) {
        location += rotation * offset;
        setWSDirty();
    }
}

void srNode::moveAlongAxis(unsigned axis, double distance)
{
    if (distance != 0) {
        location += rotation[axis] * distance;
        setWSDirty();
    }
}

// FUNCTION: SURRENDER 0x10053AD0
void srNode::moveForward(double distance)
{
    moveAlongAxis(2, distance);
}

// FUNCTION: SURRENDER 0x10053B40
void srNode::moveBackward(double distance)
{
    moveAlongAxis(2, -distance);
}

// FUNCTION: SURRENDER 0x10053BB0
void srNode::moveLeft(double distance)
{
    moveAlongAxis(0, -distance);
}

// FUNCTION: SURRENDER 0x10053C20
void srNode::moveRight(double distance)
{
    moveAlongAxis(0, distance);
}

// FUNCTION: SURRENDER 0x10053C90
void srNode::moveUp(double distance)
{
    moveAlongAxis(1, distance);
}

// FUNCTION: SURRENDER 0x10053D00
void srNode::moveDown(double distance)
{
    moveAlongAxis(1, -distance);
}

// FUNCTION: SURRENDER 0x10053F70
void srNode::rotate(const glm::dmat3& rotation)
{
    glm::dmat3 identity;
    identity = glm::dmat3(1);
    if (!(rotation == identity)) {
        (this->rotation) = (this->rotation) * rotation;
        setWSDirty();
    }
}

// FUNCTION: SURRENDER 0x100540E0
void srNode::rotate(double angle, const glm::dvec3& axis)
{
    const glm::dvec3 direction = normalizedOrZero(axis);
    if (angle != 0 && glm::dot(direction, direction) != 0) {
        rotation *= glm::mat3_cast(glm::angleAxis(angle, direction));
        setWSDirty();
    }
}

// FUNCTION: SURRENDER 0x100542A0
void srNode::rotateX(double angle)
{
    rotate(angle, glm::dvec3(1, 0, 0));
}

// FUNCTION: SURRENDER 0x10054440
void srNode::rotateY(double angle)
{
    rotate(angle, glm::dvec3(0, 1, 0));
}

// FUNCTION: SURRENDER 0x100545F0
void srNode::rotateZ(double angle)
{
    rotate(angle, glm::dvec3(0, 0, 1));
}

// FUNCTION: SURRENDER 0x10051FB0
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

// FUNCTION: SURRENDER 0x10052420
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

void srNode::turnWorldSpaceAxisToward(unsigned axis, const glm::dvec3& direction, double amount)
{
    auto basis = getWorldSpaceRotation();
    const unsigned numerator = axis == 0 ? 1 : 0;
    const unsigned denominator = axis == 2 ? 1 : 2;
    const double turn =
        std::atan2(glm::dot(basis[numerator], direction), glm::dot(basis[denominator], direction));
    const double angle = (axis == 1 ? turn : -turn) * amount;
    glm::dvec3 rotation_axis(0);
    rotation_axis[axis] = 1;
    basis *= glm::mat3_cast(glm::angleAxis(angle, rotation_axis));
    orthonormalizeRotation(basis);
    setWorldSpaceRotation(basis);
}

// FUNCTION: SURRENDER 0x10052D80
void srNode::pitchAt(const glm::dvec3& target, double amount)
{
    turnWorldSpaceAxisToward(0, normalizedOrZero(target - getWorldSpaceLocation()), amount);
}

// FUNCTION: SURRENDER 0x100533D0
void srNode::pitchAt(const srNode* target, double amount)
{
    pitchAt(target->getWorldSpaceLocation(), amount);
}

// FUNCTION: SURRENDER 0x10052FC0
void srNode::yawAt(const glm::dvec3& target, double amount)
{
    turnWorldSpaceAxisToward(1, normalizedOrZero(target - getWorldSpaceLocation()), amount);
}

// FUNCTION: SURRENDER 0x10053420
void srNode::yawAt(const srNode* target, double amount)
{
    yawAt(target->getWorldSpaceLocation(), amount);
}

// FUNCTION: SURRENDER 0x10053210
void srNode::rollUp(double amount)
{
    glm::dmat3 rotation;
    rotation = getWorldSpaceRotation<glm::dmat3>();
    const double angle = -std::atan2(rotation[0][1], rotation[1][1]) * amount;
    rotation *= glm::mat3_cast(glm::angleAxis(angle, glm::dvec3(0, 0, 1)));
    orthonormalizeRotation(rotation);
    setWorldSpaceRotation(rotation);
}

// FUNCTION: SURRENDER 0x10053470
void srNode::rollAt(const glm::dvec3& target, double amount)
{
    turnWorldSpaceAxisToward(2, target, amount);
}

// FUNCTION: SURRENDER 0x10053660
void srNode::rollAt(const srNode* target, double amount)
{
    auto rotation = target->getWorldSpaceRotation<glm::dmat3>();
    rollAt(rotation[0], amount);
}

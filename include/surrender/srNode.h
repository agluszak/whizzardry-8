#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <string_view>
#include <type_traits>
#include <vector>

#include "glm/ext/vector_double3.hpp"
#include "model_view_scope.h"
#include "srClass.h"
#include "srMath.h"

// VTABLE: SURRENDER 0x100771D0 srNode
class srNode : public srClass {
public:
    class TraverseInfo {
    public:
        struct Entry {
            srNode* node;
            w8_ulong value;
        };

        std::vector<srNode*> nodes;   /* 0x00 */
        std::vector<Entry> entries;   /* 0x08 */
        /* srScene::process stores the active renderer here; srBounder::traverse
           reads it for the child volume tests. */
        class srGERD* renderer = nullptr; /* 0x18 */
    };
    struct ProcessInfo {
        class srGERD* renderer;
    };
    /* Bounds record: box, sphere and a trailing state of 0 (empty), 1 (box+sphere) or 2
       (FLAG_GLOBAL). */
    struct BoundInfo {
        glm::vec3 minimum;
        glm::vec3 maximum;
        glm::vec3 center;
        float radius;
        int state;
    };

    enum e_processType {
        PROCESS_RENDER = 0,
        PROCESS_PUSH = 1,
        PROCESS_POP = 2,
        PROCESS_PUSH_GLOBAL = 3,
        PROCESS_POP_GLOBAL = 4
    };

    /* traverse omits a DISABLE node and does not walk the children of a TERMINATE node. Lights,
       clip planes and bounders set GLOBAL. Setting IGNORE_TRANSFORM dirties the cached world
       transform. */
    enum e_flag {
        FLAG_DISABLE = 0,
        FLAG_TERMINATE = 1,
        FLAG_GLOBAL = 2,
        FLAG_IGNORE_TRANSFORM = 3
    };

    enum e_notify { NOTIFY_BOUNDS_DIRTY = 0, NOTIFY_TRANSFORM_DIRTY = 1 };

    enum class Ownership { parent, independent };
    SR_DLL_IMPORT srNode(srNode* parent = nullptr, Ownership ownership = Ownership::parent);
    /* Copies the transform under the default parent; links are not copied. */
    srNode(const srNode& other) : srNode(nullptr) { *this = other; }
    virtual SR_DLL_IMPORT ~srNode() override;

    SR_DLL_IMPORT srNode& operator=(const srNode& other);

public:
    virtual SR_DLL_IMPORT void traverse(TraverseInfo& info);
    virtual SR_DLL_IMPORT void process(const ProcessInfo& info, e_processType type);
    virtual SR_DLL_IMPORT BoundInfo getLocalBounds();
    virtual SR_DLL_IMPORT void updateBounds();

protected:
    enum class EffectTraversal { active, childrenOnly, skipSubtree };
    void traverseEffect(TraverseInfo& info);

    template<class Activity>
    void traverseEffect(TraverseInfo& info, Activity activity)
    {
        if (next_sibling_) next_sibling_->traverse(info);
        if (testFlag(FLAG_TERMINATE)) return;
        // Evaluate activity after siblings, and preserve disabled-node descent.
        const auto mode = testFlag(FLAG_DISABLE) ? EffectTraversal::childrenOnly : activity();
        if (mode == EffectTraversal::skipSubtree) return;
        if (mode == EffectTraversal::active) {
            if (testFlag(FLAG_GLOBAL))
                info.nodes.push_back(this);
            else
                info.entries.push_back({this, PROCESS_PUSH});
        }
        if (first_child_) first_child_->traverse(info);
        if (mode == EffectTraversal::active && !testFlag(FLAG_GLOBAL))
            info.entries.push_back({this, PROCESS_POP});
    }

protected:
    virtual SR_DLL_IMPORT int processSignal(w8_ulong signal, void* value);
    SR_DLL_IMPORT void clearNotify(e_notify notification);
    SR_DLL_IMPORT void setNotify(e_notify notification);
    SR_DLL_IMPORT int testNotify(e_notify notification) const;

public:
    template<class T = srNode> T* findChild(std::string_view name) const
    {
        // Preserve lookup precedence: node, its siblings, then its children.
        const auto find = [&](auto&& self, srNode* node) -> T* {
            if (!node) return nullptr;
            if (node->getName() == name) {
                if (auto* match = dynamic_cast<T*>(node)) return match;
            }
            if (auto* match = self(self, node->getNext())) return match;
            return self(self, node->getChild());
        };
        return find(find, first_child_);
    }
    SR_DLL_IMPORT srNode* getChild() const;
    SR_DLL_IMPORT w8_long getChildCount() const;
    SR_DLL_IMPORT srNode* getNext() const;
    // FUNCTION: SURRENDER 0x10051A40 SYMBOL
    // RECOMP: ?getParent@srNode@@QBEPAV1@XZ
    srNode* getParent() const { return parent_; }
    SR_DLL_IMPORT srNode* getPrev() const;
    SR_DLL_IMPORT int isChildOf(const srNode& node) const;
    SR_DLL_IMPORT int isParentOf(const srNode& node) const;
    static SR_DLL_IMPORT int isSceneGraphLocked();
    static SR_DLL_IMPORT void lockSceneGraph();
    static SR_DLL_IMPORT void unlockSceneGraph();

    [[nodiscard]] SR_DLL_IMPORT srModelViewScope
    scopedWorldSpaceMatrix(class srGERD& renderer) const;
    SR_DLL_IMPORT double getDistance(const srNode& node) const;
    SR_DLL_IMPORT glm::dvec3 getLocation() const;
    SR_DLL_IMPORT double getLocationX() const;
    SR_DLL_IMPORT double getLocationY() const;
    SR_DLL_IMPORT double getLocationZ() const;
    template<class Matrix = glm::dmat3>
    Matrix getRotation() const
    {
        return Matrix(rotation);
    }
    SR_DLL_IMPORT glm::dvec3 getScale() const;

    template<class Scalar>
    struct Coordinates {
        glm::mat<3, 3, Scalar> rotation;
        glm::vec<3, Scalar> location;
        glm::vec<3, Scalar> scale;
    };

    template<class Scalar = double>
    Coordinates<Scalar> getWorldSpaceCoordinates() const
    {
        return {getWorldSpaceRotation<glm::mat<3, 3, Scalar>>(),
                glm::vec<3, Scalar>(getWorldSpaceLocation()),
                glm::vec<3, Scalar>(getWorldSpaceScale())};
    }
    SR_DLL_IMPORT glm::dvec3 getWorldSpaceDOF() const;
    SR_DLL_IMPORT glm::dvec3 getWorldSpaceLocation() const;
    template<class Matrix = glm::dmat4>
    Matrix getWorldSpaceMatrix() const
    {
        checkTransformation();
        if constexpr (std::is_same_v<typename Matrix::value_type, float>)
            return Matrix(world_transform1);
        else
            return Matrix(world_transform0);
    }

    template<class Matrix = glm::dmat3>
    Matrix getWorldSpaceRotation() const
    {
        const auto world = getWorldSpaceMatrix<glm::mat<4, 3, typename Matrix::value_type>>();
        Matrix result(world);
        for (int column = 0; column < 3; ++column)
            result[column] = normalizedOrZero(result[column]);
        return result;
    }
    SR_DLL_IMPORT glm::dvec3 getWorldSpaceScale() const;
    SR_DLL_IMPORT void move(const glm::dvec3& offset);
    SR_DLL_IMPORT void moveBackward(double distance);
    SR_DLL_IMPORT void moveDown(double distance);
    SR_DLL_IMPORT void moveForward(double distance);
    SR_DLL_IMPORT void moveLeft(double distance);
    SR_DLL_IMPORT void moveRight(double distance);
    SR_DLL_IMPORT void moveUp(double distance);
    SR_DLL_IMPORT void offsetLocation(const glm::dvec3& offset);
    SR_DLL_IMPORT void offsetLocation(double x, double y, double z);
    SR_DLL_IMPORT void pitchAt(const glm::dvec3& target, double amount);
    SR_DLL_IMPORT void pitchAt(const srNode* target, double amount);
    SR_DLL_IMPORT void rollAt(const glm::dvec3& target, double amount);
    SR_DLL_IMPORT void rollAt(const srNode* target, double amount);
    SR_DLL_IMPORT void rollUp(double amount);
    SR_DLL_IMPORT void rotate(const glm::dmat3& rotation);
    SR_DLL_IMPORT void rotate(double angle, const glm::dvec3& axis);
    SR_DLL_IMPORT void rotateX(double angle);
    SR_DLL_IMPORT void rotateY(double angle);
    SR_DLL_IMPORT void rotateZ(double angle);
    // Reparenting changes scene membership and preserves the ownership mode.
    // Independently owned nodes remain borrowed until adoptChild transfers them.
    SR_DLL_IMPORT int setParent(srNode* parent, int preserve_world_transform);
    SR_DLL_IMPORT void setLocation(const glm::dvec3& location);
    SR_DLL_IMPORT void setLocation(double x, double y, double z);
    SR_DLL_IMPORT void setLocationX(double x);
    SR_DLL_IMPORT void setLocationY(double y);
    SR_DLL_IMPORT void setLocationZ(double z);
    SR_DLL_IMPORT void setRotation(const glm::mat3& rotation);
    SR_DLL_IMPORT void setRotation(const glm::dmat3& rotation);
    SR_DLL_IMPORT void setRotation(const glm::dvec3& first, const glm::dvec3& second,
                                   double amount);
    SR_DLL_IMPORT void setRotation(const glm::dvec3& direction, double amount);
    SR_DLL_IMPORT void setRotation(double amount, const glm::dvec3& direction);
    SR_DLL_IMPORT void setRotation(double x, double y, double z);
    SR_DLL_IMPORT void setScale(const glm::dvec3& scale);
    SR_DLL_IMPORT void setScale(double scale);
    SR_DLL_IMPORT void setWorldSpaceLocation(const glm::dvec3& location);
    SR_DLL_IMPORT void setWorldSpaceMatrix(const glm::dmat4& matrix);
    SR_DLL_IMPORT void setWorldSpaceRotation(const glm::dmat3& rotation);
    SR_DLL_IMPORT void setFlag(e_flag flag);
    SR_DLL_IMPORT void clearFlag(e_flag flag);
    SR_DLL_IMPORT void notifyChildren(std::uint32_t notifications);
    SR_DLL_IMPORT void notifyDependent();
    SR_DLL_IMPORT void notifyParents(std::uint32_t notifications);
    SR_DLL_IMPORT void signal(w8_ulong signal, void* value);
    SR_DLL_IMPORT int testFlag(e_flag flag) const;
    SR_DLL_IMPORT void yawAt(const glm::dvec3& target, double amount);
    SR_DLL_IMPORT void yawAt(const srNode* target, double amount);

    // Remove scene ownership without the legacy nullptr-to-global-root fallback.
    void detach();

    // Transfer an independently owned node into this owning scene subtree.
    template<class T> T* adoptChild(std::unique_ptr<T> child, int preserve_world_transform = 0)
    {
        std::lock_guard access(sceneGraphCSect);
        if (!child || !child->setParent(this, preserve_world_transform)) return nullptr;
        static_cast<srNode*>(child.get())->parent_owns_ = true;
        return child.release();
    }

private:
    bool parent_owns_ = true;
    void moveAlongAxis(unsigned axis, double distance);
    void setLocationComponent(unsigned component, double value);
    void turnWorldSpaceAxisToward(unsigned axis, const glm::dvec3& direction, double amount);
    SR_DLL_IMPORT void checkTransformation() const;
    SR_DLL_IMPORT void setWSDirty();
    SR_DLL_IMPORT void signalInternal(w8_ulong signal, void* value);
    SR_DLL_IMPORT void unlink();
    SR_DLL_IMPORT void updateTransformation() const;

    static SR_DLL_IMPORT std::recursive_mutex sceneGraphCSect;
    static SR_DLL_IMPORT std::atomic<w8_long> sceneGraphLockCount;

    glm::dmat3 rotation; /* 0x018 */
    glm::dvec3 location; /* 0x060 */
    glm::dvec3 scale; /* 0x078 */
    /* Cached world transforms and the notification word mutate inside the
       const updateTransformation/getter family. */
    mutable glm::dmat4x3 world_transform0; /* 0x090: cached affine world transform */
    mutable glm::mat4x3 world_transform1; /* 0x0f0 */
    mutable std::uint32_t notifications = 0; /* 0x120 */
    std::uint32_t flags = 0; /* 0x124 */

public:
    srNode* next_sibling_; /* 0x128 */
    srNode* previous_sibling_; /* 0x12c */
    srNode* parent_; /* 0x130 */
    srNode* first_child_; /* 0x134 */
};

#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>
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

    srNode(srNode* parent = nullptr);
    // Copies values only; hierarchy links and owned children are not copied.
    srNode(const srNode& other) : srNode(nullptr) { *this = other; }
    virtual ~srNode() override;

    srNode& operator=(const srNode& other);

public:
    virtual void traverse(TraverseInfo& info);
    virtual void process(const ProcessInfo& info, e_processType type);
    virtual BoundInfo getLocalBounds();
    virtual void updateBounds();

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
    virtual int processSignal(w8_ulong signal, void* value);
    void clearNotify(e_notify notification);
    void setNotify(e_notify notification);
    int testNotify(e_notify notification) const;

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
    srNode* getChild() const;
    w8_long getChildCount() const;
    srNode* getNext() const;
    // FUNCTION: SURRENDER 0x10051A40 SYMBOL
    // RECOMP: ?getParent@srNode@@QBEPAV1@XZ
    srNode* getParent() const { return parent_; }
    srNode* getPrev() const;
    int isChildOf(const srNode& node) const;
    int isParentOf(const srNode& node) const;

    [[nodiscard]] srModelViewScope scopedWorldSpaceMatrix(class srGERD& renderer) const;
    double getDistance(const srNode& node) const;
    glm::dvec3 getLocation() const;
    double getLocationX() const;
    double getLocationY() const;
    double getLocationZ() const;
    template<class Matrix = glm::dmat3>
    Matrix getRotation() const
    {
        return Matrix(rotation);
    }
    glm::dvec3 getScale() const;

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
    glm::dvec3 getWorldSpaceDOF() const;
    glm::dvec3 getWorldSpaceLocation() const;
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
    glm::dvec3 getWorldSpaceScale() const;
    void move(const glm::dvec3& offset);
    void moveBackward(double distance);
    void moveDown(double distance);
    void moveForward(double distance);
    void moveLeft(double distance);
    void moveRight(double distance);
    void moveUp(double distance);
    void offsetLocation(const glm::dvec3& offset);
    void offsetLocation(double x, double y, double z);
    void pitchAt(const glm::dvec3& target, double amount);
    void pitchAt(const srNode* target, double amount);
    void rollAt(const glm::dvec3& target, double amount);
    void rollAt(const srNode* target, double amount);
    void rollUp(double amount);
    void rotate(const glm::dmat3& rotation);
    void rotate(double angle, const glm::dvec3& axis);
    void rotateX(double angle);
    void rotateY(double angle);
    void rotateZ(double angle);
    // Parent and sibling links borrow. Reparenting never transfers ownership;
    // nullptr detaches the node from traversal.
    int setParent(srNode* parent, int preserve_world_transform);
    void setLocation(const glm::dvec3& location);
    void setLocation(double x, double y, double z);
    void setLocationX(double x);
    void setLocationY(double y);
    void setLocationZ(double z);
    void setRotation(const glm::mat3& rotation);
    void setRotation(const glm::dmat3& rotation);
    void setRotation(const glm::dvec3& first, const glm::dvec3& second, double amount);
    void setRotation(const glm::dvec3& direction, double amount);
    void setRotation(double amount, const glm::dvec3& direction);
    void setRotation(double x, double y, double z);
    void setScale(const glm::dvec3& scale);
    void setScale(double scale);
    void setWorldSpaceLocation(const glm::dvec3& location);
    void setWorldSpaceMatrix(const glm::dmat4& matrix);
    void setWorldSpaceRotation(const glm::dmat3& rotation);
    void setFlag(e_flag flag);
    void clearFlag(e_flag flag);
    void notifyChildren(std::uint32_t notifications);
    void notifyDependent();
    void notifyParents(std::uint32_t notifications);
    void signal(w8_ulong signal, void* value);
    int testFlag(e_flag flag) const;
    void yawAt(const glm::dvec3& target, double amount);
    void yawAt(const srNode* target, double amount);

    // Explicit ownership for subtrees whose lifetime belongs to this node.
    // Reparenting an adopted node changes only its borrowed scene membership.
    template<class T> T* adoptChild(std::unique_ptr<T> child, int preserve_world_transform = 0)
    {
        if (!child || !child->setParent(this, preserve_world_transform)) return nullptr;
        auto* borrowed = child.get();
        owned_children_.push_back(std::move(child));
        return borrowed;
    }
    void eraseOwnedChild(const srNode* child);

private:
    std::vector<std::unique_ptr<srNode>> owned_children_;
    void moveAlongAxis(unsigned axis, double distance);
    void setLocationComponent(unsigned component, double value);
    void turnWorldSpaceAxisToward(unsigned axis, const glm::dvec3& direction, double amount);
    void checkTransformation() const;
    void setWSDirty();
    void signalInternal(w8_ulong signal, void* value);
    void unlink();
    void updateTransformation() const;

    glm::dmat3 rotation; /* 0x018 */
    glm::dvec3 location; /* 0x060 */
    glm::dvec3 scale; /* 0x078 */
    /* Cached world transforms and the notification word mutate inside the
       const updateTransformation/getter family. */
    mutable glm::dmat4x3 world_transform0; /* 0x090: cached affine world transform */
    mutable glm::mat4x3 world_transform1; /* 0x0f0 */
    mutable std::uint32_t notifications = 0; /* 0x120 */
    std::uint32_t flags = 0; /* 0x124 */

protected:
    // Borrowed traversal links; lifetime belongs to C++ owners.
    srNode* next_sibling_; /* 0x128 */
    srNode* previous_sibling_; /* 0x12c */
    srNode* parent_; /* 0x130 */
    srNode* first_child_; /* 0x134 */
};

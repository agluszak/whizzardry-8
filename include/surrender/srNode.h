#pragma once

#include <atomic>
#include <mutex>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "srFlags.h"
#include "srMath.h"
#include "srTypeRegistry.h"

#include <new>

// VTABLE: SURRENDER 0x10077204
// class srClassSupport<srNode, srClass, 4096>

// VTABLE: SURRENDER 0x100771D0 srNode
class srNode : public srClassSupport<srNode, srClass, 0x1000> {
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
    srNode(const srNode& other) : srNode(nullptr)
    {
        *this = other;
    }
    virtual SR_DLL_IMPORT ~srNode() override;

    SR_DLL_IMPORT srNode& operator=(const srNode& other);

    static SR_DLL_IMPORT const char* sGetClassName();

    virtual SR_DLL_IMPORT void dump(std::ostream& stream) override;

public:
    virtual SR_DLL_IMPORT void traverse(TraverseInfo& info);
    virtual SR_DLL_IMPORT void process(const ProcessInfo& info, e_processType type);
    virtual SR_DLL_IMPORT void getLocalBounds(BoundInfo& bounds);
    virtual SR_DLL_IMPORT void updateBounds();

protected:
    virtual SR_DLL_IMPORT int processSignal(w8_ulong signal, void* value);
    SR_DLL_IMPORT void clearNotify(e_notify notification);
    SR_DLL_IMPORT void setNotify(e_notify notification);
    SR_DLL_IMPORT int testNotify(e_notify notification) const;

public:
    SR_DLL_IMPORT void dumpHierarchy(std::ostream& stream, w8_long indent) const;
    SR_DLL_IMPORT srNode* findChild(std::string_view name) const;
    SR_DLL_IMPORT srNode* findChildByNameAndType(std::string_view name, w8_ulong class_id) const;
    SR_DLL_IMPORT srNode* findParent(std::string_view name) const;
    SR_DLL_IMPORT srNode* findParentByType(w8_ulong class_id) const;
    SR_DLL_IMPORT srNode* getChild() const;
    SR_DLL_IMPORT w8_long getChildCount() const;
    SR_DLL_IMPORT std::string getFullPath() const;
    SR_DLL_IMPORT w8_long getHierarchyLevel() const;
    SR_DLL_IMPORT srNode* getNext() const;
    // FUNCTION: SURRENDER 0x10051A40 SYMBOL
    // RECOMP: ?getParent@srNode@@QBEPAV1@XZ
    srNode* getParent() const
    {
        return parent_;
    }
    SR_DLL_IMPORT srNode* getPrev() const;
    SR_DLL_IMPORT int isChildOf(const srNode& node) const;
    SR_DLL_IMPORT int isParentOf(const srNode& node) const;
    static SR_DLL_IMPORT int isSceneGraphLocked();
    static SR_DLL_IMPORT void lockSceneGraph();
    static SR_DLL_IMPORT void unlockSceneGraph();

    SR_DLL_IMPORT void applyWorldSpaceMatrix(class srGERD& renderer);
    SR_DLL_IMPORT double getDistance(const srNode& node) const;
    SR_DLL_IMPORT glm::dvec3 getLocation() const;
    SR_DLL_IMPORT void getLocation(srVector3T<float>& location) const;
    // FUNCTION: SURRENDER 0x10053DD0 SYMBOL
    // RECOMP: ?getLocation@srNode@@QBEXAAV?$srVector3T@N@@@Z
    void getLocation(srVector3T<double>& location) const
    {
        location = this->location;
    }
    SR_DLL_IMPORT double getLocationX() const;
    SR_DLL_IMPORT double getLocationY() const;
    SR_DLL_IMPORT double getLocationZ() const;
    SR_DLL_IMPORT void getRotation(glm::mat3& rotation) const;
    SR_DLL_IMPORT void getRotation(glm::dmat3& rotation) const;
    SR_DLL_IMPORT glm::dvec3 getScale() const;
    SR_DLL_IMPORT void getWorldSpaceCoordinates(glm::mat3& rotation, glm::vec3& location,
                                                glm::vec3& scale) const;
    SR_DLL_IMPORT void getWorldSpaceCoordinates(glm::dmat3& rotation, glm::dvec3& location,
                                                glm::dvec3& scale) const;
    SR_DLL_IMPORT glm::dvec3 getWorldSpaceDOF() const;
    SR_DLL_IMPORT glm::dvec3 getWorldSpaceLocation() const;
    SR_DLL_IMPORT void getWorldSpaceMatrix(glm::mat4& matrix) const;
    SR_DLL_IMPORT void getWorldSpaceMatrix(glm::dmat4& matrix) const;
    SR_DLL_IMPORT void getWorldSpaceMatrix(glm::mat4x3& matrix) const;
    SR_DLL_IMPORT void getWorldSpaceMatrix(glm::dmat4x3& matrix) const;
    SR_DLL_IMPORT void getWorldSpaceRotation(glm::mat3& rotation) const;
    SR_DLL_IMPORT void getWorldSpaceRotation(glm::dmat3& rotation) const;
    SR_DLL_IMPORT glm::dvec3 getWorldSpaceScale() const;
    SR_DLL_IMPORT void move(const srVector3T<double>& offset);
    SR_DLL_IMPORT void moveBackward(double distance);
    SR_DLL_IMPORT void moveDown(double distance);
    SR_DLL_IMPORT void moveForward(double distance);
    SR_DLL_IMPORT void moveLeft(double distance);
    SR_DLL_IMPORT void moveRight(double distance);
    SR_DLL_IMPORT void moveUp(double distance);
    SR_DLL_IMPORT void offsetLocation(const srVector3T<double>& offset);
    SR_DLL_IMPORT void offsetLocation(double x, double y, double z);
    SR_DLL_IMPORT void pitchAt(const srVector3T<double>& target, double amount);
    SR_DLL_IMPORT void pitchAt(const srNode* target, double amount);
    SR_DLL_IMPORT void rollAt(const srVector3T<double>& target, double amount);
    SR_DLL_IMPORT void rollAt(const srNode* target, double amount);
    SR_DLL_IMPORT void rollUp(double amount);
    SR_DLL_IMPORT void rotate(const glm::dmat3& rotation);
    SR_DLL_IMPORT void rotate(double angle, const srVector3T<double>& axis);
    SR_DLL_IMPORT void rotateX(double angle);
    SR_DLL_IMPORT void rotateY(double angle);
    SR_DLL_IMPORT void rotateZ(double angle);
    // Reparenting changes scene membership and preserves the ownership mode.
    // Independently owned nodes remain borrowed until adoptChild transfers them.
    SR_DLL_IMPORT int setParent(srNode* parent, int preserve_world_transform);
    SR_DLL_IMPORT void setLocation(const srVector3T<double>& location);
    SR_DLL_IMPORT void setLocation(double x, double y, double z);
    SR_DLL_IMPORT void setLocationX(double x);
    SR_DLL_IMPORT void setLocationY(double y);
    SR_DLL_IMPORT void setLocationZ(double z);
    SR_DLL_IMPORT void setRotation(const glm::mat3& rotation);
    SR_DLL_IMPORT void setRotation(const glm::dmat3& rotation);
    SR_DLL_IMPORT void setRotation(const srVector3T<double>& first,
                                   const srVector3T<double>& second, double amount);
    SR_DLL_IMPORT void setRotation(const srVector3T<double>& direction, double amount);
    SR_DLL_IMPORT void setRotation(double amount, const srVector3T<double>& direction);
    SR_DLL_IMPORT void setRotation(double x, double y, double z);
    SR_DLL_IMPORT void setScale(const srVector3T<double>& scale);
    SR_DLL_IMPORT void setScale(double scale);
    SR_DLL_IMPORT void setWorldSpaceLocation(const srVector3T<double>& location);
    SR_DLL_IMPORT void setWorldSpaceMatrix(const glm::dmat4& matrix);
    SR_DLL_IMPORT void setWorldSpaceRotation(const glm::dmat3& rotation);
    SR_DLL_IMPORT void setFlag(e_flag flag);
    SR_DLL_IMPORT void clearFlag(e_flag flag);
    SR_DLL_IMPORT void notifyChildren(const srFlags<e_notify>& notifications);
    SR_DLL_IMPORT void notifyDependent();
    SR_DLL_IMPORT void notifyParents(const srFlags<e_notify>& notifications);
    SR_DLL_IMPORT void signal(w8_ulong signal, void* value);
    SR_DLL_IMPORT int testFlag(e_flag flag) const;
    SR_DLL_IMPORT void yawAt(const srVector3T<double>& target, double amount);
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
    SR_DLL_IMPORT void checkTransformation() const;
    SR_DLL_IMPORT srNode* findChildByNameAndTypeInternal(std::string_view name, w8_ulong class_id);
    SR_DLL_IMPORT srNode* findChildInternal(std::string_view name);
    SR_DLL_IMPORT srNode* findParentByTypeInternal(w8_ulong class_id);
    SR_DLL_IMPORT srNode* findParentInternal(std::string_view name);
    SR_DLL_IMPORT void getFullPathInternal(std::string& path) const;
    SR_DLL_IMPORT void setWSDirty();
    SR_DLL_IMPORT void signalInternal(w8_ulong signal, void* value);
    SR_DLL_IMPORT void unlink();
    SR_DLL_IMPORT void updateTransformation() const;

    static SR_DLL_IMPORT std::recursive_mutex sceneGraphCSect;
    static SR_DLL_IMPORT std::atomic<w8_long> sceneGraphLockCount;

    glm::dmat3 rotation; /* 0x018 */
    glm::dvec3 location; /* 0x060 */
    glm::dvec3 scale;    /* 0x078 */
    /* Cached world transforms and the notification word mutate inside the
       const updateTransformation/getter family. */
    mutable glm::dmat4x3 world_transform0; /* 0x090: cached affine world transform */
    mutable glm::mat4x3 world_transform1;  /* 0x0f0 */
    mutable srFlags<e_notify> notifications;       /* 0x120 */
    srFlags<e_flag> flags;                         /* 0x124 */

public:
    srNode* next_sibling_;     /* 0x128 */
    srNode* previous_sibling_; /* 0x12c */
    srNode* parent_;           /* 0x130 */
    srNode* first_child_;      /* 0x134 */
};


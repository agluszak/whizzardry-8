#pragma once

#include "srArray.h"
#include "srCriticalSection.h"
#include "srFlags.h"
#include "srMath.h"
#include "srTypeRegistry.h"

#include <new>

// VTABLE: SURRENDER 0x10077204
// class srClassSupport<srNode, srClass, 1, 4096>

// VTABLE: SURRENDER 0x100771D0 srNode
class srNode : public srClassSupport<srNode, srClass, true, 0x1000> {
public:
    class TraverseInfo {
    public:
        struct Entry {
            srNode* node;
            w8_ulong value;
        };

        srArray<srNode*> nodes;   /* 0x00 */
        srArray<Entry> entries;   /* 0x08 */
        unsigned int entry_count; /* 0x10 */
        unsigned int node_count;  /* 0x14 */
        /* srScene::process stores the active renderer here; srBounder::traverse
           reads it for the child volume tests. */
        class srGERD* renderer; /* 0x18 */
    };
    struct ProcessInfo {
        class srGERD* renderer;
    };
    /* Bounds record: box, sphere and a trailing state of 0 (empty), 1 (box+sphere) or 2
       (FLAG_GLOBAL). */
    struct BoundInfo {
        srVector3T<float> minimum;
        srVector3T<float> maximum;
        srVector3T<float> center;
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

    enum e_notify { NOTIFY_BOUNDS_DIRTY = 0 };

    srNode(srNode* parent = 0);

    srNode& operator=(const srNode& other);

    static const char* sGetClassName();

    virtual void dump(std::ostream& stream) override;

protected:
    virtual ~srNode() override;

public:
    virtual srClass* vInstance() override;
    virtual void traverse(TraverseInfo& info);
    virtual void process(const ProcessInfo& info, e_processType type);
    virtual void getLocalBounds(BoundInfo& bounds);
    virtual void updateBounds();

protected:
    virtual int processSignal(w8_ulong signal, void* value);
    void clearNotify(e_notify notification);
    void setNotify(e_notify notification);
    int testNotify(e_notify notification) const;

public:
    srNode* cloneHierarchy(srNode* parent);
    void dumpHierarchy(std::ostream& stream, w8_long indent) const;
    srNode* findChild(const char* name) const;
    srNode* findChildByNameAndType(const char* name, w8_ulong class_id) const;
    srNode* findParent(const char* name) const;
    srNode* findParentByType(w8_ulong class_id) const;
    srNode* getChild() const;
    w8_long getChildCount() const;
    char* getFullPath(char* path) const;
    w8_long getFullPathLength() const;
    w8_long getHierarchyLevel() const;
    srNode* getNext() const;
    // FUNCTION: SURRENDER 0x10051A40 SYMBOL
    // RECOMP: ?getParent@srNode@@QBEPAV1@XZ
    srNode* getParent() const
    {
        return parent_;
    }
    srNode* getPrev() const;
    int isChildOf(const srNode& node) const;
    int isParentOf(const srNode& node) const;
    static int isSceneGraphLocked();
    static void lockSceneGraph();
    static void unlockSceneGraph();

    void applyWorldSpaceMatrix(class srGERD& renderer);
    double getDistance(const srNode& node) const;
    srVector3T<double> getLocation() const;
    void getLocation(srVector3T<float>& location) const;
    // FUNCTION: SURRENDER 0x10053DD0 SYMBOL
    // RECOMP: ?getLocation@srNode@@QBEXAAV?$srVector3T@N@@@Z
    void getLocation(srVector3T<double>& location) const
    {
        location = this->location;
    }
    double getLocationX() const;
    double getLocationY() const;
    double getLocationZ() const;
    void getRotation(srMatrix3T<float>& rotation) const;
    void getRotation(srMatrix3T<double>& rotation) const;
    srVector3T<double> getScale() const;
    void getWorldSpaceCoordinates(srMatrix3T<float>& rotation,
                                                srVector3T<float>& location,
                                                srVector3T<float>& scale) const;
    void getWorldSpaceCoordinates(srMatrix3T<double>& rotation,
                                                srVector3T<double>& location,
                                                srVector3T<double>& scale) const;
    srVector3T<double> getWorldSpaceDOF() const;
    srVector3T<double> getWorldSpaceLocation() const;
    void getWorldSpaceMatrix(srMatrix4T<float>& matrix) const;
    void getWorldSpaceMatrix(srMatrix4T<double>& matrix) const;
    void getWorldSpaceMatrix(srMatrix4x3T<float>& matrix) const;
    void getWorldSpaceMatrix(srMatrix4x3T<double>& matrix) const;
    void getWorldSpaceRotation(srMatrix3T<float>& rotation) const;
    void getWorldSpaceRotation(srMatrix3T<double>& rotation) const;
    srVector3T<double> getWorldSpaceScale() const;
    void move(const srVector3T<double>& offset);
    void moveBackward(double distance);
    void moveDown(double distance);
    void moveForward(double distance);
    void moveLeft(double distance);
    void moveRight(double distance);
    void moveUp(double distance);
    void offsetLocation(const srVector3T<double>& offset);
    void offsetLocation(double x, double y, double z);
    void pitchAt(const srVector3T<double>& target, double amount);
    void pitchAt(const srNode* target, double amount);
    void rollAt(const srVector3T<double>& target, double amount);
    void rollAt(const srNode* target, double amount);
    void rollUp(double amount);
    void rotate(const srMatrix3T<double>& rotation);
    void rotate(double angle, const srVector3T<double>& axis);
    void rotateX(double angle);
    void rotateY(double angle);
    void rotateZ(double angle);
    int setParent(srNode* parent, int preserve_world_transform);
    void setLocation(const srVector3T<double>& location);
    void setLocation(double x, double y, double z);
    void setLocationX(double x);
    void setLocationY(double y);
    void setLocationZ(double z);
    void setRotation(const srMatrix3T<float>& rotation);
    void setRotation(const srMatrix3T<double>& rotation);
    void setRotation(const srVector3T<double>& first,
                                   const srVector3T<double>& second, double amount);
    void setRotation(const srVector3T<double>& direction, double amount);
    void setRotation(double amount, const srVector3T<double>& direction);
    void setRotation(double x, double y, double z);
    void setScale(const srVector3T<double>& scale);
    void setScale(double scale);
    void setWorldSpaceLocation(const srVector3T<double>& location);
    void setWorldSpaceMatrix(const srMatrix4T<double>& matrix);
    void setWorldSpaceRotation(const srMatrix3T<double>& rotation);
    void setFlag(e_flag flag);
    void clearFlag(e_flag flag);
    void notifyChildren(const srFlags<e_notify>& notifications);
    void notifyDependent();
    void notifyParents(const srFlags<e_notify>& notifications);
    void signal(w8_ulong signal, void* value);
    int testFlag(e_flag flag) const;
    void yawAt(const srVector3T<double>& target, double amount);
    void yawAt(const srNode* target, double amount);

private:
    void checkTransformation() const;
    srNode* cloneHierarchyInternal(srNode* parent);
    srNode* findChildByNameAndTypeInternal(const char* name, w8_ulong class_id);
    srNode* findChildInternal(const char* name);
    srNode* findParentByTypeInternal(w8_ulong class_id);
    srNode* findParentInternal(const char* name);
    void getFullPathInternal(char* path) const;
    w8_long getFullPathLengthInternal() const;
    void setWSDirty();
    void signalInternal(w8_ulong signal, void* value);
    void unlink();
    void updateTransformation() const;

    static srCriticalSection sceneGraphCSect;
    static w8_long sceneGraphLockCount;

    srMatrix3T<double> rotation; /* 0x018 */
    srVector3T<double> location; /* 0x060 */
    srVector3T<double> scale;    /* 0x078 */
    /* Cached world transforms and the notification word mutate inside the
       const updateTransformation/getter family. */
    mutable srMatrix4x3T<double> world_transform0; /* 0x090: cached affine world transform */
    mutable srMatrix4x3T<float> world_transform1;  /* 0x0f0 */
    mutable srFlags<e_notify> notifications;       /* 0x120 */
    srFlags<e_flag> flags;                         /* 0x124 */

public:
    srNode* next_sibling_;     /* 0x128 */
    srNode* previous_sibling_; /* 0x12c */
    srNode* parent_;           /* 0x130 */
    srNode* first_child_;      /* 0x134 */
};

W8_ABI_ASSERT((sizeof(srNode) == 0x138), "srNode_must_be_0x138");
W8_ABI_ASSERT((sizeof(srNode::TraverseInfo) == 0x1c), "srNode_TraverseInfo_must_be_0x1c");
static_assert((sizeof(srNode::BoundInfo) == 0x2c), "srNode_BoundInfo_must_be_0x2c");

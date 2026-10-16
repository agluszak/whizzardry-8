#pragma once

#include "srNode.h"

// VTABLE: SURRENDER 0x10076f30 srBounder
class srBounder : public srNode {
public:
    enum e_boundMode { BOUND_MODE_DYNAMIC = 0 };

    srBounder(srNode* parent = 0);

    srBounder& operator=(const srBounder& other);

    /* Empty derived destruction is the implicit srNode-base teardown. */

    virtual void traverse(TraverseInfo& info) override;
    virtual void process(const ProcessInfo& info, e_processType type) override;
    virtual void updateBounds() override;

    void forceUpdateBounds();
    e_boundMode getBoundMode() const;
    void setBoundMode(e_boundMode mode);
    void setBounds(const BoundInfo& bounds);

private:
    void checkBounds();
    void getChildBoundingBox(srNode* node);

    e_boundMode bound_mode;
    BoundInfo bounds;
    /* updateBounds stores the inverse of this node's world matrix here;
       getChildBoundingBox composes it with each child's world matrix to bring
       child bounding boxes into bounder space. */
    glm::mat4 inverse_world;
};

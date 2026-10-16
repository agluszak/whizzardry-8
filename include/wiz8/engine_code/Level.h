#pragma once

#include "surrender/srNode.h"

/* Engine Code\ReadLevel.cpp. The allocation sites at 0x0044F2A1 and
   0x004BA3E3 establish the complete size. The constructor/destructor pair
   registers the object as class 0x10007 and proves srNode as its base. */
// VTABLE: WIZ8 0x005ED0F0 stLevel
class stLevel : public srNode {
public:
    explicit stLevel(srNode* parent);
    virtual void traverse(TraverseInfo& info) override;                         /* 0x004BA0E0 */
    virtual void process(const ProcessInfo& info, e_processType type) override; /* 0x004B9DD0 */

    w8_ulong m_render_exclusion_mask; /* 0x138; renderer exclusion mask */
    w8_ulong m_submitted_polygons;    /* 0x13c; submitted polygon count */
};

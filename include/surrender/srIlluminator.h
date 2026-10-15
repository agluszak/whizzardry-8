#pragma once

#include "srNode.h"
#include "srVertexProcessor.h"

// VTABLE: SURRENDER 0x10077028
// class srClassSupport<srIlluminator, srNode, 4608>

// VTABLE: SURRENDER 0x10077068 srVertexProcessor
// VTABLE: SURRENDER 0x10077074 srClassSupport<srIlluminator, srNode, 4608>
// class srIlluminator
class srIlluminator : public srClassSupport<srIlluminator, srNode, 0x1200>,
                      public srVertexProcessor {
public:
    using srVertexProcessor::process;
    srIlluminator(srNode* parent = 0, Ownership ownership = Ownership::parent);
    ~srIlluminator() override = default;
    srIlluminator(const srIlluminator&) = default;

    srIlluminator& operator=(const srIlluminator& other);
    static const char* sGetClassName();
    virtual void traverse(TraverseInfo& info) override;
    virtual void process(const ProcessInfo& info, e_processType type) override;
    w8_ulong getGroupMask() const;
    void setGroupMask(w8_ulong mask);

    w8_ulong group_mask;       /* 0x13c */
    glm::vec3 eye_location;    /* 0x140 */
};

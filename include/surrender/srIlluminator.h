#pragma once

#include "srNode.h"
#include "srVertexProcessor.h"

// VTABLE: SURRENDER 0x10077068 srVertexProcessor
// class srIlluminator
class srIlluminator : public srNode, public srVertexProcessor {
public:
    using srVertexProcessor::process;
    srIlluminator(srNode* parent = 0);
    ~srIlluminator() override = default;
    srIlluminator(const srIlluminator&) = default;

    srIlluminator& operator=(const srIlluminator& other);

    virtual void traverse(TraverseInfo& info) override;
    virtual void process(const ProcessInfo& info, e_processType type) override;
    w8_ulong getGroupMask() const;
    void setGroupMask(w8_ulong mask);

    w8_ulong group_mask;       /* 0x13c */
    glm::vec3 eye_location;    /* 0x140 */
};

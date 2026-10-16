#pragma once

#include "srNode.h"
#include "srVertexProcessor.h"

// VTABLE: SURRENDER 0x10077068 srVertexProcessor
// class srIlluminator
class srIlluminator : public srNode, public srVertexProcessor {
public:
    using srVertexProcessor::process;
    SR_DLL_IMPORT srIlluminator(srNode* parent = 0, Ownership ownership = Ownership::parent);
    ~srIlluminator() override = default;
    srIlluminator(const srIlluminator&) = default;

    SR_DLL_IMPORT srIlluminator& operator=(const srIlluminator& other);

    virtual SR_DLL_IMPORT void traverse(TraverseInfo& info) override;
    virtual SR_DLL_IMPORT void process(const ProcessInfo& info, e_processType type) override;
    SR_DLL_IMPORT w8_ulong getGroupMask() const;
    SR_DLL_IMPORT void setGroupMask(w8_ulong mask);

    w8_ulong group_mask;       /* 0x13c */
    glm::vec3 eye_location;    /* 0x140 */
};

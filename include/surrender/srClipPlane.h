#pragma once

#include "srNode.h"

// VTABLE: SURRENDER 0x10076EF4
// class srClassSupport<srClipPlane, srNode, 5376>

// VTABLE: SURRENDER 0x10076EC0 srClipPlane
// class srClipPlane
class SR_DLL_IMPORT srClipPlane
    : public srClassSupport<srClipPlane, srNode, 0x1500> {
public:

    enum e_clip { CLIP_POSITIONAL_0 = 0 };

    srClipPlane(srNode* parent = 0);

    // FUNCTION: SURRENDER 0x1004A150
    static const char* sGetClassName()
    {
        return "srClipPlane";
    }

    virtual void dump(std::ostream& stream) override;
    virtual void traverse(TraverseInfo& info) override;
    virtual void process(const ProcessInfo& info, e_processType type) override;

    void setClipPlane(const glm::vec4& plane);
    void setClipType(e_clip type);
    glm::vec4 getClipPlane() const;
    e_clip getClipType() const;

protected:
    glm::vec4 clip_plane_; /* 0x138 */
    e_clip clip_type_;             /* 0x148 */
};



#include "glm/ext/vector_float4.hpp"
#include "surrender/srClipPlane.h"
#include "surrender/srGERD.h"
#include "surrender/srNode.h"

srClipPlane::srClipPlane(srNode* parent) : srNode(static_cast<srNode*>(0))
{
    clip_plane_ = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
    clip_type_ = CLIP_POSITIONAL_0;
    if (parent != 0) {
        setParent(parent, 0);
    }
}

void srClipPlane::process(const ProcessInfo& info, e_processType type)
{
    srGERD* renderer = info.renderer;
    if (type == PROCESS_PUSH || type == PROCESS_PUSH_GLOBAL) {
        const auto transform = scopedWorldSpaceMatrix(*renderer);
        renderer->pushClipPlane(clip_plane_, static_cast<srGERD::e_clipMode>(clip_type_));
    } else if (type == PROCESS_POP || type == PROCESS_POP_GLOBAL) {
        renderer->popClipPlane();
    }
}

void srClipPlane::traverse(TraverseInfo& info)
{
    traverseEffect(info);
}

void srClipPlane::setClipType(e_clip type)
{
    clip_type_ = type;
}

srClipPlane::e_clip srClipPlane::getClipType() const
{
    return clip_type_;
}

void srClipPlane::setClipPlane(const glm::vec4& plane)
{
    clip_plane_ = plane;
}

glm::vec4 srClipPlane::getClipPlane() const
{
    return clip_plane_;
}

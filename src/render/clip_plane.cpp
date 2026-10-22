
#include "wiz8/render/clip_plane.h"

#include "glm/ext/vector_float4.hpp"

ClipPlane::ClipPlane()
{
    clip_plane_ = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
    clip_type_ = CLIP_POSITIONAL_0;
}

void ClipPlane::setClipType(e_clip type)
{
    clip_type_ = type;
}

ClipPlane::e_clip ClipPlane::getClipType() const
{
    return clip_type_;
}

void ClipPlane::setClipPlane(const glm::vec4& plane)
{
    clip_plane_ = plane;
}

glm::vec4 ClipPlane::getClipPlane() const
{
    return clip_plane_;
}

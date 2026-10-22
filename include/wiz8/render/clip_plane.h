#pragma once
class WorldScene;
#include <string>
#include <utility>

#include "wiz8/render/transform.h"

class ClipPlane {
public:
    enum e_clip { CLIP_POSITIONAL_0 = 0 };

    ClipPlane();

    void setClipPlane(const glm::vec4& plane);
    void setClipType(e_clip type);
    glm::vec4 getClipPlane() const;
    e_clip getClipType() const;

protected:
    glm::vec4 clip_plane_; /* 0x138 */
    e_clip clip_type_;             /* 0x148 */

public:
    std::string name = "anonymous";
    const std::string& getName() const { return name; }
    void setName(std::string value) { name = std::move(value); }
    bool isNamed() const { return !name.empty() && name != "anonymous"; }
    bool visible = true;
    Transform3D transform;
};

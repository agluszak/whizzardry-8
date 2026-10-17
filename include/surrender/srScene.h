#pragma once

#include <cstdint>

#include "srNode.h"

class srCamera;
class srGERD;

// VTABLE: SURRENDER 0x10077230 srScene
// class srScene
class srScene : public srNode {
public:
    enum e_enable { ENABLE_NODE_PICK_KEYS = 0 };

    srScene(srNode* parent = 0);

    srScene& operator=(const srScene& other);

    virtual void traverse(TraverseInfo& info) override;
    virtual void process(const ProcessInfo& info, e_processType type) override;

    glm::vec3 getAmbientLight() const;
    glm::vec3 getFogColor() const;
    void render(srGERD& renderer, class srCamera* camera);

    void setAmbientLight(float red, float green, float blue);
    void setAmbientLight(const glm::vec3& color);
    void setFogColor(float red, float green, float blue);
    // FUNCTION: SURRENDER 0x10056D40 SYMBOL
    void setFogColor(const glm::vec3& color) { fog_color = color; }

protected:
    std::uint32_t enabled = 0;       /* 0x138 */
    TraverseInfo traversal;
    glm::vec3 ambient_light;         /* 0x174 */
    glm::vec3 fog_color;             /* 0x180 */
};

#pragma once
class WorldScene;
class GpuRenderer;
#include <cmath>
#include <string>
#include <utility>

#include "glm/ext/matrix_float2x2.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/render/gpu_renderer.h"
#include "wiz8/render/transform.h"

struct GroundShadowProjection {
    glm::mat2 matrix;
    glm::vec2 center;

    GroundShadowProjection(const glm::vec3& position, float width, float depth, float angle)
        : center(position.z, position.x)
    {
        const float cosine = std::cos(-angle), sine = std::sin(-angle);
        const glm::mat2 rotation(cosine, sine, -sine, cosine);
        const glm::mat2 scale(0.5f / width, 0, 0, 0.5f / depth);
        const auto transform = scale * rotation;
        // The original second coordinate exchanges x/z relative to the first.
        matrix = glm::mat2(transform[0][0], transform[1][1], transform[1][0], transform[0][1]);
    }

    glm::vec2 coordinates(const glm::vec3& position) const
    {
        return matrix * (glm::vec2(position.z, position.x) - center) + 0.5f;
    }
};

// A concrete floor-projected blob shadow.
class GroundShadow {
public:
    GroundShadow(WorldScene* scene);              /* 0x004D61B0 */
    GroundShadow(const GroundShadow& other); /* 0x004D6430 */
    ~GroundShadow();
    GroundShadow& operator=(const GroundShadow&) = delete;

    /* GrCycle's 0x004A7470 stores the navigator's facing angle here with
       fstp, which is what types it. */
    float angle;
    float depth;
    float width;

private:
public:
    void draw(GpuRenderer& renderer, const DrawTransform& view); /* 0x004D66A0 */

public:
    std::string name = "anonymous";
    const std::string& getName() const { return name; }
    void setName(std::string value) { name = std::move(value); }
    bool isNamed() const { return !name.empty() && name != "anonymous"; }
    bool visible = true;
    Transform3D transform;
    WorldScene* scene = nullptr; // Borrowed typed render membership.
    void setScene(WorldScene* value);
};

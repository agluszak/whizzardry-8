#pragma once

#include "surrender/srGERD.h"
#include "surrender/srNode.h"
#include "glm/ext/matrix_float2x2.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include <cmath>

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
        matrix = glm::mat2(transform[0][0], transform[1][1],
                            transform[1][0], transform[0][1]);
    }

    glm::vec2 coordinates(const glm::vec3& position) const
    {
        return matrix * (glm::vec2(position.z, position.x) - center) + 0.5f;
    }
};

/* The retained registry name, class id, concrete vtable, allocation size and
   clone/factory slots identify this first-party srNode subclass. */
// VTABLE: WIZ8 0x005ed3c4
class stGroundShadow : public srClassSupport<stGroundShadow, srNode, 0x10010> {
public:
    static const char* sGetClassName()
    {
        return "stGroundShadow";
    }
    stGroundShadow(srNode* parent);              /* 0x004D61B0 */
    stGroundShadow(const stGroundShadow& other); /* 0x004D6430 */
    stGroundShadow& operator=(const stGroundShadow&) = default;

    virtual void traverse(TraverseInfo& info) override;                         /* 0x004D6540 */
    virtual void process(const ProcessInfo& info, e_processType type) override; /* 0x004D6640 */

    /* GrCycle's 0x004A7470 stores the navigator's facing angle here with
       fstp, which is what types it. */
    float angle;
    float depth;
    float width;
    unsigned char padding_144[4];

private:
    void renderGroundShadow(srGERD& renderer); /* 0x004D66A0 */
};

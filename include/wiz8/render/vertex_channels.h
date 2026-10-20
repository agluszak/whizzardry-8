#pragma once

#include <cstdint>

#include "wiz8/math.h"
#include "wiz8/render/draw_shader.h"

struct ShadedVertexArrays {
    enum {
        ATTRIBUTE_DIFFUSE = 0x01u,
        ATTRIBUTE_SPECULAR = 0x02u,
        ATTRIBUTE_SPECULAR_ALPHA = 0x04u,
        ATTRIBUTE_ST0 = 0x08u,
        ATTRIBUTE_ST1 = 0x10u,
        ATTRIBUTE_Q0 = 0x20u,
        ATTRIBUTE_Q1 = 0x40u
    };
    glm::vec4* eye_locations;
    glm::vec4* diffuse;
    glm::vec4* specular;
    glm::vec2* st0;
    glm::vec2* st1;
    float* q0;
    float* q1;
    unsigned char* attributes;
};

namespace VertexChannels {
struct MaterialInfo {
    MaterialInfo() = default;

    glm::vec4 diffuse{0};          /* 0x00 */
    glm::vec4 ambient{0};          /* 0x10 */
    glm::vec4 specular{0};      /* 0x20 */
    float translucency = 0;              /* 0x30 */
    float shininess = 0;                 /* 0x34 */
    float value_38 = 0;                  /* 0x38 */
    glm::vec4 emissive{0};      /* 0x3c */
    float fog_scale = 0;                 /* 0x4c */
    std::uint32_t disabled_channels = 0; /* 0x50 */
};

/* Bit indices for enableChannel. */
enum e_channel {
    CHANNEL_DIFFUSE = 1,
    CHANNEL_SPECULAR = 2,
    CHANNEL_ALPHA = 3,
    CHANNEL_FOG = 4,
    CHANNEL_ST0 = 5,
    CHANNEL_ST1 = 6,
    CHANNEL_Q0 = 7,
    CHANNEL_Q1 = 8,
    CHANNEL_LIGHT_AMBIENT = 9,
    CHANNEL_LIGHT_DIFFUSE = 10
};

}

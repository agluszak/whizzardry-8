#pragma once

#include <cstdint>

#include "wiz8/math.h"
#include "wiz8/render/draw_shader.h"

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

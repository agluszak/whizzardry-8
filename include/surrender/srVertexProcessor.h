#pragma once

#include "srMath.h"
#include "srShader.h"

class srMaterialIFace;
class srVertexPipe;

struct srVertexArray {
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

/* Interface with no data beyond its vptr. */
#pragma pack(push, 4)
// VTABLE: SURRENDER 0x10076c74 srVertexProcessor
class srVertexProcessor {
public:
    srVertexProcessor() = default;
    srVertexProcessor(const srVertexProcessor&) = default;
    srVertexProcessor& operator=(const srVertexProcessor&) = default;
    virtual ~srVertexProcessor() {}
    struct MaterialInfo {
        // FUNCTION: WIZ8 0x00424A80
        inline MaterialInfo() : disabled_channels(0) {}

        glm::vec4 diffuse;          /* 0x00 */
        glm::vec4 ambient;          /* 0x10 */
        glm::vec4 specular;      /* 0x20 */
        float translucency;              /* 0x30 */
        float shininess;                 /* 0x34 */
        float value_38;                  /* 0x38 */
        glm::vec4 emissive;      /* 0x3c */
        float fog_scale;                 /* 0x4c */
        w8_ulong disabled_channels; /* 0x50 */
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

protected:
    // FUNCTION: WIZ8 0x0042A360

public:
    // The provider's base vtable contains these bodies, not _purecall.
    // FUNCTION: SURRENDER 0x10035470
    virtual int isActive(srVertexPipe& pipe) { return 1; }
    // FUNCTION: SURRENDER 0x10035480
    virtual void process(srVertexPipe& pipe) {}

public:
};

#pragma pack(pop)

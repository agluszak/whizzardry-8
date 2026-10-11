#pragma once

#include "glm/ext/vector_float3.hpp"

#include "srBoundingSphere.h"

#include <span>
#include <vector>

#include "srCore.h"
#include "srFlags.h"
#include "srMath.h"
#include <memory>
#include "srVertexProcessor.h"

class srMaterialIFace;

class srVertexPipe {
    friend class W8GroundShadowMapper;

public:
    struct Record;

    struct Input {
        w8_ulong vertex_count;
        const w8_ulong* active_vertices;
        int direct_vertex_indices;
        const glm::vec3* positions;
        const glm::vec3* normals;
        glm::vec3 eye_center;
        float eye_radius;
        const glm::mat4* model_view;
        const glm::mat4* normal_matrix;
        srVertexArray* vertex_arrays;
        w8_ulong exclusion_mask;
        glm::vec4 ambient_light;
        std::span<const Record> records;
        srVertexProcessor** processors;
        w8_ulong processor_count;
        float environment_minimum;
        float environment_maximum;
        float environment_scale;
        float environment_inverse_scale;
    };

    SR_DLL_IMPORT srVertexPipe();
    srVertexPipe(const srVertexPipe&) = delete;
    srVertexPipe& operator=(const srVertexPipe&) = delete;

    SR_DLL_IMPORT void applyDiffuseLight(const glm::vec4& light);
    SR_DLL_IMPORT void applyDiffuseLight(const float* values, const glm::vec4& light);
    SR_DLL_IMPORT void applyFog(const float* values);
    SR_DLL_IMPORT void copyDiffuseToSpecular();
    SR_DLL_IMPORT void copySpecularToDiffuse();
    // FUNCTION: SURRENDER 0x1002C310
    inline void disableChannel(srVertexProcessor::e_channel channel)
    {
        channel_mask &= ~(1u << channel);
    }
    // FUNCTION: SURRENDER 0x1002C330
    inline void enableChannel(srVertexProcessor::e_channel channel)
    {
        channel_mask |= 1u << channel;
    }
    SR_DLL_IMPORT const w8_ulong* getAVT() const;
    SR_DLL_IMPORT float* getAlpha();
    SR_DLL_IMPORT srFlags<srVertexProcessor::e_channel> getChannelMask() const;
    SR_DLL_IMPORT const float* getDepthCue();
    SR_DLL_IMPORT glm::vec4* getDiffuse();
    SR_DLL_IMPORT w8_ulong getExclusionMask() const;
    SR_DLL_IMPORT srBoundingSphere getEyeSpaceBoundingSphere() const;
    SR_DLL_IMPORT const glm::vec3* getEyeSpaceDir();
    SR_DLL_IMPORT const float* getEyeSpaceDist();
    SR_DLL_IMPORT const glm::vec4* getEyeSpaceLocation();
    const glm::vec3* getEyeSpaceNormal();
    SR_DLL_IMPORT const float* getEyeSpaceZDist();
    SR_DLL_IMPORT float* getFog();
    SR_DLL_IMPORT const srVertexProcessor::MaterialInfo& getMaterialInfo() const;
    SR_DLL_IMPORT float* getQ(w8_ulong index, int create);
    glm::vec2* getST(w8_ulong index, int create);
    static SR_DLL_IMPORT srFlags<srVertexProcessor::e_channel>
    getShaderDisableMask(const srShader& shader);
    static SR_DLL_IMPORT srFlags<srVertexProcessor::e_channel>
    getShaderDisableMask(const srShader* shader, const w8_ulong* channels,
                         w8_ulong channel_count);
    SR_DLL_IMPORT glm::vec4* getSpecular();
    SR_DLL_IMPORT void* getUserArray(w8_ulong index);
    w8_ulong getVertexCount() const;
    int isChannelAvailable(srVertexProcessor::e_channel channel) const;
    SR_DLL_IMPORT void process(const Input& input);
    SR_DLL_IMPORT void swapDiffuseAndSpecular();
    SR_DLL_IMPORT int testEyeSpaceBounds(const glm::vec3& center, float radius) const;

    /* Per-record render state from Input::records. flags: bit0 vertex colors present, bit1 indexed
       specular into diffuse, bit2 indexed specular into specular, bit3 indexed alpha, bits4/5
       indexed ST0/ST1, bit6 per-vertex material table. channels is the shader's channel-disable
       mask; color_source.format selects the ARGB/vector3/vector4 source for color_source.colors. */
    struct Record {
        enum {
            HAS_COLORS = 0x01u,
            HAS_DIFFUSE_MULTIPLIERS = 0x02u,
            HAS_SPECULAR_MULTIPLIERS = 0x04u,
            HAS_ALPHA = 0x08u,
            HAS_TEXCOORD0 = 0x10u,
            HAS_TEXCOORD1 = 0x20u,
            HAS_VERTEX_MATERIALS = 0x40u
        };
        w8_ulong flags = 0;
        w8_ulong channels = 0;
        srMaterialIFace* material = nullptr;
        struct ColorSource {
            enum e_format { FORMAT_ARGB = 0, FORMAT_VECTOR3 = 1, FORMAT_VECTOR4 = 2 };
            const void* colors = nullptr;
            e_format format = FORMAT_VECTOR4;

            void copyDiffuseColors(glm::vec4* destination, const w8_ulong* indices,
                                   w8_ulong count) const;
        };
        ColorSource color_source{};
        const glm::vec4* spec_for_diffuse = nullptr;
        const glm::vec4* spec_for_specular = nullptr;
        const float* alpha_source = nullptr;
        const glm::vec2* st_source[2]{};
        const std::shared_ptr<srMaterialIFace>* materials = nullptr;
        void* user[12]{};
    };

private:
    /* srMaterial::postProcess blends diffuse/specular/alpha through the batch
       scratch, setup stages and batch state; the SDK friended it. */
    friend class srMaterial;
    /* srLight::process and srLight::isActive read the batch scratch, channel
       masks and lazy-setup flags the same way; the SDK friended it too. */
    friend class srLight;
    SR_DLL_IMPORT void finishDiffuseAlpha();
    SR_DLL_IMPORT void finishSpecularFog();
    SR_DLL_IMPORT void processVertexBuffer();
    static SR_DLL_IMPORT w8_ulong scanChangeIndexed(const w8_ulong* first,
                                                         w8_ulong first_count,
                                                         const w8_ulong* second,
                                                         w8_ulong second_count);
    SR_DLL_IMPORT void setMaterial(srMaterialIFace* material);
    SR_DLL_IMPORT void setupAlpha();
    SR_DLL_IMPORT void setupDepthCue();
    SR_DLL_IMPORT void setupDiffuse();
    SR_DLL_IMPORT void setupEyeSpaceDirAndDist();
    SR_DLL_IMPORT void setupEyeSpaceNormal();
    SR_DLL_IMPORT void setupEyeSpaceZDist();
    SR_DLL_IMPORT void setupFog();
    SR_DLL_IMPORT void setupQ(w8_ulong index);
    SR_DLL_IMPORT void setupST(w8_ulong index);
    SR_DLL_IMPORT void setupSpecular();

    friend class srEnvironmentMapper;
    friend class srLight;

    struct Scratch {
        enum {
            READY_EYE_DIRECTION = 0x01u,
            READY_EYE_DISTANCE = 0x02u,
            READY_EYE_Z_DISTANCE = 0x04u,
            READY_EYE_NORMALS = 0x08u,
            READY_DEPTH_CUE = 0x10u
        };
        glm::vec3 dir[0x40];
        glm::vec3 normals[0x40];
        float dist[0x40];
        float z_dist[0x40];
        float depth_cue[0x40];
        float alpha[0x40];
        float fog[0x40];
        w8_ulong flags = 0;
    };

    /* Each lazy setup fills its batch range before use. Preserve the former heap's
       16-byte base alignment without a separate allocation or pointer alias. */
    alignas(16) Scratch scratch;
    /* Borrowed processors selected for this invocation; the pipe owns only the list. */
    std::vector<srVertexProcessor*> active_processors;
    w8_ulong channel_mask;                    /* 0x0c */
    w8_ulong lazy_setup_mask;                 /* 0x10 */
    srVertexProcessor::MaterialInfo material_info; /* 0x14 through 0x67 */
    srMaterialIFace* material;                     /* 0x68 */
    const Input* input;                            /* 0x6c */
    const w8_ulong* avt;                      /* 0x70 */
    const Record* current_record;                  /* 0x74 */
    srVertexArray* vertex_array;                   /* 0x78 */
    glm::vec4* eye_space_locations;        /* 0x7c */
    w8_ulong batch_base;                      /* 0x80 */
    w8_ulong sub_batch_offset;                /* 0x84 */
    w8_ulong vertex_count;                    /* 0x88 */
    w8_ulong batch_count;                     /* 0x8c */
};

// FUNCTION: SURRENDER 0x1002C4B0 SYMBOL
// RECOMP: ?getVertexCount@srVertexPipe@@QBEKXZ
inline w8_ulong srVertexPipe::getVertexCount() const
{
    return vertex_count;
}

// FUNCTION: SURRENDER 0x1002C4E0 SYMBOL
// RECOMP: ?isChannelAvailable@srVertexPipe@@QBEHW4e_channel@srVertexProcessor@@@Z
inline int srVertexPipe::isChannelAvailable(srVertexProcessor::e_channel channel) const
{
    return ((1u << channel) & channel_mask) != 0;
}

// FUNCTION: SURRENDER 0x1002C5C0 SYMBOL
// RECOMP: ?getEyeSpaceNormal@srVertexPipe@@QAEPBV?$srVector3T@M@@XZ
inline const glm::vec3* srVertexPipe::getEyeSpaceNormal()
{
    if ((scratch.flags & srVertexPipe::Scratch::READY_EYE_NORMALS) == 0) {
        setupEyeSpaceNormal();
    }
    return scratch.normals + sub_batch_offset;
}

// FUNCTION: SURRENDER 0x1002C780 SYMBOL
// RECOMP: ?getST@srVertexPipe@@QAEPAV?$srVector2T@M@@KH@Z
inline glm::vec2* srVertexPipe::getST(w8_ulong index, int create)
{
    srCore.getStatisticsManager()->statistics.texture_coordinate_operations += vertex_count;
    if ((create == 0) &&
        ((lazy_setup_mask & (1 << (index + srVertexProcessor::CHANNEL_ST0))) == 0)) {
        setupST(index);
    }
    lazy_setup_mask |= 1 << (index + srVertexProcessor::CHANNEL_ST0);
    return (&vertex_array->st0)[index] + batch_base + sub_batch_offset;
}

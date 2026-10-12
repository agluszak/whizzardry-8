#pragma once

#include <cstdint>
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
        Input(const glm::mat4& model_view, const glm::mat4& normal_matrix)
            : model_view(model_view), normal_matrix(normal_matrix) {}

        std::span<const std::uint32_t> active_vertices;
        bool direct_vertex_indices = false;
        std::span<const glm::vec3> positions;
        std::span<const glm::vec3> normals;
        srBoundingSphere eye_bounds;
        const glm::mat4& model_view;
        const glm::mat4& normal_matrix;
        std::span<srVertexArray> vertex_arrays;
        std::uint32_t exclusion_mask = 0;
        glm::vec4 ambient_light{0};
        std::span<const Record> records;
        std::span<srVertexProcessor* const> processors;
        float environment_minimum = 0;
        float environment_maximum = 0;
        float environment_scale = 0;
        float environment_inverse_scale = 0;

        std::uint32_t geometryIndex(std::uint32_t processed_index) const
        {
            return direct_vertex_indices ? processed_index : active_vertices[processed_index];
        }
    };

    SR_DLL_IMPORT srVertexPipe();
    srVertexPipe(const srVertexPipe&) = delete;
    srVertexPipe& operator=(const srVertexPipe&) = delete;

    SR_DLL_IMPORT void applyDiffuseLight(const glm::vec4& light);
    SR_DLL_IMPORT void applyDiffuseLight(std::span<const float> values, const glm::vec4& light);
    SR_DLL_IMPORT void applyFog(std::span<const float> values);
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
    SR_DLL_IMPORT std::span<const std::uint32_t> getAVT() const;
    SR_DLL_IMPORT std::span<float> getAlpha();
    SR_DLL_IMPORT srFlags<srVertexProcessor::e_channel> getChannelMask() const;
    SR_DLL_IMPORT std::span<const float> getDepthCue();
    SR_DLL_IMPORT std::span<glm::vec4> getDiffuse();
    SR_DLL_IMPORT std::uint32_t getExclusionMask() const;
    SR_DLL_IMPORT srBoundingSphere getEyeSpaceBoundingSphere() const;
    SR_DLL_IMPORT std::span<const glm::vec3> getEyeSpaceDir();
    SR_DLL_IMPORT std::span<const float> getEyeSpaceDist();
    SR_DLL_IMPORT std::span<const glm::vec4> getEyeSpaceLocation();
    std::span<const glm::vec3> getEyeSpaceNormal();
    SR_DLL_IMPORT std::span<const float> getEyeSpaceZDist();
    SR_DLL_IMPORT std::span<float> getFog();
    SR_DLL_IMPORT const srVertexProcessor::MaterialInfo& getMaterialInfo() const;
    SR_DLL_IMPORT float* getQ(std::uint32_t index, std::int32_t create);
    glm::vec2* getST(std::uint32_t index, std::int32_t create);
    static SR_DLL_IMPORT srFlags<srVertexProcessor::e_channel>
    getShaderDisableMask(const srShader& shader);
    static SR_DLL_IMPORT srFlags<srVertexProcessor::e_channel>
    getShaderDisableMask(const srShader* shader, const std::uint32_t* channels,
                         std::uint32_t channel_count);
    SR_DLL_IMPORT std::span<glm::vec4> getSpecular();
    SR_DLL_IMPORT void* getUserArray(std::uint32_t index);
    std::uint32_t getVertexCount() const;
    bool isChannelAvailable(srVertexProcessor::e_channel channel) const;
    SR_DLL_IMPORT void process(const Input& input);
    SR_DLL_IMPORT void swapDiffuseAndSpecular();
    SR_DLL_IMPORT bool testEyeSpaceBounds(const glm::vec3& center, float radius) const;

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
        std::uint32_t flags = 0;
        std::uint32_t channels = 0;
        srMaterialIFace* material = nullptr;
        struct ColorSource {
            enum e_format { FORMAT_ARGB = 0, FORMAT_VECTOR3 = 1, FORMAT_VECTOR4 = 2 };
            const void* colors = nullptr;
            e_format format = FORMAT_VECTOR4;

            void copyDiffuseColors(glm::vec4* destination, const std::uint32_t* indices,
                                   std::uint32_t count) const;
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
    static SR_DLL_IMPORT std::uint32_t scanChangeIndexed(const std::uint32_t* first,
                                                         std::uint32_t first_count,
                                                         const std::uint32_t* second,
                                                         std::uint32_t second_count);
    SR_DLL_IMPORT void setMaterial(srMaterialIFace* material);
    SR_DLL_IMPORT void setupAlpha();
    SR_DLL_IMPORT void setupDepthCue();
    SR_DLL_IMPORT void setupDiffuse();
    SR_DLL_IMPORT void setupEyeSpaceDirAndDist();
    SR_DLL_IMPORT void setupEyeSpaceNormal();
    SR_DLL_IMPORT void setupEyeSpaceZDist();
    SR_DLL_IMPORT void setupFog();
    SR_DLL_IMPORT void setupQ(std::uint32_t index);
    SR_DLL_IMPORT void setupST(std::uint32_t index);
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
        std::uint32_t flags = 0;
    };

    /* Each lazy setup fills its batch range before use. Preserve the former heap's
       16-byte base alignment without a separate allocation or pointer alias. */
    alignas(16) Scratch scratch;
    /* Borrowed processors selected for this invocation; the pipe owns only the list. */
    std::vector<srVertexProcessor*> active_processors;
    std::uint32_t channel_mask;                    /* 0x0c */
    std::uint32_t lazy_setup_mask;                 /* 0x10 */
    srVertexProcessor::MaterialInfo material_info; /* 0x14 through 0x67 */
    srMaterialIFace* material;                     /* 0x68 */
    const Input* input;                            /* 0x6c */
    const std::uint32_t* avt;                      /* 0x70 */
    const Record* current_record;                  /* 0x74 */
    srVertexArray* vertex_array;                   /* 0x78 */
    glm::vec4* eye_space_locations;        /* 0x7c */
    std::uint32_t batch_base;                      /* 0x80 */
    std::uint32_t sub_batch_offset;                /* 0x84 */
    std::uint32_t vertex_count;                    /* 0x88 */
    std::uint32_t batch_count;                     /* 0x8c */
private:
    using SetupChannel = void (srVertexPipe::*)();
    template<class T, std::size_t Count>
    std::span<const T> scratchChannel(const T (&values)[Count], std::uint32_t ready, SetupChannel setup)
    {
        if ((scratch.flags & ready) == 0) (this->*setup)();
        return std::span<const T>(values).subspan(sub_batch_offset, vertex_count);
    }
    void prepareOutputChannel(srVertexProcessor::e_channel channel, std::uint32_t& operations,
                              SetupChannel setup);
    void copyColorChannel(std::span<const glm::vec4> source, glm::vec4* destination,
                          srVertexProcessor::e_channel channel);

};

// FUNCTION: SURRENDER 0x1002C4B0 SYMBOL
// RECOMP: ?getVertexCount@srVertexPipe@@QBEKXZ
inline std::uint32_t srVertexPipe::getVertexCount() const
{
    return vertex_count;
}

// FUNCTION: SURRENDER 0x1002C4E0 SYMBOL
// RECOMP: ?isChannelAvailable@srVertexPipe@@QBEHW4e_channel@srVertexProcessor@@@Z
inline bool srVertexPipe::isChannelAvailable(srVertexProcessor::e_channel channel) const
{
    return ((1u << channel) & channel_mask) != 0;
}

// FUNCTION: SURRENDER 0x1002C5C0 SYMBOL
// RECOMP: ?getEyeSpaceNormal@srVertexPipe@@QAEPBV?$srVector3T@M@@XZ
inline std::span<const glm::vec3> srVertexPipe::getEyeSpaceNormal()
{
    return scratchChannel(scratch.normals, Scratch::READY_EYE_NORMALS, &srVertexPipe::setupEyeSpaceNormal);
}

inline glm::vec2* srVertexPipe::getST(std::uint32_t index, std::int32_t create)
{
    srCore.getStatisticsManager()->statistics.texture_coordinate_operations += vertex_count;
    if ((create == 0) &&
        ((lazy_setup_mask & (1 << (index + srVertexProcessor::CHANNEL_ST0))) == 0)) {
        setupST(index);
    }
    lazy_setup_mask |= 1 << (index + srVertexProcessor::CHANNEL_ST0);
    return (&vertex_array->st0)[index] + batch_base + sub_batch_offset;
}

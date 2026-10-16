#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "srBoundingSphere.h"
#include "srMath.h"
#include "srVertexProcessor.h"

class srMaterialIFace;

class srVertexPipe {
    friend class W8GroundShadowMapper;

public:
    struct Record;

    struct Input {
        Input(const glm::mat4& model_view, const glm::mat4& normal_matrix)
            : model_view(model_view), normal_matrix(normal_matrix)
        {
        }

        std::span<const w8_ulong> active_vertices;
        bool direct_vertex_indices = false;
        std::span<const glm::vec3> positions;
        std::span<const glm::vec3> normals;
        srBoundingSphere eye_bounds;
        const glm::mat4& model_view;
        const glm::mat4& normal_matrix;
        std::span<srVertexArray> vertex_arrays;
        w8_ulong exclusion_mask = 0;
        glm::vec4 ambient_light{0};
        std::span<const Record> records;
        std::span<srVertexProcessor* const> processors;
        float environment_minimum = 0;
        float environment_maximum = 0;
        float environment_scale = 0;
        float environment_inverse_scale = 0;

        w8_ulong geometryIndex(w8_ulong processed_index) const
        {
            return direct_vertex_indices ? processed_index : active_vertices[processed_index];
        }
    };

    srVertexPipe();
    srVertexPipe(const srVertexPipe&) = delete;
    srVertexPipe& operator=(const srVertexPipe&) = delete;

    void applyDiffuseLight(const glm::vec4& light);
    void applyDiffuseLight(std::span<const float> values, const glm::vec4& light);
    void applyFog(std::span<const float> values);
    void copyDiffuseToSpecular();
    void copySpecularToDiffuse();
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
    std::span<const w8_ulong> getAVT() const;
    std::span<float> getAlpha();
    std::span<const float> getDepthCue();
    std::span<glm::vec4> getDiffuse();
    w8_ulong getExclusionMask() const;
    srBoundingSphere getEyeSpaceBoundingSphere() const;
    std::span<const glm::vec3> getEyeSpaceDir();
    std::span<const float> getEyeSpaceDist();
    std::span<const glm::vec4> getEyeSpaceLocation();
    std::span<const glm::vec3> getEyeSpaceNormal();
    std::span<const float> getEyeSpaceZDist();
    std::span<float> getFog();
    const srVertexProcessor::MaterialInfo& getMaterialInfo() const;
    float* getQ(w8_ulong index, int create);
    glm::vec2* getST(w8_ulong index, int create);
    static std::uint32_t getShaderDisableMask(const srShader& shader);
    static std::uint32_t getShaderDisableMask(const srShader* shader, const w8_ulong* channels,
                                              w8_ulong channel_count);
    std::span<glm::vec4> getSpecular();
    void* getUserArray(w8_ulong index);
    w8_ulong getVertexCount() const;
    int isChannelAvailable(srVertexProcessor::e_channel channel) const;
    void process(const Input& input);
    void swapDiffuseAndSpecular();
    int testEyeSpaceBounds(const glm::vec3& center, float radius) const;

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
    void finishDiffuseAlpha();
    void finishSpecularFog();
    void processVertexBuffer();
    static w8_ulong scanChangeIndexed(const w8_ulong* first, w8_ulong first_count,
                                      const w8_ulong* second, w8_ulong second_count);
    void setMaterial(srMaterialIFace* material);
    void setupAlpha();
    void setupDepthCue();
    void setupDiffuse();
    void setupEyeSpaceDirAndDist();
    void setupEyeSpaceNormal();
    void setupEyeSpaceZDist();
    void setupFog();
    void setupQ(w8_ulong index);
    void setupST(w8_ulong index);
    void setupSpecular();

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
    w8_ulong channel_mask; /* 0x0c */
    w8_ulong lazy_setup_mask; /* 0x10 */
    srVertexProcessor::MaterialInfo material_info; /* 0x14 through 0x67 */
    srMaterialIFace* material; /* 0x68 */
    const Input* input; /* 0x6c */
    const w8_ulong* avt; /* 0x70 */
    const Record* current_record; /* 0x74 */
    srVertexArray* vertex_array; /* 0x78 */
    glm::vec4* eye_space_locations; /* 0x7c */
    w8_ulong batch_base; /* 0x80 */
    w8_ulong sub_batch_offset; /* 0x84 */
    w8_ulong vertex_count; /* 0x88 */
    w8_ulong batch_count; /* 0x8c */
private:
    using SetupChannel = void (srVertexPipe::*)();
    template<class T, std::size_t Count>
    std::span<const T> scratchChannel(const T (&values)[Count], w8_ulong ready, SetupChannel setup)
    {
        if ((scratch.flags & ready) == 0) (this->*setup)();
        return std::span<const T>(values).subspan(sub_batch_offset, vertex_count);
    }
    void prepareOutputChannel(srVertexProcessor::e_channel channel, SetupChannel setup);
    void copyColorChannel(std::span<const glm::vec4> source, glm::vec4* destination,
                          srVertexProcessor::e_channel channel);
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
inline std::span<const glm::vec3> srVertexPipe::getEyeSpaceNormal()
{
    return scratchChannel(scratch.normals, Scratch::READY_EYE_NORMALS,
                          &srVertexPipe::setupEyeSpaceNormal);
}

inline glm::vec2* srVertexPipe::getST(w8_ulong index, int create)
{

    if ((create == 0) &&
        ((lazy_setup_mask & (1 << (index + srVertexProcessor::CHANNEL_ST0))) == 0)) {
        setupST(index);
    }
    lazy_setup_mask |= 1 << (index + srVertexProcessor::CHANNEL_ST0);
    return (&vertex_array->st0)[index] + batch_base + sub_batch_offset;
}

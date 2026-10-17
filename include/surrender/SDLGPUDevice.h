#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_rect.h>

#include "srMath.h"
#include "srRendererDefs.h"
#include "srShader.h"
#include "wiz8/surface_storage.h"

struct SDL_Window;

class SDLGPUDevice {
public:
    // Backend identity only. Cache policy and CPU mip storage belong to GERD.
    struct Texture {
        SDL_GPUTexture* handle = nullptr;
        w8_ulong size = 0;
        uint32_t width = 0, height = 0, levels = 0;
    };
    struct TextureRegion {
        w8_ulong x, y, width, height;
    };
    static constexpr w8_ulong max_back_buffer_width = 8192;
    static constexpr w8_ulong max_back_buffer_height = 8192;
    static constexpr w8_ulong texture_min_dim = 1;
    static constexpr w8_ulong texture_max_dim = 4096;
    static constexpr w8_ulong texture_max_aspect = 4096;
    static constexpr w8_ulong max_texture_stages = 2;
    static constexpr w8_ulong renderer_batch_limit = 256;
    // CPU accumulation remains in GERD; the backend clears color, depth and stencil.
    struct ClearValues {
        glm::vec4 color;
        float depth;
        w8_ulong stencil;
    };
    /* Scissor rectangle {left, top, right, bottom}. */
    struct Scissor {
        w8_ulong left, top, right, bottom;
    };
    // View rectangle and depth range used by SDL GPU.
    struct ViewPort {
        w8_long x, y, width, height;
        /* Depth range forwarded by applyViewStateChanges. */
        float min_depth, max_depth;
    };
    // Resolved SDL sampler settings, also used as the sampler cache key.
    struct TexParms {
        SDL_GPUFilter mag = SDL_GPU_FILTER_NEAREST;
        SDL_GPUFilter min = SDL_GPU_FILTER_NEAREST;
        SDL_GPUSamplerMipmapMode mipmap = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        SDL_GPUSamplerAddressMode wrap_s = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        SDL_GPUSamplerAddressMode wrap_t = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        float mipmap_bias = 0;
        float max_lod = 0;
        auto operator<=>(const TexParms&) const = default;
    };

    enum e_buffer { BUFFER_COLOR = 1, BUFFER_DEPTH = 2, BUFFER_STENCIL = 4 };
    enum e_cullMode { CULL_NONE = 0, CULL_BACK = 1, CULL_FRONT = 2 };

private:
    /* One interleaved vertex in the device's own stream. */
    struct Vertex {
        glm::vec4 position;
        glm::vec4 diffuse;
        glm::vec4 specular;
        glm::vec3 texcoord0;
        glm::vec3 texcoord1;
        float fog;
    };

    // SDL vertex attributes below consume this exact packed GPU layout.
    static_assert(sizeof(Vertex) == 76);
    static_assert(offsetof(Vertex, diffuse) == 16 && offsetof(Vertex, specular) == 32);
    static_assert(offsetof(Vertex, texcoord0) == 48 && offsetof(Vertex, texcoord1) == 60);
    static_assert(offsetof(Vertex, fog) == 72);

    struct VertexUniforms {
        glm::mat4 projection;
    };

    struct FragmentUniforms {
        glm::vec4 fog_color;
        glm::ivec4 mode;
        glm::vec4 alpha;
    };

    static_assert(sizeof(FragmentUniforms) == 48);
    static_assert(offsetof(FragmentUniforms, mode) == 16 &&
                  offsetof(FragmentUniforms, alpha) == 32);

    enum PrimitiveClass {
        PRIMITIVE_CLASS_TRIANGLES,
        PRIMITIVE_CLASS_LINES,
        PRIMITIVE_CLASS_POINTS
    };

    /* Fixed-function state that SDL GPU bakes into a pipeline. */
    struct PipelineKey {
        static constexpr uint32_t STENCIL_REPLACE = 1u << 30;
        uint32_t value = 0;
        w8_long offset = 0;

        PipelineKey() = default;
        PipelineKey(const srShader& shader, SDLGPUDevice::e_cullMode cull, PrimitiveClass primitive,
                    w8_long bias)
        {
            offset = bias;
            const uint32_t packed_shader =
                shader.value &
                (srShader::PASS_MASK | srShader::MASK_DEPTH_WRITE | srShader::MASK_COLOR_WRITE |
                 0x7u << srShader::DSTBLEND_SHIFT | 0x3u << srShader::SRCBLEND_SHIFT);
            value = packed_shader | (uint32_t)cull << 24 | (uint32_t)primitive << 28;
        }

        bool operator<(const PipelineKey& other) const
        {
            return value != other.value ? value < other.value : offset < other.offset;
        }
    };

    struct DrawCommand {
        enum Kind { DRAW, CLEAR };
        Kind kind;
        /* DRAW */
        PipelineKey pipeline_key;
        uint32_t first_index;
        uint32_t index_count;
        SDL_GPUTexture* textures[2];
        SDL_GPUSampler* samplers[2];
        VertexUniforms vertex_uniforms;
        FragmentUniforms fragment_uniforms;
        /* DRAW and CLEAR */
        SDL_Rect scissor;
        SDL_GPUViewport viewport;
        /* CLEAR */
        uint32_t clear_buffers;
        SDL_FColor clear_color;
        float clear_depth;
        uint8_t clear_stencil;
    };

    struct PendingUpload {
        SDL_GPUTexture* texture;
        uint32_t level;
        uint32_t x, y, width, height;
        size_t offset;
    };

public:
    SDLGPUDevice();
    SDLGPUDevice(const SDLGPUDevice&) = delete;
    SDLGPUDevice& operator=(const SDLGPUDevice&) = delete;

    ~SDLGPUDevice();

    bool openWindow(w8_ulong target_width, w8_ulong target_height);

    void closeWindow();

    void flushFrame();

    void flipFrame();

    void clearBuffers(std::uint32_t buffers);

    SDL_Surface* lockBuffer();

    void unlockBuffer(bool modified = false);

    void setSwapInterval(w8_ulong interval);

    void setScissor(const Scissor& value);

    void setViewPort(const ViewPort& value);

    void setClearValues(const ClearValues& values);

    void setFogColor(const glm::vec4& color);

    void setShader(const srShader& value);

    void setTextureParameters(w8_ulong stage, const TexParms& parms);

    void bindTexture(w8_ulong stage, const Texture& texture);

    void deleteTexture(Texture& texture);

    Texture createTexture(w8_ulong width, w8_ulong height, w8_ulong levels);

    void uploadTexture(const Texture& texture, w8_ulong level, TextureRegion region,
                       std::span<const unsigned char> pixels, w8_ulong pitch);

    bool createContext(SDL_Window* target_window);

    void deleteContext();

    void setCullMode(e_cullMode mode);

    void setProjectionMatrix(const glm::mat4& matrix);

    void setVertexArrayInfo(const srRendererDefs::VertexArrayInfo* info);

    void drawElements(srRendererDefs::e_primitive primitive, std::span<const w8_ulong> indices);

    void drawArrays(srRendererDefs::e_primitive primitive, w8_long first, w8_ulong count);

    void setPolygonOffset(w8_long offset);

private:
    SDL_GPUShader* createShader(SDL_GPUShaderStage stage, uint32_t sampler_count);

    Texture createDeviceTexture(uint32_t texture_width, uint32_t texture_height, uint32_t levels);

    template<class Vector>
    Vector fetch(srRendererDefs::e_vertexArray slot, uint32_t index, Vector fallback) const;

    void appendVertex(uint32_t index);

    /* Expands every primitive to a list: SDL GPU has no fans, and lists let
       consecutive draws share one vertex stream. */
    void draw(srRendererDefs::e_primitive primitive, uint32_t count, const w8_ulong* indices,
              uint32_t first);

    SDL_GPUSampler* sampler(const TexParms& parameters);

    SDL_GPUGraphicsPipeline* pipeline(const PipelineKey& key);

    SDL_GPUCommandBuffer* commandBuffer();

    void uploadPending(SDL_GPUCommandBuffer* command);

    void replay(SDL_GPUCommandBuffer* command);

    SDL_GPURenderPass* beginPass(SDL_GPUCommandBuffer* command, const DrawCommand* clear);

    void releaseDeleted();

    /* Records pending work; when presenting, blits the frame to the swapchain. */
    void flush(bool present);

    /* Waits for the frame so far and copies the color target into readback. */
    void download();

    void upload();

    SDL_GPUDevice* device;
    SDL_Window* window;
    SDL_GPUTexture* color_target;
    SDL_GPUTexture* depth_target;
    SDL_GPUTextureFormat depth_format;
    SDL_GPUShader* vertex_shader;
    SDL_GPUShader* fragment_shader;
    std::map<TexParms, SDL_GPUSampler*> samplers;
    SDL_GPUBuffer* vertex_buffer;
    uint32_t vertex_buffer_size;
    SDL_GPUBuffer* index_buffer = nullptr;
    uint32_t index_buffer_size = 0;
    SDL_GPUCommandBuffer* command_buffer;
    uint32_t width;
    uint32_t height;

    srShader shader;
    e_cullMode cull_mode;
    w8_long polygon_offset;
    VertexUniforms projection;
    ClearValues clear_values;
    glm::vec4 fog_color;
    SDL_Rect scissor;
    SDL_GPUViewport viewport;
    srRendererDefs::VertexArrayInfo vertex_arrays;
    SDL_GPUTexture* bound[2];
    TexParms parameters[2];

    std::map<PipelineKey, SDL_GPUGraphicsPipeline*> pipelines;
    std::unordered_set<SDL_GPUTexture*> textures;
    SDL_GPUTexture* white_texture = nullptr;
    std::vector<SDL_GPUTexture*> released;
    std::vector<PendingUpload> uploads;
    std::vector<uint32_t> upload_pixels;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::unordered_map<uint32_t, uint32_t> vertex_remap;
    std::vector<DrawCommand> commands;
    SDLSurfaceOwner readback{nullptr, SDL_DestroySurface};
    bool readback_dirty;
    int locked;
    bool trace;
};

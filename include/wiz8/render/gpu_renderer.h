#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>

#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int3.hpp"
#include "wiz8/math.h"
#include "wiz8/render/draw_inputs.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_draw.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/vertex_shading.h"
#include "wiz8/render/vertex_stream.h"
#include "wiz8/surface_storage.h"
struct SDL_Surface;
class MeshInstance;
class Light;
class FogParams;
struct ShadedVertexArrays;

struct OverlayQuad;

class GpuRenderer {
public:
    // Backend identity only. Cache policy and CPU mip storage belong to GERD.
    struct NativeTexture {
        SDL_GPUTexture* handle = nullptr;
        w8_ulong size = 0;
        uint32_t width = 0, height = 0, levels = 0;
    };
    struct GpuTextureRegion {
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
    struct GpuClearValues {
        glm::vec4 color;
        float depth;
        w8_ulong stencil;
    };
    /* Scissor rectangle {left, top, right, bottom}. */
    struct GpuScissor {
        w8_ulong left, top, right, bottom;
    };
    // View rectangle and depth range used by SDL GPU.
    struct GpuViewPort {
        w8_long x, y, width, height;
        /* Depth range forwarded by applyViewStateChanges. */
        float min_depth, max_depth;
    };
    // Resolved SDL sampler settings, also used as the sampler cache key.
    struct GpuTexParms {
        SDL_GPUFilter mag = SDL_GPU_FILTER_NEAREST;
        SDL_GPUFilter min = SDL_GPU_FILTER_NEAREST;
        SDL_GPUSamplerMipmapMode mipmap = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        SDL_GPUSamplerAddressMode wrap_s = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        SDL_GPUSamplerAddressMode wrap_t = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        float mipmap_bias = 0;
        float max_lod = 0;
        auto operator<=>(const GpuTexParms&) const = default;
    };

    enum GpuBuffer { GPU_BUFFER_COLOR = 1, GPU_BUFFER_DEPTH = 2, GPU_BUFFER_STENCIL = 4 };

    // One draw and every input it reads besides the target's viewport and
    // scissor. Nothing a draw uses persists into the next one.
    struct DrawCall {
        vertexStream::e_primitive primitive = vertexStream::PRIMITIVE_TRIANGLES;
        // Non-indexed draws read vertices [0, vertices.count).
        vertexStream::VertexArrayInfo vertices{};
        std::span<const w8_ulong> indices;
        // Applied by the vertex shader to the supplied positions.
        glm::mat4 clip_transform{1};
        DrawShader shader;
        // A null stage samples the missing-texture pattern when texturing.
        std::array<Texture*, 2> textures{};
        glm::vec4 fog_color{0};
        w8_long polygon_offset = 0;
    };
    // A draw stage resolved to a device texture and its sampler settings.
    struct GpuStage {
        SDL_GPUTexture* texture = nullptr;
        GpuTexParms parameters;
    };

private:
    /* One interleaved vertex in the device's own stream. */
    struct GpuVertex {
        glm::vec4 position;
        glm::vec4 diffuse;
        glm::vec4 specular;
        glm::vec3 texcoord0;
        glm::vec3 texcoord1;
        float fog;
    };

    // SDL vertex attributes below consume this exact packed GPU layout.
    static_assert(sizeof(GpuVertex) == 76);
    static_assert(offsetof(GpuVertex, diffuse) == 16 && offsetof(GpuVertex, specular) == 32);
    static_assert(offsetof(GpuVertex, texcoord0) == 48 && offsetof(GpuVertex, texcoord1) == 60);
    static_assert(offsetof(GpuVertex, fog) == 72);

    struct GpuVertexUniforms {
        glm::mat4 projection;
    };

    struct GpuFragmentUniforms {
        glm::vec4 fog_color;
        glm::ivec4 mode;
        glm::vec4 alpha;
    };

    static_assert(sizeof(GpuFragmentUniforms) == 48);
    static_assert(offsetof(GpuFragmentUniforms, mode) == 16 &&
                  offsetof(GpuFragmentUniforms, alpha) == 32);

    enum GpuPrimitiveClass {
        PRIMITIVE_CLASS_TRIANGLES,
        PRIMITIVE_CLASS_LINES,
        PRIMITIVE_CLASS_POINTS
    };

    /* Fixed-function state that SDL GPU bakes into a pipeline. Mesh culling
       runs on the CPU, so device pipelines never cull. */
    struct GpuPipelineKey {
        static constexpr uint32_t STENCIL_REPLACE = 1u << 30;
        uint32_t value = 0;
        w8_long offset = 0;

        GpuPipelineKey() = default;
        GpuPipelineKey(const DrawShader& shader, GpuPrimitiveClass primitive, w8_long bias)
        {
            offset = bias;
            const uint32_t packed_shader =
                shader.value & (DrawShader::PASS_MASK | DrawShader::MASK_DEPTH_WRITE |
                                DrawShader::MASK_COLOR_WRITE | 0x7u << DrawShader::DSTBLEND_SHIFT |
                                0x3u << DrawShader::SRCBLEND_SHIFT);
            value = packed_shader | (uint32_t)primitive << 28;
        }

        bool operator<(const GpuPipelineKey& other) const
        {
            return value != other.value ? value < other.value : offset < other.offset;
        }
    };

    struct GpuDrawCommand {
        enum Kind { DRAW, CLEAR };
        Kind kind;
        /* DRAW */
        GpuPipelineKey pipeline_key;
        uint32_t first_index;
        uint32_t index_count;
        SDL_GPUTexture* textures[2];
        SDL_GPUSampler* samplers[2];
        GpuVertexUniforms vertex_uniforms;
        GpuFragmentUniforms fragment_uniforms;
        /* DRAW and CLEAR */
        SDL_Rect scissor;
        SDL_GPUViewport viewport;
        /* CLEAR */
        uint32_t clear_buffers;
        SDL_FColor clear_color;
        float clear_depth;
        uint8_t clear_stencil;
    };

    struct GpuPendingUpload {
        SDL_GPUTexture* texture;
        uint32_t level;
        uint32_t x, y, width, height;
        size_t offset;
        bool generate_mipmaps;
    };

public:
    bool gpuOpenWindow(w8_ulong target_width, w8_ulong target_height);

    void gpuCloseWindow();

    void gpuFlushFrame();

    void gpuFlipFrame();

    void gpuClearBuffers(std::uint32_t buffers);

    SDL_Surface* gpuLockBuffer();

    void gpuUnlockBuffer(bool modified = false);

    void gpuSetSwapInterval(w8_ulong interval);

    void gpuSetScissor(const GpuScissor& value);

    void gpuSetViewPort(const GpuViewPort& value);

    void gpuSetClearValues(const GpuClearValues& values);

    /* Expands every primitive to a list: SDL GPU has no fans, and lists let
       consecutive draws share one vertex stream. The call's textures arrive
       already resolved into `stages`. */
    void gpuDraw(const DrawCall& call, const std::array<GpuStage, 2>& stages);

    void gpuDeleteTexture(NativeTexture& texture);

    NativeTexture gpuCreateTexture(w8_ulong width, w8_ulong height, w8_ulong levels);

    void gpuUploadTexture(const NativeTexture& texture, w8_ulong level, GpuTextureRegion region,
                          std::span<const unsigned char> pixels, w8_ulong pitch);

    bool gpuCreateContext(SDL_Window* target_window);

    void gpuDeleteContext();

private:
    SDL_GPUShader* gpuCreateShader(SDL_GPUShaderStage stage, uint32_t sampler_count);

    NativeTexture gpuCreateDeviceTexture(uint32_t texture_width, uint32_t texture_height,
                                         uint32_t levels);

    void gpuAppendVertex(const vertexStream::VertexArrayInfo& arrays, uint32_t index);

    SDL_GPUSampler* gpuSampler(const GpuTexParms& parameters);

    SDL_GPUGraphicsPipeline* gpuPipeline(const GpuPipelineKey& key);

    SDL_GPUCommandBuffer* gpuCommandBuffer();

    void gpuUploadPending(SDL_GPUCommandBuffer* command);

    void gpuReplay(SDL_GPUCommandBuffer* command);

    SDL_GPURenderPass* gpuBeginPass(SDL_GPUCommandBuffer* command, const GpuDrawCommand* clear);

    void gpuReleaseDeleted();

    /* Records pending work; when presenting, blits the frame to the swapchain. */
    void gpuFlush(bool present);

    /* Waits for the frame so far and copies the color target into readback. */
    void gpuDownload();

    void gpuUpload();

    SDL_GPUDevice* gpu_device{};
    SDL_Window* gpu_window{};
    SDL_GPUTexture* gpu_color_target{};
    SDL_GPUTexture* gpu_depth_target{};
    SDL_GPUTextureFormat gpu_depth_format{};
    SDL_GPUShader* gpu_vertex_shader{};
    SDL_GPUShader* gpu_fragment_shader{};
    std::map<GpuTexParms, SDL_GPUSampler*> gpu_samplers;
    SDL_GPUBuffer* gpu_vertex_buffer{};
    uint32_t gpu_vertex_buffer_size{};
    SDL_GPUBuffer* gpu_index_buffer = nullptr;
    uint32_t gpu_index_buffer_size = 0;
    SDL_GPUCommandBuffer* gpu_command_buffer{};
    uint32_t gpu_width{};
    uint32_t gpu_height{};

    GpuClearValues gpu_clear_values{};
    SDL_Rect gpu_scissor{};
    SDL_GPUViewport gpu_viewport{};

    std::map<GpuPipelineKey, SDL_GPUGraphicsPipeline*> gpu_pipelines;
    std::unordered_set<SDL_GPUTexture*> gpu_textures;
    SDL_GPUTexture* gpu_white_texture = nullptr;
    std::vector<SDL_GPUTexture*> gpu_released;
    std::vector<GpuPendingUpload> gpu_uploads;
    std::vector<uint32_t> gpu_upload_pixels;
    std::vector<GpuVertex> gpu_vertices;
    std::vector<uint32_t> gpu_indices;
    std::unordered_map<uint32_t, uint32_t> gpu_vertex_remap;
    std::vector<GpuDrawCommand> gpu_commands;
    SDLSurfaceOwner gpu_readback{nullptr, SDL_DestroySurface};
    bool gpu_readback_dirty{};
    int gpu_locked{};
    bool gpu_trace{};

public:
    void drawOverlayQuad(const OverlayQuad& quad);
    glm::vec3 environment_offset{0};
    float scene_brightness = 1.0f;

public:
    struct Pick {
        /* Normalized pick point the caller fills: x and y are the cursor's
           viewport-space coordinates, z is the fixed 1.0 far value. */
        glm::vec3 position{0, 0, 1};
        MeshInstance* selected_model{};
        w8_ulong triangle_index{};
    };

    // Distance and amplitude of the frame depth cue.
    struct Environment {
        float minimum;
        float maximum;
        float near_scale;
        float far_scale;
    };

    class Renderer {
    public:
        struct TriInput {
            w8_ulong record_count;
            w8_ulong vertex_count;
            std::span<const w8_ulong> indices;
            std::span<const glm::ivec3> triangles;
            std::span<const w8_ulong> vertices;
            const MeshPass* passes;
            int direct_vertex_indices;
            const glm::mat4* project_clip_near;
            float sort_bias;
            // Depth bias recorded with the triangles' texture sets.
            w8_long polygon_offset = 0;
            w8_ulong triangleCount() const { return static_cast<w8_ulong>(indices.size()); }
        };

        struct Parameters {
            GpuRenderer* gerd;
            w8_long sorted;
            w8_long batch_limit;
            w8_ulong texture_stages;
        };

        /* The draw state of one triangle run. The interned record additionally carries a blend
           class derived from the shader's DSTBLEND field (ZERO -> 0, SRC_ALPHA pair -> 1,
           ONE -> 2, SRC_COLOR pair -> 3). */
        struct TextureSetKey {
            Texture* texture0;
            Texture* texture1;
            DrawShader shader;
            w8_long polygon_offset = 0;

            bool operator==(const TextureSetKey& other) const
            {
                return texture0 == other.texture0 && texture1 == other.texture1 &&
                       shader.value == other.shader.value && polygon_offset == other.polygon_offset;
            }
            bool operator!=(const TextureSetKey& other) const { return !(*this == other); }
        };
        struct TextureSetHash {
            size_t operator()(const TextureSetKey& key) const noexcept
            {
                return std::hash<Texture*>{}(key.texture0) ^
                       (std::hash<Texture*>{}(key.texture1) << 1) ^
                       (std::hash<w8_ulong>{}(key.shader.value) << 2) ^
                       (std::hash<w8_long>{}(key.polygon_offset) << 3);
            }
        };
        struct TextureSet {
            Texture* texture0;
            Texture* texture1;
            DrawShader shader;
            w8_ulong blend;
            w8_long polygon_offset;
        };
        /* GpuTexture-set interning cache; the map's value is the index into sets. */
        struct TextureSetCache {
            std::unordered_map<TextureSetKey, w8_ulong, TextureSetHash> map;
            std::vector<TextureSet> sets;

            void clear()
            {
                map.clear();
                sets.clear();
            }
            w8_ulong intern(const TextureSetKey& key);
        };
        /* Write pointers alloc() returns for the reserved triangle range. */
        struct IndexWrite {
            glm::ivec3* triangles;
            w8_ulong* texture_set;
            w8_ulong* sort_key;
            w8_ulong* aux;
        };
        /* Accumulated primitive work. alloc() reserves count entries plus 0x40 headroom across all
           four streams; reset() always clears the count and only frees when asked. */
        struct IndexBatch {
            std::vector<glm::ivec3> triangles;
            std::vector<w8_ulong> texture_set;
            std::vector<w8_ulong> sort_key;
            std::vector<w8_ulong> aux;
            w8_ulong count;

            IndexBatch() : count(0) {}
            IndexWrite alloc(w8_ulong count);
            void reset(int release);
        };
        /* Accumulated vertex streams. alloc() grows all streams and default-fills the new range
           (positions {0,0,0,1}, st {0,0}, q 1.0); bind() points an ShadedVertexArrays at the reserved
           range. */
        struct VertexArrays {
            std::vector<glm::vec4> diffuse;
            std::vector<glm::vec4> specular;
            std::vector<glm::vec4> positions;
            std::vector<glm::vec2> st[2];
            std::vector<float> q[2];
            std::vector<unsigned char> attributes;
            /* isBatchFull compares this signed against batch_limit. */
            w8_long count;

            VertexArrays() : count(0) {}
            ShadedVertexArrays alloc(w8_ulong count);
            ShadedVertexArrays bind(w8_ulong base);
            void reset(int release);
        };

        Renderer(const Parameters& parameters);
        ShadedVertexArrays allocVertexArray(w8_ulong count);
        /* intern the pass's {texture0,texture1,shader} key into
           texture_set, folding per-vertex texture/shader table transitions
           into the output ids. */
        void assignTextureSets(w8_ulong* texture_set, const w8_ulong* indices, w8_ulong count,
                               const MeshPass* pass, w8_long polygon_offset);
        /* expands/dedups the input triangles into the index and
           vertex batches, per record. */
        void expandTriangles(const TriInput& input, int sorted);
        /* transforms the reserved position range by the input's
           matrix in 0x80-vertex chunks (choosing ortho/perspective/generic by
           the matrix's zero pattern), derives per-vertex clip flags, and
           replicates the chunk across the remaining records. */
        void transformVertices(const TriInput& input, unsigned char* clip_flags);
        void render(const TriInput& input);
        /* the accumulated batch count passed the limit. Only
           immediate (non-sorted) renderers report full. */
        int isBatchFull() const;
        /* Submit the accumulated batch to SDL GPU. */
        void submit();
        /* the immediate (sorted == 0) and
           sorted draw paths over the accumulated index batch. */
        void drawImmediate(const vertexStream::VertexArrayInfo& arrays);
        void drawSorted(const vertexStream::VertexArrayInfo& arrays);
        void drawTriangleRuns(std::span<const w8_ulong> order,
                              const vertexStream::VertexArrayInfo& arrays);
        /* The GPU vertex streams of the bound batch: positions plus the
           per-stage texture coordinate and Q streams its attributes use. */
        vertexStream::VertexArrayInfo vertexArrays(const ShadedVertexArrays& arrays,
                                                   w8_ulong count) const;
        /* discard accumulated state; nonzero also releases
           the backing arrays. */
        void reset(int release_buffers);
        struct Statistics {
            w8_ulong triangle_chunks = 0;
            w8_ulong input_triangles = 0;
            w8_ulong clipped_triangles = 0;
            w8_ulong input_vertices = 0;
            w8_ulong device_calls = 0;
            w8_ulong device_vertices = 0;
            w8_ulong device_vertex_indices = 0;
        };
        void resetStatistics();
        Statistics getStatistics() const;

        /* CPU scratch streams owned by the renderer. */
        std::vector<unsigned char> bytes;
        std::vector<w8_ulong> dwords;
        /* render()'s per-corner dedup scratch (six slots per triangle). */
        std::vector<w8_ulong> remap;
        Statistics statistics;
        TextureSetCache texture_sets;
        IndexBatch indices;
        VertexArrays vertices;
        /* allocVertexArray() snapshots the vertex count here so render() can
           offset indices into the reserved range. */
        w8_long first_vertex;
        GpuRenderer* gerd;
        /* flushSort flushes entries where this is 1, flushImmediateRenderers
           where it is 0. */
        w8_long sorted;
        w8_long batch_limit;
        w8_ulong texture_stages;
    };

    enum e_error {
        ERROR_NONE = 0,
        ERROR_INVALID_ENUM = 1,
        ERROR_INVALID_VALUE = 2,
        ERROR_WINDOW_OPEN_FAILED = 3,
        ERROR_WINDOW_NOT_OPEN = 4,
        ERROR_BUFFER_LOCK_FAILED = 5,
        ERROR_INVALID_WHANDLE = 6,
        ERROR_CONTEXT_CREATION_FAILED = 7,
        ERROR_NO_CONTEXT = 8
    };
    enum e_closeHint {};
    /* toggle XORs 1<<option into enable_flags. The constructor sets both options. */
    enum e_enable { ENABLE_AUTO_FLIP = 4, ENABLE_CLEAR_ON_OPEN = 6 };

    GpuRenderer();
    ~GpuRenderer();

    void submitMesh(const Mesh::TriMesh& mesh, const MeshState& state, w8_long material_side = 0);

    // The open native renderer, borrowed only while its window remains open.
    static GpuRenderer* getActive();
    e_error createContext(SDL_Window* window);
    void deleteContext();
    /* openWindowInternal parameter block: the current client size and the
       requested backbuffer size. */
    struct OpenInfo {
        w8_long window_width;
        w8_long window_height;
        w8_long width;
        w8_long height;
    };
    /* Number of back buffers in the swap chain (1, 2 or 3). */
    e_error openWindow();
    e_error openWindow(w8_long width, w8_long height);
    void closeWindow(e_closeHint hint);
    int isWindowOpen() const;
    e_error beginFrame();
    void endFrame();
    void flush();
    void flushRenderers();
    void flushImmediateRenderers();
    void clear(std::uint32_t buffers);
    w8_long getHeight() const;
    w8_long getWidth() const;
    void resetStatistics();
    struct Statistics {
        /* Epoch written by resetStatistics; getStatistics returns the
           seconds elapsed since then. */
        double elapsed;
        /* Device texture bytes transferred. */
        double texture_transfer;
        double pixels_drawn;
        w8_ulong value_18;
        w8_ulong value_1c;
        w8_ulong device_triangles;
        w8_ulong device_vertices;
        w8_ulong device_vertex_indices;
        /* Frames presented: flipFrame increments once per call. */
        w8_ulong frames;
        w8_ulong triangle_chunks;
        w8_ulong input_triangles;
        /* getStatistics accumulates this only for sorted renderers. */
        w8_ulong sorted_triangles;
        w8_ulong input_vertices;
        /* Texture changes between consecutive replayed draws; the debug
           overlay prints it as "TC". */
        w8_ulong texture_binds;
        /* createNewTexture increments this created-texture count. */
        w8_ulong textures_created;
        /* draw increments this draw-call count. */
        w8_ulong draw_calls;
        w8_ulong clipped_triangles;
        w8_ulong device_calls;
    };
    Statistics getStatistics();
    w8_ulong getResidentTextureMemUsed() const;
    void setClearColor(const glm::vec4& color);
    void setClearColor(float red, float green, float blue, float alpha);
    void setClearDepth(double depth);
    void setAmbientLight(float red, float green, float blue, float alpha);
    void setAmbientLight(const glm::vec4& light);
    void setFogColor(const glm::vec4& color);
    void setScissor(w8_ulong x, w8_ulong y, w8_ulong width, w8_ulong height);
    /* Dirty-rectangle pair handed to flipFrame in {x,y,width,height} form;
       GERD converts each to GpuScissor left/top/right/bottom. */
    struct Rectangle {
        w8_long x, y, width, height;
    };
    void flipFrame();
    void flipFrame(const Rectangle* first, const Rectangle* second, w8_ulong count);
    void setTextureReduction(w8_long reduction);
    void setViewPort(w8_ulong x, w8_ulong y, w8_ulong width, w8_ulong height);
    // Rendering and resource updates run on the main thread.
    Renderer& batchRenderer(bool sorted);
    SDL_Surface* lockBuffer();
    void unlockBuffer(bool modified = false);
    glm::vec4 getAmbientLight() const;
    glm::vec4 getFogColor() const;
    std::pair<float, float> getEnvironmentRange() const;
    std::pair<float, float> getEnvironmentScaleFactor() const;
    w8_ulong getExclusionMask() const;
    void setExclusionMask(w8_ulong mask);
    // RECOMP: ?getMaxTextureStages@GpuRenderer@@QBEJXZ
    w8_long getMaxTextureStages() const { return max_texture_stages; }
    void setPickKey(w8_ulong_ptr key);
    w8_ulong_ptr getPickKey() const;
    void setEnvironmentRange(float minimum, float maximum);
    void setEnvironmentScaleFactor(float scale, float inverse_scale);
    void setTextureDefaultMagFilter(Texture::e_filter filter);
    void setTextureDefaultMinFilter(Texture::e_filter filter);
    void setTextureDefaultMipmap(Texture::e_mipmap mipmap);
    void setTextureSubImage(Texture* texture, w8_long mipmap, w8_long x, w8_long y, w8_long width,
                            w8_long height);
    // Resolves the texture and makes its device copy resident before any draw needs it.
    void prepareTexture(Texture* texture);
    void draw(const DrawCall& call);
    std::optional<Pick> popPick();
    void pushPick(const Pick& pick);
    void toggle(e_enable option);
    void invalidateResidentTextures();
    void invalidateTextureCache();
    void invalidateTexture(Texture* texture);
    void setSwapInterval(w8_ulong interval);

    /* Releases every GERD on the global list. */
    e_error getError();
    void setClearStencil(w8_ulong stencil);

    // RECOMP: ?isPickStackEmpty@GpuRenderer@@QBEHXZ
    int isPickStackEmpty() const { return pick.pick_depth == 0; }

    // RECOMP: ?isEnabled@GpuRenderer@@QBEHW4e_enable@1@@Z
    int isEnabled(e_enable option) const { return (enable_flags & (1UL << option)) != 0; }

private:
    struct TextureCacheData {
        NativeTexture gpu;
        w8_ulong width = 0, height = 0, first_level = 0, last_level = 0;
    };
    struct GpuTexture {
        explicit GpuTexture(GpuRenderer& owner) : owner(owner) {}
        ~GpuTexture() { owner.gpuDeleteTexture(device.gpu); }
        GpuTexture(const GpuTexture&) = delete;
        GpuTexture& operator=(const GpuTexture&) = delete;
        GpuRenderer& owner;
        SDL_PixelFormat pixel_format = SDL_PIXELFORMAT_BGRA32;
        TextureCacheData device;
    };

    /* Pick batch view: indices selects triangles out of the caller's stream, vertices remaps each
       corner to a position index, positions is the renderer's vec4 stream base. */
    struct PickInput {
        std::span<const w8_ulong> indices;
        std::span<const glm::ivec3> triangles;
        // Empty means direct corner indices; otherwise this is the full vertex map.
        std::span<const w8_ulong> vertices;
        std::span<const glm::vec4> positions;
    };

    GpuRenderer(const GpuRenderer&) = delete;
    GpuRenderer& operator=(const GpuRenderer&) = delete;

    friend class Renderer;
    /* For each queued Pick, w-normalize the batch's positions into pick_vertices and run the
       edge-function triangle test against the pick ray. */
    void performPickTest(const PickInput& input);
    void deleteRenderers();

    void setError(e_error error);
    void assertContext() const;
    void applyViewStateChanges();
    void checkViewStateChanges();
    void checkFrameStateChanges();
    void applyFrameStateChanges();
    void checkAllStateChanges();
    void recalcScissor();
    // The resident device copy of a resolved texture and the sampler its parameters select.
    GpuStage residentStage(Texture* texture);
    GpuTexture* createNewTexture(Texture* texture);
    GpuTexParms samplerParameters(const Texture::Parameters& parameters) const;
    void evaluateTextureDimensions(TextureCacheData& device, const Texture::Dimensions& dimensions);
    void initTexCache();
    void closeTexCache();
    void initClearColors();
    void initView();
    void initLights();
    e_error openWindowInternal(const OpenInfo& info);
    enum class RendererDrain { immediate, sorted, reset, statistics };
    void drainRenderers(RendererDrain action);
    void flushSort();

    static GpuRenderer* active;

    struct Device {
        SDL_Window* window;
        /* Current window and backbuffer dimensions. */
        OpenInfo open_info;
    };

    struct State {
        /* Depth range forwarded into GpuViewPort by applyViewStateChanges;
           initView resets it to [0.0, 1.0] and setDepthRange clamps it. */
        double depth_min;
        double depth_max;
        GpuScissor scissor;
        w8_ulong view_left;
        w8_ulong view_top;
        w8_ulong view_right;
        w8_ulong view_bottom;
        /* Bit 1: recalcScissor marks the scissor as the full view. */
        w8_ulong scissor_flags;
    };

    struct Display {
        w8_ulong swap_interval;
    };

    struct PickState {
        PickState() : pick_depth(0) {}

        Pick pick_stack[32];
        w8_ulong pick_depth;
        w8_ulong_ptr pick_key;
    };

    struct ClearState {
        struct {
            glm::vec4 color;
            double depth;
            w8_ulong stencil;
        } clear_values;
        unsigned char unknown_2c_[4];
    };

    struct TextureState {
        Texture::e_filter default_mag_filter = Texture::FILTER_GOOD;
        Texture::e_filter default_min_filter = Texture::FILTER_GOOD;
        Texture::e_mipmap default_mipmap = Texture::MIPMAP_FASTEST;
    };

    Device device;
    friend class MeshInstance;
    std::vector<glm::vec3> highlight_vertices;

    unsigned char unknown_0c_[4];
    // Immediate and depth-sorted batches.
    std::array<std::unique_ptr<Renderer>, 2> renderers;
    std::uint32_t enable_flags = 0;
    enum {
        DIRTY_SWAP_INTERVAL = 0x4UL,
        DIRTY_SCISSOR = 0x10UL,
        DIRTY_VIEWPORT = 0x80UL,
        DIRTY_DEPTH_RANGE = 0x100UL,
        DIRTY_FRAME_STATE = DIRTY_SWAP_INTERVAL,
        DIRTY_VIEW_STATE = DIRTY_SCISSOR | DIRTY_VIEWPORT | DIRTY_DEPTH_RANGE
    };
    w8_ulong dirty;
    enum {
        STATE_CONTEXT_CREATED = 0x01u,
        STATE_WINDOW_OPEN = 0x02u,
        STATE_FRAME_STARTED = 0x04u,
        STATE_FRAME_FLIPPED = 0x08u,
        STATE_CLOSING_WINDOW = 0x10u
    };
    w8_ulong state_flags;
    e_error last_error;
    State state;
    Display display;
    PickState pick;
    unsigned char unknown_19f4_[4];
    Statistics statistics;

    ClearState clear_state;
    TextureState texture_state;
    glm::vec4 fog_color;
    /* GPU texture records keyed by the source resource's identity. */
    Texture fallback_texture;
    std::unordered_map<Texture*, GpuTexture> textures;
    GpuTexture* texture_default;
    w8_long texture_reduction;
    unsigned char unknown_2045_[3];
    glm::vec4 ambient_light;
    Environment environment;

public:
    std::span<Light* const> lights;
    std::span<FogParams* const> fogs;
    double frame_time = 0;
    w8_ulong exclusion_mask;
    /* performPickTest's w-normalized {x,y,z,sign(w)} scratch per vertex;
       cleared by closeWindow. */
    std::vector<glm::vec4> pick_vertices;
};

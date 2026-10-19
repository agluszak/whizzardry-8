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
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_draw.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/vertex_shading.h"
#include "wiz8/render/vertex_stream.h"
#include "wiz8/spatial/bounding_sphere.h"
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
    enum GpuCullMode { GPU_CULL_NONE = 0, GPU_CULL_BACK = 1, GPU_CULL_FRONT = 2 };

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

    /* Fixed-function state that SDL GPU bakes into a pipeline. */
    struct GpuPipelineKey {
        static constexpr uint32_t STENCIL_REPLACE = 1u << 30;
        uint32_t value = 0;
        w8_long offset = 0;

        GpuPipelineKey() = default;
        GpuPipelineKey(const DrawShader& shader, GpuCullMode cull, GpuPrimitiveClass primitive,
                       w8_long bias)
        {
            offset = bias;
            const uint32_t packed_shader =
                shader.value & (DrawShader::PASS_MASK | DrawShader::MASK_DEPTH_WRITE |
                                DrawShader::MASK_COLOR_WRITE | 0x7u << DrawShader::DSTBLEND_SHIFT |
                                0x3u << DrawShader::SRCBLEND_SHIFT);
            value = packed_shader | (uint32_t)cull << 24 | (uint32_t)primitive << 28;
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

    void gpuSetFogColor(const glm::vec4& color);

    void gpuSetShader(const DrawShader& value);

    void gpuSetTextureParameters(w8_ulong stage, const GpuTexParms& parms);

    void gpuBindTexture(w8_ulong stage, const NativeTexture& texture);

    void gpuDeleteTexture(NativeTexture& texture);

    NativeTexture gpuCreateTexture(w8_ulong width, w8_ulong height, w8_ulong levels);

    void gpuUploadTexture(const NativeTexture& texture, w8_ulong level, GpuTextureRegion region,
                          std::span<const unsigned char> pixels, w8_ulong pitch);

    bool gpuCreateContext(SDL_Window* target_window);

    void gpuDeleteContext();

    void gpuSetCullMode(GpuCullMode mode);

    void gpuSetProjectionMatrix(const glm::mat4& matrix);

    void gpuSetVertexArrayInfo(const vertexStream::VertexArrayInfo* info);

    void gpuDrawElements(vertexStream::e_primitive primitive, std::span<const w8_ulong> indices);

    void gpuDrawArrays(vertexStream::e_primitive primitive, w8_long first, w8_ulong count);

    void gpuSetPolygonOffset(w8_long offset);

private:
    SDL_GPUShader* gpuCreateShader(SDL_GPUShaderStage stage, uint32_t sampler_count);

    NativeTexture gpuCreateDeviceTexture(uint32_t texture_width, uint32_t texture_height,
                                         uint32_t levels);

    template<class Vector>
    Vector gpuFetch(vertexStream::e_vertexArray slot, uint32_t index, Vector fallback) const;

    void gpuAppendVertex(uint32_t index);

    /* Expands every primitive to a list: SDL GPU has no fans, and lists let
       consecutive draws share one vertex stream. */
    void gpuDraw(vertexStream::e_primitive primitive, uint32_t count, const w8_ulong* indices,
                 uint32_t first);

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

    DrawShader gpu_shader;
    GpuCullMode gpu_cull_mode{};
    w8_long gpu_polygon_offset{};
    GpuVertexUniforms gpu_projection{glm::mat4(1)};
    GpuClearValues gpu_clear_values{};
    glm::vec4 gpu_fog_color{0};
    SDL_Rect gpu_scissor{};
    SDL_GPUViewport gpu_viewport{};
    vertexStream::VertexArrayInfo gpu_vertex_arrays{};
    SDL_GPUTexture* gpu_bound[2]{};
    GpuTexParms gpu_parameters[2]{};

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

    struct ClipPlanes {
        glm::vec4 planes[32];
        w8_ulong mask;
    };

    // Distance and amplitude of the frame depth cue.
    struct Environment {
        float minimum;
        float maximum;
        float near_scale;
        float far_scale;
    };

    /* ortho/frustum take this packed six-double box. */
    struct Frustum {
        double left;
        double right;
        double bottom;
        double top;
        double near_plane;
        double far_plane;
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
            w8_ulong triangleCount() const { return static_cast<w8_ulong>(indices.size()); }
        };

        struct Parameters {
            GpuRenderer* gerd;
            w8_long sorted;
            w8_long batch_limit;
            w8_ulong texture_stages;
        };

        /* The interned record additionally carries a blend class derived from the shader's DSTBLEND
           field (ZERO -> 0, SRC_ALPHA pair -> 1, ONE -> 2, SRC_COLOR pair -> 3). */
        struct TextureSetKey {
            Texture* texture0;
            Texture* texture1;
            DrawShader shader;

            bool operator==(const TextureSetKey& other) const
            {
                return texture0 == other.texture0 && texture1 == other.texture1 &&
                       shader.value == other.shader.value;
            }
            bool operator!=(const TextureSetKey& other) const { return !(*this == other); }
        };
        struct TextureSetHash {
            size_t operator()(const TextureSetKey& key) const noexcept
            {
                return std::hash<Texture*>{}(key.texture0) ^
                       (std::hash<Texture*>{}(key.texture1) << 1) ^
                       (std::hash<w8_ulong>{}(key.shader.value) << 2);
            }
        };
        struct TextureSet {
            Texture* texture0;
            Texture* texture1;
            DrawShader shader;
            w8_ulong blend;
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
                               const MeshPass* pass);
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
        void drawImmediate();
        void drawSorted();
        /* point the draw state at texture set `index`,
           updating each of texture0/texture1/shader only on change. */
        void bindTextureSet(w8_ulong index, bool force = false);
        void drawTriangleRuns(std::span<const w8_ulong> order);
        /* Borrow the per-stage texture coordinate and Q streams and program
           the GPU vertex arrays for the bound batch. */
        void programVertexArrays(ShadedVertexArrays* arrays, w8_ulong count);
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
        /* Bound draw state, refreshed per texture set. */
        Texture* texture0;
        Texture* texture1;
        DrawShader shader;
        w8_ulong clip_state;
        GpuRenderer* gerd;
        /* batchRenderer matches this against the sorted-mode enable bit;
           flushSort flushes entries where it is 1, flushImmediateRenderers
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
    /* applyRenderState translates each value to the same SDL GPU mode. */
    /* 0 selects the model-view stack and 1 the projection stack. */
    enum e_matrixMode { MATRIX_MODELVIEW = 0, MATRIX_PROJECTION = 1 };
    enum e_clipMode { CLIPMODE_POSITIONAL_0 = 0 };
    /* 0 culls back faces and 1 front faces (each flipped by the winding); 2 disables culling. */
    enum e_cullMode { CULL_BACK = 0, CULL_FRONT = 1, CULL_NONE = 2 };
    /* toggle XORs 1<<option into enable_flags. Option 1 selects sorted rendering.
       The constructor sets options 4 and 6. */
    enum e_enable {
        ENABLE_SORTED_RENDERING = 1,
        ENABLE_REVERSE_NORMALS = 3,
        ENABLE_AUTO_FLIP = 4,
        ENABLE_CLEAR_ON_OPEN = 6
    };
    enum e_winding { WINDING_POSITIONAL_0 = 0, WINDING_POSITIONAL_1 = 1 };
    enum e_visibility { VISIBILITY_OUTSIDE = 0 };
    GpuRenderer();
    ~GpuRenderer();

    void submitMesh(const Mesh::TriMesh& mesh, w8_long material_side = 0);

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
        /* applyViewStateChanges increments this counter on every apply. */
        w8_ulong view_state_applies;
        /* applyDrawStateChanges increments this counter on every apply. */
        w8_ulong draw_state_applies;
        /* applyFrameStateChanges increments this counter on every apply. */
        w8_ulong frame_state_count;
        /* GpuTexture binds counted by changeTexture after the stage's bound
           texture actually changes; the debug overlay prints it as "TC". */
        w8_ulong texture_binds;
        /* GpuTexture-parameter updates counted by setTextureParameters. */
        w8_ulong texture_parameter_sets;
        /* createNewTexture increments this created-texture count. */
        w8_ulong textures_created;
        /* setShader calls counted by applyDrawStateChanges. */
        w8_ulong shader_sets;
        /* drawArrays/drawElements increment this draw-call count. */
        w8_ulong draw_calls;
        w8_ulong clipped_triangles;
        w8_ulong device_calls;
        /* testBoundingSphere call count / visible-result count. */
        w8_ulong sphere_tests;
        w8_ulong sphere_visible;
        /* testBoundingBox call count / visible-result count. */
        w8_ulong box_tests;
        w8_ulong box_visible;
        /* classifyMatrix call count. */
        w8_ulong matrix_classifications;
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
    void matrixMode(e_matrixMode mode);
    e_matrixMode getMatrixMode() const;
    glm::mat4 getMatrix();
    glm::mat4 getMatrix(e_matrixMode mode);
    BoundingSphere eyeSpaceBounds(const BoundingSphere& sphere);
    glm::vec4 getEyeSpaceLocation(const glm::vec3& object_location);
    glm::mat4 getInverseModelViewMatrix();
    ClipPlanes getClipPlanes();
    bool pushClipPlane(const glm::vec4& plane, e_clipMode mode);
    void popClipPlane();
    glm::mat4 getProjectClipNearMatrix();
    glm::mat4 getNormalMatrix();

    float getMaxModelViewScale();
    e_cullMode getCullMode() const;
    e_winding getWinding() const;
    void setWinding(e_winding winding);
    // Rendering and resource updates run on the main thread.
    Renderer& batchRenderer();
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
    void loadIdentity();
    void multMatrix(const glm::mat4& matrix);
    void multMatrix(const glm::dmat4& matrix);
    void rotate(double angle, const glm::vec3& axis);
    void rotate(double angle, double x, double y, double z);
    void scale(double x, double y, double z);
    void scale(const glm::vec3& factors);
    void scale(double factor);
    void translate(const glm::vec3& offset);
    void translate(double x, double y, double z);
    e_visibility testBoundingSphere(const glm::vec3& center, float radius);
    e_visibility testBoundingBox(const glm::vec3& minimum, const glm::vec3& maximum);
    void setPickKey(w8_ulong_ptr key);
    w8_ulong_ptr getPickKey() const;
    void ortho(double left, double right, double bottom, double top, double near_plane,
               double far_plane);
    void ortho(const Frustum& frustum);
    void frustum(double left, double right, double bottom, double top, double near_plane,
                 double far_plane);
    void frustum(const Frustum& frustum);
    void loadMatrix(const glm::dmat4& matrix);
    void loadMatrix(const glm::mat4& matrix);
    void loadMatrix(const glm::dmat3& matrix);
    void loadMatrix(const glm::mat3& matrix);
    void setEnvironmentRange(float minimum, float maximum);
    void setEnvironmentScaleFactor(float scale, float inverse_scale);
    void setClipState(std::uint32_t state);
    void setTexture(Texture* texture, w8_ulong layer);
    void setTextureDefaultMagFilter(Texture::e_filter filter);
    void setTextureDefaultMinFilter(Texture::e_filter filter);
    void setTextureDefaultMipmap(Texture::e_mipmap mipmap);
    void setTextureSubImage(Texture* texture, w8_long mipmap, w8_long x, w8_long y, w8_long width,
                            w8_long height);
    void drawArrays(vertexStream::e_primitive primitive, w8_long first, w8_ulong count);
    void drawElements(vertexStream::e_primitive primitive, w8_ulong count,
                      vertexStream::e_indexType type, const void* indices);
    std::optional<Pick> popPick();
    void pushPick(const Pick& pick);
    void toggle(e_enable option);
    std::uint32_t drawOptions() const { return enable_flags; }
    void setDrawOptions(std::uint32_t options) { enable_flags = options; }
    void invalidateResidentTextures();
    void invalidateTextureCache();
    void invalidateTexture(Texture* texture);
    /* Clears every open renderer's binding of a texture being destroyed. */
    static void unbindTexture(const Texture* texture);
    void setSwapInterval(w8_ulong interval);
    w8_long getPolygonOffset() const;
    void setPolygonOffset(w8_long offset);

    /* Releases every GERD on the global list. */
    e_error getError();
    void setClearStencil(w8_ulong stencil);

    // RECOMP: ?isPickStackEmpty@GpuRenderer@@QBEHXZ
    int isPickStackEmpty() const { return pick.pick_depth == 0; }

    // RECOMP: ?isEnabled@GpuRenderer@@QBEHW4e_enable@1@@Z
    int isEnabled(e_enable option) const { return (enable_flags & (1UL << option)) != 0; }

    // RECOMP: ?setCullMode@GpuRenderer@@QAEXW4e_cullMode@1@@Z
    void setCullMode(e_cullMode mode)
    {
        if (state.cull_mode != mode) {
            state.cull_mode = mode;
            dirty |= DIRTY_CULLING;
        }
    }

    // RECOMP: ?setShader@GpuRenderer@@QAEXABVsrShader@@@Z
    void setShader(const DrawShader& shader)
    {
        if (this->shader.value != shader.value) {
            this->shader = shader;
            dirty |= DIRTY_SHADER;
        }
    }

    void setVertexArrayMask(std::uint32_t mask)
    {
        vertex_arrays.mask = mask;
        vertex_arrays_dirty |= DIRTY_VERTEX_ARRAY_INFO;
    }

    std::uint32_t getVertexArrayMask() const { return vertex_arrays.mask; }

    // RECOMP: ?setTexCoordPointer@GpuRenderer@@QAEXJW4e_type@vertexStream@@KPBXK@Z
    void setTexCoordPointer(w8_long components, vertexStream::e_type type, w8_ulong stride,
                            const void* values, w8_ulong layer)
    {
        w8_ulong index = layer + 4;
        vertex_arrays.components[index] = components;
        vertex_arrays.types[index] = type;
        vertex_arrays.strides[index] = stride;
        vertex_arrays.arrays[index] = values;
        vertex_arrays_dirty |= DIRTY_VERTEX_ARRAY_INFO;
    }

    // RECOMP: ?setVertexPointer@GpuRenderer@@QAEXJW4e_type@vertexStream@@KPBXJ@Z
    void setVertexPointer(w8_long primitive, vertexStream::e_type type, w8_ulong stride,
                          const void* values, w8_long count)
    {
        vertex_arrays.count = count < 0 ? 0 : count;
        vertex_arrays.components[0] = primitive;
        vertex_arrays.types[0] = type;
        vertex_arrays.strides[0] = stride;
        vertex_arrays.arrays[0] = values;
        vertex_arrays_dirty |= DIRTY_VERTEX_ARRAY_INFO;
    }

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

    void setDataPtr(vertexStream::e_vertexArray index, w8_long components,
                    vertexStream::e_type type, w8_ulong stride, const void* values);

    friend class Renderer;
    /* For each queued Pick, w-normalize the batch's positions into pick_vertices and run the
       edge-function triangle test against the pick ray. */
    void performPickTest(const PickInput& input);
    void deleteRenderers();

    void setError(e_error error);
    void resetTexture();
    void assertContext() const;
    void setMatrixDirty();
    void classifyMatrix(e_matrixMode mode);
    void applyViewStateChanges();
    void applyClipPlaneChanges();
    void applyDrawStateChanges();
    void applyFrameStateChanges();
    void checkViewStateChanges();
    void checkClipPlaneChanges();
    void checkDrawStateChanges();
    void prepareDrawState();
    void checkFrameStateChanges();
    void checkAllStateChanges();
    void recalcScissor();
    void changeTexture(Texture* texture, w8_ulong stage, int apply_parms);
    GpuTexture* createNewTexture(Texture* texture);
    void setTextureParameters(w8_ulong stage, const Texture::Parameters& parameters);
    void evaluateTextureDimensions(TextureCacheData& device, const Texture::Dimensions& dimensions);
    void resetCurrentTexPointers();
    void initTexCache();
    void closeTexCache();
    void initClearColors();
    void initView();
    void initLights();
    void initMatrices();
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
        // Current model-view and projection values.
        glm::mat4 matrix_current[2];
        /* Entries [0..5] are the eye-space frustum planes and [6..31] the user planes pushed by
           pushClipPlane (mask bits 6..31 of clip_mask). */
        glm::vec4 clip_planes[32];
        /* Depth range forwarded into GpuViewPort by applyViewStateChanges;
           initView resets it to [0.0, 1.0] and setDepthRange clamps it. */
        double depth_min;
        double depth_max;
        GpuScissor scissor;
        w8_ulong view_left;
        w8_ulong view_top;
        w8_ulong view_right;
        w8_ulong view_bottom;
        e_cullMode cull_mode;
        e_winding winding;
        e_matrixMode matrix_mode;
        unsigned char unknown_12c4_[6];
        /* Plane-enable mask: bits 0..5 frustum, bits 6..31 user planes. */
        w8_ulong clip_mask;
        /* Subset of clip_mask carrying mode-1 user planes. */
        w8_ulong clip_mode1_mask;
        w8_long clip_plane_count;
        /* Bit 1: recalcScissor marks the scissor as the full view. */
        w8_ulong scissor_flags;
        glm::mat4 inverse_modelview;
        glm::mat4 project_clip_near;
        glm::mat4 normal_matrix;
        float max_modelview_scale;
        unsigned char unknown_13c4_[4];
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
    std::array<std::unique_ptr<Renderer>, 2> renderers;
    std::uint32_t enable_flags = 0;
    enum {
        DIRTY_SWAP_INTERVAL = 0x4UL,
        DIRTY_SCISSOR = 0x10UL,
        DIRTY_MATRIX_SHIFT = 5,
        DIRTY_MODELVIEW = 0x20UL,
        DIRTY_PROJECTION = 0x40UL,
        DIRTY_VIEWPORT = 0x80UL,
        DIRTY_DEPTH_RANGE = 0x100UL,
        DIRTY_FOG_COLOR = 0x200UL,
        DIRTY_TEXTURE_SHIFT = 10,
        DIRTY_TEXTURE0 = 0x400UL,
        DIRTY_TEXTURE1 = 0x800UL,
        DIRTY_SHADER = 0x1000UL,
        DIRTY_CULLING = 0x2000UL,
        DIRTY_POLYGON_OFFSET = 0x8000UL,
        DIRTY_CLIP_PLANES = 0x10000UL,
        DIRTY_FRAME_STATE = DIRTY_SWAP_INTERVAL,
        DIRTY_VIEW_STATE = DIRTY_SCISSOR | DIRTY_MODELVIEW | DIRTY_PROJECTION | DIRTY_VIEWPORT |
            DIRTY_DEPTH_RANGE,
        DIRTY_DRAW_STATE = DIRTY_FOG_COLOR | DIRTY_TEXTURE0 | DIRTY_TEXTURE1 | DIRTY_SHADER |
            DIRTY_CULLING | DIRTY_POLYGON_OFFSET
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
    /* Bound GpuTexture per stage, swapped by changeTexture. */
    GpuTexture* texture_slots[2];
    /* Per-stage SDL sampler parameters written by setTextureParameters. */
    GpuTexParms texture_parms[2];
    TextureState texture_state;
    w8_long polygon_offset;
    glm::vec4 fog_color;
    DrawShader shader;
    /* Borrowed texture resources requested through setTexture for stages 0/1;
       a texture clears its bindings when destroyed. */
    Texture* texture_iface[2];
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
    enum { DIRTY_VERTEX_ARRAY_INFO = 0x01u };
    w8_ulong vertex_arrays_dirty;
    vertexStream::VertexArrayInfo vertex_arrays;
    /* performPickTest's w-normalized {x,y,z,sign(w)} scratch per vertex;
       cleared by closeWindow. */
    std::vector<glm::vec4> pick_vertices;
};

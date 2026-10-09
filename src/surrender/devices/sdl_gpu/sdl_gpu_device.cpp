#include "surrender/srDD_SDLGPU.h"

#include <SDL3/SDL.h>
#include <stdint.h>
#include <string.h>

#include <map>
#include <vector>

#include "sr_fragment.spv.h"
#include "sr_vertex.spv.h"

/* SurRender drives a device the way it drove OpenGL 1.x: state setters,
   client-side vertex arrays and immediate draws, interleaved with clears and
   framebuffer reads. SDL GPU records copy and render passes instead, so this
   device queues each draw with a snapshot of its state and replays the queue
   when a frame is flushed, flipped or read back. */
namespace {

/* One interleaved vertex in the device's own stream. */
struct Vertex {
    float position[4];
    float diffuse[4];
    float specular[4];
    float texcoord0[3];
    float texcoord1[3];
    float fog;
};

struct VertexUniforms {
    float projection[16];
};

struct FragmentUniforms {
    float fog_color[4];
    int32_t mode[4];
    float alpha[4];
};

enum PrimitiveClass { PRIMITIVE_CLASS_TRIANGLES, PRIMITIVE_CLASS_LINES, PRIMITIVE_CLASS_POINTS };

/* Fixed-function state that SDL GPU bakes into a pipeline. */
struct PipelineKey {
    uint32_t value;

    PipelineKey(const srShader& shader, srDD::e_cullMode cull, srDD::e_polygonMode polygon,
                PrimitiveClass primitive)
    {
        const uint32_t packed_shader =
            shader.value & (srShader::PASS_MASK | srShader::MASK_DEPTH_WRITE |
                            srShader::MASK_COLOR_WRITE | 0x7u << srShader::DSTBLEND_SHIFT |
                            0x3u << srShader::SRCBLEND_SHIFT);
        value = packed_shader | (uint32_t)cull << 24 | (uint32_t)polygon << 26 |
                (uint32_t)primitive << 28;
    }

    bool operator<(const PipelineKey& other) const
    {
        return value < other.value;
    }
};

struct DrawCommand {
    enum Kind { DRAW, CLEAR };
    Kind kind;
    /* DRAW */
    uint32_t pipeline_key;
    uint32_t first_vertex;
    uint32_t vertex_count;
    uint32_t textures[2];
    uint32_t samplers[2];
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

struct DeviceTexture {
    SDL_GPUTexture* texture;
    uint32_t width;
    uint32_t height;
    uint32_t levels;
};

struct PendingUpload {
    uint32_t texture;
    uint32_t level;
    uint32_t x, y, width, height;
    std::vector<uint32_t> pixels;
};

/* The device exposes one 32-bit ARGB format for textures and the back buffer.
   srGERD converts every texture to it, and the bytes match B8G8R8A8. */
srDD::PixelFormat argb8888Format()
{
    srDD::PixelFormat format;
    format.red_bits = 8;
    format.red_shift = 16;
    format.green_bits = 8;
    format.green_shift = 8;
    format.blue_bits = 8;
    format.blue_shift = 0;
    format.alpha_bits = 8;
    format.alpha_shift = 24;
    format.color_model = srPixelConvert::COLOR_RGB;
    format.pixel_size = srPixelConvert::PIXEL_SIZE_32;
    return format;
}

SDL_GPUCompareOp compareOp(uint32_t pass)
{
    static const SDL_GPUCompareOp ops[8] = {
        SDL_GPU_COMPAREOP_NEVER,   SDL_GPU_COMPAREOP_LESS,      SDL_GPU_COMPAREOP_EQUAL,
        SDL_GPU_COMPAREOP_LESS_OR_EQUAL, SDL_GPU_COMPAREOP_GREATER, SDL_GPU_COMPAREOP_NOT_EQUAL,
        SDL_GPU_COMPAREOP_GREATER_OR_EQUAL, SDL_GPU_COMPAREOP_ALWAYS,
    };
    return ops[pass & 7];
}

SDL_GPUBlendFactor sourceFactor(uint32_t blend)
{
    static const SDL_GPUBlendFactor factors[4] = {
        SDL_GPU_BLENDFACTOR_ZERO, SDL_GPU_BLENDFACTOR_ONE, SDL_GPU_BLENDFACTOR_SRC_ALPHA,
        SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
    };
    return factors[blend & 3];
}

SDL_GPUBlendFactor destinationFactor(uint32_t blend)
{
    static const SDL_GPUBlendFactor factors[6] = {
        SDL_GPU_BLENDFACTOR_ZERO,      SDL_GPU_BLENDFACTOR_ONE,
        SDL_GPU_BLENDFACTOR_SRC_COLOR, SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR,
        SDL_GPU_BLENDFACTOR_SRC_ALPHA, SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
    };
    return blend < 6 ? factors[blend] : SDL_GPU_BLENDFACTOR_ZERO;
}

class SDLGPUDevice : public srDD {
public:
    SDLGPUDevice()
        : device(0), window(0), color_target(0), depth_target(0), depth_format(),
          swapchain_format(), vertex_shader(0), fragment_shader(0), vertex_buffer(0),
          vertex_buffer_size(0), command_buffer(0), width(0), height(0), cull_mode(CULL_NONE),
          polygon_mode(POLYGON_FILL), polygon_offset(0), vertex_arrays(), readback_dirty(0),
          locked(0), trace(SDL_getenv("WIZ8_SRDD_TRACE") != 0)
        {
            memset(&projection, 0, sizeof(projection));
            projection.projection[0] = projection.projection[5] = projection.projection[10] =
                projection.projection[15] = 1.0f;
            memset(&clear_values, 0, sizeof(clear_values));
            memset(fog_color, 0, sizeof(fog_color));
            memset(&scissor, 0, sizeof(scissor));
            memset(&viewport, 0, sizeof(viewport));
            bound[0] = bound[1] = 0;
            parameters[0] = parameters[1] = 0;
            samplers[0] = samplers[1] = 0;
        }

    ~SDLGPUDevice() override
    {
        deleteContext();
    }

    void getInfo(Info& info) override
    {
        info.max_back_buffer_width = 8192;
        info.max_back_buffer_height = 8192;
        info.max_texture_stages = 2;
        info.texture_min_dim = 1;
        info.texture_max_dim = 4096;
        info.texture_max_aspect = 4096;
        /* No residency budget: SDL GPU owns the memory. */
        info.texture_ram = 0;
        strcpy(info.text[0], "SDL GPU");
        strcpy(info.text[1], "SDL");
        strcpy(info.text[2], SDL_GetPlatform());
        strcpy(info.text[3], device != 0 ? SDL_GetGPUDeviceDriver(device) : "none");
    }

    void getWindowList(WindowInfoList& list) override
    {
        list.entries = 0;
        list.count = 0;
    }

    void getTextureFormats(PixelFormatList& list) override
    {
        static PixelFormat formats[1] = {argb8888Format()};
        list.formats = formats;
        list.count = 1;
    }

    void getStatistics(Statistics& stats) override
    {
        memset(&stats, 0, sizeof(stats));
    }

    void resetStatistics() override {}
    void extCommand(w8_ulong, void*, w8_ulong) override {}

    int isBusy() override
    {
        return 0;
    }

    e_error openWindow(const OpenInfo& info, OpenResult& result) override
    {
        closeWindow();
        width = info.width;
        height = info.height;
        SDL_GPUTextureCreateInfo color = {};
        color.type = SDL_GPU_TEXTURETYPE_2D;
        color.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
        color.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        color.width = width;
        color.height = height;
        color.layer_count_or_depth = 1;
        color.num_levels = 1;
        color_target = SDL_CreateGPUTexture(device, &color);
        SDL_GPUTextureCreateInfo depth = color;
        depth.format = depth_format;
        depth.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        depth_target = SDL_CreateGPUTexture(device, &depth);
        if (color_target == 0 || depth_target == 0) {
            SDL_Log("srDD_SDLGPU: render targets: %s", SDL_GetError());
            closeWindow();
            return static_cast<e_error>(1);
        }
        readback.assign((size_t)width * height, 0);
        result.back_buffer_type = 2;
        return ERROR_NONE;
    }

    void closeWindow() override
    {
        if (device == 0) {
            return;
        }
        flush(false);
        SDL_WaitForGPUIdle(device);
        if (color_target != 0) {
            SDL_ReleaseGPUTexture(device, color_target);
        }
        if (depth_target != 0) {
            SDL_ReleaseGPUTexture(device, depth_target);
        }
        color_target = 0;
        depth_target = 0;
    }

    void beginFrame() override {}

    void endFrame() override
    {
        flush(false);
    }

    void flushFrame() override
    {
        flush(false);
    }

    void flipFrame(const Scissor*, const Scissor*, w8_ulong) override
    {
        flush(true);
    }

    void clearBuffers(const srFlags<e_buffer>& buffers) override
    {
        DrawCommand command = {};
        command.kind = DrawCommand::CLEAR;
        command.clear_buffers = buffers.value;
        command.clear_color = {clear_values.color.x, clear_values.color.y, clear_values.color.z,
                               clear_values.color.w};
        command.clear_depth = (float)clear_values.depth;
        command.clear_stencil = (uint8_t)clear_values.stencil;
        command.scissor = scissor;
        command.viewport = viewport;
        commands.push_back(command);
    }

    e_error bufferOp(const BufferCommand& command) override
    {
        switch (command.opcode) {
        case 0:
            if (locked++ == 0) {
                download();
            }
            break;
        case 1:
            if (locked > 0 && --locked == 0 && readback_dirty != 0) {
                upload();
            }
            break;
        case 2:
        case 3:
        case 4:
            row(command);
            break;
        case 6:
        case 7:
            column(command);
            break;
        }
        return ERROR_NONE;
    }

    void update(const Update& values) override
    {
        if ((values.flags & Update::UPDATE_SWAP_INTERVAL) != 0 && window != 0) {
            SDL_SetGPUSwapchainParameters(device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                          values.swap_interval != 0
                                              ? SDL_GPU_PRESENTMODE_VSYNC
                                              : SDL_GPU_PRESENTMODE_IMMEDIATE);
        }
    }

    void setScissor(const Scissor& value) override
    {
        scissor.x = value.left;
        scissor.y = value.top;
        scissor.w = value.right - value.left;
        scissor.h = value.bottom - value.top;
    }

    void setViewPort(const ViewPort& value) override
    {
        viewport.x = (float)value.x;
        viewport.y = (float)value.y;
        viewport.w = (float)value.width;
        viewport.h = (float)value.height;
        float depth_range[2] = {0.0f, 1.0f};
        memcpy(depth_range, value.extra, sizeof(depth_range));
        viewport.min_depth = depth_range[0];
        viewport.max_depth = depth_range[1];
        if (!(viewport.max_depth > viewport.min_depth)) {
            viewport.min_depth = 0.0f;
            viewport.max_depth = 1.0f;
        }
    }

    void setClearValues(const ClearValues& values) override
    {
        clear_values = values;
    }

    void setFogColor(const srVector4T<float>& color) override
    {
        fog_color[0] = color.x;
        fog_color[1] = color.y;
        fog_color[2] = color.z;
        fog_color[3] = color.w;
    }

    void setShader(const srShader& value) override
    {
        shader = value;
    }

    void setTextureParameters(w8_ulong stage, const TexParms& parms) override
    {
        if (stage < 2) {
            parameters[stage] = parms.packed;
        }
    }

    void bindTexture(w8_ulong stage, Texture& texture) override
    {
        if (stage >= 2) {
            return;
        }
        if (texture.resident_data == 0) {
            makeResident(texture);
        }
        bound[stage] = texture.resident_data;
    }

    void deleteTexture(Texture& texture) override
    {
        const uint32_t handle = texture.resident_data;
        if (handle == 0 || handle > textures.size()) {
            return;
        }
        /* Queued draws may still sample it; release after the next flush. */
        released.push_back(handle);
        for (int stage = 0; stage < 2; ++stage) {
            if (bound[stage] == handle) {
                bound[stage] = 0;
            }
        }
        texture.resident_data = 0;
        texture.resident_size = 0;
    }

    void texImage(Texture& texture, w8_ulong level) override
    {
        if (texture.resident_data == 0) {
            makeResident(texture);
            return;
        }
        queueLevel(texture, level, 0, 0, texture.width >> level, texture.height >> level,
                   texture.levels[level]);
    }

    void texSubImage(Texture& texture, w8_ulong level, w8_ulong x, w8_ulong y, w8_ulong right,
                     w8_ulong bottom) override
    {
        if (texture.resident_data == 0) {
            makeResident(texture);
            return;
        }
        // srGERD passes PartialRequest's exclusive bounds, not rectangle extents.
        queueLevel(texture, level, x, y, right - x, bottom - y, texture.levels[level]);
    }

    /* Only 32-bit ARGB textures are offered, so palettes never reach the device. */
    void setGlobalPalette(w8_ulong*, w8_ulong) override {}
    void bindPalette(Palette&) override {}
    void deletePalette(Palette&) override {}

    e_error createContext(w8_ulong_ptr handle) override
    {
        window = reinterpret_cast<SDL_Window*>(handle);
        device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, SDL_getenv("WIZ8_GPU_DEBUG") != 0,
                                     0);
        if (device == 0 || !SDL_ClaimWindowForGPUDevice(device, window)) {
            SDL_Log("srDD_SDLGPU: device: %s", SDL_GetError());
            return static_cast<e_error>(1);
        }
        swapchain_format = SDL_GetGPUSwapchainTextureFormat(device, window);
        depth_format = SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
                                                    SDL_GPU_TEXTURETYPE_2D,
                                                    SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)
                           ? SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT
                           : SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
        vertex_shader = createShader(sr_vertex_spv, sizeof(sr_vertex_spv),
                                     SDL_GPU_SHADERSTAGE_VERTEX, 0);
        fragment_shader = createShader(sr_fragment_spv, sizeof(sr_fragment_spv),
                                       SDL_GPU_SHADERSTAGE_FRAGMENT, 2);
        for (int filter = 0; filter < 2; ++filter) {
            SDL_GPUSamplerCreateInfo sampler = {};
            sampler.min_filter = filter != 0 ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
            sampler.mag_filter = sampler.min_filter;
            sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
            sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
            sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
            sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
            sampler.max_lod = 16.0f;
            samplers[filter] = SDL_CreateGPUSampler(device, &sampler);
        }
        /* Handle 1 is the white texture unbound stages sample. */
        DeviceTexture white = createTexture(1, 1, 1);
        textures.push_back(white);
        PendingUpload upload = {1, 0, 0, 0, 1, 1, std::vector<uint32_t>(1, 0xffffffffu)};
        uploads.push_back(upload);
        return vertex_shader != 0 && fragment_shader != 0 ? ERROR_NONE : static_cast<e_error>(1);
    }

    void deleteContext() override
    {
        if (device == 0) {
            return;
        }
        closeWindow();
        for (size_t index = 0; index < textures.size(); ++index) {
            if (textures[index].texture != 0) {
                SDL_ReleaseGPUTexture(device, textures[index].texture);
            }
        }
        textures.clear();
        for (std::map<PipelineKey, SDL_GPUGraphicsPipeline*>::iterator it = pipelines.begin();
             it != pipelines.end(); ++it) {
            SDL_ReleaseGPUGraphicsPipeline(device, it->second);
        }
        pipelines.clear();
        for (int index = 0; index < 2; ++index) {
            if (samplers[index] != 0) {
                SDL_ReleaseGPUSampler(device, samplers[index]);
            }
        }
        if (vertex_buffer != 0) {
            SDL_ReleaseGPUBuffer(device, vertex_buffer);
        }
        SDL_ReleaseGPUShader(device, vertex_shader);
        SDL_ReleaseGPUShader(device, fragment_shader);
        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyGPUDevice(device);
        device = 0;
        vertex_buffer = 0;
        vertex_buffer_size = 0;
    }

    void getDriverInfo(DriverInfo& info) override
    {
        info.dd_api_version = SR_DD_MIN_API_VERSION;
        info.driver_id = 0;
        strcpy(info.name, "SDLGPU");
        strcpy(info.api_name, "SDL GPU");
    }

    void fence() override {}

    void getBufferPixelFormat(PixelFormat& format) override
    {
        format = argb8888Format();
    }

    void preBindTexture(w8_ulong, Texture&) override {}

    void setPolygonMode(e_polygonMode mode) override
    {
        polygon_mode = mode;
    }

    void setCullMode(e_cullMode mode) override
    {
        cull_mode = mode;
    }

    void setProjectionMatrix(const srMatrix4T<float>& matrix,
                             srMatrix4T<float>::e_type) override
    {
        /* srMatrix4T stores four row vectors; the vertex shader multiplies accordingly. */
        memcpy(projection.projection, &matrix, sizeof(projection.projection));
    }

    void setVertexArrayInfo(const srRendererDefs::VertexArrayInfo* info) override
    {
        vertex_arrays = *info;
    }

    void drawElements(srRendererDefs::e_primitive primitive, w8_ulong count,
                      srRendererDefs::e_indexType, const void* indices) override
    {
        draw(primitive, count, static_cast<const w8_ulong*>(indices), 0);
    }

    void drawArrays(srRendererDefs::e_primitive primitive, w8_long first, w8_ulong count) override
    {
        draw(primitive, count, 0, first);
    }

    void setPolygonOffset(w8_long offset) override
    {
        polygon_offset = offset;
    }

private:
    SDL_GPUShader* createShader(const uint32_t* code, size_t size, SDL_GPUShaderStage stage,
                                uint32_t sampler_count)
    {
        SDL_GPUShaderCreateInfo info = {};
        info.code_size = size;
        info.code = reinterpret_cast<const Uint8*>(code);
        info.entrypoint = "main";
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.stage = stage;
        info.num_samplers = sampler_count;
        info.num_uniform_buffers = 1;
        SDL_GPUShader* shader_object = SDL_CreateGPUShader(device, &info);
        if (shader_object == 0) {
            SDL_Log("srDD_SDLGPU: shader: %s", SDL_GetError());
        }
        return shader_object;
    }

    DeviceTexture createTexture(uint32_t texture_width, uint32_t texture_height, uint32_t levels)
    {
        SDL_GPUTextureCreateInfo info = {};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = texture_width;
        info.height = texture_height;
        info.layer_count_or_depth = 1;
        info.num_levels = levels;
        DeviceTexture result = {SDL_CreateGPUTexture(device, &info), texture_width, texture_height,
                                levels};
        return result;
    }

    void makeResident(Texture& texture)
    {
        const uint32_t levels = texture.last_level - texture.first_level + 1;
        DeviceTexture created = createTexture(texture.width, texture.height, levels);
        if (created.texture == 0) {
            SDL_Log("srDD_SDLGPU: texture %ux%u: %s", texture.width, texture.height,
                    SDL_GetError());
            return;
        }
        uint32_t handle = 0;
        for (size_t index = 1; index < textures.size(); ++index) {
            if (textures[index].texture == 0) {
                handle = (uint32_t)index + 1;
                textures[index] = created;
                break;
            }
        }
        if (handle == 0) {
            textures.push_back(created);
            handle = (uint32_t)textures.size();
        }
        texture.resident_data = handle;
        texture.resident_size = texture.width * texture.height * 4;
        for (uint32_t level = texture.first_level; level <= texture.last_level; ++level) {
            queueLevel(texture, level, 0, 0, SDL_max(texture.width >> level, 1u),
                       SDL_max(texture.height >> level, 1u), texture.levels[level]);
        }
    }

    /* Copies now: srGERD may release the level storage after the bind. */
    void queueLevel(const Texture& texture, uint32_t level, uint32_t x, uint32_t y,
                    uint32_t level_width, uint32_t level_height, const void* pixels)
    {
        if (pixels == 0 || texture.resident_data == 0) {
            return;
        }
        const uint32_t pitch = SDL_max(texture.width >> level, 1u);
        PendingUpload upload;
        upload.texture = texture.resident_data;
        upload.level = level - texture.first_level;
        upload.x = x;
        upload.y = y;
        upload.width = level_width;
        upload.height = level_height;
        upload.pixels.resize((size_t)level_width * level_height);
        const uint32_t* source = static_cast<const uint32_t*>(pixels);
        for (uint32_t row = 0; row < level_height; ++row) {
            memcpy(&upload.pixels[(size_t)row * level_width],
                   source + (size_t)(y + row) * pitch + x, level_width * 4);
        }
        uploads.push_back(upload);
    }

    void fetch(float* out, const srRendererDefs::VertexArrayInfo& arrays, int slot, uint32_t index,
               int size, const float* fallback) const
    {
        memcpy(out, fallback, size * sizeof(float));
        if (((arrays.mask.value >> slot) & 1) == 0 && slot != 0) {
            return;
        }
        const int components = SDL_min((int)arrays.components[slot], size);
        const uint32_t stride =
            arrays.strides[slot] != 0 ? arrays.strides[slot] : arrays.components[slot] * 4;
        const float* source = reinterpret_cast<const float*>(
            static_cast<const char*>(arrays.arrays[slot]) + (size_t)index * stride);
        if (arrays.arrays[slot] != 0) {
            memcpy(out, source, components * sizeof(float));
        }
    }

    void appendVertex(uint32_t index)
    {
        static const float origin[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        static const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        static const float black[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        static const float texcoord[3] = {0.0f, 0.0f, 1.0f};
        static const float no_fog[1] = {0.0f};
        Vertex vertex;
        fetch(vertex.position, vertex_arrays, srRendererDefs::VERTEX_ARRAY_POSITIONS, index, 4,
              origin);
        fetch(vertex.diffuse, vertex_arrays, srRendererDefs::VERTEX_ARRAY_DIFFUSE, index, 4, white);
        fetch(vertex.specular, vertex_arrays, srRendererDefs::VERTEX_ARRAY_SPECULAR, index, 4, black);
        fetch(&vertex.fog, vertex_arrays, srRendererDefs::VERTEX_ARRAY_SPECULAR_ALPHA, index, 1,
              no_fog);
        fetch(vertex.texcoord0, vertex_arrays, srRendererDefs::VERTEX_ARRAY_TEXCOORD0, index, 3,
              texcoord);
        fetch(vertex.texcoord1, vertex_arrays, srRendererDefs::VERTEX_ARRAY_TEXCOORD1, index, 3,
              texcoord);
        vertices.push_back(vertex);
    }

    /* Expands every primitive to a list: SDL GPU has no fans, and lists let
       consecutive draws share one vertex stream. */
    void draw(srRendererDefs::e_primitive primitive, uint32_t count, const w8_ulong* indices,
              uint32_t first)
    {
        if (count == 0) {
            return;
        }
        const uint32_t start = (uint32_t)vertices.size();
        PrimitiveClass primitive_class = PRIMITIVE_CLASS_TRIANGLES;
#define SR_INDEX(i) (indices != 0 ? indices[(i)] : first + (i))
        switch (primitive) {
        case srRendererDefs::PRIMITIVE_TRIANGLES:
            for (uint32_t i = 0; i + 2 < count; i += 3) {
                appendVertex(SR_INDEX(i));
                appendVertex(SR_INDEX(i + 1));
                appendVertex(SR_INDEX(i + 2));
            }
            break;
        case srRendererDefs::PRIMITIVE_TRIANGLE_STRIP:
            for (uint32_t i = 0; i + 2 < count; ++i) {
                const bool odd = (i & 1) != 0;
                appendVertex(SR_INDEX(odd ? i + 1 : i));
                appendVertex(SR_INDEX(odd ? i : i + 1));
                appendVertex(SR_INDEX(i + 2));
            }
            break;
        case srRendererDefs::PRIMITIVE_TRIANGLE_FAN:
            for (uint32_t i = 1; i + 1 < count; ++i) {
                appendVertex(SR_INDEX(0));
                appendVertex(SR_INDEX(i));
                appendVertex(SR_INDEX(i + 1));
            }
            break;
        case srRendererDefs::PRIMITIVE_LINES:
            primitive_class = PRIMITIVE_CLASS_LINES;
            for (uint32_t i = 0; i + 1 < count; i += 2) {
                appendVertex(SR_INDEX(i));
                appendVertex(SR_INDEX(i + 1));
            }
            break;
        case srRendererDefs::PRIMITIVE_LINE_STRIP:
            primitive_class = PRIMITIVE_CLASS_LINES;
            for (uint32_t i = 0; i + 1 < count; ++i) {
                appendVertex(SR_INDEX(i));
                appendVertex(SR_INDEX(i + 1));
            }
            break;
        case srRendererDefs::PRIMITIVE_POINTS:
            primitive_class = PRIMITIVE_CLASS_POINTS;
            for (uint32_t i = 0; i < count; ++i) {
                appendVertex(SR_INDEX(i));
            }
            break;
        }
#undef SR_INDEX
        if (vertices.size() == start) {
            return;
        }
        if (trace) {
            const Vertex& v = vertices[start];
            const float* m = projection.projection;
            SDL_Log("srDD draw prim=%d count=%u shader=%08x cull=%d pos=(%g %g %g %g) "
                    "proj=[%g %g %g %g | %g %g %g %g | %g %g %g %g | %g %g %g %g] "
                    "viewport=(%g %g %g %g) scissor=(%d %d %d %d)",
                    (int)primitive, count, shader.value, (int)cull_mode, v.position[0],
                    v.position[1], v.position[2], v.position[3], m[0], m[1], m[2], m[3], m[4],
                    m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15],
                    viewport.x, viewport.y, viewport.w, viewport.h, scissor.x, scissor.y,
                    scissor.w, scissor.h);
        }
        DrawCommand command = {};
        command.kind = DrawCommand::DRAW;
        command.pipeline_key = PipelineKey(shader, cull_mode, polygon_mode, primitive_class).value;
        command.first_vertex = start;
        command.vertex_count = (uint32_t)vertices.size() - start;
        const bool texturing = (shader.value & srShader::MASK_TEXTURING) != 0;
        for (int stage = 0; stage < 2; ++stage) {
            command.textures[stage] = texturing && bound[stage] != 0 ? bound[stage] : 1;
            // srGERD packs magnification at bits 4..5; low bits are correction.
            command.samplers[stage] = ((parameters[stage] >> 4) & 3) >= 2 ? 1 : 0;
        }
        command.vertex_uniforms = projection;
        FragmentUniforms& fragment = command.fragment_uniforms;
        memcpy(fragment.fog_color, fog_color, sizeof(fog_color));
        fragment.mode[0] = texturing ? 1 : 0;
        fragment.mode[1] = (shader.value & srShader::MASK_GRADIENT) >> srShader::GRADIENT_SHIFT;
        fragment.mode[2] = (shader.value & srShader::MASK_SECONDARY_GRADIENT) != 0;
        fragment.mode[3] = (shader.value & srShader::MASK_FOG) >> srShader::FOG_SHIFT;
        fragment.alpha[0] = (shader.value & srShader::MASK_ALPHATEST) != 0 ? 1.0f : 0.0f;
        fragment.alpha[1] = 0.0f;
        command.scissor = scissor;
        command.viewport = viewport;
        commands.push_back(command);
    }

    SDL_GPUGraphicsPipeline* pipeline(uint32_t key_value)
    {
        PipelineKey key = PipelineKey(srShader(), CULL_NONE, POLYGON_FILL, PRIMITIVE_CLASS_TRIANGLES);
        key.value = key_value;
        std::map<PipelineKey, SDL_GPUGraphicsPipeline*>::iterator found = pipelines.find(key);
        if (found != pipelines.end()) {
            return found->second;
        }
        const uint32_t pass = key_value & srShader::PASS_MASK;
        const bool depth_write = (key_value & srShader::MASK_DEPTH_WRITE) != 0;
        const bool color_write = (key_value & srShader::MASK_COLOR_WRITE) != 0;
        const uint32_t destination = key_value >> srShader::DSTBLEND_SHIFT & 7;
        const uint32_t source = key_value >> srShader::SRCBLEND_SHIFT & 3;
        const uint32_t cull = key_value >> 24 & 3;
        const uint32_t polygon = key_value >> 26 & 3;
        const uint32_t primitive = key_value >> 28 & 3;

        SDL_GPUVertexBufferDescription buffer = {};
        buffer.slot = 0;
        buffer.pitch = sizeof(Vertex);
        buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        SDL_GPUVertexAttribute attributes[6] = {
            {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(Vertex, position)},
            {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(Vertex, diffuse)},
            {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(Vertex, specular)},
            {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex, texcoord0)},
            {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex, texcoord1)},
            {5, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, offsetof(Vertex, fog)},
        };
        SDL_GPUColorTargetDescription target = {};
        target.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
        target.blend_state.enable_blend = !(source == srShader::SRCBLEND_ONE &&
                                            destination == srShader::DSTBLEND_ZERO);
        target.blend_state.src_color_blendfactor = sourceFactor(source);
        target.blend_state.dst_color_blendfactor = destinationFactor(destination);
        target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        target.blend_state.src_alpha_blendfactor = sourceFactor(source);
        target.blend_state.dst_alpha_blendfactor = destinationFactor(destination);
        target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        target.blend_state.enable_color_write_mask = true;
        target.blend_state.color_write_mask =
            color_write ? (SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G |
                           SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A)
                        : 0;

        SDL_GPUGraphicsPipelineCreateInfo info = {};
        info.vertex_shader = vertex_shader;
        info.fragment_shader = fragment_shader;
        info.vertex_input_state.vertex_buffer_descriptions = &buffer;
        info.vertex_input_state.num_vertex_buffers = 1;
        info.vertex_input_state.vertex_attributes = attributes;
        info.vertex_input_state.num_vertex_attributes = 6;
        info.primitive_type = primitive == PRIMITIVE_CLASS_LINES    ? SDL_GPU_PRIMITIVETYPE_LINELIST
                              : primitive == PRIMITIVE_CLASS_POINTS ? SDL_GPU_PRIMITIVETYPE_POINTLIST
                                                                   : SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode =
            polygon == POLYGON_LINE ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = cull == CULL_BACK    ? SDL_GPU_CULLMODE_BACK
                                          : cull == CULL_FRONT ? SDL_GPU_CULLMODE_FRONT
                                                               : SDL_GPU_CULLMODE_NONE;
        info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        info.rasterizer_state.enable_depth_clip = true;
        info.depth_stencil_state.compare_op = compareOp(pass);
        info.depth_stencil_state.enable_depth_test = pass != srShader::PASS_ALWAYS || depth_write;
        info.depth_stencil_state.enable_depth_write = depth_write;
        info.target_info.color_target_descriptions = &target;
        info.target_info.num_color_targets = 1;
        info.target_info.depth_stencil_format = depth_format;
        info.target_info.has_depth_stencil_target = true;
        SDL_GPUGraphicsPipeline* created = SDL_CreateGPUGraphicsPipeline(device, &info);
        if (created == 0) {
            SDL_Log("srDD_SDLGPU: pipeline %08x: %s", key_value, SDL_GetError());
        }
        pipelines[key] = created;
        return created;
    }

    SDL_GPUCommandBuffer* commandBuffer()
    {
        if (command_buffer == 0) {
            command_buffer = SDL_AcquireGPUCommandBuffer(device);
        }
        return command_buffer;
    }

    void uploadPending(SDL_GPUCommandBuffer* command)
    {
        size_t texture_bytes = 0;
        for (size_t index = 0; index < uploads.size(); ++index) {
            texture_bytes += uploads[index].pixels.size() * 4;
        }
        const size_t vertex_bytes = vertices.size() * sizeof(Vertex);
        if (texture_bytes + vertex_bytes == 0) {
            return;
        }
        if (vertex_bytes > vertex_buffer_size) {
            if (vertex_buffer != 0) {
                SDL_ReleaseGPUBuffer(device, vertex_buffer);
            }
            vertex_buffer_size = SDL_max((uint32_t)vertex_bytes * 2, 1u << 20);
            SDL_GPUBufferCreateInfo buffer = {};
            buffer.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
            buffer.size = vertex_buffer_size;
            vertex_buffer = SDL_CreateGPUBuffer(device, &buffer);
        }
        SDL_GPUTransferBufferCreateInfo transfer_info = {};
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = (Uint32)(texture_bytes + vertex_bytes);
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
        char* mapped = static_cast<char*>(SDL_MapGPUTransferBuffer(device, transfer, false));
        size_t offset = 0;
        if (vertex_bytes != 0) {
            memcpy(mapped, vertices.data(), vertex_bytes);
        }
        offset += vertex_bytes;
        for (size_t index = 0; index < uploads.size(); ++index) {
            memcpy(mapped + offset, uploads[index].pixels.data(), uploads[index].pixels.size() * 4);
            offset += uploads[index].pixels.size() * 4;
        }
        SDL_UnmapGPUTransferBuffer(device, transfer);

        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
        if (vertex_bytes != 0) {
            SDL_GPUTransferBufferLocation source = {transfer, 0};
            SDL_GPUBufferRegion destination = {vertex_buffer, 0, (Uint32)vertex_bytes};
            SDL_UploadToGPUBuffer(copy, &source, &destination, true);
        }
        offset = vertex_bytes;
        for (size_t index = 0; index < uploads.size(); ++index) {
            const PendingUpload& upload = uploads[index];
            const DeviceTexture& texture = textures[upload.texture - 1];
            if (texture.texture != 0) {
                SDL_GPUTextureTransferInfo source = {transfer, (Uint32)offset, upload.width,
                                                     upload.height};
                SDL_GPUTextureRegion destination = {};
                destination.texture = texture.texture;
                destination.mip_level = upload.level;
                destination.x = upload.x;
                destination.y = upload.y;
                destination.w = upload.width;
                destination.h = upload.height;
                destination.d = 1;
                SDL_UploadToGPUTexture(copy, &source, &destination, false);
            }
            offset += upload.pixels.size() * 4;
        }
        SDL_EndGPUCopyPass(copy);
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        uploads.clear();
    }

    void replay(SDL_GPUCommandBuffer* command)
    {
        SDL_GPURenderPass* pass = 0;
        for (size_t index = 0; index < commands.size(); ++index) {
            const DrawCommand& entry = commands[index];
            if (entry.kind == DrawCommand::CLEAR) {
                /* A clear starts a fresh pass whose load operations perform it. */
                if (pass != 0) {
                    SDL_EndGPURenderPass(pass);
                }
                pass = beginPass(command, &entry);
                continue;
            }
            if (pass == 0) {
                pass = beginPass(command, 0);
            }
            SDL_GPUGraphicsPipeline* state = pipeline(entry.pipeline_key);
            if (state == 0) {
                continue;
            }
            SDL_BindGPUGraphicsPipeline(pass, state);
            SDL_GPUViewport view = entry.viewport;
            if (view.w <= 0.0f || view.h <= 0.0f) {
                view.x = 0.0f;
                view.y = 0.0f;
                view.w = (float)width;
                view.h = (float)height;
                view.min_depth = 0.0f;
                view.max_depth = 1.0f;
            }
            SDL_SetGPUViewport(pass, &view);
            SDL_Rect clip = entry.scissor;
            if (clip.w <= 0 || clip.h <= 0) {
                clip.x = 0;
                clip.y = 0;
                clip.w = (int)width;
                clip.h = (int)height;
            }
            SDL_SetGPUScissor(pass, &clip);
            SDL_GPUBufferBinding binding = {vertex_buffer, 0};
            SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
            SDL_GPUTextureSamplerBinding bindings[2];
            for (int stage = 0; stage < 2; ++stage) {
                SDL_GPUTexture* texture = textures[entry.textures[stage] - 1].texture;
                bindings[stage].texture = texture != 0 ? texture : textures[0].texture;
                bindings[stage].sampler = samplers[entry.samplers[stage]];
            }
            SDL_BindGPUFragmentSamplers(pass, 0, bindings, 2);
            SDL_PushGPUVertexUniformData(command, 0, &entry.vertex_uniforms,
                                         sizeof(entry.vertex_uniforms));
            SDL_PushGPUFragmentUniformData(command, 0, &entry.fragment_uniforms,
                                           sizeof(entry.fragment_uniforms));
            SDL_DrawGPUPrimitives(pass, entry.vertex_count, 1, entry.first_vertex, 0);
        }
        if (pass != 0) {
            SDL_EndGPURenderPass(pass);
        }
        commands.clear();
        vertices.clear();
    }

    SDL_GPURenderPass* beginPass(SDL_GPUCommandBuffer* command, const DrawCommand* clear)
    {
        SDL_GPUColorTargetInfo color = {};
        color.texture = color_target;
        color.load_op = SDL_GPU_LOADOP_LOAD;
        color.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPUDepthStencilTargetInfo depth = {};
        depth.texture = depth_target;
        depth.load_op = SDL_GPU_LOADOP_LOAD;
        depth.store_op = SDL_GPU_STOREOP_STORE;
        depth.stencil_load_op = SDL_GPU_LOADOP_LOAD;
        depth.stencil_store_op = SDL_GPU_STOREOP_STORE;
        if (clear != 0) {
            /* Scissored clears are not handled yet; srGERD clears full frames at startup. */
            if ((clear->clear_buffers & BUFFER_COLOR) != 0) {
                color.load_op = SDL_GPU_LOADOP_CLEAR;
                color.clear_color = clear->clear_color;
            }
            if ((clear->clear_buffers & BUFFER_DEPTH) != 0) {
                depth.load_op = SDL_GPU_LOADOP_CLEAR;
                depth.clear_depth = clear->clear_depth;
            }
            if ((clear->clear_buffers & BUFFER_STENCIL) != 0) {
                depth.stencil_load_op = SDL_GPU_LOADOP_CLEAR;
                depth.clear_stencil = clear->clear_stencil;
            }
        }
        return SDL_BeginGPURenderPass(command, &color, 1, &depth);
    }

    void releaseDeleted()
    {
        for (size_t index = 0; index < released.size(); ++index) {
            DeviceTexture& texture = textures[released[index] - 1];
            if (texture.texture != 0) {
                SDL_ReleaseGPUTexture(device, texture.texture);
            }
            texture.texture = 0;
        }
        released.clear();
    }

    /* Records pending work; when presenting, blits the frame to the swapchain. */
    void flush(bool present)
    {
        if (device == 0 || color_target == 0) {
            return;
        }
        if (commands.empty() && uploads.empty() && !present && command_buffer == 0) {
            return;
        }
        SDL_GPUCommandBuffer* command = commandBuffer();
        uploadPending(command);
        replay(command);
        if (present) {
            SDL_GPUTexture* swapchain = 0;
            Uint32 swapchain_width = 0;
            Uint32 swapchain_height = 0;
            if (SDL_WaitAndAcquireGPUSwapchainTexture(command, window, &swapchain,
                                                      &swapchain_width, &swapchain_height) &&
                swapchain != 0) {
                SDL_GPUBlitInfo blit = {};
                blit.source.texture = color_target;
                blit.source.w = width;
                blit.source.h = height;
                blit.destination.texture = swapchain;
                blit.destination.w = swapchain_width;
                blit.destination.h = swapchain_height;
                blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
                blit.filter = SDL_GPU_FILTER_LINEAR;
                SDL_BlitGPUTexture(command, &blit);
            }
        }
        SDL_SubmitGPUCommandBuffer(command);
        command_buffer = 0;
        releaseDeleted();
    }

    /* Waits for the frame so far and copies the color target into readback. */
    void download()
    {
        if (device == 0 || color_target == 0) {
            return;
        }
        SDL_GPUCommandBuffer* command = commandBuffer();
        uploadPending(command);
        replay(command);
        SDL_GPUTransferBufferCreateInfo info = {};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        info.size = width * height * 4;
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &info);
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
        SDL_GPUTextureRegion source = {};
        source.texture = color_target;
        source.w = width;
        source.h = height;
        source.d = 1;
        SDL_GPUTextureTransferInfo destination = {transfer, 0, width, height};
        SDL_DownloadFromGPUTexture(copy, &source, &destination);
        SDL_EndGPUCopyPass(copy);
        SDL_GPUFence* fence_object = SDL_SubmitGPUCommandBufferAndAcquireFence(command);
        command_buffer = 0;
        SDL_WaitForGPUFences(device, true, &fence_object, 1);
        SDL_ReleaseGPUFence(device, fence_object);
        const void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
        memcpy(readback.data(), mapped, readback.size() * 4);
        SDL_UnmapGPUTransferBuffer(device, transfer);
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        readback_dirty = 0;
        releaseDeleted();
    }

    void upload()
    {
        SDL_GPUCommandBuffer* command = commandBuffer();
        SDL_GPUTransferBufferCreateInfo info = {};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        info.size = width * height * 4;
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &info);
        memcpy(SDL_MapGPUTransferBuffer(device, transfer, false), readback.data(),
               readback.size() * 4);
        SDL_UnmapGPUTransferBuffer(device, transfer);
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
        SDL_GPUTextureTransferInfo source = {transfer, 0, width, height};
        SDL_GPUTextureRegion destination = {};
        destination.texture = color_target;
        destination.w = width;
        destination.h = height;
        destination.d = 1;
        SDL_UploadToGPUTexture(copy, &source, &destination, false);
        SDL_EndGPUCopyPass(copy);
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        readback_dirty = 0;
    }

    void row(const BufferCommand& command)
    {
        if (command.y < 0 || command.y >= (w8_long)height || command.x < 0) {
            return;
        }
        const w8_long count = SDL_min(command.count, (w8_long)width - command.x);
        uint32_t* line = &readback[(size_t)command.y * width + command.x];
        uint32_t* data = static_cast<uint32_t*>(command.data);
        for (w8_long index = 0; index < count; ++index) {
            if (command.opcode == 2) {
                data[index] = line[index];
            } else {
                line[index] = command.opcode == 3 ? data[index] : data[0];
            }
        }
        readback_dirty |= command.opcode != 2;
    }

    void column(const BufferCommand& command)
    {
        if (command.x < 0 || command.x >= (w8_long)width || command.y < 0) {
            return;
        }
        const w8_long count = SDL_min(command.count, (w8_long)height - command.y);
        uint32_t* data = static_cast<uint32_t*>(command.data);
        for (w8_long index = 0; index < count; ++index) {
            uint32_t& pixel = readback[(size_t)(command.y + index) * width + command.x];
            if (command.opcode == 6) {
                data[index] = pixel;
            } else {
                pixel = data[index];
            }
        }
        readback_dirty |= command.opcode != 6;
    }

    SDL_GPUDevice* device;
    SDL_Window* window;
    SDL_GPUTexture* color_target;
    SDL_GPUTexture* depth_target;
    SDL_GPUTextureFormat depth_format;
    SDL_GPUTextureFormat swapchain_format;
    SDL_GPUShader* vertex_shader;
    SDL_GPUShader* fragment_shader;
    SDL_GPUSampler* samplers[2];
    SDL_GPUBuffer* vertex_buffer;
    uint32_t vertex_buffer_size;
    SDL_GPUCommandBuffer* command_buffer;
    uint32_t width;
    uint32_t height;

    srShader shader;
    e_cullMode cull_mode;
    e_polygonMode polygon_mode;
    w8_long polygon_offset;
    VertexUniforms projection;
    ClearValues clear_values;
    float fog_color[4];
    SDL_Rect scissor;
    SDL_GPUViewport viewport;
    srRendererDefs::VertexArrayInfo vertex_arrays;
    uint32_t bound[2];
    uint32_t parameters[2];

    std::map<PipelineKey, SDL_GPUGraphicsPipeline*> pipelines;
    std::vector<DeviceTexture> textures;
    std::vector<uint32_t> released;
    std::vector<PendingUpload> uploads;
    std::vector<Vertex> vertices;
    std::vector<DrawCommand> commands;
    std::vector<uint32_t> readback;
    int readback_dirty;
    int locked;
    bool trace;
};

} // namespace

srDD* srCreateSDLGPUDevice()
{
    return new SDLGPUDevice;
}

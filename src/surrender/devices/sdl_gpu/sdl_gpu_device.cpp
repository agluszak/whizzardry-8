#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <span>
#include <utility>
#include <vector>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <glm/gtc/type_ptr.hpp>

#include "compat/compiler.h"
#include "glm/ext/vector_float1.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int4.hpp"
#include "glm/fwd.hpp"
#include "glm/matrix.hpp"
#include "sr_fragment.dxil.h"
#include "sr_fragment.msl.h"
#include "sr_fragment.spv.h"
#include "sr_vertex.dxil.h"
#include "sr_vertex.msl.h"
#include "sr_vertex.spv.h"
#include "surrender/SDLGPUDevice.h"
#include "surrender/srRendererDefs.h"
#include "surrender/srShader.h"
#include "wiz8/surface_storage.h"

namespace {
SDL_GPUCompareOp compareOp(uint32_t pass)
{
    static const SDL_GPUCompareOp ops[8] = {
        SDL_GPU_COMPAREOP_NEVER,
        SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_COMPAREOP_EQUAL,
        SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
        SDL_GPU_COMPAREOP_GREATER,
        SDL_GPU_COMPAREOP_NOT_EQUAL,
        SDL_GPU_COMPAREOP_GREATER_OR_EQUAL,
        SDL_GPU_COMPAREOP_ALWAYS,
    };
    return ops[pass & 7];
}

SDL_GPUBlendFactor sourceFactor(uint32_t blend)
{
    static const SDL_GPUBlendFactor factors[4] = {
        SDL_GPU_BLENDFACTOR_ZERO,
        SDL_GPU_BLENDFACTOR_ONE,
        SDL_GPU_BLENDFACTOR_SRC_ALPHA,
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

} // namespace

SDLGPUDevice::SDLGPUDevice()
    : device(0), window(0), color_target(0), depth_target(0), depth_format(), vertex_shader(0),
      fragment_shader(0), vertex_buffer(0), vertex_buffer_size(0), command_buffer(0), width(0),
      height(0), cull_mode(CULL_NONE), polygon_offset(0), vertex_arrays(), readback_dirty(false),
      locked(0), trace(SDL_getenv("WIZ8_SRDD_TRACE") != 0)
{
    projection.projection = glm::mat4(1);
    memset(&clear_values, 0, sizeof(clear_values));
    fog_color = glm::vec4(0);
    memset(&scissor, 0, sizeof(scissor));
    memset(&viewport, 0, sizeof(viewport));
    bound[0] = bound[1] = 0;
    parameters[0] = parameters[1] = {};
}

SDLGPUDevice::~SDLGPUDevice()
{
    deleteContext();
}

bool SDLGPUDevice::openWindow(w8_ulong target_width, w8_ulong target_height)
{
    closeWindow();
    width = target_width;
    height = target_height;
    scissor = {0, 0, static_cast<int>(width), static_cast<int>(height)};
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
        SDL_Log("SDL GPU: render targets: %s", SDL_GetError());
        closeWindow();
        return false;
    }
    readback = CreateCpuStorage(width, height, SDL_PIXELFORMAT_BGRA32);
    return true;
}

void SDLGPUDevice::closeWindow()
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

void SDLGPUDevice::flushFrame()
{
    flush(false);
}

void SDLGPUDevice::flipFrame()
{
    flush(true);
}

void SDLGPUDevice::clearBuffers(std::uint32_t buffers)
{
    if (buffers == 0 || scissor.w <= 0 || scissor.h <= 0) return;
    DrawCommand command = {};
    command.kind = DrawCommand::CLEAR;
    command.clear_buffers = buffers;
    command.clear_color = {clear_values.color.x, clear_values.color.y, clear_values.color.z,
                           clear_values.color.w};
    command.clear_depth = (float)clear_values.depth;
    command.clear_stencil = (uint8_t)clear_values.stencil;
    command.scissor = scissor;
    command.viewport = viewport;
    if (scissor.x != 0 || scissor.y != 0 || scissor.w != static_cast<int>(width) ||
        scissor.h != static_cast<int>(height)) {
        // Attachment load operations always clear the whole target. A triangle
        // performs a partial clear without depending on the current draw state.
        command.kind = DrawCommand::DRAW;
        srShader clear_shader;
        clear_shader.value =
            srShader::PASS_ALWAYS | (srShader::SRCBLEND_ONE << srShader::SRCBLEND_SHIFT);
        if (buffers & BUFFER_COLOR) clear_shader.value |= srShader::MASK_COLOR_WRITE;
        if (buffers & BUFFER_DEPTH) clear_shader.value |= srShader::MASK_DEPTH_WRITE;
        command.pipeline_key = PipelineKey(clear_shader, CULL_NONE, PRIMITIVE_CLASS_TRIANGLES, 0);
        if (buffers & BUFFER_STENCIL) command.pipeline_key.value |= PipelineKey::STENCIL_REPLACE;
        command.first_index = static_cast<uint32_t>(indices.size());
        command.index_count = 3;
        const float z = clear_values.depth * 2 - 1;
        for (const auto position :
             {glm::vec4(-1, -1, z, 1), glm::vec4(3, -1, z, 1), glm::vec4(-1, 3, z, 1)}) {
            indices.push_back(static_cast<uint32_t>(vertices.size()));
            vertices.push_back({.position = position, .diffuse = clear_values.color});
        }
        command.vertex_uniforms.projection = glm::mat4(1);
        command.viewport = {0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
        for (int stage = 0; stage < 2; ++stage) {
            command.textures[stage] = white_texture;
            command.samplers[stage] = sampler({});
        }
    }
    commands.push_back(command);
}

SDL_Surface* SDLGPUDevice::lockBuffer()
{
    if (locked++ == 0) {
        download();
    }
    return readback.get();
}

void SDLGPUDevice::unlockBuffer(bool modified)
{
    readback_dirty |= modified;
    if (locked > 0 && --locked == 0 && readback_dirty != 0) {
        upload();
    }
}

void SDLGPUDevice::setSwapInterval(w8_ulong interval)
{
    if (window != 0) {
        SDL_SetGPUSwapchainParameters(device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                      interval != 0 ? SDL_GPU_PRESENTMODE_VSYNC
                                                    : SDL_GPU_PRESENTMODE_IMMEDIATE);
    }
}

void SDLGPUDevice::setScissor(const Scissor& value)
{
    scissor.x = value.left;
    scissor.y = value.top;
    scissor.w = value.right - value.left;
    scissor.h = value.bottom - value.top;
}

void SDLGPUDevice::setViewPort(const ViewPort& value)
{
    viewport.x = (float)value.x;
    viewport.y = (float)value.y;
    viewport.w = (float)value.width;
    viewport.h = (float)value.height;
    viewport.min_depth = value.min_depth;
    viewport.max_depth = value.max_depth;
    if (!(viewport.max_depth > viewport.min_depth)) {
        viewport.min_depth = 0.0f;
        viewport.max_depth = 1.0f;
    }
}

void SDLGPUDevice::setClearValues(const ClearValues& values)
{
    clear_values = values;
}

void SDLGPUDevice::setFogColor(const glm::vec4& color)
{
    fog_color = color;
}

void SDLGPUDevice::setShader(const srShader& value)
{
    shader = value;
}

void SDLGPUDevice::setTextureParameters(w8_ulong stage, const TexParms& parms)
{
    if (stage < 2) {
        parameters[stage] = parms;
    }
}

void SDLGPUDevice::bindTexture(w8_ulong stage, const Texture& texture)
{
    if (stage >= 2) {
        return;
    }
    bound[stage] = texture.handle;
}

void SDLGPUDevice::deleteTexture(Texture& texture)
{
    auto* handle = texture.handle;
    if (handle == 0 || !textures.contains(handle)) {
        return;
    }
    /* Queued draws may still sample it; release after the next flush. */
    released.push_back(handle);
    for (int stage = 0; stage < 2; ++stage) {
        if (bound[stage] == handle) {
            bound[stage] = 0;
        }
    }
    texture.handle = 0;
    texture.size = 0;
}

SDLGPUDevice::Texture SDLGPUDevice::createTexture(w8_ulong width, w8_ulong height, w8_ulong levels)
{
    auto texture = createDeviceTexture(width, height, levels);
    if (texture.handle)
        textures.insert(texture.handle);
    else
        SDL_Log("SDL GPU texture %ux%u: %s", width, height, SDL_GetError());
    return texture;
}

void SDLGPUDevice::uploadTexture(const Texture& texture, w8_ulong level, TextureRegion region,
                                 std::span<const unsigned char> pixels, w8_ulong pitch)
{
    if (texture.handle == 0 || region.width == 0 || region.height == 0) {
        return;
    }
    const auto& target = texture;
    if (level >= target.levels || region.x + region.width > SDL_max(target.width >> level, 1u) ||
        region.y + region.height > SDL_max(target.height >> level, 1u) ||
        pitch < (region.x + region.width) * 4 ||
        pixels.size() <
            size_t(region.y + region.height - 1) * pitch + size_t(region.x + region.width) * 4) {
        return;
    }
    // A later update must not change the pixels sampled by an earlier queued draw.
    if (std::ranges::any_of(commands, [&](const DrawCommand& draw) {
            return draw.kind == DrawCommand::DRAW &&
                   (draw.textures[0] == texture.handle || draw.textures[1] == texture.handle);
        }))
        flush(false);
    PendingUpload pending{texture.handle,
                          level,
                          region.x,
                          region.y,
                          region.width,
                          region.height,
                          upload_pixels.size(),
                          level == 0 && texture.levels > 1};
    upload_pixels.resize(upload_pixels.size() + size_t(region.width) * region.height);
    for (w8_ulong row = 0; row < region.height; ++row) {
        memcpy(upload_pixels.data() + pending.offset + size_t(row) * region.width,
               pixels.data() + size_t(region.y + row) * pitch + size_t(region.x) * 4,
               size_t(region.width) * 4);
    }
    uploads.push_back(pending);
}

bool SDLGPUDevice::createContext(SDL_Window* target_window)
{
    window = target_window;
    device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL |
                                     SDL_GPU_SHADERFORMAT_DXIL,
                                 SDL_getenv("WIZ8_GPU_DEBUG") != 0, 0);
    if (device == 0) {
        SDL_Log("SDL GPU: device: %s", SDL_GetError());
        return false;
    }
    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        /* The offscreen video driver has no presentable surface; render into
               the color target and skip presentation (headless tests). */
        const char* driver = SDL_GetCurrentVideoDriver();
        if (driver == 0 || SDL_strcmp(driver, "offscreen") != 0) {
            SDL_Log("SDL GPU: device: %s", SDL_GetError());
            return false;
        }
        window = 0;
    }
    depth_format = SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
                                                SDL_GPU_TEXTURETYPE_2D,
                                                SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)
                       ? SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT
                       : SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    vertex_shader = createShader(SDL_GPU_SHADERSTAGE_VERTEX, 0);
    fragment_shader = createShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 2);
    const auto white = createTexture(1, 1, 1);
    white_texture = white.handle;
    const uint32_t pixel = 0xffffffffu;
    uploadTexture(white, 0, {0, 0, 1, 1},
                  {reinterpret_cast<const unsigned char*>(&pixel), sizeof(pixel)}, sizeof(pixel));
    return vertex_shader != nullptr && fragment_shader != nullptr;
}

void SDLGPUDevice::deleteContext()
{
    if (device == 0) {
        return;
    }
    closeWindow();
    for (auto* texture : textures) SDL_ReleaseGPUTexture(device, texture);
    white_texture = nullptr;
    released.clear();
    uploads.clear();
    upload_pixels.clear();
    textures.clear();
    for (std::map<PipelineKey, SDL_GPUGraphicsPipeline*>::iterator it = pipelines.begin();
         it != pipelines.end(); ++it) {
        SDL_ReleaseGPUGraphicsPipeline(device, it->second);
    }
    pipelines.clear();
    for (const auto& entry : samplers) {
        SDL_ReleaseGPUSampler(device, entry.second);
    }
    samplers.clear();
    if (vertex_buffer != 0) {
        SDL_ReleaseGPUBuffer(device, vertex_buffer);
    }
    if (index_buffer) SDL_ReleaseGPUBuffer(device, index_buffer);
    SDL_ReleaseGPUShader(device, vertex_shader);
    SDL_ReleaseGPUShader(device, fragment_shader);
    if (window != 0) {
        SDL_ReleaseWindowFromGPUDevice(device, window);
    }
    SDL_DestroyGPUDevice(device);
    device = 0;
    index_buffer = nullptr;
    index_buffer_size = 0;
    vertex_buffer = 0;
    vertex_buffer_size = 0;
}

void SDLGPUDevice::setCullMode(e_cullMode mode)
{
    cull_mode = mode;
}

void SDLGPUDevice::setProjectionMatrix(const glm::mat4& matrix)
{
    /* GLM and GLSL both store matrix columns. */
    projection.projection = matrix;
}

void SDLGPUDevice::setVertexArrayInfo(const srRendererDefs::VertexArrayInfo* info)
{
    vertex_arrays = *info;
}

void SDLGPUDevice::drawElements(srRendererDefs::e_primitive primitive,
                                std::span<const w8_ulong> indices)
{
    draw(primitive, static_cast<w8_ulong>(indices.size()), indices.data(), 0);
}

void SDLGPUDevice::drawArrays(srRendererDefs::e_primitive primitive, w8_long first, w8_ulong count)
{
    draw(primitive, count, 0, first);
}

void SDLGPUDevice::setPolygonOffset(w8_long offset)
{
    polygon_offset = offset;
}

SDL_GPUShader* SDLGPUDevice::createShader(SDL_GPUShaderStage stage, uint32_t sampler_count)
{
    SDL_GPUShaderCreateInfo info = {};
    const bool vertex = stage == SDL_GPU_SHADERSTAGE_VERTEX;
    const auto formats = SDL_GetGPUShaderFormats(device);
    if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.code = vertex ? reinterpret_cast<const Uint8*>(sr_vertex_spv)
                           : reinterpret_cast<const Uint8*>(sr_fragment_spv);
        info.code_size = vertex ? sizeof(sr_vertex_spv) : sizeof(sr_fragment_spv);
        info.entrypoint = "main";
    } else if (formats & SDL_GPU_SHADERFORMAT_MSL) {
        info.format = SDL_GPU_SHADERFORMAT_MSL;
        info.code = vertex ? sr_vertex_msl : sr_fragment_msl;
        info.code_size = vertex ? sizeof(sr_vertex_msl) - 1 : sizeof(sr_fragment_msl) - 1;
        info.entrypoint = "main0";
    } else if (formats & SDL_GPU_SHADERFORMAT_DXIL) {
        info.format = SDL_GPU_SHADERFORMAT_DXIL;
        info.code = vertex ? sr_vertex_dxil : sr_fragment_dxil;
        info.code_size = vertex ? sizeof(sr_vertex_dxil) - 1 : sizeof(sr_fragment_dxil) - 1;
        info.entrypoint = "main";
    } else {
        SDL_SetError("No embedded shader format for this SDL GPU backend");
        return nullptr;
    }
    info.stage = stage;
    info.num_samplers = sampler_count;
    info.num_uniform_buffers = 1;
    SDL_GPUShader* shader_object = SDL_CreateGPUShader(device, &info);
    if (shader_object == 0) {
        SDL_Log("SDL GPU: shader: %s", SDL_GetError());
    }
    return shader_object;
}

SDLGPUDevice::Texture SDLGPUDevice::createDeviceTexture(uint32_t texture_width,
                                                        uint32_t texture_height, uint32_t levels)
{
    SDL_GPUTextureCreateInfo info = {};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if (levels > 1) info.usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    info.width = texture_width;
    info.height = texture_height;
    info.layer_count_or_depth = 1;
    info.num_levels = levels;
    Texture result = {SDL_CreateGPUTexture(device, &info), texture_width * texture_height * 4,
                      texture_width, texture_height, levels};
    return result;
}

template<class Vector>
Vector SDLGPUDevice::fetch(srRendererDefs::e_vertexArray slot, uint32_t index,
                           Vector fallback) const
{
    if (((vertex_arrays.mask >> slot) & 1) == 0 && slot != srRendererDefs::VERTEX_ARRAY_POSITIONS)
        return fallback;
    if (vertex_arrays.arrays[slot] == nullptr) return fallback;
    const int components =
        SDL_min(static_cast<int>(vertex_arrays.components[slot]), Vector::length());
    const uint32_t stride = vertex_arrays.strides[slot] != 0
                                ? vertex_arrays.strides[slot]
                                : vertex_arrays.components[slot] * sizeof(float);
    const auto* source =
        static_cast<const char*>(vertex_arrays.arrays[slot]) + size_t(index) * stride;
    std::memcpy(glm::value_ptr(fallback), source, components * sizeof(float));
    return fallback;
}

void SDLGPUDevice::appendVertex(uint32_t index)
{
    using enum srRendererDefs::e_vertexArray;
    if (const auto found = vertex_remap.find(index); found != vertex_remap.end()) {
        indices.push_back(found->second);
        return;
    }
    const auto mapped = static_cast<uint32_t>(vertices.size());
    vertex_remap.emplace(index, mapped);
    indices.push_back(mapped);
    vertices.push_back({fetch(VERTEX_ARRAY_POSITIONS, index, glm::vec4(0, 0, 0, 1)),
                        fetch(VERTEX_ARRAY_DIFFUSE, index, glm::vec4(1)),
                        fetch(VERTEX_ARRAY_SPECULAR, index, glm::vec4(0)),
                        fetch(VERTEX_ARRAY_TEXCOORD0, index, glm::vec3(0, 0, 1)),
                        fetch(VERTEX_ARRAY_TEXCOORD1, index, glm::vec3(0, 0, 1)),
                        fetch(VERTEX_ARRAY_SPECULAR_ALPHA, index, glm::vec1(0)).x});
    if ((vertex_arrays.mask & (1u << VERTEX_ARRAY_TEXCOORD0)) && vertex_arrays.texture_weights[0])
        vertices.back().texcoord0.z = vertex_arrays.texture_weights[0][index];
    if ((vertex_arrays.mask & (1u << VERTEX_ARRAY_TEXCOORD1)) && vertex_arrays.texture_weights[1])
        vertices.back().texcoord1.z = vertex_arrays.texture_weights[1][index];
}

/* Expands every primitive to a list: SDL GPU has no fans, and lists let
       consecutive draws share one vertex stream. */
void SDLGPUDevice::draw(srRendererDefs::e_primitive primitive, uint32_t count,
                        const w8_ulong* elements, uint32_t first)
{
    if (count == 0) {
        return;
    }
    if (scissor.w <= 0 || scissor.h <= 0) return;
    vertex_remap.clear();
    const auto start = static_cast<uint32_t>(indices.size());
    const auto first_vertex = vertices.size();
    PrimitiveClass primitive_class = PRIMITIVE_CLASS_TRIANGLES;
#define SR_INDEX(i) (elements != 0 ? elements[(i)] : first + (i))
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
    if (indices.size() == start) {
        return;
    }
    if (trace) {
        const Vertex& v = vertices[first_vertex];
        const auto rows = glm::transpose(projection.projection);
        const float* m = glm::value_ptr(rows);
        SDL_Log("SDLGPUDevice draw prim=%d count=%u shader=%08x cull=%d pos=(%g %g %g %g) "
                "proj=[%g %g %g %g | %g %g %g %g | %g %g %g %g | %g %g %g %g] "
                "viewport=(%g %g %g %g) scissor=(%d %d %d %d)",
                (int)primitive, count, shader.value, (int)cull_mode, v.position[0], v.position[1],
                v.position[2], v.position[3], m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8],
                m[9], m[10], m[11], m[12], m[13], m[14], m[15], viewport.x, viewport.y, viewport.w,
                viewport.h, scissor.x, scissor.y, scissor.w, scissor.h);
        if (SDL_getenv("WIZ8_SRDD_VERTICES")) {
            for (size_t i = first_vertex; i < vertices.size(); ++i) {
                const Vertex& p = vertices[i];
                SDL_Log("SDLGPUDevice vertex %08x (%g %g %g %g) uv=(%g %g %g) rgba=(%g %g %g %g)",
                        shader.value, p.position[0], p.position[1], p.position[2], p.position[3],
                        p.texcoord0[0], p.texcoord0[1], p.texcoord0[2], p.diffuse[0], p.diffuse[1],
                        p.diffuse[2], p.diffuse[3]);
            }
        }
    }
    DrawCommand command = {};
    command.kind = DrawCommand::DRAW;
    command.pipeline_key = PipelineKey(shader, cull_mode, primitive_class, polygon_offset);
    command.first_index = start;
    command.index_count = static_cast<uint32_t>(indices.size()) - start;
    const bool texturing = (shader.value & srShader::MASK_TEXTURING) != 0;
    for (int stage = 0; stage < 2; ++stage) {
        command.textures[stage] =
            texturing && bound[stage] != nullptr ? bound[stage] : white_texture;
        command.samplers[stage] = sampler(parameters[stage]);
    }
    command.vertex_uniforms = projection;
    FragmentUniforms& fragment = command.fragment_uniforms;
    fragment.fog_color = fog_color;
    fragment.mode = glm::ivec4(texturing ? 1 : 0,
                               (shader.value & srShader::MASK_GRADIENT) >> srShader::GRADIENT_SHIFT,
                               (shader.value & srShader::MASK_SECONDARY_GRADIENT) != 0,
                               (shader.value & srShader::MASK_FOG) >> srShader::FOG_SHIFT);
    fragment.alpha =
        glm::vec4((shader.value & srShader::MASK_ALPHATEST) != 0 ? 1.0f : 0.0f, 0, 0, 0);
    command.scissor = scissor;
    command.viewport = viewport;
    commands.push_back(command);
}

SDL_GPUSampler* SDLGPUDevice::sampler(const TexParms& parameters)
{
    if (const auto found = samplers.find(parameters); found != samplers.end()) return found->second;
    SDL_GPUSamplerCreateInfo info{};
    info.mag_filter = parameters.mag;
    info.min_filter = parameters.min;
    info.mipmap_mode = parameters.mipmap;
    info.address_mode_u = parameters.wrap_s;
    info.address_mode_v = parameters.wrap_t;
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.mip_lod_bias = parameters.mipmap_bias;
    info.max_lod = parameters.max_lod;
    auto* created = SDL_CreateGPUSampler(device, &info);
    samplers.emplace(parameters, created);
    return created;
}

SDL_GPUGraphicsPipeline* SDLGPUDevice::pipeline(const PipelineKey& key)
{
    const uint32_t key_value = key.value;
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
    target.blend_state.enable_blend =
        !(source == srShader::SRCBLEND_ONE && destination == srShader::DSTBLEND_ZERO);
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
    info.primitive_type = primitive == PRIMITIVE_CLASS_LINES ? SDL_GPU_PRIMITIVETYPE_LINELIST
                          : primitive == PRIMITIVE_CLASS_POINTS
                              ? SDL_GPU_PRIMITIVETYPE_POINTLIST
                              : SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = cull == CULL_BACK    ? SDL_GPU_CULLMODE_BACK
                                      : cull == CULL_FRONT ? SDL_GPU_CULLMODE_FRONT
                                                           : SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.rasterizer_state.enable_depth_clip = true;
    // Surrender's positive offset pulls overlays toward the camera.
    info.rasterizer_state.enable_depth_bias = key.offset != 0;
    info.rasterizer_state.depth_bias_constant_factor = -float(key.offset);
    info.rasterizer_state.depth_bias_slope_factor = -float(key.offset);
    info.depth_stencil_state.compare_op = compareOp(pass);
    info.depth_stencil_state.enable_depth_test = pass != srShader::PASS_ALWAYS || depth_write;
    info.depth_stencil_state.enable_depth_write = depth_write;
    if (key_value & PipelineKey::STENCIL_REPLACE) {
        const SDL_GPUStencilOpState stencil{SDL_GPU_STENCILOP_REPLACE, SDL_GPU_STENCILOP_REPLACE,
                                            SDL_GPU_STENCILOP_REPLACE, SDL_GPU_COMPAREOP_ALWAYS};
        info.depth_stencil_state.front_stencil_state = stencil;
        info.depth_stencil_state.back_stencil_state = stencil;
        info.depth_stencil_state.enable_stencil_test = true;
        info.depth_stencil_state.write_mask = 0xff;
    }
    info.target_info.color_target_descriptions = &target;
    info.target_info.num_color_targets = 1;
    info.target_info.depth_stencil_format = depth_format;
    info.target_info.has_depth_stencil_target = true;
    SDL_GPUGraphicsPipeline* created = SDL_CreateGPUGraphicsPipeline(device, &info);
    if (created == 0) {
        SDL_Log("SDL GPU: pipeline %08x: %s", key_value, SDL_GetError());
    }
    pipelines[key] = created;
    return created;
}

SDL_GPUCommandBuffer* SDLGPUDevice::commandBuffer()
{
    if (command_buffer == 0) {
        command_buffer = SDL_AcquireGPUCommandBuffer(device);
    }
    return command_buffer;
}

void SDLGPUDevice::uploadPending(SDL_GPUCommandBuffer* command)
{
    const size_t texture_bytes = upload_pixels.size() * sizeof(uint32_t);
    const size_t vertex_bytes = vertices.size() * sizeof(Vertex);
    const size_t index_bytes = indices.size() * sizeof(uint32_t);
    if (texture_bytes + vertex_bytes + index_bytes == 0) {
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
    if (index_bytes > index_buffer_size) {
        if (index_buffer) SDL_ReleaseGPUBuffer(device, index_buffer);
        index_buffer_size = SDL_max(static_cast<uint32_t>(index_bytes) * 2, 1u << 18);
        const SDL_GPUBufferCreateInfo buffer{SDL_GPU_BUFFERUSAGE_INDEX, index_buffer_size, 0};
        index_buffer = SDL_CreateGPUBuffer(device, &buffer);
    }
    SDL_GPUTransferBufferCreateInfo transfer_info = {};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = (Uint32)(texture_bytes + vertex_bytes + index_bytes);
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
    char* mapped = static_cast<char*>(SDL_MapGPUTransferBuffer(device, transfer, false));
    size_t offset = 0;
    if (vertex_bytes != 0) {
        memcpy(mapped, vertices.data(), vertex_bytes);
    }
    if (index_bytes) memcpy(mapped + vertex_bytes, indices.data(), index_bytes);
    offset += vertex_bytes + index_bytes;
    if (texture_bytes) memcpy(mapped + offset, upload_pixels.data(), texture_bytes);
    SDL_UnmapGPUTransferBuffer(device, transfer);

    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
    if (vertex_bytes != 0) {
        SDL_GPUTransferBufferLocation source = {transfer, 0};
        SDL_GPUBufferRegion destination = {vertex_buffer, 0, (Uint32)vertex_bytes};
        SDL_UploadToGPUBuffer(copy, &source, &destination, true);
    }
    if (index_bytes) {
        const SDL_GPUTransferBufferLocation source{transfer, static_cast<Uint32>(vertex_bytes)};
        const SDL_GPUBufferRegion destination{index_buffer, 0, static_cast<Uint32>(index_bytes)};
        SDL_UploadToGPUBuffer(copy, &source, &destination, true);
    }
    offset = vertex_bytes + index_bytes;
    for (size_t index = 0; index < uploads.size(); ++index) {
        const PendingUpload& upload = uploads[index];
        if (upload.texture != nullptr) {
            SDL_GPUTextureTransferInfo source = {
                transfer, static_cast<Uint32>(offset + upload.offset * sizeof(uint32_t)),
                upload.width, upload.height};
            SDL_GPUTextureRegion destination = {};
            destination.texture = upload.texture;
            destination.mip_level = upload.level;
            destination.x = upload.x;
            destination.y = upload.y;
            destination.w = upload.width;
            destination.h = upload.height;
            destination.d = 1;
            SDL_UploadToGPUTexture(copy, &source, &destination, false);
        }
    }
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    // Generation must follow the base-level copies and precede draws that sample them.
    for (const auto& upload : uploads)
        if (upload.generate_mipmaps) SDL_GenerateMipmapsForGPUTexture(command, upload.texture);
    uploads.clear();
    upload_pixels.clear();
}

void SDLGPUDevice::replay(SDL_GPUCommandBuffer* command)
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
        if (entry.pipeline_key.value & PipelineKey::STENCIL_REPLACE)
            SDL_SetGPUStencilReference(pass, entry.clear_stencil);
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
        SDL_SetGPUScissor(pass, &clip);
        SDL_GPUBufferBinding binding = {vertex_buffer, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
        SDL_GPUTextureSamplerBinding bindings[2];
        for (int stage = 0; stage < 2; ++stage) {
            bindings[stage].texture = entry.textures[stage];
            bindings[stage].sampler = entry.samplers[stage];
        }
        SDL_BindGPUFragmentSamplers(pass, 0, bindings, 2);
        SDL_PushGPUVertexUniformData(command, 0, &entry.vertex_uniforms,
                                     sizeof(entry.vertex_uniforms));
        SDL_PushGPUFragmentUniformData(command, 0, &entry.fragment_uniforms,
                                       sizeof(entry.fragment_uniforms));
        const SDL_GPUBufferBinding index_binding{index_buffer, 0};
        SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_DrawGPUIndexedPrimitives(pass, entry.index_count, 1, entry.first_index, 0, 0);
    }
    if (pass != 0) {
        SDL_EndGPURenderPass(pass);
    }
    commands.clear();
    vertices.clear();
    indices.clear();
    vertex_remap.clear();
}

SDL_GPURenderPass* SDLGPUDevice::beginPass(SDL_GPUCommandBuffer* command, const DrawCommand* clear)
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

void SDLGPUDevice::releaseDeleted()
{
    for (auto* texture : released) {
        if (textures.erase(texture)) SDL_ReleaseGPUTexture(device, texture);
    }
    released.clear();
}

/* Records pending work; when presenting, blits the frame to the swapchain. */
void SDLGPUDevice::flush(bool present)
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
    if (present && window != 0) {
        SDL_GPUTexture* swapchain = 0;
        Uint32 swapchain_width = 0;
        Uint32 swapchain_height = 0;
        if (SDL_WaitAndAcquireGPUSwapchainTexture(command, window, &swapchain, &swapchain_width,
                                                  &swapchain_height) &&
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
void SDLGPUDevice::download()
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
    memcpy(readback->pixels, mapped, size_t(readback->pitch) * readback->h);
    SDL_UnmapGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    readback_dirty = 0;
    releaseDeleted();
}

void SDLGPUDevice::upload()
{
    SDL_GPUCommandBuffer* command = commandBuffer();
    SDL_GPUTransferBufferCreateInfo info = {};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    info.size = width * height * 4;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &info);
    memcpy(SDL_MapGPUTransferBuffer(device, transfer, false), readback->pixels,
           size_t(readback->pitch) * readback->h);
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

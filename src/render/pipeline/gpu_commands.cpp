#include <algorithm>
#include <array>
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
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/gpu_renderer.h"
#include "wiz8/render/vertex_stream.h"
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

bool GpuRenderer::gpuOpenWindow(std::uint32_t target_width, std::uint32_t target_height)
{
    gpuCloseWindow();
    gpu_width = target_width;
    gpu_height = target_height;
    gpu_scissor = {0, 0, static_cast<int>(gpu_width), static_cast<int>(gpu_height)};
    SDL_GPUTextureCreateInfo color = {};
    color.type = SDL_GPU_TEXTURETYPE_2D;
    color.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    color.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    color.width = gpu_width;
    color.height = gpu_height;
    color.layer_count_or_depth = 1;
    color.num_levels = 1;
    gpu_color_target = SDL_CreateGPUTexture(gpu_device, &color);
    SDL_GPUTextureCreateInfo depth = color;
    depth.format = gpu_depth_format;
    depth.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    gpu_depth_target = SDL_CreateGPUTexture(gpu_device, &depth);
    if (gpu_color_target == 0 || gpu_depth_target == 0) {
        SDL_Log("SDL GPU: render targets: %s", SDL_GetError());
        gpuCloseWindow();
        return false;
    }
    gpu_readback = CreateCpuStorage(gpu_width, gpu_height, SDL_PIXELFORMAT_BGRA32);
    return true;
}

void GpuRenderer::gpuCloseWindow()
{
    if (gpu_device == 0) {
        return;
    }
    gpuFlush(false);
    SDL_WaitForGPUIdle(gpu_device);
    if (gpu_color_target != 0) {
        SDL_ReleaseGPUTexture(gpu_device, gpu_color_target);
    }
    if (gpu_depth_target != 0) {
        SDL_ReleaseGPUTexture(gpu_device, gpu_depth_target);
    }
    gpu_color_target = 0;
    gpu_depth_target = 0;
}

void GpuRenderer::gpuFlushFrame()
{
    gpuFlush(false);
}

void GpuRenderer::gpuFlipFrame()
{
    gpuFlush(true);
}

void GpuRenderer::gpuClearBuffers(std::uint32_t buffers, const ClearValues& values)
{
    if (buffers == 0 || gpu_scissor.w <= 0 || gpu_scissor.h <= 0) return;
    GpuDrawCommand command = {};
    command.kind = GpuDrawCommand::CLEAR;
    command.clear_buffers = buffers;
    command.clear_color = {values.color.x, values.color.y, values.color.z, values.color.w};
    command.clear_depth = (float)values.depth;
    command.clear_stencil = (uint8_t)values.stencil;
    command.scissor = gpu_scissor;
    command.viewport = gpu_viewport;
    if (gpu_scissor.x != 0 || gpu_scissor.y != 0 || gpu_scissor.w != static_cast<int>(gpu_width) ||
        gpu_scissor.h != static_cast<int>(gpu_height)) {
        // Attachment load operations always clear the whole target. A triangle
        // performs a partial clear without depending on the current draw state.
        command.kind = GpuDrawCommand::DRAW;
        DrawShader clear_shader;
        clear_shader.value =
            DrawShader::PASS_ALWAYS | (DrawShader::SRCBLEND_ONE << DrawShader::SRCBLEND_SHIFT);
        if (buffers & GPU_BUFFER_COLOR) clear_shader.value |= DrawShader::MASK_COLOR_WRITE;
        if (buffers & GPU_BUFFER_DEPTH) clear_shader.value |= DrawShader::MASK_DEPTH_WRITE;
        command.pipeline_key = GpuPipelineKey(clear_shader, PRIMITIVE_CLASS_TRIANGLES, 0);
        if (buffers & GPU_BUFFER_STENCIL)
            command.pipeline_key.value |= GpuPipelineKey::STENCIL_REPLACE;
        command.first_index = static_cast<uint32_t>(gpu_indices.size());
        command.index_count = 3;
        const float z = values.depth * 2 - 1;
        for (const auto position :
             {glm::vec4(-1, -1, z, 1), glm::vec4(3, -1, z, 1), glm::vec4(-1, 3, z, 1)}) {
            gpu_indices.push_back(static_cast<uint32_t>(gpu_vertices.size()));
            gpu_vertices.push_back({.position = position, .diffuse = values.color});
        }
        command.vertex_uniforms.projection = glm::mat4(1);
        command.viewport = {0, 0, static_cast<float>(gpu_width), static_cast<float>(gpu_height),
                            0, 1};
        for (int stage = 0; stage < 2; ++stage) {
            command.textures[stage] = gpu_white_texture;
            command.samplers[stage] = gpuSampler({});
        }
    }
    gpu_commands.push_back(command);
}

SDL_Surface* GpuRenderer::gpuLockBuffer()
{
    if (gpu_locked++ == 0) {
        gpuDownload();
    }
    return gpu_readback.get();
}

void GpuRenderer::gpuUnlockBuffer(bool modified)
{
    gpu_readback_dirty |= modified;
    if (gpu_locked > 0 && --gpu_locked == 0 && gpu_readback_dirty != 0) {
        gpuUpload();
    }
}

void GpuRenderer::gpuSetSwapInterval(std::uint32_t interval)
{
    if (gpu_window != 0) {
        SDL_SetGPUSwapchainParameters(gpu_device, gpu_window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                      interval != 0 ? SDL_GPU_PRESENTMODE_VSYNC
                                                    : SDL_GPU_PRESENTMODE_IMMEDIATE);
    }
}

void GpuRenderer::gpuSetScissor(const GpuScissor& value)
{
    gpu_scissor.x = value.left;
    gpu_scissor.y = value.top;
    gpu_scissor.w = value.right - value.left;
    gpu_scissor.h = value.bottom - value.top;
}

void GpuRenderer::gpuSetViewPort(const GpuViewPort& value)
{
    gpu_viewport.x = (float)value.x;
    gpu_viewport.y = (float)value.y;
    gpu_viewport.w = (float)value.width;
    gpu_viewport.h = (float)value.height;
    gpu_viewport.min_depth = value.min_depth;
    gpu_viewport.max_depth = value.max_depth;
    if (!(gpu_viewport.max_depth > gpu_viewport.min_depth)) {
        gpu_viewport.min_depth = 0.0f;
        gpu_viewport.max_depth = 1.0f;
    }
}

void GpuRenderer::gpuDeleteTexture(NativeTexture& texture)
{
    auto* handle = texture.handle;
    if (handle == 0 || !gpu_textures.contains(handle)) {
        return;
    }
    /* Queued draws may still sample it; release after the next flush. */
    gpu_released.push_back(handle);
    texture.handle = 0;
    texture.size = 0;
}

GpuRenderer::NativeTexture GpuRenderer::gpuCreateTexture(std::uint32_t gpu_width,
                                                         std::uint32_t gpu_height,
                                                         std::uint32_t levels)
{
    auto texture = gpuCreateDeviceTexture(gpu_width, gpu_height, levels);
    if (texture.handle)
        gpu_textures.insert(texture.handle);
    else
        SDL_Log("SDL GPU texture %ux%u: %s", gpu_width, gpu_height, SDL_GetError());
    return texture;
}

void GpuRenderer::gpuUploadTexture(const NativeTexture& texture, std::uint32_t level,
                                   GpuTextureRegion region, std::span<const unsigned char> pixels,
                                   std::uint32_t pitch)
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
    if (std::ranges::any_of(gpu_commands, [&](const GpuDrawCommand& gpuDraw) {
            return gpuDraw.kind == GpuDrawCommand::DRAW &&
                   (gpuDraw.textures[0] == texture.handle || gpuDraw.textures[1] == texture.handle);
        }))
        gpuFlush(false);
    GpuPendingUpload pending{texture.handle,
                             level,
                             region.x,
                             region.y,
                             region.width,
                             region.height,
                             gpu_upload_pixels.size(),
                             level == 0 && texture.levels > 1};
    gpu_upload_pixels.resize(gpu_upload_pixels.size() + size_t(region.width) * region.height);
    for (std::uint32_t row = 0; row < region.height; ++row) {
        memcpy(gpu_upload_pixels.data() + pending.offset + size_t(row) * region.width,
               pixels.data() + size_t(region.y + row) * pitch + size_t(region.x) * 4,
               size_t(region.width) * 4);
    }
    gpu_uploads.push_back(pending);
}

bool GpuRenderer::gpuCreateContext(SDL_Window* target_window)
{
    gpu_trace = SDL_getenv("WIZ8_SRDD_TRACE") != nullptr;
    gpu_window = target_window;
    gpu_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL |
                                         SDL_GPU_SHADERFORMAT_DXIL,
                                     SDL_getenv("WIZ8_GPU_DEBUG") != 0, 0);
    if (gpu_device == 0) {
        SDL_Log("SDL GPU: device: %s", SDL_GetError());
        return false;
    }
    if (!SDL_ClaimWindowForGPUDevice(gpu_device, gpu_window)) {
        /* The offscreen video driver has no presentable surface; render into
               the color target and skip presentation (headless tests). */
        const char* driver = SDL_GetCurrentVideoDriver();
        if (driver == 0 || SDL_strcmp(driver, "offscreen") != 0) {
            SDL_Log("SDL GPU: device: %s", SDL_GetError());
            return false;
        }
        gpu_window = 0;
    }
    gpu_depth_format = SDL_GPUTextureSupportsFormat(
                           gpu_device, SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
                           SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)
                           ? SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT
                           : SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    gpu_vertex_shader = gpuCreateShader(SDL_GPU_SHADERSTAGE_VERTEX, 0);
    gpu_fragment_shader = gpuCreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 2);
    const auto white = gpuCreateTexture(1, 1, 1);
    gpu_white_texture = white.handle;
    const uint32_t pixel = 0xffffffffu;
    gpuUploadTexture(white, 0, {0, 0, 1, 1},
                     {reinterpret_cast<const unsigned char*>(&pixel), sizeof(pixel)},
                     sizeof(pixel));
    return gpu_vertex_shader != nullptr && gpu_fragment_shader != nullptr;
}

void GpuRenderer::gpuDeleteContext()
{
    if (gpu_device == 0) {
        return;
    }
    gpuCloseWindow();
    for (auto* texture : gpu_textures) SDL_ReleaseGPUTexture(gpu_device, texture);
    gpu_white_texture = nullptr;
    gpu_released.clear();
    gpu_uploads.clear();
    gpu_upload_pixels.clear();
    gpu_textures.clear();
    for (std::map<GpuPipelineKey, SDL_GPUGraphicsPipeline*>::iterator it = gpu_pipelines.begin();
         it != gpu_pipelines.end(); ++it) {
        SDL_ReleaseGPUGraphicsPipeline(gpu_device, it->second);
    }
    gpu_pipelines.clear();
    for (const auto& entry : gpu_samplers) {
        SDL_ReleaseGPUSampler(gpu_device, entry.second);
    }
    gpu_samplers.clear();
    if (gpu_vertex_buffer != 0) {
        SDL_ReleaseGPUBuffer(gpu_device, gpu_vertex_buffer);
    }
    if (gpu_index_buffer) SDL_ReleaseGPUBuffer(gpu_device, gpu_index_buffer);
    SDL_ReleaseGPUShader(gpu_device, gpu_vertex_shader);
    SDL_ReleaseGPUShader(gpu_device, gpu_fragment_shader);
    if (gpu_window != 0) {
        SDL_ReleaseWindowFromGPUDevice(gpu_device, gpu_window);
    }
    SDL_DestroyGPUDevice(gpu_device);
    gpu_device = 0;
    gpu_index_buffer = nullptr;
    gpu_index_buffer_size = 0;
    gpu_vertex_buffer = 0;
    gpu_vertex_buffer_size = 0;
}

SDL_GPUShader* GpuRenderer::gpuCreateShader(SDL_GPUShaderStage stage, uint32_t sampler_count)
{
    SDL_GPUShaderCreateInfo info = {};
    const bool vertex = stage == SDL_GPU_SHADERSTAGE_VERTEX;
    const auto formats = SDL_GetGPUShaderFormats(gpu_device);
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
    SDL_GPUShader* shader_object = SDL_CreateGPUShader(gpu_device, &info);
    if (shader_object == 0) {
        SDL_Log("SDL GPU: shader: %s", SDL_GetError());
    }
    return shader_object;
}

GpuRenderer::NativeTexture GpuRenderer::gpuCreateDeviceTexture(uint32_t texture_width,
                                                               uint32_t texture_height,
                                                               uint32_t levels)
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
    NativeTexture result = {SDL_CreateGPUTexture(gpu_device, &info),
                            texture_width * texture_height * 4, texture_width, texture_height,
                            levels};
    return result;
}

namespace {
template<class Vector>
Vector fetchVertex(const vertexStream::VertexArrayInfo& arrays, vertexStream::e_vertexArray slot,
                   uint32_t index, Vector fallback)
{
    if (((arrays.mask >> slot) & 1) == 0 && slot != vertexStream::VERTEX_ARRAY_POSITIONS)
        return fallback;
    if (arrays.arrays[slot] == nullptr) return fallback;
    const int components = SDL_min(static_cast<int>(arrays.components[slot]), Vector::length());
    const uint32_t stride =
        arrays.strides[slot] != 0 ? arrays.strides[slot] : arrays.components[slot] * sizeof(float);
    const auto* source = static_cast<const char*>(arrays.arrays[slot]) + size_t(index) * stride;
    std::memcpy(glm::value_ptr(fallback), source, components * sizeof(float));
    return fallback;
}
} // namespace

void GpuRenderer::gpuAppendVertex(const vertexStream::VertexArrayInfo& arrays, uint32_t index)
{
    using enum vertexStream::e_vertexArray;
    if (const auto found = gpu_vertex_remap.find(index); found != gpu_vertex_remap.end()) {
        gpu_indices.push_back(found->second);
        return;
    }
    const auto mapped = static_cast<uint32_t>(gpu_vertices.size());
    gpu_vertex_remap.emplace(index, mapped);
    gpu_indices.push_back(mapped);
    gpu_vertices.push_back(
        {fetchVertex(arrays, VERTEX_ARRAY_POSITIONS, index, glm::vec4(0, 0, 0, 1)),
         fetchVertex(arrays, VERTEX_ARRAY_DIFFUSE, index, glm::vec4(1)),
         fetchVertex(arrays, VERTEX_ARRAY_SPECULAR, index, glm::vec4(0)),
         fetchVertex(arrays, VERTEX_ARRAY_TEXCOORD0, index, glm::vec3(0, 0, 1)),
         fetchVertex(arrays, VERTEX_ARRAY_TEXCOORD1, index, glm::vec3(0, 0, 1)),
         fetchVertex(arrays, VERTEX_ARRAY_SPECULAR_ALPHA, index, glm::vec1(0)).x});
    if ((arrays.mask & (1u << VERTEX_ARRAY_TEXCOORD0)) && arrays.texture_weights[0])
        gpu_vertices.back().texcoord0.z = arrays.texture_weights[0][index];
    if ((arrays.mask & (1u << VERTEX_ARRAY_TEXCOORD1)) && arrays.texture_weights[1])
        gpu_vertices.back().texcoord1.z = arrays.texture_weights[1][index];
}

void GpuRenderer::gpuDraw(const DrawCall& call, const std::array<GpuStage, 2>& stages)
{
    const auto count =
        call.indices.empty() ? call.vertices.count : static_cast<uint32_t>(call.indices.size());
    if (count == 0) {
        return;
    }
    if (gpu_scissor.w <= 0 || gpu_scissor.h <= 0) return;
    gpu_vertex_remap.clear();
    const auto start = static_cast<uint32_t>(gpu_indices.size());
    const auto first_vertex = gpu_vertices.size();
    GpuPrimitiveClass primitive_class = PRIMITIVE_CLASS_TRIANGLES;
    const auto append = [&](uint32_t i) {
        gpuAppendVertex(call.vertices, call.indices.empty() ? i : call.indices[i]);
    };
    const auto primitive = call.primitive;
    switch (primitive) {
    case vertexStream::PRIMITIVE_TRIANGLES:
        for (uint32_t i = 0; i + 2 < count; i += 3) {
            append(i);
            append(i + 1);
            append(i + 2);
        }
        break;
    case vertexStream::PRIMITIVE_TRIANGLE_STRIP:
        for (uint32_t i = 0; i + 2 < count; ++i) {
            const bool odd = (i & 1) != 0;
            append(odd ? i + 1 : i);
            append(odd ? i : i + 1);
            append(i + 2);
        }
        break;
    case vertexStream::PRIMITIVE_TRIANGLE_FAN:
        for (uint32_t i = 1; i + 1 < count; ++i) {
            append(0);
            append(i);
            append(i + 1);
        }
        break;
    case vertexStream::PRIMITIVE_LINES:
        primitive_class = PRIMITIVE_CLASS_LINES;
        for (uint32_t i = 0; i + 1 < count; i += 2) {
            append(i);
            append(i + 1);
        }
        break;
    case vertexStream::PRIMITIVE_LINE_STRIP:
        primitive_class = PRIMITIVE_CLASS_LINES;
        for (uint32_t i = 0; i + 1 < count; ++i) {
            append(i);
            append(i + 1);
        }
        break;
    case vertexStream::PRIMITIVE_POINTS:
        primitive_class = PRIMITIVE_CLASS_POINTS;
        for (uint32_t i = 0; i < count; ++i) {
            append(i);
        }
        break;
    }
    if (gpu_indices.size() == start) {
        return;
    }
    const auto& shader = call.shader;
    if (gpu_trace) {
        const GpuVertex& v = gpu_vertices[first_vertex];
        const auto rows = glm::transpose(call.clip_transform);
        const float* m = glm::value_ptr(rows);
        SDL_Log("GpuRenderer draw prim=%d count=%u shader=%08x pos=(%g %g %g %g) "
                "proj=[%g %g %g %g | %g %g %g %g | %g %g %g %g | %g %g %g %g] "
                "viewport=(%g %g %g %g) scissor=(%d %d %d %d)",
                (int)primitive, count, shader.value, v.position[0], v.position[1], v.position[2],
                v.position[3], m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10],
                m[11], m[12], m[13], m[14], m[15], gpu_viewport.x, gpu_viewport.y, gpu_viewport.w,
                gpu_viewport.h, gpu_scissor.x, gpu_scissor.y, gpu_scissor.w, gpu_scissor.h);
        if (SDL_getenv("WIZ8_SRDD_VERTICES")) {
            for (size_t i = first_vertex; i < gpu_vertices.size(); ++i) {
                const GpuVertex& p = gpu_vertices[i];
                SDL_Log("GpuRenderer vertex %08x (%g %g %g %g) uv=(%g %g %g) rgba=(%g %g %g %g)",
                        shader.value, p.position[0], p.position[1], p.position[2], p.position[3],
                        p.texcoord0[0], p.texcoord0[1], p.texcoord0[2], p.diffuse[0], p.diffuse[1],
                        p.diffuse[2], p.diffuse[3]);
            }
        }
    }
    GpuDrawCommand command = {};
    command.kind = GpuDrawCommand::DRAW;
    command.pipeline_key = GpuPipelineKey(shader, primitive_class, call.polygon_offset);
    command.first_index = start;
    command.index_count = static_cast<uint32_t>(gpu_indices.size()) - start;
    const bool texturing = (shader.value & DrawShader::MASK_TEXTURING) != 0;
    for (int stage = 0; stage < 2; ++stage) {
        command.textures[stage] = texturing && stages[stage].texture != nullptr
                                      ? stages[stage].texture
                                      : gpu_white_texture;
        command.samplers[stage] = gpuSampler(stages[stage].parameters);
    }
    /* GLM and GLSL both store matrix columns. */
    command.vertex_uniforms.projection = call.clip_transform;
    GpuFragmentUniforms& fragment = command.fragment_uniforms;
    fragment.fog_color = call.fog_color;
    fragment.mode = glm::ivec4(
        texturing ? 1 : 0, (shader.value & DrawShader::MASK_GRADIENT) >> DrawShader::GRADIENT_SHIFT,
        (shader.value & DrawShader::MASK_SECONDARY_GRADIENT) != 0,
        (shader.value & DrawShader::MASK_FOG) >> DrawShader::FOG_SHIFT);
    fragment.alpha =
        glm::vec4((shader.value & DrawShader::MASK_ALPHATEST) != 0 ? 1.0f : 0.0f, 0, 0, 0);
    command.scissor = gpu_scissor;
    command.viewport = gpu_viewport;
    gpu_commands.push_back(command);
}

SDL_GPUSampler* GpuRenderer::gpuSampler(const GpuTexParms& gpu_parameters)
{
    if (const auto found = gpu_samplers.find(gpu_parameters); found != gpu_samplers.end())
        return found->second;
    SDL_GPUSamplerCreateInfo info{};
    info.mag_filter = gpu_parameters.mag;
    info.min_filter = gpu_parameters.min;
    info.mipmap_mode = gpu_parameters.mipmap;
    info.address_mode_u = gpu_parameters.wrap_s;
    info.address_mode_v = gpu_parameters.wrap_t;
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.mip_lod_bias = gpu_parameters.mipmap_bias;
    info.max_lod = gpu_parameters.max_lod;
    auto* created = SDL_CreateGPUSampler(gpu_device, &info);
    gpu_samplers.emplace(gpu_parameters, created);
    return created;
}

SDL_GPUGraphicsPipeline* GpuRenderer::gpuPipeline(const GpuPipelineKey& key)
{
    const uint32_t key_value = key.value;
    std::map<GpuPipelineKey, SDL_GPUGraphicsPipeline*>::iterator found = gpu_pipelines.find(key);
    if (found != gpu_pipelines.end()) {
        return found->second;
    }
    const uint32_t pass = key_value & DrawShader::PASS_MASK;
    const bool depth_write = (key_value & DrawShader::MASK_DEPTH_WRITE) != 0;
    const bool color_write = (key_value & DrawShader::MASK_COLOR_WRITE) != 0;
    const uint32_t destination = key_value >> DrawShader::DSTBLEND_SHIFT & 7;
    const uint32_t source = key_value >> DrawShader::SRCBLEND_SHIFT & 3;
    const uint32_t primitive = key_value >> 28 & 3;

    SDL_GPUVertexBufferDescription buffer = {};
    buffer.slot = 0;
    buffer.pitch = sizeof(GpuVertex);
    buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attributes[6] = {
        {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuVertex, position)},
        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuVertex, diffuse)},
        {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GpuVertex, specular)},
        {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuVertex, texcoord0)},
        {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GpuVertex, texcoord1)},
        {5, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, offsetof(GpuVertex, fog)},
    };
    SDL_GPUColorTargetDescription target = {};
    target.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    target.blend_state.enable_blend =
        !(source == DrawShader::SRCBLEND_ONE && destination == DrawShader::DSTBLEND_ZERO);
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
    info.vertex_shader = gpu_vertex_shader;
    info.fragment_shader = gpu_fragment_shader;
    info.vertex_input_state.vertex_buffer_descriptions = &buffer;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes;
    info.vertex_input_state.num_vertex_attributes = 6;
    info.primitive_type = primitive == PRIMITIVE_CLASS_LINES ? SDL_GPU_PRIMITIVETYPE_LINELIST
                          : primitive == PRIMITIVE_CLASS_POINTS
                              ? SDL_GPU_PRIMITIVETYPE_POINTLIST
                              : SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.rasterizer_state.enable_depth_clip = true;
    // Surrender's positive offset pulls overlays toward the camera.
    info.rasterizer_state.enable_depth_bias = key.offset != 0;
    info.rasterizer_state.depth_bias_constant_factor = -float(key.offset);
    info.rasterizer_state.depth_bias_slope_factor = -float(key.offset);
    info.depth_stencil_state.compare_op = compareOp(pass);
    info.depth_stencil_state.enable_depth_test = pass != DrawShader::PASS_ALWAYS || depth_write;
    info.depth_stencil_state.enable_depth_write = depth_write;
    if (key_value & GpuPipelineKey::STENCIL_REPLACE) {
        const SDL_GPUStencilOpState stencil{SDL_GPU_STENCILOP_REPLACE, SDL_GPU_STENCILOP_REPLACE,
                                            SDL_GPU_STENCILOP_REPLACE, SDL_GPU_COMPAREOP_ALWAYS};
        info.depth_stencil_state.front_stencil_state = stencil;
        info.depth_stencil_state.back_stencil_state = stencil;
        info.depth_stencil_state.enable_stencil_test = true;
        info.depth_stencil_state.write_mask = 0xff;
    }
    info.target_info.color_target_descriptions = &target;
    info.target_info.num_color_targets = 1;
    info.target_info.depth_stencil_format = gpu_depth_format;
    info.target_info.has_depth_stencil_target = true;
    SDL_GPUGraphicsPipeline* created = SDL_CreateGPUGraphicsPipeline(gpu_device, &info);
    if (created == 0) {
        SDL_Log("SDL GPU: pipeline %08x: %s", key_value, SDL_GetError());
    }
    gpu_pipelines[key] = created;
    return created;
}

SDL_GPUCommandBuffer* GpuRenderer::gpuCommandBuffer()
{
    if (gpu_command_buffer == 0) {
        gpu_command_buffer = SDL_AcquireGPUCommandBuffer(gpu_device);
    }
    return gpu_command_buffer;
}

void GpuRenderer::gpuUploadPending(SDL_GPUCommandBuffer* command)
{
    const size_t texture_bytes = gpu_upload_pixels.size() * sizeof(uint32_t);
    const size_t vertex_bytes = gpu_vertices.size() * sizeof(GpuVertex);
    const size_t index_bytes = gpu_indices.size() * sizeof(uint32_t);
    if (texture_bytes + vertex_bytes + index_bytes == 0) {
        return;
    }
    if (vertex_bytes > gpu_vertex_buffer_size) {
        if (gpu_vertex_buffer != 0) {
            SDL_ReleaseGPUBuffer(gpu_device, gpu_vertex_buffer);
        }
        gpu_vertex_buffer_size = SDL_max((uint32_t)vertex_bytes * 2, 1u << 20);
        SDL_GPUBufferCreateInfo buffer = {};
        buffer.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        buffer.size = gpu_vertex_buffer_size;
        gpu_vertex_buffer = SDL_CreateGPUBuffer(gpu_device, &buffer);
    }
    if (index_bytes > gpu_index_buffer_size) {
        if (gpu_index_buffer) SDL_ReleaseGPUBuffer(gpu_device, gpu_index_buffer);
        gpu_index_buffer_size = SDL_max(static_cast<uint32_t>(index_bytes) * 2, 1u << 18);
        const SDL_GPUBufferCreateInfo buffer{SDL_GPU_BUFFERUSAGE_INDEX, gpu_index_buffer_size, 0};
        gpu_index_buffer = SDL_CreateGPUBuffer(gpu_device, &buffer);
    }
    SDL_GPUTransferBufferCreateInfo transfer_info = {};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = (Uint32)(texture_bytes + vertex_bytes + index_bytes);
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(gpu_device, &transfer_info);
    char* mapped = static_cast<char*>(SDL_MapGPUTransferBuffer(gpu_device, transfer, false));
    size_t offset = 0;
    if (vertex_bytes != 0) {
        memcpy(mapped, gpu_vertices.data(), vertex_bytes);
    }
    if (index_bytes) memcpy(mapped + vertex_bytes, gpu_indices.data(), index_bytes);
    offset += vertex_bytes + index_bytes;
    if (texture_bytes) memcpy(mapped + offset, gpu_upload_pixels.data(), texture_bytes);
    SDL_UnmapGPUTransferBuffer(gpu_device, transfer);

    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
    if (vertex_bytes != 0) {
        SDL_GPUTransferBufferLocation source = {transfer, 0};
        SDL_GPUBufferRegion destination = {gpu_vertex_buffer, 0, (Uint32)vertex_bytes};
        SDL_UploadToGPUBuffer(copy, &source, &destination, true);
    }
    if (index_bytes) {
        const SDL_GPUTransferBufferLocation source{transfer, static_cast<Uint32>(vertex_bytes)};
        const SDL_GPUBufferRegion destination{gpu_index_buffer, 0,
                                              static_cast<Uint32>(index_bytes)};
        SDL_UploadToGPUBuffer(copy, &source, &destination, true);
    }
    offset = vertex_bytes + index_bytes;
    for (size_t index = 0; index < gpu_uploads.size(); ++index) {
        const GpuPendingUpload& gpuUpload = gpu_uploads[index];
        if (gpuUpload.texture != nullptr) {
            SDL_GPUTextureTransferInfo source = {
                transfer, static_cast<Uint32>(offset + gpuUpload.offset * sizeof(uint32_t)),
                gpuUpload.width, gpuUpload.height};
            SDL_GPUTextureRegion destination = {};
            destination.texture = gpuUpload.texture;
            destination.mip_level = gpuUpload.level;
            destination.x = gpuUpload.x;
            destination.y = gpuUpload.y;
            destination.w = gpuUpload.width;
            destination.h = gpuUpload.height;
            destination.d = 1;
            SDL_UploadToGPUTexture(copy, &source, &destination, false);
        }
    }
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(gpu_device, transfer);
    // Generation must follow the base-level copies and precede draws that sample them.
    for (const auto& gpuUpload : gpu_uploads)
        if (gpuUpload.generate_mipmaps)
            SDL_GenerateMipmapsForGPUTexture(command, gpuUpload.texture);
    gpu_uploads.clear();
    gpu_upload_pixels.clear();
}

void GpuRenderer::gpuReplay(SDL_GPUCommandBuffer* command)
{
    SDL_GPURenderPass* pass = 0;
    std::array<SDL_GPUTexture*, 2> bound{};
    for (size_t index = 0; index < gpu_commands.size(); ++index) {
        const GpuDrawCommand& entry = gpu_commands[index];
        if (entry.kind == GpuDrawCommand::CLEAR) {
            /* A clear starts a fresh pass whose load operations perform it. */
            if (pass != 0) {
                SDL_EndGPURenderPass(pass);
            }
            pass = gpuBeginPass(command, &entry);
            continue;
        }
        if (pass == 0) {
            pass = gpuBeginPass(command, 0);
        }
        SDL_GPUGraphicsPipeline* state = gpuPipeline(entry.pipeline_key);
        if (state == 0) {
            continue;
        }
        SDL_BindGPUGraphicsPipeline(pass, state);
        if (entry.pipeline_key.value & GpuPipelineKey::STENCIL_REPLACE)
            SDL_SetGPUStencilReference(pass, entry.clear_stencil);
        SDL_GPUViewport view = entry.viewport;
        if (view.w <= 0.0f || view.h <= 0.0f) {
            view.x = 0.0f;
            view.y = 0.0f;
            view.w = (float)gpu_width;
            view.h = (float)gpu_height;
            view.min_depth = 0.0f;
            view.max_depth = 1.0f;
        }
        SDL_SetGPUViewport(pass, &view);
        SDL_Rect clip = entry.scissor;
        SDL_SetGPUScissor(pass, &clip);
        SDL_GPUBufferBinding binding = {gpu_vertex_buffer, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
        SDL_GPUTextureSamplerBinding bindings[2];
        for (int stage = 0; stage < 2; ++stage) {
            bindings[stage].texture = entry.textures[stage];
            bindings[stage].sampler = entry.samplers[stage];
            if (std::exchange(bound[stage], entry.textures[stage]) != entry.textures[stage])
                ++statistics.texture_binds;
        }
        SDL_BindGPUFragmentSamplers(pass, 0, bindings, 2);
        SDL_PushGPUVertexUniformData(command, 0, &entry.vertex_uniforms,
                                     sizeof(entry.vertex_uniforms));
        SDL_PushGPUFragmentUniformData(command, 0, &entry.fragment_uniforms,
                                       sizeof(entry.fragment_uniforms));
        const SDL_GPUBufferBinding index_binding{gpu_index_buffer, 0};
        SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_DrawGPUIndexedPrimitives(pass, entry.index_count, 1, entry.first_index, 0, 0);
    }
    if (pass != 0) {
        SDL_EndGPURenderPass(pass);
    }
    gpu_commands.clear();
    gpu_vertices.clear();
    gpu_indices.clear();
    gpu_vertex_remap.clear();
}

SDL_GPURenderPass* GpuRenderer::gpuBeginPass(SDL_GPUCommandBuffer* command,
                                             const GpuDrawCommand* clear)
{
    SDL_GPUColorTargetInfo color = {};
    color.texture = gpu_color_target;
    color.load_op = SDL_GPU_LOADOP_LOAD;
    color.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth = {};
    depth.texture = gpu_depth_target;
    depth.load_op = SDL_GPU_LOADOP_LOAD;
    depth.store_op = SDL_GPU_STOREOP_STORE;
    depth.stencil_load_op = SDL_GPU_LOADOP_LOAD;
    depth.stencil_store_op = SDL_GPU_STOREOP_STORE;
    if (clear != 0) {
        if ((clear->clear_buffers & GPU_BUFFER_COLOR) != 0) {
            color.load_op = SDL_GPU_LOADOP_CLEAR;
            color.clear_color = clear->clear_color;
        }
        if ((clear->clear_buffers & GPU_BUFFER_DEPTH) != 0) {
            depth.load_op = SDL_GPU_LOADOP_CLEAR;
            depth.clear_depth = clear->clear_depth;
        }
        if ((clear->clear_buffers & GPU_BUFFER_STENCIL) != 0) {
            depth.stencil_load_op = SDL_GPU_LOADOP_CLEAR;
            depth.clear_stencil = clear->clear_stencil;
        }
    }
    return SDL_BeginGPURenderPass(command, &color, 1, &depth);
}

void GpuRenderer::gpuReleaseDeleted()
{
    for (auto* texture : gpu_released) {
        if (gpu_textures.erase(texture)) SDL_ReleaseGPUTexture(gpu_device, texture);
    }
    gpu_released.clear();
}

/* Records pending work; when presenting, blits the frame to the swapchain. */
void GpuRenderer::gpuFlush(bool present)
{
    if (gpu_device == 0 || gpu_color_target == 0) {
        return;
    }
    if (gpu_commands.empty() && gpu_uploads.empty() && !present && gpu_command_buffer == 0) {
        return;
    }
    SDL_GPUCommandBuffer* command = gpuCommandBuffer();
    gpuUploadPending(command);
    gpuReplay(command);
    if (present && gpu_window != 0) {
        SDL_GPUTexture* swapchain = 0;
        Uint32 swapchain_width = 0;
        Uint32 swapchain_height = 0;
        if (SDL_WaitAndAcquireGPUSwapchainTexture(command, gpu_window, &swapchain, &swapchain_width,
                                                  &swapchain_height) &&
            swapchain != 0) {
            SDL_GPUBlitInfo blit = {};
            blit.source.texture = gpu_color_target;
            blit.source.w = gpu_width;
            blit.source.h = gpu_height;
            blit.destination.texture = swapchain;
            blit.destination.w = swapchain_width;
            blit.destination.h = swapchain_height;
            blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
            blit.filter = SDL_GPU_FILTER_LINEAR;
            SDL_BlitGPUTexture(command, &blit);
        }
    }
    SDL_SubmitGPUCommandBuffer(command);
    gpu_command_buffer = 0;
    gpuReleaseDeleted();
}

/* Waits for the frame so far and copies the color target into readback. */
void GpuRenderer::gpuDownload()
{
    if (gpu_device == 0 || gpu_color_target == 0) {
        return;
    }
    SDL_GPUCommandBuffer* command = gpuCommandBuffer();
    gpuUploadPending(command);
    gpuReplay(command);
    SDL_GPUTransferBufferCreateInfo info = {};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    info.size = gpu_width * gpu_height * 4;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(gpu_device, &info);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
    SDL_GPUTextureRegion source = {};
    source.texture = gpu_color_target;
    source.w = gpu_width;
    source.h = gpu_height;
    source.d = 1;
    SDL_GPUTextureTransferInfo destination = {transfer, 0, gpu_width, gpu_height};
    SDL_DownloadFromGPUTexture(copy, &source, &destination);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence* fence_object = SDL_SubmitGPUCommandBufferAndAcquireFence(command);
    gpu_command_buffer = 0;
    SDL_WaitForGPUFences(gpu_device, true, &fence_object, 1);
    SDL_ReleaseGPUFence(gpu_device, fence_object);
    const void* mapped = SDL_MapGPUTransferBuffer(gpu_device, transfer, false);
    memcpy(gpu_readback->pixels, mapped, size_t(gpu_readback->pitch) * gpu_readback->h);
    SDL_UnmapGPUTransferBuffer(gpu_device, transfer);
    SDL_ReleaseGPUTransferBuffer(gpu_device, transfer);
    gpu_readback_dirty = 0;
    gpuReleaseDeleted();
}

void GpuRenderer::gpuUpload()
{
    SDL_GPUCommandBuffer* command = gpuCommandBuffer();
    SDL_GPUTransferBufferCreateInfo info = {};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    info.size = gpu_width * gpu_height * 4;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(gpu_device, &info);
    memcpy(SDL_MapGPUTransferBuffer(gpu_device, transfer, false), gpu_readback->pixels,
           size_t(gpu_readback->pitch) * gpu_readback->h);
    SDL_UnmapGPUTransferBuffer(gpu_device, transfer);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
    SDL_GPUTextureTransferInfo source = {transfer, 0, gpu_width, gpu_height};
    SDL_GPUTextureRegion destination = {};
    destination.texture = gpu_color_target;
    destination.w = gpu_width;
    destination.h = gpu_height;
    destination.d = 1;
    SDL_UploadToGPUTexture(copy, &source, &destination, false);
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(gpu_device, transfer);
    gpu_readback_dirty = 0;
}

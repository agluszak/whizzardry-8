#include "gpu_canvas.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

#include <SDL3/SDL.h>

#include "shade_frag.spv.h"
#include "sprite_frag.spv.h"
#include "wiz8/canvas/shading.h"

namespace {

void check(bool ok)
{
    if (!ok) throw std::runtime_error(SDL_GetError());
}

template<class T> T* check(T* pointer)
{
    if (!pointer) throw std::runtime_error(SDL_GetError());
    return pointer;
}

// The exact 8-bit expansion SDL uses for ARGB1555, so conversion back is lossless.
std::array<std::uint8_t, 4> expand(std::uint16_t color)
{
    static const auto* format = SDL_GetPixelFormatDetails(kFramePixelFormat);
    std::array<std::uint8_t, 4> rgba{};
    SDL_GetRGBA(color, format, nullptr, &rgba[0], &rgba[1], &rgba[2], &rgba[3]);
    return rgba;
}

std::array<float, 4> expandFloat(std::uint16_t color)
{
    const auto rgba = expand(color);
    return {rgba[0] / 255.f, rgba[1] / 255.f, rgba[2] / 255.f, rgba[3] / 255.f};
}

struct SpriteUniforms {
    std::array<float, 4> foreground;
    std::array<float, 4> background;
    std::array<float, 4> shadow;
    std::array<std::int32_t, 4> mode;
};

SDL_Texture* targetTexture(SDL_Renderer* renderer)
{
    auto* texture =
        check(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET,
                                GpuCanvas::kWidth, GpuCanvas::kHeight));
    check(SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST));
    check(SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE));
    return texture;
}

} // namespace

GpuCanvas::GpuCanvas(SDL_GPUDevice* device) : device(device)
{
    renderer = check(SDL_CreateGPURenderer(device, nullptr));
    canvas = targetTexture(renderer);
    scratch = targetTexture(renderer);
    SDL_GPUSamplerCreateInfo sampler{};
    sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w =
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    nearest = check(SDL_CreateGPUSampler(device, &sampler));
    sprite_shader = fragmentShader(sprite_frag, 1);
    shade_shader = fragmentShader(shade_frag, 0);

    std::vector<std::uint8_t> table(65536 * 4);
    for (std::size_t index = 0; index < ShadeTable.size(); ++index)
        std::ranges::copy(expand(ShadeTable[index]), table.begin() + index * 4);
    shade_table = uploadRgba(256, 256, table);
    shade_state = renderState(shade_shader, shade_table);

    check(SDL_SetRenderTarget(renderer, canvas));
    check(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE));
    check(SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0));
    check(SDL_RenderClear(renderer));
}

GpuCanvas::~GpuCanvas()
{
    for (auto& [key, state] : sprite_states) SDL_DestroyGPURenderState(state);
    SDL_DestroyGPURenderState(shade_state);
    for (auto& [frame, texture] : frames) SDL_DestroyTexture(texture);
    SDL_DestroyTexture(scratch);
    SDL_DestroyTexture(canvas);
    SDL_DestroyRenderer(renderer);
    for (auto& [colors, texture] : palettes) SDL_ReleaseGPUTexture(device, texture);
    SDL_ReleaseGPUTexture(device, shade_table);
    SDL_ReleaseGPUShader(device, sprite_shader);
    SDL_ReleaseGPUShader(device, shade_shader);
    SDL_ReleaseGPUSampler(device, nearest);
}

void GpuCanvas::setClip(const SDL_Rect* clip)
{
    check(SDL_SetRenderClipRect(renderer, clip));
}

void GpuCanvas::fill(const SDL_Rect& area, std::uint16_t color)
{
    const auto rgba = expand(color);
    check(SDL_SetRenderDrawColor(renderer, rgba[0], rgba[1], rgba[2], rgba[3]));
    SDL_FRect rect;
    SDL_RectToFRect(&area, &rect);
    check(SDL_RenderFillRect(renderer, &rect));
}

void GpuCanvas::drawSprite(const SpriteSheet& sprites, std::size_t frame_index, int x, int y,
                           SpriteDraw options)
{
    if (frame_index >= sprites.frames.size()) return;
    const auto& frame = sprites.frames[frame_index];
    if (frame.indices.empty()) return;
    const SDL_FRect destination{float(options.mirror ? x - frame.offset.x : x + frame.offset.x),
                                float(y + frame.offset.y), float(frame.width), float(frame.height)};
    const auto& palette = options.palette ? *options.palette : sprites.colors;
    check(SDL_SetGPURenderState(renderer, spriteState(palette, options)));
    check(SDL_RenderTextureRotated(renderer, frameTexture(sprites, frame_index), nullptr,
                                   &destination, 0, nullptr,
                                   options.mirror ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE));
    check(SDL_SetGPURenderState(renderer, nullptr));
}

void GpuCanvas::shadowRect(const SDL_Rect& area)
{
    // The shader reads the destination, so it samples a copy of the region.
    SDL_FRect rect;
    SDL_RectToFRect(&area, &rect);
    SDL_Rect clip{};
    const bool clipped = SDL_RenderClipEnabled(renderer);
    if (clipped) check(SDL_GetRenderClipRect(renderer, &clip));
    check(SDL_SetRenderTarget(renderer, scratch));
    check(SDL_RenderTexture(renderer, canvas, &rect, &rect));
    check(SDL_SetRenderTarget(renderer, canvas));
    if (clipped) check(SDL_SetRenderClipRect(renderer, &clip));
    check(SDL_SetGPURenderState(renderer, shade_state));
    check(SDL_RenderTexture(renderer, scratch, &rect, &rect));
    check(SDL_SetGPURenderState(renderer, nullptr));
}

void GpuCanvas::flush()
{
    check(SDL_RenderPresent(renderer));
}

SDL_GPUTexture* GpuCanvas::gpuTexture() const
{
    return static_cast<SDL_GPUTexture*>(SDL_GetPointerProperty(
        SDL_GetTextureProperties(canvas), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
}

std::vector<std::uint16_t> GpuCanvas::readArgb1555()
{
    auto* rgba = check(SDL_RenderReadPixels(renderer, nullptr));
    auto* packed = check(SDL_ConvertSurface(rgba, kFramePixelFormat));
    SDL_DestroySurface(rgba);
    std::vector<std::uint16_t> pixels(static_cast<std::size_t>(kWidth) * kHeight);
    for (int y = 0; y < kHeight; ++y)
        std::copy_n(reinterpret_cast<const std::uint16_t*>(
                        static_cast<const std::uint8_t*>(packed->pixels) + y * packed->pitch),
                    kWidth, pixels.begin() + y * kWidth);
    SDL_DestroySurface(packed);
    return pixels;
}

SDL_Texture* GpuCanvas::frameTexture(const SpriteSheet& sprites, std::size_t frame_index)
{
    const auto& frame = sprites.frames[frame_index];
    auto& texture = frames[&frame];
    if (texture) return texture;
    // R: palette index; G: 0 only for the transparent-run sentinel.
    std::vector<std::uint8_t> texels(frame.indices.size() * 4);
    for (std::size_t pixel = 0; pixel < frame.indices.size(); ++pixel) {
        const auto index = frame.indices[pixel];
        const bool run = index != SpriteFrame::kTransparent;
        texels[pixel * 4] = run ? static_cast<std::uint8_t>(index) : 0;
        texels[pixel * 4 + 1] = run ? 255 : 0;
        texels[pixel * 4 + 3] = 255;
    }
    texture = check(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                      frame.width, frame.height));
    check(SDL_UpdateTexture(texture, nullptr, texels.data(), frame.width * 4));
    check(SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST));
    check(SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE));
    return texture;
}

SDL_GPUTexture* GpuCanvas::paletteTexture(const Palette16& palette)
{
    auto& texture = palettes[std::vector<std::uint16_t>(palette.begin(), palette.end())];
    if (texture) return texture;
    std::vector<std::uint8_t> rgba(256 * 4);
    for (std::size_t index = 0; index < palette.size(); ++index)
        std::ranges::copy(expand(palette[index]), rgba.begin() + index * 4);
    texture = uploadRgba(256, 1, rgba);
    return texture;
}

SDL_GPURenderState* GpuCanvas::spriteState(const Palette16& palette, const SpriteDraw& options)
{
    const bool mono = options.blend == SpriteBlend::Mono;
    std::vector<std::uint16_t> key{mono, options.foreground, options.background, options.shadow};
    if (!mono) key.insert(key.end(), palette.begin(), palette.end());
    auto& state = sprite_states[key];
    if (state) return state;
    state = renderState(sprite_shader, paletteTexture(palette));
    const SpriteUniforms uniforms{expandFloat(options.foreground),
                                  expandFloat(options.background),
                                  expandFloat(options.shadow),
                                  {mono, options.background != 0, options.shadow != 0, 0}};
    check(SDL_SetGPURenderStateFragmentUniforms(state, 0, &uniforms, sizeof(uniforms)));
    return state;
}

SDL_GPUTexture* GpuCanvas::uploadRgba(int width, int height, std::span<const std::uint8_t> rgba)
{
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = static_cast<Uint32>(width);
    info.height = static_cast<Uint32>(height);
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    auto* texture = check(SDL_CreateGPUTexture(device, &info));
    const SDL_GPUTransferBufferCreateInfo transfer_info{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                                                        static_cast<Uint32>(rgba.size()), 0};
    auto* transfer = check(SDL_CreateGPUTransferBuffer(device, &transfer_info));
    std::ranges::copy(
        rgba, static_cast<std::uint8_t*>(check(SDL_MapGPUTransferBuffer(device, transfer, false))));
    SDL_UnmapGPUTransferBuffer(device, transfer);
    auto* commands = check(SDL_AcquireGPUCommandBuffer(device));
    auto* copy = SDL_BeginGPUCopyPass(commands);
    const SDL_GPUTextureTransferInfo source{transfer, 0, 0, 0};
    SDL_GPUTextureRegion region{};
    region.texture = texture;
    region.w = info.width;
    region.h = info.height;
    region.d = 1;
    SDL_UploadToGPUTexture(copy, &source, &region, false);
    SDL_EndGPUCopyPass(copy);
    check(SDL_SubmitGPUCommandBuffer(commands));
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return texture;
}

SDL_GPUShader* GpuCanvas::fragmentShader(std::span<const std::uint32_t> code,
                                         Uint32 uniform_buffers)
{
    SDL_GPUShaderCreateInfo info{};
    info.code = reinterpret_cast<const Uint8*>(code.data());
    info.code_size = code.size_bytes();
    info.entrypoint = "main";
    info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_samplers = 2;
    info.num_uniform_buffers = uniform_buffers;
    return check(SDL_CreateGPUShader(device, &info));
}

SDL_GPURenderState* GpuCanvas::renderState(SDL_GPUShader* shader, SDL_GPUTexture* table)
{
    const SDL_GPUTextureSamplerBinding binding{table, nearest};
    SDL_GPURenderStateCreateInfo info{};
    info.fragment_shader = shader;
    info.num_sampler_bindings = 1;
    info.sampler_bindings = &binding;
    return check(SDL_CreateGPURenderState(renderer, &info));
}

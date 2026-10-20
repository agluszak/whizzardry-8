#pragma once

// The 640x480 UI canvas drawn by SDL 3.4's GPU renderer on the game's GPU
// device, instead of CPU pixel loops into an ARGB1555 surface that is then
// uploaded.
//
// SDL owns quads, batching, clipping, render targets and textures. Two small
// fragment shaders keep the canvas rules that SDL's built-in blending cannot
// express:
//  - STI sprites: palette indices always write (index 0 included) while the
//    transparent-run sentinel leaves the destination untouched; mono glyphs use
//    the foreground/background/shadow rules of DrawSprite.
//  - ShadowRect: a lookup through ShadeTable, which leaves transparent canvas
//    pixels transparent. A translucent black quad would darken the world view.
// The target stores exact 8-bit expansions of ARGB1555 values, so it converts
// back to the CPU canvas format without loss.

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <vector>

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_render.h>

#include "wiz8/canvas/himage.h"
#include "wiz8/canvas/sprite.h"

class GpuCanvas {
public:
    static constexpr int kWidth = 640;
    static constexpr int kHeight = 480;

    // `device` is the device the 3D renderer presents with; the canvas texture
    // can be sampled there directly (gpuTexture()).
    explicit GpuCanvas(SDL_GPUDevice* device);
    ~GpuCanvas();
    GpuCanvas(const GpuCanvas&) = delete;
    GpuCanvas& operator=(const GpuCanvas&) = delete;

    void setClip(const SDL_Rect* clip);
    void fill(const SDL_Rect& area, std::uint16_t color);
    void drawSprite(const SpriteSheet& sprites, std::size_t frame, int x, int y,
                    SpriteDraw options = {});
    void shadowRect(const SDL_Rect& area);
    // Submits the queued drawing; afterwards gpuTexture() holds the result.
    void flush();

    SDL_GPUTexture* gpuTexture() const;
    // The canvas converted to the CPU canvas pixel format.
    std::vector<std::uint16_t> readArgb1555();

private:
    SDL_Texture* frameTexture(const SpriteSheet& sprites, std::size_t frame);
    SDL_GPUTexture* paletteTexture(const Palette16& palette);
    SDL_GPUTexture* uploadRgba(int width, int height, std::span<const std::uint8_t> rgba);
    SDL_GPUShader* fragmentShader(std::span<const std::uint32_t> code, Uint32 uniform_buffers);
    SDL_GPURenderState* renderState(SDL_GPUShader* shader, SDL_GPUTexture* table);
    SDL_GPURenderState* spriteState(const Palette16& palette, const SpriteDraw& options);

    SDL_GPUDevice* device;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* canvas = nullptr;
    SDL_Texture* scratch = nullptr;
    SDL_GPUSampler* nearest = nullptr;
    SDL_GPUShader* sprite_shader = nullptr;
    SDL_GPUShader* shade_shader = nullptr;
    SDL_GPUTexture* shade_table = nullptr;
    SDL_GPURenderState* shade_state = nullptr;
    std::map<const SpriteFrame*, SDL_Texture*> frames;
    std::map<std::vector<std::uint16_t>, SDL_GPUTexture*> palettes;
    // Uniforms belong to a render state and are read when the batch executes,
    // so every distinct palette and mono colour set gets its own state.
    std::map<std::vector<std::uint16_t>, SDL_GPURenderState*> sprite_states;
};

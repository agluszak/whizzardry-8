#include "wiz8/render/texture.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <utility>

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>

#include "compat/native.h"
#include "wiz8/filesystem.h"
#include "wiz8/platform/random.h"
#include "wiz8/render/gpu_renderer.h"
#include "wiz8/render/image_io.h"
#include "wiz8/texture_animation.h"

Texture::Texture(std::shared_ptr<SDL_Surface> pixels) : surface(std::move(pixels))
{
    texture_dimensions_.width = 0x40;
    texture_dimensions_.height = 0x40;
    texture_dimensions_.format = SDL_PIXELFORMAT_ARGB4444;
    texture_flags_ = 0;
    texture_flags_ |= 1 << FLAG_DIRTY_DEFAULTS;
}

Texture::~Texture()
{
    invalidate();
}

Texture& Texture::operator=(const Texture& other)
{
    if (this != &other) {
        invalidate();
        name = other.name;
        source_path = other.source_path;
        playback = other.playback;
        parameters = other.parameters;
        texture_dimensions_ = other.texture_dimensions_;
        texture_flags_ = other.texture_flags_;
        surface = other.surface;
    }
    return *this;
}

void Texture::setDimensions(const Dimensions& dimensions)
{
    texture_dimensions_ = dimensions;
    texture_flags_ &= ~(1 << FLAG_DIRTY_DEFAULTS);
}

void Texture::setupDefaultValuesFromSurface(SDL_Surface* surface)
{
    if (surface != 0) {
        texture_dimensions_.width = surface->w;
        texture_dimensions_.height = surface->h;
        texture_dimensions_.format = surface->format;
    } else {
        texture_dimensions_.width = 1;
        texture_dimensions_.height = 1;
    }
}

void Texture::setMipmapBias(float bias)
{
    parameters.mipmap_bias = bias;
}

void Texture::setMagFilter(e_filter value)
{
    parameters.mag = value;
}

void Texture::setMinFilter(e_filter value)
{
    parameters.min = value;
}

void Texture::setMipmap(e_mipmap value)
{
    parameters.mipmap = value;
}

void Texture::setWrapS(e_wrap value)
{
    parameters.wrap_s = value;
}

void Texture::setWrapT(e_wrap value)
{
    parameters.wrap_t = value;
}

Texture* Texture::resolve()
{
    if (playback) {
        UpdateFrame();
        return playback->textures.empty() ? nullptr
                                          : playback->textures.at(playback->frame)->resolve();
    }
    if (!source_path.empty() && !surface &&
        !(texture_flags_ & (1u << FLAG_GENERATESURFACE_FAILURE)))
        loadSurface();
    return source_path.empty() || surface ? this : nullptr;
}

Texture::Dimensions Texture::getDimensions()
{
    if ((texture_flags_ & (1 << FLAG_DIRTY_DEFAULTS)) != 0) {
        setupDefaultValues();
    }
    texture_dimensions_.generate_mipmaps = parameters.mipmap != MIPMAP_NONE;
    return texture_dimensions_;
}

Texture::Parameters Texture::getParameters()
{
    return parameters;
}

void Texture::invalidate()
{
    if (!source_path.empty()) {
        surface.reset();
        texture_flags_ |= 1u << FLAG_DIRTY_DEFAULTS;
        texture_flags_ &= ~(1u << FLAG_GENERATESURFACE_FAILURE);
    }
    if (auto* renderer = GpuRenderer::getActive()) renderer->invalidateTexture(this);
}

void Texture::setSurfacePtr(std::shared_ptr<SDL_Surface> pixels)
{
    if (pixels == surface) return;
    invalidate();
    surface = std::move(pixels);
    setupDefaultValues();
}

void Texture::setupDefaultValues()
{
    if (playback && !playback->textures.empty()) {
        texture_dimensions_ = playback->textures.front()->getDimensions();
    } else {
        if (!source_path.empty() && !surface) loadSurface();
        setupDefaultValuesFromSurface(surface.get());
    }
    texture_flags_ &= ~(1u << FLAG_DIRTY_DEFAULTS);
}

Texture::Texture(TexturePlayback playback) : Texture()
{
    this->playback = std::move(playback);
    this->playback->frame_tick = w8_get_ticks();
}
Texture::Texture(std::filesystem::path path, bool preload) : Texture()
{
    source_path = std::move(path);
    name = wiz8::path_to_utf8(source_path);
    if (preload) setupDefaultValues();
}
Texture::Texture(const Texture& other) : Texture()
{
    *this = other;
    if (playback) {
        playback->frame = 0;
        playback->direction = 1;
        playback->frame_tick = w8_get_ticks();
    }
}
void Texture::releaseSurface()
{
    surface.reset();
    texture_flags_ |= 1u << FLAG_DIRTY_DEFAULTS;
}
void Texture::loadSurface()
{
    if (source_path.empty()) return;
    try {
        auto file = wiz8::open_file(source_path);
        surface = imageIO::loadTga(*file);
    } catch (const std::exception&) {
        surface.reset();
    }
    if (!surface) texture_flags_ |= 1u << FLAG_GENERATESURFACE_FAILURE;
    setupDefaultValuesFromSurface(surface.get());
    texture_flags_ &= ~(1u << FLAG_DIRTY_DEFAULTS);
}
bool Texture::hasAlpha()
{
    auto* texture = resolve();
    return texture && texture->surface && SDL_ISPIXELFORMAT_ALPHA(texture->surface->format);
}
unsigned char Texture::Prepare()
{
    if (playback && !playback->textures.empty()) return playback->textures.front()->hasAlpha();
    return hasAlpha();
}
void Texture::SetFrame(int frame)
{
    if (!playback) return;
    playback->frame = frame;
    playback->frame_tick = w8_get_ticks();
}
void Texture::AddTexture(std::shared_ptr<Texture> texture)
{
    playback->textures.push_back(std::move(texture));
    texture_flags_ |= 1u << FLAG_DIRTY_DEFAULTS;
}
void Texture::UpdateFrame()
{
    if (!playback) return;
    auto& state = *playback;
    const int count = static_cast<int>(state.textures.size());

    if (count == 0) return;
    if (state.animation_mode == W8_TEXTURE_ANIM_MANUAL) {
        return;
    }

    if (state.trigger_mode == W8_TEXTURE_TRIGGER_RANDOM_FRAME) {
        if (Random15() / static_cast<float>(0x7fff) < state.probability) {
            state.frame = std::min(
                count - 1, static_cast<int>(Random15() / static_cast<float>(0x7fff) * count));
        }
        return;
    }

    if (state.trigger_mode == W8_TEXTURE_TRIGGER_RANDOM_START) {
        if (!state.running && Random15() / static_cast<float>(0x7fff) < state.probability) {
            state.running = true;
            state.direction = 0;
            state.frame = 0;
            state.frame_tick = w8_get_ticks();
        }
        if (!state.running) {
            return;
        }
    }
    if (count == 0) {
        return;
    }

    const int elapsed_frames =
        static_cast<int>((w8_get_ticks() - state.frame_tick) * state.frame_rate * 0.001f);
    if (state.animation_mode == W8_TEXTURE_ANIM_LOOP) {
        int next = (state.direction * elapsed_frames) % count;
        if (next < state.frame) {
            state.frame = 0;
            state.running = false;
            return;
        }
        state.frame = next;
    } else if (state.animation_mode == W8_TEXTURE_ANIM_PING_PONG) {
        if ((elapsed_frames / count & 1) != 0) {
            state.direction = -1;
            state.frame = count - elapsed_frames % count - 1;
        } else {
            if (state.direction == -1) {
                state.running = false;
                return;
            }
            state.direction = 1;
            state.frame = elapsed_frames % count;
        }
    } else if (state.animation_mode == W8_TEXTURE_ANIM_PLAY_ONCE) {
        if (elapsed_frames >= count) {
            state.running = false;
            state.frame = count - 1;
        } else {
            state.frame = elapsed_frames;
        }
    }
}

int Texture::IsFinished() const
{
    return playback && playback->animation_mode == W8_TEXTURE_ANIM_PLAY_ONCE &&
           playback->frame == std::max(static_cast<int>(playback->textures.size()) - 1, 0);
}

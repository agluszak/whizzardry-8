#include "wiz8/render/texture.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <utility>

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <vsg/core/Array2D.h>
#include <vsg/core/Data.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/maths/vec4.h>
#include <vsg/state/Image.h>
#include <vsg/state/ImageInfo.h>
#include <vsg/state/ImageView.h>
#include <vsg/state/Sampler.h>
#include <vulkan/vulkan_core.h>

#include "wiz8/filesystem.h"
#include "wiz8/platform/random.h"
#include "wiz8/render/image_io.h"
#include "wiz8/surface_storage.h"
#include "wiz8/texture_animation.h"

namespace {
void copyRgbaPixels(vsg::ubvec4Array2D& destination, const SDL_Surface& source)
{
    for (std::uint32_t y = 0; y < destination.height(); ++y) {
        const auto* row =
            static_cast<const std::uint8_t*>(source.pixels) + std::size_t(y) * source.pitch;
        for (std::uint32_t x = 0; x < destination.width(); ++x) {
            const auto* pixel = row + std::size_t(x) * 4;
            destination.at(x, y) = vsg::ubvec4(pixel[0], pixel[1], pixel[2], pixel[3]);
        }
    }
}
}

Texture::Parameters Texture::defaults{
    .mag = FILTER_GOOD, .min = FILTER_GOOD, .mipmap = MIPMAP_FASTEST};

Texture::Texture(std::shared_ptr<SDL_Surface> pixels) : surface(std::move(pixels))
{
    texture_dimensions_.width = 0x40;
    texture_dimensions_.height = 0x40;
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
        return playback->textures.empty() ? nullptr
                                          : playback->textures.at(playback->frame)->resolve();
    }
    if (!source_path.empty() && !surface &&
        !(texture_flags_ & (1u << FLAG_GENERATESURFACE_FAILURE)))
        loadSurface();
    return source_path.empty() || surface ? this : nullptr;
}

Texture* Texture::currentFrame()
{
    if (playback)
        return playback->textures.empty() ? nullptr
                                          : playback->textures.at(playback->frame)->currentFrame();
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
    image_info = {};
    image_reduction = -1;
}

vsg::ref_ptr<vsg::ImageInfo> Texture::imageInfo(int reduction)
{
    auto* current = resolve();
    if (!current) return {};
    if (current != this) return current->imageInfo(reduction);
    auto effective = parameters;
    if (effective.mag == FILTER_DEFAULT) effective.mag = defaults.mag;
    if (effective.min == FILTER_DEFAULT) effective.min = defaults.min;
    if (effective.mipmap == MIPMAP_DEFAULT) effective.mipmap = defaults.mipmap;
    const auto dimensions = getDimensions();
    reduction = dimensions.native_size ? 0 : std::clamp(reduction, 0, 16);
    const auto width = std::max(1u, dimensions.width >> reduction);
    const auto height = std::max(1u, dimensions.height >> reduction);
    if (image_info && reduction == image_reduction && effective == image_parameters &&
        image_info->imageView->image->extent.width == width &&
        image_info->imageView->image->extent.height == height)
        return image_info;
    if (!surface) return {};
    auto rgba =
        CreateCpuStorage(static_cast<int>(width), static_cast<int>(height), SDL_PIXELFORMAT_RGBA32);
    StretchCpuSurface(*surface, *rgba, SDL_SCALEMODE_LINEAR);
    auto pixels =
        vsg::ubvec4Array2D::create(width, height, vsg::Data::Properties{VK_FORMAT_R8G8B8A8_UNORM});
    copyRgbaPixels(*pixels, *rgba);
    if (dimensions.native_size) pixels->properties.dataVariance = vsg::DYNAMIC_DATA;
    auto sampler = vsg::Sampler::create();
    const auto filter = [](e_filter value) {
        return value == FILTER_NONE || value == FILTER_FASTEST ? VK_FILTER_NEAREST
                                                               : VK_FILTER_LINEAR;
    };
    sampler->magFilter = filter(effective.mag);
    sampler->minFilter = filter(effective.min);
    sampler->mipmapMode = effective.mipmap == MIPMAP_BEST ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                                          : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler->maxLod = effective.mipmap == MIPMAP_NONE || dimensions.native_size ? 0.0f : 16.0f;
    sampler->mipLodBias = effective.mipmap_bias;
    sampler->addressModeU = effective.wrap_s == WRAP_CLAMP ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                                                           : VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler->addressModeV = effective.wrap_t == WRAP_CLAMP ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                                                           : VK_SAMPLER_ADDRESS_MODE_REPEAT;
    image_info = vsg::ImageInfo::create(sampler, pixels);
    image_info->computeNumMipMapLevels();
    image_parameters = effective;
    image_reduction = reduction;
    return image_info;
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
        playback->elapsed = {};
        playback->last_update.reset();
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
    auto* texture = currentFrame();
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
    playback->elapsed = {};
}
void Texture::AddTexture(std::shared_ptr<Texture> texture)
{
    playback->textures.push_back(std::move(texture));
    texture_flags_ |= 1u << FLAG_DIRTY_DEFAULTS;
}
void Texture::setAnimationClock(TextureClock clock)
{
    if (!playback || playback->clock == clock) return;
    playback->clock = clock;
    playback->last_update.reset();
    for (const auto& frame : playback->textures)
        if (frame) frame->setAnimationClock(clock);
}

void Texture::UpdateFrame(std::chrono::milliseconds now, TextureClock clock)
{
    if (!playback || playback->clock != clock) return;
    auto& state = *playback;
    if (state.last_update == now) return;
    if (state.last_update && now >= *state.last_update) state.elapsed += now - *state.last_update;
    state.last_update = now;
    const auto advance = [&] {
        const int count = static_cast<int>(state.textures.size());

        if (count == 0) return;
        if (state.animation_mode == W8_TEXTURE_ANIM_MANUAL) {
            return;
        }

        if (state.trigger_mode == W8_TEXTURE_TRIGGER_RANDOM_FRAME) {
            if (PresentationRng().next() / static_cast<float>(0x7fff) < state.probability) {
                state.frame =
                    std::min(count - 1, static_cast<int>(PresentationRng().next() /
                                                         static_cast<float>(0x7fff) * count));
            }
            return;
        }

        if (state.trigger_mode == W8_TEXTURE_TRIGGER_RANDOM_START) {
            if (!state.running &&
                PresentationRng().next() / static_cast<float>(0x7fff) < state.probability) {
                state.running = true;
                state.direction = 0;
                state.frame = 0;
                state.elapsed = {};
            }
            if (!state.running) {
                return;
            }
        }
        if (count == 0) {
            return;
        }

        const int elapsed_frames =
            static_cast<int>(state.elapsed.count() * state.frame_rate * 0.001f);
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
    };
    advance();
    if (!state.textures.empty())
        if (const auto& frame = state.textures.at(state.frame)) frame->UpdateFrame(now, clock);
}

int Texture::IsFinished() const
{
    return playback && playback->animation_mode == W8_TEXTURE_ANIM_PLAY_ONCE &&
           playback->frame == std::max(static_cast<int>(playback->textures.size()) - 1, 0);
}

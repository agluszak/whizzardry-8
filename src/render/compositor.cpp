#include "wiz8/render/compositor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <vsg/core/Array2D.h>
#include <vsg/core/Data.h>
#include <vsg/state/ImageInfo.h>
#include <vsg/state/Sampler.h>
#include <vulkan/vulkan_core.h>

#include "wiz8/platform/timer.h"
#include "wiz8/render/renderer.h"
#include "wiz8/render/texture.h"

OverlayQuad::OverlayQuad(const OverlayQuad& other)
{
    *this = other;
}
OverlayQuad& OverlayQuad::operator=(const OverlayQuad& other)
{
    if (this == &other) return *this;
    texture = other.texture;
    image = other.image;
    position = other.position;
    size = other.size;
    scale = other.scale;
    uv_min = other.uv_min;
    uv_max = other.uv_max;
    color = other.color;
    rotation = other.rotation;
    visible = other.visible;
    layer = other.layer;
    glow_enabled = other.glow_enabled;
    glow_colors = other.glow_colors;
    glow_period = other.glow_period;
    shader = other.shader;
    return *this;
}
OverlayQuad::~OverlayQuad()
{
    detach();
}
void OverlayQuad::detach()
{
    if (compositor) compositor->detach(*this);
}
Compositor::~Compositor()
{
    clear();
}
void Compositor::attach(OverlayQuad& quad, OverlayLayer layer)
{
    quad.detach();
    quad.compositor = this;
    quad.layer = layer;
    widgets.push_back(&quad);
}
void Compositor::detach(OverlayQuad& quad)
{
    std::erase(widgets, &quad);
    quad.compositor = nullptr;
}
void Compositor::clear()
{
    for (auto* quad : widgets) quad->compositor = nullptr;
    widgets.clear();
    frame_quads.clear();
}
void Compositor::draw(Renderer& renderer, OverlayLayer layer)
{
    frame_quads.clear();
    for (const auto* quad : widgets)
        if (quad->visible && quad->layer == layer) frame_quads.push_back(*quad);
    for (auto& quad : frame_quads) {
        if (quad.glow_enabled) {
            const auto period = std::max(quad.glow_period, 1u);
            const float weight = std::abs(
                std::sin((MonotonicMilliseconds() % period) * 6.283185307179586f / period));
            const float alpha = quad.color.a;
            quad.color = quad.glow_colors[0] * (1 - weight) + quad.glow_colors[1] * weight;
            quad.color.a = alpha;
        }
        renderer.drawOverlayQuad(quad);
    }
}

UiCanvasTexture::UiCanvasTexture(std::shared_ptr<SDL_Surface> surface)
    : source(std::move(surface)),
      pixels(vsg::ubvec4Array2D::create(source->w, source->h,
                                        vsg::Data::Properties{VK_FORMAT_R8G8B8A8_UNORM}))
{
    if (SDL_ISPIXELFORMAT_INDEXED(source->format))
        throw std::invalid_argument("UI canvases require a non-indexed pixel format");
    pixels->properties.dataVariance = vsg::DYNAMIC_DATA;
    auto sampler = vsg::Sampler::create();
    sampler->addressModeU = sampler->addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler->magFilter = sampler->minFilter = VK_FILTER_LINEAR;
    sampler->maxLod = 0;
    quad.image = vsg::ImageInfo::create(sampler, pixels);
    quad.size = {640, 480};
    updateRectangle(nullptr, 0, 0, source->w, source->h);
}
void UiCanvasTexture::draw(Renderer* renderer)
{
    quad.uv_min = {pixel_offset / source->w, pixel_offset / source->h};
    quad.uv_max = quad.uv_min + 1.0f;
    quad.shader.value = 0x100a017 | (alpha_test ? DrawShader::MASK_ALPHATEST : 0);
    renderer->drawOverlayQuad(quad);
}
void UiCanvasTexture::updateRectangle(Renderer*, int left, int top, int right, int bottom)
{
    SDL_Rect rectangle{left, top, right - left, bottom - top};
    const SDL_Rect bounds{0, 0, source->w, source->h};
    if (!SDL_GetRectIntersection(&rectangle, &bounds, &rectangle)) return;
    const auto* input = static_cast<const std::uint8_t*>(source->pixels) +
                        std::size_t(rectangle.y) * source->pitch +
                        rectangle.x * SDL_BYTESPERPIXEL(source->format);
    auto* output = &pixels->at(rectangle.x, rectangle.y);
    if (!SDL_ConvertPixels(rectangle.w, rectangle.h, source->format, input, source->pitch,
                           SDL_PIXELFORMAT_RGBA32, output, source->w * 4))
        throw std::runtime_error(SDL_GetError());
    pixels->dirty();
}

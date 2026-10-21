#include "wiz8/render/compositor.h"

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

#include "wiz8/render/renderer.h"

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
    layers[std::size_t(layer)]->addChild(quad.node);
}
void Compositor::detach(OverlayQuad& quad)
{
    std::erase(widgets, &quad);
    std::erase(layers[std::size_t(quad.layer)]->children, quad.node);
    quad.compositor = nullptr;
}
void Compositor::clear()
{
    for (auto* quad : widgets) quad->compositor = nullptr;
    widgets.clear();
    for (auto& layer : layers) layer->children.clear();
}
void Compositor::draw(Renderer& renderer, OverlayLayer layer)
{
    for (auto* quad : widgets)
        if (quad->layer == layer) renderer.prepareOverlay(*quad);
    renderer.addOverlay(layers[std::size_t(layer)]);
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
    quad.material.alpha_test = alpha_test;
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

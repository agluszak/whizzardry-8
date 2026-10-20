#include "wiz8/render/compositor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>

#include <SDL3/SDL_surface.h>

#include "wiz8/platform/timer.h"
#include "wiz8/render/renderer.h"
#include "wiz8/render/texture.h"
#include "wiz8/surface_storage.h"

OverlayQuad::OverlayQuad(const OverlayQuad& other)
{
    *this = other;
}
OverlayQuad& OverlayQuad::operator=(const OverlayQuad& other)
{
    if (this == &other) return *this;
    texture = other.texture;
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
    : source(std::move(surface)), published(CreateCpuStorage(source->w, source->h, source->format)),
      texture(std::make_shared<Texture>(published))
{
    const auto bytes = SDL_BYTESPERPIXEL(source->format);
    for (int row = 0; row < source->h; ++row)
        std::memcpy(static_cast<std::uint8_t*>(published->pixels) + row * published->pitch,
                    static_cast<const std::uint8_t*>(source->pixels) + row * source->pitch,
                    std::size_t(source->w) * bytes);
    auto dimensions = texture->getDimensions();
    dimensions.native_size = true;
    texture->setDimensions(dimensions);
    texture->setMipmap(Texture::MIPMAP_NONE);
    texture->setWrapS(Texture::WRAP_CLAMP);
    texture->setWrapT(Texture::WRAP_CLAMP);
}
void UiCanvasTexture::draw(Renderer* renderer)
{
    OverlayQuad quad;
    quad.texture = texture;
    quad.size = {640, 480};
    const auto dimensions = texture->getDimensions();
    quad.uv_min = {pixel_offset / dimensions.width, pixel_offset / dimensions.height};
    quad.uv_max = quad.uv_min + 1.0f;
    quad.shader.value = 0x100a017 | (alpha_test ? DrawShader::MASK_ALPHATEST : 0);
    renderer->drawOverlayQuad(quad);
}
void UiCanvasTexture::updateRectangle(Renderer*, int left, int top, int right, int bottom)
{
    SDL_Rect rectangle{left, top, right - left, bottom - top};
    const SDL_Rect bounds{0, 0, source->w, source->h};
    if (!SDL_GetRectIntersection(&rectangle, &bounds, &rectangle)) return;
    const auto bytes = SDL_BYTESPERPIXEL(source->format);
    for (int row = rectangle.y; row < rectangle.y + rectangle.h; ++row)
        std::memcpy(static_cast<std::uint8_t*>(published->pixels) + row * published->pitch +
                        rectangle.x * bytes,
                    static_cast<const std::uint8_t*>(source->pixels) + row * source->pitch +
                        rectangle.x * bytes,
                    std::size_t(rectangle.w) * bytes);
    texture->publish();
}

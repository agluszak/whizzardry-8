#include "wiz8/render/compositor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include <SDL3/SDL_surface.h>

#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/platform/timer.h"
#include "wiz8/render/gpu_renderer.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/vertex_stream.h"

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
void Compositor::draw(GpuRenderer& renderer, OverlayLayer layer)
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

void GpuRenderer::drawOverlayQuad(const OverlayQuad& quad)
{
    const auto size = quad.scaledSize();
    if (!quad.visible || size.x <= 0 || size.y <= 0) return;
    const glm::vec2 center = quad.position + size * .5f;
    const float cosine = std::cos(quad.rotation), sine = std::sin(quad.rotation);
    std::array<glm::vec3, 4> positions;
    const std::array<glm::vec2, 4> corners{
        {{-0.5f, -0.5f}, {0.5f, -0.5f}, {-0.5f, 0.5f}, {0.5f, 0.5f}}};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const auto offset = corners[i] * size;
        positions[i] = {center.x + cosine * offset.x - sine * offset.y,
                        center.y + sine * offset.x + cosine * offset.y, 0};
    }
    const std::array<glm::vec2, 4> uv{
        {quad.uv_min, {quad.uv_max.x, quad.uv_min.y}, {quad.uv_min.x, quad.uv_max.y}, quad.uv_max}};
    const std::array<glm::vec4, 4> colors{quad.color, quad.color, quad.color, quad.color};
    vertexStream::VertexArrayInfo vertices{};
    const auto stream = [&](vertexStream::e_vertexArray slot, std::int32_t components,
                            std::uint32_t stride, const void* values) {
        vertices.components[slot] = components;
        vertices.strides[slot] = stride;
        vertices.arrays[slot] = values;
    };
    vertices.mask = (1u << vertexStream::VERTEX_ARRAY_DIFFUSE) |
                    (quad.texture ? 1u << vertexStream::VERTEX_ARRAY_TEXCOORD0 : 0);
    vertices.count = positions.size();
    stream(vertexStream::VERTEX_ARRAY_POSITIONS, 3, sizeof(positions[0]), positions.data());
    stream(vertexStream::VERTEX_ARRAY_DIFFUSE, 4, sizeof(colors[0]), colors.data());
    stream(vertexStream::VERTEX_ARRAY_TEXCOORD0, 2, sizeof(uv[0]), uv.data());
    auto shader = quad.shader;
    shader.value = (shader.value & ~DrawShader::PASS_MASK) | DrawShader::PASS_ALWAYS;
    shader.value &= ~(DrawShader::MASK_DEPTH_WRITE | DrawShader::MASK_FOG);
    if (quad.texture)
        shader.value |= DrawShader::MASK_TEXTURING;
    else
        shader.value &= ~DrawShader::MASK_TEXTURING;
    shader.value = (shader.value & ~DrawShader::MASK_GRADIENT) | DrawShader::MASK_GRADIENT_MODULATE;
    // Quads are laid out on the 640x480 design canvas, top-left origin.
    draw({.primitive = vertexStream::PRIMITIVE_TRIANGLE_STRIP,
          .vertices = vertices,
          .clip_transform = glm::ortho(0.0f, 640.0f, 480.0f, 0.0f, -1.0f, 1.0f),
          .shader = shader,
          .textures = {quad.texture.get(), nullptr},
          .fog_color = getFogColor()});
}

UiCanvasTexture::UiCanvasTexture(std::shared_ptr<SDL_Surface> surface)
    : texture(std::make_shared<Texture>(std::move(surface)))
{
    auto dimensions = texture->getDimensions();
    dimensions.native_size = true;
    texture->setDimensions(dimensions);
    texture->setMipmap(Texture::MIPMAP_NONE);
    texture->setWrapS(Texture::WRAP_CLAMP);
    texture->setWrapT(Texture::WRAP_CLAMP);
}
void UiCanvasTexture::draw(GpuRenderer* renderer)
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
void UiCanvasTexture::updateRectangle(GpuRenderer* renderer, int left, int top, int right,
                                      int bottom)
{
    renderer->setTextureSubImage(texture.get(), 0, left, top, right - left, bottom - top);
}

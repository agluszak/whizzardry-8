#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <SDL3/SDL_rect.h>
#include <vsg/core/Array2D.h>

#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float4.hpp"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/texture.h"

class Renderer;
class Compositor;

enum class OverlayLayer : std::uint8_t {
    PreWorld,
    UnderUi,
    OverUi,
    SquareAspect,
    Fullscreen,
    Cursor
};

// A widget owns its quad; the compositor borrows it while attached and snapshots
// visible values for each frame. There are no meshes, scene nodes or integer handles.
struct OverlayQuad {
    OverlayQuad() = default;
    OverlayQuad(const OverlayQuad& other);
    OverlayQuad& operator=(const OverlayQuad& other);
    ~OverlayQuad();

    std::shared_ptr<Texture> texture;
    vsg::ref_ptr<vsg::ImageInfo> image;
    glm::vec2 position{0};
    glm::vec2 size{0};
    glm::vec2 scale{1};
    glm::vec2 uv_min{0};
    glm::vec2 uv_max{1};
    glm::vec4 color{1};
    float rotation = 0;
    bool visible = true;
    OverlayLayer layer = OverlayLayer::OverUi;
    bool glow_enabled = false;
    std::array<glm::vec4, 2> glow_colors{glm::vec4(0), glm::vec4(1)};
    unsigned glow_period = 2000;
    DrawShader shader;

    glm::vec2 scaledSize() const { return size * scale; }
    void detach();

private:
    friend class Compositor;
    Compositor* compositor = nullptr;
};

class Compositor {
public:
    Compositor() = default;
    ~Compositor();
    Compositor(const Compositor&) = delete;
    Compositor& operator=(const Compositor&) = delete;
    void attach(OverlayQuad& quad, OverlayLayer layer);
    void detach(OverlayQuad& quad);
    void clear();
    void draw(Renderer& renderer, OverlayLayer layer);
    const std::vector<OverlayQuad>& frameQuads() const { return frame_quads; }

private:
    std::vector<OverlayQuad*> widgets;
    std::vector<OverlayQuad> frame_quads;
};

// One native-size streaming texture for an SDL UI/movie canvas. Dirty uploads
// publish into a separate snapshot, so Draw without Refresh stays private.
// VSG uploads the published canvas and composes it with the scene.
class UiCanvasTexture {
public:
    explicit UiCanvasTexture(std::shared_ptr<SDL_Surface> surface);
    void draw(Renderer* renderer);
    void setScale(float offset) { pixel_offset = offset; }
    void setAlphaTestEnabled(bool enabled) { alpha_test = enabled; }
    void updateRectangle(Renderer* renderer, int left, int top, int right, int bottom);

private:
    std::shared_ptr<SDL_Surface> source;
    vsg::ref_ptr<vsg::ubvec4Array2D> pixels;
    OverlayQuad quad;
    float pixel_offset = 0;
    bool alpha_test = false;
};

extern Compositor g_compositor;

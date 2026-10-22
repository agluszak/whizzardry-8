#pragma once

#include <cstdint>
#include <memory>

#include <glm/vec4.hpp>
#include <vsg/app/View.h>
#include <vsg/core/ref_ptr.h>

struct SDL_Window;
struct SDL_Surface;
struct OverlayQuad;
namespace render {
class RenderContext;
}

// SDL owns the platform window. VSG owns its Vulkan surface, device, views,
// graphics resources and presentation. A completed frame is the retirement
// boundary: GPU work has finished when endFrame returns, so the game can freely
// remove scene objects before the next frame.
class Renderer {
public:
    explicit Renderer(SDL_Window* window);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool reopen();
    void close();
    bool isOpen() const;
    std::uint32_t width() const;
    std::uint32_t height() const;
    void setVsync(bool enabled);
    std::uint64_t frameNumber() const;
    render::RenderContext& context();

    void beginFrame(const glm::vec4& clear = {0, 0, 0, 1});
    void addView(vsg::ref_ptr<vsg::View> view);
    void prepareOverlay(OverlayQuad& quad);
    void addOverlay(vsg::ref_ptr<vsg::Node> node);
    void drawOverlayQuad(OverlayQuad& quad);
    // Capture copies the rendered image before the presentation engine owns it.
    // The returned surface remains valid until the next captured frame.
    void endFrame(bool present = true, bool capture = false);
    SDL_Surface* readback() const;
    // Renders the last presented frame again offscreen and copies it, for
    // callers that only sometimes need the image. Valid as readback() is.
    SDL_Surface* captureLastFrame();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

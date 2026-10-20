#include "wiz8/render/gpu_renderer.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>

#include "glm/common.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/fwd.hpp"
#include "glm/vector_relational.hpp"
#include "wiz8/platform/timer.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/picking_math.h"
#include "wiz8/render/texture.h"
#include "wiz8/surface_storage.h"

GpuRenderer* GpuRenderer::active = nullptr;

GpuRenderer::GpuRenderer()
    : dirty(0), state_flags(0), state{}, statistics{}, texture_state{}, texture_default(nullptr)
{
    auto missing = CreateCpuStorage(8, 8, SDL_PIXELFORMAT_BGRA32);
    SDL_Rect square{0, 0, 4, 4};
    SDL_FillSurfaceRect(missing.get(), nullptr, 0xff000000);
    SDL_FillSurfaceRect(missing.get(), &square, 0xffff00ff);
    square.x = square.y = 4;
    SDL_FillSurfaceRect(missing.get(), &square, 0xffff00ff);
    fallback_texture.setSurfacePtr(std::shared_ptr<SDL_Surface>(std::move(missing)));
    fallback_texture.setName("Missing texture");
    this->device.window = 0;
    enable_flags = (1u << ENABLE_AUTO_FLIP) | (1u << ENABLE_CLEAR_ON_OPEN);
    state_flags = 0;
    dirty = 0xffffffff;
    setError(ERROR_NONE);
    display.swap_interval = 1;
    initView();
}

GpuRenderer::~GpuRenderer()
{
    deleteRenderers();
    deleteContext();
    gpuDeleteContext();
}

GpuRenderer::Statistics GpuRenderer::getStatistics()
{
    this->statistics.input_triangles = 0;
    this->statistics.input_vertices = 0;
    this->statistics.triangle_chunks = 0;
    this->statistics.sorted_triangles = 0;
    this->statistics.clipped_triangles = 0;
    for (auto& owned : renderers) {
        Renderer* renderer = owned.get();
        if (renderer != 0) {
            const auto renderer_stats = renderer->getStatistics();
            this->statistics.input_triangles += renderer_stats.input_triangles;
            this->statistics.clipped_triangles += renderer_stats.clipped_triangles;
            this->statistics.input_vertices += renderer_stats.input_vertices;
            this->statistics.triangle_chunks += renderer_stats.triangle_chunks;
            if (renderer->sorted == 1) {
                this->statistics.sorted_triangles += renderer_stats.device_vertex_indices;
            }
        }
    }
    auto result = this->statistics;
    result.elapsed = static_cast<double>(MonotonicMicroseconds()) / 1'000'000 - result.elapsed;
    return result;
}

void GpuRenderer::resetStatistics()
{
    statistics = {};
    drainRenderers(RendererDrain::statistics);
    statistics.elapsed = static_cast<double>(MonotonicMicroseconds()) / 1'000'000;
}

void GpuRenderer::toggle(e_enable option)
{
    enable_flags ^= 1u << option;
}

void GpuRenderer::setSwapInterval(std::uint32_t interval)
{
    if (interval != display.swap_interval) {
        display.swap_interval = interval;
        dirty |= DIRTY_SWAP_INTERVAL;
    }
}

void GpuRenderer::draw(const DrawCall& call)
{
    // Draws issued while the window closes are never presented.
    if ((state_flags & STATE_CLOSING_WINDOW) != 0) return;
    checkViewStateChanges();
    const std::array stages{residentStage(call.textures[0], call.texture_reduction),
                            residentStage(call.textures[1], call.texture_reduction)};
    gpuDraw(call, stages);
    statistics.draw_calls++;
}

void GpuRenderer::setError(e_error error)
{
    last_error = error;
}

GpuRenderer* GpuRenderer::getActive()
{
    return active;
}

void GpuRenderer::invalidateTexture(Texture* texture)
{
    const auto entry = textures.find(texture);
    if (entry == textures.end() || &entry->second == texture_default) return;
    textures.erase(entry);
}

void GpuRenderer::invalidateResidentTextures()
{
    flushImmediateRenderers();
    invalidateTextureCache();
}

void GpuRenderer::setTextureSubImage(Texture* texture, std::int32_t mipmap, std::int32_t x,
                                     std::int32_t y, std::int32_t width, std::int32_t height)
{
    if (!isWindowOpen() || !texture || mipmap < 0) return;
    texture = texture->resolve();
    const auto entry = textures.find(texture);
    if (entry == textures.end()) return;
    auto& resident = entry->second;
    if (static_cast<std::uint32_t>(mipmap) > resident.device.last_level) return;
    auto* pixels = texture->getSurfacePtr();
    if (!pixels) return;
    const auto level_width = resident.device.width >> mipmap;
    const auto level_height = resident.device.height >> mipmap;
    SDL_Rect region{x, y, width, height};
    const SDL_Rect bounds{0, 0, static_cast<int>(level_width), static_cast<int>(level_height)};
    if (!SDL_GetRectIntersection(&region, &bounds, &region)) return;
    auto destination = CreateCpuStorage(level_width, level_height, resident.pixel_format);
    if (!SDL_BlitSurface(pixels, &region, destination.get(), &region))
        throw std::runtime_error(SDL_GetError());
    const auto* data = static_cast<const unsigned char*>(destination->pixels);
    gpuUploadTexture(resident.device.gpu, mipmap,
                     {static_cast<std::uint32_t>(region.x), static_cast<std::uint32_t>(region.y),
                      static_cast<std::uint32_t>(region.w), static_cast<std::uint32_t>(region.h)},
                     {data, std::size_t(destination->pitch) * level_height}, destination->pitch);
}

void GpuRenderer::invalidateTextureCache()
{
    std::erase_if(textures, [this](const auto& entry) { return &entry.second != texture_default; });
}

std::uint32_t GpuRenderer::getResidentTextureMemUsed() const
{
    std::uint32_t used = 0;
    for (const auto& [id, record] : textures) {
        const auto* texture = &record;
        if (texture->device.gpu.handle != 0 && texture->device.gpu.size > 0) {
            used += texture->device.gpu.size;
        }
    }
    return used;
}

void GpuRenderer::checkFrameStateChanges()
{
    if ((dirty & DIRTY_FRAME_STATE) != 0) {
        applyFrameStateChanges();
    }
}

void GpuRenderer::applyFrameStateChanges()
{
    if ((dirty & DIRTY_SWAP_INTERVAL) != 0) {
        gpuSetSwapInterval(display.swap_interval);
    }
    dirty &= ~DIRTY_FRAME_STATE;
}

void GpuRenderer::drainRenderers(RendererDrain action)
{
    for (auto& renderer : renderers) {
        if (!renderer) continue;
        if (action == RendererDrain::immediate && renderer->sorted != 0) continue;
        if (action == RendererDrain::sorted && renderer->sorted != 1) continue;
        if (action == RendererDrain::reset)
            renderer->reset(0);
        else if (action == RendererDrain::statistics)
            renderer->resetStatistics();
        else
            renderer->submit();
    }
}

void GpuRenderer::flushRenderers()
{
    flushImmediateRenderers();
    flushSort();
    drainRenderers(RendererDrain::reset);
}

void GpuRenderer::flushSort()
{
    drainRenderers(RendererDrain::sorted);
}

void GpuRenderer::flushImmediateRenderers()
{
    drainRenderers(RendererDrain::immediate);
}

GpuRenderer::Renderer& GpuRenderer::batchRenderer(bool sorted)
{
    auto& renderer = renderers[sorted];
    if (!renderer) {
        renderer = std::make_unique<Renderer>(
            Renderer::Parameters{this, int(sorted), int(GpuRenderer::renderer_batch_limit),
                                 GpuRenderer::max_texture_stages});
    }
    return *renderer;
}

GpuRenderer::e_error GpuRenderer::beginFrame()
{
    if ((state_flags & STATE_CONTEXT_CREATED) == 0) {
        return ERROR_NO_CONTEXT;
    }
    if (isWindowOpen() == 0) {
        return ERROR_WINDOW_NOT_OPEN;
    }
    if (device.window == nullptr) {
        return ERROR_INVALID_WHANDLE;
    }
    if ((state_flags & STATE_FRAME_STARTED) == 0) {
        if ((enable_flags & (1u << ENABLE_AUTO_FLIP)) != 0 &&
            (state_flags & STATE_FRAME_FLIPPED) == 0) {
            flipFrame();
        }
        checkFrameStateChanges();
        state_flags |= STATE_FRAME_STARTED;
        state_flags &= ~STATE_FRAME_FLIPPED;
    }
    return ERROR_NONE;
}

void GpuRenderer::endFrame()
{
    if (isWindowOpen() == 0) {
        setError(ERROR_WINDOW_NOT_OPEN);
        return;
    }
    if ((state_flags & STATE_FRAME_STARTED) != 0) {
        flushRenderers();
        gpuFlushFrame();
        state_flags &= ~STATE_FRAME_STARTED;
    }
}

void GpuRenderer::flipFrame()
{
    flipFrame(0, 0, 0);
}

void GpuRenderer::flipFrame(const Rectangle* first, const Rectangle* second, std::uint32_t count)
{
    if (isWindowOpen() == 0) {
        setError(ERROR_WINDOW_NOT_OPEN);
        return;
    }
    if ((state_flags & STATE_FRAME_FLIPPED) != 0) {
        return;
    }
    flush();
    gpuFlipFrame();
    statistics.frames += 1;
    state_flags |= STATE_FRAME_FLIPPED;
}

void GpuRenderer::flush()
{
    if (isWindowOpen() == 0) {
        setError(ERROR_WINDOW_NOT_OPEN);
        return;
    }
    checkAllStateChanges();
    flushImmediateRenderers();
    gpuFlushFrame();
}

std::int32_t GpuRenderer::getWidth() const
{
    return device.open_info.width;
}

std::int32_t GpuRenderer::getHeight() const
{
    return device.open_info.height;
}

int GpuRenderer::isWindowOpen() const
{
    return (state_flags >> 1) & 1;
}

void GpuRenderer::setViewPort(std::uint32_t x, std::uint32_t y, std::uint32_t width,
                              std::uint32_t height)
{
    if (width < 1) {
        width = 1;
    }
    if (height < 1) {
        height = 1;
    }
    state.view_top = y;
    state.view_left = x;
    state.view_bottom = y + height;
    state.view_right = x + width;
    if (state.view_left >= (std::uint32_t)getWidth()) {
        state.view_left = getWidth();
    }
    if (state.view_top >= (std::uint32_t)getHeight()) {
        state.view_top = getHeight();
    }
    if (state.view_right >= (std::uint32_t)getWidth()) {
        state.view_right = getWidth();
    }
    if (state.view_bottom >= (std::uint32_t)getHeight()) {
        state.view_bottom = getHeight();
    }
    dirty |= DIRTY_VIEWPORT;
}

void GpuRenderer::performPickTest(const PickInput& input)
{
    if (input.picks.empty()) return;
    const auto vertexIndex = [&](int vertex) -> std::uint32_t {
        if (vertex < 0 ||
            (!input.vertices.empty() && static_cast<std::size_t>(vertex) >= input.vertices.size()))
            throw std::invalid_argument("Invalid picking vertex index");
        const auto mapped =
            input.vertices.empty() ? static_cast<std::uint32_t>(vertex) : input.vertices[vertex];
        if (mapped >= input.positions.size())
            throw std::invalid_argument("Invalid picking position index");
        return mapped;
    };
    // Validate the whole selection before any queued pick can be changed.
    for (const auto index : input.indices) {
        if (index >= input.triangles.size())
            throw std::invalid_argument("Invalid picking triangle index");
        const auto& triangle = input.triangles[index];
        for (const auto vertex : {triangle.x, triangle.y, triangle.z}) vertexIndex(vertex);
    }
    pick_vertices.resize(input.positions.size());
    std::ranges::transform(input.positions, pick_vertices.begin(), pickingMath::projectedPosition);
    for (auto& queued : input.picks) {
        for (const auto index : input.indices) {
            const auto& triangle = input.triangles[index];
            const std::array corners{pick_vertices[vertexIndex(triangle.x)],
                                     pick_vertices[vertexIndex(triangle.y)],
                                     pick_vertices[vertexIndex(triangle.z)]};
            if (const auto depth = pickingMath::triangleDepth(corners, queued.position)) {
                queued.position.z = *depth;
                queued.selected_model = input.owner;
                queued.triangle_index = index;
            }
        }
    }
}

void GpuRenderer::clear(std::uint32_t buffers, const ClearValues& values)
{
    if (isWindowOpen() == 0) {
        setError(ERROR_WINDOW_NOT_OPEN);
        return;
    }
    if (buffers != 0) {
        checkViewStateChanges();
        // Non-positive components, NaN included, clear to zero.
        const auto color = glm::mix(glm::clamp(values.color, 0.0f, 1.0f), glm::vec4(0),
                                    glm::lessThanEqual(values.color, glm::vec4(0)));
        gpuClearBuffers(buffers, {color, std::clamp(values.depth, 0.0f, 1.0f), values.stencil});
    }
}

void GpuRenderer::applyViewStateChanges()
{
    if ((dirty & (DIRTY_VIEWPORT | DIRTY_DEPTH_RANGE)) != 0) {
        GpuRenderer::GpuViewPort viewport;
        viewport.x = state.view_left;
        viewport.y = state.view_top;
        viewport.width = state.view_right - viewport.x;
        viewport.height = state.view_bottom - viewport.y;
        viewport.min_depth = state.depth_min;
        viewport.max_depth = state.depth_max;
        if ((state_flags & STATE_CLOSING_WINDOW) == 0) {
            gpuSetViewPort(viewport);
        }
    }
    if ((dirty & DIRTY_SCISSOR) != 0) {
        recalcScissor();
    }
    dirty &= ~DIRTY_VIEW_STATE;
}

void GpuRenderer::setScissor(std::uint32_t x, std::uint32_t y, std::uint32_t width,
                             std::uint32_t height)
{
    state.scissor.left = x;
    state.scissor.right = x + width;
    state.scissor.top = y;
    state.scissor.bottom = y + height;
    if (state.scissor.left >= (std::uint32_t)getWidth()) {
        state.scissor.left = getWidth();
    }
    if (state.scissor.top >= (std::uint32_t)getHeight()) {
        state.scissor.top = getHeight();
    }
    if (state.scissor.right >= (std::uint32_t)getWidth()) {
        state.scissor.right = getWidth();
    }
    if (state.scissor.bottom >= (std::uint32_t)getHeight()) {
        state.scissor.bottom = getHeight();
    }
    recalcScissor();
    dirty |= DIRTY_SCISSOR;
}

void GpuRenderer::recalcScissor()
{
    if (state.scissor.left == 0 && state.scissor.right == (std::uint32_t)getWidth() &&
        state.scissor.top == 0 && state.scissor.bottom == (std::uint32_t)getHeight()) {
        state.scissor_flags |= 2;
    } else {
        state.scissor_flags &= ~2UL;
    }
    gpuSetScissor(state.scissor);
}

void GpuRenderer::assertContext() const {}

void GpuRenderer::checkViewStateChanges()
{
    if ((dirty & DIRTY_VIEW_STATE) != 0) {
        applyViewStateChanges();
    }
}

void GpuRenderer::checkAllStateChanges()
{
    if (dirty != 0) {
        checkFrameStateChanges();
        checkViewStateChanges();
    }
}

GpuRenderer::GpuTexParms GpuRenderer::samplerParameters(const Texture::Parameters& requested) const
{
    const auto mag =
        requested.mag == Texture::FILTER_DEFAULT ? texture_state.default_mag_filter : requested.mag;
    const auto min =
        requested.min == Texture::FILTER_DEFAULT ? texture_state.default_min_filter : requested.min;
    const auto mip = requested.mipmap == Texture::MIPMAP_DEFAULT ? texture_state.default_mipmap
                                                                 : requested.mipmap;
    return {mag >= Texture::FILTER_GOOD ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST,
            min >= Texture::FILTER_GOOD ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST,
            mip == Texture::MIPMAP_BEST ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR
                                        : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
            requested.wrap_s == Texture::WRAP_CLAMP ? SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE
                                                    : SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
            requested.wrap_t == Texture::WRAP_CLAMP ? SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE
                                                    : SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
            requested.mipmap_bias,
            mip == Texture::MIPMAP_NONE ? 0.0f : 16.0f};
}

GpuRenderer::GpuStage GpuRenderer::residentStage(Texture* texture, std::int32_t reduction)
{
    texture = texture ? texture->resolve() : nullptr;
    if (!texture) texture = &fallback_texture;
    const auto entry = textures.find(texture);
    const auto* resident =
        entry == textures.end() ? createNewTexture(texture, reduction) : &entry->second;
    return {resident->device.gpu.handle, samplerParameters(texture->getParameters())};
}

void GpuRenderer::prepareTexture(Texture* texture)
{
    if ((state_flags & STATE_CLOSING_WINDOW) == 0) residentStage(texture, 0);
}

namespace {

std::uint32_t nextTextureDimension(std::uint32_t value)
{
    // Preserve the historical wrap to one above the largest power of two;
    // bit_ceil requires its result to be representable.
    return value > (1u << 31) ? 1u : std::bit_ceil(value);
}

} // namespace

GpuRenderer::e_error GpuRenderer::createContext(SDL_Window* window)
{
    deleteContext();
    if (window == nullptr) {
        return ERROR_INVALID_WHANDLE;
    }
    if (!gpuCreateContext(window)) {
        return ERROR_CONTEXT_CREATION_FAILED;
    }
    device.window = window;
    resetStatistics();
    state_flags |= STATE_CONTEXT_CREATED;
    return ERROR_NONE;
}

void GpuRenderer::deleteContext()
{
    if ((state_flags & STATE_CONTEXT_CREATED) != 0) {
        closeWindow(static_cast<e_closeHint>(0));
        closeTexCache();
        gpuDeleteContext();
        state_flags &= ~STATE_CONTEXT_CREATED;
        device.window = 0;
    }
}

void GpuRenderer::deleteRenderers()
{
    for (auto& renderer : renderers) renderer.reset();
}

SDL_Surface* GpuRenderer::lockBuffer()
{
    if (!isWindowOpen()) {
        setError(ERROR_WINDOW_NOT_OPEN);
        return nullptr;
    }
    flush();
    checkViewStateChanges();
    auto* surface = gpuLockBuffer();
    const SDL_Rect clip{int(state.scissor.left), int(state.scissor.top),
                        int(state.scissor.right - state.scissor.left),
                        int(state.scissor.bottom - state.scissor.top)};
    if (surface) SDL_SetSurfaceClipRect(surface, &clip);
    return surface;
}

void GpuRenderer::unlockBuffer(bool modified)
{
    gpuUnlockBuffer(modified);
}

void GpuRenderer::closeWindow(e_closeHint hint)
{
    if (isWindowOpen() != 0) {
        state_flags |= STATE_CLOSING_WINDOW;
        flush();
        deleteRenderers();
        invalidateResidentTextures();
        closeTexCache();
        gpuCloseWindow();
        resetStatistics();
        device.open_info = {};
        state_flags &= ~STATE_WINDOW_OPEN;
        active = nullptr;
        state_flags &= ~STATE_CLOSING_WINDOW;
        decltype(pick_vertices){}.swap(pick_vertices);
    }
}

GpuRenderer::e_error GpuRenderer::openWindow()
{
    if ((state_flags & STATE_CONTEXT_CREATED) == 0) {
        return ERROR_NO_CONTEXT;
    }
    int width = 0, height = 0;
    if (device.window) SDL_GetWindowSizeInPixels(device.window, &width, &height);
    return openWindow(width, height);
}

GpuRenderer::e_error GpuRenderer::openWindow(std::int32_t width, std::int32_t height)
{
    if ((state_flags & STATE_CONTEXT_CREATED) == 0) {
        return ERROR_NO_CONTEXT;
    }
    if (device.window == nullptr) {
        return ERROR_INVALID_WHANDLE;
    }
    OpenInfo info{};
    SDL_GetWindowSizeInPixels(device.window, &info.window_width, &info.window_height);
    info.width = width;
    info.height = height;
    return openWindowInternal(info);
}

GpuRenderer::e_error GpuRenderer::openWindowInternal(const OpenInfo& info)
{
    if ((std::uint32_t)info.width > (std::uint32_t)info.window_width ||
        (std::uint32_t)info.height > (std::uint32_t)info.window_height) {
        return ERROR_INVALID_VALUE;
    }
    if ((state_flags & STATE_CONTEXT_CREATED) == 0) {
        return ERROR_NO_CONTEXT;
    }
    closeWindow(static_cast<e_closeHint>(1));
    if (info.width != 0 && info.height != 0 && info.window_width != 0 && info.window_height != 0) {
        if (device.window == nullptr) {
            return ERROR_INVALID_WHANDLE;
        }
        if (GpuRenderer::max_back_buffer_width < (std::uint32_t)info.width ||
            GpuRenderer::max_back_buffer_height < (std::uint32_t)info.height) {
            return ERROR_INVALID_VALUE;
        }
        device.open_info = {};
        if (gpuOpenWindow(info.width, info.height)) {
            device.open_info = info;
            std::uint32_t flags = state_flags & ~4UL;
            active = this;
            state_flags |= STATE_WINDOW_OPEN;
            state_flags = flags | STATE_WINDOW_OPEN;
            state_flags |= STATE_FRAME_FLIPPED;
            resetStatistics();
            dirty = 0xffffffff;
            initTexCache();
            state.scissor.left = 0;
            state.scissor.top = 0;
            state.scissor.right = getWidth();
            state.scissor.bottom = getHeight();
            state.view_left = 0;
            state.view_top = 0;
            state.view_right = getWidth();
            state.view_bottom = getHeight();
            if ((enable_flags & (1UL << ENABLE_CLEAR_ON_OPEN)) != 0) {
                clear(GpuRenderer::GPU_BUFFER_COLOR | GpuRenderer::GPU_BUFFER_DEPTH |
                          GpuRenderer::GPU_BUFFER_STENCIL,
                      {});
                flipFrame();
                flush();
            }
            return ERROR_NONE;
        }
    }
    return ERROR_WINDOW_OPEN_FAILED;
}

void GpuRenderer::closeTexCache()
{
    texture_default = nullptr;
    textures.clear();
}

void GpuRenderer::initTexCache()
{
    closeTexCache();
    texture_default = createNewTexture(&fallback_texture, 0);
}

void GpuRenderer::evaluateTextureDimensions(TextureCacheData& device,
                                            const Texture::Dimensions& dimensions,
                                            std::int32_t reduction)
{
    if (dimensions.native_size) {
        device.width = dimensions.width;
        device.height = dimensions.height;
        device.first_level = device.last_level = 0;
        return;
    }
    std::uint32_t min_dim = GpuRenderer::texture_min_dim;
    const auto shift = std::clamp(reduction, std::int32_t{0}, std::int32_t{7});
    std::uint32_t width = nextTextureDimension(dimensions.width) >> shift;
    std::uint32_t height = nextTextureDimension(dimensions.height) >> shift;
    std::uint32_t device_width = min_dim;
    if (min_dim <= width && width <= GpuRenderer::texture_max_dim) {
        device_width = width;
    }
    std::uint32_t device_height = min_dim;
    if (min_dim <= height && height <= GpuRenderer::texture_max_dim) {
        device_height = height;
    }
    int unbalanced = 0;
    if (device_height < device_width) {
        while (GpuRenderer::texture_max_aspect < device_width / device_height &&
               device_height < GpuRenderer::texture_max_dim) {
            device_height *= 2;
        }
        unbalanced = device_height < device_width;
    }
    if (unbalanced == 0 && device_height != device_width) {
        while (GpuRenderer::texture_max_aspect < device_height / device_width &&
               device_width < GpuRenderer::texture_max_dim) {
            device_width *= 2;
        }
    }
    device.width = device_width;
    device.height = device_height;
    device.first_level = 0;
    device.last_level = 0;
    if (dimensions.generate_mipmaps && GpuRenderer::texture_min_dim < device_width) {
        do {
            if (device_height <= GpuRenderer::texture_min_dim) {
                return;
            }
            ++device.last_level;
            device_width >>= 1;
            device_height >>= 1;
        } while (GpuRenderer::texture_min_dim < device_width);
    }
}

GpuRenderer::GpuTexture* GpuRenderer::createNewTexture(Texture* texture, std::int32_t reduction)
{
    auto [entry, inserted] = textures.try_emplace(texture, *this);
    auto* result = &entry->second;
    if (!inserted) return result;
    try {
        const auto dimensions = texture->getDimensions();
        evaluateTextureDimensions(result->device, dimensions, reduction);
        SDL_Surface* source = texture->getSurfacePtr();
        result->device.gpu = gpuCreateTexture(result->device.width, result->device.height,
                                              result->device.last_level + 1);
        if (!result->device.gpu.handle) throw std::runtime_error("Cannot create GPU texture");
        if (source) {
            const auto w = result->device.width;
            const auto h = result->device.height;
            auto destination = CreateCpuStorage(w, h, result->pixel_format);
            if (dimensions.native_size) {
                if (!SDL_BlitSurface(source, nullptr, destination.get(), nullptr))
                    throw std::runtime_error(SDL_GetError());
            } else {
                StretchCpuSurface(*source, *destination, SDL_SCALEMODE_LINEAR);
            }
            const auto* data = static_cast<const unsigned char*>(destination->pixels);
            gpuUploadTexture(result->device.gpu, 0, {0, 0, w, h},
                             {data, std::size_t(destination->pitch) * h}, destination->pitch);
        }
        ++statistics.textures_created;
        return result;
    } catch (...) {
        textures.erase(entry);
        throw;
    }
}

GpuRenderer::e_error GpuRenderer::getError()
{
    e_error error = last_error;
    setError(ERROR_NONE);
    return error;
}

void GpuRenderer::initView()
{
    state.depth_min = 0.0;
    state.depth_max = 1.0;
}

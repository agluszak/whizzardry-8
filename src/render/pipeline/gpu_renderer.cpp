#include "wiz8/render/gpu_renderer.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
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
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "compat/compiler.h"
#include "glm/common.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/fwd.hpp"
#include "glm/geometric.hpp"
#include "glm/matrix.hpp"
#include "glm/vector_relational.hpp"
#include "wiz8/compat/native.h"
#include "wiz8/render/clip.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/matrix_scope.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/picking_math.h"
#include "wiz8/render/plane_math.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/vertex_stream.h"
#include "wiz8/spatial/bounding_box.h"
#include "wiz8/spatial/bounding_sphere.h"
#include "wiz8/surface_storage.h"

class MeshInstance;

GpuRenderer* GpuRenderer::active = nullptr;

GpuRenderer::GpuRenderer()
    : dirty(0), state_flags(0), state{}, statistics{}, clear_state{}, texture_slots{},
      texture_parms{}, texture_state{}, polygon_offset(0), texture_default(nullptr),
      texture_reduction(0), vertex_arrays_dirty(0)
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
    vertex_arrays_dirty = 0xffffffff;
    vertex_arrays.clip = vertexStream::FRUSTUM_CLIP_MASK;
    vertex_arrays.count = 0;
    vertex_arrays.mask = 0;
    for (w8_ulong index = 0; index < 6; ++index) {
        vertex_arrays.components[index] = 0;
        vertex_arrays.types[index] = vertexStream::TYPE_FLOAT;
        vertex_arrays.strides[index] = 0;
        vertex_arrays.arrays[index] = 0;
    }
    for (auto& plane : state.clip_planes) {
        plane = glm::vec4(0);
    }
    state.project_clip_near = glm::mat4(0);
    state.max_modelview_scale = 0.0f;
    pick.pick_key = 0;
    exclusion_mask = 0;
    enable_flags = (1u << ENABLE_AUTO_FLIP) | (1u << ENABLE_CLEAR_ON_OPEN);
    state_flags = 0;
    dirty = 0xffffffff;
    setError(ERROR_NONE);
    display.swap_interval = 1;
    fog_color = glm::vec4(0);
    shader = DrawShader();
    texture_iface[0] = 0;
    texture_iface[1] = 0;
    initClearColors();
    initLights();
    initMatrices();
    initView();
}

GpuRenderer::~GpuRenderer()
{
    deleteRenderers();
    deleteContext();
    gpuDeleteContext();
}

void GpuRenderer::setTextureReduction(w8_long reduction)
{
    texture_reduction = std::clamp(reduction, w8_long{0}, w8_long{7});
}

void GpuRenderer::setTexture(Texture* texture, w8_ulong layer)
{
    if (layer < GpuRenderer::max_texture_stages && texture_iface[layer] != texture) {
        texture_iface[layer] = texture;
        changeTexture(texture, layer, 0);
        dirty |= 1 << (layer + DIRTY_TEXTURE_SHIFT);
    }
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
    result.elapsed = static_cast<double>(w8_clock_us()) / 1'000'000 - result.elapsed;
    return result;
}

void GpuRenderer::resetStatistics()
{
    statistics = {};
    drainRenderers(RendererDrain::statistics);
    statistics.elapsed = static_cast<double>(w8_clock_us()) / 1'000'000;
}

void GpuRenderer::toggle(e_enable option)
{
    enable_flags ^= 1u << option;
}

void GpuRenderer::setSwapInterval(w8_ulong interval)
{
    if (interval != display.swap_interval) {
        display.swap_interval = interval;
        dirty |= DIRTY_SWAP_INTERVAL;
    }
}

void GpuRenderer::setClipState(std::uint32_t state)
{
    if (state != vertex_arrays.clip) {
        vertex_arrays.clip = state;
        vertex_arrays_dirty |= DIRTY_VERTEX_ARRAY_INFO;
    }
}

GpuRenderer::e_winding GpuRenderer::getWinding() const
{
    return state.winding;
}

void GpuRenderer::setWinding(e_winding winding)
{
    if (state.winding != winding) {
        state.winding = winding;
        dirty |= DIRTY_CULLING;
    }
}

void GpuRenderer::setAmbientLight(const glm::vec4& light)
{
    ambient_light = light;
}

glm::vec4 GpuRenderer::getAmbientLight() const
{
    return ambient_light;
}

void GpuRenderer::setAmbientLight(float red, float green, float blue, float alpha)
{
    glm::vec4 light;
    light.x = red;
    light.y = green;
    light.z = blue;
    light.w = alpha;
    setAmbientLight(light);
}

GpuRenderer::e_cullMode GpuRenderer::getCullMode() const
{
    return state.cull_mode;
}

std::pair<float, float> GpuRenderer::getEnvironmentRange() const
{
    return {environment.minimum, environment.maximum};
}

std::pair<float, float> GpuRenderer::getEnvironmentScaleFactor() const
{
    return {environment.near_scale, environment.far_scale};
}

void GpuRenderer::setPolygonOffset(w8_long offset)
{
    if (offset != polygon_offset) {
        flushImmediateRenderers();
        polygon_offset = offset;
        dirty |= DIRTY_POLYGON_OFFSET;
    }
}

w8_long GpuRenderer::getPolygonOffset() const
{
    return polygon_offset;
}

void GpuRenderer::setClearColor(const glm::vec4& color)
{
    {
        const auto color_value = color;
        (clear_state.clear_values.color) =
            glm::mix(glm::clamp(color_value, float(0), float(1)), glm::vec4(0),
                     glm::lessThanEqual(color_value, glm::vec4(0)));
    }
}

void GpuRenderer::setClearColor(float red, float green, float blue, float alpha)
{
    setClearColor(glm::vec4(red, green, blue, alpha));
}

void GpuRenderer::setClearDepth(double depth)
{
    clear_state.clear_values.depth = std::clamp(depth, 0.0, 1.0);
}

/* Both indexed and array draws consume the same pending renderer state. */
void GpuRenderer::prepareDrawState()
{
    checkViewStateChanges();
    checkDrawStateChanges();
    if ((vertex_arrays_dirty & DIRTY_VERTEX_ARRAY_INFO) != 0) {
        gpuSetVertexArrayInfo(&vertex_arrays);
        vertex_arrays_dirty &= ~DIRTY_VERTEX_ARRAY_INFO;
    }
}

void GpuRenderer::drawArrays(vertexStream::e_primitive primitive, w8_long first, w8_ulong count)
{
    if ((enable_flags & 4) == 0) {
        prepareDrawState();
        gpuDrawArrays(primitive, first, count);
        statistics.draw_calls++;
    }
}

void GpuRenderer::drawElements(vertexStream::e_primitive primitive, w8_ulong count,
                               vertexStream::e_indexType type, const void* indices)
{
    if ((enable_flags & 4) == 0) {
        prepareDrawState();
        gpuDrawElements(primitive, {static_cast<const w8_ulong*>(indices), count});
        statistics.draw_calls++;
    }
}

void GpuRenderer::setError(e_error error)
{
    last_error = error;
}

GpuRenderer* GpuRenderer::getActive()
{
    return active;
}

void GpuRenderer::unbindTexture(const Texture* texture)
{
    if (auto* renderer = getActive()) {
        for (auto*& bound : renderer->texture_iface)
            if (bound == texture) bound = nullptr;
    }
}

void GpuRenderer::invalidateTexture(Texture* texture)
{
    const auto entry = textures.find(texture);
    if (entry == textures.end() || &entry->second == texture_default) return;
    for (unsigned stage = 0; stage < 2; ++stage) {
        if (texture_slots[stage] == &entry->second) {
            texture_slots[stage] = nullptr;
            dirty |= 1u << (stage + DIRTY_TEXTURE_SHIFT);
        }
    }
    textures.erase(entry);
}

void GpuRenderer::resetCurrentTexPointers()
{
    for (w8_ulong stage = 0; stage < GpuRenderer::max_texture_stages; ++stage) {
        texture_iface[stage] = 0;
        texture_slots[stage] = 0;
    }
    dirty |= DIRTY_TEXTURE0;
    dirty |= DIRTY_TEXTURE1;
}

void GpuRenderer::invalidateResidentTextures()
{
    flushImmediateRenderers();
    invalidateTextureCache();
}

void GpuRenderer::setTextureSubImage(Texture* texture, w8_long mipmap, w8_long x, w8_long y,
                                     w8_long width, w8_long height)
{
    if (!isWindowOpen() || !texture || mipmap < 0) return;
    texture = texture->resolve();
    const auto entry = textures.find(texture);
    if (entry == textures.end()) return;
    auto& resident = entry->second;
    if (static_cast<w8_ulong>(mipmap) > resident.device.last_level) return;
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
                     {static_cast<w8_ulong>(region.x), static_cast<w8_ulong>(region.y),
                      static_cast<w8_ulong>(region.w), static_cast<w8_ulong>(region.h)},
                     {data, std::size_t(destination->pitch) * level_height}, destination->pitch);
}

void GpuRenderer::invalidateTextureCache()
{
    resetCurrentTexPointers();
    std::erase_if(textures, [this](const auto& entry) { return &entry.second != texture_default; });
}

w8_ulong GpuRenderer::getResidentTextureMemUsed() const
{
    w8_ulong used = 0;
    for (const auto& [id, record] : textures) {
        const auto* texture = &record;
        if (texture->device.gpu.handle != 0 && texture->device.gpu.size > 0) {
            used += texture->device.gpu.size;
        }
    }
    return used;
}

void GpuRenderer::matrixMode(e_matrixMode mode)
{
    state.matrix_mode = mode;
}

void GpuRenderer::setMatrixDirty()
{
    if (state.matrix_mode == MATRIX_PROJECTION) {
        dirty |= DIRTY_CLIP_PLANES;
    }
    dirty |= 1 << (static_cast<int>(state.matrix_mode) + DIRTY_MATRIX_SHIFT);
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
    statistics.frame_state_count++;
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

GpuRenderer::Renderer& GpuRenderer::batchRenderer()
{
    const bool sorted = (enable_flags & (1u << ENABLE_SORTED_RENDERING)) != 0;
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

void GpuRenderer::flipFrame(const Rectangle* first, const Rectangle* second, w8_ulong count)
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

w8_long GpuRenderer::getWidth() const
{
    return device.open_info.width;
}

w8_long GpuRenderer::getHeight() const
{
    return device.open_info.height;
}

int GpuRenderer::isWindowOpen() const
{
    return (state_flags >> 1) & 1;
}

void GpuRenderer::setViewPort(w8_ulong x, w8_ulong y, w8_ulong width, w8_ulong height)
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
    if (state.view_left >= (w8_ulong)getWidth()) {
        state.view_left = getWidth();
    }
    if (state.view_top >= (w8_ulong)getHeight()) {
        state.view_top = getHeight();
    }
    if (state.view_right >= (w8_ulong)getWidth()) {
        state.view_right = getWidth();
    }
    if (state.view_bottom >= (w8_ulong)getHeight()) {
        state.view_bottom = getHeight();
    }
    dirty |= DIRTY_VIEWPORT;
}

void GpuRenderer::performPickTest(const PickInput& input)
{
    if (pick.pick_depth == 0) return;
    const auto vertexIndex = [&](int vertex) -> w8_ulong {
        if (vertex < 0 ||
            (!input.vertices.empty() && static_cast<std::size_t>(vertex) >= input.vertices.size()))
            throw std::invalid_argument("Invalid picking vertex index");
        const auto mapped =
            input.vertices.empty() ? static_cast<w8_ulong>(vertex) : input.vertices[vertex];
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
    for (auto& queued : std::span{pick.pick_stack}.first(pick.pick_depth)) {
        for (const auto index : input.indices) {
            const auto& triangle = input.triangles[index];
            const std::array corners{pick_vertices[vertexIndex(triangle.x)],
                                     pick_vertices[vertexIndex(triangle.y)],
                                     pick_vertices[vertexIndex(triangle.z)]};
            if (const auto depth = pickingMath::triangleDepth(corners, queued.position)) {
                queued.position.z = *depth;
                /* reinterpret-ok: the public pick key names the selected instance. */
                queued.selected_model = reinterpret_cast<MeshInstance*>(pick.pick_key);
                queued.triangle_index = index;
            }
        }
    }
}

void GpuRenderer::pushPick(const Pick& pick)
{
    if (this->pick.pick_depth < 0x20) {
        this->pick.pick_stack[this->pick.pick_depth] = pick;
        this->pick.pick_depth += 1;
    }
}

std::optional<GpuRenderer::Pick> GpuRenderer::popPick()
{
    if (pick.pick_depth == 0) return std::nullopt;
    return pick.pick_stack[--pick.pick_depth];
}

void GpuRenderer::setPickKey(w8_ulong_ptr key)
{
    pick.pick_key = key;
}

void GpuRenderer::setExclusionMask(w8_ulong mask)
{
    exclusion_mask = mask;
}

w8_ulong GpuRenderer::getExclusionMask() const
{
    return exclusion_mask;
}

void GpuRenderer::resetTexture()
{
    dirty |= DIRTY_TEXTURE0;
    dirty |= DIRTY_TEXTURE1;
}

glm::mat4 GpuRenderer::getNormalMatrix()
{
    checkViewStateChanges();
    glm::mat4 matrix = state.normal_matrix;
    if ((enable_flags & (1u << ENABLE_REVERSE_NORMALS)) != 0)
        matrix = glm::mat4(-glm::mat3(matrix));
    return matrix;
}

glm::mat4 GpuRenderer::getProjectClipNearMatrix()
{
    checkViewStateChanges();
    return state.project_clip_near;
}

void GpuRenderer::ortho(const Frustum& frustum)
{
    auto& current = state.matrix_current[state.matrix_mode];
    current *= glm::ortho(float(frustum.left), float(frustum.right), float(frustum.bottom),
                          float(frustum.top), float(frustum.near_plane), float(frustum.far_plane));
    setMatrixDirty();
}

void GpuRenderer::ortho(double left, double right, double bottom, double top, double near_plane,
                        double far_plane)
{
    Frustum frustum;
    frustum.left = left;
    frustum.right = right;
    frustum.bottom = bottom;
    frustum.top = top;
    frustum.near_plane = near_plane;
    frustum.far_plane = far_plane;
    ortho(frustum);
}

void GpuRenderer::rotate(double angle, const glm::vec3& axis)
{
    if (glm::dot(axis, axis) == 0) {
        loadIdentity();
        return;
    }
    auto& current = state.matrix_current[state.matrix_mode];
    current = glm::rotate(current, float(angle), axis);
    setMatrixDirty();
}

void GpuRenderer::scale(const glm::vec3& factors)
{
    auto& current = state.matrix_current[state.matrix_mode];
    current = glm::scale(current, factors);
    setMatrixDirty();
}

void GpuRenderer::scale(double x, double y, double z)
{
    scale(glm::vec3(x, y, z));
}

void GpuRenderer::translate(const glm::vec3& offset)
{
    auto& current = state.matrix_current[state.matrix_mode];
    current = glm::translate(current, offset);
    setMatrixDirty();
}

void GpuRenderer::translate(double x, double y, double z)
{
    translate(glm::vec3(x, y, z));
}

void GpuRenderer::loadIdentity()
{
    auto& current = state.matrix_current[state.matrix_mode];
    if (current != glm::mat4(1)) {
        current = glm::mat4(1);
        setMatrixDirty();
    }
}

glm::mat4 GpuRenderer::getMatrix(e_matrixMode mode)
{
    checkViewStateChanges();
    return state.matrix_current[mode];
}

glm::mat4 GpuRenderer::getInverseModelViewMatrix()
{
    checkViewStateChanges();
    return state.inverse_modelview;
}

void GpuRenderer::clear(std::uint32_t buffers)
{
    if (isWindowOpen() == 0) {
        setError(ERROR_WINDOW_NOT_OPEN);
        return;
    }
    if (buffers != 0) {
        checkViewStateChanges();
        const auto& values = clear_state.clear_values;
        gpuSetClearValues({values.color, static_cast<float>(values.depth), values.stencil});
        gpuClearBuffers(buffers);
    }
}

void GpuRenderer::applyViewStateChanges()
{
    w8_ulong dirty = this->dirty;
    statistics.view_state_applies += 1;
    if ((dirty & DIRTY_MODELVIEW) != 0) {
        classifyMatrix(MATRIX_MODELVIEW);
        dirty = this->dirty & ~DIRTY_MODELVIEW;
        this->dirty = dirty;
        if (dirty == 0) {
            return;
        }
    }
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
    if ((this->dirty & DIRTY_PROJECTION) != 0) {
        classifyMatrix(MATRIX_PROJECTION);
        state.project_clip_near = state.matrix_current[MATRIX_PROJECTION];
        glm::mat4& projection = state.matrix_current[MATRIX_PROJECTION];
        if (((projection[0][3] != 0.0f) || (projection[1][3] != 0.0f) ||
             (projection[2][3] != 0.0f)) &&
            ((projection[3][3] + projection[3][2] != 0.0f) &&
             (fabs((projection[2][3] + projection[2][2]) / (projection[3][3] + projection[3][2])) !=
              1.0))) {
            float scale = (float)fabs((projection[2][3] + projection[2][2]) /
                                      (projection[3][3] + projection[3][2]));
            state.project_clip_near *= scale;
        }
        if ((state_flags & STATE_CLOSING_WINDOW) == 0) {
            gpuSetProjectionMatrix(state.project_clip_near);
        }
    }
    if ((this->dirty & DIRTY_SCISSOR) != 0) {
        recalcScissor();
    }
    this->dirty &= ~DIRTY_VIEW_STATE;
}

void GpuRenderer::setScissor(w8_ulong x, w8_ulong y, w8_ulong width, w8_ulong height)
{
    state.scissor.left = x;
    state.scissor.right = x + width;
    state.scissor.top = y;
    state.scissor.bottom = y + height;
    if (state.scissor.left >= (w8_ulong)getWidth()) {
        state.scissor.left = getWidth();
    }
    if (state.scissor.top >= (w8_ulong)getHeight()) {
        state.scissor.top = getHeight();
    }
    if (state.scissor.right >= (w8_ulong)getWidth()) {
        state.scissor.right = getWidth();
    }
    if (state.scissor.bottom >= (w8_ulong)getHeight()) {
        state.scissor.bottom = getHeight();
    }
    recalcScissor();
    dirty |= DIRTY_SCISSOR;
}

void GpuRenderer::recalcScissor()
{
    if (state.scissor.left == 0 && state.scissor.right == (w8_ulong)getWidth() &&
        state.scissor.top == 0 && state.scissor.bottom == (w8_ulong)getHeight()) {
        state.scissor_flags |= 2;
    } else {
        state.scissor_flags &= ~2UL;
    }
    gpuSetScissor(state.scissor);
}

void GpuRenderer::classifyMatrix(e_matrixMode mode)
{
    statistics.matrix_classifications += 1;
    if (mode != MATRIX_MODELVIEW) {
        return;
    }
    const auto& modelview = state.matrix_current[MATRIX_MODELVIEW];
    glm::mat3 basis(modelview);
    const glm::vec3 lengths(glm::length(basis[0]), glm::length(basis[1]), glm::length(basis[2]));
    state.inverse_modelview = glm::inverse(modelview);
    state.max_modelview_scale = glm::max(lengths.x, glm::max(lengths.y, lengths.z));
    // Lighting expects a transform with model scaling removed.
    for (int column = 0; column < 3; ++column) basis[column] /= lengths[column];
    state.normal_matrix = glm::mat4(glm::transpose(glm::inverse(basis)));
}

void GpuRenderer::assertContext() const {}

void GpuRenderer::checkViewStateChanges()
{
    if ((dirty & DIRTY_VIEW_STATE) != 0) {
        applyViewStateChanges();
    }
}

void GpuRenderer::checkClipPlaneChanges()
{
    if ((dirty & DIRTY_CLIP_PLANES) != 0) {
        applyClipPlaneChanges();
    }
}

void GpuRenderer::checkDrawStateChanges()
{
    if ((dirty & DIRTY_DRAW_STATE) != 0) {
        applyDrawStateChanges();
    }
}

void GpuRenderer::checkAllStateChanges()
{
    if (dirty != 0) {
        checkDrawStateChanges();
        checkFrameStateChanges();
        checkViewStateChanges();
    }
}

GpuRenderer::e_matrixMode GpuRenderer::getMatrixMode() const
{
    return state.matrix_mode;
}

glm::mat4 GpuRenderer::getMatrix()
{
    return getMatrix(state.matrix_mode);
}

float GpuRenderer::getMaxModelViewScale()
{
    checkViewStateChanges();
    return state.max_modelview_scale;
}

MatrixScope::MatrixScope(GpuRenderer& renderer, GpuRenderer::e_matrixMode target)
    : renderer(&renderer), previous(renderer.getMatrix(target)),
      previous_mode(renderer.getMatrixMode()), target(target)
{
    renderer.matrixMode(target);
}
MatrixScope::MatrixScope(MatrixScope&& other) noexcept
    : renderer(std::exchange(other.renderer, nullptr)), previous(other.previous),
      previous_mode(other.previous_mode), target(other.target)
{
}
MatrixScope::~MatrixScope()
{
    if (!renderer) return;
    renderer->matrixMode(target);
    renderer->loadMatrix(previous);
    renderer->matrixMode(previous_mode);
}

void GpuRenderer::multMatrix(const glm::mat4& matrix)
{
    assertContext();
    state.matrix_current[state.matrix_mode] *= matrix;
    setMatrixDirty();
}

void GpuRenderer::multMatrix(const glm::dmat4& matrix)
{
    multMatrix(glm::mat4(matrix));
}

void GpuRenderer::loadMatrix(const glm::dmat4& matrix)
{
    loadMatrix(glm::mat4(matrix));
}

void GpuRenderer::loadMatrix(const glm::mat4& matrix)
{
    state.matrix_current[state.matrix_mode] = matrix;
    setMatrixDirty();
}

void GpuRenderer::loadMatrix(const glm::dmat3& matrix)
{
    loadMatrix(glm::mat4(matrix));
}

void GpuRenderer::loadMatrix(const glm::mat3& matrix)
{
    loadMatrix(glm::mat4(matrix));
}

void GpuRenderer::scale(double factor)
{
    scale(factor, factor, factor);
}

void GpuRenderer::rotate(double angle, double x, double y, double z)
{
    rotate(angle, glm::vec3(x, y, z));
}

void GpuRenderer::frustum(const Frustum& bounds)
{
    if (bounds.near_plane <= 0 || bounds.far_plane <= 0) return;
    auto& current = state.matrix_current[state.matrix_mode];
    current *= glm::frustum(float(bounds.left), float(bounds.right), float(bounds.bottom),
                            float(bounds.top), float(bounds.near_plane), float(bounds.far_plane));
    setMatrixDirty();
}

void GpuRenderer::frustum(double left, double right, double bottom, double top, double near_plane,
                          double far_plane)
{
    Frustum bounds;
    bounds.left = left;
    bounds.right = right;
    bounds.bottom = bottom;
    bounds.top = top;
    bounds.near_plane = near_plane;
    bounds.far_plane = far_plane;
    frustum(bounds);
}

void GpuRenderer::applyClipPlaneChanges()
{
    const auto rows = glm::transpose(state.matrix_current[MATRIX_PROJECTION]);
    state.clip_planes[0] = rows[3] + rows[0];
    state.clip_planes[1] = rows[3] - rows[0];
    state.clip_planes[2] = rows[3] + rows[1];
    state.clip_planes[3] = rows[3] - rows[1];
    state.clip_planes[4] = rows[3] + rows[2];
    state.clip_planes[5] = rows[3] - rows[2];
    for (int i = 0; i < 6; ++i) state.clip_planes[i] = planeMath::normalized(state.clip_planes[i]);
    dirty &= ~DIRTY_CLIP_PLANES;
}

bool GpuRenderer::pushClipPlane(const glm::vec4& plane, e_clipMode mode)
{
    if (state.clip_plane_count >= 26) return false;
    {
        checkClipPlaneChanges();
        checkViewStateChanges();
        w8_ulong bit = 1u << (state.clip_plane_count + 6);
        state.clip_planes[state.clip_plane_count + 6] =
            glm::transpose(state.inverse_modelview) * planeMath::normalized(plane);
        state.clip_mask |= bit;
        if (mode == 1) {
            state.clip_mode1_mask |= bit;
        } else {
            state.clip_mode1_mask &= ~bit;
        }
    }
    state.clip_plane_count += 1;
    return true;
}

void GpuRenderer::popClipPlane()
{
    if (state.clip_plane_count != 0) {
        state.clip_plane_count -= 1;
        if (state.clip_plane_count < 26) {
            w8_ulong mask = ~(1u << (state.clip_plane_count + 6));
            state.clip_mask &= mask;
            state.clip_mode1_mask &= mask;
        }
    }
}

GpuRenderer::ClipPlanes GpuRenderer::getClipPlanes()
{
    checkClipPlaneChanges();
    ClipPlanes planes{};
    for (int plane = 0; plane != 6; ++plane) {
        planes.planes[plane] = state.clip_planes[plane];
    }
    if ((state.clip_mask & 0xffffffc0) != 0) {
        for (w8_ulong index = 6; index < 0x20; ++index) {
            if ((state.clip_mask & (1u << index)) != 0) {
                planes.planes[index] = state.clip_planes[index];
            }
        }
    }
    planes.mask = state.clip_mask;
    return planes;
}

void GpuRenderer::setEnvironmentRange(float minimum, float maximum)
{
    environment.minimum = minimum;
    environment.maximum = maximum;
}

void GpuRenderer::setEnvironmentScaleFactor(float scale, float inverse_scale)
{
    if (0.0f < scale) {
        if (1.0f <= scale) {
            scale = 1.0f;
        }
    } else {
        scale = 0.0f;
    }
    environment.near_scale = scale;
    if (0.0f < inverse_scale) {
        if (inverse_scale < 1.0f) {
            environment.far_scale = inverse_scale;
            return;
        }
        environment.far_scale = 1.0f;
        return;
    }
    environment.far_scale = 0.0f;
}

/* Each component clamps through (0,1): strictly positive keeps the value, 1.0 or above saturates,
   anything else becomes 0. The color only updates (and dirties state) when a clamped component
   differs. */
void GpuRenderer::setFogColor(const glm::vec4& color)
{
    const auto clamped = glm::clamp(color, 0.0f, 1.0f);
    if (clamped != fog_color) {
        flushImmediateRenderers();
        fog_color = clamped;
        dirty |= DIRTY_FOG_COLOR;
    }
}

glm::vec4 GpuRenderer::getFogColor() const
{
    return fog_color;
}

w8_ulong_ptr GpuRenderer::getPickKey() const
{
    return pick.pick_key;
}

GpuRenderer::e_visibility GpuRenderer::testBoundingSphere(const glm::vec3& center, float radius)
{
    statistics.sphere_tests++;
    checkViewStateChanges();
    checkClipPlaneChanges();
    const glm::vec4 eye = state.matrix_current[MATRIX_MODELVIEW] * glm::vec4(center, 1);
    const float negative_radius = -radius * state.max_modelview_scale;
    for (unsigned i = 0; i < 32; ++i)
        if ((state.clip_mask & (1u << i)) && glm::dot(eye, state.clip_planes[i]) <= negative_radius)
            return VISIBILITY_OUTSIDE;
    statistics.sphere_visible++;
    return static_cast<e_visibility>(1);
}

GpuRenderer::e_visibility GpuRenderer::testBoundingBox(const glm::vec3& minimum,
                                                       const glm::vec3& maximum)
{
    statistics.box_tests++;
    checkViewStateChanges();
    glm::mat4 combined = state.matrix_current[MATRIX_PROJECTION];
    combined = combined * state.matrix_current[MATRIX_MODELVIEW];
    if (clipMath::testBoundingBox(combined, minimum, maximum)) {
        statistics.box_visible++;
        return static_cast<e_visibility>(1);
    }
    return VISIBILITY_OUTSIDE;
}

glm::vec4 GpuRenderer::getEyeSpaceLocation(const glm::vec3& object_location)
{
    checkViewStateChanges();
    return state.matrix_current[MATRIX_MODELVIEW] * glm::vec4(object_location, 1);
}

BoundingSphere GpuRenderer::eyeSpaceBounds(const BoundingSphere& sphere)
{
    const auto center = glm::vec3(getEyeSpaceLocation(sphere.center));
    return {center, sphere.radius * state.max_modelview_scale};
}

void GpuRenderer::applyDrawStateChanges()
{
    if ((dirty & DIRTY_FOG_COLOR) != 0) {
        gpuSetFogColor(fog_color);
    }
    if ((dirty & DIRTY_SHADER) != 0) {
        gpuSetShader(shader);
        statistics.shader_sets++;
    }
    if ((dirty & DIRTY_TEXTURE0) != 0) {
        changeTexture(texture_iface[0], 0, 1);
    }
    if (((dirty & DIRTY_TEXTURE1) != 0) && (1 < GpuRenderer::max_texture_stages)) {
        changeTexture(texture_iface[1], 1, 1);
    }
    if ((dirty & DIRTY_CULLING) != 0) {
        switch (state.cull_mode) {
        case CULL_BACK:
            gpuSetCullMode(state.winding != 0 ? GpuRenderer::GPU_CULL_FRONT
                                              : GpuRenderer::GPU_CULL_BACK);
            break;
        case CULL_FRONT:
            gpuSetCullMode(state.winding == 0 ? GpuRenderer::GPU_CULL_FRONT
                                              : GpuRenderer::GPU_CULL_BACK);
            break;
        case CULL_NONE:
            gpuSetCullMode(GpuRenderer::GPU_CULL_NONE);
            break;
        }
    }
    if ((dirty & DIRTY_POLYGON_OFFSET) != 0) {
        gpuSetPolygonOffset(polygon_offset);
    }
    dirty &= ~DIRTY_DRAW_STATE;
    statistics.draw_state_applies++;
}

void GpuRenderer::setTextureParameters(w8_ulong stage, const Texture::Parameters& requested)
{
    const auto mag =
        requested.mag == Texture::FILTER_DEFAULT ? texture_state.default_mag_filter : requested.mag;
    const auto min =
        requested.min == Texture::FILTER_DEFAULT ? texture_state.default_min_filter : requested.min;
    const auto mip = requested.mipmap == Texture::MIPMAP_DEFAULT ? texture_state.default_mipmap
                                                                 : requested.mipmap;
    const GpuRenderer::GpuTexParms resolved{
        mag >= Texture::FILTER_GOOD ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST,
        min >= Texture::FILTER_GOOD ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST,
        mip == Texture::MIPMAP_BEST ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR
                                    : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        requested.wrap_s == Texture::WRAP_CLAMP ? SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE
                                                : SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        requested.wrap_t == Texture::WRAP_CLAMP ? SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE
                                                : SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        requested.mipmap_bias,
        mip == Texture::MIPMAP_NONE ? 0.0f : 16.0f};
    if (texture_parms[stage] == resolved) return;
    texture_parms[stage] = resolved;
    ++statistics.texture_parameter_sets;
    gpuSetTextureParameters(stage, resolved);
}

void GpuRenderer::changeTexture(Texture* texture, w8_ulong stage, int apply_parms)
{
    if ((state_flags & STATE_CLOSING_WINDOW) || stage >= GpuRenderer::max_texture_stages) return;
    texture = texture ? texture->resolve() : nullptr;
    if (!texture) texture = &fallback_texture;
    const auto entry = textures.find(texture);
    auto* found = entry == textures.end() ? createNewTexture(texture) : &entry->second;
    if (!apply_parms) return;
    if (texture_slots[stage] != found) {
        texture_slots[stage] = found;
        gpuBindTexture(stage, found->device.gpu);
        ++statistics.texture_binds;
    }
    setTextureParameters(stage, texture->getParameters());
}

namespace {

w8_ulong nextTextureDimension(w8_ulong value)
{
    // Preserve the historical wrap to one above the largest power of two;
    // bit_ceil requires its result to be representable.
    return value > (1u << 31) ? 1u : std::bit_ceil(value);
}

} // namespace

void GpuRenderer::initLights()
{
    ambient_light = glm::vec4(0.2f, 0.2f, 0.2f, 1.0f);
}

void GpuRenderer::initMatrices()
{
    state.matrix_mode = MATRIX_MODELVIEW;
    for (w8_long i = 0; i < 2; i++) {
        state.matrix_current[i] = glm::mat4(1);
    }
    state.normal_matrix = glm::mat4(1);
    state.inverse_modelview = glm::mat4(1);
}

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
        shader = DrawShader();
        texture_iface[0] = 0;
        texture_iface[1] = 0;
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

GpuRenderer::e_error GpuRenderer::openWindow(w8_long width, w8_long height)
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
    if ((w8_ulong)info.width > (w8_ulong)info.window_width ||
        (w8_ulong)info.height > (w8_ulong)info.window_height) {
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
        if (GpuRenderer::max_back_buffer_width < (w8_ulong)info.width ||
            GpuRenderer::max_back_buffer_height < (w8_ulong)info.height) {
            return ERROR_INVALID_VALUE;
        }
        device.open_info = {};
        if (gpuOpenWindow(info.width, info.height)) {
            device.open_info = info;
            w8_ulong flags = state_flags & ~4UL;
            active = this;
            state_flags |= STATE_WINDOW_OPEN;
            state_flags = flags | STATE_WINDOW_OPEN;
            state_flags |= STATE_FRAME_FLIPPED;
            resetStatistics();
            dirty = 0xffffffff;
            initTexCache();
            texture_iface[0] = 0;
            texture_iface[1] = 0;
            shader = DrawShader();
            for (w8_ulong stage = 0; stage < GpuRenderer::max_texture_stages; ++stage) {
                texture_parms[stage].mipmap_bias = -1234567.0f;
                setTexture(0, stage);
            }
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
                      GpuRenderer::GPU_BUFFER_STENCIL);
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
    resetCurrentTexPointers();
    texture_default = nullptr;
    textures.clear();
}

void GpuRenderer::initTexCache()
{
    closeTexCache();
    texture_default = createNewTexture(&fallback_texture);
}

void GpuRenderer::evaluateTextureDimensions(TextureCacheData& device,
                                            const Texture::Dimensions& dimensions)
{
    if (dimensions.native_size) {
        device.width = dimensions.width;
        device.height = dimensions.height;
        device.first_level = device.last_level = 0;
        return;
    }
    w8_ulong min_dim = GpuRenderer::texture_min_dim;
    const auto reduction = texture_reduction;
    w8_ulong width = nextTextureDimension(dimensions.width) >> (reduction & 0x1f);
    w8_ulong height = nextTextureDimension(dimensions.height) >> (reduction & 0x1f);
    w8_ulong device_width = min_dim;
    if (min_dim <= width && width <= GpuRenderer::texture_max_dim) {
        device_width = width;
    }
    w8_ulong device_height = min_dim;
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

GpuRenderer::GpuTexture* GpuRenderer::createNewTexture(Texture* texture)
{
    auto [entry, inserted] = textures.try_emplace(texture, *this);
    auto* result = &entry->second;
    if (!inserted) return result;
    try {
        const auto dimensions = texture->getDimensions();
        evaluateTextureDimensions(result->device, dimensions);
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

void GpuRenderer::setClearStencil(w8_ulong stencil)
{
    clear_state.clear_values.stencil = stencil;
}

GpuRenderer::e_error GpuRenderer::getError()
{
    e_error error = last_error;
    setError(ERROR_NONE);
    return error;
}

void GpuRenderer::setDataPtr(vertexStream::e_vertexArray index, w8_long components,
                             vertexStream::e_type type, w8_ulong stride, const void* values)
{
    vertex_arrays.components[index] = components;
    vertex_arrays.types[index] = type;
    vertex_arrays.strides[index] = stride;
    vertex_arrays.arrays[index] = values;
    vertex_arrays_dirty |= DIRTY_VERTEX_ARRAY_INFO;
}

void GpuRenderer::initClearColors()
{
    setClearColor(glm::vec4(0));
    setClearDepth(1.0);
    setClearStencil(0);
}

void GpuRenderer::initView()
{
    state.depth_min = 0.0;
    state.depth_max = 1.0;
    state.clip_plane_count = 0;
    state.clip_mask = vertexStream::FRUSTUM_CLIP_MASK;
    state.clip_mode1_mask = 0;
    state.cull_mode = CULL_BACK;
    state.winding = static_cast<e_winding>(0);
}

bool clipMath::testBoundingBox(const glm::mat4& matrix, const glm::vec3& minimum,
                               const glm::vec3& maximum)
{
    std::array<glm::vec4, 8> corners;
    const BoundingBox box{minimum, maximum};
    for (unsigned i = 0; i < corners.size(); ++i) {
        // This visibility interface enumerates x in bit 0 and z in bit 2.
        const auto corner = ((i & 1) << 2) | (i & 2) | ((i & 4) >> 2);
        corners[i] = matrix * glm::vec4(box.corner(corner), 1);
    }
    std::array<std::uint8_t, 8> flags;
    clipFlags(flags, corners);
    std::uint8_t outside = 0x3f;
    for (const auto flag : flags) outside &= flag;
    return outside == 0;
}

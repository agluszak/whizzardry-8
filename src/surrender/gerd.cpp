#include <algorithm>
#include <array>
#include <cmath>
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
#include "surrender/SDLGPUDevice.h"
#include "surrender/model_view_scope.h"
#include "surrender/picking_math.h"
#include "surrender/plane_math.h"
#include "surrender/srBoundingBox.h"
#include "surrender/srBoundingSphere.h"
#include "surrender/srClip.h"
#include "surrender/srGERD.h"
#include "surrender/srMath.h"
#include "surrender/srModel.h"
#include "surrender/srRendererDefs.h"
#include "surrender/srShader.h"
#include "surrender/srTexture.h"
#include "wiz8/compat/native.h"
#include "wiz8/surface_storage.h"

class srModelInstance;
class srVertexProcessor;

srGERD* srGERD::active = nullptr;

// FUNCTION: SURRENDER 0x10019A00 SYMBOL
// RECOMP: ??0MatrixStack@srGERD@@QAE@XZ
srGERD::MatrixStack::MatrixStack() : depth(0) {}

// FUNCTION: SURRENDER 0x10019320
srGERD::srGERD()
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
    vertex_arrays.clip = srRendererDefs::FRUSTUM_CLIP_MASK;
    vertex_arrays.count = 0;
    vertex_arrays.mask = 0;
    for (w8_ulong index = 0; index < 6; ++index) {
        vertex_arrays.components[index] = 0;
        vertex_arrays.types[index] = srRendererDefs::TYPE_FLOAT;
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
    shader = srShader();
    texture_iface[0] = 0;
    texture_iface[1] = 0;
    initClearColors();
    initLights();
    initMatrices();
    initView();
    setName("SDL GPU");
}

// FUNCTION: SURRENDER 0x10019F40
srGERD::~srGERD()
{
    deleteRenderers();
    deleteContext();
}

// FUNCTION: SURRENDER 0x1001B010
srGERD& srGERD::operator=(const srGERD& other)
{
    return *this;
}

// FUNCTION: SURRENDER 0x10017920

// FUNCTION: SURRENDER 0x10017A50
void srGERD::setTextureReduction(w8_long reduction)
{
    texture_reduction = std::clamp(reduction, w8_long{0}, w8_long{7});
}

// FUNCTION: SURRENDER 0x10017AC0
void srGERD::setTexture(srTexture* texture, w8_ulong layer)
{
    if (layer < SDLGPUDevice::max_texture_stages && texture_iface[layer] != texture) {
        texture_iface[layer] = texture;
        changeTexture(texture, layer, 0);
        dirty |= 1 << (layer + DIRTY_TEXTURE_SHIFT);
    }
}

// FUNCTION: SURRENDER 0x10017F30

// FUNCTION: SURRENDER 0x10017F50

// FUNCTION: SURRENDER 0x1001AAF0
srGERD::Statistics srGERD::getStatistics()
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

// FUNCTION: SURRENDER 0x1001ACD0
void srGERD::resetStatistics()
{
    statistics = {};
    drainRenderers(RendererDrain::statistics);
    statistics.elapsed = static_cast<double>(w8_clock_us()) / 1'000'000;
}

// FUNCTION: SURRENDER 0x1001ADD0
void srGERD::toggle(e_enable option)
{
    enable_flags ^= 1UL << option;
}

// FUNCTION: SURRENDER 0x1001AE60
void srGERD::popEnable()
{
    w8_ulong flags = 0;
    if (environment_state.enable_depth != 0) {
        environment_state.enable_depth -= 1;
        flags = environment_state.enable_stack[environment_state.enable_depth];
    }
    if (flags != enable_flags) {
        for (e_enable option = static_cast<e_enable>(0); static_cast<int>(option) < 7;
             option = static_cast<e_enable>(static_cast<int>(option) + 1)) {
            w8_ulong bit = 1UL << option;
            if (((flags & bit) != 0) != ((enable_flags & bit) != 0)) {
                toggle(option);
            }
        }
    }
}

// FUNCTION: SURRENDER 0x1001AEC0
void srGERD::setSwapInterval(w8_ulong interval)
{
    if (interval != display.swap_interval) {
        display.swap_interval = interval;
        dirty |= DIRTY_SWAP_INTERVAL;
    }
}

// FUNCTION: SURRENDER 0x1001BE70
void srGERD::setClipState(std::uint32_t state)
{
    if (state != vertex_arrays.clip) {
        vertex_arrays.clip = state;
        vertex_arrays_dirty |= DIRTY_VERTEX_ARRAY_INFO;
    }
}

// FUNCTION: SURRENDER 0x1001C380
srGERD::e_winding srGERD::getWinding() const
{
    return state.winding;
}

// FUNCTION: SURRENDER 0x1001C390
void srGERD::setWinding(e_winding winding)
{
    if (state.winding != winding) {
        state.winding = winding;
        dirty |= DIRTY_CULLING;
    }
}

// FUNCTION: SURRENDER 0x1001C3B0
void srGERD::setAmbientLight(const glm::vec4& light)
{
    ambient_light = light;
}

// FUNCTION: SURRENDER 0x1001C410
glm::vec4 srGERD::getAmbientLight() const
{
    return ambient_light;
}

// FUNCTION: SURRENDER 0x1001C480
void srGERD::setAmbientLight(float red, float green, float blue, float alpha)
{
    glm::vec4 light;
    light.x = red;
    light.y = green;
    light.z = blue;
    light.w = alpha;
    setAmbientLight(light);
}

// FUNCTION: SURRENDER 0x1001C4C0
srGERD::e_cullMode srGERD::getCullMode() const
{
    return state.cull_mode;
}

// FUNCTION: SURRENDER 0x1001C4F0
std::pair<float, float> srGERD::getEnvironmentRange() const
{
    return {environment.x, environment.y};
}

// FUNCTION: SURRENDER 0x1001C5A0
std::pair<float, float> srGERD::getEnvironmentScaleFactor() const
{
    return {environment.z, environment.w};
}

// FUNCTION: SURRENDER 0x1001C8A0
void srGERD::setPolygonOffset(w8_long offset)
{
    if (offset != polygon_offset) {
        flushImmediateRenderers();
        polygon_offset = offset;
        dirty |= DIRTY_POLYGON_OFFSET;
    }
}

// FUNCTION: SURRENDER 0x1001C8D0
w8_long srGERD::getPolygonOffset() const
{
    return polygon_offset;
}

// FUNCTION: SURRENDER 0x1001CA30
void srGERD::setClearColor(const glm::vec4& color)
{
    {
        const auto color_value = color;
        (clear_state.clear_values.color) =
            glm::mix(glm::clamp(color_value, float(0), float(1)), glm::vec4(0),
                     glm::lessThanEqual(color_value, glm::vec4(0)));
    }
}

// FUNCTION: SURRENDER 0x1001CB00
void srGERD::setClearColor(float red, float green, float blue, float alpha)
{
    setClearColor(glm::vec4(red, green, blue, alpha));
}

// FUNCTION: SURRENDER 0x1001CB70
void srGERD::setClearDepth(double depth)
{
    clear_state.clear_values.depth = std::clamp(depth, 0.0, 1.0);
}

/* Both indexed and array draws consume the same pending renderer state. */
void srGERD::prepareDrawState()
{
    checkViewStateChanges();
    checkDrawStateChanges();
    if ((vertex_arrays_dirty & DIRTY_VERTEX_ARRAY_INFO) != 0) {
        device.gpu.setVertexArrayInfo(&vertex_arrays);
        vertex_arrays_dirty &= ~DIRTY_VERTEX_ARRAY_INFO;
    }
}

// FUNCTION: SURRENDER 0x1001CC10
void srGERD::drawArrays(srRendererDefs::e_primitive primitive, w8_long first, w8_ulong count)
{
    if ((enable_flags & 4) == 0) {
        prepareDrawState();
        device.gpu.drawArrays(primitive, first, count);
        statistics.draw_calls++;
    }
}

// FUNCTION: SURRENDER 0x1001CC90
void srGERD::drawElements(srRendererDefs::e_primitive primitive, w8_ulong count,
                          srRendererDefs::e_indexType type, const void* indices)
{
    if ((enable_flags & 4) == 0) {
        prepareDrawState();
        device.gpu.drawElements(primitive, {static_cast<const w8_ulong*>(indices), count});
        statistics.draw_calls++;
    }
}

// FUNCTION: SURRENDER 0x1001CEC0
w8_ulong srGERD::getVertexProcessorCount() const
{
    return static_cast<w8_ulong>(vertex_processors.size());
}

// FUNCTION: SURRENDER 0x1001CED0
const std::vector<srVertexProcessor*>& srGERD::getVertexProcessors() const
{
    return vertex_processors;
}

// FUNCTION: SURRENDER 0x1001CFC0
void srGERD::setError(e_error error)
{
    last_error = error;
}

srGERD* srGERD::getActive()
{
    return active;
}

void srGERD::unbindTexture(const srTexture* texture)
{
    if (auto* renderer = getActive()) {
        for (auto*& bound : renderer->texture_iface)
            if (bound == texture) bound = nullptr;
    }
}

// FUNCTION: SURRENDER 0x10018330
void srGERD::invalidateTexture(srTexture* texture)
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

// FUNCTION: SURRENDER 0x100281E0
void srGERD::resetCurrentTexPointers()
{
    for (w8_ulong stage = 0; stage < SDLGPUDevice::max_texture_stages; ++stage) {
        texture_iface[stage] = 0;
        texture_slots[stage] = 0;
    }
    dirty |= DIRTY_TEXTURE0;
    dirty |= DIRTY_TEXTURE1;
}

// FUNCTION: SURRENDER 0x10028150

void srGERD::invalidateResidentTextures()
{
    flushImmediateRenderers();
    invalidateTextureCache();
}

// FUNCTION: SURRENDER 0x100180A0
void srGERD::setTextureSubImage(srTexture* texture, w8_long mipmap, w8_long x, w8_long y,
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
    const SDL_Rect source{region.x + texture->source_region.x, region.y + texture->source_region.y,
                          region.w, region.h};
    if (!SDL_BlitSurface(pixels, &source, destination.get(), &region))
        throw std::runtime_error(SDL_GetError());
    const auto* data = static_cast<const unsigned char*>(destination->pixels);
    device.gpu.uploadTexture(resident.device.gpu, mipmap,
                             {static_cast<w8_ulong>(region.x), static_cast<w8_ulong>(region.y),
                              static_cast<w8_ulong>(region.w), static_cast<w8_ulong>(region.h)},
                             {data, std::size_t(destination->pitch) * level_height},
                             destination->pitch);
}

// FUNCTION: SURRENDER 0x10018480
void srGERD::invalidateTextureCache()
{
    resetCurrentTexPointers();
    std::erase_if(textures, [this](const auto& entry) { return &entry.second != texture_default; });
}

// FUNCTION: SURRENDER 0x10018500
w8_ulong srGERD::getResidentTextureMemUsed() const
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

// FUNCTION: SURRENDER 0x10023550
void srGERD::matrixMode(e_matrixMode mode)
{
    state.matrix_mode = mode;
}

// FUNCTION: SURRENDER 0x100235B0
bool srGERD::pushMatrix()
{
    MatrixStack& stack = state.matrix_stacks[state.matrix_mode];
    if (stack.depth < 0x20) {
        stack.stack[stack.depth] = state.matrix_current[state.matrix_mode];
        stack.depth++;
        return true;
    }
    return false;
}

// FUNCTION: SURRENDER 0x10023560
void srGERD::popMatrix()
{
    glm::mat4* matrix = &state.matrix_current[state.matrix_mode];
    MatrixStack& stack = state.matrix_stacks[state.matrix_mode];
    if (stack.depth != 0) {
        stack.depth--;
        *matrix = stack.stack[stack.depth];
    }
    setMatrixDirty();
}

// FUNCTION: SURRENDER 0x100236D0
void srGERD::setMatrixDirty()
{
    if (state.matrix_mode == MATRIX_PROJECTION) {
        dirty |= DIRTY_CLIP_PLANES;
    }
    dirty |= 1 << (static_cast<int>(state.matrix_mode) + DIRTY_MATRIX_SHIFT);
}

// FUNCTION: SURRENDER 0x1001D2D0
void srGERD::checkFrameStateChanges()
{
    if ((dirty & DIRTY_FRAME_STATE) != 0) {
        applyFrameStateChanges();
    }
}

// FUNCTION: SURRENDER 0x1001B450
void srGERD::applyFrameStateChanges()
{
    if ((dirty & DIRTY_SWAP_INTERVAL) != 0) {
        device.gpu.setSwapInterval(display.swap_interval);
    }
    dirty &= ~DIRTY_FRAME_STATE;
    statistics.frame_state_count++;
}

void srGERD::drainRenderers(RendererDrain action)
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

// FUNCTION: SURRENDER 0x10019A40
void srGERD::flushRenderers()
{
    flushImmediateRenderers();
    flushSort();
    drainRenderers(RendererDrain::reset);
}

// FUNCTION: SURRENDER 0x10019AD0
void srGERD::flushSort()
{
    drainRenderers(RendererDrain::sorted);
}

// FUNCTION: SURRENDER 0x10019B60
void srGERD::flushImmediateRenderers()
{
    drainRenderers(RendererDrain::immediate);
}

srGERD::Renderer& srGERD::batchRenderer()
{
    const bool sorted = (enable_flags & (1UL << ENABLE_SORTED_RENDERING)) != 0;
    auto& renderer = renderers[sorted];
    if (!renderer) {
        renderer = std::make_unique<Renderer>(
            Renderer::Parameters{this, int(sorted), int(SDLGPUDevice::renderer_batch_limit),
                                 SDLGPUDevice::max_texture_stages});
    }
    return *renderer;
}

// FUNCTION: SURRENDER 0x1001A790
srGERD::e_error srGERD::beginFrame()
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
        if ((enable_flags & (1UL << ENABLE_AUTO_FLIP)) != 0 &&
            (state_flags & STATE_FRAME_FLIPPED) == 0) {
            flipFrame();
        }
        checkFrameStateChanges();
        state_flags |= STATE_FRAME_STARTED;
        state_flags &= ~STATE_FRAME_FLIPPED;
    }
    return ERROR_NONE;
}

// FUNCTION: SURRENDER 0x1001A810
void srGERD::endFrame()
{
    if (isWindowOpen() == 0) {
        setError(ERROR_WINDOW_NOT_OPEN);
        return;
    }
    if ((state_flags & STATE_FRAME_STARTED) != 0) {
        flushRenderers();
        device.gpu.flushFrame();
        state_flags &= ~STATE_FRAME_STARTED;
    }
}

// FUNCTION: SURRENDER 0x1001A850
void srGERD::flipFrame()
{
    flipFrame(0, 0, 0);
}

// FUNCTION: SURRENDER 0x1001A860
void srGERD::flipFrame(const Rectangle* first, const Rectangle* second, w8_ulong count)
{
    if (isWindowOpen() == 0) {
        setError(ERROR_WINDOW_NOT_OPEN);
        return;
    }
    if ((state_flags & STATE_FRAME_FLIPPED) != 0) {
        return;
    }
    flush();
    device.gpu.flipFrame();
    statistics.frames += 1;
    state_flags |= STATE_FRAME_FLIPPED;
}

// FUNCTION: SURRENDER 0x1001AAB0
void srGERD::flush()
{
    if (isWindowOpen() == 0) {
        setError(ERROR_WINDOW_NOT_OPEN);
        return;
    }
    checkAllStateChanges();
    flushImmediateRenderers();
    device.gpu.flushFrame();
}

// FUNCTION: SURRENDER 0x1001D020
w8_long srGERD::getWidth() const
{
    return device.open_info.width;
}

// FUNCTION: SURRENDER 0x1001D0A0
w8_long srGERD::getHeight() const
{
    return device.open_info.height;
}

// FUNCTION: SURRENDER 0x1001D0D0
int srGERD::isWindowOpen() const
{
    return (state_flags >> 1) & 1;
}

// FUNCTION: SURRENDER 0x1001D030
// FUNCTION: SURRENDER 0x1001D1F0
void srGERD::pushEnable()
{
    w8_ulong depth = environment_state.enable_depth;
    if (depth < 0x10) {
        environment_state.enable_depth = depth + 1;
        environment_state.enable_stack[depth] = enable_flags;
    }
}

// FUNCTION: SURRENDER 0x1001D410
void srGERD::setViewPort(w8_ulong x, w8_ulong y, w8_ulong width, w8_ulong height)
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

// FUNCTION: SURRENDER 0x1001D630
void srGERD::performPickTest(const PickInput& input)
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
    std::ranges::transform(input.positions, pick_vertices.begin(),
                           srPickingMath::projectedPosition);
    for (auto& queued : std::span{pick.pick_stack}.first(pick.pick_depth)) {
        for (const auto index : input.indices) {
            const auto& triangle = input.triangles[index];
            const std::array corners{pick_vertices[vertexIndex(triangle.x)],
                                     pick_vertices[vertexIndex(triangle.y)],
                                     pick_vertices[vertexIndex(triangle.z)]};
            if (const auto depth = srPickingMath::triangleDepth(corners, queued.position)) {
                queued.position.z = *depth;
                /* reinterpret-ok: the public pick key names the selected instance. */
                queued.selected_model = reinterpret_cast<srModelInstance*>(pick.pick_key);
                queued.triangle_index = index;
            }
        }
    }
}

// FUNCTION: SURRENDER 0x1001DAA0
void srGERD::pushPick(const Pick& pick)
{
    if (this->pick.pick_depth < 0x20) {
        this->pick.pick_stack[this->pick.pick_depth] = pick;
        this->pick.pick_depth += 1;
    }
}

// FUNCTION: SURRENDER 0x1001DAE0
std::optional<srGERD::Pick> srGERD::popPick()
{
    if (pick.pick_depth == 0) return std::nullopt;
    return pick.pick_stack[--pick.pick_depth];
}

// FUNCTION: SURRENDER 0x1001DB10
void srGERD::setPickKey(w8_ulong_ptr key)
{
    pick.pick_key = key;
}

// FUNCTION: SURRENDER 0x1001EE50
void srGERD::setExclusionMask(w8_ulong mask)
{
    exclusion_mask = mask;
}

// FUNCTION: SURRENDER 0x1001EE60
w8_ulong srGERD::getExclusionMask() const
{
    return exclusion_mask;
}

// FUNCTION: SURRENDER 0x100293E0
void srGERD::resetTexture()
{
    dirty |= DIRTY_TEXTURE0;
    dirty |= DIRTY_TEXTURE1;
}

// FUNCTION: SURRENDER 0x100213C0
glm::mat4 srGERD::getNormalMatrix()
{
    checkViewStateChanges();
    glm::mat4 matrix = state.normal_matrix;
    if ((enable_flags & (1UL << ENABLE_REVERSE_NORMALS)) != 0)
        matrix = glm::mat4(-glm::mat3(matrix));
    return matrix;
}

// FUNCTION: SURRENDER 0x100214F0
glm::mat4 srGERD::getProjectClipNearMatrix()
{
    checkViewStateChanges();
    return state.project_clip_near;
}

// FUNCTION: SURRENDER 0x10022190
void srGERD::ortho(const Frustum& frustum)
{
    auto& current = state.matrix_current[state.matrix_mode];
    current *= glm::ortho(float(frustum.left), float(frustum.right), float(frustum.bottom),
                          float(frustum.top), float(frustum.near_plane), float(frustum.far_plane));
    setMatrixDirty();
}

// FUNCTION: SURRENDER 0x10022330
void srGERD::ortho(double left, double right, double bottom, double top, double near_plane,
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

// FUNCTION: SURRENDER 0x100223A0
void srGERD::rotate(double angle, const glm::vec3& axis)
{
    if (glm::dot(axis, axis) == 0) {
        loadIdentity();
        return;
    }
    auto& current = state.matrix_current[state.matrix_mode];
    current = glm::rotate(current, float(angle), axis);
    setMatrixDirty();
}

// FUNCTION: SURRENDER 0x10022780
void srGERD::scale(const glm::vec3& factors)
{
    auto& current = state.matrix_current[state.matrix_mode];
    current = glm::scale(current, factors);
    setMatrixDirty();
}

// FUNCTION: SURRENDER 0x10022870
void srGERD::scale(double x, double y, double z)
{
    scale(glm::vec3(x, y, z));
}

// FUNCTION: SURRENDER 0x10022960
void srGERD::translate(const glm::vec3& offset)
{
    auto& current = state.matrix_current[state.matrix_mode];
    current = glm::translate(current, offset);
    setMatrixDirty();
}

// FUNCTION: SURRENDER 0x10022A20
void srGERD::translate(double x, double y, double z)
{
    translate(glm::vec3(x, y, z));
}

// FUNCTION: SURRENDER 0x100231D0
void srGERD::loadIdentity()
{
    auto& current = state.matrix_current[state.matrix_mode];
    if (current != glm::mat4(1)) {
        current = glm::mat4(1);
        setMatrixDirty();
    }
}

// FUNCTION: SURRENDER 0x10023320
glm::mat4 srGERD::getMatrix(e_matrixMode mode)
{
    checkViewStateChanges();
    return state.matrix_current[mode];
}

// FUNCTION: SURRENDER 0x10023510
glm::mat4 srGERD::getInverseModelViewMatrix()
{
    checkViewStateChanges();
    return state.inverse_modelview;
}

// FUNCTION: SURRENDER 0x10020D30
void srGERD::clear(std::uint32_t buffers)
{
    if (isWindowOpen() == 0) {
        setError(ERROR_WINDOW_NOT_OPEN);
        return;
    }
    if (buffers != 0) {
        checkViewStateChanges();
        const auto& values = clear_state.clear_values;
        device.gpu.setClearValues({values.color, static_cast<float>(values.depth), values.stencil});
        device.gpu.clearBuffers(buffers);
    }
}

// FUNCTION: SURRENDER 0x1001BC70
void srGERD::applyViewStateChanges()
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
        SDLGPUDevice::ViewPort viewport;
        viewport.x = state.view_left;
        viewport.y = state.view_top;
        viewport.width = state.view_right - viewport.x;
        viewport.height = state.view_bottom - viewport.y;
        viewport.min_depth = state.depth_min;
        viewport.max_depth = state.depth_max;
        if ((state_flags & STATE_CLOSING_WINDOW) == 0) {
            device.gpu.setViewPort(viewport);
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
            device.gpu.setProjectionMatrix(state.project_clip_near);
        }
    }
    if ((this->dirty & DIRTY_SCISSOR) != 0) {
        recalcScissor();
    }
    this->dirty &= ~DIRTY_VIEW_STATE;
}

// FUNCTION: SURRENDER 0x1001D580
void srGERD::setScissor(w8_ulong x, w8_ulong y, w8_ulong width, w8_ulong height)
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

// FUNCTION: SURRENDER 0x100204C0
void srGERD::recalcScissor()
{
    if (state.scissor.left == 0 && state.scissor.right == (w8_ulong)getWidth() &&
        state.scissor.top == 0 && state.scissor.bottom == (w8_ulong)getHeight()) {
        state.scissor_flags |= 2;
    } else {
        state.scissor_flags &= ~2UL;
    }
    device.gpu.setScissor(state.scissor);
}

// FUNCTION: SURRENDER 0x100215A0
void srGERD::classifyMatrix(e_matrixMode mode)
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

// FUNCTION: SURRENDER 0x1001CF40
void srGERD::assertContext() const {}

// FUNCTION: SURRENDER 0x1001BB10
void srGERD::checkViewStateChanges()
{
    if ((dirty & DIRTY_VIEW_STATE) != 0) {
        applyViewStateChanges();
    }
}

// FUNCTION: SURRENDER 0x1001BB30
void srGERD::checkClipPlaneChanges()
{
    if ((dirty & DIRTY_CLIP_PLANES) != 0) {
        applyClipPlaneChanges();
    }
}

// FUNCTION: SURRENDER 0x1001BB20
void srGERD::checkDrawStateChanges()
{
    if ((dirty & DIRTY_DRAW_STATE) != 0) {
        applyDrawStateChanges();
    }
}

// FUNCTION: SURRENDER 0x1001D2A0
void srGERD::checkAllStateChanges()
{
    if (dirty != 0) {
        checkDrawStateChanges();
        checkFrameStateChanges();
        checkViewStateChanges();
    }
}

// FUNCTION: SURRENDER 0x10023540
srGERD::e_matrixMode srGERD::getMatrixMode() const
{
    return state.matrix_mode;
}

// FUNCTION: SURRENDER 0x10023350
glm::mat4 srGERD::getMatrix()
{
    return getMatrix(state.matrix_mode);
}

// FUNCTION: SURRENDER 0x10021380
float srGERD::getMaxModelViewScale()
{
    checkViewStateChanges();
    return state.max_modelview_scale;
}

srModelViewScope::srModelViewScope(srGERD& renderer) : renderer(&renderer)
{
    renderer.matrixMode(srGERD::MATRIX_MODELVIEW);
    if (!renderer.pushMatrix()) throw std::overflow_error("Model-view matrix stack is full");
}

srModelViewScope::srModelViewScope(srModelViewScope&& other) noexcept
    : renderer(std::exchange(other.renderer, nullptr))
{
}

srModelViewScope::~srModelViewScope()
{
    if (renderer) {
        renderer->matrixMode(srGERD::MATRIX_MODELVIEW);
        renderer->popMatrix();
    }
}

// FUNCTION: SURRENDER 0x10022C50
void srGERD::multMatrix(const glm::mat4& matrix)
{
    assertContext();
    state.matrix_current[state.matrix_mode] *= matrix;
    setMatrixDirty();
}

// FUNCTION: SURRENDER 0x10022F10
void srGERD::multMatrix(const glm::dmat4& matrix)
{
    multMatrix(glm::mat4(matrix));
}

// FUNCTION: SURRENDER 0x10022A70
void srGERD::loadMatrix(const glm::dmat4& matrix)
{
    loadMatrix(glm::mat4(matrix));
}

// FUNCTION: SURRENDER 0x10022B20
void srGERD::loadMatrix(const glm::mat4& matrix)
{
    state.matrix_current[state.matrix_mode] = matrix;
    setMatrixDirty();
}

// FUNCTION: SURRENDER 0x10022B50
void srGERD::loadMatrix(const glm::dmat3& matrix)
{
    loadMatrix(glm::mat4(matrix));
}

// FUNCTION: SURRENDER 0x10022BD0
void srGERD::loadMatrix(const glm::mat3& matrix)
{
    loadMatrix(glm::mat4(matrix));
}

// FUNCTION: SURRENDER 0x100228C0
void srGERD::scale(double factor)
{
    scale(factor, factor, factor);
}

// FUNCTION: SURRENDER 0x10022730
void srGERD::rotate(double angle, double x, double y, double z)
{
    rotate(angle, glm::vec3(x, y, z));
}

// FUNCTION: SURRENDER 0x10021DF0
void srGERD::frustum(const Frustum& bounds)
{
    if (bounds.near_plane <= 0 || bounds.far_plane <= 0) return;
    auto& current = state.matrix_current[state.matrix_mode];
    current *= glm::frustum(float(bounds.left), float(bounds.right), float(bounds.bottom),
                            float(bounds.top), float(bounds.near_plane), float(bounds.far_plane));
    setMatrixDirty();
}

// FUNCTION: SURRENDER 0x10021FF0
void srGERD::frustum(double left, double right, double bottom, double top, double near_plane,
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

// FUNCTION: SURRENDER 0x1001B820
void srGERD::applyClipPlaneChanges()
{
    const auto rows = glm::transpose(state.matrix_current[MATRIX_PROJECTION]);
    state.clip_planes[0] = rows[3] + rows[0];
    state.clip_planes[1] = rows[3] - rows[0];
    state.clip_planes[2] = rows[3] + rows[1];
    state.clip_planes[3] = rows[3] - rows[1];
    state.clip_planes[4] = rows[3] + rows[2];
    state.clip_planes[5] = rows[3] - rows[2];
    for (int i = 0; i < 6; ++i)
        state.clip_planes[i] = srPlaneMath::normalized(state.clip_planes[i]);
    dirty &= ~DIRTY_CLIP_PLANES;
}

// FUNCTION: SURRENDER 0x1001C010
void srGERD::pushClipPlane(const glm::vec4& plane, e_clipMode mode)
{
    if ((w8_ulong)state.clip_plane_count < 0x1a) {
        checkClipPlaneChanges();
        checkViewStateChanges();
        w8_ulong bit = 1UL << (state.clip_plane_count + 6);
        state.clip_planes[state.clip_plane_count + 6] =
            glm::transpose(state.inverse_modelview) * srPlaneMath::normalized(plane);
        state.clip_mask |= bit;
        if (mode == 1) {
            state.clip_mode1_mask |= bit;
        } else {
            state.clip_mode1_mask &= ~bit;
        }
    }
    state.clip_plane_count += 1;
}

// FUNCTION: SURRENDER 0x1001C1F0
void srGERD::popClipPlane()
{
    if (state.clip_plane_count != 0) {
        state.clip_plane_count -= 1;
        w8_ulong mask = ~(1UL << (state.clip_plane_count + 6));
        state.clip_mask &= mask;
        state.clip_mode1_mask &= mask;
    }
}

// FUNCTION: SURRENDER 0x1001C230
srGERD::ClipPlanes srGERD::getClipPlanes()
{
    checkClipPlaneChanges();
    ClipPlanes planes{};
    for (int plane = 0; plane != 6; ++plane) {
        planes.planes[plane] = state.clip_planes[plane];
    }
    if ((state.clip_mask & 0xffffffc0) != 0) {
        for (w8_ulong index = 6; index < 0x20; ++index) {
            if ((state.clip_mask & (1UL << index)) != 0) {
                planes.planes[index] = state.clip_planes[index];
            }
        }
    }
    planes.mask = state.clip_mask;
    return planes;
}

// FUNCTION: SURRENDER 0x1001C5C0
void srGERD::pushEnvironment()
{
    if (environment_state.environment_depth < 0x10) {
        environment_state.environment_stack[environment_state.environment_depth] = environment;
        environment_state.environment_depth += 1;
    }
}

// FUNCTION: SURRENDER 0x1001C600
void srGERD::popEnvironment()
{
    if (environment_state.environment_depth != 0) {
        environment_state.environment_depth -= 1;
        environment = environment_state.environment_stack[environment_state.environment_depth];
    }
}

// FUNCTION: SURRENDER 0x1001C4D0
void srGERD::setEnvironmentRange(float minimum, float maximum)
{
    environment.x = minimum;
    environment.y = maximum;
}

// FUNCTION: SURRENDER 0x1001C510
void srGERD::setEnvironmentScaleFactor(float scale, float inverse_scale)
{
    if (0.0f < scale) {
        if (1.0f <= scale) {
            scale = 1.0f;
        }
    } else {
        scale = 0.0f;
    }
    environment.z = scale;
    if (0.0f < inverse_scale) {
        if (inverse_scale < 1.0f) {
            environment.w = inverse_scale;
            return;
        }
        environment.w = 1.0f;
        return;
    }
    environment.w = 0.0f;
}

// FUNCTION: SURRENDER 0x1001CE00
void srGERD::pushVertexProcessor(srVertexProcessor& processor)
{
    vertex_processors.push_back(&processor);
}

// FUNCTION: SURRENDER 0x1001CEA0
void srGERD::popVertexProcessor()
{
    if (!vertex_processors.empty()) {
        vertex_processors.pop_back();
    }
}

/* Each component clamps through (0,1): strictly positive keeps the value, 1.0 or above saturates,
   anything else becomes 0. The color only updates (and dirties state) when a clamped component
   differs. */
// FUNCTION: SURRENDER 0x1001C6B0
void srGERD::setFogColor(const glm::vec4& color)
{
    const auto clamped = glm::clamp(color, 0.0f, 1.0f);
    if (clamped != fog_color) {
        flushImmediateRenderers();
        fog_color = clamped;
        dirty |= DIRTY_FOG_COLOR;
    }
}

// FUNCTION: SURRENDER 0x1001C680
glm::vec4 srGERD::getFogColor() const
{
    return fog_color;
}

// FUNCTION: SURRENDER 0x1001DB20
w8_ulong_ptr srGERD::getPickKey() const
{
    return pick.pick_key;
}

// FUNCTION: SURRENDER 0x1001DB30
srGERD::e_visibility srGERD::testBoundingSphere(const glm::vec3& center, float radius)
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

// FUNCTION: SURRENDER 0x1001DD60
srGERD::e_visibility srGERD::testBoundingBox(const glm::vec3& minimum, const glm::vec3& maximum)
{
    statistics.box_tests++;
    checkViewStateChanges();
    glm::mat4 combined = state.matrix_current[MATRIX_PROJECTION];
    combined = combined * state.matrix_current[MATRIX_MODELVIEW];
    if (srRender::testBoundingBox(combined, minimum, maximum)) {
        statistics.box_visible++;
        return static_cast<e_visibility>(1);
    }
    return VISIBILITY_OUTSIDE;
}

// FUNCTION: SURRENDER 0x100235F0
glm::vec4 srGERD::getEyeSpaceLocation(const glm::vec3& object_location)
{
    checkViewStateChanges();
    return state.matrix_current[MATRIX_MODELVIEW] * glm::vec4(object_location, 1);
}

// FUNCTION: SURRENDER 0x1001DDF0
srBoundingSphere srGERD::eyeSpaceBounds(const srBoundingSphere& sphere)
{
    const auto center = glm::vec3(getEyeSpaceLocation(sphere.center));
    return {center, sphere.radius * state.max_modelview_scale};
}

// FUNCTION: SURRENDER 0x1001B570
void srGERD::applyDrawStateChanges()
{
    if ((dirty & DIRTY_FOG_COLOR) != 0) {
        device.gpu.setFogColor(fog_color);
    }
    if ((dirty & DIRTY_SHADER) != 0) {
        device.gpu.setShader(shader);
        statistics.shader_sets++;
    }
    if ((dirty & DIRTY_TEXTURE0) != 0) {
        changeTexture(texture_iface[0], 0, 1);
    }
    if (((dirty & DIRTY_TEXTURE1) != 0) && (1 < SDLGPUDevice::max_texture_stages)) {
        changeTexture(texture_iface[1], 1, 1);
    }
    if ((dirty & DIRTY_CULLING) != 0) {
        switch (state.cull_mode) {
        case CULL_BACK:
            device.gpu.setCullMode(state.winding != 0 ? SDLGPUDevice::CULL_FRONT
                                                      : SDLGPUDevice::CULL_BACK);
            break;
        case CULL_FRONT:
            device.gpu.setCullMode(state.winding == 0 ? SDLGPUDevice::CULL_FRONT
                                                      : SDLGPUDevice::CULL_BACK);
            break;
        case CULL_NONE:
            device.gpu.setCullMode(SDLGPUDevice::CULL_NONE);
            break;
        }
    }
    if ((dirty & DIRTY_POLYGON_OFFSET) != 0) {
        device.gpu.setPolygonOffset(polygon_offset);
    }
    dirty &= ~DIRTY_DRAW_STATE;
    statistics.draw_state_applies++;
}

// FUNCTION: SURRENDER 0x100281B0
void srGERD::setTextureParameters(w8_ulong stage, const srTexture::Parameters& requested)
{
    const auto mag = requested.mag == srTexture::FILTER_DEFAULT ? texture_state.default_mag_filter
                                                                : requested.mag;
    const auto min = requested.min == srTexture::FILTER_DEFAULT ? texture_state.default_min_filter
                                                                : requested.min;
    const auto mip = requested.mipmap == srTexture::MIPMAP_DEFAULT ? texture_state.default_mipmap
                                                                   : requested.mipmap;
    const SDLGPUDevice::TexParms resolved{
        mag >= srTexture::FILTER_GOOD ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST,
        min >= srTexture::FILTER_GOOD ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST,
        mip == srTexture::MIPMAP_BEST ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR
                                      : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        requested.wrap_s == srTexture::WRAP_CLAMP ? SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE
                                                  : SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        requested.wrap_t == srTexture::WRAP_CLAMP ? SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE
                                                  : SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        requested.mipmap_bias,
        mip == srTexture::MIPMAP_NONE ? 0.0f : 16.0f};
    if (texture_parms[stage] == resolved) return;
    texture_parms[stage] = resolved;
    ++statistics.texture_parameter_sets;
    device.gpu.setTextureParameters(stage, resolved);
}

// FUNCTION: SURRENDER 0x10029400
void srGERD::changeTexture(srTexture* texture, w8_ulong stage, int apply_parms)
{
    if ((state_flags & STATE_CLOSING_WINDOW) || stage >= SDLGPUDevice::max_texture_stages) return;
    texture = texture ? texture->resolve() : nullptr;
    if (!texture) texture = &fallback_texture;
    const auto entry = textures.find(texture);
    auto* found = entry == textures.end() ? createNewTexture(texture) : &entry->second;
    if (!apply_parms) return;
    if (texture_slots[stage] != found) {
        texture_slots[stage] = found;
        device.gpu.bindTexture(stage, found->device.gpu);
        ++statistics.texture_binds;
    }
    setTextureParameters(stage, texture->getParameters());
}

namespace {

w8_ulong nextTextureDimension(w8_ulong value)
{
    if (value < 2) {
        return 1;
    }
    w8_ulong v = value - 1;
    if (v == 0) {
        return 1;
    }
    unsigned char shift = 0;
    if ((v & 0xffff0000) != 0) {
        shift = 16;
        v >>= 16;
    }
    if ((v >> 8) != 0) {
        shift += 8;
        v >>= 8;
    }
    if ((v & 0xf0) != 0) {
        shift += 4;
        v >>= 4;
    }
    if ((v & 0xc) != 0) {
        shift += 2;
        v >>= 2;
    }
    if ((v & 2) != 0) {
        shift += 1;
    }
    return 1UL << (shift + 1U & 0x1f);
}

} // namespace

// FUNCTION: SURRENDER 0x10019080

// FUNCTION: SURRENDER 0x1001C3E0
void srGERD::initLights()
{
    ambient_light = glm::vec4(0.2f, 0.2f, 0.2f, 1.0f);
}

// FUNCTION: SURRENDER 0x10021BE0
void srGERD::initMatrices()
{
    state.matrix_mode = MATRIX_MODELVIEW;
    for (w8_long i = 0; i < 2; i++) {
        state.matrix_current[i] = glm::mat4(1);
        state.matrix_stacks[i].depth = 0;
    }
    state.normal_matrix = glm::mat4(1);
    state.inverse_modelview = glm::mat4(1);
}

// FUNCTION: SURRENDER 0x100191E0
srGERD::e_error srGERD::createContext(SDL_Window* window)
{
    deleteContext();
    if (window == nullptr) {
        return ERROR_INVALID_WHANDLE;
    }
    if (!device.gpu.createContext(window)) {
        return ERROR_CONTEXT_CREATION_FAILED;
    }
    device.window = window;
    resetStatistics();
    state_flags |= STATE_CONTEXT_CREATED;
    return ERROR_NONE;
}

// FUNCTION: SURRENDER 0x100192A0
void srGERD::deleteContext()
{
    if ((state_flags & STATE_CONTEXT_CREATED) != 0) {
        closeWindow(static_cast<e_closeHint>(0));
        closeTexCache();
        device.gpu.deleteContext();
        state_flags &= ~STATE_CONTEXT_CREATED;
        device.window = 0;
    }
}

// FUNCTION: SURRENDER 0x10019EB0
void srGERD::deleteRenderers()
{
    for (auto& renderer : renderers) renderer.reset();
}

// FUNCTION: SURRENDER 0x10020DF0
SDL_Surface* srGERD::lockBuffer()
{
    if (!isWindowOpen()) {
        setError(ERROR_WINDOW_NOT_OPEN);
        return nullptr;
    }
    flush();
    checkViewStateChanges();
    auto* surface = device.gpu.lockBuffer();
    const SDL_Rect clip{int(state.scissor.left), int(state.scissor.top),
                        int(state.scissor.right - state.scissor.left),
                        int(state.scissor.bottom - state.scissor.top)};
    if (surface) SDL_SetSurfaceClipRect(surface, &clip);
    return surface;
}

void srGERD::unlockBuffer(bool modified)
{
    device.gpu.unlockBuffer(modified);
}

// FUNCTION: SURRENDER 0x1001A5F0
void srGERD::closeWindow(e_closeHint hint)
{
    if (isWindowOpen() != 0) {
        state_flags |= STATE_CLOSING_WINDOW;
        flush();
        deleteRenderers();
        invalidateResidentTextures();
        closeTexCache();
        device.gpu.closeWindow();
        resetStatistics();
        device.open_info = {};
        state_flags &= ~STATE_WINDOW_OPEN;
        active = nullptr;
        state_flags &= ~STATE_CLOSING_WINDOW;
        shader = srShader();
        texture_iface[0] = 0;
        texture_iface[1] = 0;
        decltype(pick_vertices){}.swap(pick_vertices);
    }
}

// FUNCTION: SURRENDER 0x1001CD10

// FUNCTION: SURRENDER 0x1001A120
srGERD::e_error srGERD::openWindow()
{
    if ((state_flags & STATE_CONTEXT_CREATED) == 0) {
        return ERROR_NO_CONTEXT;
    }
    int width = 0, height = 0;
    if (device.window) SDL_GetWindowSizeInPixels(device.window, &width, &height);
    return openWindow(width, height);
}

// FUNCTION: SURRENDER 0x1001A160
srGERD::e_error srGERD::openWindow(w8_long width, w8_long height)
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

// FUNCTION: SURRENDER 0x1001A290
srGERD::e_error srGERD::openWindowInternal(const OpenInfo& info)
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
        if (SDLGPUDevice::max_back_buffer_width < (w8_ulong)info.width ||
            SDLGPUDevice::max_back_buffer_height < (w8_ulong)info.height) {
            return ERROR_INVALID_VALUE;
        }
        device.open_info = {};
        if (device.gpu.openWindow(info.width, info.height)) {
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
            shader = srShader();
            for (w8_ulong stage = 0; stage < SDLGPUDevice::max_texture_stages; ++stage) {
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
                clear(SDLGPUDevice::BUFFER_COLOR | SDLGPUDevice::BUFFER_DEPTH |
                      SDLGPUDevice::BUFFER_STENCIL);
                flipFrame();
                flush();
            }
            return ERROR_NONE;
        }
    }
    return ERROR_WINDOW_OPEN_FAILED;
}

// FUNCTION: SURRENDER 0x10028460
void srGERD::closeTexCache()
{
    resetCurrentTexPointers();
    texture_default = nullptr;
    textures.clear();
}

void srGERD::initTexCache()
{
    closeTexCache();
    texture_default = createNewTexture(&fallback_texture);
}

// FUNCTION: SURRENDER 0x10028B80
void srGERD::evaluateTextureDimensions(TextureCacheData& device,
                                       const srTexture::Dimensions& dimensions)
{
    w8_ulong min_dim = SDLGPUDevice::texture_min_dim;
    const auto reduction = texture_reduction;
    w8_ulong width = nextTextureDimension(dimensions.width) >> (reduction & 0x1f);
    w8_ulong height = nextTextureDimension(dimensions.height) >> (reduction & 0x1f);
    w8_ulong device_width = min_dim;
    if (min_dim <= width && width <= SDLGPUDevice::texture_max_dim) {
        device_width = width;
    }
    w8_ulong device_height = min_dim;
    if (min_dim <= height && height <= SDLGPUDevice::texture_max_dim) {
        device_height = height;
    }
    int unbalanced = 0;
    if (device_height < device_width) {
        while (SDLGPUDevice::texture_max_aspect < device_width / device_height &&
               device_height < SDLGPUDevice::texture_max_dim) {
            device_height *= 2;
        }
        unbalanced = device_height < device_width;
    }
    if (unbalanced == 0 && device_height != device_width) {
        while (SDLGPUDevice::texture_max_aspect < device_height / device_width &&
               device_width < SDLGPUDevice::texture_max_dim) {
            device_width *= 2;
        }
    }
    device.width = device_width;
    device.height = device_height;
    device.first_level = 0;
    device.last_level = 0;
    if (dimensions.generate_mipmaps && SDLGPUDevice::texture_min_dim < device_width) {
        do {
            if (device_height <= SDLGPUDevice::texture_min_dim) {
                return;
            }
            ++device.last_level;
            device_width >>= 1;
            device_height >>= 1;
        } while (SDLGPUDevice::texture_min_dim < device_width);
    }
}

// FUNCTION: SURRENDER 0x10028E60
// FUNCTION: SURRENDER 0x10029090
srGERD::Texture* srGERD::createNewTexture(srTexture* texture)
{
    auto [entry, inserted] = textures.try_emplace(texture, device.gpu);
    auto* result = &entry->second;
    if (!inserted) return result;
    try {
        evaluateTextureDimensions(result->device, texture->getDimensions());
        SDL_Surface* source = texture->getSurfacePtr();
        SDLSurfaceOwner region{nullptr, SDL_DestroySurface};
        if (source && texture->source_region.w > 0 && texture->source_region.h > 0) {
            const auto& rect = texture->source_region;
            region = CreateCpuStorage(rect.w, rect.h, source->format);
            if (!SDL_BlitSurface(source, &rect, region.get(), nullptr))
                throw std::runtime_error(SDL_GetError());
            source = region.get();
        }
        result->device.gpu = device.gpu.createTexture(result->device.width, result->device.height,
                                                      result->device.last_level + 1);
        if (!result->device.gpu.handle) throw std::runtime_error("Cannot create GPU texture");
        SDLSurfaceOwner previous{nullptr, SDL_DestroySurface};
        for (unsigned level = 0; source && level <= result->device.last_level; ++level) {
            const auto w = result->device.width >> level;
            const auto h = result->device.height >> level;
            auto destination = CreateCpuStorage(w, h, result->pixel_format);
            StretchCpuSurface(*source, *destination, SDL_SCALEMODE_LINEAR);
            const auto* data = static_cast<const unsigned char*>(destination->pixels);
            device.gpu.uploadTexture(result->device.gpu, level, {0, 0, w, h},
                                     {data, std::size_t(destination->pitch) * h},
                                     destination->pitch);
            previous = std::move(destination);
            source = previous.get();
        }
        ++statistics.textures_created;
        return result;
    } catch (...) {
        textures.erase(entry);
        throw;
    }
}

// FUNCTION: SURRENDER 0x10017D00

// FUNCTION: SURRENDER 0x10017DD0

// FUNCTION: SURRENDER 0x10029600
// FUNCTION: SURRENDER 0x1001AFD0

// FUNCTION: SURRENDER 0x1001AFF0

// GLOBAL: SURRENDER 0x100993A4

// FUNCTION: SURRENDER 0x10020DD0

// FUNCTION: SURRENDER 0x1001C870

// FUNCTION: SURRENDER 0x1001C8E0

// FUNCTION: SURRENDER 0x1001CBE0
void srGERD::setClearStencil(w8_ulong stencil)
{
    clear_state.clear_values.stencil = stencil;
}

// FUNCTION: SURRENDER 0x1001CFD0

// FUNCTION: SURRENDER 0x1001D010
srGERD::e_error srGERD::getError()
{
    e_error error = last_error;
    setError(ERROR_NONE);
    return error;
}

// FUNCTION: SURRENDER 0x1001D0C0

// FUNCTION: SURRENDER 0x1001D1E0

// FUNCTION: SURRENDER 0x1001BC30
void srGERD::setDataPtr(srRendererDefs::e_vertexArray index, w8_long components,
                        srRendererDefs::e_type type, w8_ulong stride, const void* values)
{
    vertex_arrays.components[index] = components;
    vertex_arrays.types[index] = type;
    vertex_arrays.strides[index] = stride;
    vertex_arrays.arrays[index] = values;
    vertex_arrays_dirty |= DIRTY_VERTEX_ARRAY_INFO;
}

// FUNCTION: SURRENDER 0x10020550
void srGERD::initClearColors()
{
    setClearColor(glm::vec4(0));
    setClearDepth(1.0);
    setClearStencil(0);
}

// FUNCTION: SURRENDER 0x1001D2E0
void srGERD::initView()
{
    state.depth_min = 0.0;
    state.depth_max = 1.0;
    state.clip_plane_count = 0;
    state.clip_mask = srRendererDefs::FRUSTUM_CLIP_MASK;
    state.clip_mode1_mask = 0;
    state.cull_mode = CULL_BACK;
    state.winding = static_cast<e_winding>(0);
}

// FUNCTION: SURRENDER 0x10027F60

// FUNCTION: SURRENDER 0x10068700
bool srRender::testBoundingBox(const glm::mat4& matrix, const glm::vec3& minimum,
                               const glm::vec3& maximum)
{
    std::array<glm::vec4, 8> corners;
    const srBoundingBox box{minimum, maximum};
    for (unsigned i = 0; i < corners.size(); ++i) {
        // This visibility interface enumerates x in bit 0 and z in bit 2.
        const auto corner = ((i & 1) << 2) | (i & 2) | ((i & 4) >> 2);
        corners[i] = matrix * glm::vec4(box.corner(corner), 1);
    }
    std::array<SRBYTE, 8> flags;
    clipFlags(flags, corners);
    SRBYTE outside = 0x3f;
    for (const auto flag : flags) outside &= flag;
    return outside == 0;
}

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "compat/compiler.h"
#include "glm/common.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int3.hpp"
#include "glm/ext/vector_uint4_sized.hpp"
#include "glm/fwd.hpp"
#include "glm/geometric.hpp"
#include "glm/matrix.hpp"
#include "glm/vector_relational.hpp"
#include "surrender/model_view_scope.h"
#include "surrender/picking_math.h"
#include "surrender/plane_math.h"
#include "surrender/srARGB.h"
#include "surrender/srBoundingBox.h"
#include "surrender/srBoundingSphere.h"
#include "surrender/srClip.h"
#include "surrender/srClock.h"
#include "surrender/srColorSurface.h"
#include "surrender/srColorSurfaceIFace.h"
#include "surrender/srCore.h"
#include "surrender/srDD.h"
#include "surrender/srFlags.h"
#include "surrender/srGERD.h"
#include "surrender/srMath.h"
#include "surrender/srModel.h"
#include "surrender/srPalette.h"
#include "surrender/srPixelConvert.h"
#include "surrender/srRendererDefs.h"
#include "surrender/srShader.h"
#include "surrender/srTextureIFace.h"
#include "surrender/srWindow.h"

class srModelInstance;
class srVertexProcessor;

// GLOBAL: SURRENDER 0x100A4780
srGERD* srGERD::first;

// GLOBAL: SURRENDER 0x100A4784
srGERD* srGERD::firstOpen;

// FUNCTION: SURRENDER 0x1001EEA0
srGERD::TexturePool::TexturePool() : count(0), free(nullptr) {}

srGERD::Texture* srGERD::TexturePool::allocate()
{
    if (free == 0) {
        w8_ulong chunk_count = count;
        if (chunk_count < 2) {
            chunk_count = 1;
        } else if (0xff < (w8_long)chunk_count) {
            chunk_count = 0x100;
        }
        chunks.emplace_back(chunk_count);
        Texture* chunk = chunks.back().data();
        free = chunk;
        Texture* link = chunk;
        for (w8_ulong index = chunk_count; index != 0; --index) {
            link->prev = link + 1;
            ++link;
        }
        chunk[chunk_count - 1].prev = 0;
    }
    Texture* texture = free;
    free = texture->prev;
    ++count;
    return texture;
}

void srGERD::TexturePool::release(Texture* texture)
{
    if (texture == 0) {
        return;
    }
    --count;
    texture->prev = free;
    free = texture;
    if (count == 0) {
        chunks.clear();
        free = nullptr;
    }
}

// FUNCTION: SURRENDER 0x10019A00 SYMBOL
// RECOMP: ??0MatrixStack@srGERD@@QAE@XZ
srGERD::MatrixStack::MatrixStack() : depth(0) {}

class srGERD::LockSurface : public srColorSurfaceIFace {
public:
    LockSurface(srGERD* gerd, const srPixelConvert::PixelFormat& format);
    LockSurface& operator=(const LockSurface&) = default;
    w8_long getDataSize() override { return 0; }

    /* Cannot construct a LockSurface without the owning GERD and pixel format. */
    virtual void getPixelColumn(w8_ulong* pixels, w8_long x, w8_long y0, w8_long y1) override;
    virtual void setPixelColumn(const w8_ulong* pixels, w8_long x, w8_long y0, w8_long y1) override;
    virtual void setHLine(w8_long y, w8_long x0, w8_long x1, w8_ulong pixel) override;
    virtual void getPixelRow(w8_ulong* pixels, w8_long y, w8_long x0, w8_long x1) override;
    virtual void setPixelRow(const w8_ulong* pixels, w8_long y, w8_long x0, w8_long x1) override;
    virtual void getPixelRowRaw(void* pixels, w8_long y, w8_long x0, w8_long x1) override;
    virtual void setPixelRowRaw(const void* pixels, w8_long y, w8_long x0, w8_long x1) override;
    void setScissor(w8_ulong left, w8_ulong top, w8_ulong right, w8_ulong bottom);

private:
    void transferPixels(void* pixels, w8_long fixed, w8_long begin, w8_long end, bool vertical,
                        bool write, bool raw = false);
    srGERD* gerd;
    w8_ulong left;
    w8_ulong top;
    w8_ulong right;
    w8_ulong bottom;
    srColorSurface scratch;

public:
    /* Set by lockBuffer when the locked pixel format is 32-bit ARGB. */
    unsigned char argb32;

private:
};

// FUNCTION: SURRENDER 0x10019320
srGERD::srGERD(srDD* device)
    : dirty(0), state_flags(0), device{}, state{}, frame_statistics{}, statistics{},
      lock_surface(nullptr), buffer_lock_count(0), clear_state{}, texture_slots{}, texture_parms{},
      texture_state{}, polygon_mode(POLYGON_FILL), polygon_offset(0), texture_deleted(nullptr),
      texture_head(nullptr), texture_default(nullptr), texture_cache_used(0), texture_cache_size(0),
      texture_sequence(0), texture_reduction(0), texture_hash_enabled(false), vertex_arrays_dirty(0)
{
    owner_thread = std::this_thread::get_id();
    this->device.dd = device;
    this->device.window = 0;
    this->device.back_buffer_type = static_cast<e_backBuffer>(0);
    vertex_arrays_dirty = 0xffffffff;
    vertex_arrays.clip.value = srRendererDefs::FRUSTUM_CLIP_MASK;
    vertex_arrays.count = 0;
    vertex_arrays.mask.value = 0;
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
    state.modelview_scale_type = static_cast<srRendererDefs::e_scaleType>(0);
    state.max_modelview_scale = 0.0f;
    for (auto& color : global_palette) {
        color.blue = color.green = color.red = color.alpha = 0;
    }
    pick.pick_key = 0;
    exclusion_mask = 0;
    enable_flags.value = 0;
    enable_flags.set(ENABLE_AUTO_FLIP, 1);
    enable_flags.set(ENABLE_CLEAR_ON_OPEN, 1);
    state_flags = 0;
    dirty = 0xffffffff;
    setError(ERROR_NONE);
    display.gamma = glm::vec3(1.0f);
    display.swap_interval = 1;
    display.antialias = ANTIALIAS_NONE;
    fog_color = glm::vec4(0);
    shader = srShader();
    texture_iface[0] = 0;
    texture_iface[1] = 0;
    enable_flags.set(ENABLE_POSITIONAL_0, 1);
    this->device.hints[0] = static_cast<e_hintMode>(0);
    initTextureParameterMatrix();
    this->device.driver_info = getDD()->getDriverInfo();
    initClearColors();
    initLights();
    initMatrices();
    initView();
    srDD::e_driverID driver_id = getDriverID();
    srGERD* node = getFirst();
    srGERD* previous = 0;
    if (node != 0) {
        do {
            if (driver_id <= node->getDriverID()) {
                break;
            }
            previous = node;
            node = node->getNext();
        } while (node != 0);
    }
    if (previous != 0) {
        prev = previous;
        next = previous->getNext();
    } else {
        prev = 0;
        next = getFirst();
    }
    if (prev != 0) {
        prev->next = this;
    } else {
        first = this;
    }
    if (next != 0) {
        next->prev = this;
    }
    prev_open = 0;
    next_open = 0;
    setName(this->device.driver_info.name);
}

// FUNCTION: SURRENDER 0x10019F40
srGERD::~srGERD()
{
    deleteRenderers();
    deleteContext();
    delete device.dd;
    if (prev != 0) {
        prev->next = next;
    }
    if (next != 0) {
        next->prev = prev;
    }
    if (this == first) {
        first = next;
    }
}

// FUNCTION: SURRENDER 0x1001B010
srGERD& srGERD::operator=(const srGERD& other)
{
    return *this;
}

// FUNCTION: SURRENDER 0x10017920
srDD* srGERD::getDD() const
{
    statistics.device_calls++;
    return device.dd;
}

// FUNCTION: SURRENDER 0x10017A50
void srGERD::setTextureReduction(w8_long reduction)
{
    std::lock_guard access(state_section);
    texture_reduction = std::clamp(reduction, w8_long{0}, w8_long{7});
}

// FUNCTION: SURRENDER 0x10017AC0
void srGERD::setTexture(srTextureIFace* texture, w8_ulong layer)
{
    std::lock_guard access(state_section);
    if (layer < device.info.max_texture_stages && texture_iface[layer] != texture) {
        texture_iface[layer] = texture;
        changeTexture(texture, layer, 0);
        dirty |= 1 << (layer + DIRTY_TEXTURE_SHIFT);
    }
}

// FUNCTION: SURRENDER 0x10017F30
w8_ulong srGERD::getTextureCacheSize() const
{
    std::lock_guard access(state_section);
    return texture_cache_size;
}

// FUNCTION: SURRENDER 0x10017F50
w8_ulong srGERD::getTextureCacheUsed() const
{
    std::lock_guard access(state_section);
    return texture_cache_used;
}

// FUNCTION: SURRENDER 0x1001AAF0
srGERD::Statistics srGERD::getStatistics()
{
    std::lock_guard access(renderers_section);
    this->statistics.input_triangles = 0;
    this->statistics.input_vertices = 0;
    this->statistics.triangle_chunks = 0;
    this->statistics.sorted_triangles = 0;
    this->statistics.clipped_triangles = 0;
    for (auto& owned : renderers) {
        auto* entry = &owned;
        Renderer* renderer = entry->renderer.get();
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
    result.elapsed = srCore.getTimer()->seconds() - result.elapsed;
    return result;
}

// FUNCTION: SURRENDER 0x1001ACD0
void srGERD::resetStatistics()
{
    statistics = {};
    drainRenderers(RendererDrain::statistics);
    statistics.elapsed = srCore.getTimer()->seconds();
    frame_statistics = statistics;
}

// FUNCTION: SURRENDER 0x1001ADD0
void srGERD::toggle(e_enable option)
{
    enable_flags.value ^= 1UL << option;
    if (option == ENABLE_POSITIONAL_0) {
        dirty |= DIRTY_FRAME_ENABLE;
        return;
    }
}

// FUNCTION: SURRENDER 0x1001AE60
void srGERD::popEnable()
{
    w8_ulong flags = 0;
    if (environment_state.enable_depth != 0) {
        environment_state.enable_depth -= 1;
        flags = environment_state.enable_stack[environment_state.enable_depth].value;
    }
    if (flags != enable_flags.value) {
        for (e_enable option = static_cast<e_enable>(0); static_cast<int>(option) < 7;
             option = static_cast<e_enable>(static_cast<int>(option) + 1)) {
            w8_ulong bit = 1UL << option;
            if (((flags & bit) != 0) != ((enable_flags.value & bit) != 0)) {
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

// FUNCTION: SURRENDER 0x1001AEE0
void srGERD::setGamma(const glm::vec3& gamma)
{
    const auto adjusted = glm::max(gamma, glm::vec3(0));
    if (!(adjusted == display.gamma)) {
        display.gamma = adjusted;
        dirty |= DIRTY_GAMMA;
    }
}

// FUNCTION: SURRENDER 0x1001AFB0
void srGERD::setAntiAlias(e_antiAlias mode)
{
    if (mode != display.antialias) {
        display.antialias = mode;
        dirty |= DIRTY_ANTIALIAS;
    }
}

// FUNCTION: SURRENDER 0x1001BE70
void srGERD::setClipState(srFlags<srRendererDefs::e_clip> state)
{
    if (state.value != vertex_arrays.clip.value) {
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
        getDD()->setVertexArrayInfo(&vertex_arrays);
        vertex_arrays_dirty &= ~DIRTY_VERTEX_ARRAY_INFO;
    }
}

// FUNCTION: SURRENDER 0x1001CC10
void srGERD::drawArrays(srRendererDefs::e_primitive primitive, w8_long first, w8_ulong count)
{
    if ((enable_flags.value & 4) == 0) {
        prepareDrawState();
        getDD()->drawArrays(primitive, first, count);
        statistics.draw_calls++;
    }
}

// FUNCTION: SURRENDER 0x1001CC90
void srGERD::drawElements(srRendererDefs::e_primitive primitive, w8_ulong count,
                          srRendererDefs::e_indexType type, const void* indices)
{
    if ((enable_flags.value & 4) == 0) {
        prepareDrawState();
        getDD()->drawElements(primitive, {static_cast<const w8_ulong*>(indices), count});
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

// FUNCTION: SURRENDER 0x1001CF80
srGERD* srGERD::getNext() const
{
    return next;
}

// FUNCTION: SURRENDER 0x1001CF50
srGERD* srGERD::getFirstOpen()
{
    return firstOpen;
}

// FUNCTION: SURRENDER 0x1001CF90
srGERD* srGERD::getNextOpen() const
{
    return next_open;
}

void srGERD::unbindTexture(const srTextureIFace* texture)
{
    for (srGERD* gerd = getFirstOpen(); gerd != 0; gerd = gerd->getNextOpen()) {
        std::lock_guard access(gerd->state_section);
        for (auto*& bound : gerd->texture_iface) {
            if (bound == texture) {
                bound = 0;
            }
        }
    }
}

// FUNCTION: SURRENDER 0x10018330
void srGERD::invalidateTexture(srTextureIFace* texture)
{
    std::lock_guard access(state_section);
    if (texture != 0) {
        invalidateTextureByFrameHandle(texture->getTextureFrameHandle());
    }
}

// FUNCTION: SURRENDER 0x100281E0
void srGERD::invalidateTexture(Texture& texture)
{
    Texture* candidate = &texture;
    if (candidate != 0 && candidate != texture_default) {
        markTextureAsDeleted(texture);
    }
}

// FUNCTION: SURRENDER 0x10028050
void srGERD::invalidateResidentTexture(Texture& texture)
{
    if (texture.device.gpu.handle != 0) {
        if (texture.surface_data.empty() || std::this_thread::get_id() != owner_thread) {
            invalidateTexture(texture);
        } else {
            getDD()->deleteTexture(texture.device.gpu);
            texture.device.gpu.handle = 0;
            texture.device.gpu.size = 0;
            for (w8_ulong stage = 0; stage < device.info.max_texture_stages; ++stage) {
                if (texture_slots[stage] == &texture) {
                    texture_slots[stage] = 0;
                }
            }
        }
        dirty |= DIRTY_TEXTURE0;
        dirty |= DIRTY_TEXTURE1;
    }
}

// FUNCTION: SURRENDER 0x100280F0
void srGERD::resetCurrentTexPointers()
{
    for (w8_ulong stage = 0; stage < device.info.max_texture_stages; ++stage) {
        texture_iface[stage] = 0;
        texture_slots[stage] = 0;
    }
    dirty |= DIRTY_TEXTURE0;
    dirty |= DIRTY_TEXTURE1;
}

// FUNCTION: SURRENDER 0x10028150
void srGERD::markTextureAsDeleted(Texture& texture)
{
    if (texture.device.deleted == 0) {
        if (texture.prev != 0) {
            texture.prev->next = texture.next;
        }
        if (texture.next != 0) {
            texture.next->prev = texture.prev;
        }
        if (&texture == texture_head) {
            texture_head = texture.next;
        }
        texture.prev = 0;
        Texture* previous = texture_deleted;
        texture.next = previous;
        if (previous != 0) {
            previous->prev = &texture;
        }
        texture_deleted = &texture;
        texture.device.deleted = 1;
    }
}

// FUNCTION: SURRENDER 0x10018390
void srGERD::invalidateTextureByFrameHandle(w8_ulong handle)
{
    std::lock_guard access(state_section);
    if (handle != 0 && texture_hash_enabled) {
        const auto entry = texture_lookup.find(handle);
        Texture* texture = entry == texture_lookup.end() ? nullptr : entry->second;
        if (texture != 0) {
            invalidateTexture(*texture);
        }
    }
}

// FUNCTION: SURRENDER 0x10017BC0
void srGERD::invalidateResidentTextures()
{
    flushImmediateRenderers();
    std::lock_guard access(state_section);
    Texture* texture = texture_head;
    while (texture != 0) {
        Texture* next = texture->next;
        invalidateResidentTexture(*texture);
        texture = next;
    }
    resetCurrentTexPointers();
}

// FUNCTION: SURRENDER 0x10017C40
void srGERD::invalidateResidentTexture(srTextureIFace* texture)
{
    flushImmediateRenderers();
    std::lock_guard access(state_section);
    if (texture != 0) {
        w8_ulong handle = texture->getTextureFrameHandle();
        const auto entry = texture_lookup.find(handle);
        Texture* found = entry == texture_lookup.end() ? nullptr : entry->second;
        if (found != 0) {
            invalidateResidentTexture(*found);
        }
    }
}

// FUNCTION: SURRENDER 0x10017EC0
void srGERD::setTextureCacheSize(w8_ulong bytes)
{
    std::lock_guard access(state_section);
    texture_cache_size = bytes;
    if (bytes != 0 && bytes < texture_cache_used) {
        releaseTextureMemory(texture_cache_used - bytes);
    }
}

// FUNCTION: SURRENDER 0x100180A0
void srGERD::setTextureSubImage(srTextureIFace* texture, w8_long mipmap, w8_long x, w8_long y,
                                w8_long width, w8_long height)
{
    std::lock_guard access(state_section);
    if (isWindowOpen() == 0) {
        return;
    }
    w8_ulong handle = texture->getTextureFrameHandle();
    const auto entry = texture_lookup.find(handle);
    Texture* resident = entry == texture_lookup.end() ? nullptr : entry->second;
    if (resident != 0 && resident->device.first_level <= (w8_ulong)mipmap &&
        (w8_ulong)mipmap <= resident->device.last_level) {
        srTextureIFace::PartialRequest request;
        request.width = resident->device.width;
        request.height = resident->device.height;
        request.mipmap_level = mipmap;
        request.source_right = x + width;
        request.source_bottom = y + height;
        if (x < 0) {
            x = 0;
        }
        if (y < 0) {
            y = 0;
        }
        request.destination_x = x;
        request.destination_y = y;
        w8_ulong shift = (w8_ulong)((char)mipmap - (char)resident->device.first_level);
        w8_ulong level_width = request.width >> (shift & 0x1f);
        if (level_width == 0) {
            level_width = 1;
        }
        w8_ulong level_height = request.height >> (shift & 0x1f);
        if (level_height == 0) {
            level_height = 1;
        }
        if (level_width < (w8_ulong)request.source_right) {
            request.source_right = (w8_long)level_width;
        }
        if (level_height < (w8_ulong)request.source_bottom) {
            request.source_bottom = (w8_long)level_height;
        }
        w8_ulong bytes_per_pixel = static_cast<w8_ulong>(resident->pixel_format.pixel_size) + 1;
        w8_ulong pitch = bytes_per_pixel * level_width;
        if (resident->surface_data.empty()) {
            std::vector<unsigned char> staging(static_cast<std::size_t>(bytes_per_pixel) *
                                               level_height * level_width);
            auto destination = std::make_unique<srColorSurface>(
                resident->pixel_format, staging.data(), level_width, level_height, pitch);
            request.destination = destination.get();
            texture->getMipmapLevelPartial(request);
            destination.reset();
            getDD()->uploadTexture(resident->device.gpu, mipmap - resident->device.first_level,
                                   {static_cast<w8_ulong>(x), static_cast<w8_ulong>(y),
                                    static_cast<w8_ulong>(request.source_right - x),
                                    static_cast<w8_ulong>(request.source_bottom - y)},
                                   staging, pitch);
        } else {
            auto destination = std::make_unique<srColorSurface>(resident->pixel_format,
                                                                resident->mipPixels(mipmap).data(),
                                                                level_width, level_height, pitch);
            request.destination = destination.get();
            texture->getMipmapLevelPartial(request);
            if (resident->device.gpu.handle == 0) {
                uploadTexture(*resident);
            } else {
                getDD()->uploadTexture(resident->device.gpu, mipmap - resident->device.first_level,
                                       {static_cast<w8_ulong>(x), static_cast<w8_ulong>(y),
                                        static_cast<w8_ulong>(request.source_right - x),
                                        static_cast<w8_ulong>(request.source_bottom - y)},
                                       resident->mipPixels(mipmap), pitch);
            }
        }
    }
}

// FUNCTION: SURRENDER 0x10018480
void srGERD::invalidateTextureCache()
{
    std::lock_guard access(state_section);
    if (texture_hash_enabled) {
        Texture* texture = texture_head;
        while (texture != 0) {
            Texture* next = texture->next;
            invalidateTexture(*texture);
            texture = next;
        }
        resetCurrentTexPointers();
        texture_sequence = 0;
    }
}

// FUNCTION: SURRENDER 0x10018500
w8_ulong srGERD::getResidentTextureMemUsed() const
{
    std::lock_guard access(state_section);
    w8_ulong used = 0;
    for (Texture* texture = texture_head; texture != 0; texture = texture->next) {
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
        getDD()->setSwapInterval(display.swap_interval);
    }
    dirty &= ~DIRTY_FRAME_STATE;
    statistics.frame_state_count++;
}

// Wait with the registry lock released, then perform the requested operation
// on the same stable entry. Device submission and buffer resets are owner-only;
// statistics retains its existing cross-thread wait policy.
void srGERD::drainRenderers(RendererDrain action)
{
    if (action != RendererDrain::statistics && std::this_thread::get_id() != owner_thread) return;
    std::unique_lock access(renderers_section);
    for (auto& entry : renderers) {
        if (action == RendererDrain::immediate && entry.renderer->sorted != 0) continue;
        if (action == RendererDrain::sorted && entry.renderer->sorted != 1) continue;
        renderers_idle.wait(access, [&entry] { return !entry.busy; });
        if (action == RendererDrain::reset)
            entry.renderer->reset(0);
        else if (action == RendererDrain::statistics)
            entry.renderer->resetStatistics();
        else
            entry.renderer->submit();
    }
}

// FUNCTION: SURRENDER 0x10019A40
void srGERD::flushRenderers()
{
    if (std::this_thread::get_id() != owner_thread) return;
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

// FUNCTION: SURRENDER 0x10019BF0
srGERD::RendererEntry* srGERD::createRenderer(int sorted)
{
    std::lock_guard access(renderers_section);
    Renderer::Parameters parameters;
    parameters.gerd = this;
    parameters.sorted = sorted != 0;
    parameters.batch_limit = device.info.renderer_batch_limit;
    parameters.texture_stages = device.info.max_texture_stages;
    auto renderer = std::make_unique<Renderer>(parameters);
    renderers.push_front(RendererEntry{std::move(renderer)});
    renderers_idle.notify_all();
    return &renderers.front();
}

// FUNCTION: SURRENDER 0x10019CC0
srGERD::Renderer* srGERD::lockRenderer()
{
    int sorted = 0;
    if ((enable_flags.value & (1UL << ENABLE_SORTED_RENDERING)) != 0) {
        sorted = 1;
    }
    std::unique_lock access(renderers_section);
    for (;;) {
        flushNonBusyRenderers();
        for (auto& entry : renderers) {
            if (!entry.busy && entry.renderer->sorted == sorted) {
                return _lockRenderer(&entry);
            }
        }
        if (sorted == 0) {
            return _lockRenderer(createRenderer(0));
        }
        // A sorted batch is shared. Release the registry lock while its borrower finishes.
        renderers_idle.wait(access);
    }
}

// FUNCTION: SURRENDER 0x10019D70
srGERD::Renderer* srGERD::_lockRenderer(RendererEntry* entry)
{
    entry->busy = 1;
    return entry->renderer.get();
}

// FUNCTION: SURRENDER 0x10019D90
void srGERD::unlockRenderer(Renderer* renderer, int submit)
{
    std::lock_guard access(renderers_section);
    for (auto& owned : renderers) {
        auto* entry = &owned;
        if (entry->renderer.get() == renderer) {
            entry->busy = 0;
            renderers_idle.notify_all();
            if (std::this_thread::get_id() == owner_thread &&
                (submit != 0 || renderer->isBatchFull() != 0)) {
                renderer->submit();
            }
            return;
        }
    }
}

// FUNCTION: SURRENDER 0x10019E30
void srGERD::flushNonBusyRenderers()
{
    if (std::this_thread::get_id() == owner_thread) {
        std::lock_guard access(renderers_section);
        for (auto& owned : renderers) {
            auto* entry = &owned;
            if (entry->busy == 0 && entry->renderer->isBatchFull() != 0) {
                entry->renderer->submit();
            }
        }
    }
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
    if (srWindow::isWindow(device.window) == 0) {
        return ERROR_INVALID_WHANDLE;
    }
    if ((state_flags & STATE_FRAME_STARTED) == 0) {
        if ((enable_flags.value & (1UL << ENABLE_AUTO_FLIP)) != 0 &&
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
        getDD()->flushFrame();
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
    getDD()->flipFrame();
    statistics.frames += 1;
    state_flags |= STATE_FRAME_FLIPPED;
    frame_statistics = getStatistics();
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
    getDD()->flushFrame();
}

// FUNCTION: SURRENDER 0x1001CD20
int srGERD::isContextCreated() const
{
    return state_flags & STATE_CONTEXT_CREATED;
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

// FUNCTION: SURRENDER 0x1001D0B0
w8_ulong_ptr srGERD::getWindowHandle() const
{
    return device.window;
}

// FUNCTION: SURRENDER 0x1001D0D0
int srGERD::isWindowOpen() const
{
    return (state_flags >> 1) & 1;
}

// FUNCTION: SURRENDER 0x1001D030
std::optional<srPixelConvert::PixelFormat> srGERD::getPixelFormat() const
{
    if (isWindowOpen() == 0) {
        return std::nullopt;
    }
    return srPixelConvert::pixelFormat(srPixelConvert::SURFACE_ARGB32);
}

// FUNCTION: SURRENDER 0x1001D1A0
srGERD* srGERD::getFirst()
{
    return first;
}

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

// FUNCTION: SURRENDER 0x100213A0
srRendererDefs::e_scaleType srGERD::getModelViewScaleType()
{
    checkViewStateChanges();
    return state.modelview_scale_type;
}

// FUNCTION: SURRENDER 0x100213C0
glm::mat4 srGERD::getNormalMatrix()
{
    checkViewStateChanges();
    glm::mat4 matrix = state.normal_matrix;
    if ((enable_flags.value & (1UL << ENABLE_REVERSE_NORMALS)) != 0)
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

// FUNCTION: SURRENDER 0x1001F790
short srGERD::accumConvert(float value) const
{
    if (value <= -1.0f) {
        return -32767;
    }
    if (value >= 1.0f) {
        return 32767;
    }
    return static_cast<short>(srFloatToInt(value * 32767.0f));
}

// FUNCTION: SURRENDER 0x1001F7F0
void srGERD::accumAlloc()
{
    if (getWidth() != 0 && getHeight() != 0) {
        accum_buffer.resize(static_cast<std::size_t>(getWidth()) * getHeight(), AccumPixel(0));
        accum_scratch.resize(getWidth());
        accumClear();
    }
}

// FUNCTION: SURRENDER 0x1001F8D0
void srGERD::accumClear()
{
    if (accum_buffer.empty()) {
        accumAlloc();
    }
    if (accum_buffer.empty()) {
        return;
    }
    short first = accumConvert(clear_state.clear_values.accum.x);
    short second = accumConvert(clear_state.clear_values.accum.y);
    short third = accumConvert(clear_state.clear_values.accum.z);
    short fourth = accumConvert(clear_state.clear_values.accum.w);
    w8_ulong left = state.scissor.left;
    w8_ulong top = state.scissor.top;
    w8_ulong width = state.scissor.right - left;
    w8_ulong height = state.scissor.bottom - top;
    AccumPixel* row = accum_buffer.data() + getWidth() * top + left;
    if (width == 0 || height == 0) {
        return;
    }
    AccumPixel value{fourth, first, second, third};
    if (first == third && second == fourth) {
        /* Preserve the retail dword fill, including its signed low-lane carry. */
        std::uint32_t constant =
            static_cast<std::uint32_t>(first) * 0x10000u + static_cast<std::uint32_t>(second);
        const auto lanes = std::bit_cast<std::array<short, 2>>(constant);
        value = {lanes[0], lanes[1], lanes[0], lanes[1]};
    }
    for (; height != 0; height--) {
        std::fill_n(row, width, value);
        row += getWidth();
    }
}

// VTABLE: SURRENDER 0x100767F8
// class srGERD::LockSurface

/* Locked-buffer surface created by lockBuffer. It keeps its own scissor rect and proxies pixel
   access through device bufferOp commands, staging through a 1-pixel-high srColorSurface scratch
   buffer when the locked format is not 32-bit ARGB. */
// FUNCTION: SURRENDER 0x100205D0
srGERD::LockSurface::LockSurface(srGERD* gerd, const srPixelConvert::PixelFormat& format)
    : scratch(format, std::max(gerd->getWidth(), gerd->getHeight()), 1)
{
    SurfaceDesc description{};
    description.width = gerd->getWidth();
    description.height = gerd->getHeight();
    description.pitch = (format.pixel_size + 1) * description.width;
    description.pixel_format = format;
    setSurfaceDesc(description);
    this->gerd = gerd;
    left = 0;
    top = 0;
    right = 0;
    bottom = 0;
    argb32 = 0;
}

// FUNCTION: SURRENDER 0x100207A0
void srGERD::LockSurface::setHLine(w8_long y, w8_long x0, w8_long x1, w8_ulong pixel)
{
    if (y < (w8_long)top || y >= (w8_long)bottom) {
        return;
    }
    if (x0 < (w8_long)left) {
        x0 = left;
    }
    if (x1 > (w8_long)right) {
        x1 = right;
    }
    if (x0 >= x1) {
        return;
    }
    /* Convert the pixel through the scratch surface, then hand the device a
       pointer to the converted value. */
    scratch.setPixel(0, 0, pixel);
    w8_ulong converted = scratch.getPixelRaw(0, 0);
    gerd->getDD()->fillPixels(x0, y, x1 - x0, converted);
}

void srGERD::LockSurface::transferPixels(void* pixels, w8_long fixed, w8_long begin, w8_long end,
                                         bool vertical, bool write, bool raw)
{
    const w8_long fixedMin = vertical ? left : top;
    const w8_long fixedMax = vertical ? right : bottom;
    const w8_long start = vertical ? top : left;
    const w8_long stop = vertical ? bottom : right;
    if (fixed < fixedMin || fixed >= fixedMax) return;
    // Raw reads historically skip one byte per clipped pixel; raw writes skip
    // a complete packed pixel. Keep that device-interface behavior here.
    const size_t stride = raw ? (write ? pixel_format.pixel_size + 1 : 1) : sizeof(w8_ulong);
    if (begin < start) {
        pixels = static_cast<unsigned char*>(pixels) + size_t(start - begin) * stride;
        begin = start;
    }
    end = std::min(end, stop);
    if (begin >= end) return;
    const bool convert = !raw && !argb32;
    const w8_long count = end - begin;
    if (convert && write) scratch.setPixelRow(static_cast<const w8_ulong*>(pixels), 0, 0, count);
    const auto x = vertical ? fixed : begin;
    const auto y = vertical ? begin : fixed;
    auto* data = static_cast<w8_ulong*>(convert ? scratch.getDataPtr() : pixels);
    if (write) {
        gerd->getDD()->writePixels(x, y, vertical, {data, static_cast<std::size_t>(count)});
    } else {
        gerd->getDD()->readPixels(x, y, vertical, {data, static_cast<std::size_t>(count)});
    }
    if (convert && !write) scratch.getPixelRow(static_cast<w8_ulong*>(pixels), 0, 0, count);
}

// FUNCTION: SURRENDER 0x10020990
void srGERD::LockSurface::getPixelRow(w8_ulong* pixels, w8_long y, w8_long x0, w8_long x1)
{
    transferPixels(pixels, y, x0, x1, false, false);
}

// FUNCTION: SURRENDER 0x10020840
void srGERD::LockSurface::setPixelRow(const w8_ulong* pixels, w8_long y, w8_long x0, w8_long x1)
{
    transferPixels(const_cast<w8_ulong*>(pixels), y, x0, x1, false, true);
}

// FUNCTION: SURRENDER 0x10020A70
void srGERD::LockSurface::getPixelRowRaw(void* pixels, w8_long y, w8_long x0, w8_long x1)
{
    transferPixels(pixels, y, x0, x1, false, false, true);
}

// FUNCTION: SURRENDER 0x10020900
void srGERD::LockSurface::setPixelRowRaw(const void* pixels, w8_long y, w8_long x0, w8_long x1)
{
    transferPixels(const_cast<void*>(pixels), y, x0, x1, false, true, true);
}

// FUNCTION: SURRENDER 0x10020BB0
void srGERD::LockSurface::getPixelColumn(w8_ulong* pixels, w8_long x, w8_long y0, w8_long y1)
{
    transferPixels(pixels, x, y0, y1, true, false);
}

// FUNCTION: SURRENDER 0x10020AF0
void srGERD::LockSurface::setPixelColumn(const w8_ulong* pixels, w8_long x, w8_long y0, w8_long y1)
{
    transferPixels(const_cast<w8_ulong*>(pixels), x, y0, y1, true, true);
}

// FUNCTION: SURRENDER 0x10020780
void srGERD::LockSurface::setScissor(w8_ulong left, w8_ulong top, w8_ulong right, w8_ulong bottom)
{
    this->left = left;
    this->right = right;
    this->top = top;
    this->bottom = bottom;
}

// FUNCTION: SURRENDER 0x10020C90
void srGERD::_lockBuffer()
{
    if (buffer_lock_count == 0) {
        flushImmediateRenderers();
        getDD()->flushFrame();
        getDD()->lockBuffer();
    }
    buffer_lock_count += 1;
}

// FUNCTION: SURRENDER 0x10020CF0
void srGERD::_unlockBuffer()
{
    if (buffer_lock_count != 0) {
        buffer_lock_count -= 1;
        if (buffer_lock_count == 0) {
            getDD()->unlockBuffer();
        }
    }
}

// FUNCTION: SURRENDER 0x10020D30
void srGERD::clear(const srFlags<e_buffer>& buffers)
{
    if (isWindowOpen() == 0) {
        setError(ERROR_WINDOW_NOT_OPEN);
        return;
    }
    if (buffers.value != 0) {
        checkViewStateChanges();
        /* GERD buffer bits 0,1,3 map to DD bits 0,1,2; GERD bit 2 is the
           software accumulation buffer serviced by accumClear. */
        srFlags<srDD::e_buffer> device_buffers;
        device_buffers.value = (buffers.value & BUFFER_COLOR) != 0;
        if ((buffers.value & BUFFER_DEPTH) != 0) {
            device_buffers.value |= srDD::BUFFER_DEPTH;
        }
        if ((buffers.value & BUFFER_STENCIL) != 0) {
            device_buffers.value |= srDD::BUFFER_STENCIL;
        }
        if (device_buffers.value != 0) {
            const auto& values = clear_state.clear_values;
            getDD()->setClearValues(
                {values.color, static_cast<float>(values.depth), values.stencil});
            getDD()->clearBuffers(device_buffers);
        }
        if ((buffers.value & BUFFER_ACCUM) != 0) {
            accumClear();
        }
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
        srDD::ViewPort viewport;
        viewport.x = state.view_left;
        viewport.y = state.view_top;
        viewport.width = state.view_right - viewport.x;
        viewport.height = state.view_bottom - viewport.y;
        viewport.min_depth = state.depth_min;
        viewport.max_depth = state.depth_max;
        if ((state_flags & STATE_CLOSING_WINDOW) == 0) {
            getDD()->setViewPort(viewport);
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
            getDD()->setProjectionMatrix(state.project_clip_near);
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
    getDD()->setScissor(state.scissor);
    if (lock_surface != 0) {
        lock_surface->setScissor(state.scissor.left, state.scissor.top, state.scissor.right,
                                 state.scissor.bottom);
    }
}

// FUNCTION: SURRENDER 0x100215A0
void srGERD::classifyMatrix(e_matrixMode mode)
{
    statistics.matrix_classifications += 1;
    if (mode == MATRIX_MODELVIEW) {
        const auto& modelview = state.matrix_current[MATRIX_MODELVIEW];
        glm::mat3 basis(modelview);
        const glm::vec3 lengths(glm::length(basis[0]), glm::length(basis[1]),
                                glm::length(basis[2]));
        const glm::vec3 squared = lengths * lengths;
        state.matrix_class[MATRIX_MODELVIEW] = srRendererDefs::MATRIX_TYPE_GENERAL;
        state.inverse_modelview = glm::inverse(modelview);
        const bool uniform =
            std::abs(squared.x - squared.y) <= 1e-5f && std::abs(squared.x - squared.z) <= 1e-5f;
        state.modelview_scale_type = !uniform ? srRendererDefs::SCALE_TYPE_NON_UNIFORM
                                     : std::abs(squared.x - 1) > 1e-5f
                                         ? srRendererDefs::SCALE_TYPE_UNIFORM
                                         : srRendererDefs::SCALE_TYPE_UNIT;
        state.max_modelview_scale = glm::max(lengths.x, glm::max(lengths.y, lengths.z));
        // Lighting expects a transform with model scaling removed.
        for (int column = 0; column < 3; ++column) basis[column] /= lengths[column];
        state.normal_matrix = glm::mat4(glm::transpose(glm::inverse(basis)));
        return;
    }
    glm::mat4& matrix = state.matrix_current[mode];
    w8_ulong mask = 0;
    w8_ulong bit = 1;
    for (w8_long row = 0; row < 4; row++) {
        if (matrix[0][row] == 0.0f) {
            mask |= bit;
        }
        if (matrix[1][row] == 0.0f) {
            mask |= bit * 2;
        }
        if (matrix[2][row] == 0.0f) {
            mask |= bit * 4;
        }
        if (matrix[3][row] == 0.0f) {
            mask |= bit * 8;
        }
        bit *= 0x10;
    }
    srRendererDefs::e_matrixType type;
    if (mask == 0x7bde && matrix[0][0] == 1.0f && matrix[1][1] == 1.0f && matrix[2][2] == 1.0f &&
        matrix[3][3] == 1.0f) {
        type = srRendererDefs::MATRIX_TYPE_IDENTITY;
    } else if ((mask & 0xb39a) == 0xb39a) {
        type = srRendererDefs::MATRIX_TYPE_PERSPECTIVE;
    } else if ((mask & 0x7356) == 0x7356) {
        type = srRendererDefs::MATRIX_TYPE_ORTHOGRAPHIC;
    } else if ((mask & 0x7000) == 0x7000 && matrix[3][3] == 1.0f) {
        type = srRendererDefs::MATRIX_TYPE_AFFINE;
    } else {
        type = srRendererDefs::MATRIX_TYPE_GENERAL;
    }
    state.matrix_class[mode] = type;
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

// FUNCTION: SURRENDER 0x10022060
void srGERD::perspective(double fov_y, double aspect, double near_plane, double far_plane)
{
    if (near_plane <= 0 || far_plane <= 0) return;
    auto& current = state.matrix_current[state.matrix_mode];
    current *= glm::perspective(float(fov_y), float(aspect), float(near_plane), float(far_plane));
    setMatrixDirty();
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
        state.clip_modes[state.clip_plane_count] = static_cast<unsigned char>(mode);
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
    planes.mode1_mask = state.clip_mode1_mask;
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
    std::lock_guard access(state_section);
    if ((dirty & DIRTY_FOG_COLOR) != 0) {
        getDD()->setFogColor(fog_color);
    }
    if ((dirty & DIRTY_SHADER) != 0) {
        getDD()->setShader(shader);
        statistics.shader_sets++;
    }
    removeDeletedTextures();
    if ((dirty & DIRTY_TEXTURE0) != 0) {
        changeTexture(texture_iface[0], 0, 1);
    }
    if (((dirty & DIRTY_TEXTURE1) != 0) && (1 < device.info.max_texture_stages)) {
        changeTexture(texture_iface[1], 1, 1);
    }
    if ((dirty & DIRTY_CULLING) != 0) {
        switch (state.cull_mode) {
        case CULL_BACK:
            getDD()->setCullMode(state.winding != 0 ? srDD::CULL_FRONT : srDD::CULL_BACK);
            break;
        case CULL_FRONT:
            getDD()->setCullMode(state.winding == 0 ? srDD::CULL_FRONT : srDD::CULL_BACK);
            break;
        case CULL_NONE:
            getDD()->setCullMode(srDD::CULL_NONE);
            break;
        }
    }
    if ((dirty & DIRTY_POLYGON_MODE) != 0) {
        switch (polygon_mode) {
        case POLYGON_POINT:
            getDD()->setPolygonMode(srDD::POLYGON_POINT);
            break;
        case POLYGON_LINE:
            getDD()->setPolygonMode(srDD::POLYGON_LINE);
            break;
        case POLYGON_FILL:
            getDD()->setPolygonMode(srDD::POLYGON_FILL);
            break;
        }
    }
    if ((dirty & DIRTY_POLYGON_OFFSET) != 0) {
        getDD()->setPolygonOffset(polygon_offset);
    }
    dirty &= ~DIRTY_DRAW_STATE;
    statistics.draw_state_applies++;
}

// FUNCTION: SURRENDER 0x100281B0
void srGERD::removeDeletedTextures()
{
    Texture* texture = texture_deleted;
    while (texture != 0) {
        Texture* next = texture->next;
        deleteTexture(*texture);
        texture = next;
    }
}

// FUNCTION: SURRENDER 0x100286B0
void srGERD::releaseTextureSurfaceData(Texture& texture)
{
    decltype(texture.surface_data){}.swap(texture.surface_data);
    texture_cache_used -= texture.device.size;
    texture.device.size = 0;
}

// FUNCTION: SURRENDER 0x100286F0
void srGERD::deleteTexture(Texture& texture)
{
    for (w8_ulong stage = 0; stage < device.info.max_texture_stages; ++stage) {
        if (&texture == texture_slots[stage]) {
            texture_slots[stage] = 0;
            dirty |= 1UL << (stage + DIRTY_TEXTURE_SHIFT);
        }
    }
    if (texture.prev != 0) {
        texture.prev->next = texture.next;
    }
    if (texture.next != 0) {
        texture.next->prev = texture.prev;
    }
    if (&texture == texture_deleted) {
        texture_deleted = texture.next;
    }
    texture_lookup.erase(texture.id);
    getDD()->deleteTexture(texture.device.gpu);
    texture.name.clear();
    texture.device.gpu.handle = 0;
    texture.device.gpu.size = 0;
    texture.prev = 0;
    texture.next = 0;
    texture.device.deleted = 0;
    releaseTextureSurfaceData(texture);
    texture.id = 0;
    texture.device = {};
    texture_pool.release(&texture);
}

// FUNCTION: SURRENDER 0x10028910
srGERD::Texture* srGERD::findLowestPriority()
{
    Texture* texture = texture_head;
    Texture* found = 0;
    w8_ulong last_use = texture_sequence + 1;
    float lowest = 1.01f;
    if (texture == 0) {
        return 0;
    }
    do {
        if (texture->device.priority < lowest ||
            (texture->device.priority == lowest && texture->device.last_use < last_use)) {
            int bound = 0;
            for (w8_ulong stage = 0; stage < device.info.max_texture_stages; ++stage) {
                if (texture_slots[stage] == texture) {
                    bound = 1;
                    break;
                }
            }
            if (bound == 0) {
                lowest = texture->device.priority;
                last_use = texture->device.last_use;
                found = texture;
            }
        }
        texture = texture->next;
        if (texture == 0) {
            return found;
        }
    } while (true);
}

// FUNCTION: SURRENDER 0x10028990
srGERD::Texture* srGERD::allocTexture(w8_ulong id)
{
    Texture* texture = texture_pool.allocate();
    texture->id = id;
    texture_lookup.emplace(texture->id, texture);
    texture->next = texture_head;
    texture->prev = 0;
    if (texture_head != 0) {
        texture_head->prev = texture;
    }
    texture_head = texture;
    texture->device = {};
    texture->name.clear();
    texture->device.resident = 0;
    texture->device.deleted = 0;
    return texture;
}

// FUNCTION: SURRENDER 0x10028FB0
void srGERD::setTextureParameters(w8_ulong stage, const srTextureIFace::Parameters& parameters)
{
    w8_ulong state = parameters.packed_state;
    float bias = parameters.mipmap_bias;
    w8_ulong packed =
        ((((texture_state.wrap_s_map[(state >> srTextureIFace::Parameters::WRAP_S_SHIFT) & 1] &
            0xfffffff3) |
           (texture_state.wrap_t_map[(state >> srTextureIFace::Parameters::WRAP_T_SHIFT) & 1] << 2))
              << 2 |
          (texture_state.mipmap_map[(state >> srTextureIFace::Parameters::MIPMAP_SHIFT) & 3] &
           0xffffffc3))
             << 2 |
         (texture_state
              .min_filter_map[(state >> srTextureIFace::Parameters::MIN_FILTER_SHIFT) & 7] &
          0xffffff03))
            << 2 |
        (texture_state.mag_filter_map[(state >> srTextureIFace::Parameters::MAG_FILTER_SHIFT) & 7] &
         0xfffffc0f);
    packed = (packed << 4) |
             (texture_state.correction_map[state & srTextureIFace::Parameters::CORRECTION_MASK] &
              0xffffc00f);
    srDD::TexParms* parms = &texture_parms[stage];
    if (packed == parms->packed && bias == parms->mipmap_bias) {
        return;
    }
    parms->packed = packed;
    parms->mipmap_bias = bias;
    ++statistics.texture_parameter_sets;
    getDD()->setTextureParameters(stage, *parms);
}

// FUNCTION: SURRENDER 0x10029400
void srGERD::changeTexture(srTextureIFace* texture, w8_ulong stage, int apply_parms)
{
    if ((state_flags & STATE_CLOSING_WINDOW) != 0) {
        return;
    }
    if (device.info.max_texture_stages <= stage) {
        return;
    }
    Texture* found;
    if (texture == 0) {
        if (texture_slots[stage] == texture_default) {
            return;
        }
    } else {
        w8_ulong id = texture->getTextureFrameHandle();
        if (id != 0) {
            const auto entry = texture_lookup.find(id);
            if (entry != texture_lookup.end()) {
                found = entry->second;
                if (found != 0) {
                    goto bound;
                }
            }
            found = createNewTexture(texture);
            if (found != 0) {
                goto bound;
            }
        }
    }
    found = texture_default;
    texture = srCore.getTexture();
bound:
    ++texture_sequence;
    found->device.last_use = texture_sequence;
    if (apply_parms == 0) {
        return;
    }
    Texture* previous = texture_slots[stage];
    texture_slots[stage] = found;
    if (previous != found) {
        if (found->device.gpu.handle == 0) {
            uploadTexture(*found);
        }
        getDD()->bindTexture(stage, found->device.gpu);
        if (found != texture_default && found->device.resident == 0 &&
            !found->surface_data.empty() &&
            (device.info.flags & srDD::Info::RELEASE_SURFACE_AFTER_BIND) != 0) {
            releaseTextureSurfaceData(*found);
        }
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
void srGERD::initDDInfo()
{
    device.info = getDD()->getInfo();
    if (device.info.max_texture_stages > 2) {
        device.info.max_texture_stages = 2;
    }
}

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
        state.matrix_class[i] = srRendererDefs::MATRIX_TYPE_IDENTITY;
        state.matrix_stacks[i].depth = 0;
    }
    state.normal_matrix = glm::mat4(1);
    state.inverse_modelview = glm::mat4(1);
}

// FUNCTION: SURRENDER 0x100191E0
srGERD::e_error srGERD::createContext(w8_ulong_ptr window)
{
    deleteContext();
    if (srWindow::isWindow(window) == 0) {
        return ERROR_INVALID_WHANDLE;
    }
    for (srGERD* gerd = getFirst(); gerd != 0; gerd = gerd->getNext()) {
        if (gerd->isContextCreated() != 0 && gerd->getWindowHandle() == window) {
            return ERROR_SHARED_CONTEXT;
        }
    }
    if (!getDD()->createContext(reinterpret_cast<SDL_Window*>(window))) {
        return ERROR_CONTEXT_CREATION_FAILED;
    }
    device.window = window;
    initDDInfo();
    initGlobalPalette();
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
        getDD()->deleteContext();
        state_flags &= ~STATE_CONTEXT_CREATED;
        device.window = 0;
    }
}

// FUNCTION: SURRENDER 0x10019EB0
void srGERD::deleteRenderers()
{
    std::unique_lock access(renderers_section);
    renderers_idle.wait(access, [this] {
        return std::none_of(renderers.begin(), renderers.end(),
                            [](const auto& entry) { return entry.busy; });
    });
    renderers.clear();
}

// FUNCTION: SURRENDER 0x10020DF0
srColorSurfaceIFace* srGERD::lockBuffer()
{
    if (lock_surface != 0) {
        return 0;
    }
    const auto format = getPixelFormat();
    if (!format) {
        setError(ERROR_WINDOW_NOT_OPEN);
        return nullptr;
    }
    flush();
    _lockBuffer();
    checkViewStateChanges();
    lock_surface = std::make_unique<LockSurface>(this, *format);
    lock_surface->setScissor(state.scissor.left, state.scissor.top, state.scissor.right,
                             state.scissor.bottom);
    if (format->color_model == srPixelConvert::COLOR_RGB &&
        format->pixel_size == srPixelConvert::PIXEL_SIZE_32 && format->red_bits == 8 &&
        format->green_bits == 8 && format->blue_bits == 8 && format->alpha_bits == 8 &&
        format->red_shift == 0x10 && format->green_shift == 8 && format->blue_shift == 0 &&
        format->alpha_shift == 0x18) {
        lock_surface->argb32 = 1;
    }
    return lock_surface.get();
}

// FUNCTION: SURRENDER 0x10020F30
void srGERD::unlockBuffer()
{
    if (lock_surface != 0) {
        lock_surface.reset();
        _unlockBuffer();
    }
}

// FUNCTION: SURRENDER 0x1001A5F0
void srGERD::closeWindow(e_closeHint hint)
{
    std::lock_guard access(state_section);
    if (isWindowOpen() != 0) {
        state_flags |= STATE_CLOSING_WINDOW;
        flush();
        deleteRenderers();
        unlockBuffer();
        invalidateResidentTextures();
        closeTexCache();
        getDD()->closeWindow();
        resetStatistics();
        decltype(accum_buffer){}.swap(accum_buffer);
        decltype(accum_scratch){}.swap(accum_scratch);
        device.open_info = {};
        state_flags &= ~STATE_WINDOW_OPEN;
        device.back_buffer_type = static_cast<e_backBuffer>(0);
        if (prev_open != 0) {
            prev_open->next_open = next_open;
        }
        if (next_open != 0) {
            next_open->prev_open = prev_open;
        }
        if (this == firstOpen) {
            firstOpen = next_open;
        }
        prev_open = 0;
        next_open = 0;
        state_flags &= ~STATE_CLOSING_WINDOW;
        shader = srShader();
        texture_iface[0] = 0;
        texture_iface[1] = 0;
        decltype(pick_vertices){}.swap(pick_vertices);
    }
}

// FUNCTION: SURRENDER 0x1001CD10
srGERD::e_backBuffer srGERD::getBackBufferType() const
{
    return device.back_buffer_type;
}

// FUNCTION: SURRENDER 0x1001A120
srGERD::e_error srGERD::openWindow()
{
    if ((state_flags & STATE_CONTEXT_CREATED) == 0) {
        return ERROR_NO_CONTEXT;
    }
    return openWindow(srWindow::getWidth(device.window), srWindow::getHeight(device.window));
}

// FUNCTION: SURRENDER 0x1001A160
srGERD::e_error srGERD::openWindow(w8_long width, w8_long height)
{
    if ((state_flags & STATE_CONTEXT_CREATED) == 0) {
        return ERROR_NO_CONTEXT;
    }
    if (srWindow::isWindow(device.window) == 0) {
        return ERROR_INVALID_WHANDLE;
    }
    OpenInfo info;
    info.window_width = srWindow::getWidth(device.window);
    info.window_height = srWindow::getHeight(device.window);
    info.width = width;
    info.height = height;
    return openWindowInternal(info);
}

// FUNCTION: SURRENDER 0x1001A290
srGERD::e_error srGERD::openWindowInternal(const OpenInfo& info)
{
    std::lock_guard access(state_section);
    if ((w8_ulong)info.width > (w8_ulong)info.window_width ||
        (w8_ulong)info.height > (w8_ulong)info.window_height) {
        return ERROR_INVALID_VALUE;
    }
    if ((state_flags & STATE_CONTEXT_CREATED) == 0) {
        return ERROR_NO_CONTEXT;
    }
    closeWindow(static_cast<e_closeHint>(1));
    if (info.width != 0 && info.height != 0 && info.window_width != 0 && info.window_height != 0) {
        if (srWindow::isWindow(device.window) == 0) {
            return ERROR_INVALID_WHANDLE;
        }
        if (device.info.max_back_buffer_width < (w8_ulong)info.width ||
            device.info.max_back_buffer_height < (w8_ulong)info.height) {
            return ERROR_INVALID_VALUE;
        }
        device.open_info = {};
        if (getDD()->openWindow(info.width, info.height)) {
            device.open_info = info;
            device.back_buffer_type = BACKBUFFER_ONE;
            prev_open = 0;
            next_open = firstOpen;
            if (firstOpen != 0) {
                firstOpen->prev_open = this;
            }
            w8_ulong flags = state_flags & ~4UL;
            firstOpen = this;
            state_flags |= STATE_WINDOW_OPEN;
            state_flags = flags | STATE_WINDOW_OPEN;
            state_flags |= STATE_FRAME_FLIPPED;
            resetStatistics();
            dirty = 0xffffffff;
            initTexCache();
            texture_iface[0] = 0;
            texture_iface[1] = 0;
            shader = srShader();
            for (w8_ulong stage = 0; stage < device.info.max_texture_stages; ++stage) {
                texture_parms[stage].mipmap_bias = 0.0f;
                texture_parms[stage].packed = 0x1a1;
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
            createRenderer(1);
            if ((enable_flags.value & (1UL << ENABLE_CLEAR_ON_OPEN)) != 0) {
                w8_long count;
                e_backBuffer back_buffer = getBackBufferType();
                if (back_buffer == static_cast<e_backBuffer>(2)) {
                    count = 2;
                } else if (back_buffer == static_cast<e_backBuffer>(3)) {
                    count = 3;
                } else {
                    count = 1;
                }
                for (; count != 0; count--) {
                    clear(srFlags<e_buffer>(~static_cast<w8_ulong>(BUFFER_ACCUM)));
                    flipFrame();
                }
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
    if (texture_hash_enabled != 0) {
        invalidateTextureCache();
        removeDeletedTextures();
        markTextureAsDeleted(*texture_default);
        deleteTexture(*texture_default);
        texture_default = 0;
        texture_lookup.clear();
        texture_deleted = 0;
        texture_head = 0;
        texture_cache_used = 0;
        texture_hash_enabled = 0;
    }
}

// FUNCTION: SURRENDER 0x10028200
void srGERD::initTexCache()
{
    if (texture_hash_enabled == 0) {
        texture_lookup.clear();
        texture_head = 0;
        texture_deleted = 0;
        texture_cache_used = 0;
        resetCurrentTexPointers();
        texture_default = createNewTexture(srCore.getTexture());
        texture_default->device.priority = 1.1f;
        texture_hash_enabled = 1;
        invalidateTextureCache();
    }
}

// FUNCTION: SURRENDER 0x10028B80
void srGERD::evaluateTextureDimensions(TextureCacheData& device,
                                       const srTextureIFace::Dimensions& dimensions)
{
    w8_ulong min_dim = this->device.info.texture_min_dim;
    unsigned char reduction = 0;
    if ((dimensions.hints & (1UL << srTextureIFace::HINT_NO_REDUCTION)) == 0) {
        reduction = (unsigned char)texture_reduction;
    }
    w8_ulong width = nextTextureDimension(dimensions.width) >> (reduction & 0x1f);
    w8_ulong height = nextTextureDimension(dimensions.height) >> (reduction & 0x1f);
    w8_ulong device_width = min_dim;
    if (min_dim <= width && width <= this->device.info.texture_max_dim) {
        device_width = width;
    }
    w8_ulong device_height = min_dim;
    if (min_dim <= height && height <= this->device.info.texture_max_dim) {
        device_height = height;
    }
    int unbalanced = 0;
    if (device_height < device_width) {
        while (this->device.info.texture_max_aspect < device_width / device_height &&
               device_height < this->device.info.texture_max_dim) {
            device_height *= 2;
        }
        unbalanced = device_height < device_width;
    }
    if (unbalanced == 0 && device_height != device_width) {
        while (this->device.info.texture_max_aspect < device_height / device_width &&
               device_width < this->device.info.texture_max_dim) {
            device_width *= 2;
        }
    }
    device.width = device_width;
    device.height = device_height;
    device.first_level = 0;
    device.last_level = 0;
    if (((dimensions.hints & (1UL << srTextureIFace::HINT_NO_MIPMAPS)) == 0 ||
         (this->device.info.flags & 0x40) != 0) &&
        this->device.info.texture_min_dim < device_width) {
        do {
            if (device_height <= this->device.info.texture_min_dim) {
                return;
            }
            ++device.last_level;
            device_width >>= 1;
            device_height >>= 1;
        } while (this->device.info.texture_min_dim < device_width);
    }
}

// FUNCTION: SURRENDER 0x10028E60
void srGERD::releaseTextureMemory(w8_long bytes)
{
    if (0 < bytes) {
        Texture* texture = findLowestPriority();
        while (texture != 0) {
            bytes -= (w8_long)texture->device.size;
            markTextureAsDeleted(*texture);
            deleteTexture(*texture);
            if (bytes < 1) {
                return;
            }
            texture = findLowestPriority();
        }
    }
}

// FUNCTION: SURRENDER 0x10028EB0
w8_ulong srGERD::getTextureBytesNeeded(Texture& texture) const
{
    w8_ulong width = texture.device.width;
    w8_ulong height = texture.device.height;
    w8_ulong bytes = 0;
    if (texture.device.first_level <= texture.device.last_level) {
        w8_long count = (w8_long)(texture.device.last_level - texture.device.first_level) + 1;
        do {
            bytes += height * width * (texture.pixel_format.pixel_size + 1);
            width >>= 1;
            height >>= 1;
            --count;
        } while (count != 0);
    }
    return bytes;
}

// FUNCTION: SURRENDER 0x10028EF0
void srGERD::allocTextureData(Texture& texture)
{
    w8_ulong size = getTextureBytesNeeded(texture);
    if (texture_cache_size != 0 && texture_cache_size < size + texture_cache_used) {
        float priority = texture.device.priority;
        texture.device.priority = 1.2f;
        releaseTextureMemory((w8_long)(size - texture_cache_size + texture_cache_used));
        texture.device.priority = priority;
    }
    texture.surface_data.resize(size);
    texture.device.size = size;
    texture_cache_used += size;
}

std::span<unsigned char> srGERD::Texture::mipPixels(w8_ulong level)
{
    std::size_t offset = 0;
    for (w8_ulong previous = device.first_level; previous < level; ++previous) {
        offset += (device.width >> previous) * (device.height >> previous) * 4;
    }
    const auto bytes = (device.width >> level) * (device.height >> level) * 4;
    return std::span(surface_data).subspan(offset, bytes);
}

void srGERD::uploadTexture(Texture& texture)
{
    texture.device.gpu =
        getDD()->createTexture(texture.device.width, texture.device.height,
                               texture.device.last_level - texture.device.first_level + 1);
    for (w8_ulong level = texture.device.first_level; level <= texture.device.last_level; ++level) {
        const auto width = texture.device.width >> level;
        const auto height = texture.device.height >> level;
        getDD()->uploadTexture(texture.device.gpu, level - texture.device.first_level,
                               {0, 0, width, height}, texture.mipPixels(level), width * 4);
    }
}

// FUNCTION: SURRENDER 0x10029090
srGERD::Texture* srGERD::createNewTexture(srTextureIFace* texture)
{
    Texture* result = allocTexture(texture->getTextureFrameHandle());
    result->name = texture->getName();
    const auto dimensions = texture->getDimensions();
    evaluateTextureDimensions(result->device, dimensions);
    result->device.resident = (dimensions.hints >> srTextureIFace::HINT_RESIDENT) & 1;
    allocTextureData(*result);
    std::array<srColorSurfaceIFace*, 12> destinations{};
    std::unique_ptr<srColorSurface> surfaces[12];

    w8_ulong width = result->device.width;
    w8_ulong height = result->device.height;
    for (w8_ulong level = result->device.first_level; level <= result->device.last_level; ++level) {
        surfaces[level] = std::make_unique<srColorSurface>(
            result->pixel_format, result->mipPixels(level).data(), width, height,
            (w8_ulong)(result->pixel_format.pixel_size + 1) * width);
        auto* surface = surfaces[level].get();
        destinations[level] = surface;
        surface->setFilter(dimensions.filter);
        width >>= 1;
        height >>= 1;
    }
    const srTextureIFace::MultiRequest request{
        result->device.first_level,
        std::span<srColorSurfaceIFace* const>(destinations)
            .subspan(result->device.first_level,
                     result->device.last_level - result->device.first_level + 1)};
    texture->getMipmapData(request);

    result->device.gpu.handle = 0;
    result->device.priority = texture->getPriority();
    ++statistics.textures_created;
    return result;
}

// FUNCTION: SURRENDER 0x10017930
w8_long srGERD::getMaxTextureWidth() const
{
    return device.info.texture_max_dim;
}

// FUNCTION: SURRENDER 0x10017940
w8_long srGERD::getMaxTextureHeight() const
{
    return device.info.texture_max_dim;
}

// FUNCTION: SURRENDER 0x10017950
w8_long srGERD::getMaxTextureAspectRatio() const
{
    return device.info.texture_max_aspect;
}

// FUNCTION: SURRENDER 0x10017960
void srGERD::setGlobalPalette(const srPalette& palette)
{
    std::lock_guard access(state_section);
    if (palette.matchPalette(global_palette, 0x100) != 0) {
        return;
    }
    w8_ulong count = palette.getPaletteSize();
    if (count > 0x100) {
        count = 0x100;
    }
    for (w8_ulong i = 0; i < count; ++i) {
        global_palette[i] = palette.getColor(i);
    }
}

// FUNCTION: SURRENDER 0x10017AA0
w8_long srGERD::getTextureReduction() const
{
    std::lock_guard access(state_section);
    return texture_reduction;
}

// FUNCTION: SURRENDER 0x10017D00
int srGERD::isTextureCached(srTextureIFace* texture) const
{
    std::lock_guard access(state_section);
    if (texture != 0) {
        w8_ulong handle = texture->getTextureFrameHandle();
        return texture_lookup.contains(handle);
    }
    return 0;
}

// FUNCTION: SURRENDER 0x10017DD0
int srGERD::isTextureResident(srTextureIFace* texture) const
{
    std::lock_guard access(state_section);
    if (texture != 0 && isWindowOpen() != 0) {
        w8_ulong handle = texture->getTextureFrameHandle();
        const auto entry = texture_lookup.find(handle);
        Texture* resident = entry == texture_lookup.end() ? nullptr : entry->second;
        if (resident != 0 && resident->device.gpu.handle != 0 && resident->device.gpu.size != 0) {
            return 1;
        }
    }
    return 0;
}

// FUNCTION: SURRENDER 0x10029600
void srGERD::initGlobalPalette()
{
    float level = 0.0f;
    for (w8_long i = 0; i < 0x100; ++i) {
        unsigned char gray = (unsigned char)srFloatToInt(level * 255.0);
        global_palette[i].blue = gray;
        global_palette[i].green = gray;
        global_palette[i].red = gray;
        global_palette[i].alpha = 0xff;
        level += 0.003921569f;
    }
}

// FUNCTION: SURRENDER 0x10018E60
void srGERD::releaseAll()
{
    while (getFirst() != 0) {
        delete getFirst();
    }
}

// FUNCTION: SURRENDER 0x1001AFD0
void srGERD::setHint(e_hint hint, e_hintMode mode)
{
    if (device.hints[hint] != mode) {
        device.hints[hint] = mode;
    }
}

// FUNCTION: SURRENDER 0x1001AFF0
const char* srGERD::getErrorString(e_error error)
{
    if (0 <= error && error < 10) {
        return errStrings[error];
    }
    return "UNKNOWN ERROR";
}

// GLOBAL: SURRENDER 0x100993A4
const char* srGERD::errStrings[10] = {"ERROR_NONE",
                                      "ERROR_INVALID_ENUM",
                                      "ERROR_INVALID_VALUE",
                                      "ERROR_WINDOW_OPEN_FAILED",
                                      "ERROR_WINDOW_NOT_OPEN",
                                      "ERROR_BUFFER_LOCK_FAILED",
                                      "ERROR_INVALID_WHANDLE",
                                      "ERROR_SHARED_CONTEXT",
                                      "ERROR_CONTEXT_CREATION_FAILED",
                                      "ERROR_NO_CONTEXT"};

// FUNCTION: SURRENDER 0x1001B400
w8_long srGERD::getGERDCount()
{
    w8_long count = 0;
    for (srGERD* gerd = first; gerd != 0; gerd = gerd->getNext()) {
        count++;
    }
    return count;
}

// FUNCTION: SURRENDER 0x1001B420
srGERD* srGERD::getGERD(w8_ulong index)
{
    w8_ulong current = 0;
    for (srGERD* gerd = first; gerd != 0; gerd = gerd->getNext()) {
        if (current == index) {
            return gerd;
        }
        current++;
    }
    return 0;
}

// FUNCTION: SURRENDER 0x10020DD0
int srGERD::isBufferLocked()
{
    return lock_surface != 0;
}

// FUNCTION: SURRENDER 0x1001C830
w8_long srGERD::getAccumAlphaBits() const
{
    return 0x10;
}

// FUNCTION: SURRENDER 0x1001C840
w8_long srGERD::getAccumRedBits() const
{
    return 0x10;
}

// FUNCTION: SURRENDER 0x1001C850
w8_long srGERD::getAccumGreenBits() const
{
    return 0x10;
}

// FUNCTION: SURRENDER 0x1001C860
w8_long srGERD::getAccumBlueBits() const
{
    return 0x10;
}

// FUNCTION: SURRENDER 0x1001C870
void srGERD::setPolygonMode(e_polygonMode mode)
{
    if (mode != polygon_mode) {
        flushImmediateRenderers();
        polygon_mode = mode;
        dirty |= DIRTY_POLYGON_MODE;
    }
}

// FUNCTION: SURRENDER 0x1001C8E0
srGERD::e_polygonMode srGERD::getPolygonMode() const
{
    return polygon_mode;
}

// FUNCTION: SURRENDER 0x1001C920
void srGERD::setClearAccum(float red, float green, float blue, float alpha)
{
    glm::vec4 color;
    color.x = red;
    color.y = green;
    color.z = blue;
    color.w = alpha;
    setClearAccum(color);
}

// FUNCTION: SURRENDER 0x1001C960
void srGERD::setClearAccum(const glm::vec4& color)
{
    clear_state.clear_values.accum = glm::clamp(color, -1.0f, 1.0f);
}

// FUNCTION: SURRENDER 0x1001CBE0
void srGERD::setClearStencil(w8_ulong stencil)
{
    clear_state.clear_values.stencil = stencil;
}

// FUNCTION: SURRENDER 0x1001CBF0
double srGERD::getClearDepth() const
{
    return clear_state.clear_values.depth;
}

// FUNCTION: SURRENDER 0x1001CC00
w8_ulong srGERD::getClearStencil() const
{
    return clear_state.clear_values.stencil;
}

// FUNCTION: SURRENDER 0x1001CF60
srGERD* srGERD::getPrev() const
{
    return prev;
}

// FUNCTION: SURRENDER 0x1001CF70
srGERD* srGERD::getPrevOpen() const
{
    return prev_open;
}

// FUNCTION: SURRENDER 0x1001CFD0
srGERD::e_hintMode srGERD::getHint(e_hint hint) const
{
    return device.hints[hint];
}

// FUNCTION: SURRENDER 0x1001D000
srGERD::e_antiAlias srGERD::getAntiAlias() const
{
    return display.antialias;
}

// FUNCTION: SURRENDER 0x1001D010
srGERD::e_error srGERD::getError()
{
    e_error error = last_error;
    setError(ERROR_NONE);
    return error;
}

// FUNCTION: SURRENDER 0x1001D0C0
int srGERD::isFlipped() const
{
    return (state_flags >> 3) & 1;
}

// FUNCTION: SURRENDER 0x1001D1C0
srDD::e_driverID srGERD::getDriverID() const
{
    return (srDD::e_driverID)device.driver_info.driver_id;
}

// FUNCTION: SURRENDER 0x1001D1E0
w8_ulong srGERD::getSwapInterval() const
{
    return display.swap_interval;
}

// FUNCTION: SURRENDER 0x1001D360
void srGERD::setDepthRange(double minimum, double maximum)
{
    if (minimum <= 0.0) {
        minimum = 0.0;
    } else if (minimum >= 1.0) {
        minimum = 1.0;
    }
    state.depth_min = minimum;
    if (maximum > 0.0) {
        if (maximum < 1.0) {
            state.depth_max = maximum;
        } else {
            state.depth_max = 1.0;
        }
    } else {
        state.depth_max = 0.0;
    }
    dirty |= DIRTY_DEPTH_RANGE;
}

// FUNCTION: SURRENDER 0x1001BB00
srShader srGERD::getShader() const
{
    return shader;
}

// FUNCTION: SURRENDER 0x1001BAA0
void srGERD::disable(e_enable option)
{
    if ((enable_flags.value & (1UL << option)) != 0) {
        toggle(option);
    }
}

// FUNCTION: SURRENDER 0x1001BAC0
void srGERD::enable(e_enable option)
{
    if ((enable_flags.value & (1UL << option)) == 0) {
        toggle(option);
    }
}

// FUNCTION: SURRENDER 0x1001BBB0
void srGERD::drawTriangle(const glm::ivec3& triangle)
{
    drawElements(static_cast<srRendererDefs::e_primitive>(3), 3, srRendererDefs::INDEX_ULONG,
                 &triangle);
}

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

// FUNCTION: SURRENDER 0x1001BF10
void srGERD::setDiffusePointer(w8_long components, srRendererDefs::e_type type, w8_ulong stride,
                               const void* values)
{
    setDataPtr(srRendererDefs::VERTEX_ARRAY_DIFFUSE, components, type, stride, values);
}

// FUNCTION: SURRENDER 0x1001BF50
void srGERD::setSpecularPointer(w8_long components, srRendererDefs::e_type type, w8_ulong stride,
                                const void* values)
{
    vertex_arrays.components[2] = components;
    vertex_arrays.types[2] = type;
    vertex_arrays.strides[2] = stride;
    vertex_arrays.arrays[2] = values;
    vertex_arrays_dirty |= DIRTY_VERTEX_ARRAY_INFO;
}

// FUNCTION: SURRENDER 0x1001BF90
void srGERD::setFogPointer(w8_long components, srRendererDefs::e_type type, w8_ulong stride,
                           const void* values)
{
    vertex_arrays.components[3] = components;
    vertex_arrays.types[3] = type;
    vertex_arrays.strides[3] = stride;
    vertex_arrays.arrays[3] = values;
    vertex_arrays_dirty |= DIRTY_VERTEX_ARRAY_INFO;
}

// FUNCTION: SURRENDER 0x10020550
void srGERD::initClearColors()
{
    glm::vec4 color;
    color = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    setClearAccum(color);
    color = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    setClearColor(color);
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
void srGERD::initTextureParameterMatrix()
{
    texture_state.correction_map[0] = 0;
    texture_state.correction_map[1] = 1;
    texture_state.correction_map[2] = 2;
    texture_state.correction_map[3] = 1;
    texture_state.mag_filter_map[0] = 0;
    texture_state.mag_filter_map[1] = 1;
    texture_state.mag_filter_map[2] = 2;
    texture_state.mag_filter_map[3] = 3;
    texture_state.mag_filter_map[4] = 2;
    texture_state.min_filter_map[0] = 0;
    texture_state.min_filter_map[1] = 1;
    texture_state.min_filter_map[2] = 2;
    texture_state.min_filter_map[3] = 3;
    texture_state.min_filter_map[4] = 2;
    texture_state.mipmap_map[0] = 0;
    texture_state.mipmap_map[1] = 1;
    texture_state.mipmap_map[2] = 2;
    texture_state.mipmap_map[3] = 1;
    texture_state.wrap_s_map[0] = 0;
    texture_state.wrap_s_map[1] = 1;
    texture_state.wrap_t_map[0] = 0;
    texture_state.wrap_t_map[1] = 1;
    texture_state.default_correction = static_cast<srTextureIFace::e_correction>(1);
    texture_state.default_mag_filter = static_cast<srTextureIFace::e_filter>(2);
    texture_state.default_min_filter = static_cast<srTextureIFace::e_filter>(2);
    texture_state.default_mipmap = static_cast<srTextureIFace::e_mipmap>(1);
    texture_state.default_texture_params[0] = 0;
    texture_state.default_texture_params[1] = 1;
    texture_state.default_texture_params[2] = 2;
    texture_state.default_texture_params[3] = 3;
    texture_state.default_texture_params[4] = 0;
    texture_state.default_compression = static_cast<srTextureIFace::e_compression>(0);
}

// FUNCTION: SURRENDER 0x1001FBD0
void srGERD::accumulate(e_accum operation, float scale)
{
    if (!isWindowOpen()) {
        return;
    }
    w8_long width = state.scissor.right - state.scissor.left;
    w8_long height = state.scissor.bottom - state.scissor.top;
    if (width == 0 || height == 0) {
        return;
    }
    if (accum_buffer.empty()) {
        accumAlloc();
        if (accum_buffer.empty()) {
            return;
        }
    }
    srColorSurfaceIFace* surface = 0;
    if ((operation == ACCUM_LOAD || operation == ACCUM_ACCUMULATE || operation == ACCUM_RETURN) &&
        (surface = lockBuffer()) == 0) {
        setError(ERROR_BUFFER_LOCK_FAILED);
        return;
    }
    AccumPixel* row = accum_buffer.data() + getWidth() * state.scissor.top + state.scissor.left;
    srARGB* pixels = accum_scratch.data();
    {
        short addend = accumConvert(scale);
        switch (operation) {
        case ACCUM_LOAD:
        case ACCUM_ACCUMULATE: {
            short table[256];
            for (w8_long i = 0; i < 256; ++i)
                table[i] = (short)(w8_long)(i * (scale * (32767.0f / 255.0f)));

            for (w8_long y = 0; y < height; ++y) {
                surface->getPixelRow(
                    reinterpret_cast<w8_ulong*>(pixels), // reinterpret-ok: packed ARGB row
                    state.scissor.top + y, state.scissor.left, state.scissor.right);
                for (w8_long x = 0; x < width; ++x) {
                    const AccumPixel value{table[pixels[x].blue], table[pixels[x].green],
                                           table[pixels[x].red], table[pixels[x].alpha]};
                    row[x] = operation == ACCUM_LOAD ? row[x] + value : value;
                }
                row += getWidth();
            }
            break;
        }
        case ACCUM_MULTIPLY: {
            for (w8_long y = 0; y < height; y++) {
                for (w8_long x = 0; x < width; x++) {
                    row[x] += addend;
                }
                row += getWidth();
            }
            break;
        }
        case ACCUM_ADD: {
            for (w8_long y = 0; y < height; y++) {
                for (w8_long x = 0; x < width; x++) {
                    for (unsigned channel = 0; channel < 4; ++channel)
                        row[x][channel] = static_cast<short>(srFloatToInt(row[x][channel] * scale));
                }
                row += getWidth();
            }
            break;
        }
        case ACCUM_RETURN: {
            unsigned char table[512];
            for (w8_long i = 0; i < 512; i++) {
                float level = i * (1.0f / 255.0f);
                if (level <= 0.0f) {
                    level = 0.0f;
                } else if (level >= 1.0f) {
                    level = 1.0f;
                }
                table[i] = (unsigned char)(w8_long)(level * scale * 255.0f + 0.5f);
            }
            for (w8_long y = 0; y < height; y++) {
                for (w8_long x = 0; x < width; x++) {
                    const auto indices = glm::u16vec4(row[x]) >> static_cast<unsigned short>(7);
                    const glm::u8vec4 color{table[indices.x], table[indices.y], table[indices.z],
                                            table[indices.w]};
                    // The device row interface consumes four packed BGRA bytes.
                    std::memcpy(&pixels[x], &color, sizeof(srARGB));
                }
                surface->setPixelRow(
                    reinterpret_cast<const w8_ulong*>(pixels), // reinterpret-ok: ARGB row buffer
                    // through the dword pixel-row ABI
                    state.scissor.top + y, state.scissor.left, state.scissor.right);
                row += getWidth();
            }
            break;
        }
        default:
            setError(ERROR_INVALID_ENUM);
        }
    }
    if (surface != 0) {
        unlockBuffer();
    }
}

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

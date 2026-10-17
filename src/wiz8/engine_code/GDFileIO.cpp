#include "wiz8/engine_code/GDFileIO.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "asset_paths.h"
#include "geometry.h"
#include "glm/ext/vector_float3.hpp"
#include "surrender/srBoundingBox.h"
#include "surrender/srDebug.h"
#include "wiz8/binary_reader.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/LevelFile.h"
#include "wiz8/engine_code/OctBuildTree.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/octree_spatial.h"
#include "wiz8/filesystem.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/retail_geometry_records.h"
#include "wiz8/retail_geometry_types.h"
#include "wiz8/retail_text_records.h"

constexpr float kSurfaceFlatNormalThreshold = 0.9959999918937683f;

class Trigger;

// GLOBAL: WIZ8 0x005ec1ac
// GLOBAL: WIZ8 0x005ec1b0
// GLOBAL: WIZ8 0x005ec1b4
// GLOBAL: WIZ8 0x005ff56c

// GLOBAL: WIZ8 0x00659a58
static int g_integrated_trigger_count;

// GLOBAL: WIZ8 0x00603ab8
float g_default_momentum_scale = 0.30000001192092896f;
// GLOBAL: WIZ8 0x00603abc
float g_default_motion_limit = 112.5f;

// GLOBAL: WIZ8 0x005ec1a4
const float g_path_endpoint_scale = 0.9900000095367432f;

/* Opens a game-data file, builds its record, and pulls the polygon and
   vertex banks through the record reader. */
static std::unique_ptr<W8GameData> ReadGeometry(std::unique_ptr<wiz8::File> file, bool secondary)
{
    auto game_data = std::make_unique<W8GameData>(nullptr, secondary);
    game_data->ReadWGDList(*file, 0);
    game_data->ReadWGDList(*file, 1);
    return game_data;
}

// FUNCTION: WIZ8 0x00447570
std::unique_ptr<W8GameData> ReadGameData(const char* path, bool secondary)
{
    std::unique_ptr<wiz8::File> file;
    try {
        file = wiz8::open_file(wiz8::asset_path(path));
    } catch (const std::exception&) {
        return 0; // Retail: a level without game data (e.g. Test\DefaultSky) is valid.
    }
    try {
        return ReadGeometry(std::move(file), secondary);
    } catch (const std::exception&) {
        return 0;
    }
}

/* The WGD face record's fixed head: three vertex indexes, the source plane,
   and a version tag checked before the rest of the record is read. */
struct W8GDFaceHeader {
    std::int32_t vertex_indices[3];
    float plane[3];
    std::int32_t version;
};
static_assert(sizeof(W8GDFaceHeader) == 0x1c);

/* The WGD face record's tail: classification flag, slope/value pair, footstep
   selectors, trace chance, and the trigger index the writer overrode. */
struct W8GDFaceData { /* 0x18 */
    int type;
    float slope;
    float contact_margin;
    unsigned char material;
    unsigned char surface;
    unsigned char pad_0e[2];
    int chance;
    int trigger_index;
};

/* The conditional-face record following a non-primary face: the group key the
   interface compiler buckets on and the interface's name. */
struct W8GDExtendedFace { /* 0x44 */
    int group;
    char name[0x40];
};

namespace {
struct ConditionalFace {
    int interface_id, surface, group;
};
struct InterfaceBank {
    std::vector<W8GDInterface> interfaces;
    std::vector<W8GDInterfaceState> states;
    std::vector<int> polygons{0};
};
InterfaceBank BuildInterfaces(std::size_t count, const std::vector<ConditionalFace>& faces)
{
    InterfaceBank bank;
    bank.interfaces.resize(count);
    if (!count) {
        bank.polygons.clear();
        return bank;
    }
    struct Group {
        int id;
        std::vector<int> polygons;
    };
    std::vector<std::vector<Group>> groups(count);
    for (std::size_t id = 1; id < count; ++id) groups[id].push_back({0, {}});
    for (const auto& face : faces) {
        auto& list = groups.at(face.interface_id);
        auto group = std::find_if(list.begin(), list.end(),
                                  [&](const auto& entry) { return entry.id == face.group; });
        if (group == list.end()) {
            list.push_back({face.group, {}});
            group = std::prev(list.end());
        }
        group->polygons.push_back(face.surface);
    }
    for (std::size_t id = 1; id < count; ++id) {
        bank.interfaces[id] = {int(id), int(groups[id].size()), int(bank.states.size())};
        for (const auto& [group, polygons] : groups[id]) {
            bank.states.push_back({group, int(polygons.size()), int(bank.polygons.size())});
            bank.polygons.insert(bank.polygons.end(), polygons.begin(), polygons.end());
            bank.polygons.push_back(0);
        }
    }
    return bank;
}
}

// FUNCTION: WIZ8 0x00447660
unsigned char W8GameData::ReadWGDList(wiz8::File& file, int poly_type)
{
    if (poly_type < 0 || poly_type > 2) throw std::runtime_error("invalid WGD poly type");
    wiz8::BinaryReader reader(file);
    const auto vertex_count = reader.record<int>();
    const auto face_count = reader.record<int>();
    if (vertex_count < 0 || face_count < 0 || vertex_count > 200000 || face_count > 200000 ||
        VerticesCount() > std::numeric_limits<int>::max() - vertex_count ||
        SurfacesCount() > std::numeric_limits<int>::max() - face_count)
        throw std::runtime_error("invalid WGD record count");
    if (!vertex_count && !face_count) return 0;
    if (!vertex_count || !face_count) throw std::runtime_error("incomplete WGD bank counts");
    if (poly_type == 0 && (VerticesCount() || SurfacesCount()))
        throw std::runtime_error("primary WGD bank already loaded");
    reader.require(std::size_t(vertex_count) * 12 +
                   std::size_t(face_count) * (poly_type ? 0x78 : 0x34) + 24);

    std::vector<glm::vec3> vertices;
    std::vector<W8GDSurface> surfaces;
    if (VerticesCount()) vertices = m_pVertices;
    if (SurfacesCount()) surfaces = m_pSurfaces;
    auto names = interface_names;
    std::vector<ConditionalFace> conditional;
    // Preserve the earlier secondary banks when rebuilding the interface table.
    for (int id = 1; id < InterfacesCount(); ++id) {
        const auto& interface = m_pInterfaces[id];
        for (int state = 0; state < interface.state_count; ++state) {
            const auto& group = m_pStates[interface.iStates + state];
            for (int poly = 0; poly < group.poly_count; ++poly)
                conditional.push_back({id, m_piCondPolys[group.poly_first + poly], group.group});
        }
    }
    const auto input_vertices = reader.array<wiz8::retail::Vec3f>(vertex_count);
    if (!std::isfinite(kWorldScale) || kWorldScale <= 0)
        throw std::runtime_error("invalid world scale");
    auto bank_min =
        glm::vec3(input_vertices.front().x, input_vertices.front().y, input_vertices.front().z) *
        kWorldScale;
    auto bank_max = bank_min;
    for (const auto& vertex : input_vertices) {
        const auto transformed = glm::vec3(vertex.x, vertex.y, vertex.z) * kWorldScale;
        for (int axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(transformed[axis])) throw std::runtime_error("nonfinite WGD vertex");
            bank_min[axis] = std::min(bank_min[axis], transformed[axis]);
            bank_max[axis] = std::max(bank_max[axis], transformed[axis]);
        }
        vertices.push_back(transformed);
    }
    surfaces.reserve(surfaces.size() + face_count);
    for (int index = 0; index < face_count; ++index) {
        const auto header = reader.record<W8GDFaceHeader>();
        if (header.version != 2) throw std::runtime_error("wrong WGD version");
        for (int vertex : header.vertex_indices)
            if (vertex < 0 || vertex >= vertex_count)
                throw std::runtime_error("invalid WGD vertex index");
        const auto data = reader.record<W8GDFaceData>();
        W8GDSurface surface{};
        surface.contact_margin = data.contact_margin;
        surface.slope = data.slope;
        surface.chance = data.chance;
        surface.footstep_material = data.material;
        surface.footstep_surface = data.surface;
        surface.flags = data.type == 1 ? W8_GD_SURFACE_WALKABLE | W8_GD_SURFACE_PATHFINDING : 0;
        unsigned axis = 0;
        for (unsigned candidate = 1; candidate < 3; ++candidate)
            if (fabs(header.plane[candidate]) > fabs(header.plane[axis])) axis = candidate;
        surface.flags |= axis;
        for (int corner = 0; corner < 3; ++corner) {
            surface.vertex_indices[corner] = header.vertex_indices[corner] + VerticesCount();
            surface.edge_link[corner] = -1;
        }
        surface.index = int(surfaces.size());
        ClassifySurfacePlane(vertices.data(), &surface);
        if (poly_type) {
            const auto extended = reader.record<W8GDExtendedFace>();
            if (!memchr(extended.name, '\0', sizeof(extended.name)))
                throw std::runtime_error("unterminated WGD interface name");
            auto name = std::find(names.begin(), names.end(), extended.name);
            if (name == names.end()) {
                names.emplace_back(extended.name);
                name = std::prev(names.end());
            }
            surface.trigger_index = int(name - names.begin()) + 1;
            conditional.push_back({surface.trigger_index, int(surface.index), extended.group});
            if (extended.group) surface.flags |= W8_GD_SURFACE_CONDITIONAL_DISABLED;
        }
        surfaces.push_back(surface);
    }
    const auto lower = reader.record<glm::vec3>();
    const auto upper = reader.record<glm::vec3>();
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(lower[axis] * kWorldScale) ||
            !std::isfinite(upper[axis] * kWorldScale) || lower[axis] > upper[axis])
            throw std::runtime_error("nonfinite WGD bounds");
        bank_min[axis] = std::min(bank_min[axis], lower[axis] * kWorldScale);
        bank_max[axis] = std::max(bank_max[axis], upper[axis] * kWorldScale);
        if (VerticesCount()) {
            bank_min[axis] = std::min(bank_min[axis], minimum[axis]);
            bank_max[axis] = std::max(bank_max[axis], maximum[axis]);
        }
    }
    auto interfaces = BuildInterfaces(names.empty() ? 0 : names.size() + 1, conditional);
    // Move the fully validated banks into their permanent owners.
    m_pVertices.swap(vertices);
    m_pSurfaces.swap(surfaces);

    m_pInterfaces.swap(interfaces.interfaces);
    m_pStates.swap(interfaces.states);
    m_piCondPolys.swap(interfaces.polygons);
    geometry_index.reset();
    interface_names.swap(names);
    minimum = bank_min;
    maximum = bank_max;
    return 1;
}

/* Answers the 1-based ordinal of the name-table entry matching `name`,
   else -1. */
// FUNCTION: WIZ8 0x004482A0
int W8GameData::FindPointerByName(const char* name)
{
    const auto found = std::find(interface_names.begin(), interface_names.end(), name);
    return found == interface_names.end() ? -1 : int(found - interface_names.begin()) + 1;
}

void W8GameData::AddTriggerTriangle(int trigger_index, int vertex_0, int vertex_1, int vertex_2)
{
    m_pTrigSurfaces.emplace_back();
    W8GDSurface* surface = &m_pTrigSurfaces.back();
    surface->flags = W8_GD_SURFACE_CROSSING;
    surface->index = SurfacesCount() + TrigSurfacesCount() - 1;
    surface->trigger_index = trigger_index;
    surface->contact_margin = 1.1f;
    surface->vertex_indices[0] = vertex_0;
    surface->vertex_indices[1] = vertex_1;
    surface->vertex_indices[2] = vertex_2;
    ClassifySurfacePlane(m_pTrigVertices.data(), surface);
    for (int& vertex_index : surface->vertex_indices) {
        vertex_index += VerticesCount();
    }
    surface->edge_link[0] = -1;
    surface->edge_link[1] = -1;
    surface->edge_link[2] = -1;
    surface->hit_plane = 0;
}

// FUNCTION: WIZ8 0x00448310
void W8GameData::AddTriggerPlane(const glm::vec3* trigger_vertices, Trigger* trigger)
{
    int trigger_index = 0;
    int index;
    if (octree != 0) {
        if (m_ppTriggers.empty()) {
            g_integrated_trigger_count = 0;
            m_ppTriggers.resize(m_iNumTriggers);
        }
        if (g_integrated_trigger_count >= m_iNumTriggers) {
            srAssertFail("(iTriggerCount < m_iNumTriggers)",
                         "C:\\Projects\\Wizardry 8\\Engine Code\\GDFileIO.cpp", 0x259,
                         "AddTriggerPlane: Too many triggers for trigger array.");
        }
        m_ppTriggers[g_integrated_trigger_count++] = trigger;
        return;
    }

    if (TrigSurfacesCount() >= 500) {
        srAssertFail("(TrigSurfacesCount() < MAX_TRIG_SURFACES)",
                     "C:\\Projects\\Wizardry 8\\Engine Code\\GDFileIO.cpp", 0x26c, 0);
    }

    for (index = 0; index < m_iNumTriggers && trigger_index == 0; ++index) {
        if (m_ppTriggers[index] == trigger) {
            trigger_index = index;
        }
    }
    if (trigger_index == 0) {
        trigger_index = m_iNumTriggers++;
        m_ppTriggers.push_back(trigger);
    }
    for (index = 0; index < 4; ++index) {
        m_pTrigVertices.push_back(trigger_vertices[index]);
    }

    AddTriggerTriangle(trigger_index, TrigVerticesCount() - 4, TrigVerticesCount() - 3,
                       TrigVerticesCount() - 2);

    AddTriggerTriangle(trigger_index, TrigVerticesCount() - 2, TrigVerticesCount() - 1,
                       TrigVerticesCount() - 4);
}

// FUNCTION: WIZ8 0x00448840
void W8GameData::IntegrateTriggers()
{
    if (TrigVerticesCount() == 0) {
        return;
    }

    m_pVertices.insert(m_pVertices.end(), m_pTrigVertices.begin(), m_pTrigVertices.end());

    m_pTrigVertices.clear();
    integrated_surface_count = TrigSurfacesCount();

    int end = SurfacesCount() + TrigSurfacesCount();
    for (int index = SurfacesCount(); index < end; ++index) {
        W8GDSurface* surface = index < SurfacesCount() ? &m_pSurfaces[index]
                                                       : &m_pTrigSurfaces[index - SurfacesCount()];
        geometry_index->InsertSurface(surface, m_pVertices, 3);
    }
    pending_trigger_bits.assign(m_iNumTriggers, false);
    active_trigger_bits.assign(m_iNumTriggers, false);
}

/* Append an owned environment derived from the linked surface plane. */
// FUNCTION: WIZ8 0x00448E60

struct W8ProcessedGameDataHeader {
    unsigned int version;
    glm::vec3 minimum;
    glm::vec3 maximum;
    int vertex_count;
    int surface_count;
    int trigger_surface_base;
    int trigger_surface_count;
    int integrated_surface_count;
    int interface_count;
    int state_count;
    int trigger_count;
    int cond_poly_count;
    int environ_count;
    unsigned char reserved[0x24];
};

// FUNCTION: WIZ8 0x0041a820
unsigned char W8EnvironRecord::RescaleToReference(const W8EnvironRecord* reference)
{
    if (reference == 0) {
        float difference = static_cast<float>(fabs(g_navigator_gravity + vector.y));
        if (g_navigator_gravity * kCameraSnapEpsilon < difference) {
            return 1;
        }
        difference = static_cast<float>(fabs(forward_scale - g_camera_level_forward_scale));
        if (g_camera_level_forward_scale * kCameraSnapEpsilon < difference) {
            return 1;
        }
        difference = static_cast<float>(fabs(motion_limit - g_default_motion_limit));
        if (g_default_motion_limit * kCameraSnapEpsilon < difference) {
            return 1;
        }
        difference = static_cast<float>(fabs(momentum_scale - g_default_momentum_scale));
        if (g_default_momentum_scale * kCameraSnapEpsilon < difference) {
            return 1;
        }
        return 0;
    }

    float scale = g_navigator_gravity / -reference->gravity_y;
    gravity_x *= scale;
    gravity_y *= scale;
    gravity_z *= scale;
    forward_scale *= g_camera_level_forward_scale / reference->forward_scale;
    motion_limit *= g_default_motion_limit / reference->motion_limit;
    momentum_scale *= g_default_momentum_scale / reference->momentum_scale;
    return 0;
}

/* Read the processed GameData header and all of its variable-size banks. */
// FUNCTION: WIZ8 0x00449240
void W8GameData::ReadProcessedGameData(wiz8::File* handle)
{
    wiz8::BinaryReader reader(*handle);
    const auto header = reader.record<W8ProcessedGameDataHeader>();
    if (header.version != 1) {
        throw std::runtime_error("ReadProcessedGameData: Unsupported version.");
    }
    const auto remaining = handle->size() - handle->tell();
    std::uint64_t bytes = 0;
    const auto count_bytes = [&](int count, std::size_t stride) {
        if (count < 0 || count > std::numeric_limits<int>::max() - 2) {
            throw std::runtime_error("ReadProcessedGameData: Invalid record count.");
        }
        bytes += static_cast<std::uint64_t>(count) * stride;
    };
    count_bytes(header.vertex_count, sizeof(wiz8::retail::Vec3f));
    count_bytes(header.surface_count, sizeof(wiz8::retail::GDSurface));
    count_bytes(header.interface_count, sizeof(W8GDInterface));
    count_bytes(header.state_count, sizeof(W8GDInterfaceState));
    count_bytes(header.cond_poly_count, sizeof(int));
    count_bytes(header.environ_count, sizeof(wiz8::retail::Environ));
    // Trigger bits have no serialized bank; their IDs are carried by surfaces.
    if (remaining < 0 || bytes > static_cast<std::uint64_t>(remaining) ||
        header.trigger_count < 0 || header.trigger_count > header.surface_count ||
        header.trigger_surface_base < 0 || header.trigger_surface_count < 0 ||
        header.trigger_surface_base > header.surface_count ||
        header.trigger_surface_count > header.surface_count - header.trigger_surface_base ||
        header.integrated_surface_count < 0 ||
        header.integrated_surface_count > header.surface_count) {
        throw std::runtime_error("ReadProcessedGameData: Invalid geometry banks.");
    }

    std::vector<glm::vec3> vertices;
    vertices.reserve(header.vertex_count);
    for (const auto& image : reader.array<wiz8::retail::Vec3f>(header.vertex_count))
        vertices.emplace_back(image.x, image.y, image.z);
    std::vector<W8GDSurface> surfaces;
    surfaces.reserve(header.surface_count);
    for (const auto& image : reader.array<wiz8::retail::GDSurface>(header.surface_count))
        wiz8::retail::decode_image(image, surfaces.emplace_back());
    auto interfaces = reader.array<W8GDInterface>(header.interface_count);
    auto states = reader.array<W8GDInterfaceState>(header.state_count);
    auto cond_polys = reader.array<int>(header.cond_poly_count);
    std::vector<W8EnvironRecord> environs(header.environ_count);
    for (auto& environ : environs) wiz8::retail::read_exact(*handle, environ);
    for (const auto& surface : surfaces) {
        for (int vertex : surface.vertex_indices)
            if (vertex < 0 || vertex >= header.vertex_count)
                throw std::runtime_error("invalid processed vertex index");
    }
    for (std::size_t id = 1; id < interfaces.size(); ++id) {
        const auto& interface = interfaces[id];
        if (interface.id != int(id) || interface.iStates < 0 || interface.state_count < 0 ||
            interface.iStates > header.state_count ||
            interface.state_count > header.state_count - interface.iStates)
            throw std::runtime_error("invalid processed interface states");
    }
    for (const auto& state : states) {
        if (state.poly_first < 0 || state.poly_count < 0 ||
            state.poly_first > header.cond_poly_count ||
            state.poly_count > header.cond_poly_count - state.poly_first)
            throw std::runtime_error("invalid processed polygon slice");
        for (int poly = 0; poly < state.poly_count; ++poly) {
            const auto index = cond_polys[state.poly_first + poly];
            if (index < 0 || index >= header.surface_count)
                throw std::runtime_error("invalid processed conditional surface");
        }
    }
    std::vector<bool> pending_bits(header.trigger_count);
    std::vector<bool> active_bits(header.trigger_count);
    if (!environs.empty() && environs[0].RescaleToReference(0) != 0) {
        for (std::size_t index = 1; index < environs.size(); ++index) {
            environs[index].RescaleToReference(&environs[0]);
        }
        environs[0].RescaleToReference(&environs[0]);
    }

    std::vector<std::unique_ptr<W8EnvironRecord>> environment_objects;
    for (const auto& environ : environs)
        environment_objects.push_back(std::make_unique<W8EnvironRecord>(environ));
    m_pVertices.swap(vertices);
    m_pSurfaces.swap(surfaces);

    m_pInterfaces.swap(interfaces);
    m_pStates.swap(states);
    m_piCondPolys.swap(cond_polys);
    m_ppEnvirons.swap(environment_objects);
    geometry_index.reset();

    minimum = header.minimum;
    maximum = header.maximum;
    trigger_surface_base = header.trigger_surface_base;
    trigger_surface_count = header.trigger_surface_count;
    integrated_surface_count = header.integrated_surface_count;
    m_iNumTriggers = header.trigger_count;
    m_ppTriggers.clear();
    m_pTrigVertices.clear();
    m_pTrigSurfaces.clear();
    pending_trigger_bits = std::move(pending_bits);
    active_trigger_bits = std::move(active_bits);
    interface_names.clear();
}

/* 0x0044902E is the constructor's shared body entry: the SEH wrapper at
   0x00449010 zeroes EBX and falls through into the code below. */
// FUNCTION: WIZ8 0x00449010
W8GameData::W8GameData(wiz8::File* handle, bool)
{
    geometry_index = 0;
    octree = 0;
    integrated_surface_count = 0;
    trigger_surface_count = 0;
    trigger_surface_base = 0;

    m_iNumTriggers = 0;
    last_hit_surface = 0;

    trace_flag4_gate = false;
    minimum = glm::vec3(1.0e8f);
    maximum = glm::vec3(-1.0e8f);
    if (handle != 0) {
        ReadProcessedGameData(handle);
    }
    if (EnvironsCount() == 0) {
        m_ppEnvirons.push_back(std::make_unique<W8EnvironRecord>());
    }
}

/* Build the processed level's spatial index once and publish every surface
   from its primary 0x4c-byte bank.  The constructor expands only local bounds,
   leaving the serialized GameData limits unchanged. */
// FUNCTION: WIZ8 0x004497c0
unsigned char InitializeGameData(W8GameData* game_data)
{
    if (game_data == 0) {
        return 0;
    }

    glm::vec3 minimum = game_data->minimum;
    glm::vec3 maximum = game_data->maximum;
    if (game_data->geometry_index == 0) {
        game_data->geometry_index =
            std::make_unique<W8OctBuildTree>(2000.0f, srBoundingBox{minimum, maximum}, 0x40, 0);
    }

    for (int index = 0; index < game_data->SurfacesCount(); ++index) {
        if (game_data->geometry_index->InsertSurface(&game_data->m_pSurfaces[index],
                                                     game_data->m_pVertices, 3) == 0) {
            return 0;
        }
    }
    return 1;
}

/* Rebuild one indexed surface plane and derive the runtime classification
   carried by the level-geometry record. Bit 0x80 requests dominant-axis
   selection; bit 4 is the walkable slope classification. */
// FUNCTION: WIZ8 0x004498c0
void ClassifySurfacePlane(const glm::vec3* vertices, W8GDSurface* surface)
{
    surface->plane =
        trianglePlane(vertices[surface->vertex_indices[0]], vertices[surface->vertex_indices[1]],
                      vertices[surface->vertex_indices[2]]);

    unsigned int flags = surface->flags;
    if ((flags & W8_GD_SURFACE_CROSSING) != 0) {
        const auto dominant_axis = dominantPlaneAxis(surface->plane.normal);
        flags |= dominant_axis;
        surface->flags = flags;
    }

    if ((surface->flags & W8_GD_SURFACE_WALKABLE) != 0) {
        surface->flags |= W8_GD_SURFACE_PATHFINDING;
    }

    float upper_value = 1.0f;
    if (0.5f < surface->plane.normal.y) {
        if ((surface->flags & W8_GD_SURFACE_WALKABLE) == 0 &&
            kSurfaceFlatNormalThreshold < surface->plane.normal.y) {
            surface->flags |= W8_GD_SURFACE_WALKABLE;
            surface->slope = 1.0f;
        }
        if (surface->slope < 0.0f) {
            surface->flags |= W8_GD_SURFACE_EXPLICIT_SLOPE;
            surface->slope = 0.0f;
        }
    } else if (surface->contact_margin < 1.01f && g_path_endpoint_scale < surface->contact_margin &&
               (surface->flags & W8_GD_SURFACE_WALKABLE) != 0) {
        surface->contact_margin = 0.1f;
    }

    flags = surface->flags;
    surface->contact_margin *= kWorldScale;
    if ((flags & W8_GD_SURFACE_WALKABLE) == 0) {
        surface->slope = 0.0f;
    } else if (surface->slope < kVectorLengthSquaredEpsilon &&
               (flags & W8_GD_SURFACE_EXPLICIT_SLOPE) == 0) {
        if (surface->plane.normal.y <= 0.75f) {
            upper_value = surface->plane.normal.y;
        }
        surface->slope = upper_value;
    }
    surface->flags = flags & ~W8_GD_SURFACE_COLLISION_PROCESSED;
}

/* Release level globals; member owners release the runtime banks. */
// FUNCTION: WIZ8 0x00449BB0
W8GameData::~W8GameData() = default;

#include "wiz8/engine_code/GDFileIO.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "geometry.h"
#include "glm/ext/vector_float3.hpp"
#include "surrender/srBoundingBox.h"
#include "surrender/srMath.h"
#include "wiz8/binary_reader.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/GameTimeAccumulator.h"
#include "wiz8/engine_code/LevelFile.h"
#include "wiz8/engine_code/OctBuildPreTree.h"
#include "wiz8/engine_code/OctBuildTree.h"
#include "wiz8/engine_code/OctPreTree.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/materials.h"
#include "wiz8/filesystem.h"
#include "wiz8/float_constants.h"
#include "wiz8/index_algorithms.h"
#include "wiz8/retail_geometry_records.h"
#include "wiz8/retail_text_records.h"
#include "wiz8/sr_api.h"
#include "wiz8/surface_geometry.h"

class Trigger;

// GLOBAL: WIZ8 0x005ec1a8
const float g_float_negative_one_third = -0.3333333432674408f;
// GLOBAL: WIZ8 0x005ebc58
const float g_vector_length_squared_epsilon = 1.0000000116860974e-07f;
// GLOBAL: WIZ8 0x005ec028
const float g_float_one_and_one_hundredth = 1.0099999904632568f;
// GLOBAL: WIZ8 0x005ec1a0
const float g_surface_flat_normal_threshold = 0.9959999918937683f;
// GLOBAL: WIZ8 0x005ec1ac
const float g_vertex_sort_x_weight = 3000.0f;
// GLOBAL: WIZ8 0x005ec1b0
const float g_vertex_sort_y_weight = 60000.0f;
// GLOBAL: WIZ8 0x005ec1b4
const float g_vertex_sort_z_weight = 900000.0f;
// GLOBAL: WIZ8 0x005ff56c
static char g_string[] = "\n";

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
static W8GameData* ReadGeometry(std::unique_ptr<wiz8::File> file, bool secondary)
{
    auto game_data = std::make_unique<W8GameData>(nullptr, secondary);
    const auto got_polygons = game_data->ReadWGDList(*file, 0);
    const auto got_vertices = game_data->ReadWGDList(*file, 1);
    if (got_vertices == 0 && got_polygons == 0) {
        ReportBuildStatus(7, "ReadGameData: No polygons or vertices in GameData!\n");
    }
    return game_data.release();
}

// FUNCTION: WIZ8 0x00447570
W8GameData* ReadGameData(const char* path, bool secondary)
{
    std::unique_ptr<wiz8::File> file;
    try {
        file = wiz8::open_file(path);
    } catch (const std::exception&) {
        return 0; // Retail: a level without game data (e.g. Test\DefaultSky) is valid.
    }
    try {
        return ReadGeometry(std::move(file), secondary);
    } catch (const std::exception& error) {
        ReportBuildStatus(7, error.what());
        return 0;
    }
}

W8GameData* ReadHostGameData(const std::filesystem::path& path, bool secondary)
{
    try {
        return ReadGeometry(wiz8::open_host_file(path), secondary);
    } catch (const std::exception& error) {
        ReportBuildStatus(7, error.what());
        return 0;
    }
}

/* The WGD face record's fixed head: three vertex indexes, the source plane,
   and a version tag checked before the rest of the record is read. */
struct W8GDFaceHeader { /* 0x1c */
    int vertex_indices[3];
    srVector3T<float> plane;
    int version;
};

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
    if (!std::isfinite(g_world_scale) || g_world_scale <= 0)
        throw std::runtime_error("invalid world scale");
    auto bank_min =
        glm::vec3(input_vertices.front().x, input_vertices.front().y, input_vertices.front().z) *
        g_world_scale;
    auto bank_max = bank_min;
    for (const auto& vertex : input_vertices) {
        const auto transformed = glm::vec3(vertex.x, vertex.y, vertex.z) * g_world_scale;
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
            if (fabs((&header.plane.x)[candidate]) > fabs((&header.plane.x)[axis]))
                axis = candidate;
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
    const auto lower = reader.record<srVector3T<float>>();
    const auto upper = reader.record<srVector3T<float>>();
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite((&lower.x)[axis] * g_world_scale) ||
            !std::isfinite((&upper.x)[axis] * g_world_scale) || (&lower.x)[axis] > (&upper.x)[axis])
            throw std::runtime_error("nonfinite WGD bounds");
        bank_min[axis] = std::min(bank_min[axis], (&lower.x)[axis] * g_world_scale);
        bank_max[axis] = std::max(bank_max[axis], (&upper.x)[axis] * g_world_scale);
        if (VerticesCount()) {
            bank_min[axis] = std::min(bank_min[axis], (&minimum.x)[axis]);
            bank_max[axis] = std::max(bank_max[axis], (&maximum.x)[axis]);
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
    for (int index = 0; index < 3; ++index) {
        surface->vertex_indices[index] += VerticesCount();
    }
    surface->edge_link[0] = -1;
    surface->edge_link[1] = -1;
    surface->edge_link[2] = -1;
    surface->hit_plane = 0;
}

// FUNCTION: WIZ8 0x00448310
void W8GameData::AddTriggerPlane(const srVector3T<float>* trigger_vertices, Trigger* trigger)
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
        m_pTrigVertices.push_back(trigger_vertices[index].glmValue());
    }

    AddTriggerTriangle(trigger_index, TrigVerticesCount() - 4, TrigVerticesCount() - 3,
                       TrigVerticesCount() - 2);

    AddTriggerTriangle(trigger_index, TrigVerticesCount() - 2, TrigVerticesCount() - 1,
                       TrigVerticesCount() - 4);
}

/* Registers a level-file plane's two triangles (vertices 0,1,2 and 2,3,0) as
   a trigger-surface pair under the auto-numbered trigger index. */
// FUNCTION: WIZ8 0x004485F0
void W8GameData::AddLevelPlane(const W8LevelFilePlane& plane)
{
    int index;
    const srVector3T<float>* vertices = plane.vertices;

    if (TrigSurfacesCount() >= 500) {
        srAssertFail("(TrigSurfacesCount() < MAX_TRIG_SURFACES)",
                     "C:\\Projects\\Wizardry 8\\Engine Code\\GDFileIO.cpp", 0x2cc, 0);
    }
    for (index = 0; index < 4; ++index) {
        m_pTrigVertices.emplace_back(vertices[index].x * g_world_scale,
                                     vertices[index].y * g_world_scale,
                                     vertices[index].z * g_world_scale);
    }

    AddTriggerTriangle(m_iNumTriggers, TrigVerticesCount() - 4, TrigVerticesCount() - 3,
                       TrigVerticesCount() - 2);

    AddTriggerTriangle(m_iNumTriggers, TrigVerticesCount() - 2, TrigVerticesCount() - 1,
                       TrigVerticesCount() - 4);
    ++m_iNumTriggers;
    m_ppTriggers.push_back(nullptr);
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
        geometry_index->InsertSurface(surface, 3);
    }
    pending_trigger_bits.assign(m_iNumTriggers, false);
    active_trigger_bits.assign(m_iNumTriggers, false);
}

/* The CompileGameData counterpart of IntegrateTriggers: folds the
   trigger banks into the main vertex and surface arrays without rebuilding
   the spatial index. */
// FUNCTION: WIZ8 0x00448A60
void W8GameData::IntegrateTriggerGeometry()
{
    if (TrigVerticesCount() != 0) {
        m_pVertices.insert(m_pVertices.end(), m_pTrigVertices.begin(), m_pTrigVertices.end());
        m_pSurfaces.insert(m_pSurfaces.end(), m_pTrigSurfaces.begin(), m_pTrigSurfaces.end());

        integrated_surface_count = TrigSurfacesCount();
        m_pTrigVertices.clear();
        m_pTrigSurfaces.clear();
    }
}

/* Registers the linked record's vertices as twelve trigger surfaces. */
// FUNCTION: WIZ8 0x00448BF0
void W8GameData::AddLinkedRecord(const srVector3T<float>* vertices, float value, float scalar,
                                 const signed char* face)
{
    AddTriggerPlane(vertices, value, scalar, face);
}

/* Appends a linked record's vertices to the trigger bank, scaled by
   g_double_five_hundred, and emits twelve consecutive trigger surfaces under the
   current environment index. The surface numbered `*face` also grows an
   environment record scaled by `value`/`scalar`. */
// FUNCTION: WIZ8 0x00448C60
void W8GameData::AddTriggerPlane(const srVector3T<float>* vertices, float value, float scalar,
                                 const signed char* face)
{
    const int environment_id = EnvironsCount();
    if (!face || *face < 0 || *face >= 12)
        throw std::runtime_error("invalid linked environment face");
    int index;
    int vertex_base;

    if (TrigSurfacesCount() >= 500) {
        srAssertFail("(TrigSurfacesCount() < MAX_TRIG_SURFACES)",
                     "C:\\Projects\\Wizardry 8\\Engine Code\\GDFileIO.cpp", 0x3b9, 0);
    }
    vertex_base = TrigVerticesCount();
    for (index = 0; index < 36; ++index) {
        m_pTrigVertices.emplace_back(static_cast<float>(vertices[index].x * g_double_five_hundred),
                                     static_cast<float>(vertices[index].y * g_double_five_hundred),
                                     static_cast<float>(vertices[index].z * g_double_five_hundred));
    }
    for (index = 0; index < 12; ++index) {
        const auto ordinal = m_pTrigSurfaces.size();
        m_pTrigSurfaces.emplace_back();
        W8GDSurface* surface = &m_pTrigSurfaces.back();
        surface->flags = W8_GD_SURFACE_CROSSING_MASK;
        surface->index = int(ordinal) + SurfacesCount();
        surface->trigger_index = environment_id;
        surface->vertex_indices[0] = vertex_base;
        surface->vertex_indices[1] = vertex_base + 1;
        surface->vertex_indices[2] = vertex_base + 2;
        surface->contact_margin = 0.0f;
        vertex_base += 3;
        ClassifySurfacePlane(m_pTrigVertices.data(), surface);
        if (index == *face) {
            CreateGDEnviron(surface, value);
            W8EnvironRecord* environ_record = m_ppEnvirons.back().get();
            environ_record->forward_scale *= scalar;
            environ_record->motion_limit =
                environ_record->forward_scale * environ_record->momentum_scale * 2.0f;
        }
        surface->vertex_indices[0] += VerticesCount();
        surface->vertex_indices[1] += VerticesCount();
        surface->vertex_indices[2] += VerticesCount();
    }
}

/* Append an owned environment derived from the linked surface plane. */
// FUNCTION: WIZ8 0x00448E60
void W8GameData::CreateGDEnviron(const W8GDSurface* surface, float scale)
{
    m_ppEnvirons.push_back(std::make_unique<W8EnvironRecord>());
    m_ppEnvirons.back()->gravity_x = g_navigator_gravity * surface->plane.normal.x * scale;
    m_ppEnvirons.back()->gravity_y =
        (scale * surface->plane.normal.y - g_float_one) * g_navigator_gravity;
    m_ppEnvirons.back()->gravity_z = g_navigator_gravity * surface->plane.normal.z * scale;
}

struct W8ProcessedGameDataHeader {
    unsigned int version;
    srVector3T<float> minimum;
    srVector3T<float> maximum;
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
        if (g_navigator_gravity * g_camera_snap_epsilon < difference) {
            return 1;
        }
        difference = static_cast<float>(fabs(forward_scale - g_camera_level_forward_scale));
        if (g_camera_level_forward_scale * g_camera_snap_epsilon < difference) {
            return 1;
        }
        difference = static_cast<float>(fabs(motion_limit - g_default_motion_limit));
        if (g_default_motion_limit * g_camera_snap_epsilon < difference) {
            return 1;
        }
        difference = static_cast<float>(fabs(momentum_scale - g_default_momentum_scale));
        if (g_default_momentum_scale * g_camera_snap_epsilon < difference) {
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
    count_bytes(header.vertex_count, sizeof(srVector3T<float>));
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
    const auto active_environment =
        std::find_if(m_ppEnvirons.begin(), m_ppEnvirons.end(),
                     [](const auto& environment) { return environment.get() == g_environ; });
    const bool replace_active_environ = active_environment != m_ppEnvirons.end();
    const auto active_environment_index = active_environment - m_ppEnvirons.begin();
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
    if (replace_active_environ) {
        g_environ =
            m_ppEnvirons.empty()
                ? nullptr
                : m_ppEnvirons[active_environment_index < EnvironsCount() ? active_environment_index
                                                                          : 0]
                      .get();
    }
}

/* Builds the processed game-data record in place: zeroed storage, bound
   extremes, the shared engine-time object on first use, a default
   environment bank, and the previous level-data teardown. */
/* 0x0044902E is the constructor's shared body entry: the SEH wrapper at
   0x00449010 zeroes EBX and falls through into the code below. */
// FUNCTION: WIZ8 0x00449010
W8GameData::W8GameData(wiz8::File* handle, bool secondary)
{
    geometry_index = 0;
    octree = 0;
    integrated_surface_count = 0;
    trigger_surface_count = 0;
    trigger_surface_base = 0;

    m_iNumTriggers = 0;
    last_hit_surface = 0;

    trace_flag4_gate = false;
    minimum = 1.0e8f;
    maximum = -1.0e8f;
    if (!secondary) {
        MoveTimer(4);
        if (g_game_time_accumulator == 0) {
            g_game_time_accumulator = new W8GameTimeAccumulator();
        }
    }
    if (handle != 0) {
        ReadProcessedGameData(handle);
    }
    if (g_environ != 0) {
        delete g_environ;
    }
    if (EnvironsCount() == 0) {
        m_ppEnvirons.push_back(std::make_unique<W8EnvironRecord>());
    }
    W8LevelDataRecord* old_level = g_level_data;
    g_environ = m_ppEnvirons[0].get();
    if (old_level != 0) {
        delete old_level;
        g_level_data = 0;
    }
    SetOctreeGameData(this);
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

    srVector3T<float> minimum = game_data->minimum;
    srVector3T<float> maximum = game_data->maximum;
    if (game_data->geometry_index == 0) {
        game_data->geometry_index = std::make_unique<W8OctBuildTree>(
            2000.0f, srBoundingBox{minimum.glmValue(), maximum.glmValue()}, 0x40, 0);
    }

    for (int index = 0; index < game_data->SurfacesCount(); ++index) {
        if (game_data->geometry_index->InsertSurface(&game_data->m_pSurfaces[index], 3) == 0) {
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

    float upper_value = g_float_one;
    if (g_float_half < surface->plane.normal.y) {
        if ((surface->flags & W8_GD_SURFACE_WALKABLE) == 0 &&
            g_surface_flat_normal_threshold < surface->plane.normal.y) {
            surface->flags |= W8_GD_SURFACE_WALKABLE;
            surface->slope = g_float_one;
        }
        if (surface->slope < g_float_zero) {
            surface->flags |= W8_GD_SURFACE_EXPLICIT_SLOPE;
            surface->slope = g_float_zero;
        }
    } else if (surface->contact_margin < g_float_one_and_one_hundredth &&
               g_path_endpoint_scale < surface->contact_margin &&
               (surface->flags & W8_GD_SURFACE_WALKABLE) != 0) {
        surface->contact_margin = 0.1f;
    }

    flags = surface->flags;
    surface->contact_margin *= g_world_scale;
    if ((flags & W8_GD_SURFACE_WALKABLE) == 0) {
        surface->slope = g_float_zero;
    } else if (surface->slope < g_vector_length_squared_epsilon &&
               (flags & W8_GD_SURFACE_EXPLICIT_SLOPE) == 0) {
        if (surface->plane.normal.y <= g_float_three_quarters) {
            upper_value = surface->plane.normal.y;
        }
        surface->slope = upper_value;
    }
    surface->flags = flags & ~W8_GD_SURFACE_COLLISION_PROCESSED;
}

/* Release level globals; member owners release the runtime banks. */
// FUNCTION: WIZ8 0x00449BB0
W8GameData::~W8GameData()
{

    ReleaseLevelData();
    m_iNumTriggers = 0;

    SetOctreeGameData(0);
}

bool ShareSurfaceEdge(W8GDSurface* first, W8GDSurface* second, glm::vec3* vertices);
static void LinkSurfaceEdge(int polygon, int edge,
                            std::unordered_multimap<unsigned int, int>* table,
                            W8GDSurface* surfaces, unsigned int multiplier, glm::vec3* vertices);

/* Welds duplicate vertices through a spatial hash, repacks the surface array
   collision-flag faces first, fills the shared build vertex/polygon arrays
   and stitches polygon edge links. */
static void InitializeCompiledSurface(W8GDSurface* compiled, const W8GDSurface* surface,
                                      int polygon_count, W8OctPreTreeGeometry& geometry)
{
    *compiled = *surface;
    compiled->edge_link[2] = -1;
    compiled->edge_link[1] = -1;
    compiled->edge_link[0] = -1;
    compiled->hit_plane = 0;
    W8OctRegionPolygon* polygon = geometry.m_polygons.data() + polygon_count;
    polygon->ordinal = polygon_count;
    polygon->plane = compiled->plane;
    polygon->degenerate = 0;
    polygon->visited = false;
    polygon->vertices[0] = geometry.m_vertices.data() + compiled->vertex_indices[0];
    polygon->vertices[1] = geometry.m_vertices.data() + compiled->vertex_indices[1];
    polygon->vertices[2] = geometry.m_vertices.data() + compiled->vertex_indices[2];
}

// FUNCTION: WIZ8 0x00449D10
W8OctPreTreeGeometry W8GameData::CompileGameData()
{
    std::unordered_map<unsigned int, int> weld_table;
    std::unordered_multimap<unsigned int, int> edge_table;
    char message[1024];
    int i;
    int j;
    unsigned int progress = 0;
    int redundant = 0;
    int weld_count = 0;
    bool announce = false;
    bool found;

    ReportStartupMessage(g_string);
    ReportStartupMessage("Processing GameData geometry...\n");
    IntegrateTriggerGeometry();
    if (VerticesCount() == 0 || SurfacesCount() == 0) {
        return {};
    }

    std::vector<W8OctPreTreeVertex> weld_storage(VerticesCount());
    auto* weld_records = weld_storage.data();
    W8OctPreTreeGeometry geometry{};
    geometry.m_vertices.resize(VerticesCount());
    std::vector<int> cond_polys(CondPolysCount());
    std::vector<glm::vec3> compiled_vertices(VerticesCount());
    auto* new_vertices = compiled_vertices.data();

    if (!m_pVertices.empty() && 0 < VerticesCount()) {
        W8OctPreTreeVertex* vertex = weld_records;
        const glm::vec3* source = m_pVertices.data();
        glm::vec3* new_vertex = new_vertices;
        W8OctPreTreeVertex* gd_vertex = geometry.m_vertices.data();
        for (i = 0; i < VerticesCount(); ++i) {
            vertex->position = *source;
            unsigned int percent =
                static_cast<unsigned int>(i * g_octree_cell_scale / VerticesCount());
            if (progress + 10 < percent) {
                announce = true;
                progress += 10;
            }
            found = false;
            unsigned int key = static_cast<unsigned int>(
                vertex->position.z * g_float_one_five_hundredth * g_vertex_sort_z_weight +
                vertex->position.y * g_float_one_five_hundredth * g_vertex_sort_y_weight +
                vertex->position.x * g_float_one_five_hundredth * g_vertex_sort_x_weight);
            int linked = LookupIndexValue(weld_table, key);
            if (linked == 0) {
                int vertex_id = i + 1;
                weld_table.insert_or_assign(key, vertex_id);
            } else {
                int last = 0;
                while (linked != 0) {
                    if (found) {
                        break;
                    }
                    int candidate_index = linked - 1;
                    W8OctPreTreeVertex* candidate = weld_records + candidate_index;
                    if (fabs(vertex->position.x - candidate->position.x) >= g_camera_snap_epsilon ||
                        fabs(vertex->position.y - candidate->position.y) >= g_camera_snap_epsilon ||
                        fabs(vertex->position.z - candidate->position.z) >= g_camera_snap_epsilon) {
                        last = candidate_index;
                        linked = candidate->m_kind;
                    } else {
                        vertex->m_vertex_index = candidate->m_vertex_index;
                        ++redundant;
                        found = true;
                        linked = candidate_index;
                    }
                }
                if (!found) {
                    weld_records[last].m_kind = weld_count + 1;
                }
            }
            if (!found) {
                *new_vertex = *source;
                vertex->m_vertex_index = weld_count;
                *gd_vertex = *vertex;
                ++weld_count;
                ++gd_vertex;
                ++new_vertex;
            }
            if (announce) {
                sprintf(message, "  %d%% Complete:  %d Redundant Vertices  \r", progress,
                        redundant);
                ReportStartupMessage(message);
            }
            vertex->m_visited = false;
            ++vertex;
            ++source;
            announce = false;
        }
    }

    for (i = 0; i < SurfacesCount(); ++i) {
        W8GDSurface* surface = m_pSurfaces.data() + i;
        for (j = 0; j < 3; ++j) {
            surface->vertex_indices[j] = weld_records[surface->vertex_indices[j]].m_vertex_index;
        }
    }
    compiled_vertices.resize(weld_count);
    unsigned int multiplier =
        weld_count < 0xffff ? 0xffff : 0xffffffffu / static_cast<unsigned int>(weld_count);

    geometry.m_polygons.resize(SurfacesCount());
    std::vector<W8GDSurface> compiled_surfaces(SurfacesCount());
    auto* new_surfaces = compiled_surfaces.data();

    int polygon_count = 0;
    const auto order = surfaceGeometry::compilationOrder(m_pSurfaces);
    for (const auto old_index : order.indices) {
        auto& surface = m_pSurfaces[old_index];
        surface.index = polygon_count;
        if (!(surface.flags & W8_GD_SURFACE_WALKABLE)) surface.slope = 0;
        InitializeCompiledSurface(new_surfaces + polygon_count, &surface, polygon_count, geometry);
        for (int edge = 0; edge < 3; ++edge)
            LinkSurfaceEdge(polygon_count, edge, &edge_table, new_surfaces, multiplier,
                            new_vertices);
        for (int conditional = 0; conditional < CondPolysCount(); ++conditional)
            if (m_piCondPolys[conditional] == old_index) cond_polys[conditional] = polygon_count;
        ++polygon_count;
    }
    const int walkable_count = order.walkable_count;
    trigger_surface_count = polygon_count - walkable_count;
    trigger_surface_base = walkable_count;
    compiled_surfaces.resize(polygon_count);

    m_pVertices.swap(compiled_vertices);
    m_pSurfaces.swap(compiled_surfaces);

    m_piCondPolys.swap(cond_polys);
    sprintf(message, "GameData:  %d Polygons,  \t%d Vertices.\n\n", polygon_count, weld_count);
    ReportBuildStatus(6, message);
    return geometry;
}

/* Tests two polygons for a shared vertex pair; when they share an edge the
   matching corner slot on each surface is linked to the other's index. */
// FUNCTION: WIZ8 0x0044A970
bool ShareSurfaceEdge(W8GDSurface* first, W8GDSurface* second, glm::vec3* vertices)
{
    int first_slot = -1;
    int second_slot = -1;
    int last_first = -1;
    int last_second = -1;
    int* first_index = first->vertex_indices;
    for (int i = 0; i < 3; ++i) {
        int* second_index = second->vertex_indices;
        for (int j = 0; j < 3; ++j) {
            if (*first_index == *second_index) {
                if (first_slot < 0) {
                    second_slot = j;
                    first_slot = i;
                } else {
                    last_first = i;
                    last_second = j;
                }
            }
            ++second_index;
        }
        ++first_index;
    }
    if (last_first < 0) {
        return false;
    }
    if ((last_first < first_slot && last_first - first_slot < 2) ||
        (first_slot < last_first && 1 < last_first - first_slot)) {
        first_slot = last_first;
    }
    if ((last_second < second_slot && last_second - second_slot < 2) ||
        (second_slot < last_second && 1 < last_second - second_slot)) {
        second_slot = last_second;
    }
    first->edge_link[first_slot] = second->index;
    second->edge_link[second_slot] = first->index;
    return true;
}

/* Registers one triangle edge in the edge hash under its undirected vertex
   pair key; when an earlier polygon carries the same key the pair is handed
   to ShareSurfaceEdge to link. */
// FUNCTION: WIZ8 0x0044A7D0
static void LinkSurfaceEdge(int polygon, int edge,
                            std::unordered_multimap<unsigned int, int>* table,
                            W8GDSurface* surfaces, unsigned int multiplier, glm::vec3* vertices)
{
    W8GDSurface* surface = surfaces + polygon;
    int next = (edge + 1) % 3;
    int low = edge;
    int high = next;
    if (surface->vertex_indices[next] < surface->vertex_indices[edge]) {
        low = next;
        high = edge;
    }
    unsigned int key = surface->vertex_indices[low] * multiplier + surface->vertex_indices[high];
    const auto [first, last] = table->equal_range(key);
    for (auto candidate = first; candidate != last; ++candidate) {
        if (candidate->second != 0 &&
            ShareSurfaceEdge(surfaces + candidate->second, surface, vertices)) {
            return;
        }
    }
    InsertIndexValue(*table, key, polygon);
}

// FUNCTION: WIZ8 0x0044aa40
unsigned char W8GameData::WriteGameData(wiz8::File* handle)
{
    W8ProcessedGameDataHeader header;
    int index;

    header.version = 1;
    header.minimum = minimum;
    header.maximum = maximum;
    header.vertex_count = VerticesCount();
    header.surface_count = SurfacesCount();
    header.trigger_surface_base = trigger_surface_base;
    header.trigger_surface_count = trigger_surface_count;
    header.integrated_surface_count = integrated_surface_count;
    header.interface_count = InterfacesCount();
    header.state_count = StatesCount();
    header.trigger_count = m_iNumTriggers;
    header.cond_poly_count = CondPolysCount();
    header.environ_count = EnvironsCount();
    memset(header.reserved, 0, sizeof(header.reserved));

    if (handle == 0) {
        ReportBuildStatus(7, "WriteGameData: File not open.\n");
        return 0;
    }
    handle->write(&header, sizeof(header));
    std::vector<wiz8::retail::Vec3f> vertex_images;
    vertex_images.reserve(m_pVertices.size());
    for (const auto& point : m_pVertices) vertex_images.push_back({point.x, point.y, point.z});
    handle->write(vertex_images.data(), vertex_images.size() * sizeof(wiz8::retail::Vec3f));
    for (const auto& surface : m_pSurfaces) wiz8::retail::write(*handle, surface);
    handle->write(m_pInterfaces.data(),
                  static_cast<std::size_t>(InterfacesCount()) * sizeof(W8GDInterface));
    handle->write(m_pStates.data(),
                  static_cast<std::size_t>(StatesCount()) * sizeof(W8GDInterfaceState));
    handle->write(m_piCondPolys.data(), static_cast<std::size_t>(CondPolysCount()) * sizeof(int));
    for (index = 0; index < EnvironsCount(); ++index) {
        wiz8::retail::write(*handle, *m_ppEnvirons[index]);
    }
    return 1;
}

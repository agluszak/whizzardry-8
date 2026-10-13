#include "glm/ext/vector_int3.hpp"
#include "surrender/bounding_radius.h"
#include <ranges>
#include <glm/gtc/constants.hpp>
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>
#include <bit>
#include <array>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "glm/common.hpp"
#include "glm/geometric.hpp"
#include "glm/vector_relational.hpp"
#include "surrender/srModeler.h"
#include "surrender/plane_math.h"
#include "surrender/triangle_projection.h"
#include "surrender/srTriangulator.h"
#include "compat/compiler.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_double2.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "surrender/srMeshModel.h"
#include "surrender/srShader.h"

class srMaterialIFace;
class srTextureIFace;

/* autoSmooth's worker: buckets triangle indices by deduplicated shade vertex, turns every triangle
   pair coincident at a vertex into a candidate edge weighted by the facing/material test, floods
   smooth-group bits through the smooth edges, then copies the assigned mask back into
   Triangle::flags. */
namespace {

class AutoSmoother {
public:
    AutoSmoother(srModeler::Triangle* triangles, w8_ulong triangle_count,
                 srModeler::VertexHash* hash, double threshold, int smooth);
    void smooth();

    /* Triangles sharing one shade-group vertex. */
    struct VertexEntry {
        std::vector<w8_ulong> triangles;
    };

    /* One triangle-pair coincidence at a shared vertex: smooth is the edge
       test result, group the assigned smooth group (-1 until assigned). */
    struct Edge {
        int smooth;
        w8_long group;
        w8_ulong first;
        w8_ulong second;
    };

    /* Per triangle: its edge list plus the assigned/blocked group masks;
       groups becomes the triangle's new flags. */
    struct TriangleEntry {
        TriangleEntry() : blocked(0), groups(0) {}

        std::vector<w8_ulong> edges;
        w8_ulong blocked;
        w8_ulong groups;
    };

    int isSmooth(w8_ulong first, w8_ulong second, double cosine, int smooth);
    void markEdge(Edge* edge, w8_long group);
    void assignGroups(w8_long group);

    srModeler::Triangle* triangles;
    w8_ulong triangle_count;
    srModeler::VertexHash* hash;
    std::vector<VertexEntry> vertices;
    std::vector<Edge> edges;
    std::vector<TriangleEntry> entries;
};

// FUNCTION: SURRENDER 0x100370B0
AutoSmoother::AutoSmoother(srModeler::Triangle* triangles, w8_ulong triangle_count,
                           srModeler::VertexHash* hash, double threshold, int smooth)
    : triangles(triangles), triangle_count(triangle_count), hash(hash)
{
    // Shade indices identify positions shared by triangles, including attribute seams.
    std::size_t vertex_count = 0;
    for (w8_ulong i = 0; i < hash->unique_count; ++i)
        vertex_count = std::max(vertex_count, std::size_t(hash->entries[i].shade_index) + 1);
    vertices.resize(vertex_count);
    for (w8_ulong i = 0; i < triangle_count; ++i)
        for (int corner = 0; corner < 3; ++corner)
            vertices[hash->table[i * 3 + corner]->shade_index].triangles.push_back(i);
    entries.resize(triangle_count);
    const double cosine = std::cos(threshold);
    for (const auto& vertex : vertices)
        for (std::size_t i = 0; i < vertex.triangles.size(); ++i)
            for (std::size_t j = i + 1; j < vertex.triangles.size(); ++j) {
                const auto first = vertex.triangles[i], second = vertex.triangles[j];
                const auto index = static_cast<w8_ulong>(edges.size());
                edges.push_back({isSmooth(first, second, cosine, smooth), -1, first, second});
                entries[first].edges.push_back(index);
                entries[second].edges.push_back(index);
            }
}

// FUNCTION: SURRENDER 0x100375D0
int AutoSmoother::isSmooth(w8_ulong first, w8_ulong second, double cosine, int smooth)
{
    const srModeler::Triangle* first_triangle = &triangles[first];
    const srModeler::Triangle* second_triangle = &triangles[second];
    if (smooth == 0) {
        for (w8_long pass = 0; pass < 4; ++pass) {
            if (first_triangle->shaders[pass].value != second_triangle->shaders[pass].value) {
                return 0;
            }
            for (w8_long layer = 0; layer < 2; ++layer) {
                if (first_triangle->textures[pass][layer] !=
                    second_triangle->textures[pass][layer]) {
                    return 0;
                }
            }
        }
    }
    const auto normal = [](const srModeler::Triangle& triangle) {
        const auto& v = triangle.vertices;
        return srPlaneMath::triangleNormal(v[1].position, v[0].position, v[2].position);
    };
    const auto first_normal = normal(*first_triangle);
    const auto second_normal = normal(*second_triangle);
    const float magnitude = glm::length(first_normal) * glm::length(second_normal);
    return magnitude == 0 || glm::dot(first_normal, second_normal) / magnitude > cosine;
}

// FUNCTION: SURRENDER 0x100378B0
void AutoSmoother::markEdge(Edge* edge, w8_long group)
{
    edge->group = group;
    const w8_ulong bit = 1u << (group & 0x1f);
    for (const auto triangle : {edge->first, edge->second}) {
        entries[triangle].groups |= bit;
        for (const auto index : entries[triangle].edges) {
            const auto& other = edges[index];
            if (!other.smooth)
                entries[other.first == triangle ? other.second : other.first].blocked |= bit;
        }
    }
}

// FUNCTION: SURRENDER 0x100379C0
void AutoSmoother::assignGroups(w8_long group)
{
    if (group >= 0x1f) {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "%s", "srModeler::autoSmooth() ran out of groups.");
        for (auto& edge : edges) {
            if (edge.group == -1) {
                entries[edge.first].groups |= 0x80000000;
                entries[edge.second].groups |= 0x80000000;
                edge.group = 0x1f;
            }
        }
        return;
    }
    for (w8_ulong index = 0; index < edges.size(); ++index) {
        Edge* edge = &edges[index];
        if (edge->smooth != 0 && edge->group == -1) {
            ++group;
            markEdge(edge, group);
            /* Rescan from the seed edge: a smooth edge with no hard-edge
               neighbour blocking this group joins it immediately. */
            for (w8_ulong scan = index; scan < edges.size(); ++scan) {
                Edge* other = &edges[scan];
                if (other->smooth != 0 && other->group == -1) {
                    w8_ulong bit = 1u << (group & 0x1f);
                    if ((entries[other->first].blocked & bit) == 0 &&
                        (entries[other->second].blocked & bit) == 0) {
                        markEdge(other, group);
                    }
                }
            }
            assignGroups(group);
        }
    }
}

// FUNCTION: SURRENDER 0x10037B20
void AutoSmoother::smooth()
{
    w8_ulong flags = 0;
    for (w8_ulong i = 0; i < triangle_count; ++i)
        flags |= triangles[i].flags;
    if (!flags)
        return;
    // Start one below the highest occupied group so the seed reuses that slot.
    const int highest = std::bit_width(flags) - 1;
    if (highest < 31) {
        assignGroups(highest - 1);
        for (w8_ulong i = 0; i < triangle_count; ++i)
            triangles[i].flags = entries[i].groups;
    }
}

} // namespace


// FUNCTION: SURRENDER 0x10037BC0
// RECOMP: ??0Vertex@srModeler@@QAE@XZ
srModeler::Vertex::Vertex()
{
    reset();
}

// FUNCTION: SURRENDER 0x100386A0
void srModeler::Vertex::reset()
{
    position = glm::vec3(0);
    shade_index = 0;
    for (int pass = 0; pass < 4; ++pass) {
        dig[pass] = glm::vec3(0);
        dcg[pass] = scg[pass] = glm::vec3(1);
        weights[pass] = 1;
        for (int side = 0; side < 2; ++side)
            materials[pass][side].reset();
    }
    std::fill(std::begin(uv), std::end(uv), glm::vec2(0));
}

// FUNCTION: SURRENDER 0x10038420
void srModeler::Vertex::interpolate(const Vertex& first, const Vertex& second, float fraction)
{
    position = glm::mix(first.position, second.position, fraction);
    for (int pass = 0; pass < 4; ++pass) {
        dig[pass] = glm::mix(first.dig[pass], second.dig[pass], fraction);
        scg[pass] = glm::mix(first.scg[pass], second.scg[pass], fraction);
        dcg[pass] = glm::mix(first.dcg[pass], second.dcg[pass], fraction);
        weights[pass] = glm::mix(first.weights[pass], second.weights[pass], fraction);
    }
    for (int slot = 0; slot < 8; ++slot)
        uv[slot] = glm::mix(first.uv[slot], second.uv[slot], fraction);
    shade_index = 0;
}

// FUNCTION: SURRENDER 0x10038B50
srModeler::Triangle::Triangle() : flags(0)
{
    reset();
}

static void ResetModelerFace(auto& face, int vertex_count)
{
    for (int vertex = 0; vertex < vertex_count; ++vertex) {
        face.vertices[vertex].reset();
    }
    for (int pass = 0; pass < 4; ++pass) {
        face.shaders[pass] = srShader();
        face.textures[pass][0].reset();
        face.textures[pass][1].reset();
    }
    face.flags = 1;
    face.disabled = 0;
}

// FUNCTION: SURRENDER 0x10038AF0
void srModeler::Triangle::reset()
{
    ResetModelerFace(*this, 3);
}

// FUNCTION: SURRENDER 0x10038840
void srModeler::Triangle::flipFacing()
{
    std::swap(vertices[1], vertices[2]);
}

// FUNCTION: SURRENDER 0x10038890
srModeler::Polygon::Polygon(int vertices) : vertices(vertices), vertex_count(vertices), flags(0)
{
    reset();
}

// FUNCTION: SURRENDER 0x100389A0
void srModeler::Polygon::reset()
{
    ResetModelerFace(*this, vertex_count);
}

// FUNCTION: SURRENDER 0x10038A00
void srModeler::Polygon::reAllocate(int vertices)
{
    if (this->vertices.size() < static_cast<std::size_t>(vertices)) {
        this->vertices.assign(vertices, Vertex{});
        reset();
    }
    vertex_count = vertices;
}

// FUNCTION: SURRENDER 0x1003BAE0
srModeler::srModeler()
{
    triangle_count = 0;
    pass_count = 1;
}

// FUNCTION: SURRENDER 0x10039A70
void srModeler::discard()
{
    setTriangleCount(0);
    pass_count = 1;
}

// FUNCTION: SURRENDER 0x10037C00
w8_ulong srModeler::getTriangleCount() const
{
    return triangle_count;
}

// FUNCTION: SURRENDER 0x1003A460
void srModeler::setTriangleCount(w8_ulong triangles)
{
    this->triangles.resize(triangles);
    triangle_count = triangles;
}

// FUNCTION: SURRENDER 0x1003A480
w8_ulong srModeler::addTriangle(const Triangle& triangle)
{
    triangles.push_back(triangle);
    triangle_count = static_cast<w8_ulong>(triangles.size());
    return triangle_count - 1;
}

// FUNCTION: SURRENDER 0x1003A330
std::optional<srModeler::Triangle> srModeler::getTriangle(w8_ulong index) const
{
    if (triangle_count <= index)
        return std::nullopt;
    return triangles[index];
}

// FUNCTION: SURRENDER 0x1003B0B0
void srModeler::setTriangle(w8_ulong index, const Triangle& triangle)
{
    if (index < triangle_count) {
        triangles[index] = triangle;
    }
}

// FUNCTION: SURRENDER 0x10039CC0
void srModeler::setTriangleVertex(w8_ulong triangle, w8_ulong vertex, const Vertex& value)
{
    if (triangle < triangle_count && vertex < 3) {
        triangles[triangle].vertices[vertex] = value;
    }
}

// FUNCTION: SURRENDER 0x10039C80
void srModeler::flipTriangle(w8_ulong triangle)
{
    if (triangle < triangle_count) {
        triangles[triangle].flipFacing();
    }
}

// FUNCTION: SURRENDER 0x10039C40
void srModeler::flipTriangles()
{
    for (auto& triangle : triangles)
        triangle.flipFacing();
}

// FUNCTION: SURRENDER 0x1003A420
void srModeler::enableTriangle(w8_ulong triangle)
{
    if (triangle < triangle_count) {
        triangles[triangle].disabled = 0;
    }
}

// FUNCTION: SURRENDER 0x1003A3E0
void srModeler::disableTriangle(w8_ulong triangle)
{
    if (triangle < triangle_count) {
        triangles[triangle].disabled = 1;
    }
}

// FUNCTION: SURRENDER 0x1003A0F0
w8_ulong srModeler::getEnabledTriangleCount()
{
    return std::count_if(triangles.begin(), triangles.end(),
                         [](const Triangle& triangle) { return !triangle.disabled; });
}

// FUNCTION: SURRENDER 0x1003A130
void srModeler::removeDisabledTriangles()
{
    std::erase_if(triangles, [](const Triangle& triangle) { return triangle.disabled != 0; });
    triangle_count = static_cast<w8_ulong>(triangles.size());
}

// FUNCTION: SURRENDER 0x1003A240
void srModeler::disableDegenerateTriangles()
{
    for (auto& triangle : triangles) {
        const auto& vertices = triangle.vertices;
        const auto normal = srPlaneMath::triangleNormal(vertices[0].position, vertices[1].position,
                                                         vertices[2].position);
        if (normal == glm::vec3(0))
            triangle.disabled = 1;
    }
}

// FUNCTION: SURRENDER 0x10038D60
void srModeler::addFromModeler(srModeler& other)
{
    // Copy first so appending the modeler to itself is safe.
    const auto incoming = other.triangles;
    triangles.insert(triangles.end(), incoming.begin(), incoming.end());
    triangle_count = static_cast<w8_ulong>(triangles.size());
}

template<class Transform>
static void TransformModelerPositions(std::span<srModeler::Triangle> triangles, Transform transform)
{
    for (auto& triangle : triangles)
        for (auto& vertex : triangle.vertices)
            vertex.position = transform(vertex.position);
}

// FUNCTION: SURRENDER 0x10039E10
void srModeler::scale(const glm::vec3& scale)
{
    TransformModelerPositions(triangles, [&](const glm::vec3& position) { return position * scale; });
}

// FUNCTION: SURRENDER 0x10039F90
void srModeler::scale(w8_ulong triangle, const glm::vec3& scale)
{
    if (triangle < triangles.size())
        TransformModelerPositions({&triangles[triangle], 1},
                                  [&](const glm::vec3& position) { return position * scale; });
}

// FUNCTION: SURRENDER 0x10039E90
void srModeler::move(const glm::vec3& delta)
{
    TransformModelerPositions(triangles, [&](const glm::vec3& position) { return position + delta; });
}

// FUNCTION: SURRENDER 0x10039F10
void srModeler::move(w8_ulong triangle, const glm::vec3& delta)
{
    if (triangle < triangles.size())
        TransformModelerPositions({&triangles[triangle], 1},
                                  [&](const glm::vec3& position) { return position + delta; });
}

// FUNCTION: SURRENDER 0x10039D20
void srModeler::rotate(const glm::mat3& matrix)
{
    TransformModelerPositions(triangles, [&](const glm::vec3& position) { return matrix * position; });
}

// FUNCTION: SURRENDER 0x1003A010
void srModeler::rotate(w8_ulong triangle, const glm::mat3& matrix)
{
    if (triangle < triangle_count)
        TransformModelerPositions({&triangles[triangle], 1},
                                  [&](const glm::vec3& position) { return matrix * position; });
}

// FUNCTION: SURRENDER 0x1003AA80
double srModeler::getMaxVertexDist()
{
    auto positions = triangles | std::views::transform([](const auto& triangle) {
        return std::span{triangle.vertices};
    }) | std::views::join | std::views::transform([](const auto& vertex) {
        return vertex.position;
    });
    return boundingRadius(positions, glm::dvec3(0));
}

// FUNCTION: SURRENDER 0x1003AB30
std::optional<std::pair<float, float>> srModeler::getAxialBounds(e_axis axis) const
{
    if (axis < AXIS_X || axis > AXIS_Z)
        return std::nullopt;
    float minimum = triangles.empty() ? 0 : triangles.front().vertices[0].position[axis];
    float maximum = minimum;
    for (const auto& triangle : triangles)
        for (const auto& vertex : triangle.vertices) {
            minimum = std::min(minimum, vertex.position[axis]);
            maximum = std::max(maximum, vertex.position[axis]);
        }
    return std::pair{minimum, maximum};
}

// FUNCTION: SURRENDER 0x1003BB00
void srModeler::setPassCount(w8_long passes)
{
    pass_count = std::clamp(passes, w8_long{0}, w8_long{4});
}

// FUNCTION: SURRENDER 0x1003BB30
w8_long srModeler::getPassCount() const
{
    return pass_count;
}

// FUNCTION: SURRENDER 0x10038BF0
srModeler::VertexHash::VertexHash(w8_ulong vertex_count)
    : entries(vertex_count), table(vertex_count), unique_count(0)
{
}

// FUNCTION: SURRENDER 0x100390A0
w8_ulong srModeler::VertexHash::hash(double x, double y, double z)
{
    return static_cast<int>(x * 12345.6f + y * 1714.3849f + z * 27561.3f) & 0x3ff;
}

// FUNCTION: SURRENDER 0x10038DB0
std::unique_ptr<srModeler::VertexHash> srModeler::getUniqueVertexList()
{
    if (triangle_count == 0) {
        return nullptr;
    }
    auto hash = std::make_unique<VertexHash>(triangle_count * 3);
    Triangle* triangle = triangles.data();
    w8_ulong unique = 0;
    w8_ulong slot = 0;
    const double radius = getMaxVertexDist();
    const double scale = radius > 0 ? 1.0 / radius : 1.0;
    for (w8_ulong index = 0; index < triangle_count; ++index) {
        w8_ulong flags = triangle->flags;
        for (auto& triangle_vertex : triangle->vertices) {
            Vertex* source = &triangle_vertex;
            w8_ulong group = 0xffffffff;
            w8_ulong bucket = VertexHash::hash(
                source->position.x * scale, source->position.y * scale, source->position.z * scale);
            VertexHash::Entry* entry;
            for (entry = hash->buckets[bucket]; entry != 0; entry = entry->next) {
                Vertex* other = entry->vertex;
                if (glm::all(glm::lessThan(
                        glm::abs((glm::dvec3(source->position) - glm::dvec3(other->position)) *
                                 scale),
                        glm::dvec3(0.0001f))) &&
                    (entry->flags & flags) != 0 && source->shade_index == other->shade_index) {
                    group = entry->shade_index;
                }
                if (*source == *other && (entry->flags & flags) != 0) {
                    hash->table[slot] = entry;
                    break;
                }
            }
            if (entry == 0) {
                VertexHash::Entry* created = &hash->entries[unique];
                hash->table[slot] = created;
                created->flags = flags;
                created->vertex = source;
                created->index = unique;
                if (group == 0xffffffff) {
                    created->shade_index = unique;
                } else {
                    created->shade_index = group;
                }
                created->next = hash->buckets[bucket];
                ++unique;
                hash->buckets[bucket] = created;
            }
            ++slot;
        }
        ++triangle;
    }
    hash->unique_count = unique;
    return hash;
}

// FUNCTION: SURRENDER 0x100390D0
w8_ulong srModeler::getUniqueVertexCount()
{
    auto hash = getUniqueVertexList();
    return hash->unique_count;
}

// FUNCTION: SURRENDER 0x1003A4A0
bool srModeler::isClockwise(std::span<const glm::vec2> points)
{
    // The signed polygon area gives the winding without normalizing every edge.
    double area = 0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const glm::dvec2 a(points[i]), b(points[(i + 1) % points.size()]);
        area += srTriangleProjection::orientedArea(glm::dvec2(0), a, b);
    }
    return area <= 0;
}

// FUNCTION: SURRENDER 0x1003A650
void srModeler::addPolygon(const Polygon& polygon)
{
    const int count = polygon.vertex_count;
    if (count < 3)
        return;
    Triangle triangle;
    triangle.flags = polygon.flags;
    triangle.disabled = polygon.disabled;
    for (int pass = 0; pass < 4; ++pass) {
        triangle.shaders[pass] = polygon.shaders[pass];
        for (int layer = 0; layer < 2; ++layer)
            triangle.textures[pass][layer] = polygon.textures[pass][layer];
    }
    if (count == 3) {
        std::copy_n(polygon.vertices.begin(), 3, std::begin(triangle.vertices));
        addTriangle(triangle);
        return;
    }
    glm::vec3 normal(0);
    for (int i = 0; i < count; ++i) {
        const auto& a = polygon.vertices[(i + count - 1) % count].position;
        const auto& b = polygon.vertices[i].position;
        const auto& c = polygon.vertices[(i + 1) % count].position;
        normal += glm::abs(srPlaneMath::triangleNormal(b, a, c));
    }
    const int omitted_axis = normal.z >= normal.x && normal.z >= normal.y ? 2
                             : normal.y >= normal.x                       ? 1
                                                                          : 0;
    const int u = omitted_axis == 0 ? 1 : 0;
    const int v = omitted_axis == 2 ? 1 : 2;
    std::vector<glm::vec2> points(count);
    for (int i = 0; i < count; ++i)
        points[i] = {polygon.vertices[i].position[u], polygon.vertices[i].position[v]};
    if (isClockwise(points))
        for (auto& point : points)
            point.y = -point.y;
    srTriangulator triangulator(points);
    while (const auto indices = triangulator.next()) {
        for (int i = 0; i < 3; ++i)
            triangle.vertices[i] = polygon.vertices[(*indices)[i]];
        addTriangle(triangle);
    }
}

static bool ValidPlanarMapping(w8_long pass, w8_long layer, const srModeler::MappingInfo& mapping)
{
    return pass >= 0 && pass < 4 && layer >= 0 && layer < 2 &&
           mapping.axis_u >= srModeler::AXIS_X && mapping.axis_u <= srModeler::AXIS_Z &&
           mapping.axis_v >= srModeler::AXIS_X && mapping.axis_v <= srModeler::AXIS_Z;
}

static void MapModelerPlane(std::span<srModeler::Triangle> triangles, int slot,
                            const srModeler::MappingInfo& mapping, const glm::vec2& minimum,
                            const glm::vec2& scale)
{
    for (auto& triangle : triangles)
        for (auto& vertex : triangle.vertices)
            vertex.uv[slot] =
                (glm::vec2(vertex.position[mapping.axis_u], -vertex.position[mapping.axis_v]) -
                 minimum) * scale + glm::vec2(mapping.u_offset, mapping.v_offset);
}

// FUNCTION: SURRENDER 0x1003AC00
void srModeler::planarMap(w8_long pass, w8_long layer, const MappingInfo& mapping)
{
    if (!ValidPlanarMapping(pass, layer, mapping) || triangles.empty()) return;
    const auto [u_min, u_max] = *getAxialBounds(mapping.axis_u);
    const auto [v_min, v_max] = *getAxialBounds(mapping.axis_v);
    const glm::vec2 minimum(u_min, v_min), maximum(u_max, v_max);
    const auto extent = maximum - minimum;
    const glm::vec2 scale(extent.x != 0 ? mapping.u_scale / extent.x : 0,
                          extent.y != 0 ? mapping.v_scale / extent.y : 0);
    MapModelerPlane(triangles, pass * 2 + layer, mapping, minimum, scale);
}

// FUNCTION: SURRENDER 0x1003AD60
void srModeler::planarMapAbsolute(w8_long pass, w8_long layer, const MappingInfo& mapping)
{
    if (!ValidPlanarMapping(pass, layer, mapping)) return;
    // Absolute mapping historically uses u_scale for both axes.
    MapModelerPlane(triangles, pass * 2 + layer, mapping, glm::vec2(0), glm::vec2(mapping.u_scale));
}

// FUNCTION: SURRENDER 0x1003AE30
void srModeler::removeMapping(w8_long pass, w8_long layer)
{
    if (pass >= 0 && pass < 4 && layer >= 0 && layer < 2)
        for (auto& triangle : triangles)
            for (auto& vertex : triangle.vertices)
                vertex.uv[pass * 2 + layer] = glm::vec2(0);
}

// FUNCTION: SURRENDER 0x1003AEC0
void srModeler::cylinderMap(w8_long pass, w8_long layer, const MappingInfo& mapping)
{
    if (pass < 0 || pass >= 4 || layer < 0 || layer >= 2 || triangles.empty() ||
        mapping.axis_u < AXIS_X || mapping.axis_u > AXIS_Z)
        return;
    const int axis = mapping.axis_u;
    const int second = axis == AXIS_X ? AXIS_Y : AXIS_X;
    const int third = axis == AXIS_Z ? AXIS_Y : AXIS_Z;
    const auto [minimum, maximum] = *getAxialBounds(mapping.axis_u);
    const float scale = maximum != minimum ? mapping.u_scale / (maximum - minimum) : 0;
    const int slot = pass * 2 + layer;
    for (auto& triangle : triangles) {
        for (auto& vertex : triangle.vertices) {
            const auto& position = vertex.position;
            const double angle = std::atan2(position[third], position[second]);
            vertex.uv[slot] = {
                (position[axis] - minimum) * scale + mapping.u_offset,
                float(-angle / glm::two_pi<double>() * mapping.v_scale + mapping.v_offset)};
        }
        // Keep each triangle continuous across the cylindrical seam.
        for (int i = 0; i < 3; ++i) {
            auto& a = triangle.vertices[i].uv[slot].y;
            auto& b = triangle.vertices[(i + 1) % 3].uv[slot].y;
            if (std::abs(a - b) > mapping.v_scale * 0.8f)
                (a <= b ? a : b) += mapping.v_scale;
        }
    }
}

/* Preserve each generator's vertex order and diagonal. Grid replaces corner
   1; curved surfaces replace corner 0. The lower sphere reverses the first
   triangle, then the second triangle reverses again around the same split. */
static void AppendModelerQuad(srModeler& modeler, const std::array<glm::vec3, 3>& corners,
                              const glm::vec3& opposite, int replaced_corner, bool reverse = false)
{
    srModeler::Triangle triangle;
    for (int corner = 0; corner < 3; ++corner) triangle.vertices[corner].position = corners[corner];
    if (reverse) triangle.flipFacing();
    modeler.addTriangle(triangle);
    triangle.vertices[replaced_corner].position = opposite;
    triangle.flipFacing();
    modeler.addTriangle(triangle);
}

// FUNCTION: SURRENDER 0x100398E0
void srModeler::createGrid(w8_long columns, w8_long rows)
{
    discard();
    columns = std::max(columns, w8_long{1});
    rows = std::max(rows, w8_long{1});
    for (int row = 0; row < rows; ++row)
        for (int column = 0; column < columns; ++column) {
            const float left = float(column) / columns - 0.5f;
            const float right = float(column + 1) / columns - 0.5f;
            const float bottom = float(row) / rows - 0.5f;
            const float top = float(row + 1) / rows - 0.5f;
            AppendModelerQuad(*this, {{{left, bottom, 0}, {left, top, 0}, {right, top, 0}}},
                               {right, bottom, 0}, 1);
        }
}

// FUNCTION: SURRENDER 0x10039580
void srModeler::createSphere(w8_long detail)
{
    discard();
    detail = std::max(detail, w8_long{3});
    const int bands = detail / 2;
    const auto point = [=](int band, int segment, float hemisphere) {
        if (band == bands)
            return glm::vec3(0, 0, hemisphere * 0.5f);
        segment %= detail;
        const double latitude = glm::half_pi<double>() * band / bands;
        const double longitude = glm::two_pi<double>() * segment / detail;
        return glm::vec3(0.5 * std::cos(latitude) * std::cos(longitude),
                         0.5 * std::cos(latitude) * std::sin(longitude),
                         hemisphere * 0.5 * std::sin(latitude));
    };
    for (int band = 0; band < bands; ++band)
        for (int segment = 0; segment < detail; ++segment)
            for (float hemisphere : {1.0f, -1.0f}) {
                AppendModelerQuad(*this,
                    {point(band, segment, hemisphere), point(band, segment + 1, hemisphere),
                     point(band + 1, segment, hemisphere)},
                    point(band + 1, segment + 1, hemisphere), 0, hemisphere < 0);
            }
}

// FUNCTION: SURRENDER 0x10039360
void srModeler::createTorus(w8_long major_segments, w8_long minor_segments, double radius)
{
    discard();
    major_segments = std::max(major_segments, w8_long{3});
    minor_segments = std::max(minor_segments, w8_long{3});
    radius = std::clamp(std::abs(radius), 0.0, 1.0) * 0.5;
    const auto point = [=](int major, int minor) {
        major %= major_segments;
        minor %= minor_segments;
        const double u = glm::two_pi<double>() * major / major_segments;
        const double v = glm::two_pi<double>() * minor / minor_segments;
        const double ring = 0.5 + radius * std::cos(v);
        return glm::vec3(ring * std::cos(u), ring * std::sin(u), radius * std::sin(v));
    };
    for (int major = 0; major < major_segments; ++major)
        for (int minor = 0; minor < minor_segments; ++minor) {
            AppendModelerQuad(*this,
                {point(major, minor), point(major + 1, minor), point(major, minor + 1)},
                point(major + 1, minor + 1), 0);
        }
}

// FUNCTION: SURRENDER 0x10039130
void srModeler::tesselateEdges(w8_ulong triangle, double threshold)
{
    if (triangle < triangle_count && 0.0 < threshold) {
        Triangle* source = &triangles[triangle];
        double longest = 0.0;
        int edge = 0;
        int vertex;
        for (vertex = 0; vertex < 3; ++vertex) {
            int next = (vertex + 1) % 3;
            const double distance = glm::distance(glm::dvec3(source->vertices[vertex].position),
                                                  glm::dvec3(source->vertices[next].position));
            if (longest < distance) {
                longest = distance;
                edge = vertex;
            }
        }
        if (threshold < longest && longest != 0.0) {
            Triangle child;
            for (int pass = 0; pass < 4; ++pass) {
                child.shaders[pass] = source->shaders[pass];
                for (int layer = 0; layer < 2; ++layer) {
                    child.textures[pass][layer] = source->textures[pass][layer];
                }
            }
            child.flags = source->flags;
            child.vertices[0] = source->vertices[(edge + 2) % 3];
            child.vertices[1] = source->vertices[edge];
            child.vertices[2].interpolate(source->vertices[edge], source->vertices[(edge + 1) % 3],
                                          0.5f);
            source->vertices[edge] = child.vertices[2];
            w8_ulong added = addTriangle(child);
            tesselateEdges(triangle, threshold);
            tesselateEdges(added, threshold);
        }
    }
}

// FUNCTION: SURRENDER 0x10039100
void srModeler::tesselateEdges(double threshold)
{
    w8_ulong count = triangle_count;
    for (w8_ulong index = 0; index < count; ++index) {
        tesselateEdges(index, threshold);
    }
}

// FUNCTION: SURRENDER 0x1003B940
void srModeler::autoSmooth(double threshold, int smooth)
{
    if (triangles.empty())
        return;
    auto hash = getUniqueVertexList();
    AutoSmoother smoother(triangles.data(), triangle_count, hash.get(), threshold, smooth);
    smoother.smooth();
}

// FUNCTION: SURRENDER 0x1003BB40
void srModeler::setMaterial(std::shared_ptr<srMaterialIFace> material, w8_long pass, srMeshModel::e_side side)
{
    if (pass >= 0 && pass < 4 &&
        (side == srMeshModel::SIDE_FRONT || side == srMeshModel::SIDE_BACK))
        for (auto& triangle : triangles)
            for (auto& vertex : triangle.vertices)
                vertex.materials[pass][side] = material;
}

// FUNCTION: SURRENDER 0x1003BBC0
void srModeler::setTexture(std::shared_ptr<srTextureIFace> texture, w8_long pass, w8_long layer)
{
    if (pass >= 0 && pass < 4 && layer >= 0 && layer < 2)
        for (auto& triangle : triangles)
            triangle.textures[pass][layer] = texture;
}

// FUNCTION: SURRENDER 0x1003BC20
void srModeler::setShader(srShader shader, w8_long pass)
{
    if (pass >= 0 && pass < 4)
        for (auto& triangle : triangles)
            triangle.shaders[pass] = shader;
}

// FUNCTION: SURRENDER 0x1003B160
void srModeler::convert(srMeshModel& model, int remove_degenerate)
{
    if (remove_degenerate) {
        disableDegenerateTriangles();
        removeDisabledTriangles();
    }
    if (triangles.empty() || pass_count == 0) {
        model.reset(0, 0);
        return;
    }
    auto hash = getUniqueVertexList();
    const auto unique = hash->unique_count;
    model.reset(triangle_count, unique);
    model.pass_count = pass_count;
    for (int pass = 0; pass < pass_count; ++pass) {
        const auto shader = triangles.front().shaders[pass];
        model.setShader(shader, pass);
        if (std::any_of(triangles.begin(), triangles.end(), [&](const Triangle& triangle) {
                return triangle.shaders[pass].value != shader.value;
            })) {
            auto* table = model.getPolyShader(pass, 1);
            for (std::size_t i = 0; i < triangles.size(); ++i)
                table[i] = triangles[i].shaders[pass];
        }
        for (int layer = 0; layer < 2; ++layer) {
            const auto texture = triangles.front().textures[pass][layer];
            model.setTexture(texture, pass, layer);
            if (std::any_of(triangles.begin(), triangles.end(), [&](const Triangle& triangle) {
                    return triangle.textures[pass][layer] != texture;
                })) {
                auto* table = model.getPolyTexture(pass, layer, 1);
                for (std::size_t i = 0; i < triangles.size(); ++i)
                    table[i] = triangles[i].textures[pass][layer];
            }
        }
        bool has_dcg = false, has_dig = false, has_scg = false;
        std::array<bool, 2> has_uv{};
        for (w8_ulong i = 0; i < unique; ++i) {
            const auto& vertex = *hash->entries[i].vertex;
            has_dcg |= vertex.dcg[pass] != glm::vec3(1) || vertex.weights[pass] != 1;
            has_dig |= vertex.dig[pass] != glm::vec3(0);
            has_scg |= vertex.scg[pass] != glm::vec3(1);
            for (int layer = 0; layer < 2; ++layer)
                has_uv[layer] |= vertex.uv[pass * 2 + layer] != glm::vec2(0);
        }
        auto* dcg = has_dcg ? model.getVertexDCG(pass, 1) : nullptr;
        auto* dig = has_dig ? model.getVertexDIG(pass, 1) : nullptr;
        auto* scg = has_scg ? model.getVertexSCG(pass, 1) : nullptr;
        for (w8_ulong i = 0; i < unique; ++i) {
            const auto& vertex = *hash->entries[i].vertex;
            if (dcg)
                dcg[i] = glm::vec4(vertex.dcg[pass], vertex.weights[pass]);
            if (dig)
                dig[i] = vertex.dig[pass];
            if (scg)
                scg[i] = glm::vec4(vertex.scg[pass], 1);
        }
        for (int side = 0; side < 2; ++side) {
            const auto material = hash->entries[0].vertex->materials[pass][side];
            const auto facing = static_cast<srMeshModel::e_side>(side);
            model.setMaterial(material, pass, facing);
            bool different = false;
            for (w8_ulong i = 1; i < unique; ++i)
                different |= hash->entries[i].vertex->materials[pass][side] != material;
            if (different) {
                auto* table = model.getVertexMaterial(pass, facing, 1);
                for (w8_ulong i = 0; i < unique; ++i)
                    table[i] = hash->entries[i].vertex->materials[pass][side];
            }
        }
        for (int layer = 0; layer < 2; ++layer)
            if (has_uv[layer]) {
                auto* table = model.getVertexTexCoords(pass, layer, 1);
                for (w8_ulong i = 0; i < unique; ++i)
                    table[i] = hash->entries[i].vertex->uv[pass * 2 + layer];
            }
    }
    auto* locations = model.getVertexLoc();
    auto* shades = model.getVertexShadeIndex(1);
    for (w8_ulong i = 0; i < unique; ++i) {
        locations[i] = hash->entries[i].vertex->position;
        shades[i] = hash->entries[i].shade_index;
    }
    auto* polygons = model.getPolyVertex();
    for (w8_ulong i = 0; i < triangle_count; ++i)
        polygons[i] = glm::ivec3(hash->table[i * 3]->index, hash->table[i * 3 + 1]->index,
                                 hash->table[i * 3 + 2]->index);
    model.setDirtyAll();
}

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "compat/compiler.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int3.hpp"
#include "glm/geometric.hpp"
#include "surrender/bounding_radius.h"
#include "surrender/srBoundingBox.h"
#include "surrender/srMaterialIFace.h"
#include "surrender/srModel.h"
#include "surrender/srShader.h"
#include "surrender/srTextureIFace.h"
#include "surrender/srVertexProcessor.h"

/* D:\srsdk1x\sources\corelib\srMeshModel.cpp */

#include <limits>

#include "surrender/mesh_math.h"
#include "surrender/mesh_submission.h"
#include "surrender/srGERD.h"
#include "surrender/srMaterial.h"
#include "surrender/srMath.h"
#include "surrender/srMeshModel.h"
#include "surrender/srTriMeshPipeline.h"
#include "surrender/srTriangleCuller.h"
#include "surrender/srVertexPipe.h"

// A validated permutation carries both directions for attribute-table reorder
// and references that point into those tables. Validate before mutating a mesh.
class MeshPermutation {
public:
    MeshPermutation(std::span<const w8_ulong> order, w8_long count)
        : order(order), inverse(order.size())
    {
        if (order.size() != static_cast<std::size_t>(count))
            throw std::invalid_argument("Mesh permutation length mismatch");
        std::vector<bool> seen(order.size());
        for (std::size_t i = 0; i < order.size(); ++i) {
            if (order[i] >= order.size() || seen[order[i]])
                throw std::invalid_argument("Invalid mesh permutation");
            seen[order[i]] = true;
            inverse[order[i]] = static_cast<w8_ulong>(i);
        }
    }

    template<class T> void apply(T* table) const
    {
        if (!table || order.empty()) return;
        const std::vector<T> original(table, table + order.size());
        for (std::size_t i = 0; i < order.size(); ++i) table[i] = original[order[i]];
    }

    void remapCorners(std::span<glm::ivec3> triangles, bool retain_external) const
    {
        for (auto& triangle : triangles)
            triangle = srMeshMath::remapTriangle(triangle, inverse, retain_external);
    }

    std::span<const w8_ulong> order;
    std::vector<w8_ulong> inverse;
};

/* Bounds-checked pass/side slots; out-of-range indices are ignored. */
// FUNCTION: SURRENDER 0x100403E0
void srMeshModel::setShader(srShader shader, w8_long pass)
{
    if (pass >= 0 && pass < 4) {
        shaders[pass] = shader;
        setDirty(DIRTY_TRI_MESH);
    }
}

// FUNCTION: SURRENDER 0x10040400
srShader srMeshModel::getShader(w8_long pass) const
{
    if (pass >= 0 && pass < 4) {
        return shaders[pass];
    }
    srShader shader;
    shader.value = 0x0100241b;
    return shader;
}

// FUNCTION: SURRENDER 0x10040330
srMaterialIFace* srMeshModel::getMaterial(w8_long pass, e_side side) const
{
    if (pass >= 0 && pass < 4 && (int)side >= 0 && (int)side < 2) {
        return materials[pass][side].get();
    }
    return 0;
}

// FUNCTION: SURRENDER 0x100402E0
void srMeshModel::setMaterial(std::shared_ptr<srMaterialIFace> material, w8_long pass, e_side side)
{
    if (pass >= 0 && pass < 4 && (int)side >= 0 && (int)side < 2) {
        materials[pass][side] = std::move(material);
        setDirty(DIRTY_TRI_MESH);
    }
}

// FUNCTION: SURRENDER 0x100403B0
srTextureIFace* srMeshModel::getTexture(w8_long pass, w8_long layer) const
{
    if (pass >= 0 && pass < 4 && layer >= 0 && layer < 2) {
        return textures[pass][layer].get();
    }
    return 0;
}

// FUNCTION: SURRENDER 0x10040360
void srMeshModel::setTexture(std::shared_ptr<srTextureIFace> texture, w8_long pass, w8_long layer)
{
    if (pass >= 0 && pass < 4 && layer >= 0 && layer < 2) {
        textures[pass][layer] = std::move(texture);
        setDirty(DIRTY_TRI_MESH);
    }
}

// FUNCTION: SURRENDER 0x10042800
w8_long srMeshModel::getActivePolygonCount()
{
    return active_polygon_count;
}

// FUNCTION: SURRENDER 0x10042810
void srMeshModel::setActivePolygonCount(w8_long count)
{
    active_polygon_count = count;
    setDirty(DIRTY_TRI_MESH);
}

// FUNCTION: SURRENDER 0x10042820
void srMeshModel::setUVCount(w8_long count)
{
    if (count < vertex_location_count) {
        count = vertex_location_count;
    }
    if (count != uv_count) {
        uv_count = count;
        for (auto& pass : texcoords) {
            for (auto& table : pass) {
                if (!table.empty()) {
                    table.resize(uv_count, glm::vec2(0.0f, 0.0f));
                }
            }
        }
        setDirty(DIRTY_TRI_MESH);
    }
}

// FUNCTION: SURRENDER 0x10042980
w8_long srMeshModel::getUVCount() const
{
    return uv_count;
}

/* Lazily grown table accessors: a table is supplied only when its governing count is nonzero; on
   first use it is resized and then cleared or seeded. */
// FUNCTION: SURRENDER 0x1003FFD0
glm::vec3* srMeshModel::getVertexLoc()
{
    if (vertex_location_count == 0) {
        return 0;
    }
    if (vertex_locations.empty()) {
        vertex_locations.resize(vertex_location_count, glm::vec3(0.0f, 0.0f, 0.0f));
        setDirty(DIRTY_TRI_MESH);
    }
    return vertex_locations.data();
}

// FUNCTION: SURRENDER 0x100400D0
glm::vec3* srMeshModel::getVertexNormal()
{
    if (vertex_location_count == 0) {
        return 0;
    }
    if (vertex_normals.empty()) {
        vertex_normals.resize(vertex_location_count, glm::vec3(0.0f, 0.0f, 0.0f));
        setDirty(DIRTY_TRI_MESH);
    }
    if ((dirty_flags & (1UL << DIRTY_VERTEX_NORMALS)) != 0) {
        calculateVertexNormals();
    }
    return vertex_normals.data();
}

// FUNCTION: SURRENDER 0x10040160
glm::vec4* srMeshModel::getPolyEq()
{
    if (polygon_count == 0) {
        return 0;
    }
    if (poly_equations.empty()) {
        glm::vec4 zero;
        zero = glm::vec4(0);
        poly_equations.resize(polygon_count, zero);
        setDirty(DIRTY_TRI_MESH);
    }
    if ((dirty_flags & (1UL << DIRTY_POLYGON_NORMALS)) != 0) {
        calculatePolygonNormals();
    }
    return poly_equations.data();
}

// FUNCTION: SURRENDER 0x10040250
w8_ulong* srMeshModel::getActivePolygonTable(int table)
{
    if (active_polygons.empty() && table != 0 && polygon_count != 0) {
        active_polygons.resize(polygon_count);
        for (w8_long index = 0; index < polygon_count; ++index) {
            active_polygons[index] = index;
        }
        setDirty(DIRTY_TRI_MESH);
    }
    return active_polygons.empty() ? nullptr : active_polygons.data();
}

// FUNCTION: SURRENDER 0x1003FED0
glm::ivec3* srMeshModel::getPolyVertex()
{
    if (polygon_count == 0) {
        return 0;
    }
    if (poly_vertices.empty()) {
        poly_vertices.resize(polygon_count, glm::ivec3(0));
        setDirty(DIRTY_TRI_MESH);
    }
    return poly_vertices.data();
}

template<class T>
static T* GetLazyMeshTable(srMeshModel& mesh, std::vector<T>& slot, w8_long count, int create,
                           const T& initial = T{})
{
    if (slot.empty() && create != 0 && count != 0) {
        slot.resize(count, initial);
        mesh.setDirty(srMeshModel::DIRTY_TRI_MESH);
    }
    return slot.empty() ? nullptr : slot.data();
}

// FUNCTION: SURRENDER 0x10040430
std::shared_ptr<srMaterialIFace>* srMeshModel::getVertexMaterial(w8_long vertex, e_side side,
                                                                 int table)
{
    if (vertex < 0 || vertex > 3 || (int)side < 0 || (int)side > 1) {
        return 0;
    }
    return GetLazyMeshTable(*this, vertex_materials[vertex][side], vertex_location_count, table);
}

// FUNCTION: SURRENDER 0x10040560
std::shared_ptr<srTextureIFace>* srMeshModel::getPolyTexture(w8_long polygon, w8_long layer,
                                                             int table)
{
    if (polygon < 0 || polygon > 3 || layer < 0 || layer > 1) {
        return 0;
    }
    return GetLazyMeshTable(*this, poly_textures[polygon][layer], polygon_count, table);
}

// FUNCTION: SURRENDER 0x10040690
srShader* srMeshModel::getPolyShader(w8_long polygon, int layer)
{
    if (polygon < 0 || polygon > 3) {
        return 0;
    }
    return GetLazyMeshTable(*this, poly_shaders[polygon], polygon_count, layer);
}

// FUNCTION: SURRENDER 0x10040720
glm::ivec3* srMeshModel::getPolyUVIndex(w8_long layer, int table)
{
    if (layer < 0 || layer > 3) {
        return 0;
    }
    auto& slot = poly_uv_indices[layer];
    if (slot.empty() && table != 0 && polygon_count != 0) {
        getPolyVertex();
        slot = poly_vertices;
        setDirty(DIRTY_TRI_MESH);
    }
    return slot.empty() ? nullptr : slot.data();
}

// FUNCTION: SURRENDER 0x100407E0
glm::vec3* srMeshModel::getVertexDIG(w8_long vertex, int table)
{
    if (vertex < 0 || vertex > 3) {
        return 0;
    }
    return GetLazyMeshTable(*this, dig[vertex], vertex_location_count, table,
                            glm::vec3(0.0f, 0.0f, 0.0f));
}

// FUNCTION: SURRENDER 0x10040A70
glm::vec2* srMeshModel::getVertexTexCoords(w8_long vertex, w8_long layer, int table)
{
    if (vertex < 0 || vertex > 3 || layer < 0 || layer > 1) {
        return 0;
    }
    return GetLazyMeshTable(*this, texcoords[vertex][layer], uv_count, table, glm::vec2(0.0f));
}

// FUNCTION: SURRENDER 0x10040B80
w8_ulong* srMeshModel::getVertexShadeIndex(int table)
{
    if (vertex_shade_indices.empty() && table != 0 && vertex_location_count != 0) {
        vertex_shade_indices.resize(vertex_location_count, 0);
        setDirty(DIRTY_VERTEX_NORMALS);
    }
    return vertex_shade_indices.empty() ? nullptr : vertex_shade_indices.data();
}

// FUNCTION: SURRENDER 0x10041B60
void srMeshModel::setDirtyBounds()
{
    setDirty(DIRTY_BOUNDS);
}

// FUNCTION: SURRENDER 0x10041B90
void srMeshModel::setDirtyNormals()
{
    setDirty(DIRTY_POLYGON_NORMALS);
    setDirty(DIRTY_VERTEX_NORMALS);
}

// FUNCTION: SURRENDER 0x1003CF30
srMeshModel::srMeshModel(w8_long polygons, w8_long vertices)
{
    reset(polygons, vertices);
}

// FUNCTION: SURRENDER 0x1003D2C0
void srMeshModel::reset(w8_long polygons, w8_long vertices)
{
    /* Reset really discards table storage and its references; pass-wide material/texture/shader
       state and sort bias survive. Default destruction needs no model reset or client callbacks.
       This includes the former freeAll path at SURRENDER 0x1003DEA0. */
    tri_mesh = TriMesh{};
    active_polygons = std::vector<w8_ulong>{};
    poly_vertices = std::vector<glm::ivec3>{};
    poly_equations = std::vector<glm::vec4>{};
    vertex_locations = std::vector<glm::vec3>{};
    vertex_normals = std::vector<glm::vec3>{};
    vertex_shade_indices = std::vector<w8_ulong>{};
    vertex_materials = {};
    poly_textures = {};
    texcoords = {};
    poly_uv_indices = {};
    poly_shaders = {};
    dig = {};
    dcg = {};
    scg = {};
    bounds_minimum = glm::vec3(0);
    bounds_maximum = glm::vec3(0);
    bounds_center = glm::vec3(0);
    bounds_radius = 0.0f;
    vertex_location_count = vertices;
    uv_count = vertices;
    polygon_count = polygons;
    active_polygon_count = polygons;
    pass_count = 1;
    render_control = 0;
    render_control |= 1UL << CONTROL_FRONT;
    render_control |= 1UL << CONTROL_SKIP_AUTO_BOX;
    setDirtyAll();
}

// FUNCTION: SURRENDER 0x1003E120
void srMeshModel::calculateBounds()
{
    bounds_minimum = glm::vec3(0);
    bounds_maximum = glm::vec3(0);
    bounds_center = glm::vec3(0);
    bounds_radius = 0.0f;
    if (vertex_location_count != 0) {
        const std::span<const glm::vec3> vertices(getVertexLoc(), vertex_location_count);
        const auto box = boundingBox(vertices);
        bounds_minimum = box.minimum;
        bounds_maximum = box.maximum;
        const auto center = box.center();
        bounds_center = center;
        bounds_radius = boundingRadius(vertices, center) * 1.00001f;
        dirty_flags &= ~(1UL << DIRTY_BOUNDS);
    }
}

// FUNCTION: SURRENDER 0x1003E280
void srMeshModel::calculatePolygonNormals()
{
    dirty_flags &= ~(1UL << DIRTY_POLYGON_NORMALS);
    glm::vec4* equations = getPolyEq();
    glm::ivec3* polygons = getPolyVertex();
    glm::vec3* vertices = getVertexLoc();
    for (w8_long polygon = 0; polygon < polygon_count; polygon++) {
        const auto first = vertices[polygons[polygon].x];
        const auto normal = srMeshMath::polygonNormal(
            std::span<const glm::vec3>(vertices, vertex_location_count), polygons[polygon]);
        equations[polygon] = glm::vec4(normal, -glm::dot(normal, first));
    }
}

// FUNCTION: SURRENDER 0x1003E390
void srMeshModel::calculateVertexNormals()
{
    if (polygon_count == 0 || vertex_location_count == 0) return;
    if (dirty_flags & (1UL << DIRTY_POLYGON_NORMALS)) calculatePolygonNormals();
    dirty_flags &= ~(1UL << DIRTY_VERTEX_NORMALS);
    const auto* equations = getPolyEq();
    const auto* polygons = getPolyVertex();
    const auto* shade_indices = getVertexShadeIndex(0);
    const auto smooth = srMeshMath::vertexNormalSums(
        std::span<const glm::ivec3>(polygons, polygon_count),
        std::span<const glm::vec4>(equations, polygon_count), vertex_location_count,
        shade_indices ? std::span<const w8_ulong>(shade_indices, vertex_location_count)
                      : std::span<const w8_ulong>{});
    auto* normals = getVertexNormal();
    for (std::size_t vertex = 0; vertex < static_cast<std::size_t>(vertex_location_count);
         ++vertex) {
        normals[vertex] = normalizedOrZero(smooth[vertex]);
    }
}

// FUNCTION: SURRENDER 0x1003E690
void srMeshModel::scale(const glm::vec3& scale)
{
    glm::vec3* vertices = getVertexLoc();
    for (w8_long index = 0; index < vertex_location_count; index++) {
        vertices[index] *= scale;
    }
    setDirtyAll();
}

// FUNCTION: SURRENDER 0x1003E780
void srMeshModel::applyMatrix(const glm::mat3& matrix)
{
    auto* vertices = getVertexLoc();
    for (w8_long i = 0; i < vertex_location_count; ++i) vertices[i] = matrix * vertices[i];
    setDirtyAll();
}

// FUNCTION: SURRENDER 0x1003E8C0
void srMeshModel::relocateVertices(const glm::vec3& offset)
{
    glm::vec3* vertices = getVertexLoc();
    for (w8_long index = 0; index < vertex_location_count; index++) {
        vertices[index] += offset;
    }
    setDirtyAll();
}

// FUNCTION: SURRENDER 0x1003E9B0
void srMeshModel::centerVertices()
{
    if (vertex_location_count == 0) return;
    glm::dvec3 sum(0);
    for (const auto& vertex : std::span(getVertexLoc(), vertex_location_count))
        sum += glm::dvec3(vertex);
    relocateVertices(glm::vec3(-sum / double(vertex_location_count)));
}

// FUNCTION: SURRENDER 0x1003EA90
double srMeshModel::getAverageRadius()
{
    if (vertex_location_count == 0) {
        return 0.0;
    }
    double radius = 0;
    for (const auto& vertex : std::span(getVertexLoc(), vertex_location_count))
        radius += glm::length(vertex);
    return radius / vertex_location_count;
}

// FUNCTION: SURRENDER 0x1003EB20
double srMeshModel::getMaxRadius()
{
    return boundingRadius(std::span(getVertexLoc(), vertex_location_count), glm::vec3(0));
}

// FUNCTION: SURRENDER 0x1003EBB0
void srMeshModel::scaleToAverageRadius(double radius)
{
    const double current_radius = getAverageRadius();
    if (current_radius > 0) scale(glm::vec3(static_cast<float>(radius / current_radius)));
}

// FUNCTION: SURRENDER 0x1003EBF0
void srMeshModel::scaleToMaxRadius(double radius)
{
    const double current_radius = getMaxRadius();
    if (current_radius > 0) scale(glm::vec3(static_cast<float>(radius / current_radius)));
}

// FUNCTION: SURRENDER 0x1003EC30
void srMeshModel::flipFaces()
{
    if (polygon_count != 0) {
        glm::ivec3* polygons = getPolyVertex();
        for (w8_long polygon = 0; polygon < polygon_count; polygon++) {
            w8_long first = polygons[polygon].x;
            polygons[polygon].x = polygons[polygon].y;
            polygons[polygon].y = first;
        }
        setDirtyAll();
    }
}

// FUNCTION: SURRENDER 0x1003ED20
w8_long srMeshModel::findClosestVertex(const glm::vec3& position)
{
    const auto target = position;
    float minimum = std::numeric_limits<float>::infinity();
    w8_long closest = 0;
    const auto* vertices = getVertexLoc();
    for (w8_long i = 0; i < vertex_location_count; ++i) {
        const auto offset = vertices[i] - target;
        const float distance = glm::dot(offset, offset);
        if (distance < minimum) {
            minimum = distance;
            closest = i;
        }
    }
    return closest;
}

// FUNCTION: SURRENDER 0x1003DC70
srMeshModel::TriMesh srMeshModel::getTriMesh()
{
    if ((dirty_flags & (1UL << DIRTY_TRI_MESH)) != 0) {
        updateTriMesh();
    }
    return tri_mesh;
}

// FUNCTION: SURRENDER 0x1003DC90
void srMeshModel::updateTriMesh()
{
    tri_mesh = TriMesh{};
    tri_mesh.vertex_count = vertex_location_count;
    tri_mesh.polygon_count = polygon_count;
    tri_mesh.pass_count = pass_count;
    tri_mesh.control_flags = render_control;
    tri_mesh.sort_bias = sort_bias;
    tri_mesh.poly_vertices = getPolyVertex();
    tri_mesh.poly_equations = getPolyEq();
    tri_mesh.positions = getVertexLoc();
    tri_mesh.normals = getVertexNormal();
    for (w8_long pass = 0; pass < 4; pass++) {
        for (w8_long side = 0; side < 2; side++) {
            tri_mesh.materials[pass][side] =
                std::static_pointer_cast<srMaterial>(materials[pass][side]);
            tri_mesh.vertex_materials[pass][side] =
                getVertexMaterial(pass, static_cast<e_side>(side), 0);
        }
        for (w8_long layer = 0; layer < 2; layer++) {
            tri_mesh.textures[pass][layer] = textures[pass][layer];
            tri_mesh.poly_textures[pass][layer] = getPolyTexture(pass, layer, 0);
            tri_mesh.texcoords[pass][layer] = getVertexTexCoords(pass, layer, 0);
        }
        tri_mesh.shaders[pass] = shaders[pass];
        tri_mesh.poly_uv[pass] = getPolyUVIndex(pass, 0);
        tri_mesh.poly_shaders[pass] = getPolyShader(pass, 0);
        tri_mesh.dig[pass] = getVertexDIG(pass, 0);
        tri_mesh.dcg[pass] = getVertexDCG(pass, 0);
        tri_mesh.scg[pass] = getVertexSCG(pass, 0);
    }
    constexpr auto manual_bounds =
        (1UL << CONTROL_SKIP_AUTO_BOX) | (1UL << CONTROL_SKIP_AUTO_SPHERE);
    if ((tri_mesh.control_flags & manual_bounds) != manual_bounds) {
        const auto bounds = getBounds();
        if ((tri_mesh.control_flags & (1UL << CONTROL_SKIP_AUTO_BOX)) == 0) {
            tri_mesh.bounds_minimum = bounds.box.minimum;
            tri_mesh.bounds_maximum = bounds.box.maximum;
        }
        if ((tri_mesh.control_flags & (1UL << CONTROL_SKIP_AUTO_SPHERE)) == 0) {
            tri_mesh.bounds_center = bounds.sphere.center;
            tri_mesh.bounds_radius = bounds.sphere.radius;
        }
    }
    tri_mesh.active_polygons.reset();
    if (const auto* active = getActivePolygonTable(0)) {
        tri_mesh.active_polygons = std::span<const w8_ulong>(active, active_polygon_count);
    }
    dirty_flags &= ~(1UL << DIRTY_TRI_MESH);
}

// FUNCTION: SURRENDER 0x1003FDA0
void srMeshModel::setBounds(const glm::vec3& minimum, const glm::vec3& maximum,
                            const glm::vec3& center, float radius)
{
    bounds_minimum = minimum;
    bounds_maximum = maximum;
    bounds_center = center;
    bounds_radius = radius;
    clearDirty(DIRTY_BOUNDS);
    setDirty(DIRTY_TRI_MESH);
}

srModel::Bounds srMeshModel::getBounds()
{
    if ((dirty_flags & (1UL << DIRTY_BOUNDS)) != 0) {
        calculateBounds();
    }
    return {{bounds_minimum, bounds_maximum}, {bounds_center, bounds_radius}};
}

static glm::vec4*
GetVertexColorTable(srMeshModel& mesh,
                    std::array<std::vector<glm::vec4>, srMeshModel::MAX_PASSES>& colors,
                    w8_long pass, int create)
{
    if (pass < 0 || pass >= srMeshModel::MAX_PASSES) {
        return nullptr;
    }
    return GetLazyMeshTable(mesh, colors[pass], mesh.vertex_location_count, create, glm::vec4(1));
}

// FUNCTION: SURRENDER 0x100408B0
glm::vec4* srMeshModel::getVertexSCG(w8_long vertex, int table)
{
    return GetVertexColorTable(*this, scg, vertex, table);
}

// FUNCTION: SURRENDER 0x10040990
glm::vec4* srMeshModel::getVertexDCG(w8_long vertex, int table)
{
    return GetVertexColorTable(*this, dcg, vertex, table);
}

bool srMeshModel::isVisible(srGERD& renderer)
{
    const bool test_sphere = (render_control & (1UL << CONTROL_SKIP_AUTO_SPHERE)) == 0;
    const bool test_box =
        (render_control & (1UL << CONTROL_SKIP_AUTO_BOX)) == 0 && vertex_location_count >= 8;
    if (!test_sphere && !test_box) return true;
    const auto bounds = getBounds();
    if (test_sphere && renderer.testBoundingSphere(bounds.sphere.center, bounds.sphere.radius) ==
                           srGERD::VISIBILITY_OUTSIDE)
        return false;
    if (test_box && renderer.testBoundingBox(bounds.box.minimum, bounds.box.maximum) ==
                        srGERD::VISIBILITY_OUTSIDE)
        return false;
    return true;
}

// FUNCTION: SURRENDER 0x1003CEA0
void srMeshModel::render(srGERD& renderer)
{
    if (!isVisible(renderer)) return;
    renderTriMesh(renderer, getTriMesh());
}

// FUNCTION: SURRENDER 0x100418C0
void srMeshModel::reindexPolygons(std::span<const w8_ulong> indices)
{
    const MeshPermutation permutation(indices, polygon_count);
    if (!polygon_count) return;
    permutation.apply(getPolyEq());
    permutation.apply(getPolyVertex());
    for (w8_long pass = 0; pass < pass_count; ++pass) {
        permutation.apply(getPolyUVIndex(pass, 0));
        permutation.apply(getPolyShader(pass, 0));
        for (w8_long layer = 0; layer < 2; ++layer)
            permutation.apply(getPolyTexture(pass, layer, 0));
    }
    if (active_polygon_count && getActivePolygonTable(0)) {
        auto* table = getActivePolygonTable(1);
        for (w8_long i = 0; i < active_polygon_count; ++i) table[i] = permutation.inverse[table[i]];
    }
    setDirtyAll();
}

// FUNCTION: SURRENDER 0x10042230
void srMeshModel::reindexVertices(std::span<const w8_ulong> indices)
{
    const MeshPermutation permutation(indices, vertex_location_count);
    if (!vertex_location_count) return;
    permutation.apply(getVertexNormal());
    permutation.apply(getVertexLoc());
    if (auto* shade = getVertexShadeIndex(0)) {
        permutation.apply(shade);
        for (w8_long i = 0; i < vertex_location_count; ++i)
            shade[i] = permutation.inverse[shade[i]];
        for (w8_long i = 0; i < vertex_location_count; ++i) {
            const auto value = shade[i];
            if (value < static_cast<w8_ulong>(i)) {
                shade[i] = shade[value];
                shade[value] = value;
            }
        }
    }
    for (w8_long pass = 0; pass < pass_count; ++pass) {
        for (w8_long side = 0; side < 2; ++side)
            permutation.apply(getVertexMaterial(pass, static_cast<e_side>(side), 0));
        for (w8_long table = 0; table < 2; ++table)
            permutation.apply(getVertexTexCoords(pass, table, 0));
        permutation.apply(getVertexDIG(pass, 0));
        permutation.apply(getVertexSCG(pass, 0));
        permutation.apply(getVertexDCG(pass, 0));
        if (getPolyUVIndex(pass, 0))
            permutation.remapCorners(
                {getPolyUVIndex(pass, 1), static_cast<std::size_t>(polygon_count)}, true);
    }
    permutation.remapCorners({getPolyVertex(), static_cast<std::size_t>(polygon_count)}, false);
    setDirtyAll();
}
// GLOBAL: SURRENDER 0x100A4790
srTriMeshPipeline::srTriMeshPipeline()
{
    Reset(nullptr);
}

// FUNCTION: SURRENDER 0x10044070
void srTriMeshPipeline::SetFlags(srShader shader)
{
    this->shader = shader;
    current_pass->shader = shader;
}

/* Bind a renderer and rebuild the current slot through PrepareSlot. */
// FUNCTION: SURRENDER 0x100441A0
void srTriMeshPipeline::Reset(srGERD* renderer)
{
    slot_count = 0;
    this->renderer = renderer;
    flags = 0;
    flags |= FRUSTUM_CLIPPING;
    flags |= LIMIT_VERTEX_BATCHES;
    triangle_count = 0;
    active_triangles.reset();
    projected_vertices = 0;
    triangles = 0;
    vertex_count = 0;
    positions = 0;
    vertex_extras = 0;
    bounds_source = srTriMeshPipeline::BOUNDS_FROM_VERTICES;
    bounds_minimum = glm::vec3(0);
    bounds_maximum = glm::vec3(0);
    bounds_center = glm::vec3(0);
    bounds_radius = 0.0f;
    unknown_70 = 0;
    sort_bias = 0.0f;
    shader.value = 0x0100241b;
    texture0 = 0;
    texture1 = 0;
    material = nullptr;

    PrepareSlot();
}

// FUNCTION: SURRENDER 0x100442B0
void srTriMeshPipeline::Flush()
{
    if (slot_count > 0) {
        FlushSlots();
    }
}

/* Growth relocates slot objects but not the mesh tables they borrow. Rebind the writable slot
   after each possible allocation, and initialize only this slot, never previously submitted ones.
   Renderer::render consumes those earlier borrows synchronously during FlushSlots. */
// FUNCTION: SURRENDER 0x100442E0
void srTriMeshPipeline::PrepareSlot()
{
    if (records.size() <= slot_count) {
        records.resize(slot_count + 1);
    }
    current_record = &records[slot_count];
    if (passes.size() <= slot_count) {
        passes.resize(slot_count + 1);
    }
    current_pass = &passes[slot_count];

    *current_record = Record{};
    *current_pass = Pass{};
    current_record->material = material;
    current_pass->textures[0] = texture0;
    current_pass->textures[1] = texture1;
    current_pass->shader = shader;
}

// FUNCTION: SURRENDER 0x100443A0
void srTriMeshPipeline::FlushSlots()
{
    if (triangle_count == 0 || (active_triangles && active_triangles->empty())) {
        return;
    }

    if (bounds_source != srTriMeshPipeline::BOUNDS_SPHERE) {
        if (bounds_source == srTriMeshPipeline::BOUNDS_FROM_VERTICES && vertex_count != 0) {
            const auto box = boundingBox(std::span<const glm::vec3>(positions, vertex_count));
            bounds_minimum = box.minimum;
            bounds_maximum = box.maximum;
        }

        bounds_center = (bounds_minimum + bounds_maximum) * 0.5f;

        bounds_radius = glm::distance(bounds_center, bounds_minimum) * 1.001f;
    }

    const auto eye_bounds = renderer->eyeSpaceBounds({bounds_center, bounds_radius});
    const auto model_view = this->renderer->getMatrix(srGERD::MATRIX_MODELVIEW);
    const auto inverse_model_view = this->renderer->getInverseModelViewMatrix();
    const auto clip_planes = this->renderer->getClipPlanes();
    const auto project_clip_near = this->renderer->getProjectClipNearMatrix();
    const auto normal_matrix = this->renderer->getNormalMatrix();

    srGERD::e_cullMode cull_mode = this->renderer->getCullMode();
    srGERD::e_winding winding = this->renderer->getWinding();

    srTriangleCuller::Input culler_input(model_view, inverse_model_view);
    if (cull_mode == srGERD::CULL_NONE) {
        culler_input.cull_mode = 2;
    } else if (cull_mode == srGERD::CULL_FRONT) {
        culler_input.cull_mode = winding == srGERD::WINDING_POSITIONAL_0;
    } else {
        culler_input.cull_mode = winding != srGERD::WINDING_POSITIONAL_0;
    }

    culler_input.vertices = {positions, vertex_count};
    culler_input.clip_planes = clip_planes.planes;

    if ((this->flags & FRUSTUM_CLIPPING) == 0) {
        culler_input.clip_mask = 0;
    } else {
        const auto clip = srTriangleCuller::getClipMask(eye_bounds.center, eye_bounds.radius,
                                                        clip_planes.planes, clip_planes.mask);
        const bool retain_clip_mask =
            clip.mask != 0 && ((clip.mask & 0xffffffc0UL) != 0 ||
                               (slot_count * triangle_count > 45 && clip.depth > 0.23f));
        culler_input.clip_mask = retain_clip_mask ? clip.mask : 0;
    }

    const auto total =
        active_triangles ? static_cast<w8_ulong>(active_triangles->size()) : triangle_count;
    w8_ulong batch_limit = total;
    if ((this->flags & LIMIT_VERTEX_BATCHES) != 0) {
        double ratio = static_cast<double>(vertex_count) / triangle_count;
        if (ratio > 3.0f) {
            ratio = 3.0f;
        }
        batch_limit = static_cast<w8_ulong>(1300.0f / (slot_count * ratio));
        if (cull_mode == srGERD::CULL_NONE) {
            batch_limit >>= 1;
        }
        if (batch_limit > total) {
            batch_limit = total;
        }
    }

    w8_ulong processed = 0;
    while (processed < total) {
        w8_ulong batch_count = total - processed;
        if (batch_count > batch_limit) {
            batch_count = batch_limit;
        }

        if (!active_triangles) {
            culler_input.active_triangles.reset();
            culler_input.polygon_planes =
                projected_vertices
                    ? std::span<const glm::vec4>(projected_vertices + processed, batch_count)
                    : std::span<const glm::vec4>{};
            culler_input.triangles = {triangles + processed, batch_count};
        } else {
            culler_input.active_triangles = active_triangles->subspan(processed, batch_count);
            culler_input.polygon_planes =
                projected_vertices ? std::span<const glm::vec4>(projected_vertices, triangle_count)
                                   : std::span<const glm::vec4>{};
            culler_input.triangles = {triangles, triangle_count};
        }

        auto culler_output = srTriangleCuller::cull(culler_input);
        if (!culler_output.indices.empty()) {

            auto* renderer = &this->renderer->batchRenderer();

            {
                this->vertex_arrays.resize(slot_count);
                srVertexArray* vertex_arrays = this->vertex_arrays.data();
                vertex_arrays[0] =
                    renderer->allocVertexArray(slot_count * culler_output.vertexCount());
                for (w8_ulong slot = 1; slot < slot_count; ++slot) {
                    const auto offset = renderer->first_vertex + slot * culler_output.vertexCount();
                    vertex_arrays[slot] = renderer->vertices.bind(offset);
                }

                w8_ulong processor_count = this->renderer->getVertexProcessorCount();
                srVertexProcessor** processors = 0;
                if (processor_count != 0) {
                    vertex_processors = this->renderer->getVertexProcessors();
                    processors = vertex_processors.data();
                }

                const auto ambient_light = this->renderer->getAmbientLight();
                const auto [environment_minimum, environment_maximum] =
                    this->renderer->getEnvironmentRange();
                const auto [environment_scale, environment_inverse_scale] =
                    this->renderer->getEnvironmentScaleFactor();
                w8_ulong exclusion_mask = this->renderer->getExclusionMask();

                srVertexPipe::Input pipe_input(model_view, normal_matrix);
                pipe_input.active_vertices = culler_output.avt;
                pipe_input.direct_vertex_indices = false;
                pipe_input.positions = {positions, vertex_count};
                if (vertex_extras) pipe_input.normals = {vertex_extras, vertex_count};
                pipe_input.eye_bounds = eye_bounds;
                pipe_input.vertex_arrays = std::span{this->vertex_arrays}.first(slot_count);
                pipe_input.exclusion_mask = exclusion_mask;
                pipe_input.ambient_light = ambient_light;
                pipe_input.records = std::span{records}.first(slot_count);
                pipe_input.processors = {processors, processor_count};
                pipe_input.environment_minimum = environment_minimum;
                pipe_input.environment_maximum = environment_maximum;
                pipe_input.environment_scale = environment_scale;
                pipe_input.environment_inverse_scale = environment_inverse_scale;

                if (!active_triangles && processed != 0) {
                    for (auto& index : culler_output.indices) index += processed;
                }

                w8_ulong renderer_disable_mask = 0;
                if (this->renderer->getMaxTextureStages() == 1) {
                    renderer_disable_mask = (1UL << srVertexProcessor::CHANNEL_ST1) |
                                            (1UL << srVertexProcessor::CHANNEL_Q1);
                }

                for (w8_ulong pass_index = 0; pass_index < slot_count; ++pass_index) {
                    passes[pass_index].texcoords = records[pass_index].st_source[0];

                    w8_ulong disable_mask;
                    if (passes[pass_index].shaders != 0) {
                        disable_mask = srVertexPipe::getShaderDisableMask(
                            passes[pass_index].shaders, culler_output.indices.data(),
                            culler_output.triangleCount());
                    } else {
                        disable_mask =
                            srVertexPipe::getShaderDisableMask(passes[pass_index].shader);
                    }
                    records[pass_index].channels = disable_mask | renderer_disable_mask;
                }

                vertex_pipe.process(pipe_input);

                srGERD::Renderer::TriInput render_input;
                render_input.record_count = slot_count;
                render_input.vertex_count = culler_output.vertexCount();
                render_input.indices = culler_output.indices;
                render_input.triangles = {triangles, triangle_count};
                render_input.vertices = culler_output.vertex_remap;
                render_input.passes = &passes[0];
                render_input.direct_vertex_indices = false;
                render_input.project_clip_near = &project_clip_near;
                render_input.sort_bias = sort_bias;
                renderer->render(render_input);
            }
            if (renderer->isBatchFull()) renderer->submit();
        }

        processed += batch_limit;
    }
}

srTriMeshPipeline& srGERD::meshPipeline()
{
    mesh_pipeline.Reset(this);
    return mesh_pipeline;
}

srTriMeshPipeline& srTriMeshPipeline::beginMesh(srGERD& renderer, const srMeshModel::TriMesh& mesh)
{
    auto& pipeline = renderer.meshPipeline();
    pipeline.records.resize(static_cast<std::size_t>(mesh.pass_count) + 1);
    pipeline.passes.resize(static_cast<std::size_t>(mesh.pass_count) + 1);
    pipeline.PrepareSlot();
    pipeline.sort_bias = mesh.sort_bias;
    pipeline.triangles = mesh.poly_vertices;
    pipeline.triangle_count = static_cast<w8_ulong>(mesh.polygon_count);
    pipeline.positions = mesh.positions;
    pipeline.vertex_count = static_cast<w8_ulong>(mesh.vertex_count);
    pipeline.vertex_extras = mesh.normals;
    pipeline.projected_vertices = mesh.poly_equations;
    pipeline.active_triangles = mesh.active_polygons;
    pipeline.SetMeshBounds(mesh);
    return pipeline;
}

void srTriMeshPipeline::SetMeshBounds(const srMeshModel::TriMesh& mesh)
{
    if ((mesh.control_flags & (1UL << srMeshModel::CONTROL_SKIP_AUTO_BOX)) == 0) {
        bounds_minimum = mesh.bounds_minimum;
        bounds_maximum = mesh.bounds_maximum;
        if (bounds_source == srTriMeshPipeline::BOUNDS_FROM_VERTICES) {
            bounds_source = srTriMeshPipeline::BOUNDS_BOX;
        }
    }
    if ((mesh.control_flags & (1UL << srMeshModel::CONTROL_SKIP_AUTO_SPHERE)) == 0) {
        bounds_center = mesh.bounds_center;
        bounds_radius = mesh.bounds_radius;
        bounds_source = srTriMeshPipeline::BOUNDS_SPHERE;
    }
}

void srTriMeshPipeline::AppendMeshPass(const srMeshModel::TriMesh& mesh, w8_long pass,
                                       w8_long material_side, srShader pass_shader)
{
    current_record->flags = 0;
    current_pass->shaders = 0;
    current_pass->texture_tables[0] = 0;
    current_pass->texture_tables[1] = 0;

    if (mesh.dig[pass] != 0) {
        current_record->color_source.colors = mesh.dig[pass];
        current_record->color_source.format = srVertexPipe::Record::ColorSource::FORMAT_VECTOR3;
        current_record->flags |= srVertexPipe::Record::HAS_COLORS;
    }
    if (mesh.dcg[pass] != 0) {
        current_record->spec_for_diffuse = mesh.dcg[pass];
        current_record->flags |= srVertexPipe::Record::HAS_DIFFUSE_MULTIPLIERS;
    }
    if (mesh.scg[pass] != 0) {
        current_record->spec_for_specular = mesh.scg[pass];
        current_record->flags |= srVertexPipe::Record::HAS_SPECULAR_MULTIPLIERS;
    }

    if (mesh.vertex_materials[pass][material_side] == 0) {
        material = mesh.materials[pass][material_side].get();
        current_record->material = material;
    } else {
        current_record->materials = mesh.vertex_materials[pass][material_side];
        current_record->flags |= srVertexPipe::Record::HAS_VERTEX_MATERIALS;
    }

    if (mesh.poly_uv[pass] != 0) {
        current_pass->poly_uv = mesh.poly_uv[pass];
    }

    if (mesh.poly_shaders[pass] == 0) {
        SetFlags(pass_shader);
    } else {
        current_pass->shaders = mesh.poly_shaders[pass];
    }

    if (mesh.texcoords[pass][0] != 0) {
        current_record->st_source[0] = mesh.texcoords[pass][0];
        current_record->flags |= srVertexPipe::Record::HAS_TEXCOORD0;
    }
    if (mesh.texcoords[pass][1] != 0) {
        current_record->flags |= srVertexPipe::Record::HAS_TEXCOORD1;
        current_record->st_source[1] = mesh.texcoords[pass][1];
    }

    for (w8_long layer = 0; layer < 2; ++layer) {
        if (mesh.poly_textures[pass][layer] == 0) {
            srTextureIFace* texture = mesh.textures[pass][layer].get();
            (layer == 0 ? texture0 : texture1) = texture;
            current_pass->textures[layer] = texture;
        } else {
            current_pass->texture_tables[layer] = mesh.poly_textures[pass][layer];
        }
    }

    ++slot_count;
    PrepareSlot();
}

// FUNCTION: SURRENDER 0x1003CA80
void srMeshModel::renderTriMesh(srGERD& renderer, const TriMesh& mesh)
{
    if (mesh.polygon_count != 0 && mesh.vertex_count != 0) {
        const srMeshSubmission::EnableScope enabled(
            renderer, (mesh.control_flags & (1UL << CONTROL_SORTED_RENDERING)) != 0);
        srMeshSubmission::drawSides(
            renderer, mesh,
            [&](srTriMeshPipeline&, w8_long side) {
                w8_long material_side = side;
                if (side == 1) {
                    renderer.setCullMode(srGERD::CULL_FRONT);
                    if (!renderer.isEnabled(srGERD::ENABLE_REVERSE_NORMALS))
                        renderer.toggle(srGERD::ENABLE_REVERSE_NORMALS);
                    if ((mesh.control_flags & (1UL << CONTROL_REUSE_FRONT_MATERIAL)) != 0)
                        material_side = 0;
                } else {
                    renderer.setCullMode((mesh.control_flags & (1UL << CONTROL_NO_FRONT_CULL)) != 0
                                             ? srGERD::CULL_NONE
                                             : srGERD::CULL_BACK);
                    if (renderer.isEnabled(srGERD::ENABLE_REVERSE_NORMALS))
                        renderer.toggle(srGERD::ENABLE_REVERSE_NORMALS);
                }
                return material_side;
            },
            [&](w8_long pass) { return mesh.shaders[pass]; });
    }
}

// FUNCTION: SURRENDER 0x100417D0
float srMeshModel::getSortBias() const
{
    return sort_bias;
}

// FUNCTION: SURRENDER 0x100417E0
void srMeshModel::disable(e_control control)
{
    render_control &= ~(1u << control);
    setDirty(DIRTY_TRI_MESH);
}

// FUNCTION: SURRENDER 0x10041870
int srMeshModel::isEnabled(e_control control) const
{
    return (render_control & (1 << control)) != 0;
}

// FUNCTION: SURRENDER 0x10041890
void srMeshModel::setPassCount(w8_long count)
{
    pass_count = std::clamp<w8_long>(count, 1, MAX_PASSES);
    setDirty(DIRTY_TRI_MESH);
}

// FUNCTION: SURRENDER 0x10041AC0
w8_long srMeshModel::getPassCount() const
{
    return pass_count;
}

// FUNCTION: SURRENDER 0x10041AD0
w8_long srMeshModel::getPolygonCount() const
{
    return polygon_count;
}

// FUNCTION: SURRENDER 0x10041AE0
w8_long srMeshModel::getVertexCount() const
{
    return vertex_location_count;
}

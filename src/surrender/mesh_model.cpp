#include <algorithm>
#include <cmath>
#include <vector>
#include <array>
#include <cstdio>
#include <memory>
#include <span>
#include <utility>

#include "glm/common.hpp"
#include "glm/geometric.hpp"
#include "surrender/srBoundingBox.h"
#include "compat/compiler.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "surrender/srFlags.h"
#include "surrender/srMaterialIFace.h"
#include "surrender/srModel.h"
#include "surrender/srShader.h"
#include "surrender/srStatisticsManager.h"
#include "surrender/srTextureIFace.h"
#include "surrender/srTypeRegistry.h"
#include "surrender/srVertexProcessor.h"

/* D:\srsdk1x\sources\corelib\srMeshModel.cpp */

#include <ostream>
#include <limits>

#include "surrender/srMeshModel.h"
#include "surrender/srStreamFlags.h"
#include "surrender/srCore.h"
#include "surrender/srDebug.h"
#include "surrender/srGERD.h"
#include "surrender/srMaterial.h"
#include "surrender/srTriMeshPipeline.h"
#include "surrender/srTriangleCuller.h"
#include "surrender/srVertexPipe.h"
#include "surrender/srMath.h"

/* Flag-name tables dump walks while printing the render_control and dirty_flags bits. Nothing sets
   them, so dump prints numeric bit indices. */
// GLOBAL: SURRENDER 0x100A4998
static const char* s_control_names;

// GLOBAL: SURRENDER 0x100A499C
static const char* s_flag_names0;

/* Copy to scratch, then reorder the table. */
template <class T> static void permuteTable(T* table, const w8_ulong* indices, w8_long count)
{
    if (count > 0) {
        std::vector<T> scratch(table, table + count);
        for (w8_long index = 0; index < count; ++index) {
            table[index] = scratch[indices[index]];
        }
    }
}

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
srVector3T<float>* srMeshModel::getVertexLoc()
{
    if (vertex_location_count == 0) {
        return 0;
    }
    if (vertex_locations.empty()) {
        vertex_locations.resize(vertex_location_count, srVector3T<float>(0.0f, 0.0f, 0.0f));
        setDirty(DIRTY_TRI_MESH);
    }
    return vertex_locations.data();
}

// FUNCTION: SURRENDER 0x100400D0
srVector3T<float>* srMeshModel::getVertexNormal()
{
    if (vertex_location_count == 0) {
        return 0;
    }
    if (vertex_normals.empty()) {
        vertex_normals.resize(vertex_location_count, srVector3T<float>(0.0f, 0.0f, 0.0f));
        setDirty(DIRTY_TRI_MESH);
    }
    if ((dirty_flags.value & (1UL << DIRTY_VERTEX_NORMALS)) != 0) {
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
    if ((dirty_flags.value & (1UL << DIRTY_POLYGON_NORMALS)) != 0) {
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
srVector3i* srMeshModel::getPolyVertex()
{
    if (polygon_count == 0) {
        return 0;
    }
    if (poly_vertices.empty()) {
        srVector3i zero;
        zero.x = zero.y = zero.z = 0;
        poly_vertices.resize(polygon_count, zero);
        setDirty(DIRTY_TRI_MESH);
    }
    return poly_vertices.data();
}

template<class T>
static T* GetLazyMeshTable(srMeshModel& mesh, std::vector<T>& slot, w8_long count,
                           int create, const T& initial = T{})
{
    if (slot.empty() && create != 0 && count != 0) {
        slot.resize(count, initial);
        mesh.setDirty(srMeshModel::DIRTY_TRI_MESH);
    }
    return slot.empty() ? nullptr : slot.data();
}

// FUNCTION: SURRENDER 0x10040430
std::shared_ptr<srMaterialIFace>* srMeshModel::getVertexMaterial(w8_long vertex, e_side side, int table)
{
    if (vertex < 0 || vertex > 3 || (int)side < 0 || (int)side > 1) {
        return 0;
    }
    return GetLazyMeshTable(*this, vertex_materials[vertex][side], vertex_location_count, table);
}

// FUNCTION: SURRENDER 0x10040560
std::shared_ptr<srTextureIFace>* srMeshModel::getPolyTexture(w8_long polygon, w8_long layer, int table)
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
srVector3i* srMeshModel::getPolyUVIndex(w8_long layer, int table)
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
srVector3T<float>* srMeshModel::getVertexDIG(w8_long vertex, int table)
{
    if (vertex < 0 || vertex > 3) {
        return 0;
    }
    return GetLazyMeshTable(*this, dig[vertex], vertex_location_count, table,
                            srVector3T<float>(0.0f, 0.0f, 0.0f));
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
    poly_vertices = std::vector<srVector3i>{};
    poly_equations = std::vector<glm::vec4>{};
    vertex_locations = std::vector<srVector3T<float>>{};
    vertex_normals = std::vector<srVector3T<float>>{};
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
    render_control.value = 0;
    render_control.value |= 1UL << CONTROL_FRONT;
    render_control.value |= 1UL << CONTROL_SKIP_AUTO_BOX;
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
        const std::span<const srVector3> vertices(getVertexLoc(), vertex_location_count);
        const auto box = boundingBox(vertices);
        bounds_minimum = box.minimum;
        bounds_maximum = box.maximum;
        const auto center = box.center();
        bounds_center = center;
        float radius_squared = 0;
        for (const auto& vertex : vertices) {
            const auto offset = vertex.glmValue() - center;
            radius_squared = glm::max(radius_squared, glm::dot(offset, offset));
        }
        bounds_radius = std::sqrt(radius_squared) * 1.00001f;
        dirty_flags.value &= ~(1UL << DIRTY_BOUNDS);
    }
}

// FUNCTION: SURRENDER 0x1003E280
void srMeshModel::calculatePolygonNormals()
{
    dirty_flags.value &= ~(1UL << DIRTY_POLYGON_NORMALS);
    glm::vec4* equations = getPolyEq();
    srVector3i* polygons = getPolyVertex();
    srVector3T<float>* vertices = getVertexLoc();
    for (w8_long polygon = 0; polygon < polygon_count; polygon++) {
        const auto first = vertices[polygons[polygon].x].glmValue();
        const auto second = vertices[polygons[polygon].y].glmValue();
        const auto third = vertices[polygons[polygon].z].glmValue();
        const auto normal = glm::cross(second - first, third - first);
        equations[polygon] = glm::vec4(normal, -glm::dot(normal, first));
    }
}

// FUNCTION: SURRENDER 0x1003E390
void srMeshModel::calculateVertexNormals()
{
    if (polygon_count == 0 || vertex_location_count == 0)
        return;
    if (dirty_flags.value & (1UL << DIRTY_POLYGON_NORMALS))
        calculatePolygonNormals();
    dirty_flags.value &= ~(1UL << DIRTY_VERTEX_NORMALS);
    const auto* equations = getPolyEq();
    const auto* polygons = getPolyVertex();
    const auto* shade_indices = getVertexShadeIndex(0);
    std::vector<glm::vec3> smooth(vertex_location_count, glm::vec3(0));
    for (w8_long polygon = 0; polygon < polygon_count; ++polygon) {
        const glm::vec3 normal(equations[polygon]);
        for (const auto vertex : {polygons[polygon].x, polygons[polygon].y, polygons[polygon].z})
            smooth[shade_indices ? shade_indices[vertex] : vertex] += normal;
    }
    auto* normals = getVertexNormal();
    for (std::size_t vertex = 0; vertex < vertex_location_count; ++vertex)
        normals[vertex] = normalizedOrZero(smooth[shade_indices ? shade_indices[vertex] : vertex]);
}

// FUNCTION: SURRENDER 0x1003E690
void srMeshModel::scale(const srVector3T<float>& scale)
{
    srVector3T<float>* vertices = getVertexLoc();
    for (w8_long index = 0; index < vertex_location_count; index++) {
        vertices[index] *= scale;
    }
    setDirtyAll();
}

// FUNCTION: SURRENDER 0x1003E780
void srMeshModel::applyMatrix(const glm::mat3& matrix)
{
    auto* vertices = getVertexLoc();
    for (w8_long i = 0; i < vertex_location_count; ++i)
        vertices[i] = matrix * vertices[i].glmValue();
    setDirtyAll();
}

// FUNCTION: SURRENDER 0x1003E8C0
void srMeshModel::relocateVertices(const srVector3T<float>& offset)
{
    srVector3T<float>* vertices = getVertexLoc();
    for (w8_long index = 0; index < vertex_location_count; index++) {
        vertices[index] += offset;
    }
    setDirtyAll();
}

// FUNCTION: SURRENDER 0x1003E9B0
void srMeshModel::centerVertices()
{
    if (vertex_location_count == 0)
        return;
    glm::dvec3 sum(0);
    for (const auto& vertex : std::span(getVertexLoc(), vertex_location_count))
        sum += glm::dvec3(vertex.glmValue());
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
        radius += vertex.Length();
    return radius / vertex_location_count;
}

// FUNCTION: SURRENDER 0x1003EB20
double srMeshModel::getMaxRadius()
{
    float maximum = 0;
    for (const auto& vertex : std::span(getVertexLoc(), vertex_location_count)) {
        const auto value = vertex.glmValue();
        maximum = glm::max(maximum, glm::dot(value, value));
    }
    return std::sqrt(maximum);
}

// FUNCTION: SURRENDER 0x1003EBB0
void srMeshModel::scaleToAverageRadius(double radius)
{
    const double current_radius = getAverageRadius();
    if (current_radius > 0)
        scale(glm::vec3(static_cast<float>(radius / current_radius)));
}

// FUNCTION: SURRENDER 0x1003EBF0
void srMeshModel::scaleToMaxRadius(double radius)
{
    const double current_radius = getMaxRadius();
    if (current_radius > 0)
        scale(glm::vec3(static_cast<float>(radius / current_radius)));
}

// FUNCTION: SURRENDER 0x1003EC30
void srMeshModel::flipFaces()
{
    if (polygon_count != 0) {
        srVector3i* polygons = getPolyVertex();
        for (w8_long polygon = 0; polygon < polygon_count; polygon++) {
            w8_long first = polygons[polygon].x;
            polygons[polygon].x = polygons[polygon].y;
            polygons[polygon].y = first;
        }
        setDirtyAll();
    }
}

// FUNCTION: SURRENDER 0x1003ED20
w8_long srMeshModel::findClosestVertex(const srVector3T<float>& position)
{
    const auto target = position.glmValue();
    float minimum = std::numeric_limits<float>::infinity();
    w8_long closest = 0;
    const auto* vertices = getVertexLoc();
    for (w8_long i = 0; i < vertex_location_count; ++i) {
        const auto offset = vertices[i].glmValue() - target;
        const float distance = glm::dot(offset, offset);
        if (distance < minimum) {
            minimum = distance;
            closest = i;
        }
    }
    return closest;
}

// FUNCTION: SURRENDER 0x1003DC70
const srMeshModel::TriMesh& srMeshModel::getTriMesh()
{
    if ((dirty_flags.value & (1UL << DIRTY_TRI_MESH)) != 0) {
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
    tri_mesh.control_flags = render_control.value;
    tri_mesh.sort_bias = sort_bias;
    tri_mesh.poly_vertices = getPolyVertex();
    tri_mesh.poly_equations = getPolyEq();
    tri_mesh.positions = getVertexLoc();
    tri_mesh.normals = getVertexNormal();
    for (w8_long pass = 0; pass < 4; pass++) {
        for (w8_long side = 0; side < 2; side++) {
            tri_mesh.materials[pass][side] = std::static_pointer_cast<srMaterial>(materials[pass][side]);
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
    if ((tri_mesh.control_flags & (1UL << srMeshModel::CONTROL_SKIP_AUTO_BOX)) == 0) {
        const auto box = getBoundingBox();
        tri_mesh.bounds_minimum = box.minimum;
        tri_mesh.bounds_maximum = box.maximum;
    }
    if ((tri_mesh.control_flags & (1UL << srMeshModel::CONTROL_SKIP_AUTO_SPHERE)) == 0) {
        const auto sphere = getBoundingSphere();
        tri_mesh.bounds_center = sphere.center;
        tri_mesh.bounds_radius = sphere.radius;
    }
    tri_mesh.active_polygons.reset();
    if (const auto* active = getActivePolygonTable(0)) {
        tri_mesh.active_polygons = std::span<const w8_ulong>(active, active_polygon_count);
    }
    dirty_flags.value &= ~(1UL << DIRTY_TRI_MESH);
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

// FUNCTION: SURRENDER 0x1003FE40
srBoundingBox srMeshModel::getBoundingBox()
{
    if ((dirty_flags.value & (1UL << DIRTY_BOUNDS)) != 0) {
        calculateBounds();
    }
    return {bounds_minimum, bounds_maximum};
}

// FUNCTION: SURRENDER 0x1003FE90
srModel::BoundingSphere srMeshModel::getBoundingSphere()
{
    if ((dirty_flags.value & (1UL << DIRTY_BOUNDS)) != 0) {
        calculateBounds();
    }
    return {bounds_center, bounds_radius};
}

static glm::vec4* GetVertexColorTable(srMeshModel& mesh,
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

// FUNCTION: SURRENDER 0x1003CEA0
void srMeshModel::render(srGERD& renderer)
{
    if ((render_control.value & (1UL << CONTROL_SKIP_AUTO_SPHERE)) == 0) {
        const auto [center, radius] = getBoundingSphere();
        if (renderer.testBoundingSphere(center, radius) == srGERD::VISIBILITY_OUTSIDE) {
            return;
        }
    }
    if ((render_control.value & (1UL << CONTROL_SKIP_AUTO_BOX)) == 0 &&
        vertex_location_count >= 8) {
        const auto [minimum, maximum] = getBoundingBox();
        if (renderer.testBoundingBox(minimum, maximum) == srGERD::VISIBILITY_OUTSIDE) {
            return;
        }
    }
    renderTriMesh(renderer, getTriMesh());
}

// FUNCTION: SURRENDER 0x100418C0
void srMeshModel::reindexPolygons(const w8_ulong* indices)
{
    w8_long index;
    w8_long pass;
    if (polygon_count != 0) {
        permuteTable(getPolyEq(), indices, polygon_count);
        permuteTable(getPolyVertex(), indices, polygon_count);
        for (pass = 0; pass < pass_count; ++pass) {
            if (getPolyUVIndex(pass, 0) != 0) {
                permuteTable(getPolyUVIndex(pass, 0), indices, polygon_count);
            }
            if (getPolyShader(pass, 0) != 0) {
                permuteTable(getPolyShader(pass, 0), indices, polygon_count);
            }
            for (w8_long layer = 0; layer < 2; ++layer) {
                if (getPolyTexture(pass, layer, 0) != 0) {
                    permuteTable(getPolyTexture(pass, layer, 0), indices, polygon_count);
                }
            }
        }
        if (active_polygon_count != 0 && getActivePolygonTable(0) != 0) {
            w8_ulong* table = getActivePolygonTable(1);
            std::vector<w8_ulong> forward(polygon_count);
            for (index = 0; index < polygon_count; ++index) {
                forward[indices[index]] = index;
            }
            for (index = 0; index < active_polygon_count; ++index) {
                table[index] = forward[table[index]];
            }
        }
        setDirtyAll();
    }
}

// FUNCTION: SURRENDER 0x10042230
void srMeshModel::reindexVertices(const w8_ulong* indices)
{
    w8_long component;
    w8_long pass;
    w8_long polygon;
    w8_long side;
    w8_long table;
    w8_long index;
    if (vertex_location_count != 0) {
        std::vector<w8_ulong> forward(vertex_location_count);
        for (index = 0; index < vertex_location_count; ++index) {
            forward[indices[index]] = index;
        }
        permuteTable(getVertexNormal(), indices, vertex_location_count);
        permuteTable(getVertexLoc(), indices, vertex_location_count);
        w8_ulong* shade = getVertexShadeIndex(0);
        if (shade != 0) {
            std::vector<w8_ulong> copy(shade, shade + vertex_location_count);
            for (index = 0; index < vertex_location_count; ++index) {
                shade[index] = forward[copy[indices[index]]];
            }
            for (index = 0; index < vertex_location_count; ++index) {
                w8_ulong value = shade[index];
                if (value < (w8_ulong)index) {
                    shade[index] = shade[value];
                    shade[value] = value;
                }
            }
        }
        for (pass = 0; pass < pass_count; ++pass) {
            for (side = 0; side < 2; ++side) {
                if (getVertexMaterial(pass, static_cast<e_side>(side), 0) != 0) {
                    permuteTable(getVertexMaterial(pass, static_cast<e_side>(side), 0), indices,
                                   vertex_location_count);
                }
            }
            for (table = 0; table < 2; ++table) {
                if (getVertexTexCoords(pass, table, 0) != 0) {
                    permuteTable(getVertexTexCoords(pass, table, 0), indices,
                                 vertex_location_count);
                }
            }
            if (getVertexDIG(pass, 0) != 0) {
                permuteTable(getVertexDIG(pass, 0), indices, vertex_location_count);
            }
            if (getVertexSCG(pass, 0) != 0) {
                permuteTable(getVertexSCG(pass, 0), indices, vertex_location_count);
            }
            if (getVertexDCG(pass, 0) != 0) {
                permuteTable(getVertexDCG(pass, 0), indices, vertex_location_count);
            }
            if (getPolyUVIndex(pass, 0) != 0) {
                srVector3i* uv = getPolyUVIndex(pass, 1);
                for (polygon = 0; polygon < polygon_count; ++polygon, ++uv) {
                    int* corner = &uv->x;
                    for (component = 0; component < 3; ++component, ++corner) {
                        int index = *corner;
                        if (index < vertex_location_count) {
                            index = forward[index];
                        }
                        *corner = index;
                    }
                }
            }
        }
        srVector3i* vertices = getPolyVertex();
        for (polygon = 0; polygon < polygon_count; ++polygon, ++vertices) {
            int* corner = &vertices->x;
            for (component = 0; component < 3; ++component, ++corner) {
                *corner = forward[*corner];
            }
        }
        setDirtyAll();
    }
}

// FUNCTION: SURRENDER 0x10040C40
void srMeshModel::verify(srRuntimeClass::e_verify mode)
{
    w8_long i;
    w8_long j;
    w8_long p;
    w8_long s;
    w8_long v;

    srClass::verify(mode);
    auto t = getTriMesh();
    if (t.vertex_count <= 0) {
        srAssertFail("t.vnum > 0", "D:\\srsdk1x\\sources\\corelib\\srMeshModel.cpp", 0x5cf, 0);
    }
    if (t.polygon_count <= 0) {
        srAssertFail("t.pnum > 0", "D:\\srsdk1x\\sources\\corelib\\srMeshModel.cpp", 0x5d0, 0);
    }
    if (t.pass_count > 0 && t.pass_count <= MAX_PASSES) {
        if (!std::isfinite(t.sort_bias)) {
            srAssertFail("srFinite(t.sortBias)", "D:\\srsdk1x\\sources\\corelib\\srMeshModel.cpp",
                         0x5d2, 0);
        }
        if (t.active_polygons) {
            const auto active_count = static_cast<w8_long>(t.active_polygons->size());
            if (t.polygon_count < active_count) {
                srAssertFail("t.aPnum <= t.pnum", "D:\\srsdk1x\\sources\\corelib\\srMeshModel.cpp",
                             0x5da, 0);
            }
            for (i = 0; i < active_count; ++i) {
                /* c-style-cast-ok: the assert text spells (SRDWORD)(t.pnum). */
                if ((SRDWORD)t.polygon_count <= (*t.active_polygons)[i]) {
                    srAssertFail("t.APT[i] < (SRDWORD)(t.pnum)",
                                 "D:\\srsdk1x\\sources\\corelib\\srMeshModel.cpp", 0x5dd, 0);
                }
            }
            for (i = 0; i < active_count; ++i) {
                for (j = i + 1; j < active_count; ++j) {
                    if ((*t.active_polygons)[i] == (*t.active_polygons)[j]) {
                        srAssertFail("t.APT[i] != t.APT[j]",
                                     "D:\\srsdk1x\\sources\\corelib\\srMeshModel.cpp", 0x5e1, 0);
                    }
                }
            }
        }
        if (t.poly_vertices == 0) {
            srAssertFail("t.pVertex", "D:\\srsdk1x\\sources\\corelib\\srMeshModel.cpp", 0x5e5, 0);
        }
        for (i = 0; i < t.polygon_count; ++i) {
            int* component = &t.poly_vertices[i].x;
            for (j = 0; j < 3; ++j, ++component) {
                if (*component < 0 || t.vertex_count <= *component) {
                    srAssertFail("t.pVertex[i][j] >= 0 && t.pVertex[i][j] < t.vnum",
                                 "D:\\srsdk1x\\sources\\corelib\\srMeshModel.cpp", 0x5ed, 0);
                }
            }
        }
        if (t.poly_equations == 0) {
            srAssertFail("t.pEq", "D:\\srsdk1x\\sources\\corelib\\srMeshModel.cpp", 0x5f1, 0);
        }
        for (i = 0; i < t.polygon_count; ++i) {
            if (!isFinite(t.poly_equations[i])) {
                srAssertFail("t.pEq[i].isValid()", "D:\\srsdk1x\\sources\\corelib\\srMeshModel.cpp",
                             0x5f6, 0);
            }
        }
        if (t.positions == 0) {
            srAssertFail("t.vLoc", "D:\\srsdk1x\\sources\\corelib\\srMeshModel.cpp", 0x5fa, 0);
        }
        for (i = 0; i < t.vertex_count; ++i) {
            if (!t.positions[i].isValid()) {
                srAssertFail("t.vLoc[i].isValid()",
                             "D:\\srsdk1x\\sources\\corelib\\srMeshModel.cpp", 0x5fe, 0);
            }
        }
        if (t.normals == 0) {
            srAssertFail("t.vNorm", "D:\\srsdk1x\\sources\\corelib\\srMeshModel.cpp", 0x602, 0);
        }
        for (i = 0; i < t.vertex_count; ++i) {
            if (fabs(t.normals[i].Length() - 1.0) >= 0.01) {
                srAssertFail("fabs(t.vNorm[i].length()-1.0) < 0.01",
                             "D:\\srsdk1x\\sources\\corelib\\srMeshModel.cpp", 0x606, 0);
            }
        }
        for (p = 0; p < pass_count; ++p) {
            if (t.dig[p] != 0) {
                for (i = 0; i < t.vertex_count; ++i) {
                    if (!t.dig[p][i].isValid()) {
                        srAssertFail("t.DIG[p][i].isValid()",
                                     "D:\\srsdk1x\\sources\\corelib\\srMeshModel.cpp", 0x611, 0);
                    }
                }
            }
            if (t.dcg[p] != 0) {
                for (i = 0; i < t.vertex_count; ++i) {
                    if (!isFinite(t.dcg[p][i])) {
                        srAssertFail("t.DCG[p][i].isValid()",
                                     "D:\\srsdk1x\\sources\\corelib\\srMeshModel.cpp", 0x615, 0);
                    }
                }
            }
            if (t.scg[p] != 0) {
                for (i = 0; i < t.vertex_count; ++i) {
                    if (!isFinite(t.scg[p][i])) {
                        srAssertFail("t.SCG[p][i].isValid()",
                                     "D:\\srsdk1x\\sources\\corelib\\srMeshModel.cpp", 0x619, 0);
                    }
                }
            }
            for (j = 0; j < 2; ++j) {
                if (t.texcoords[p][j] != 0) {
                for (i = 0; i < t.vertex_count; ++i) {
                    if (!isFinite(t.texcoords[p][j][i])) {
                            srAssertFail("t.vUV[p][j][i].isValid()",
                                         "D:\\srsdk1x\\sources\\corelib\\srMeshModel.cpp", 0x620,
                                         0);
                        }
                    }
                }
                if (t.poly_textures[p][j] != 0) {
                    for (i = 0; i < t.polygon_count; ++i) {
                        if (!t.poly_textures[p][j][i]) {
                            srAssertFail("t.pTexture[p][j][i]",
                                         "D:\\srsdk1x\\sources\\corelib\\srMeshModel.cpp", 0x625,
                                         0);
                        }
                    }
                }
            }
            for (s = 0; s < 2; ++s) {
                if (t.vertex_materials[p][s] != 0) {
                    for (v = 0; v < t.vertex_count; ++v) {
                        if (!t.vertex_materials[p][s][v]) {
                            srAssertFail("t.vMaterial[p][s][v]",
                                         "D:\\srsdk1x\\sources\\corelib\\srMeshModel.cpp", 0x62f,
                                         0);
                        }
                    }
                }
            }
            if (t.poly_shaders[p] == 0) {
                if (!t.shaders[p].isValid()) {
                    srAssertFail("t.shader[p].isValid()",
                                 "D:\\srsdk1x\\sources\\corelib\\srMeshModel.cpp", 0x639, 0);
                }
            } else {
                for (v = 0; v < t.polygon_count; ++v) {
                    if (!t.poly_shaders[p][v].isValid()) {
                        srAssertFail("t.pShader[p][v].isValid()",
                                     "D:\\srsdk1x\\sources\\corelib\\srMeshModel.cpp", 0x637, 0);
                    }
                }
            }
        }
        return;
    }
    srAssertFail("t.passes > 0 && t.passes <= MAX_PASSES",
                 "D:\\srsdk1x\\sources\\corelib\\srMeshModel.cpp", 0x5d1, 0);
}

// FUNCTION: SURRENDER 0x1003EE00
void srMeshModel::dump(std::ostream& stream)
{
    w8_long pass;
    int stage;
    w8_long polygon;
    w8_long vertex;
    w8_long side;
    w8_long changes;

    srModel::dump(stream);
    w8_long flags = srGetStreamFlags(stream);
    srSetStreamFlags(stream, (flags & 0xfffffe7fL) | 0x40);
    stream.width(0x20);
    stream << "  Polygons: " << polygon_count << '\n';
    stream.width(0x20);
    stream << "  Vertices: " << vertex_location_count << '\n';
    stream.width(0x20);
    stream << "  Passes: " << pass_count << '\n';
    stream.width(0x20);
    const auto [minimum, maximum] = getBoundingBox();
    stream << "  Bounding box min: ";
    stream << '{' << minimum.x << ',' << minimum.y << ',' << minimum.z << '}' << '\n';
    stream.width(0x20);
    stream << "  Bounding box max: ";
    stream << '{' << maximum.x << ',' << maximum.y << ',' << maximum.z << '}' << '\n';
    const auto [center, radius] = getBoundingSphere();
    stream.width(0x20);
    stream << "  Bounding sphere origin: ";
    stream << '{' << center.x << ',' << center.y << ',' << center.z << '}' << '\n';
    stream.width(0x20);
    stream << "  Bounding sphere radius: " << radius << '\n';
    stream.width(0x20);
    stream << "  Sort bias: " << sort_bias << '\n';
    stream.width(0x20);
    stream << "  Flags: ";
    srDumpFlags(stream, dirty_flags.value, s_flag_names0);
    stream << '\n';
    stream.width(0x20);
    stream << "  Control flags: ";
    srDumpFlags(stream, render_control.value, s_control_names);
    stream << "\n\n";
    stream << "  Materials, textures, shaders\n";
    stream << "  ----------------------------\n";
    for (pass = 0; pass < 4; ++pass) {
        bool defined = (this->materials[pass][0] != 0) || (this->materials[pass][1] != 0);
        for (stage = 0; stage < 2; ++stage) {
            if (this->textures[pass][stage] != 0) {
                defined = true;
            }
        }
        if (defined) {
            stream << "    Pass " << pass << '\n';
        }
        if (this->materials[pass][0] != 0) {
            stream.width(0x20);
            stream << "      Front material: " << this->materials[pass][0]->getName() << '\n';
        }
        if (this->materials[pass][1] != 0) {
            stream.width(0x20);
            stream << "      Back material: " << this->materials[pass][1]->getName() << '\n';
        }
        if (defined) {
            stream.width(0x20);
            stream << "      Shader: " << this->shaders[pass] << '\n';
        }
        for (stage = 0; stage < 2; ++stage) {
            if (this->textures[pass][stage] != 0) {
                char label[36];
                sprintf(label, "      Texture %d", stage);
                stream.width(0x20);
                stream << label << this->textures[pass][stage]->getName() << '\n';
            }
        }
    }
    stream << '\n' << "  Polygon fields defined\n";
    stream << "  ----------------------\n";
    stream.width(0x20);
    if (getPolyEq() != 0) {
        stream << "    Polygon plane equations" << '\n';
    }
    stream.width(0x20);
    if (getPolyVertex() != 0) {
        stream << "    Polygon vertex indices" << '\n' << '\n';
    }
    for (pass = 0; pass < 4; ++pass) {
        srShader* shaders = getPolyShader(pass, 0);
        srVector3i* uv = getPolyUVIndex(pass, 0);
        std::shared_ptr<srTextureIFace>* textures[2];
        bool defined = false;
        for (stage = 0; stage < 2; ++stage) {
            textures[stage] = getPolyTexture(pass, stage, 0);
            if (textures[stage] != 0) {
                defined = true;
            }
        }
        if (defined || shaders != 0 || uv != 0) {
            stream << "    Pass " << pass << '\n';
            if (shaders != 0) {
                stream << "      Shader override array" << '\n';
            }
            if (uv != 0) {
                stream << "      Polygon UV override array" << '\n';
            }
            for (stage = 0; stage < 2; ++stage) {
                if (textures[stage] != 0) {
                    stream << "      Texture override array (stage " << stage << ")\n";
                }
            }
        }
    }
    stream << "\n  Vertex fields defined\n";
    stream << "  ---------------------\n";
    stream.width(0x20);
    if (getVertexLoc() != 0) {
        stream << "    Vertex locations" << '\n';
    }
    stream.width(0x20);
    if (getVertexNormal() != 0) {
        stream << "    Vertex normals" << '\n';
    }
    stream.width(0x20);
    if (getVertexShadeIndex(0) != 0) {
        stream << "    Vertex normal remapping indices" << '\n';
    }
    for (pass = 0; pass < 4; ++pass) {
        srVector3T<float>* dig = getVertexDIG(pass, 0);
        glm::vec4* scg = getVertexSCG(pass, 0);
        glm::vec4* dcg = getVertexDCG(pass, 0);
        glm::vec2* uvs[2];
        for (stage = 0; stage < 2; ++stage) {
            uvs[stage] = getVertexTexCoords(pass, stage, 0);
        }
        std::shared_ptr<srMaterialIFace>* front = getVertexMaterial(pass, SIDE_FRONT, 0);
        std::shared_ptr<srMaterialIFace>* back = getVertexMaterial(pass, SIDE_BACK, 0);
        bool defined = dig != 0 || scg != 0 || dcg != 0 || front != 0 || back != 0;
        for (stage = 0; stage < 2; ++stage) {
            if (uvs[stage] != 0) {
                defined = true;
            }
        }
        if (defined) {
            stream << "    Pass " << pass << '\n';
        }
        if (dig != 0) {
            stream << "      DIG array\n";
        }
        if (scg != 0) {
            stream << "      SCG array\n";
        }
        if (dcg != 0) {
            stream << "      DCG array\n";
        }
        for (stage = 0; stage < 2; ++stage) {
            if (uvs[stage] != 0) {
                stream << "      Texture coordinate array (stage " << stage << ")";
                if (stage == 0) {
                    stream << " (" << getUVCount() << " UV entries)";
                }
                stream << '\n';
            }
        }
        if (front != 0) {
            stream << "      Front material override array\n";
        }
        if (back != 0) {
            stream << "      Back material override array\n";
        }
    }
    stream << '\n';
    srVector3i* vertices = getPolyVertex();
    w8_ulong strips = 1;
    for (pass = 0; pass < pass_count; ++pass) {
        for (polygon = 1; polygon < polygon_count; ++polygon) {
            int* next = &vertices[polygon].x;
            int* previous = &vertices[polygon - 1].x;
            w8_long shared = 0;
            for (w8_long a = 0; a < 3; ++a) {
                for (w8_long b = 0; b < 3; ++b) {
                    if (next[a] == previous[b]) {
                        ++shared;
                    }
                }
            }
            if (getPolyShader(pass, 0) != 0) {
                srShader* shaders = getPolyShader(pass, 1);
                if (shaders[polygon].value != shaders[polygon - 1].value) {
                    shared = 0;
                }
            }
            for (stage = 0; stage < 2; ++stage) {
                if (getPolyTexture(pass, stage, 0) != 0) {
                    std::shared_ptr<srTextureIFace>* textures = getPolyTexture(pass, stage, 1);
                    if (textures[polygon] != textures[polygon - 1]) {
                        shared = 0;
                    }
                }
            }
            if (shared < 2) {
                ++strips;
            }
        }
    }
    w8_ulong shader_changes = 0;
    w8_ulong texture_changes = 0;
    w8_ulong material_changes = 0;
    for (pass = 0; pass < pass_count; ++pass) {
        if (getPolyShader(pass, 0) != 0) {
            srShader* shaders = getPolyShader(pass, 1);
            changes = 1;
            for (polygon = 1; polygon < polygon_count; ++polygon) {
                if (shaders[polygon].value != shaders[polygon - 1].value) {
                    ++changes;
                }
            }
            shader_changes += changes;
        }
        for (stage = 0; stage < 2; ++stage) {
            if (getPolyTexture(pass, stage, 0) != 0) {
                std::shared_ptr<srTextureIFace>* textures = getPolyTexture(pass, stage, 1);
                changes = 1;
                for (polygon = 1; polygon < polygon_count; ++polygon) {
                    if (textures[polygon] != textures[polygon - 1]) {
                        ++changes;
                    }
                }
                texture_changes += changes;
            }
        }
        for (side = 0; side < 2; ++side) {
            if (getVertexMaterial(pass, static_cast<e_side>(side), 0) != 0) {
                std::shared_ptr<srMaterialIFace>* materials =
                    getVertexMaterial(pass, static_cast<e_side>(side), 1);
                changes = 1;
                for (vertex = 1; vertex < vertex_location_count; ++vertex) {
                    if (materials[vertex] != materials[vertex - 1]) {
                        ++changes;
                    }
                }
                material_changes += changes;
            }
        }
    }
    if (texture_changes == 0) {
        texture_changes = 1;
    }
    if (shader_changes == 0) {
        shader_changes = 1;
    }
    if (material_changes == 0) {
        material_changes = 1;
    }
    stream.width(0x20);
    stream << "  Total triangle strips:" << strips << '\n';
    stream.width(0x20);
    stream << "  Texture changes:" << texture_changes << '\n';
    stream.width(0x20);
    stream << "  Shader changes:" << shader_changes << '\n';
    stream.width(0x20);
    stream << "  Material changes:" << material_changes << '\n';
    stream << '\n';
    srSetStreamFlags(stream, flags & 0x7fff);
}

// GLOBAL: SURRENDER 0x100A4790
srTriMeshPipeline* srTriMeshPipeline::pipe = 0;

srTriMeshPipeline::srTriMeshPipeline() : vertex_pipe(std::make_unique<srVertexPipe>())
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
    material = srCore.getMaterial();

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
            const auto box = boundingBox(std::span<const srVector3>(positions, vertex_count));
            bounds_minimum = box.minimum;
            bounds_maximum = box.maximum;
        }

        bounds_center = (bounds_minimum + bounds_maximum) * 0.5f;

        bounds_radius = glm::distance(bounds_center, bounds_minimum) * 1.001f;
    }

    const auto eye_bounds = this->renderer->getEyeSpaceBounds(bounds_center, bounds_radius);
    const auto model_view = this->renderer->getMatrix(srGERD::MATRIX_MODELVIEW);
    const auto inverse_model_view = this->renderer->getInverseModelViewMatrix();
    const auto clip_planes = this->renderer->getClipPlanes();
    const auto project_clip_near = this->renderer->getProjectClipNearMatrix();
    const auto normal_matrix = this->renderer->getNormalMatrix();

    srGERD::e_cullMode cull_mode = this->renderer->getCullMode();
    srGERD::e_winding winding = this->renderer->getWinding();

    srTriangleCuller::Input culler_input;
    if (cull_mode == srGERD::CULL_NONE) {
        culler_input.cull_mode = 2;
    } else if (cull_mode == srGERD::CULL_FRONT) {
        culler_input.cull_mode = winding == srGERD::WINDING_POSITIONAL_0;
    } else {
        culler_input.cull_mode = winding != srGERD::WINDING_POSITIONAL_0;
    }

    culler_input.vertex_count = vertex_count;
    culler_input.vertices = positions;
    culler_input.clip_planes = clip_planes.planes;
    culler_input.model_view = &model_view;
    culler_input.inverse_model_view = &inverse_model_view;

    if ((this->flags & FRUSTUM_CLIPPING) == 0) {
        culler_input.clip_mask = 0;
    } else {
        const auto clip = srTriangleCuller::getClipMask(
            eye_bounds.center, eye_bounds.radius, clip_planes.planes, clip_planes.mask);
        const bool retain_clip_mask =
            clip.mask != 0 && ((clip.mask & 0xffffffc0UL) != 0 ||
                               (slot_count * triangle_count > 45 && clip.depth > 0.23f));
        culler_input.clip_mask = retain_clip_mask ? clip.mask : 0;
    }

    if (!active_triangles) {
        srCore.getStatisticsManager()->statistics.triangles_submitted +=
            slot_count * triangle_count;
    } else {
        srCore.getStatisticsManager()->statistics.triangles_submitted +=
            slot_count * active_triangles->size();
    }
    ++srCore.getStatisticsManager()->statistics.meshes_submitted;
    srCore.getStatisticsManager()->statistics.vertices_submitted += slot_count * vertex_count;

    const auto total = active_triangles ? static_cast<w8_ulong>(active_triangles->size())
                                       : triangle_count;
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
            culler_input.triangle_count = batch_count;
            culler_input.active_triangle_count = 0;
            culler_input.active_triangles = 0;
            culler_input.projected_vertices = projected_vertices + processed;
            culler_input.triangles = triangles + processed;
        } else {
            culler_input.triangle_count = triangle_count;
            culler_input.active_triangle_count = batch_count;
            culler_input.active_triangles = active_triangles->data() + processed;
            culler_input.projected_vertices = projected_vertices;
            culler_input.triangles = triangles;
        }

        auto culler_output = srTriangleCuller::cull(culler_input);
        if (!culler_output.indices.empty()) {
            srCore.getStatisticsManager()->statistics.triangles_after_culling +=
                slot_count * culler_output.triangleCount();
            srCore.getStatisticsManager()->statistics.vertices_after_culling +=
                slot_count * culler_output.vertexCount();

            srGERD::Renderer* renderer = this->renderer->lockRenderer();

            this->vertex_arrays.resize(slot_count);
            srVertexArray* vertex_arrays = this->vertex_arrays.data();
            vertex_arrays[0] = renderer->allocVertexArray(slot_count * culler_output.vertexCount());
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

            srVertexPipe::Input pipe_input;
            pipe_input.vertex_count = culler_output.vertexCount();
            pipe_input.active_vertices = culler_output.avt.data();
            pipe_input.direct_vertex_indices = false;
            pipe_input.positions = positions;
            pipe_input.normals = vertex_extras;
            pipe_input.eye_center = eye_bounds.center;
            pipe_input.eye_radius = eye_bounds.radius;
            pipe_input.model_view = &model_view;
            pipe_input.normal_matrix = &normal_matrix;
            pipe_input.vertex_arrays = &this->vertex_arrays[0];
            pipe_input.exclusion_mask = exclusion_mask;
            pipe_input.ambient_light = ambient_light;
            pipe_input.records = std::span{records}.first(slot_count);
            pipe_input.processors = processors;
            pipe_input.processor_count = processor_count;
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
                    srFlags<srVertexProcessor::e_channel> flags =
                        srVertexPipe::getShaderDisableMask(passes[pass_index].shaders,
                                                           culler_output.indices.data(),
                                                           culler_output.triangleCount());
                    disable_mask = flags.value;
                } else {
                    srFlags<srVertexProcessor::e_channel> flags =
                        srVertexPipe::getShaderDisableMask(passes[pass_index].shader);
                    disable_mask = flags.value;
                }
                records[pass_index].channels = disable_mask | renderer_disable_mask;
            }

            vertex_pipe->process(pipe_input);

            srGERD::Renderer::TriInput render_input;
            render_input.triangle_count = culler_output.triangleCount();
            render_input.record_count = slot_count;
            render_input.vertex_count = culler_output.vertexCount();
            render_input.indices = culler_output.indices.data();
            render_input.triangles = triangles;
            render_input.vertices = culler_output.vertex_remap.data();
            render_input.passes = &passes[0];
            render_input.direct_vertex_indices = false;
            render_input.project_clip_near = &project_clip_near;
            render_input.sort_bias = sort_bias;
            renderer->render(render_input);
            this->renderer->unlockRenderer(renderer, 0);
        }

        processed += batch_limit;
    }
}

// FUNCTION: SURRENDER 0x10043E00
srTriMeshPipeline* srTriMeshPipeline::Get(srGERD* renderer)
{
    srTriMeshPipeline* pipeline;

    if (renderer == 0) {
        return 0;
    }

    if (pipe == 0) {
        pipe = new srTriMeshPipeline();
    }

    pipeline = pipe;
    pipeline->Reset(renderer);
    return pipe;
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
        current_record->color_source.format =
            srVertexPipe::Record::ColorSource::FORMAT_VECTOR3;
        current_record->flags |= srVertexPipe::Record::HAS_COLORS;
    }
    if (mesh.dcg[pass] != 0) {
        current_record->spec_for_diffuse = mesh.dcg[pass];
        current_record->flags |=
            srVertexPipe::Record::HAS_DIFFUSE_MULTIPLIERS;
    }
    if (mesh.scg[pass] != 0) {
        current_record->spec_for_specular = mesh.scg[pass];
        current_record->flags |=
            srVertexPipe::Record::HAS_SPECULAR_MULTIPLIERS;
    }

    if (mesh.vertex_materials[pass][material_side] == 0) {
        material = mesh.materials[pass][material_side].get();
        current_record->material = material;
    } else {
        current_record->materials =
            mesh.vertex_materials[pass][material_side];
        current_record->flags |=
            srVertexPipe::Record::HAS_VERTEX_MATERIALS;
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
            current_pass->texture_tables[layer] =
                mesh.poly_textures[pass][layer];
        }
    }

    ++slot_count;
    PrepareSlot();
}

// FUNCTION: SURRENDER 0x1003CA80
void srMeshModel::renderTriMesh(srGERD& renderer, const TriMesh& mesh)
{
    if (mesh.polygon_count != 0 && mesh.vertex_count != 0) {
        renderer.pushEnable();

        if ((mesh.control_flags & (1UL << srMeshModel::CONTROL_SORTED_RENDERING)) != 0 &&
            !renderer.isEnabled(srGERD::ENABLE_SORTED_RENDERING)) {
            renderer.toggle(srGERD::ENABLE_SORTED_RENDERING);
        }

        w8_long material_side;
        for (w8_long side = 1; side >= 0; --side) {
            if ((mesh.control_flags & (1u << side)) != 0) {
                srTriMeshPipeline* pipeline = srTriMeshPipeline::Get(&renderer);
                pipeline->records.resize(static_cast<std::size_t>(mesh.pass_count) + 1);
                pipeline->passes.resize(static_cast<std::size_t>(mesh.pass_count) + 1);
                pipeline->PrepareSlot();
                pipeline->sort_bias = mesh.sort_bias;
                pipeline->triangles = mesh.poly_vertices;
                pipeline->triangle_count = static_cast<w8_ulong>(mesh.polygon_count);
                pipeline->positions = mesh.positions;
                pipeline->vertex_count = static_cast<w8_ulong>(mesh.vertex_count);
                pipeline->vertex_extras = mesh.normals;
                pipeline->projected_vertices = mesh.poly_equations;

                if (mesh.active_polygons) {
                    pipeline->active_triangles = mesh.active_polygons;
                }

                pipeline->SetMeshBounds(mesh);

                material_side = side;
                if (side == 1) {
                    renderer.setCullMode(srGERD::CULL_FRONT);
                    if (!renderer.isEnabled(srGERD::ENABLE_REVERSE_NORMALS)) {
                        renderer.toggle(srGERD::ENABLE_REVERSE_NORMALS);
                    }
                    if ((mesh.control_flags & (1UL << srMeshModel::CONTROL_REUSE_FRONT_MATERIAL)) !=
                        0) {
                        material_side = 0;
                    }
                } else {
                    if ((mesh.control_flags & (1UL << srMeshModel::CONTROL_NO_FRONT_CULL)) == 0) {
                        renderer.setCullMode(srGERD::CULL_BACK);
                    } else {
                        renderer.setCullMode(srGERD::CULL_NONE);
                    }
                    if (renderer.isEnabled(srGERD::ENABLE_REVERSE_NORMALS)) {
                        renderer.toggle(srGERD::ENABLE_REVERSE_NORMALS);
                    }
                }

                for (w8_long pass = 0; pass < mesh.pass_count; ++pass) {
                    pipeline->AppendMeshPass(mesh, pass, material_side, mesh.shaders[pass]);
                }

                pipeline->FlushIfCurrent();
            }
        }

        renderer.popEnable();
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
    render_control.set(control, 0);
    setDirty(DIRTY_TRI_MESH);
}

// FUNCTION: SURRENDER 0x10041870
int srMeshModel::isEnabled(e_control control) const
{
    return (render_control.value & (1 << control)) != 0;
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

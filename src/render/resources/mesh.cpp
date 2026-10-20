#include "wiz8/render/mesh.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int3.hpp"
#include "glm/geometric.hpp"
#include "wiz8/platform/timer.h"
#include "wiz8/render/bounding_radius.h"
#include "wiz8/render/draw_inputs.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/material.h"
#include "wiz8/render/texture.h"
#include "wiz8/spatial/bounding_box.h"

/* D:\srsdk1x\sources\corelib\Mesh.cpp */

#include <limits>

#include "wiz8/math.h"
#include "wiz8/render/mesh_math.h"
#include "wiz8/render/mesh_resources.h"

// A validated permutation carries both directions for attribute-table reorder
// and references that point into those tables. Validate before mutating a mesh.
class MeshPermutation {
public:
    MeshPermutation(std::span<const std::uint32_t> order, std::int32_t count)
        : order(order), inverse(order.size())
    {
        if (order.size() != static_cast<std::size_t>(count))
            throw std::invalid_argument("Mesh permutation length mismatch");
        std::vector<bool> seen(order.size());
        for (std::size_t i = 0; i < order.size(); ++i) {
            if (order[i] >= order.size() || seen[order[i]])
                throw std::invalid_argument("Invalid mesh permutation");
            seen[order[i]] = true;
            inverse[order[i]] = static_cast<std::uint32_t>(i);
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
            triangle = meshMath::remapTriangle(triangle, inverse, retain_external);
    }

    std::span<const std::uint32_t> order;
    std::vector<std::uint32_t> inverse;
};

/* Bounds-checked pass/side slots; out-of-range indices are ignored. */
void Mesh::setShader(DrawShader shader, std::int32_t pass)
{
    if (pass >= 0 && pass < 4) {
        shaders[pass] = shader;
        setDirty(DIRTY_TRI_MESH);
    }
}

DrawShader Mesh::getShader(std::int32_t pass) const
{
    if (pass >= 0 && pass < 4) {
        return shaders[pass];
    }
    DrawShader shader;
    shader.value = 0x0100241b;
    return shader;
}

Material* Mesh::getMaterial(std::int32_t pass, e_side side) const
{
    if (pass >= 0 && pass < 4 && (int)side >= 0 && (int)side < 2) {
        return materials[pass][side].get();
    }
    return 0;
}

void Mesh::setMaterial(std::shared_ptr<Material> material, std::int32_t pass, e_side side)
{
    if (pass >= 0 && pass < 4 && (int)side >= 0 && (int)side < 2) {
        materials[pass][side] = std::move(material);
        setDirty(DIRTY_TRI_MESH);
    }
}

Texture* Mesh::getTexture(std::int32_t pass, std::int32_t layer) const
{
    if (pass >= 0 && pass < 4 && layer >= 0 && layer < 2) {
        return textures[pass][layer].get();
    }
    return 0;
}

void Mesh::setTexture(std::shared_ptr<Texture> texture, std::int32_t pass, std::int32_t layer)
{
    if (pass >= 0 && pass < 4 && layer >= 0 && layer < 2) {
        textures[pass][layer] = std::move(texture);
        setDirty(DIRTY_TRI_MESH);
    }
}

std::int32_t Mesh::getActivePolygonCount()
{
    return active_polygon_count;
}

void Mesh::setActivePolygonCount(std::int32_t count)
{
    active_polygon_count = count;
    // Visibility selects existing triangles; it never changes render topology.
    dirty_flags |= 1u << DIRTY_TRI_MESH;
}

void Mesh::setUVCount(std::int32_t count)
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

std::int32_t Mesh::getUVCount() const
{
    return uv_count;
}

/* Lazily grown table accessors: a table is supplied only when its governing count is nonzero; on
   first use it is resized and then cleared or seeded. */
glm::vec3* Mesh::getVertexLoc()
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

glm::vec3* Mesh::getVertexNormal()
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

glm::vec4* Mesh::getPolyEq()
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

std::uint32_t* Mesh::getActivePolygonTable(int table)
{
    if (active_polygons.empty() && table != 0 && polygon_count != 0) {
        active_polygons.resize(polygon_count);
        for (std::int32_t index = 0; index < polygon_count; ++index) {
            active_polygons[index] = index;
        }
        dirty_flags |= 1u << DIRTY_TRI_MESH;
    }
    return active_polygons.empty() ? nullptr : active_polygons.data();
}

glm::ivec3* Mesh::getPolyVertex()
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
static T* GetLazyMeshTable(Mesh& mesh, std::vector<T>& slot, std::int32_t count, int create,
                           const T& initial = T{})
{
    if (slot.empty() && create != 0 && count != 0) {
        slot.resize(count, initial);
        mesh.setDirty(Mesh::DIRTY_TRI_MESH);
    }
    return slot.empty() ? nullptr : slot.data();
}

MeshResources<Material>* Mesh::getVertexMaterialTable(std::int32_t pass, e_side side, bool create)
{
    if (pass < 0 || pass > 3 || (int)side < 0 || (int)side > 1) {
        return 0;
    }
    auto& assignments = vertex_materials[pass][side];
    if (create && assignments.empty() && vertex_location_count) {
        assignments.resize(vertex_location_count);
        setDirty(DIRTY_TRI_MESH);
    }
    return assignments.empty() ? nullptr : &assignments;
}

MeshResources<Texture>* Mesh::getPolygonTextureTable(std::int32_t pass, std::int32_t layer,
                                                     bool create)
{
    if (pass < 0 || pass > 3 || layer < 0 || layer > 1) {
        return 0;
    }
    auto& assignments = poly_textures[pass][layer];
    if (create && assignments.empty() && polygon_count) {
        assignments.resize(polygon_count);
        setDirty(DIRTY_TRI_MESH);
    }
    return assignments.empty() ? nullptr : &assignments;
}

DrawShader* Mesh::getPolyShader(std::int32_t polygon, int layer)
{
    if (polygon < 0 || polygon > 3) {
        return 0;
    }
    return GetLazyMeshTable(*this, poly_shaders[polygon], polygon_count, layer);
}

glm::ivec3* Mesh::getPolyUVIndex(std::int32_t layer, int table)
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

glm::vec3* Mesh::getVertexDIG(std::int32_t vertex, int table)
{
    if (vertex < 0 || vertex > 3) {
        return 0;
    }
    return GetLazyMeshTable(*this, dig[vertex], vertex_location_count, table,
                            glm::vec3(0.0f, 0.0f, 0.0f));
}

glm::vec2* Mesh::getVertexTexCoords(std::int32_t vertex, std::int32_t layer, int table)
{
    if (vertex < 0 || vertex > 3 || layer < 0 || layer > 1) {
        return 0;
    }
    return GetLazyMeshTable(*this, texcoords[vertex][layer], uv_count, table, glm::vec2(0.0f));
}

std::uint32_t* Mesh::getVertexShadeIndex(int table)
{
    if (vertex_shade_indices.empty() && table != 0 && vertex_location_count != 0) {
        vertex_shade_indices.resize(vertex_location_count, 0);
        setDirty(DIRTY_VERTEX_NORMALS);
    }
    return vertex_shade_indices.empty() ? nullptr : vertex_shade_indices.data();
}

Mesh::Mesh(std::int32_t polygons, std::int32_t vertices)
{
    reset(polygons, vertices);
    vertex_lighting_ready = false;
    flags = W8_MESH_VERTEX_LIGHTING_DIRTY;
    ambient_color = glm::vec3(-1);
    vertex_light_table = 0;
    vertex_compression_scale = 1.0f;
    last_decompress_release_tick = MonotonicMilliseconds();
    render_control &= ~(1u << CONTROL_SKIP_AUTO_BOX);
    automap_filter_active = false;
}

void Mesh::reset(std::int32_t polygons, std::int32_t vertices)
{
    /* Reset really discards table storage and its references; pass-wide material/texture/shader
       state and sort bias survive. Default destruction needs no model reset or client callbacks.
       This includes the former freeAll path at SURRENDER 0x1003DEA0. */
    tri_mesh = TriMesh{};
    active_polygons = std::vector<std::uint32_t>{};
    poly_vertices = std::vector<glm::ivec3>{};
    poly_equations = std::vector<glm::vec4>{};
    vertex_locations = std::vector<glm::vec3>{};
    vertex_normals = std::vector<glm::vec3>{};
    vertex_shade_indices = std::vector<std::uint32_t>{};
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

void Mesh::calculateBounds()
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

void Mesh::calculatePolygonNormals()
{
    dirty_flags &= ~(1UL << DIRTY_POLYGON_NORMALS);
    glm::vec4* equations = getPolyEq();
    glm::ivec3* polygons = getPolyVertex();
    glm::vec3* vertices = getVertexLoc();
    for (std::int32_t polygon = 0; polygon < polygon_count; polygon++) {
        const auto first = vertices[polygons[polygon].x];
        const auto normal = meshMath::polygonNormal(
            std::span<const glm::vec3>(vertices, vertex_location_count), polygons[polygon]);
        equations[polygon] = glm::vec4(normal, -glm::dot(normal, first));
    }
}

void Mesh::calculateVertexNormals()
{
    if (polygon_count == 0 || vertex_location_count == 0) return;
    if (dirty_flags & (1UL << DIRTY_POLYGON_NORMALS)) calculatePolygonNormals();
    dirty_flags &= ~(1UL << DIRTY_VERTEX_NORMALS);
    const auto* equations = getPolyEq();
    const auto* polygons = getPolyVertex();
    const auto* shade_indices = getVertexShadeIndex(0);
    const auto smooth = meshMath::vertexNormalSums(
        std::span<const glm::ivec3>(polygons, polygon_count),
        std::span<const glm::vec4>(equations, polygon_count), vertex_location_count,
        shade_indices ? std::span<const std::uint32_t>(shade_indices, vertex_location_count)
                      : std::span<const std::uint32_t>{});
    auto* normals = getVertexNormal();
    for (std::size_t vertex = 0; vertex < static_cast<std::size_t>(vertex_location_count);
         ++vertex) {
        normals[vertex] = normalizedOrZero(smooth[vertex]);
    }
}

void Mesh::scale(const glm::vec3& scale)
{
    glm::vec3* vertices = getVertexLoc();
    for (std::int32_t index = 0; index < vertex_location_count; index++) {
        vertices[index] *= scale;
    }
    setDirtyAll();
}

void Mesh::relocateVertices(const glm::vec3& offset)
{
    glm::vec3* vertices = getVertexLoc();
    for (std::int32_t index = 0; index < vertex_location_count; index++) {
        vertices[index] += offset;
    }
    setDirtyAll();
}

double Mesh::getAverageRadius()
{
    if (vertex_location_count == 0) {
        return 0.0;
    }
    double radius = 0;
    for (const auto& vertex : std::span(getVertexLoc(), vertex_location_count))
        radius += glm::length(vertex);
    return radius / vertex_location_count;
}

double Mesh::getMaxRadius()
{
    return boundingRadius(std::span(getVertexLoc(), vertex_location_count), glm::vec3(0));
}

void Mesh::scaleToAverageRadius(double radius)
{
    const double current_radius = getAverageRadius();
    if (current_radius > 0) scale(glm::vec3(static_cast<float>(radius / current_radius)));
}

void Mesh::scaleToMaxRadius(double radius)
{
    const double current_radius = getMaxRadius();
    if (current_radius > 0) scale(glm::vec3(static_cast<float>(radius / current_radius)));
}

std::int32_t Mesh::findClosestVertex(const glm::vec3& position)
{
    const auto target = position;
    float minimum = std::numeric_limits<float>::infinity();
    std::int32_t closest = 0;
    const auto* vertices = getVertexLoc();
    for (std::int32_t i = 0; i < vertex_location_count; ++i) {
        const auto offset = vertices[i] - target;
        const float distance = glm::dot(offset, offset);
        if (distance < minimum) {
            minimum = distance;
            closest = i;
        }
    }
    return closest;
}

Mesh::TriMesh Mesh::getTriMesh()
{
    if ((flags & W8_MESH_VERTEX_LIGHTING_DIRTY) != 0) {
        updateBakedLighting();
        flags &= ~static_cast<unsigned int>(W8_MESH_VERTEX_LIGHTING_DIRTY);
    }
    if ((dirty_flags & (1UL << DIRTY_TRI_MESH)) != 0) {
        updateTriMesh();
    }
    return tri_mesh;
}

void Mesh::updateTriMesh()
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
    for (std::int32_t pass = 0; pass < 4; pass++) {
        for (std::int32_t side = 0; side < 2; side++) {
            tri_mesh.materials[pass][side] =
                std::static_pointer_cast<Material>(materials[pass][side]);
            tri_mesh.vertex_materials[pass][side] =
                getVertexMaterialTable(pass, static_cast<e_side>(side), false);
        }
        for (std::int32_t layer = 0; layer < 2; layer++) {
            tri_mesh.textures[pass][layer] = textures[pass][layer];
            tri_mesh.poly_textures[pass][layer] = getPolygonTextureTable(pass, layer, false);
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
        tri_mesh.active_polygons = std::span<const std::uint32_t>(active, active_polygon_count);
    }
    dirty_flags &= ~(1UL << DIRTY_TRI_MESH);
}

void Mesh::setBounds(const glm::vec3& minimum, const glm::vec3& maximum, const glm::vec3& center,
                     float radius)
{
    bounds_minimum = minimum;
    bounds_maximum = maximum;
    bounds_center = center;
    bounds_radius = radius;
    clearDirty(DIRTY_BOUNDS);
    setDirty(DIRTY_TRI_MESH);
}

static glm::vec4* GetVertexColorTable(Mesh& mesh,
                                      std::array<std::vector<glm::vec4>, Mesh::MAX_PASSES>& colors,
                                      std::int32_t pass, int create)
{
    if (pass < 0 || pass >= Mesh::MAX_PASSES) {
        return nullptr;
    }
    return GetLazyMeshTable(mesh, colors[pass], mesh.vertex_location_count, create, glm::vec4(1));
}

glm::vec4* Mesh::getVertexSCG(std::int32_t vertex, int table)
{
    return GetVertexColorTable(*this, scg, vertex, table);
}

glm::vec4* Mesh::getVertexDCG(std::int32_t vertex, int table)
{
    return GetVertexColorTable(*this, dcg, vertex, table);
}

bool Mesh::isVisible(const DrawTransform& transform)
{
    const bool test_sphere = (render_control & (1UL << CONTROL_SKIP_AUTO_SPHERE)) == 0;
    const bool test_box =
        (render_control & (1UL << CONTROL_SKIP_AUTO_BOX)) == 0 && vertex_location_count >= 8;
    if (!test_sphere && !test_box) return true;
    const auto bounds = getBounds();
    if (test_sphere && !transform.sphereVisible(bounds.sphere.center, bounds.sphere.radius))
        return false;
    if (test_box && !transform.boxVisible(bounds.box.minimum, bounds.box.maximum)) return false;
    return true;
}

void Mesh::reindexPolygons(std::span<const std::uint32_t> indices)
{
    const MeshPermutation permutation(indices, polygon_count);
    if (!polygon_count) return;
    permutation.apply(getPolyEq());
    permutation.apply(getPolyVertex());
    for (std::int32_t pass = 0; pass < pass_count; ++pass) {
        permutation.apply(getPolyUVIndex(pass, 0));
        permutation.apply(getPolyShader(pass, 0));
        for (std::int32_t layer = 0; layer < 2; ++layer)
            if (auto* table = getPolygonTextureTable(pass, layer, false))
                permutation.apply(table->indices.data());
    }
    if (active_polygon_count && getActivePolygonTable(0)) {
        auto* table = getActivePolygonTable(1);
        for (std::int32_t i = 0; i < active_polygon_count; ++i)
            table[i] = permutation.inverse[table[i]];
    }
    setDirtyAll();
}

void Mesh::reindexVertices(std::span<const std::uint32_t> indices)
{
    const MeshPermutation permutation(indices, vertex_location_count);
    if (!vertex_location_count) return;
    permutation.apply(getVertexNormal());
    permutation.apply(getVertexLoc());
    if (auto* shade = getVertexShadeIndex(0)) {
        permutation.apply(shade);
        for (std::int32_t i = 0; i < vertex_location_count; ++i)
            shade[i] = permutation.inverse[shade[i]];
        for (std::int32_t i = 0; i < vertex_location_count; ++i) {
            const auto value = shade[i];
            if (value < static_cast<std::uint32_t>(i)) {
                shade[i] = shade[value];
                shade[value] = value;
            }
        }
    }
    for (std::int32_t pass = 0; pass < pass_count; ++pass) {
        for (std::int32_t side = 0; side < 2; ++side)
            if (auto* table = getVertexMaterialTable(pass, static_cast<e_side>(side), false))
                permutation.apply(table->indices.data());
        for (std::int32_t table = 0; table < 2; ++table)
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

float Mesh::getSortBias() const
{
    return sort_bias;
}

void Mesh::disable(e_control control)
{
    render_control &= ~(1u << control);
    setDirty(DIRTY_TRI_MESH);
}

void Mesh::setPassCount(std::int32_t count)
{
    pass_count = std::clamp<std::int32_t>(count, 1, MAX_PASSES);
    setDirty(DIRTY_TRI_MESH);
}

std::int32_t Mesh::getPassCount() const
{
    return pass_count;
}

std::int32_t Mesh::getPolygonCount() const
{
    return polygon_count;
}

std::int32_t Mesh::getVertexCount() const
{
    return vertex_location_count;
}

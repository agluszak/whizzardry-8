#include "wiz8/render/mesh.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int3.hpp"
#include "glm/geometric.hpp"
#include "wiz8/render/bounding_radius.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/material.h"
#include "wiz8/render/texture.h"
#include "wiz8/spatial/bounding_box.h"

/* D:\srsdk1x\sources\corelib\Mesh.cpp */

#include "wiz8/math.h"
#include "wiz8/render/mesh_math.h"

/* Bounds-checked pass/side slots; out-of-range indices are ignored. */
void Mesh::setShader(DrawShader shader, std::int32_t pass)
{
    if (pass >= 0 && pass < 4) {
        shaders[pass] = shader;
        invalidateRenderAsset();
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
        invalidateRenderAsset();
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
        invalidateRenderAsset();
    }
}

std::int32_t Mesh::getActivePolygonCount()
{
    return active_polygon_count;
}

void Mesh::setActivePolygonCount(std::int32_t count)
{
    // Visibility selects existing triangles; it never changes render topology.
    active_polygon_count = count;
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
        invalidateRenderAsset();
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
        invalidateRenderAsset();
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
        invalidateRenderAsset();
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
        invalidateRenderAsset();
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
        invalidateRenderAsset();
    }
    return poly_vertices.data();
}

template<class T>
static T* GetLazyMeshTable(Mesh& mesh, std::vector<T>& slot, std::int32_t count, int create,
                           const T& initial = T{})
{
    if (slot.empty() && create != 0 && count != 0) {
        slot.resize(count, initial);
        mesh.invalidateRenderAsset();
    }
    return slot.empty() ? nullptr : slot.data();
}

std::vector<std::shared_ptr<Material>>* Mesh::getVertexMaterialTable(std::int32_t pass, e_side side,
                                                                     bool create)
{
    if (pass < 0 || pass > 3 || (int)side < 0 || (int)side > 1) {
        return 0;
    }
    auto& assignments = vertex_materials[pass][side];
    if (create && assignments.empty() && vertex_location_count) {
        assignments.resize(vertex_location_count);
        invalidateRenderAsset();
    }
    return assignments.empty() ? nullptr : &assignments;
}

std::vector<std::shared_ptr<Texture>>* Mesh::getPolygonTextureTable(std::int32_t pass,
                                                                    std::int32_t layer, bool create)
{
    if (pass < 0 || pass > 3 || layer < 0 || layer > 1) {
        return 0;
    }
    auto& assignments = poly_textures[pass][layer];
    if (create && assignments.empty() && polygon_count) {
        assignments.resize(polygon_count);
        invalidateRenderAsset();
    }
    return assignments.empty() ? nullptr : &assignments;
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
        invalidateRenderAsset();
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
    vertex_compression_scale = 1.0f;
    render_control &= ~(1u << CONTROL_SKIP_AUTO_BOX);
}

void Mesh::reset(std::int32_t polygons, std::int32_t vertices)
{
    /* Reset really discards table storage and its references; pass-wide material/texture/shader
       state survives. Default destruction needs no model reset or client callbacks.
       This includes the former freeAll path at SURRENDER 0x1003DEA0. */
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

void Mesh::applyPendingLighting()
{
    if ((flags & W8_MESH_VERTEX_LIGHTING_DIRTY) != 0) {
        updateBakedLighting();
        flags &= ~static_cast<unsigned int>(W8_MESH_VERTEX_LIGHTING_DIRTY);
    }
}

void Mesh::setBounds(const glm::vec3& minimum, const glm::vec3& maximum, const glm::vec3& center,
                     float radius)
{
    bounds_minimum = minimum;
    bounds_maximum = maximum;
    bounds_center = center;
    bounds_radius = radius;
    clearDirty(DIRTY_BOUNDS);
    invalidateRenderAsset();
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

std::int32_t Mesh::getPolygonCount() const
{
    return polygon_count;
}

std::int32_t Mesh::getVertexCount() const
{
    return vertex_location_count;
}

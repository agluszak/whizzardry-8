#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int3.hpp"
#include "wiz8/math.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/material.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/texture.h"

static_assert(std::is_same_v<decltype(Mesh::poly_vertices), std::vector<glm::ivec3>>);
static_assert(std::is_same_v<decltype(Mesh::vertex_locations), std::vector<glm::vec3>>);

static bool zeroNormals()
{
    std::array<glm::vec3, 2> normals;
    normals[0] = glm::vec3(0, 0, 0);
    normals[1] = glm::vec3(0, 0, 2);
    for (auto& normal : normals) normal = normalizedOrZero(normal);
    REQUIRE(normals[0] == glm::vec3(0, 0, 0));
    REQUIRE(normals[1] == glm::vec3(0, 0, 1));
    return true;
}

static bool lazyTables()
{
    Mesh empty;
    REQUIRE((empty.getPolyVertex() == nullptr && empty.getVertexLoc() == nullptr));
    REQUIRE((empty.getVertexNormal() == nullptr && empty.getPolyEq() == nullptr));
    REQUIRE((empty.getActivePolygonTable(1) == nullptr && empty.getVertexShadeIndex(1) == nullptr));
    REQUIRE((empty.getPolyShader(0, 1) == nullptr && empty.getPolyUVIndex(0, 1) == nullptr));
    REQUIRE((empty.getVertexTexCoords(0, 0, 1) == nullptr && empty.getVertexDIG(0, 1) == nullptr));
    REQUIRE((empty.getVertexDCG(0, 1) == nullptr && empty.getVertexSCG(0, 1) == nullptr));
    REQUIRE(empty.getPolygonTextureTable(0, 0, true) == nullptr);
    REQUIRE(empty.getVertexMaterialTable(0, Mesh::SIDE_FRONT, true) == nullptr);

    Mesh mesh(2, 3);
    REQUIRE((mesh.poly_vertices.empty() && mesh.vertex_locations.empty() &&
             mesh.vertex_normals.empty()));
    REQUIRE((mesh.poly_equations.empty() && mesh.active_polygons.empty()));
    for (int pass = 0; pass < Mesh::MAX_PASSES; ++pass) {
        REQUIRE(
            (mesh.getPolyShader(pass, 0) == nullptr && mesh.getPolyUVIndex(pass, 0) == nullptr));
        REQUIRE((mesh.getVertexDIG(pass, 0) == nullptr && mesh.getVertexDCG(pass, 0) == nullptr));
        REQUIRE(mesh.getVertexSCG(pass, 0) == nullptr);
        for (int side = 0; side < 2; ++side) {
            REQUIRE(mesh.getPolygonTextureTable(pass, side, false) == nullptr);
            REQUIRE(mesh.getVertexMaterialTable(pass, static_cast<Mesh::e_side>(side), false) ==
                    nullptr);
            REQUIRE(mesh.getVertexTexCoords(pass, side, 0) == nullptr);
        }
    }
    auto* locations = mesh.getVertexLoc();
    REQUIRE((locations == mesh.vertex_locations.data() && mesh.vertex_locations.size() == 3));
    REQUIRE(mesh.getVertexLoc() == locations);
    for (const auto& value : mesh.vertex_locations) REQUIRE(value == glm::vec3(0, 0, 0));
    for (const auto& value : mesh.vertex_normals) REQUIRE(value == glm::vec3(0, 0, 0));
    auto* corners = mesh.getPolyVertex();
    corners[0].x = 0;
    corners[0].y = 1;
    corners[0].z = 2;
    auto* uv_indices = mesh.getPolyUVIndex(2, 1);
    REQUIRE((uv_indices != corners && uv_indices[0].x == 0 && uv_indices[0].y == 1 &&
             uv_indices[0].z == 2));
    REQUIRE(mesh.getPolyUVIndex(0, 0) == nullptr);
    auto* active = mesh.getActivePolygonTable(1);
    REQUIRE((active[0] == 0 && active[1] == 1));
    mesh.setActivePolygonCount(1);
    auto* dig = mesh.getVertexDIG(0, 1);
    for (int index = 0; index < 3; ++index) {
        REQUIRE(dig[index] == glm::vec3(0, 0, 0));
        const auto& dcg = mesh.getVertexDCG(0, 1)[index];
        const auto& scg = mesh.getVertexSCG(0, 1)[index];
        REQUIRE((dcg.x == 1 && dcg.y == 1 && dcg.z == 1 && dcg.w == 1));
        REQUIRE((scg.x == 1 && scg.y == 1 && scg.z == 1 && scg.w == 1));
    }
    REQUIRE(mesh.getPolyShader(3, 1)[0].value == DrawShader().value);
    REQUIRE(mesh.getVertexShadeIndex(1)[0] == 0);
    REQUIRE((*mesh.getPolygonTextureTable(3, 1, true))[0].get() == nullptr);
    REQUIRE((*mesh.getVertexMaterialTable(3, Mesh::SIDE_BACK, true))[0].get() == nullptr);
    REQUIRE((mesh.getVertexDIG(-1, 1) == nullptr && mesh.getVertexDIG(4, 1) == nullptr));
    REQUIRE(mesh.getVertexTexCoords(0, 2, 1) == nullptr);
    REQUIRE((mesh.getPolygonTextureTable(4, 0, true) == nullptr &&
             mesh.getPolyShader(-1, 1) == nullptr));
    REQUIRE(mesh.getVertexMaterialTable(0, static_cast<Mesh::e_side>(2), true) == nullptr);
    return true;
}

static bool uvGrowth()
{
    Mesh mesh(1, 3);
    auto* uv = mesh.getVertexTexCoords(1, 1, 1);
    uv[0] = glm::vec2(0.25f, 0.75f);
    mesh.setUVCount(12);
    uv = mesh.getVertexTexCoords(1, 1, 0);
    REQUIRE((mesh.texcoords[1][1].size() == 12 && uv[0].x == 0.25f && uv[0].y == 0.75f));
    for (int index = 3; index < 12; ++index) REQUIRE((uv[index].x == 0 && uv[index].y == 0));
    REQUIRE(mesh.getVertexTexCoords(0, 0, 0) == nullptr);
    mesh.setUVCount(1);
    REQUIRE((mesh.getUVCount() == 3 && mesh.texcoords[1][1].size() == 3));
    mesh.setUVCount(6);
    uv = mesh.getVertexTexCoords(1, 1, 0);
    for (int index = 3; index < 6; ++index) REQUIRE((uv[index].x == 0 && uv[index].y == 0));

    // A cleared table may still have allocation capacity; it is nevertheless absent/null.
    Mesh no_vertices;
    no_vertices.setUVCount(8);
    REQUIRE(no_vertices.getVertexTexCoords(0, 0, 1) != nullptr);
    no_vertices.setUVCount(0);
    REQUIRE(no_vertices.getVertexTexCoords(0, 0, 0) == nullptr);
    no_vertices.setUVCount(4);
    REQUIRE(no_vertices.getVertexTexCoords(0, 0, 0) == nullptr);
    REQUIRE(no_vertices.getVertexTexCoords(0, 0, 1)[0].x == 0);
    return true;
}

static bool reset()
{
    auto material = std::make_shared<Material>();
    auto texture = std::make_shared<Texture>();
    {
        Mesh mesh(1, 3);
        mesh.getVertexLoc()[1] = glm::vec3(1, 2, 3);
        mesh.setUVCount(7);
        mesh.getVertexTexCoords(2, 0, 1)[6] = glm::vec2(0.5f, 0.75f);
        (*mesh.getVertexMaterialTable(1, Mesh::SIDE_BACK, true))[2] = material;
        (*mesh.getPolygonTextureTable(1, 0, true))[0] = texture;
        mesh.setMaterial(material, 0, Mesh::SIDE_FRONT);
        mesh.setTexture(texture, 0, 0);
        mesh.setSortBias(0.25f);
        mesh.setPassCount(3);
        REQUIRE(mesh.texcoords[2][0][6].x == 0.5f);
        mesh.reset(2, 4);
        REQUIRE(
            (mesh.getVertexCount() == 4 && mesh.getUVCount() == 4 && mesh.getPolygonCount() == 2));
        REQUIRE((mesh.getPassCount() == 1 && mesh.getActivePolygonCount() == 2));
        REQUIRE((mesh.getSortBias() == 0.25f &&
                 mesh.getMaterial(0, Mesh::SIDE_FRONT) == material.get()));
        REQUIRE(mesh.getTexture(0, 0) == texture.get());
        REQUIRE((mesh.vertex_locations.empty() && mesh.vertex_locations.capacity() == 0));
        REQUIRE((mesh.texcoords[2][0].empty() && mesh.texcoords[2][0].capacity() == 0));
        REQUIRE(mesh.getVertexMaterialTable(1, Mesh::SIDE_BACK, false) == nullptr);
        REQUIRE((mesh.getPolygonTextureTable(1, 0, false) == nullptr &&
                 mesh.getActivePolygonTable(0) == nullptr));
        REQUIRE(mesh.getVertexLoc()[1] == glm::vec3(0, 0, 0));
    }
    REQUIRE((material.use_count() == 1 && texture.use_count() == 1));
    return true;
}

TEST_CASE("mesh_ownership_test")
{
    REQUIRE(zeroNormals());
    REQUIRE(lazyTables());
    REQUIRE(uvGrowth());
    REQUIRE(reset());
}

TEST_CASE("Mesh resource assignments keep resources until their last use and survive reindexing")
{
    Mesh mesh(3, 4);
    auto material = std::make_shared<Material>();
    auto texture = std::make_shared<Texture>();
    auto* materials = mesh.getVertexMaterialTable(0, Mesh::SIDE_FRONT, true);
    auto* textures = mesh.getPolygonTextureTable(0, 0, true);
    std::ranges::fill(*materials, material);
    std::ranges::fill(*textures, texture);
    auto other = std::make_shared<Material>();
    (*materials)[2] = other;
    (*materials)[3] = nullptr;
    const std::array<std::uint32_t, 4> permutation{3, 2, 0, 1};
    mesh.reindexVertices(permutation);
    REQUIRE((*materials)[0] == nullptr);
    REQUIRE((*materials)[1] == other);
    REQUIRE((*materials)[2] == material);
    std::weak_ptr<Material> material_lifetime = other;
    other.reset();
    (*materials)[1] = material;
    REQUIRE(material_lifetime.expired()); // Replacing the last assignment releases ownership.
    (*textures)[1] = nullptr;
    const std::array<std::uint32_t, 3> polygons{1, 2, 0};
    mesh.reindexPolygons(polygons);
    REQUIRE((*textures)[0] == nullptr);
    REQUIRE((*textures)[1] == texture);
    REQUIRE((*textures)[2] == texture);
    // Shrinking releases a resource only once no remaining polygon uses it.
    std::weak_ptr<Texture> texture_lifetime = texture;
    texture.reset();
    textures->resize(2);
    REQUIRE_FALSE(texture_lifetime.expired());
    textures->resize(1);
    REQUIRE(texture_lifetime.expired());
}

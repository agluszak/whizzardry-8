#include <algorithm>
#include <array>
#include <memory>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <vsg/core/Array.h>

#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/render_mesh.h"
#include "wiz8/render/world_scene.h"

TEST_CASE("Animated instances share topology and keep independent CPU poses", "[renderer][pose]")
{
    Mesh mesh(2, 4);
    auto* triangles = mesh.getPolyVertex();
    triangles[0] = {0, 1, 2};
    triangles[1] = {0, 2, 3};
    mesh.setUVCount(5);
    auto* uv = mesh.getVertexTexCoords(0, 0, 1);
    uv[0] = {0, 0};
    uv[1] = {1, 0};
    uv[2] = {1, 1};
    uv[3] = {0, 1};
    uv[4] = {.5f, .5f};
    auto* corners = mesh.getPolyUVIndex(0, 1);
    corners[0] = {0, 1, 2};
    corners[1] = {4, 2, 3};
    mesh.InitializeVertexFrames(2);
    mesh.vertex_compression_scale = .5f;
    const std::array<short, 12> first{0, 0, 0, 2, 0, 0, 2, 2, 0, 0, 2, 0};
    const std::array<short, 12> second{4, 0, 0, 6, 0, 0, 6, 2, 0, 4, 2, 0};
    std::ranges::copy(first, mesh.GetVertex(0));
    std::ranges::copy(second, mesh.GetVertex(1));
    mesh.FinalizeVertexFrame(0);
    mesh.FinalizeVertexFrame(1);
    // Deliberately different normals prove interpolation affects positions only.
    for (int i = 0; i < 4; ++i) {
        mesh.frames[0].compressed_vertex_normals[i * 3] = 0;
        mesh.frames[0].compressed_vertex_normals[i * 3 + 1] = 0;
        mesh.frames[0].compressed_vertex_normals[i * 3 + 2] = 127;
        mesh.frames[1].compressed_vertex_normals[i * 3] = 0;
        mesh.frames[1].compressed_vertex_normals[i * 3 + 1] = 127;
        mesh.frames[1].compressed_vertex_normals[i * 3 + 2] = 0;
    }
    render::MeshPose lead(mesh), follower(mesh);
    REQUIRE(lead.asset == follower.asset);
    REQUIRE(lead.asset == mesh.renderAsset());
    REQUIRE_FALSE(lead.passes.empty());
    lead.update(mesh, 0, .25f, 1);
    follower.update(mesh, 0, .75f, 1);
    REQUIRE(lead.positions[0].x == Catch::Approx(.5f));
    REQUIRE(follower.positions[0].x == Catch::Approx(1.5f));
    REQUIRE(lead.normals[0] == glm::vec3(0, 0, 1));
    REQUIRE(follower.normals[0] == glm::vec3(0, 0, 1));
    REQUIRE(lead.passes[0].positions != follower.passes[0].positions);
    const auto& source = lead.asset->passes[0].source_vertices;
    REQUIRE(std::ranges::count(source, 0u) == 2);
    for (std::size_t i = 0; i < source.size(); ++i) {
        const auto expected = lead.positions[source[i]];
        REQUIRE((*lead.passes[0].positions)[i] == vsg::vec3(expected.x, expected.y, expected.z));
    }
    // A second view consumes the same arrays and does not resample the pose.
    const auto positions = lead.passes[0].positions;
    const auto original = lead.positions;
    lead.update(mesh, 0, .25f, 1);
    REQUIRE(lead.passes[0].positions == positions);
    REQUIRE(lead.positions == original);
    lead.update(mesh, 1, 0, 2);
    REQUIRE(lead.normals[0] == glm::vec3(0, 1, 0));
    REQUIRE(follower.positions[0].x == Catch::Approx(1.5f));
}

TEST_CASE("Scene membership retains entity ownership and ordinary VSG subgraphs",
          "[renderer][ownership]")
{
    MeshInstance instance;
    instance.transform.setLocation(4, 5, 6);
    auto ordinary = vsg::Group::create();
    {
        WorldScene first, second;
        first.root->addChild(ordinary);
        first.attach(instance);
        first.attach(instance);
        REQUIRE(first.dynamic_meshes.size() == 1);
        second.attach(instance);
        REQUIRE(first.dynamic_meshes.empty());
        REQUIRE(second.dynamic_meshes.size() == 1);
        REQUIRE(first.root->children.back() == ordinary);
        REQUIRE(instance.transform.getLocation() == glm::dvec3(4, 5, 6));
    }
    REQUIRE(instance.scene == nullptr);
    REQUIRE(instance.transform.getLocation() == glm::dvec3(4, 5, 6));
    REQUIRE(ordinary->children.empty());
}

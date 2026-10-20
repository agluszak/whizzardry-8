#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <vsg/core/Array.h>
#include <vsg/core/Data.h>
#include <vsg/maths/vec3.h>
#include <vsg/nodes/Group.h>
#include <vsg/utils/LineSegmentIntersector.h>

#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_double3.hpp"
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
    lead.update(mesh, 0, .25f);
    follower.update(mesh, 0, .75f);
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
    vsg::ModifiedCount modified;
    positions->getModifiedCount(modified);
    lead.update(mesh, 0, .25f);
    REQUIRE(lead.passes[0].positions == positions);
    REQUIRE(lead.positions == original);
    REQUIRE_FALSE(positions->differentModifiedCount(modified));
    auto hit =
        vsg::LineSegmentIntersector::create(vsg::dvec3(.75, .25, -1), vsg::dvec3(.75, .25, 1));
    lead.passes[0].draws[0]->accept(*hit);
    REQUIRE_FALSE(hit->intersections.empty());
    lead.update(mesh, 1, 0);
    auto moved =
        vsg::LineSegmentIntersector::create(vsg::dvec3(.75, .25, -1), vsg::dvec3(.75, .25, 1));
    lead.passes[0].draws[0]->accept(*moved);
    REQUIRE(moved->intersections.empty());
    REQUIRE(lead.normals[0] == glm::vec3(0, 1, 0));
    REQUIRE(follower.positions[0].x == Catch::Approx(1.5f));
}

TEST_CASE("Static instances share ordinary position and normal arrays", "[renderer][ownership]")
{
    Mesh mesh(1, 3);
    mesh.getPolyVertex()[0] = {0, 1, 2};
    auto* positions = mesh.getVertexLoc();
    positions[0] = {0, 0, 0};
    positions[1] = {1, 0, 0};
    positions[2] = {0, 1, 0};
    render::MeshPose first(mesh), second(mesh);
    first.update(mesh, 0, 0);
    second.update(mesh, 0, 0);
    REQUIRE_FALSE(first.passes.empty());
    REQUIRE(first.asset == second.asset);
    REQUIRE(first.passes[0].positions == second.passes[0].positions);
    REQUIRE(first.passes[0].normals == second.passes[0].normals);
    const auto asset = first.asset;
    mesh.getActivePolygonTable(1);
    mesh.setActivePolygonCount(0);
    first.update(mesh, 0, 0);
    REQUIRE(first.asset == asset);
    REQUIRE(first.passes[0].draws[0]->indexCount == 0);
    mesh.setActivePolygonCount(1);
    first.update(mesh, 0, 0);
    REQUIRE(first.asset == asset);
    REQUIRE(first.passes[0].draws[0]->indexCount == 3);
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

TEST_CASE("Attached instances inherit graph parent movement and detach in either order",
          "[renderer][ownership]")
{
    WorldScene scene;
    auto parent = std::make_unique<MeshInstance>(&scene);
    parent->transform.setLocation(4, 5, 6);
    MeshInstance child;
    child.transform.setLocation(1, 2, 3);
    child.attachTo(*parent);
    REQUIRE(glm::dvec3(child.worldMatrix()[3]) == glm::dvec3(5, 7, 9));
    parent->transform.setLocation(10, 20, 30);
    REQUIRE(glm::dvec3(child.worldMatrix()[3]) == glm::dvec3(11, 22, 33));
    parent.reset();
    child.setScene(nullptr);
    REQUIRE(scene.dynamic_meshes.empty());
}

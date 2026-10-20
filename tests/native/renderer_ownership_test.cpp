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

#include "glm/ext/matrix_double3x3.hpp"
#include "glm/ext/matrix_double4x4.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/render/light.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/render_mesh.h"
#include "wiz8/render/texture.h"
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

TEST_CASE("Replacing a damage texture refreshes blanking and the shared asset",
          "[renderer][materials]")
{
    auto mesh = std::make_shared<Mesh>(1, 3);
    mesh->getPolyVertex()[0] = {0, 1, 2};
    auto* positions = mesh->getVertexLoc();
    positions[0] = {0, 0, 0};
    positions[1] = {1, 0, 0};
    positions[2] = {0, 1, 0};
    auto body = std::make_shared<Texture>();
    body->source_path = "body.tga";
    body->setName("Body");
    auto* base = mesh->getPolygonTextureTable(0, 0, true);
    base->set(0, body);
    MeshInstance instance;
    instance.setModel(mesh);
    const int damage = instance.AddDamageStage("damaged");
    REQUIRE(damage == 0);
    const int skin = instance.damage_stage_tables[damage];
    render::MeshPose pose(*mesh);
    pose.update(*mesh, 0, 0, skin);
    REQUIRE(pose.passes[0].draws[0]->indexCount == 3);
    const auto original_asset = pose.asset;

    auto blank = std::make_shared<Texture>();
    blank->source_path = "blank-body.tga";
    blank->setName("blank-body");
    REQUIRE(instance.ReplaceDamageStageTexture(damage, "BODY", blank));
    pose.update(*mesh, 0, 0, skin);
    REQUIRE(pose.asset != original_asset);
    REQUIRE(pose.passes[0].draws[0]->indexCount == 0);
    REQUIRE(mesh->GetActivePolygons(skin, true).value().empty());

    REQUIRE(instance.ReplaceDamageStageTexture(damage, "BLANK-BODY", body));
    pose.update(*mesh, 0, 0, skin);
    REQUIRE(pose.passes[0].draws[0]->indexCount == 3);
    REQUIRE(pose.texture(*mesh, 0, 0, skin) == body);
    REQUIRE((*base)[0] == body);
}

TEST_CASE("Mapped vertices use the current interpolated pose before the first render",
          "[renderer][pose]")
{
    auto mesh = std::make_shared<Mesh>(1, 3);
    mesh->getPolyVertex()[0] = {0, 1, 2};
    mesh->InitializeVertexFrames(2);
    mesh->vertex_compression_scale = 1;
    const std::array<short, 9> first{0, 0, 0, 1, 0, 0, 0, 1, 0};
    const std::array<short, 9> second{4, 0, 0, 5, 0, 0, 4, 1, 0};
    std::ranges::copy(first, mesh->GetVertex(0));
    std::ranges::copy(second, mesh->GetVertex(1));
    mesh->FinalizeVertexFrame(0);
    mesh->FinalizeVertexFrame(1);
    MeshInstance instance;
    instance.setModel(mesh);
    instance.frame_index = 0;
    instance.frame_interpolation = .5f;
    REQUIRE(instance.sampledPosition(*mesh, 0) == glm::vec3(2, 0, 0));
    instance.frame_index = 1;
    instance.frame_interpolation = 0;
    REQUIRE(instance.sampledPosition(*mesh, 0) == glm::vec3(4, 0, 0));
}

TEST_CASE("Cycle lights preserve authored orientation and range under scaled actor visuals",
          "[renderer][ownership]")
{
    WorldScene scene;
    MeshInstance actor(&scene);
    actor.transform.setLocation(10, 20, 30);
    actor.transform.setRotation(.2, .7, -.1);
    Light inherited, cycle;
    inherited.transform.setLocation(2, 3, 4);
    inherited.transform.setRotation(.4, -.3, .6);
    inherited.transform.setScale(glm::dvec3(2, 1, 3));
    cycle.transform = inherited.transform;
    inherited.attachTo(actor);
    cycle.attachTo(actor, {}, Light::Attachment::CycleOrigin);
    const auto authored_basis = glm::dmat3(cycle.transform.matrix());
    for (const auto& scale : std::array{glm::dvec3(2, 3, 4), glm::dvec3(-2, 0, 4), glm::dvec3(0)}) {
        actor.transform.setScale(scale);
        const auto expected =
            actor.transform.position + actor.transform.rotation * cycle.transform.position;
        REQUIRE(glm::dvec3(cycle.worldMatrix()[3]) == expected);
        REQUIRE(glm::dmat3(cycle.worldMatrix()) == authored_basis);
        REQUIRE(inherited.worldMatrix() == actor.worldMatrix() * inherited.transform.matrix());
        REQUIRE(glm::dmat3(inherited.worldMatrix()) != authored_basis);
    }
    actor.transform.setLocation(-5, 8, 13);
    const auto moved =
        actor.transform.position + actor.transform.rotation * cycle.transform.position;
    REQUIRE(glm::dvec3(cycle.worldMatrix()[3]) == moved);
    // Changing policy on the same parent restores ordinary graph inheritance.
    cycle.attachTo(actor);
    REQUIRE(cycle.worldMatrix() == actor.worldMatrix() * cycle.transform.matrix());
}

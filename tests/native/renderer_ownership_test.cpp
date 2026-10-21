#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <vsg/app/Camera.h>
#include <vsg/app/View.h>
#include <vsg/app/ViewMatrix.h>
#include <vsg/commands/BindIndexBuffer.h>
#include <vsg/core/Array.h>
#include <vsg/core/ConstVisitor.h>
#include <vsg/core/Data.h>
#include <vsg/core/Object.h>
#include <vsg/core/Visitor.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/maths/mat4.h>
#include <vsg/maths/transform.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>
#include <vsg/nodes/CullNode.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/nodes/Node.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/Switch.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/BufferInfo.h>
#include <vsg/state/DescriptorBuffer.h>
#include <vsg/state/DescriptorSet.h>
#include <vsg/state/GraphicsPipeline.h>
#include <vsg/state/PipelineLayout.h>
#include <vsg/state/ViewDependentState.h>
#include <vsg/state/ViewportState.h>
#include <vsg/utils/LineSegmentIntersector.h>
#include <vulkan/vulkan_core.h>

#include "glm/ext/matrix_double3x3.hpp"
#include "glm/ext/matrix_double4x4.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/render/camera.h"
#include "wiz8/render/clip_plane.h"
#include "wiz8/render/draw_inputs.h"
#include "wiz8/render/light.h"
#include "wiz8/render/material.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/render/render_mesh.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/vsg_material.h"
#include "wiz8/render/vsg_scene.h"
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

TEST_CASE("Replacing a damage texture keeps the shared asset and refreshes blanking",
          "[renderer][materials]")
{
    auto mesh = std::make_shared<Mesh>(1, 3);
    mesh->getPolyVertex()[0] = {0, 1, 2};
    auto* positions = mesh->getVertexLoc();
    positions[0] = {0, 0, 0};
    positions[1] = {1, 0, 0};
    positions[2] = {0, 1, 0};
    const auto texture = [](const char* name) {
        auto result = std::make_shared<Texture>();
        result->source_path = std::string(name) + ".tga";
        result->setName(name);
        return result;
    };
    auto body = texture("Body");
    auto* base = mesh->getPolygonTextureTable(0, 0, true);
    (*base)[0] = body;
    MeshInstance instance;
    instance.setModel(mesh);
    const int damage = instance.AddDamageStage("damaged");
    REQUIRE(damage == 0);
    const int skin = instance.damage_stage_tables[damage];
    render::MeshPose pose(*mesh);
    pose.update(*mesh, 0, 0, skin);
    REQUIRE(pose.passes[0].draws[0]->indexCount == 3);
    const auto asset = pose.asset;

    auto scarred = texture("Scarred");
    REQUIRE(instance.ReplaceDamageStageTexture(damage, "BODY", scarred));
    pose.update(*mesh, 0, 0, skin);
    REQUIRE(pose.asset == asset);
    REQUIRE(pose.texture(*mesh, 0, 0, skin) == scarred);
    REQUIRE(pose.passes[0].draws[0]->indexCount == 3);

    REQUIRE(instance.ReplaceDamageStageTexture(damage, "SCARRED", texture("blank-body")));
    pose.update(*mesh, 0, 0, skin);
    REQUIRE(pose.asset == asset);
    REQUIRE(pose.passes[0].draws[0]->indexCount == 0);
    REQUIRE(mesh->GetActivePolygons(skin, true).value().empty());

    REQUIRE(instance.ReplaceDamageStageTexture(damage, "BLANK-BODY", body));
    pose.update(*mesh, 0, 0, skin);
    REQUIRE(pose.asset == asset);
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

TEST_CASE("Cached views retain compiled resources while camera and scene inputs change",
          "[renderer][ownership]")
{
    struct Resources : vsg::ConstVisitor {
        std::vector<const vsg::DescriptorSet*> descriptors;
        std::vector<const vsg::Data*> indices;
        void apply(const vsg::Object& object) override { object.traverse(*this); }
        void apply(const vsg::DescriptorSet& set) override { descriptors.push_back(&set); }
        void apply(const vsg::BindIndexBuffer& bind) override
        {
            indices.push_back(bind.indices->data.get());
        }
    };
    render::RenderContext context({640, 480});
    WorldScene scene;
    Camera3D camera;
    auto viewport = vsg::ViewportState::create(0, 0, 640, 480);
    auto geometry = vsg::VertexIndexDraw::create();
    geometry->assignIndices(vsg::uintArray::create(6));
    geometry->indexCount = 3;
    scene.root->addChild(render::DrawNode::create(geometry, vsg::vec3Array::create(3), true));
    const auto view = scene.createView(context, &camera, {}, viewport, 1);
    const auto view_camera = view->camera;
    const auto view_matrix = view_camera->viewMatrix;
    const auto original_matrix = view_matrix->transform();
    const auto state = view->viewDependentState;
    Resources first;
    state->accept(first);
    REQUIRE(first.descriptors.size() == 1);
    REQUIRE(first.indices.size() == 2);

    camera.transform.setLocation(3, 4, 5);
    const auto next = scene.createView(context, &camera, {}, viewport, 2);
    REQUIRE(next == view);
    REQUIRE(next->camera == view_camera);
    REQUIRE(next->camera->viewMatrix == view_matrix);
    REQUIRE(view_matrix->transform() != original_matrix);
    REQUIRE(next->viewDependentState == state);
    Resources unchanged;
    state->accept(unchanged);
    REQUIRE(unchanged.descriptors == first.descriptors);
    REQUIRE(unchanged.indices == first.indices);
    const auto simultaneous = scene.createView(context, &camera, {}, viewport, 2);
    REQUIRE(simultaneous != view);
    REQUIRE(simultaneous->viewDependentState != state);

    // A larger translucent stream grows its own buffer; ordinary authored-face
    // indices retain their compiled allocation. View descriptors grow only
    // when additional view records need more space.
    geometry->indexCount = 6;
    for (int index = 0; index < 8; ++index)
        scene.clip_planes.push_back(std::make_unique<ClipPlane>());
    REQUIRE(scene.createView(context, &camera, {}, viewport, 3) == view);
    REQUIRE(view->viewDependentState == state);
    Resources grown;
    state->accept(grown);
    REQUIRE(grown.descriptors != first.descriptors);
    REQUIRE(grown.indices[0] != first.indices[0]);
    REQUIRE(grown.indices[1] == first.indices[1]);
    geometry->indexCount = 3;
    scene.clip_planes.clear();
    REQUIRE(scene.createView(context, &camera, {}, viewport, 4) == view);
    Resources reduced;
    state->accept(reduced);
    REQUIRE(reduced.descriptors == grown.descriptors);
    REQUIRE(reduced.indices == grown.indices);

    const auto resized = vsg::ViewportState::create(0, 0, 800, 600);
    REQUIRE(scene.createView(context, &camera, {}, resized, 5) == view);
    REQUIRE(view->camera == view_camera);
    REQUIRE(view->camera->viewportState == resized);
    REQUIRE(view->viewDependentState == state);
}

TEST_CASE("Instance graph and material buffers survive live pose and appearance updates",
          "[renderer][ownership]")
{
    struct Draws : vsg::Visitor {
        std::vector<vsg::ref_ptr<render::DrawNode>> nodes;
        void apply(vsg::Object& object) override { object.traverse(*this); }
        void apply(vsg::StateGroup& group) override
        {
            if (auto* draw = dynamic_cast<render::DrawNode*>(&group)) nodes.emplace_back(draw);
            group.traverse(*this);
        }
    };
    const auto boundData = [](const render::DrawNode& node, std::uint32_t set) {
        for (const auto& command : node.stateCommands) {
            const auto* bind = command->cast<vsg::BindDescriptorSet>();
            if (!bind || bind->firstSet != set) continue;
            const auto* buffer =
                bind->descriptorSet->descriptors.front()->cast<vsg::DescriptorBuffer>();
            if (buffer) return buffer->bufferInfoList.front()->data.cast<vsg::vec4Array>();
        }
        return vsg::ref_ptr<vsg::vec4Array>{};
    };
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
    auto material = std::make_shared<Material>();
    material->uv_mapping = Material::UvMapping::Scroll;
    material->uv_velocity = {.25f, 0};
    mesh->setMaterial(material, 0, Mesh::SIDE_FRONT);
    WorldScene scene;
    MeshInstance instance(&scene);
    instance.setModel(mesh);
    render::RenderContext context({640, 480});
    const auto viewport = vsg::ViewportState::create(0, 0, 640, 480);
    scene.createView(context, &scene.camera, {}, viewport, 1);
    Draws original;
    scene.root->accept(original);
    REQUIRE(original.nodes.size() == 1);
    const auto leaf = original.nodes.front();
    const auto draw = leaf->draw;
    const auto positions = leaf->positions;
    const auto instance_data = boundData(*leaf, 1);
    const auto material_data = boundData(*leaf, 2);
    REQUIRE(instance_data);
    REQUIRE(material_data);
    const auto commands = leaf->stateCommands;

    instance.frame_index = 1;
    material->setEmissive({.3f, .5f, .9f, 1});
    scene.ambient_light = {.4f, .2f, .1f};
    scene.createView(context, &scene.camera, {.time = 2}, viewport, 2);
    Draws updated;
    scene.root->accept(updated);
    REQUIRE(updated.nodes.size() == 1);
    REQUIRE(updated.nodes.front() == leaf);
    REQUIRE(leaf->draw == draw);
    REQUIRE(leaf->positions == positions);
    REQUIRE(positions->at(0).x == Catch::Approx(4));
    REQUIRE(leaf->stateCommands == commands);
    REQUIRE(boundData(*leaf, 1) == instance_data);
    REQUIRE(boundData(*leaf, 2) == material_data);
    REQUIRE(instance_data->at(0) == vsg::vec4(.4f, .2f, .1f, 1));
    REQUIRE(material_data->at(4) == vsg::vec4(.3f, .5f, .9f, 1));
    REQUIRE(material_data->at(6).y == Catch::Approx(.5f));
}

TEST_CASE("Attached subtrees follow their parent between scenes and through detachment",
          "[renderer][ownership]")
{
    WorldScene first, second;
    MeshInstance parent, child, grandchild;
    Light light;
    parent.transform.setLocation(10, 20, 30);
    child.transform.setLocation(1, 2, 3);
    grandchild.transform.setLocation(4, 5, 6);
    child.attachTo(parent);
    grandchild.attachTo(child, 0);
    light.attachTo(child);
    child.visible = false;
    parent.setScene(&first);
    REQUIRE(first.dynamic_meshes.size() == 3);
    REQUIRE(first.lights.size() == 1);
    REQUIRE(child.scene == &first);
    REQUIRE(grandchild.scene == &first);
    REQUIRE(light.scene == &first);
    const auto inherited = grandchild.worldMatrix();

    parent.setScene(&second);
    REQUIRE(first.dynamic_meshes.empty());
    REQUIRE(first.lights.empty());
    REQUIRE(second.dynamic_meshes.size() == 3);
    REQUIRE(second.lights.size() == 1);
    REQUIRE(child.scene == &second);
    REQUIRE(grandchild.scene == &second);
    REQUIRE(light.scene == &second);
    REQUIRE(grandchild.worldMatrix() == inherited);
    parent.setScene(&second);
    REQUIRE(second.dynamic_meshes.size() == 3);
    REQUIRE(second.lights.size() == 1);

    parent.setScene(nullptr);
    REQUIRE(second.dynamic_meshes.empty());
    REQUIRE(second.lights.empty());
    REQUIRE(child.scene == nullptr);
    REQUIRE(grandchild.scene == nullptr);
    REQUIRE(light.scene == nullptr);
    REQUIRE(grandchild.worldMatrix() == inherited);
    parent.setScene(&first);
    REQUIRE(first.dynamic_meshes.size() == 3);
    REQUIRE(first.lights.size() == 1);
}

TEST_CASE("Static parents adopt attached visuals and lights without becoming dynamic",
          "[renderer][ownership]")
{
    MeshInstance child;
    Light light;
    auto scene = std::make_unique<WorldScene>();
    auto parent = std::make_unique<MeshInstance>();
    child.attachTo(*parent);
    light.attachTo(*parent);
    const auto* attached_parent = scene->addStaticMesh(std::move(parent));
    REQUIRE(attached_parent->scene == scene.get());
    REQUIRE(scene->dynamic_meshes.size() == 1);
    REQUIRE(scene->dynamic_meshes.front() == &child);
    REQUIRE(scene->lights.size() == 1);
    REQUIRE(child.scene == scene.get());
    REQUIRE(light.scene == scene.get());
    scene.reset();
    REQUIRE(child.scene == nullptr);
    REQUIRE(light.scene == nullptr);
}

TEST_CASE("Directly adopting a worldless attached light removes its previous collector",
          "[renderer][ownership]")
{
    WorldScene scene;
    MeshInstance parent;
    Light light;
    light.attachTo(parent, 0);
    const auto previous = light.render_attachment;
    REQUIRE(parent.renderNode()->children.size() == 2);
    scene.attach(light);
    REQUIRE(parent.renderNode()->children.size() == 1);
    REQUIRE(light.render_attachment == nullptr);
    REQUIRE(light.render_node != nullptr);
    REQUIRE(light.scene == &scene);
    REQUIRE(scene.lights.size() == 1);
    REQUIRE(previous->children.size() == 1);
}

TEST_CASE("Orthographic picking tests triangle faces against parallel view rays",
          "[renderer][picking]")
{
    render::RenderContext context({640, 480});
    WorldScene scene;
    auto mesh = std::make_shared<Mesh>(1, 3);
    mesh->getPolyVertex()[0] = {0, 1, 2};
    auto* positions = mesh->getVertexLoc();
    positions[0] = {9, -1, 4};
    positions[1] = {10, 1, 5};
    positions[2] = {11, -1, 6};
    MeshInstance instance(&scene);
    instance.setModel(mesh);
    scene.camera.setClipRange(.1, 20);
    scene.camera.setProjectionType(Camera3D::PROJECT_ORTHOGRAPHIC);
    scene.camera.setViewPlane({-20, -10, 20, 10}, .1);
    const auto view =
        scene.createView(context, &scene.camera, {}, vsg::ViewportState::create(0, 0, 640, 480), 1);
    Pick pick{.position = {.5f, 0, 1}};
    scene.pick(*view->camera, std::span{&pick, 1});
    REQUIRE(render::gameOwner(pick) == &instance);
    REQUIRE(pick.position.z < 1);
}

TEST_CASE("Ordinary VSG picking follows transforms and masks without Wizardry clip state",
          "[renderer][picking]")
{
    render::RenderContext context({640, 480});
    WorldScene scene;
    auto positions = vsg::vec3Array::create(3);
    positions->at(0) = {-1, -1, 0};
    positions->at(1) = {0, 1, 0};
    positions->at(2) = {1, -1, 0};
    auto indices = vsg::uintArray::create(3);
    indices->at(0) = 0;
    indices->at(1) = 1;
    indices->at(2) = 2;
    auto draw = vsg::VertexIndexDraw::create();
    draw->assignArrays({positions});
    draw->assignIndices(indices);
    draw->indexCount = 3;
    draw->instanceCount = 1;
    auto transform = vsg::MatrixTransform::create(vsg::translate(0., 0., 5.));
    transform->addChild(draw);
    auto mask = vsg::Switch::create();
    mask->addChild(render::render_mask | render::pick_mask, transform);
    scene.root->addChild(mask);
    auto plane = std::make_unique<ClipPlane>();
    plane->setClipPlane({0, 0, -1, 1});
    scene.clip_planes.push_back(std::move(plane));
    const auto view =
        scene.createView(context, &scene.camera, {}, vsg::ViewportState::create(0, 0, 640, 480), 1);
    Pick visible;
    scene.pick(*view->camera, std::span{&visible, 1});
    REQUIRE(visible.position.z < 1);
    REQUIRE(render::gameOwner(visible) == nullptr);
    REQUIRE(visible.hit);
    REQUIRE(visible.hit->nodePath.back() == draw.get());
    REQUIRE(std::ranges::find(visible.hit->nodePath, transform.get()) !=
            visible.hit->nodePath.end());
    REQUIRE(visible.hit->indexRatios.size() == 3);
    REQUIRE(visible.hit->worldIntersection.z == Catch::Approx(5));

    mask->children[0].mask = render::render_mask;
    Pick unpickable;
    scene.pick(*view->camera, std::span{&unpickable, 1});
    REQUIRE(unpickable.position.z == 1);
    REQUIRE_FALSE(unpickable.hit);
    mask->children[0].mask = render::render_mask | render::pick_mask;
    transform->matrix = vsg::translate(0., 0., .05);
    Pick before_near;
    scene.pick(*view->camera, std::span{&before_near, 1});
    REQUIRE(before_near.position.z == 1);
}

TEST_CASE("A scene refreshes scoped material bindings when its render context is replaced",
          "[renderer][ownership]")
{
    struct Bindings : vsg::ConstVisitor {
        const vsg::PipelineLayout* expected;
        const vsg::Node* current_particle;
        unsigned particle_roots = 0;
        unsigned views = 0;
        unsigned pipelines = 0;
        Bindings(const vsg::PipelineLayout* expected, const vsg::Node* current_particle)
            : expected(expected), current_particle(current_particle)
        {
        }
        void apply(const vsg::Object& object) override { object.traverse(*this); }
        void apply(const vsg::Node& node) override
        {
            if (&node == current_particle) ++particle_roots;
            node.traverse(*this);
        }
        void apply(const vsg::BindViewDescriptorSets& bind) override
        {
            REQUIRE(bind.layout.get() == expected);
            ++views;
        }
        void apply(const vsg::BindGraphicsPipeline& bind) override
        {
            REQUIRE(bind.pipeline->layout.get() == expected);
            ++pipelines;
        }
    };
    WorldScene scene;
    MeshInstance instance(&scene);
    instance.setModel(std::make_shared<Mesh>(1, 3));
    instance.getModel()->getPolyVertex()[0] = {0, 1, 2};
    auto* positions = instance.getModel()->getVertexLoc();
    positions[0] = {-1, -1, 5};
    positions[1] = {0, 1, 5};
    positions[2] = {1, -1, 5};
    ParticleSystem particle(&scene, 1);
    particle.particle_active[0] = true;
    particle.active_particle_count = 1;
    particle.particle_positions[0] = {0, 0, 5};
    auto original = std::make_unique<render::RenderContext>(VkExtent2D{640, 480});
    const auto viewport = vsg::ViewportState::create(0, 0, 640, 480);
    scene.createView(*original, &scene.camera, {}, viewport, 1);
    const auto previous_particle = particle.renderNode();
    original.reset();
    render::RenderContext replacement({640, 480});
    // An external owner may request the new graphics state before the scene.
    REQUIRE(particle.renderNode(replacement) == previous_particle);
    // A new renderer restarts its frame counter, too.
    const auto view = scene.createView(replacement, &scene.camera, {}, viewport, 1);
    REQUIRE(particle.renderNode() == previous_particle);
    Bindings bindings(replacement.layout.get(), particle.renderNode().get());
    view->accept(bindings);
    REQUIRE(bindings.views == 2);
    REQUIRE(bindings.pipelines == 3);
    REQUIRE(bindings.particle_roots == 1);
}

TEST_CASE("VSG cull bounds enclose sky geometry and animated highlight shells",
          "[renderer][culling]")
{
    struct Bounds : vsg::ConstVisitor {
        const vsg::CullNode* current = nullptr;
        std::vector<const vsg::CullNode*> nodes;
        std::size_t draws = 0;
        void apply(const vsg::Object& object) override { object.traverse(*this); }
        void apply(const vsg::CullNode& node) override
        {
            const auto* previous = current;
            current = &node;
            nodes.push_back(&node);
            node.traverse(*this);
            current = previous;
        }
        void apply(const vsg::StateGroup& group) override
        {
            if (const auto* draw = dynamic_cast<const render::DrawNode*>(&group)) {
                REQUIRE(current);
                ++draws;
                const auto radius = current->bound.radius + .0001;
                for (const auto& position : *draw->positions) {
                    const auto offset = vsg::dvec3(position) - current->bound.center;
                    REQUIRE(vsg::dot(offset, offset) <= radius * radius);
                }
            }
            group.traverse(*this);
        }
    };
    auto mesh = std::make_shared<Mesh>(1, 3);
    mesh->getPolyVertex()[0] = {0, 1, 2};
    mesh->InitializeVertexFrames(2);
    mesh->vertex_compression_scale = 1;
    const std::array<short, 9> first{100, 0, 0, 102, 0, 0, 100, 2, 0};
    const std::array<short, 9> second{104, 0, 0, 106, 0, 0, 104, 2, 0};
    std::ranges::copy(first, mesh->GetVertex(0));
    std::ranges::copy(second, mesh->GetVertex(1));
    mesh->FinalizeVertexFrame(0);
    mesh->FinalizeVertexFrame(1);
    mesh->setTexture(std::make_shared<Texture>(), 0, 0);
    // Sky meshes mark their bounds as authored. Their VSG nodes
    // still need to enclose the real geometry, including its later poses.
    mesh->enable(Mesh::CONTROL_SKIP_AUTO_BOX);
    mesh->enable(Mesh::CONTROL_SKIP_AUTO_SPHERE);
    MeshInstance instance;
    instance.setModel(mesh);
    render::RenderContext context({640, 480});
    instance.renderNode()->prepare(context, {}, 1);
    Bounds body;
    instance.renderNode()->accept(body);
    REQUIRE(body.draws == 1);
    REQUIRE(body.nodes.size() == 1);
    const auto* bound = body.nodes.front();
    const auto body_radius = bound->bound.radius;

    instance.highlight_colour = {1, 0, 0, 1};
    instance.renderNode()->prepare(context, {}, 2);
    Bounds shell;
    instance.renderNode()->accept(shell);
    REQUIRE(shell.draws == 2);
    REQUIRE(shell.nodes == body.nodes);
    REQUIRE(bound->bound.radius > body_radius);
    const auto shell_radius = bound->bound.radius;

    instance.frame_index = 1;
    instance.transform.setScale(.5);
    instance.renderNode()->prepare(context, {}, 3);
    Bounds moved;
    instance.renderNode()->accept(moved);
    REQUIRE(moved.draws == 2);
    REQUIRE(moved.nodes == body.nodes);
    REQUIRE(bound->bound.radius > shell_radius);

    instance.highlight_colour = {};
    instance.renderNode()->prepare(context, {}, 4);
    Bounds unhighlighted;
    instance.renderNode()->accept(unhighlighted);
    REQUIRE(unhighlighted.draws == 1);
    REQUIRE(unhighlighted.nodes == body.nodes);
    REQUIRE(bound->bound.radius == body_radius);
}

TEST_CASE("Mesh bound changes reach the shared asset and its cull node in place",
          "[renderer][culling]")
{
    struct Culls : vsg::ConstVisitor {
        std::vector<const vsg::CullNode*> nodes;
        void apply(const vsg::Object& object) override { object.traverse(*this); }
        void apply(const vsg::CullNode& node) override
        {
            nodes.push_back(&node);
            node.traverse(*this);
        }
    };
    auto mesh = std::make_shared<Mesh>(1, 3);
    mesh->getPolyVertex()[0] = {0, 1, 2};
    auto* positions = mesh->getVertexLoc();
    positions[1] = {1, 0, 0};
    positions[2] = {0, 1, 0};
    MeshInstance instance;
    instance.setModel(mesh);
    render::RenderContext context({640, 480});
    instance.renderNode()->prepare(context, {}, 1);
    const auto asset = mesh->renderAsset();

    mesh->setBounds({10, 10, 10}, {12, 12, 12}, {11, 11, 11}, 2);
    instance.renderNode()->prepare(context, {}, 2);
    REQUIRE(mesh->renderAsset() == asset);
    Culls culls;
    instance.renderNode()->accept(culls);
    REQUIRE(culls.nodes.size() == 1);
    REQUIRE(culls.nodes.front()->bound.center == vsg::dvec3(11, 11, 11));
    REQUIRE(culls.nodes.front()->bound.radius == 2);
}

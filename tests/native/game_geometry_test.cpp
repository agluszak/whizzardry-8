#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <SDL3/SDL_surface.h>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <vsg/core/Array.h>
#include <vsg/core/ConstVisitor.h>
#include <vsg/core/Object.h>
#include <vsg/nodes/Node.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/DescriptorBuffer.h>
#include <vsg/state/GraphicsPipeline.h>
#include <vsg/state/PipelineLayout.h>
#include <vulkan/vulkan_core.h>

#include "geometry.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int2.hpp"
#include "glm/ext/vector_int3.hpp"
#include "glm/fwd.hpp"
#include "glm/geometric.hpp"
#include "scoped_asset_roots.h"
#include "scoped_game.h"
#include "temporary_directory.h"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/audio/SoundEvent.h"
#include "wiz8/audio/sound3d.h"
#include "wiz8/camera_motion.h"
#include "wiz8/canvas/himage.h"
#include "wiz8/design_rect.h"
#include "wiz8/direction_angles.h"
#include "wiz8/entities/animation.h"
#include "wiz8/entities/animation_mesh.h"
#include "wiz8/entities/animation_types.h"
#include "wiz8/entities/cursor_nodes.h"
#include "wiz8/entities/cycle_player.h"
#include "wiz8/entities/keyframe_track.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/filesystem.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/ItemManager.h"
#include "wiz8/octree_walk.h"
#include "wiz8/placement_math.h"
#include "wiz8/platform/GameTimeAccumulator.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/render/camera.h"
#include "wiz8/render/clip_plane.h"
#include "wiz8/render/compiled_mesh.h"
#include "wiz8/render/draw_inputs.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/ground_shadow.h"
#include "wiz8/render/light.h"
#include "wiz8/render/material.h"
#include "wiz8/render/material_loader.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/mesh_loader.h"
#include "wiz8/render/mesh_updates.h"
#include "wiz8/render/particle_modes.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/render/plane_math.h"
#include "wiz8/render/segment_math.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/triangle_projection.h"
#include "wiz8/render/vsg_material.h"
#include "wiz8/render/vsg_scene.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/spatial/bounding_box.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/mesh_links.h"
#include "wiz8/spatial/navigation.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/spatial/octree_geometry.h"
#include "wiz8/spatial/surface_index.h"
#include "wiz8/surface_geometry.h"
#include "wiz8/texture_animation.h"
#include "wiz8/world/camera_paths.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/environment_state.h"
#include "wiz8/world/level_loader.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/party_motion.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

static bool close(const glm::vec3& actual, const glm::vec3& expected, float tolerance = 1e-4f)
{
    return glm::length(actual - expected) < tolerance;
}

static void requireEffectLayout(const vsg::Node& root, const vsg::PipelineLayout* expected)
{
    struct Check : vsg::ConstVisitor {
        explicit Check(const vsg::PipelineLayout* expected) : expected(expected) {}
        const vsg::PipelineLayout* expected;
        unsigned pipelines = 0;
        unsigned descriptors = 0;
        void apply(const vsg::Object& object) override { object.traverse(*this); }
        void apply(const vsg::StateGroup& group) override
        {
            for (const auto& command : group.stateCommands) {
                if (const auto* bind = command->cast<vsg::BindGraphicsPipeline>()) {
                    REQUIRE(bind->pipeline->layout.get() == expected);
                    ++pipelines;
                } else if (const auto* bind = command->cast<vsg::BindDescriptorSet>()) {
                    REQUIRE(bind->layout.get() == expected);
                    ++descriptors;
                }
            }
            group.traverse(*this);
        }
    } check(expected);
    root.accept(check);
    REQUIRE(check.pipelines > 0);
    REQUIRE(check.descriptors >= 3);
}

static void bounds()
{
    const auto empty = boundingBox(std::span<const glm::vec3>{});
    REQUIRE((empty.minimum == glm::vec3(0) && empty.maximum == glm::vec3(0)));
    const std::array<glm::vec3, 3> points{{{-4, 2, -3}, {-1, 8, -7}, {-2, 5, -1}}};
    const auto box = boundingBox(std::span<const glm::vec3>(points));
    REQUIRE((box.minimum == glm::vec3(-4, 2, -7)));
    REQUIRE((box.maximum == glm::vec3(-1, 8, -1)));
    REQUIRE((box.center() == glm::vec3(-2.5f, 5, -4)));
    REQUIRE((box.contains({-4, 2, -7}) && !box.contains({0, 5, -4})));
    REQUIRE((box.overlaps({{-1, 8, -1}, {2, 9, 1}})));
    REQUIRE((!box.overlaps({{0, 2, -7}, {1, 8, -1}})));
    REQUIRE((box.intersectsSphere({-2, 5, -4}, 0)));
    REQUIRE((box.intersectsSphere({-2, 5, 0}, 1))); // Tangent above the maximum Z face.
    REQUIRE((!box.intersectsSphere({-2, 5, 0}, 0.9f)));
    REQUIRE((!box.intersectsSphere({0, 5, 0}, 1))); // Close to a face, outside the corner.
    REQUIRE((!box.intersectsSphere({-2, 5, -4}, -1)));
    REQUIRE((!box.intersectsSphere(glm::vec3(std::numeric_limits<float>::quiet_NaN()), 1)));
    const auto matrix = glm::translate(glm::mat4(1), glm::vec3(10, 20, 30)) *
                        glm::eulerAngleY(glm::half_pi<float>());
    const auto rotated = box.transformed(matrix);
    REQUIRE(close(rotated.minimum, {3, 22, 31}));
    REQUIRE(close(rotated.maximum, {9, 28, 34}));
    // The animation transform scales the translated coordinates, including
    // negative scale. It therefore differs from translating a scaled box.
    glm::mat3 quarter_turn(0);
    quarter_turn[0] = glm::vec3(0, 1, 0);
    quarter_turn[1] = glm::vec3(-1, 0, 0);
    quarter_turn[2] = glm::vec3(0, 0, 1);
    const glm::vec3 translation(10, 20, 30), scale(-2, 3, .5f);
    glm::vec3 minimum(-1, -2, -3), maximum(4, 5, 6);
    TransformBounds(&quarter_turn, &translation, &scale, &minimum, &maximum);
    REQUIRE(minimum == glm::vec3(-24, 57, 13.5f));
    REQUIRE(maximum == glm::vec3(-10, 72, 18));
    minimum = glm::vec3(0, 0, 0);
    maximum = glm::vec3(0, 0, 0);
    TransformBounds(&quarter_turn, &translation, &scale, &minimum, &maximum);
    REQUIRE((minimum == glm::vec3(-20, 60, 15) && maximum == minimum));
}

static void planes_and_segments()
{
    const glm::vec3 a(0, 2, 0), b(0, 2, 3), c(4, 2, 0);
    const auto ground = probeGround(a);
    REQUIRE((ground.position == a && !ground.contact));
    const auto surface = groundSurfaceInfo(a);
    REQUIRE(surface.height == 2);
    REQUIRE((surface.surface == 0 && surface.material == 0));
    W8Plane plane{};
    plane = trianglePlane(a, b, c);
    REQUIRE(close(plane.normal, {0, 1, 0}));
    REQUIRE(std::abs(SignedPlaneDistance(plane, {5, 4, 7}) - 2) < 1e-5f);
    plane = trianglePlane(a, a, a);
    REQUIRE((plane.normal == glm::vec3(0) && plane.w == 0));

    const glm::vec3 start(0, 0, 0), end(2, 0, 2);
    const glm::vec3 crossing_a(0, 0, 2), crossing_b(2, 0, 0);
    REQUIRE(SegmentCrossesEdge(start, end, crossing_a, crossing_b, 1));
    REQUIRE(SegmentCrossesEdge(end, start, crossing_b, crossing_a, 1));
    const glm::vec3 parallel_a(0, 0, 1), parallel_b(1, 0, 2);
    REQUIRE(!SegmentCrossesEdge(start, end, parallel_a, parallel_b, 1));
    const glm::vec3 on_line(1, 0, 1), beyond(3, 0, 3), away(4, 0, 4);
    REQUIRE(SegmentCrossesEdge(start, end, on_line, beyond, 1));
    REQUIRE(!SegmentCrossesEdge(start, end, beyond, away, 1));
    REQUIRE(SegmentCrossesEdge(on_line, on_line, start, end, 1));
    REQUIRE(!SegmentCrossesEdge(parallel_a, parallel_a, start, end, 1));

    // The spatial and planar callers share endpoint clamping but measure
    // different dimensions. Exercise before, within and beyond the segment.
    const glm::vec3 segment_from(0), segment_to(4, 0, 0);
    const glm::vec2 planar_from(0), planar_to(4, 0);
    for (float x : {-2.f, 2.f, 6.f}) {
        const glm::vec3 point(x, 3, 4);
        const glm::vec2 planar(x, 3);
        const auto projection = segmentMath::project(point, segment_from, segment_to);
        const auto planar_projection = segmentMath::project(planar, planar_from, planar_to);
        REQUIRE(std::abs(projection.distance - (x == 2 ? 5.f : std::sqrt(29.f))) < 1e-5f);
        REQUIRE(std::abs(planar_projection.distance - (x == 2 ? 3.f : std::sqrt(13.f))) < 1e-5f);
        const float expected_fraction = x < 0 ? 0 : x > 4 ? 1 : .5f;
        REQUIRE(projection.fraction == expected_fraction);
        REQUIRE(planar_projection.fraction == expected_fraction);
        REQUIRE(projection.point == glm::vec3(expected_fraction * 4, 0, 0));
        REQUIRE(planar_projection.point == glm::vec2(expected_fraction * 4, 0));
    }
    const auto degenerate = segmentMath::project(glm::vec3(0, 3, 4), segment_from, segment_from);
    const auto degenerate_planar = segmentMath::project(glm::vec2(0, 3), planar_from, planar_from);
    REQUIRE(degenerate.distance == 5);
    REQUIRE((degenerate.point == segment_from && degenerate.fraction == 0));
    REQUIRE(degenerate_planar.distance == 3);
    REQUIRE((degenerate_planar.point == planar_from && degenerate_planar.fraction == 0));

    LevelEnvironment environment;
    environment.scale = 2;
    environment.airborne = 0;
    environment.SetScaledMotion(glm::vec3(4, 6, 8));
    REQUIRE((environment.vector == glm::vec3(2, 3, 4) && environment.airborne == 1));

    glm::vec3 vector(3, 4, 5), target(0, 2, 0), zero(0);
    vector = ProjectVectorOntoVector(vector, target);
    REQUIRE(vector == glm::vec3(0, 4, 0));
    vector = ProjectVectorOntoVector(vector, zero);
    REQUIRE(vector == glm::vec3(0, 4, 0));
}

static void camera_and_throw()
{
    const auto up = directionAngles({0, 1, 0});
    REQUIRE((up.yaw == 0 && std::abs(up.pitch + glm::half_pi<float>()) < 1e-6f));
    const auto zero = directionAngles({0, 0, 0});
    REQUIRE((zero.yaw == 0 && zero.pitch == 0));
    glm::vec3 position(1, 2, 3);
    OffsetPositionByYawPitch(10, &position, glm::half_pi<float>(), 0);
    REQUIRE(close(position, {11, 2, 3}));
    OffsetPositionByYawPitch(5, &position, 0, glm::half_pi<float>());
    REQUIRE(close(position, {11, -3, 3}));

    const PartyCamera previous_camera = partyCamera();
    auto& camera = partyCamera();
    camera.m_position = glm::vec3(3, 4, 5);
    camera.m_yaw = .25f;
    camera.m_pitch = -.5f;
    World world{};
    world.party_camera.SetOrientation(.25f, -.5f);
    const auto empty_location = cameraLocationOrZero(world);
    const auto empty_state = cameraState(world);
    Camera3D node;
    node.transform.setLocation(1.25, -2.5, 3.75);
    world.camera = &node;
    const auto live_location = cameraLocation();
    const auto node_location = cameraLocation(world);
    const auto state = cameraState(world);
    const auto rotation = cameraRotation(world);
    partyCamera() = previous_camera;
    REQUIRE(empty_location == glm::vec3(0));
    REQUIRE(empty_state.position == glm::vec3(0, 0, 0));
    REQUIRE(live_location == glm::vec3(3, 4, 5));
    REQUIRE(node_location == glm::vec3(1.25f, -2.5f, 3.75f));
    REQUIRE(state.position == node_location);
    REQUIRE((state.orientation.yaw == .25f && state.orientation.pitch == -.5f));
    REQUIRE(state.retail.yaw == wiz8::retail::CameraPoseOpaque{}.yaw);
    REQUIRE(state.retail.pitch == wiz8::retail::CameraPoseOpaque{}.pitch);
    REQUIRE(rotation == glm::mat3(1));
    camera.SetOrientation(glm::half_pi<float>(), 0);
    glm::vec3 forward;
    camera.GetForwardPoint(10, &forward);
    REQUIRE(close(forward - camera.m_position, {10, 0, 0}));
    glm::mat3 vertical(glm::eulerAngleX(glm::half_pi<float>()));
    camera.ApplyRotationMatrix(&vertical, nullptr);
    REQUIRE((std::isfinite(camera.m_yaw) && std::isfinite(camera.m_pitch)));
    camera.SetOrientation(glm::two_pi<float>() - 0.1f, 0);
    REQUIRE(camera.BeginOrientationTransition(0, glm::two_pi<float>() + 0.1f, true));
    REQUIRE((std::abs(camera.m_yaw_distance - 0.2f) < 1e-5f && camera.m_yaw_velocity > 0));
    camera.Update(camera.m_transition_duration + 1);
    REQUIRE((!camera.m_transition_active && !camera.m_forced_transition));
    REQUIRE(std::abs(camera.m_yaw - 0.1f) < 1e-5f);
    REQUIRE(!camera.BeginOrientationTransition(0, camera.m_yaw, true));
    camera.Update(0.01f);
    REQUIRE((std::isfinite(camera.m_yaw) && std::isfinite(camera.m_pitch)));
    REQUIRE(camera.BeginOrientationTransition(-0.2f, glm::two_pi<float>() - 0.1f, true));
    REQUIRE((camera.m_yaw_velocity < 0 && camera.m_pitch_velocity < 0));

    const auto center = ItemThrowDirection({0.5f, 0.5f}, glm::mat3(1));
    const auto left = ItemThrowDirection({0, 0.5f}, glm::mat3(1));
    const auto right = ItemThrowDirection({1, 0.5f}, glm::mat3(1));
    const auto top = ItemThrowDirection({0.5f, 0}, glm::mat3(1));
    const auto bottom = ItemThrowDirection({0.5f, 1}, glm::mat3(1));
    REQUIRE(std::abs(glm::length(center) - 2500) < 0.01f);
    REQUIRE((std::abs(center.x) < 1e-4f && center.y < 0 && center.z > 0));
    REQUIRE((left.x < 0 && right.x > 0));
    REQUIRE((std::abs(left.y - center.y) < 1e-3f && std::abs(right.y - center.y) < 1e-3f));
    REQUIRE((top.y > center.y && bottom.y < center.y));
    REQUIRE(
        close(ItemThrowDirection({0.5f, 0.5f}, glm::mat3(glm::eulerAngleY(glm::half_pi<float>()))),
              {center.z, center.y, -center.x}, 0.01f));
}

static void linked_mesh_bounds()
{
    Mesh mesh(0, 2);
    auto* vertices = mesh.getVertexLoc();
    vertices[0] = glm::vec3(2, 3, 4);
    vertices[1] = glm::vec3(4, 5, 6);
    auto* linked = new Mesh(0, 2);
    linked->getVertexLoc()[0] = glm::vec3(6, 7, 8);
    linked->getVertexLoc()[1] = glm::vec3(8, 9, 10);
    mesh.AddPart(std::unique_ptr<Mesh>(linked));
    mesh.AddPart(std::make_unique<Mesh>(0, 0));
    const auto linked_bounds = mesh.getBounds();
    REQUIRE(linked_bounds.box.minimum == glm::vec3(2, 3, 4));
    REQUIRE(linked_bounds.box.maximum == glm::vec3(8, 9, 10));
    REQUIRE(linked_bounds.sphere.center == glm::vec3(5, 6, 7));
    for (auto* member : meshParts(&mesh)) {
        REQUIRE(member->bounds_minimum == glm::vec3(2, 3, 4));
        REQUIRE(member->bounds_maximum == glm::vec3(8, 9, 10));
        REQUIRE(member->bounds_center == glm::vec3(5, 6, 7));
        REQUIRE(std::abs(member->bounds_radius - std::sqrt(27.0f)) < 1e-5f);
    }
    // Replacing all positions with negative values must not expand the box to the origin.
    for (auto* member : meshParts(&mesh)) {
        for (auto& vertex : std::span(member->getVertexLoc(), member->vertex_location_count))
            vertex = -vertex;
        member->setDirtyAll();
    }
    mesh.calculateCompoundBounds();
    REQUIRE(mesh.bounds_minimum == glm::vec3(-8, -9, -10));
    REQUIRE(mesh.bounds_maximum == glm::vec3(-2, -3, -4));
}

static void baked_lighting()
{
    auto material = std::make_shared<Material>();
    material->parms.diffuse = glm::vec4(0.5f, 1, 0.25f, 1);
    auto model = std::make_shared<Mesh>(0, 2);
    auto& mesh = *model;
    mesh.getVertexLoc()[0] = glm::vec3(1, 0, 0);
    mesh.getVertexLoc()[1] = glm::vec3(2, 0, 0);
    const auto normal = glm::normalize(glm::vec3(1, 1, 0));
    mesh.getVertexNormal()[0] = normal;
    mesh.getVertexNormal()[1] = normal;
    mesh.setMaterial(material, 0, Mesh::SIDE_FRONT);
    MeshInstance instance(nullptr);
    instance.setModel(model);
    instance.transform.setLocation(10, 0, 0);
    instance.transform.setScale(glm::dvec3(2, 1, 1));
    instance.transform.setRotation(0, glm::half_pi<double>(), 0);
    Light light(nullptr);
    const std::array<Light*, 1> lights{&light};
    light.diffuse = glm::vec3(1, 0.5f, 1);
    light.transform.setLocation(10, 4, -2);
    light.setLinearAttenuation(10, 1.0f / 511);
    light.intensity = 1;
    REQUIRE(BakeInstanceVertexLighting(&instance, lights));
    const float first_contribution = 2 / std::sqrt(5.0f) * 0.6f;
    const float second_contribution = 0.6f * (1 - std::sqrt(20.0f) / 10);
    const auto colors = mesh.GetVertexLights(false);
    REQUIRE(close(colors[0], glm::vec3(0.5f, 0.5f, 0.25f) * first_contribution));
    REQUIRE(close(colors[1], glm::vec3(0.5f, 0.5f, 0.25f) * second_contribution));
    REQUIRE((mesh.vertex_lighting_ready && (mesh.flags & W8_MESH_VERTEX_LIGHTING_DIRTY)));

    mesh.setMaterial(nullptr, 0, Mesh::SIDE_FRONT);
    colors[0] = glm::vec3(0);
    colors[1] = glm::vec3(0);
    REQUIRE(BakeInstanceVertexLighting(&instance, lights));
    REQUIRE(close(colors[0], light.diffuse * first_contribution));
    REQUIRE(close(colors[1], light.diffuse * second_contribution));
    mesh.setMaterial(material, 0, Mesh::SIDE_FRONT);

    colors[0] = glm::vec3(0);
    colors[1] = glm::vec3(0);
    const auto world_normal = glm::normalize(glm::vec3(0, 2, -1));
    const auto position = glm::dvec3(glm::vec3(10, 0, -2) + world_normal * 0.5f);
    light.transform.setLocation(glm::dvec3(position));
    light.setLinearAttenuation(1, 1.0f / 511);
    REQUIRE(BakeInstanceVertexLighting(&instance, lights));
    REQUIRE(close(colors[0], glm::vec3(0.25f, 0.25f, 0.125f)));
    REQUIRE(colors[1] == glm::vec3(0, 0, 0));

    colors[0] = glm::vec3(0);
    light.transform.setLocation(10, 0, -2); // Coincident light and vertex remain finite.
    REQUIRE(BakeInstanceVertexLighting(&instance, lights));
    REQUIRE(colors[0] == glm::vec3(0, 0, 0));
    light.setName("Sun");
    light.transform.setLocation(glm::dvec3(position));
    REQUIRE(BakeInstanceVertexLighting(&instance, lights));
    REQUIRE(colors[0] == glm::vec3(0, 0, 0));
    instance.setModel(nullptr);
}

static void renderer_bounds()
{
    Mesh empty;
    const auto empty_bounds = empty.getBounds();
    REQUIRE(empty_bounds.box.minimum == glm::vec3(0));
    REQUIRE(empty_bounds.box.maximum == glm::vec3(0));
    REQUIRE(empty_bounds.sphere.center == glm::vec3(0));
    REQUIRE(empty_bounds.sphere.radius == 0);
}

TEST_CASE("World cursor meshes preserve face winding and UV seams", "[geometry]")
{
    const auto cursor = CreateWorldCursorCube();
    auto* instance = cursor->node.get();
    REQUIRE(instance != nullptr);
    auto* cube = instance->getModel();
    REQUIRE(cube != nullptr);
    REQUIRE(cube->getPolygonCount() == 12);
    REQUIRE(cube->getVertexCount() == 24);
    REQUIRE(cube->getMaterial(0, Mesh::SIDE_FRONT) != nullptr);
    REQUIRE(cube->getMaterial(0, Mesh::SIDE_BACK) == nullptr);
    REQUIRE(cube->getTexture(0, 0) != nullptr);
    const glm::vec3 center(0, 250, 0);
    for (const auto& face : cube->poly_vertices) {
        const auto a = cube->vertex_locations[face.x];
        const auto b = cube->vertex_locations[face.y];
        const auto c = cube->vertex_locations[face.z];
        REQUIRE(glm::dot(glm::cross(b - a, c - a), a - center) > 0);
    }
    for (int face = 0; face < 6; ++face) {
        const auto first = face * 4;
        REQUIRE(cube->texcoords[0][0][first] == glm::vec2(1, 1));
        REQUIRE(cube->texcoords[0][0][first + 1] == glm::vec2(0, 1));
        REQUIRE(cube->texcoords[0][0][first + 2] == glm::vec2(0, 0));
        REQUIRE(cube->texcoords[0][0][first + 3] == glm::vec2(1, 0));
    }
    const auto* normals = cube->getVertexNormal();
    for (int i = 0; i < 24; ++i)
        for (int j = 0; j < i; ++j)
            if (cube->vertex_locations[i] == cube->vertex_locations[j]) {
                REQUIRE(cube->vertex_shade_indices[i] == cube->vertex_shade_indices[j]);
                REQUIRE(normals[i] == normals[j]);
            }
}

TEST_CASE("Renderer bounds", "[geometry]")
{
    renderer_bounds();
}

TEST_CASE("Bounding volumes", "[geometry]")
{
    bounds();
}

TEST_CASE("Planes and segments", "[geometry]")
{
    planes_and_segments();
}

TEST_CASE("Cameras and throwing", "[geometry]")
{
    camera_and_throw();
}

TEST_CASE("Linked mesh bounds", "[geometry]")
{
    linked_mesh_bounds();
}

TEST_CASE("Model bounds are independent values", "[geometry]")
{
    auto mesh = std::make_shared<Mesh>(0, 2);
    mesh->getVertexLoc()[0] = glm::vec3(-4, -3, -2);
    mesh->getVertexLoc()[1] = glm::vec3(2, 3, 4);
    Mesh& model = *mesh;
    auto box = model.getBounds().box;
    const auto sphere = model.getBounds().sphere;
    REQUIRE(box.minimum == glm::vec3(-4, -3, -2));
    REQUIRE(box.maximum == glm::vec3(2, 3, 4));
    REQUIRE(sphere.center == glm::vec3(-1, 0, 1));
    REQUIRE(std::abs(sphere.radius - std::sqrt(27.0f) * 1.00001f) < 1e-5f);
    box.minimum = glm::vec3(99);
    REQUIRE(model.getBounds().box.minimum == glm::vec3(-4, -3, -2));

    mesh->getVertexLoc()[1] = glm::vec3(4, 5, 6);
    mesh->setDirtyAll();
    REQUIRE(model.getBounds().box.maximum == glm::vec3(4, 5, 6));
    Mesh linked(0, 1);
    linked.getVertexLoc()[0] = glm::vec3(-2, -4, -6);
    auto part = std::make_unique<Mesh>(0, 1);
    part->getVertexLoc()[0] = glm::vec3(2, 4, 6);
    linked.AddPart(std::move(part));
    Mesh& linked_model = linked;
    REQUIRE(linked_model.getBounds().box.minimum == glm::vec3(-2, -4, -6));
    REQUIRE(linked_model.getBounds().box.maximum == glm::vec3(2, 4, 6));
    REQUIRE(linked_model.getBounds().sphere.center == glm::vec3(0));
    REQUIRE(std::abs(linked_model.getBounds().sphere.radius - std::sqrt(56.0f)) < 1e-5f);
}

TEST_CASE("Baked lighting", "[geometry]")
{
    baked_lighting();
}

TEST_CASE("navigator route growth, copying and movement")
{
    const glm::vec3 origin(0, 0, 0), end(3, 0, 4);
    NavigatorRoute route(&origin, &end);
    REQUIRE((route.MeasurePathLength() == 5));
    route.InitializeSegment(&origin, &end);
    route.path_position_index = 1;
    for (unsigned short i = 1; i <= 35; ++i) {
        const glm::vec3 position(i, 0, 0);
        route.AppendPathPosition(&position, i);
    }
    REQUIRE((route.path_position_index == 36 && route.path.size() > 36));
    for (unsigned short i = 1; i <= 35; ++i) {
        REQUIRE((route.path[i].position == glm::vec3(i, 0, 0)));
        REQUIRE((route.path[i].value == i));
    }
    route.path[36] = {glm::vec3(36, 0, 0), 0};
    NavigatorRoute copied;
    copied.path[0].position = glm::vec3(-1, 0, 0);
    copied.CopyPathFrom(&route);
    REQUIRE((copied.path[0].position == glm::vec3(-1, 0, 0)));
    REQUIRE((copied.path_position_index == route.path_position_index));
    for (unsigned i = 1; i <= 36; ++i) {
        REQUIRE((copied.path[i].position == route.path[i].position));
        REQUIRE((copied.path[i].value == route.path[i].value));
    }
    route.path[1].position = glm::vec3(99, 0, 0);
    REQUIRE((copied.path[1].position == glm::vec3(1, 0, 0)));
    copied.InitializeSegment(&origin, &end);
    for (const auto& entry : copied.path) REQUIRE((entry.value == 0));
    REQUIRE((copied.MeasurePathLength() == 5));
    glm::vec3 current = origin, direction;
    REQUIRE((!copied.AdvancePositionWithDirection(&current, 2, &direction)));
    REQUIRE((close(current, {1.2f, 0, 1.6f})));
    REQUIRE((copied.AdvancePositionWithDirection(&current, 4, &direction)));
    REQUIRE((current == end));

    // An append may borrow a position from the same storage being grown.
    NavigatorRoute aliased(&origin, &end);
    aliased.path_position_index = 9;
    aliased.AppendPathPosition(&aliased.path[1].position, 7);
    REQUIRE((aliased.path[9].position == end && aliased.path[9].value == 7));
}

TEST_CASE("Route hop heights use supplied surfaces with independent waypoint indices",
          "[geometry][navigation][ownership]")
{
    std::array<W8PathSurface, 4> low_surfaces{}, high_surfaces{};
    for (auto& surface : low_surfaces) surface.flags = 1 << 12;
    for (auto& surface : high_surfaces) surface.flags = 2 << 12;
    NavigatorRoute route;
    route.path[1] = {{0, 0, 0}, 1};
    route.path[2] = {{1000, 0, 0}, 2};
    route.path_position_index = 2;
    route.path_cursor = 2;
    const glm::vec3 position{500, 0, 750};
    REQUIRE_FALSE(route.checkPositionHopHeight(low_surfaces, position));
    REQUIRE(route.checkPositionHopHeight(high_surfaces, position));
    REQUIRE_FALSE(route.checkPositionHopHeight(low_surfaces, position));
    REQUIRE_FALSE(route.checkPredictedHopHeight(low_surfaces, position));
    REQUIRE(route.checkPredictedHopHeight(high_surfaces, position));

    // Prediction still chooses the nearer following segment when available.
    route.path[3] = {{1000, 0, 1000}, 3};
    route.path_position_index = 3;
    const glm::vec3 following{1000, 0, 750};
    REQUIRE_FALSE(route.checkPositionHopHeight(low_surfaces, following));
    REQUIRE(route.checkPredictedHopHeight(low_surfaces, following));
}

TEST_CASE("Inclusive cell boxes retain order clipping and extreme endpoints", "[geometry][octree]")
{
    std::vector<glm::ivec3> cells;
    const auto append = [&](const glm::ivec3& cell) { cells.push_back(cell); };
    visitOctreeCellBox({-1, 2, 3}, {0, 3, 4}, append);
    const std::vector<glm::ivec3> expected{{-1, 2, 3}, {-1, 2, 4}, {-1, 3, 3}, {-1, 3, 4},
                                           {0, 2, 3},  {0, 2, 4},  {0, 3, 3},  {0, 3, 4}};
    REQUIRE(cells == expected);
    cells.clear();
    visitClippedOctreeCellBox({-1, 2, 3}, {0, 3, 4}, {1, 4, 5}, append);
    REQUIRE(cells == std::vector<glm::ivec3>(expected.begin() + 4, expected.end()));
    cells.clear();
    visitOctreeCellBox({1, 0, 0}, {0, 0, 0}, append);
    visitClippedOctreeCellBox({0, 0, 0}, {0, 0, 0}, {0, 0, 0}, append);
    REQUIRE(cells.empty());
    const glm::ivec3 extreme(std::numeric_limits<int>::max());
    visitOctreeCellBox(extreme, extreme, append);
    REQUIRE(cells == std::vector<glm::ivec3>{extreme});
}

TEST_CASE("Ground shadow projection retains rotated scale center and coordinate axis order",
          "[geometry][rendering]")
{
    const glm::vec3 center(10, 0, 20), vertex(12, 4, 25);
    for (float angle : {0.0f, 0.3f, glm::half_pi<float>()}) {
        const GroundShadowProjection projection(center, 4, 2, angle);
        const auto coordinates = projection.coordinates(vertex);
        const float cosine = std::cos(-angle), sine = std::sin(-angle);
        const glm::vec2 expected(0.125f * (5 * cosine - 2 * sine) + 0.5f,
                                 0.25f * (2 * sine + 5 * cosine) + 0.5f);
        REQUIRE(glm::distance(coordinates, expected) < 1e-5f);
        REQUIRE(projection.coordinates(center) == glm::vec2(0.5f));
    }
}

TEST_CASE("Ground polygon queries return owned sorted runs beyond thirty faces",
          "[geometry][octree]")
{
    const ScopedGame game;
    game.game.level.replacePrimary(std::make_unique<World>());
    auto& world = *GetWorld();
    constexpr unsigned faces = 42;
    auto mesh = std::make_shared<Mesh>(faces, 3);
    mesh->getVertexLoc()[0] = {-1, 0, -1};
    mesh->getVertexLoc()[1] = {0, 0, 1};
    mesh->getVertexLoc()[2] = {1, 0, -1};
    for (unsigned i = 0; i < faces; ++i) mesh->getPolyVertex()[i] = {0, 1, 2};
    MeshInstance instance(nullptr);
    instance.setModel(mesh);
    world.psrMeshes = {nullptr, &instance};
    LevelSpatialIndex tree("", &world);
    tree.m_meshCount = 2;
    tree.m_alpha_bits.assign(2, false);
    tree.registry.current_regions = {true, false};
    tree.m_polygon_bit_count = faces + 1;
    tree.m_spatial.m_bounds.minimum = {-100, -100, -100};
    tree.m_spatial.m_node_extent = 1000;
    tree.m_leaf_grid_dimensions = {1, 1, 1};
    tree.m_leaf_lookup.resize(1);
    tree.m_leaf_lookup[0] = 1;
    tree.m_leaves.resize(2);
    tree.m_leaves[1].polygon_offset = 1;
    tree.m_polygon_index_stream.resize(faces + 2);
    tree.m_polygon_index_stream[1] = faces;
    tree.m_aulPolyLookup.resize(faces + 1);
    for (unsigned i = 0; i < faces; ++i) {
        tree.m_polygon_index_stream[i + 2] = i + 1;
        tree.m_aulPolyLookup[i + 1] = (1u << 16) | (faces - i - 1);
    }
    const auto polygons = tree.polygonsNearPoint({0, 1, 0}, 2, 2);
    REQUIRE(polygons.size() == faces);
    for (unsigned i = 0; i < faces; ++i) REQUIRE(polygons[i] == ((1u << 16) | i));
    tree.m_alpha_bits[1] = true;
    REQUIRE(tree.polygonsNearPoint({0, 1, 0}, 2, 2).empty());
    REQUIRE(polygons.size() == faces);
    REQUIRE(polygons.front() == (1u << 16));
    REQUIRE(tree.registry.current_regions == std::vector<bool>{true, false});
}

TEST_CASE("Ground shadow shading retains the source terrain bounds", "[geometry][rendering]")
{
    const ScopedTemporaryDirectory directory("wiz8-shadow-bounds");
    const auto assets = directory.path / "assets";
    const auto user = directory.path / "user";
    const auto bitmap_path = assets / "data/monsters/bitmaps";
    std::filesystem::create_directories(bitmap_path);
    std::filesystem::create_directory(user);
    const ScopedAssetRoots asset_roots({wiz8::path_to_utf8(assets), wiz8::path_to_utf8(user)});
    // A one-pixel opaque TGA keeps this a CPU graph test without retail assets.
    const std::array<unsigned char, 22> tga{0, 0, 2, 0, 0, 0,  0, 0,   0,   0,   0,
                                            0, 1, 0, 1, 0, 32, 8, 255, 255, 255, 255};
    {
        std::ofstream output(bitmap_path / "shadow.tga", std::ios::binary);
        output.write(reinterpret_cast<const char*>(tga.data()), tga.size());
        REQUIRE(output.good());
    }
    const ScopedGame game;
    game.game.level.replacePrimary(std::make_unique<World>());
    auto& world = *GetWorld();
    auto mesh = std::make_shared<Mesh>(1, 4);
    mesh->getVertexLoc()[0] = {-1, 0, -1};
    mesh->getVertexLoc()[1] = {0, 0, 1};
    mesh->getVertexLoc()[2] = {1, 0, -1};
    // Unselected terrain still contributes to the original shading bound.
    mesh->getVertexLoc()[3] = {10000, 0, 0};
    mesh->getPolyVertex()[0] = {0, 1, 2};
    MeshInstance instance;
    instance.setModel(mesh);
    world.psrMeshes = {nullptr, &instance};
    world.spatial = std::make_unique<LevelSpatialIndex>("", &world);
    auto& tree = *world.spatial;
    tree.m_meshCount = 2;
    tree.m_alpha_bits.assign(2, false);
    tree.m_polygon_bit_count = 2;
    tree.m_spatial.m_bounds.minimum = {-50000, -50000, -50000};
    tree.m_spatial.m_node_extent = 100000;
    tree.m_leaf_grid_dimensions = {1, 1, 1};
    tree.m_leaf_lookup = {1};
    tree.m_leaves.resize(2);
    tree.m_leaves[1].polygon_offset = 1;
    tree.m_polygon_index_stream = {0, 1, 1};
    tree.m_aulPolyLookup = {0, 1u << 16};

    struct ShadowSetting {
        unsigned char previous = g_settings.monster_shadows;
        ~ShadowSetting() { g_settings.monster_shadows = previous; }
    } setting;
    g_settings.monster_shadows = true;
    GroundShadow shadow(world.scene.get());
    std::optional<render::RenderContext> context;
    context.emplace(VkExtent2D{640, 480});
    shadow.updateRenderNode(*context, {}, 0);
    struct ShadingBounds : vsg::ConstVisitor {
        std::vector<glm::vec4> bounds;
        void apply(const vsg::Object& object) override { object.traverse(*this); }
        void apply(const vsg::StateGroup& group) override
        {
            if (dynamic_cast<const render::DrawNode*>(&group))
                for (const auto& command : group.stateCommands) {
                    const auto* bind = command->cast<vsg::BindDescriptorSet>();
                    if (!bind || bind->firstSet != 1) continue;
                    const auto* buffer =
                        bind->descriptorSet->descriptors.front()->cast<vsg::DescriptorBuffer>();
                    REQUIRE(buffer);
                    const auto data = buffer->bufferInfoList.front()->data.cast<vsg::vec4Array>();
                    REQUIRE(data);
                    const auto value = data->at(1);
                    bounds.emplace_back(value.x, value.y, value.z, value.w);
                }
            group.traverse(*this);
        }
    } before;
    shadow.renderNode()->accept(before);
    REQUIRE(before.bounds.size() == 1);
    const auto source = mesh->getBounds().sphere;
    REQUIRE(before.bounds.front() == glm::vec4(source.center, source.radius));
    REQUIRE(before.bounds.front().w > shadow.width);

    mesh->setBounds({-20, -30, -40}, {20, 30, 40}, {5, 6, 7}, 80);
    shadow.width *= 2;
    shadow.updateRenderNode(*context, {}, 1);
    ShadingBounds after;
    shadow.renderNode()->accept(after);
    REQUIRE(after.bounds == std::vector<glm::vec4>{glm::vec4(5, 6, 7, 80)});

    const auto previous_root = shadow.renderNode();
    const auto* address = &*context;
    context.reset();
    context.emplace(VkExtent2D{640, 480});
    REQUIRE(&*context == address);
    REQUIRE(shadow.renderNode(*context) == previous_root);
    shadow.updateRenderNode(*context, {}, 1);
    requireEffectLayout(*previous_root, context->layout.get());
    ShadingBounds recreated;
    shadow.renderNode()->accept(recreated);
    REQUIRE(recreated.bounds == after.bounds);
}

TEST_CASE("Octree queries own dense results across later queries", "[geometry][octree]")
{
    LevelSpatialIndex tree("", nullptr);
    tree.m_spatial.m_bounds.minimum = {0, 0, 0};
    tree.m_spatial.m_bounds.maximum = {1000, 1000, 1000};
    tree.m_spatial.m_node_extent = 1000;
    tree.m_leaf_grid_dimensions = {1, 1, 1};
    tree.registry.cells = std::make_unique<W8OctreeObjectRegistry>();
    tree.m_object_bit_count = 10001;
    const glm::ivec3 cell(0, 0, 0);
    // Populate the query index directly. RegisterObjectCell also updates the
    // reverse index, navigator entries and de-duplicates one insertion at a
    // time; none of those operations is under test here. With 10k objects in
    // one cell, repeating those updates dominates this query regression.
    auto& by_cell = tree.registry.cells->by_cell;
    by_cell.reserve(10001);
    const auto cell_key = PackOctreeCellKey(cell.x, cell.y, cell.z);
    for (unsigned id = 1; id <= 10001; ++id)
        by_cell.emplace(cell_key, PackOctreeObjectKey(W8_OCTREE_KIND_LOCATION, id));
    const BoundingBox bounds{{0, 0, 0}, {999, 999, 999}};
    const auto dense = tree.queryObjects(bounds, W8_OCTREE_KIND_LOCATION, -1);
    REQUIRE(dense.size() == 10001);

    // The dense query crosses the former fixed capacity. Exercise the
    // convenience queries on a smaller set and retain the earlier result.
    tree.registry.cells->by_cell.clear();
    tree.registry.cells->by_object.clear();
    for (int id = 1; id <= 512; ++id)
        tree.registry.cells->RegisterObjectCell(W8_OCTREE_KIND_LOCATION, id, cell);
    const auto nearby = tree.nearbyLocations({500, 500, 500}, 1);
    REQUIRE(nearby.size() == 512);
    const auto locations = tree.locationsInBox(bounds, 7);
    REQUIRE(locations.size() == 511);
    REQUIRE(std::ranges::find(locations, 7u) == locations.end());
    REQUIRE(tree.locationsInBox(bounds, 0) == nearby);
    REQUIRE(tree.queryObjects(bounds, W8_OCTREE_KIND_PROP, -1).empty());
    REQUIRE(dense.size() == 10001);
    REQUIRE(tree.locationsInBox({{2000, 2000, 2000}, {3000, 3000, 3000}}, 0).empty());
    REQUIRE(tree.locationsInBox({{999, 999, 999}, {0, 0, 0}}, 0).size() == 512);
}

TEST_CASE("Octree walks retain ordered crossings, early exits and coordinate rounding",
          "[geometry][octree]")
{
    LevelSpatialIndex tree("", nullptr);
    tree.m_spatial.m_bounds.minimum = {0, 0, 0};
    tree.m_spatial.m_node_extent = 100;
    const std::vector<glm::ivec3> diagonal{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {1, 1, 1},
                                           {2, 1, 1}, {2, 1, 2}, {2, 2, 2}};
    for (int signs = 0; signs < 8; ++signs) {
        glm::vec3 from(25), to(225);
        for (int axis = 0; axis < 3; ++axis)
            if ((signs & (1 << axis)) != 0) {
                from[axis] = 275;
                to[axis] = 75;
            }
        const auto walk = tree.cellWalk(from, to);
        auto expected = diagonal;
        for (auto& cell : expected)
            for (int axis = 0; axis < 3; ++axis)
                if ((signs & (1 << axis)) != 0) cell[axis] = 2 - cell[axis];
        std::vector<glm::ivec3> visited;
        walk.visitCells([&](const glm::ivec3& cell) {
            visited.push_back(cell);
            return true;
        });
        REQUIRE(visited == expected);
        // Stop at every possible crossing, including either secondary axis.
        for (std::size_t stop = 1; stop <= expected.size(); ++stop) {
            visited.clear();
            walk.visitCells([&](const glm::ivec3& cell) {
                visited.push_back(cell);
                return visited.size() < stop;
            });
            REQUIRE(visited == std::vector<glm::ivec3>(expected.begin(), expected.begin() + stop));
        }
    }
    for (int axis = 0; axis < 3; ++axis) {
        glm::vec3 from(25), to(25);
        to[axis] = 225;
        const auto walk = tree.cellWalk(from, to);
        REQUIRE(walk.major_axis == axis);
        std::vector<glm::ivec3> visited;
        walk.visitCells([&](const glm::ivec3& cell) {
            visited.push_back(cell);
            return true;
        });
        REQUIRE(visited.size() == 3);
        for (int index = 0; index < 3; ++index) {
            glm::ivec3 expected(0);
            expected[axis] = index;
            REQUIRE(visited[index] == expected);
        }
    }
    REQUIRE(tree.cellAt({-99, -100, -199}) == glm::ivec3(0, -1, -1));
    const auto stationary = tree.cellWalk({125, 225, 325}, {125, 225, 325});
    std::vector<glm::ivec3> visited;
    stationary.visitCells([&](const glm::ivec3& cell) {
        visited.push_back(cell);
        return true;
    });
    REQUIRE((visited == std::vector<glm::ivec3>{{1, 2, 3}}));

    // The preprocessing walk deliberately retains its world-quantized start.
    tree.m_spatial.m_node_extent = 100.123f;
    const glm::vec3 boundary{100.121f, 25, 25};
    auto walk = tree.cellWalk(boundary, {350, 25, 25});
    REQUIRE(walk.cell.x == 1);
    REQUIRE(tree.cellAt(boundary).x == 0);
    walk.cell = tree.cellAt(boundary);
    visited.clear();
    walk.visitCells([&](const glm::ivec3& cell) {
        visited.push_back(cell);
        return true;
    });
    REQUIRE(visited.front().x == 0);
}

TEST_CASE("Build-tree queries own uncapped ordered results and independent state",
          "[geometry][octree]")
{
    const BoundingBox domain{{0, 0, 0}, {100, 100, 100}};
    SurfaceIndex tree(1000, domain);
    REQUIRE(tree.queryAlongSegment({0, 0, 0}, {0, 0, 0}, 1).empty());
    const std::array<glm::vec3, 3> vertices{{{50, 0, 0}, {50, 100, 0}, {50, 0, 100}}};
    std::vector<W8GDSurface> surfaces(10050);
    for (auto& surface : surfaces) {
        surface.vertex_indices[0] = 0;
        surface.vertex_indices[1] = 1;
        surface.vertex_indices[2] = 2;
        surface.plane = {glm::vec3(1, 0, 0), -50};
        REQUIRE(tree.InsertSurface(&surface, vertices));
    }
    const auto hits = tree.queryAlongSegment({50, 50, 50}, {0, 0, 0}, 100);
    REQUIRE(hits.size() == surfaces.size());
    for (std::size_t index = 0; index < hits.size(); ++index) {
        REQUIRE(hits[index] == &surfaces[index]);
        REQUIRE((surfaces[index].flags & 0x2000u) == 0);
    }

    SurfaceIndex other(1000, domain);
    W8GDSurface separate = surfaces.front();
    REQUIRE(other.InsertSurface(&separate, vertices));
    const auto other_hits = other.queryAlongSegment({50, 50, 50}, {0, 0, 0}, 100);
    REQUIRE(other_hits == std::vector<const W8GDSurface*>{&separate});
    REQUIRE(tree.queryAlongSegment({50, 50, 50}, {0, 0, 0}, 100) == hits);
    REQUIRE(other.queryAlongSegment({5000, 5000, 5000}, {0, 0, 0}, 1).empty());
    REQUIRE(hits.front() == &surfaces.front());

    // Stale serialized bits are data, never mutable query bookkeeping.
    surfaces[0].flags = 0x2000;
    REQUIRE(tree.queryAlongSegment({50, 50, 50}, {}, 100).size() == surfaces.size());
    REQUIRE(surfaces[0].flags == 0x2000);
}

TEST_CASE("Swept query coordinates retain negative-cell policy and complete offsets",
          "[geometry][octree]")
{
    LevelSpatialIndex tree("", nullptr);
    tree.m_spatial.m_bounds.minimum = {0, 0, 0};
    tree.m_spatial.m_bounds.maximum = {100, 100, 100};
    tree.m_spatial.m_node_extent = 100;
    const auto below = tree.cellCoordinates({-99, -100, -199});
    REQUIRE(below.cell == glm::ivec3(-1, -2, -2));
    REQUIRE(close(below.offset, {-0.49f, 0.5f, -0.49f}));
    REQUIRE_FALSE(below.inside);
    const auto upper = tree.cellCoordinates({100, 100, 100});
    REQUIRE(upper.cell == glm::ivec3(1));
    REQUIRE(upper.offset == glm::vec3(-0.5f));
    REQUIRE(upper.inside);
    const auto lower = tree.cellCoordinates({0, 0, 0});
    REQUIRE(lower.cell == glm::ivec3(0));
    REQUIRE(lower.offset == glm::vec3(-0.5f));
    REQUIRE(lower.inside);
    REQUIRE(tree.projectRegionsAt({101, 50, 50}).cell == glm::ivec3(1, 0, 0));
}

TEST_CASE("Collision index expands level bounds locally", "[geometry][octree]")
{
    const BoundingBox level{glm::vec3(10, 20, 30), glm::vec3(1010, 2020, 3030)};
    SurfaceIndex tree(1000, level);
    REQUIRE(level.minimum == glm::vec3(10, 20, 30));
    REQUIRE(level.maximum == glm::vec3(1010, 2020, 3030));
    std::array<W8GDSurface, 3> surfaces{};
    const std::array x_positions{-480.0f, 3500.0f, 3600.0f};
    for (unsigned i = 0; i < surfaces.size(); ++i) {
        auto& surface = surfaces[i];
        surface.vertex_indices[0] = 0;
        surface.vertex_indices[1] = 1;
        surface.vertex_indices[2] = 2;
        surface.plane = {glm::vec3(1, 0, 0), -x_positions[i]};
        const std::array<glm::vec3, 3> vertices{
            {{x_positions[i], 30, 40}, {x_positions[i], 80, 40}, {x_positions[i], 30, 90}}};
        REQUIRE(tree.InsertSurface(&surface, vertices) == (i < 2));
    }
    REQUIRE(tree.queryAlongSegment({-480, 50, 50}, {}, 5) ==
            std::vector<const W8GDSurface*>{&surfaces[0]});
    REQUIRE(tree.queryAlongSegment({3500, 50, 50}, {}, 5) ==
            std::vector<const W8GDSurface*>{&surfaces[1]});
}

TEST_CASE("Item bounds preserve local extents and report absent world items", "[geometry][item]")
{
    WorldItemEntity item;
    item.m_psrMesh = nullptr;
    item.bounds_minimum = {-5, -3, -1};
    item.bounds_maximum = {7, 9, 11};
    item.bounds_radius = 12;
    item.location = {100, 200, 300};

    const auto local = item.GetCachedLocalBounds();
    const auto radius = item.GetBoundsRadius();
    auto world = GetItemWorldBounds(&item);
    REQUIRE(world);
    REQUIRE(local.minimum == glm::vec3(-5, -3, -1));
    REQUIRE(local.maximum == glm::vec3(7, 9, 11));
    REQUIRE(radius == 12);
    REQUIRE(world.value().minimum == glm::vec3(95, 197, 299));
    REQUIRE(world.value().maximum == glm::vec3(107, 209, 311));
    world.value().minimum = glm::vec3(0);
    REQUIRE(item.GetCachedLocalBounds().minimum == local.minimum);
    REQUIRE(GetItemWorldBounds(&item).value().minimum == glm::vec3(95, 197, 299));

    const auto search = item.GetSearchPosition();
    REQUIRE(close(search, {100, 207.92f, 300}));
    item.bounds_minimum = glm::vec3(0);
    item.bounds_radius = 0;
    REQUIRE(item.GetCachedLocalBounds().minimum == glm::vec3(0));
    REQUIRE(item.GetBoundsRadius() == 0);
    REQUIRE(local.minimum == glm::vec3(-5, -3, -1));
    REQUIRE(radius == 12);
    REQUIRE_FALSE(GetItemWorldBounds(nullptr));
}

TEST_CASE("Viewport bounds normalize each edge into an independent value", "[geometry][viewport]")
{
    struct RestoreViewport {
        DesignRect saved = g_viewport;
        ~RestoreViewport() { g_viewport = saved; }
    } restore;
    // Odd surface dimensions retain independent origin/extent rounding.
    const auto scaled = ToSurfaceRect({23, 18, 617, 450}, 801, 601);
    REQUIRE(scaled.x == 28);
    REQUIRE(scaled.y == 22);
    REQUIRE(scaled.w == 743);
    REQUIRE(scaled.h == 540);
    SetViewport({23, 18, 618, 451});
    REQUIRE(g_viewport == DesignRect{23, 18, 618, 451});
    g_viewport = {80, 60, 560, 420};
    const auto bounds = GetScaledViewportBounds();
    REQUIRE(glm::length(bounds.minimum - glm::vec2(0.125f)) < 1e-6f);
    REQUIRE(glm::length(bounds.maximum - glm::vec2(0.875f)) < 1e-6f);
    g_viewport = {-80, -60, 720, 540};
    const auto outside = GetScaledViewportBounds();
    REQUIRE(glm::length(outside.minimum - glm::vec2(-0.125f)) < 1e-6f);
    REQUIRE(glm::length(outside.maximum - glm::vec2(1.125f)) < 1e-6f);
    REQUIRE(glm::length(bounds.minimum - glm::vec2(0.125f)) < 1e-6f);
    REQUIRE(glm::length(bounds.maximum - glm::vec2(0.875f)) < 1e-6f);
}

TEST_CASE("Representation transform queries preserve local and parent coordinates",
          "[geometry][transform]")
{
    Monster rep;
    rep.visual.parent_location = {10, 20, 30};
    const glm::vec3 local(1, 2, 3);
    rep.visual.SetLocation(&local);
    const auto world = rep.visual.GetLocation();
    const auto local_copy = rep.visual.GetLocalLocation();
    auto rotation = rep.visual.GetRotation();
    REQUIRE(world == glm::vec3(11, 22, 33));
    REQUIRE(local_copy == local);
    REQUIRE(rotation == glm::mat3(1));
    rotation[0][0] = 2;
    REQUIRE(rep.visual.GetRotation() == glm::mat3(1));
    rep.visual.SetRotation(&rotation);
    REQUIRE(rep.visual.GetRotation() == rotation);
    const glm::vec3 next(-1, -2, -3);
    rep.visual.SetLocation(&next);
    REQUIRE(rep.visual.GetLocation() == glm::vec3(9, 18, 27));
    REQUIRE(world == glm::vec3(11, 22, 33));
    REQUIRE(local_copy == local);
}

TEST_CASE("Navigator owns its replaceable path", "[geometry][navigation]")
{
    Navigator navigator;
    auto first = std::optional<KeyframeTrack>(std::in_place);
    navigator.SetPathAI(std::move(first));
    REQUIRE(navigator.GetPathAI() != nullptr);

    navigator.SetNavigationMode(W8_NAVIGATOR_MODE_GROUNDED);
    REQUIRE(navigator.GetPathAI()->animated);

    navigator.SetPathAI(std::optional<KeyframeTrack>(std::in_place));
    REQUIRE(navigator.GetPathAI() != nullptr);
    navigator.SetPathAI(std::nullopt);
    REQUIRE(navigator.GetPathAI() == nullptr);
}

TEST_CASE("Light copies retain independent animation and path state", "[geometry][ownership]")
{
    Light source;
    auto& keys = source.animator.emplace<KeyframedLightAnim>();
    keys.key_frames = {1, 2};
    keys.time = 1.5f;
    source.path_ai = std::optional<KeyframeTrack>(std::in_place);
    source.path_ai->nodes.emplace_back(1, 2, 3);
    source.path_ai->timed = true;
    Light copy(source);
    auto& copied = std::get<KeyframedLightAnim>(copy.animator);
    REQUIRE(copied.time == 1.5f);
    copied.key_frames.front() = 99;
    copy.path_ai->nodes.front().x = 99;
    REQUIRE(keys.key_frames.front() == 1);
    REQUIRE(source.path_ai->nodes.front().x == 1);
    REQUIRE_FALSE(copy.path_ai->timed);
    // A fade starts at its game event, even if the light is sampled later.
    Light fading;
    fading.animator.emplace<MonsterLightAnim>();
    fading.StartFadeOut(10);
    const glm::vec3 position(0);
    fading.Update(&position, 15);
    REQUIRE(fading.intensity == 0.5f);
    fading.Update(&position, 20);
    REQUIRE(fading.intensity == 0);
}

TEST_CASE("Movement queries return velocity and waypoint snapshots", "[geometry][navigation]")
{
    Navigator navigator;
    navigator.movement.velocity = {1, 2, 3};
    const auto velocity = navigator.GetVelocity();
    navigator.movement.velocity = {4, 5, 6};
    REQUIRE(navigator.GetVelocity() == glm::vec3(4, 5, 6));
    REQUIRE(velocity == glm::vec3(1, 2, 3));

    NavigatorRoute route;
    route.start_waypoint = {1, 2, 3};
    route.path_destination = {7, 8, 9};
    route.flags = W8_NAV_ATTACHMENT_START_WAYPOINT;
    const auto start = route.GetNextPosition();
    REQUIRE(start == route.start_waypoint);
    route.flags = 0;
    route.path_cursor = 0;
    route.path_position_index = 1;
    route.path[0].position = {4, 5, 6};
    const auto waypoint = route.GetNextPosition();
    REQUIRE(waypoint == glm::vec3(4, 5, 6));
    route.path[0].position = {10, 11, 12};
    REQUIRE(waypoint == glm::vec3(4, 5, 6));
    route.path_cursor = route.path_position_index;
    REQUIRE(route.GetNextPosition() == route.path_destination);
    REQUIRE(start == glm::vec3(1, 2, 3));
}

TEST_CASE("Prop position and delta queries preserve animation stepping", "[geometry][animation]")
{
    Prop prop;
    prop.animation.emplace();
    auto& animation = prop.animation.value();
    prop.location = {10, 20, 30};
    prop.animation_position = {3, 5, 7};
    prop.previous_animation_position = {1, 2, 3};
    const auto static_position = prop.GetPosition();
    REQUIRE(static_position == glm::vec3(10, 20, 30));
    animation.path_lists = true;
    REQUIRE(prop.GetPosition() == prop.animation_position);
    REQUIRE(static_position == glm::vec3(10, 20, 30));
    prop.animation_behaviour = W8_ANIMATION_PLAY_ONCE;
    prop.frame_direction = W8_ANIMATION_FORWARD;
    prop.first_frame = 0;
    prop.last_frame = 3;
    prop.subcycle = 1;
    const auto delta = prop.GetDelta();
    REQUIRE(delta == glm::vec3(2, 3, 4));
    REQUIRE(prop.subcycle == 1);
    prop.subcycle = 3;
    REQUIRE(prop.GetDelta() == glm::vec3(0, 0, 0));
    prop.animation_behaviour = W8_ANIMATION_REPEAT;
    prop.frame_method = W8_ANIMATION_WRAP;
    REQUIRE(prop.GetDelta() == glm::vec3(0, 0, 0));
    prop.frame_method = W8_ANIMATION_PING_PONG;
    REQUIRE(prop.GetDelta() == delta);
    prop.frame_direction = W8_ANIMATION_REVERSE;
    prop.subcycle = 1;
    REQUIRE(prop.GetDelta() == delta);
}

TEST_CASE("Animation bounds retain composite caching and path values", "[geometry][animation]")
{
    Animation animation;
    animation.path_lists = true;
    animation.frame_count = 2;
    REQUIRE_FALSE(AnimObjGetBounds(&animation, 0, 2));
    REQUIRE_FALSE(AnimObjGetBounds(&animation, 0, 1));
    REQUIRE(animation.frame_bounds.size() == 2);
    REQUIRE_FALSE(animation.frame_bounds[1].known);
    for (int index = 0; index < 2; ++index) {
        auto mesh = std::make_unique<AnimationMesh>();
        mesh->flags.loaded = true;
        mesh->flags.radius_loaded = true;
        mesh->frame_count = 1;
        mesh->bounds_minimum = index == 0 ? glm::vec3(-1, -2, -3) : glm::vec3(-4, -1, -2);
        mesh->bounds_maximum = index == 0 ? glm::vec3(4, 5, 6) : glm::vec3(2, 8, 3);
        mesh->meshes.push_back(std::make_unique<MeshInstance>(nullptr));
        animation.meshes[0].push_back(std::move(mesh));
        auto path = std::optional<KeyframeTrack>(std::in_place);
        PathAIAddPoint(*path, glm::vec3(100, 200, 300));
        PathAIAddPoint(*path, glm::vec3(400, 500, 600));
        path->rotations.resize(path->nodes.size(), glm::mat3(1));
        PathAISetValue((path ? &*path : nullptr), 0.75f);
        animation.paths[0].push_back(std::move(path));
    }
    const auto bounds = AnimObjGetBounds(&animation, 0, 1);
    REQUIRE(bounds);
    REQUIRE(bounds.value().minimum == glm::vec3(-4, -2, -3));
    REQUIRE(bounds.value().maximum == glm::vec3(4, 8, 6));
    REQUIRE(animation.frame_bounds[1].known);
    for (auto& path : animation.paths[0])
        REQUIRE(PathAIGetValue((path ? &*path : nullptr)) == 0.75f);
    const auto stamp = g_animesh_cache_stamp;
    animation.meshes[0][0]->bounds_minimum = {-10, -20, -30};
    const auto cached = AnimObjGetBounds(&animation, 0, 1);
    REQUIRE(cached);
    REQUIRE(cached.value().minimum == bounds.value().minimum);
    REQUIRE(cached.value().maximum == bounds.value().maximum);
    REQUIRE(g_animesh_cache_stamp == stamp);
    animation.frame_bounds[1].known = false;
    const auto refreshed = AnimObjGetBounds(&animation, 0, 1);
    REQUIRE(refreshed);
    REQUIRE(refreshed->minimum == glm::vec3(-10, -20, -30));
    for (auto& path : animation.paths[0])
        REQUIRE(PathAIGetValue((path ? &*path : nullptr)) == 0.75f);
}

TEST_CASE("Prop bounds aggregate frames without changing the current frame",
          "[geometry][animation]")
{
    Prop prop;
    prop.animation.emplace();
    auto& animation = prop.animation.value();
    animation.path_lists = true;
    animation.frame_count = 3;
    animation.frame_bounds = {{true, {-4, -2, -8}, {3, 5, 6}},
                              {true, {-1, -9, -3}, {12, 2, 5}},
                              {true, {-3, -5, -6}, {4, 8, 9}}};
    prop.subcycle = 1;
    const auto center = prop.GetCenterPosition();
    REQUIRE(center);
    REQUIRE(center.value() == glm::vec3(5.5f, -3.5f, 1));
    const auto bounds = prop.GetBounds();
    REQUIRE(bounds);
    REQUIRE(bounds.value().minimum == glm::vec3(-4, -9, -8));
    REQUIRE(bounds.value().maximum == glm::vec3(12, 8, 9));
    REQUIRE(prop.subcycle == 1);
    animation.frame_bounds[2].known = false;
    REQUIRE_FALSE(prop.GetBounds());
    REQUIRE(prop.subcycle == 1);
    animation.frame_bounds[1].known = false;
    REQUIRE_FALSE(prop.GetFrameBounds());
    REQUIRE_FALSE(prop.GetCenterPosition());
    REQUIRE(bounds.value().maximum == glm::vec3(12, 8, 9));
}

TEST_CASE("Monster bounds scale and translate independent optional values", "[geometry][animation]")
{
    Animation animation;
    animation.frame_bounds = {{true, {-1, -2, -3}, {4, 5, 6}}};
    Monster monster;
    monster.visual.active_animation = &animation;
    monster.visual.m_bLOD = 2;
    monster.visual.subcycle = 0;
    monster.scale = 2;
    monster.nav.movement.position = {10, 20, 30};
    const auto bounds = monster.GetAnimationBounds();
    REQUIRE(bounds);
    REQUIRE(bounds.value().minimum == glm::vec3(-2, -4, -6));
    REQUIRE(bounds.value().maximum == glm::vec3(8, 10, 12));
    const auto world = monster.GetWorldAnimationBounds();
    REQUIRE(world);
    REQUIRE(world->minimum == bounds->minimum + monster.nav.GetPosition());
    REQUIRE(world->maximum == bounds->maximum + monster.nav.GetPosition());
    const auto center = monster.GetAnimationCenter();
    REQUIRE(center);
    REQUIRE(std::abs(center->y - monster.nav.GetPosition().y - 14 * 0.66f) < 0.0001f);
    monster.visual.active_animation = nullptr;
    REQUIRE_FALSE(monster.GetAnimationBounds());
    REQUIRE_FALSE(monster.GetAnimationCenter());
    REQUIRE_FALSE(monster.GetWorldAnimationBounds());
    REQUIRE(bounds.value().minimum == glm::vec3(-2, -4, -6));
    REQUIRE(animation.frame_bounds[0].minimum == glm::vec3(-1, -2, -3));
}

TEST_CASE("Quad visibility owns ordered cells without a fixed capacity", "[geometry]")
{
    const auto cells = rasterizeQuadTriangle({256, 256}, {{{0, 0}, {255, 0}, {128, 255}}});
    REQUIRE(cells.size() > 20000);
    for (std::size_t index = 0; index < cells.size(); ++index) {
        REQUIRE(cells[index].x >= 0);
        REQUIRE(cells[index].x < 256);
        REQUIRE(cells[index].y >= 0);
        REQUIRE(cells[index].y < 256);
        if (index) {
            REQUIRE(cells[index].y >= cells[index - 1].y);
            if (cells[index].y == cells[index - 1].y)
                REQUIRE(cells[index].x == cells[index - 1].x + 1);
        }
    }
    REQUIRE(rasterizeQuadTriangle({256, 256}, {{{128, 255}, {255, 0}, {0, 0}}}) == cells);
    const auto clipped = rasterizeQuadTriangle({8, 8}, {{{-10, -10}, {15, 2}, {3, 20}}});
    REQUIRE_FALSE(clipped.empty());
    for (const auto& cell : clipped) {
        REQUIRE(cell.x >= 0);
        REQUIRE(cell.x < 8);
        REQUIRE(cell.y >= 0);
        REQUIRE(cell.y < 8);
    }
    REQUIRE(rasterizeQuadTriangle({8, 8}, {{{1, 1}, {2, 1}, {3, 1}}}).empty());
    REQUIRE(rasterizeQuadTriangle({8, 8}, {{{1, 1}, {1, 2}, {1, 3}}}).empty());
    REQUIRE(rasterizeQuadTriangle({0, 0}, {{{0, 0}, {10, 2}, {3, 20}}}).empty());
}

TEST_CASE("Clip plane normalization preserves consumer transform ordering and degenerate planes",
          "[geometry][rendering]")
{
    const auto scale = glm::scale(glm::mat4(1), glm::vec3(2, 3, 4));
    // GERD normalizes in object space before the inverse-transpose transform.
    REQUIRE(eyeSpaceClipPlane(scale, {2, 0, 0, -2}) == glm::vec4(0.5f, 0, 0, -1));
}

TEST_CASE("Plane contacts retain collision skin and occluder clearance", "[geometry]")
{
    const glm::vec4 plane(1, 0, 0, 0);
    const PlaneSegment crossing{{3, 4, 5}, {-7, 4, 5}, {-1, 0, 0}, 10, 10};
    for (auto mode : {PlaneContactMode::Collision, PlaneContactMode::Occlusion}) {
        const auto hit = planeContact(plane, crossing, mode);
        REQUIRE(hit.has_value());
        REQUIRE(hit.value().point == glm::vec3(0, 4, 5));
        REQUIRE(hit.value().distance == 3);
    }
    const PlaneSegment skin{{3, 4, 5}, {0.5f, 4, 5}, {-1, 0, 0}, 2.5f, 4};
    const auto endpoint = planeContact(plane, skin, PlaneContactMode::Collision);
    REQUIRE(endpoint.has_value());
    REQUIRE(endpoint.value().point == skin.end);
    REQUIRE(endpoint.value().distance == skin.length);
    REQUIRE_FALSE(planeContact(plane, skin, PlaneContactMode::Occlusion));
    const PlaneSegment nearer{{1.1f, 0, 0}, {-0.1f, 0, 0}, {-1, 0, 0}, 1.2f, 1.15f};
    const auto closest = planeContact(plane, nearer, PlaneContactMode::Collision);
    REQUIRE(closest.has_value());
    REQUIRE(std::abs(closest.value().distance - 1.1f) < 1e-6f);
    auto exit = crossing;
    exit.end.x = -5;
    exit.length = 8;
    REQUIRE(planeContact(plane, exit, PlaneContactMode::Occlusion));
    exit.end.x = -4.99f;
    REQUIRE_FALSE(planeContact(plane, exit, PlaneContactMode::Occlusion));
    auto inside_skin = crossing;
    inside_skin.start.x = 0.5f;
    const auto start = planeContact(plane, inside_skin, PlaneContactMode::Collision);
    REQUIRE(start.has_value());
    REQUIRE(start.value().point == inside_skin.start);
    REQUIRE(start.value().distance == 0);
    const auto surface_start = planeContact(plane, inside_skin, PlaneContactMode::SurfaceCollision);
    REQUIRE(surface_start.has_value());
    REQUIRE(surface_start.value().point == inside_skin.start);
    REQUIRE(surface_start.value().distance == 0.5f);
    inside_skin.start.x = 0;
    REQUIRE_FALSE(planeContact(plane, inside_skin, PlaneContactMode::Collision));
    REQUIRE_FALSE(planeContact(-plane, crossing, PlaneContactMode::Collision));
    auto limited = crossing;
    limited.limit = 2;
    REQUIRE_FALSE(planeContact(plane, limited, PlaneContactMode::Collision));
    REQUIRE(dominantPlaneAxis({-2, 2, 2}) == 0);
    REQUIRE(dominantPlaneAxis({0, -3, 3}) == 1);
    REQUIRE(dominantPlaneAxis({0, 0, 0}) == 0);
}

TEST_CASE("Surface triangles are complete values and reject invalid vertex indices", "[geometry]")
{
    std::vector<glm::vec3> vertices = {{0, 0, 0}, {0, 2, 0}, {0, 0, 2}};
    const std::array<int, 3> indices{2, 0, 1};
    const auto triangle = surfaceTriangle(vertices, indices);
    REQUIRE(triangle);
    REQUIRE((triangle.value())[0] == glm::vec3(0, 0, 2));
    REQUIRE((triangle.value())[1] == glm::vec3(0));
    REQUIRE((triangle.value())[2] == glm::vec3(0, 2, 0));
    vertices[2] = {99, 99, 99};
    REQUIRE((triangle.value())[0] == glm::vec3(0, 0, 2));
    REQUIRE(!surfaceTriangle(vertices, std::array<int, 3>{0, 1, 3}));
    REQUIRE(!surfaceTriangle(vertices, std::array<int, 3>{0, -1, 2}));
    vertices.clear();
    REQUIRE(!surfaceTriangle(vertices, indices));
}

TEST_CASE("Particle activation preserves emission limits and world placement", "[geometry]")
{
    ParticleSystem empty(nullptr, 0);
    REQUIRE(!empty.activateParticle(true));
    ParticleSystem particles(nullptr, 2);
    particles.direction_mode = W8_PARTICLE_DIRECTION_FIXED;
    particles.speed_mode = W8_PARTICLE_SPEED_FIXED;
    particles.initial_speed = 5;
    const auto first = particles.activateParticle(false);
    REQUIRE(first);
    REQUIRE(first.value() == 0);
    REQUIRE(particles.velocities[first.value()] == glm::vec3(0, -5, 0));
    REQUIRE(particles.activateParticle(false));
    REQUIRE(!particles.activateParticle(false));
    const auto replaced = particles.activateParticle(true);
    REQUIRE(replaced);
    REQUIRE(replaced.value() == 0);
    REQUIRE(particles.active_particle_count == 2);
    particles.emission_limit = particles.emission_count;
    REQUIRE(!particles.activateParticle(true));
    particles.DeactivateParticle(0);
    particles.DeactivateParticle(0);
    REQUIRE(particles.active_particle_count == 1);

    particles.emission_minimum = particles.emission_maximum = {1, 2, 3};
    particles.size_scale = 2;
    particles.transform.setLocation(100, 200, 300);
    REQUIRE(particles.emissionPosition() == glm::vec3(102, 204, 306));
}

TEST_CASE("Attached particle orientation follows rotations independently of model scale",
          "[geometry][particles]")
{
    MeshInstance parent, model;
    model.setModel(std::make_shared<Mesh>());
    model.attachTo(parent);
    parent.transform.setRotation(.2, -.3, .4);
    model.transform.setRotation(-.4, .1, -.2);
    CyclePlayer cycle;
    cycle.current_model_instance = &model;
    auto& attachment = cycle.particles.emplace_back();
    attachment.position = {1, 2, 3};
    attachment.rotation = glm::mat3(1);
    attachment.m_pstParticles = std::make_unique<ParticleSystem>(nullptr, 1);
    auto& particle = *attachment.m_pstParticles;
    particle.direction_mode = W8_PARTICLE_DIRECTION_CONE;
    const auto expected_rotation = parent.transform.getRotation() * model.transform.getRotation();
    for (const auto& scale : {glm::dvec3(-2, 3, -4), glm::dvec3(0), glm::dvec3(2, 3, 4)}) {
        parent.transform.setScale(scale);
        model.transform.setScale(scale);
        cycle.UpdateParticleAttachments();
        const auto expected_position =
            glm::dvec3(model.worldMatrix() * glm::dvec4(attachment.position, 1));
        REQUIRE(particle.transform.getLocation() == expected_position);
        const auto rotation = particle.transform.getRotation();
        for (int column = 0; column < 3; ++column)
            REQUIRE(glm::length(rotation[column] - expected_rotation[column]) < 1e-12);
    }
}

TEST_CASE("Particle simulation respects activation and completes without a view", "[geometry]")
{
    const ScopedGame game;
    ParticleSystem particles(nullptr, 2);
    particles.emission_gap = 0;
    particles.emission_mode = W8_PARTICLE_EMISSION_SINGLE;
    particles.emission_interval = 0;
    particles.direction_mode = W8_PARTICLE_DIRECTION_FIXED;
    particles.speed_mode = W8_PARTICLE_SPEED_FIXED;
    particles.initial_speed = 0;
    particles.visible = false;
    particles.AdvanceSimulation(std::chrono::milliseconds{0});
    REQUIRE(particles.emission_count == 0);
    particles.visible = true;
    particles.SetTraversalEnabled(false);
    particles.AdvanceSimulation(std::chrono::milliseconds{0});
    REQUIRE(particles.emission_count == 0);

    particles.SetTraversalEnabled(true);
    particles.AdvanceSimulation(std::chrono::milliseconds{0});
    REQUIRE(particles.emission_count == 1);
    particles.AdvanceSimulation(std::chrono::milliseconds{0});
    REQUIRE(particles.active_particle_count == 2);
    particles.DeactivateParticle(0);
    particles.SetActive(false);
    particles.AdvanceSimulation(std::chrono::milliseconds{0});
    REQUIRE_FALSE(particles.particle_active[0]);
    REQUIRE(particles.particle_active[1]);

    const auto centers = particles.particle_positions;
    // Constructing VSG state and arrays needs no graphics device. Preparing
    // additional views must consume the current simulation without advancing it.
    std::optional<render::RenderContext> context;
    context.emplace(VkExtent2D{640, 480});
    particles.updateRenderNode(*context, {}, 0);
    particles.updateRenderNode(*context, {}, 1);
    REQUIRE(particles.particle_positions == centers);
    REQUIRE(particles.emission_count == 2);
    struct CheckParticleDraws : vsg::ConstVisitor {
        std::uint32_t index_count = 0;
        void apply(const vsg::Object& object) override { object.traverse(*this); }
        void apply(const vsg::VertexIndexDraw& draw) override
        {
            if (!draw.indexCount) return;
            // A valid index range with VSG's default zero instance count
            // silently renders nothing through the ordinary draw path.
            REQUIRE(draw.instanceCount == 1);
            index_count += draw.indexCount;
        }
    } draws;
    particles.renderNode()->accept(draws);
    REQUIRE(draws.index_count == 6);

    // Reconstructing a renderer can reuse the C++ context's storage. Resource
    // identity and its frame guard must still restart with the new layout.
    const auto previous_root = particles.renderNode();
    const auto* address = &*context;
    context.reset();
    context.emplace(VkExtent2D{640, 480});
    REQUIRE(&*context == address);
    REQUIRE(particles.renderNode(*context) == previous_root);
    particles.updateRenderNode(*context, {}, 1);
    requireEffectLayout(*previous_root, context->layout.get());
    draws.index_count = 0;
    particles.renderNode()->accept(draws);
    REQUIRE(draws.index_count == 6);

    particles.release_when_done = true;
    particles.lifetime_ms = 0;
    game.game.time += std::chrono::milliseconds{1};
    particles.AdvanceSimulation(std::chrono::milliseconds{0});
    REQUIRE(particles.active_particle_count == 0);
    REQUIRE(particles.finished);
    particles.updateRenderNode(*context, {}, 2);
    draws.index_count = 0;
    particles.renderNode()->accept(draws);
    REQUIRE(draws.index_count == 0);
}

TEST_CASE("Particle simulation resolves nested animation before views consume it",
          "[geometry][particles]")
{
    ScopedGame game;
    auto first = std::make_shared<Texture>();
    auto last = std::make_shared<Texture>();
    auto inner = std::make_shared<Texture>(TexturePlayback{});
    inner->AddTexture(first);
    inner->AddTexture(last);
    inner->playback->animation_mode = W8_TEXTURE_ANIM_PLAY_ONCE;
    inner->playback->frame_rate = 10;
    auto outer = std::make_shared<Texture>(TexturePlayback{});
    outer->playback->animation_mode = W8_TEXTURE_ANIM_MANUAL;
    outer->AddTexture(inner);
    ParticleSystem particles(nullptr, 1);
    particles.emission_gap = 0;
    particles.initial_speed = 0;
    particles.speed_mode = W8_PARTICLE_SPEED_FIXED;
    particles.SetTexture(outer);
    REQUIRE(particles.activateParticle(false));
    auto& prepared = *particles.texture_frames.front()->playback->textures.front();
    REQUIRE(&prepared != inner.get());
    REQUIRE(particles.texture_frames.front()->currentFrame() == first.get());
    game.game.time += std::chrono::milliseconds{1000};
    particles.AdvanceSimulation(game.game.time);
    REQUIRE(particles.texture_frames.front()->currentFrame() == last.get());

    // Time may elapse before another view. Only simulation advances playback.
    REQUIRE(inner->currentFrame() == first.get());
    prepared.SetFrame(0);
    game.game.time += std::chrono::milliseconds{1000};
    render::RenderContext context({640, 480});
    particles.updateRenderNode(context, {}, 0);
    particles.updateRenderNode(context, {}, 1);
    REQUIRE(particles.texture_frames.front()->currentFrame() == first.get());
}

TEST_CASE("Bounds probes retain ordering across reversed and degenerate extents", "[geometry]")
{
    const BoundingBox bounds{{1, 2, 3}, {5, 8, 13}};
    const std::array<glm::vec3, 9> expected{{{3, 5, 8},
                                             {5, 8, 13},
                                             {1, 2, 3},
                                             {1, 8, 13},
                                             {1, 8, 3},
                                             {5, 8, 3},
                                             {5, 2, 13},
                                             {1, 2, 13},
                                             {5, 2, 3}}};
    REQUIRE(bounds.visibilitySamples() == expected);
    const auto reversed = BoundingBox{bounds.maximum, bounds.minimum}.visibilitySamples();
    REQUIRE(reversed[0] == expected[0]);
    REQUIRE(reversed[1] == bounds.minimum);
    REQUIRE(reversed[2] == bounds.maximum);
    const auto point = BoundingBox{{4, 5, 6}, {4, 5, 6}}.visibilitySamples();
    REQUIRE(std::ranges::all_of(point, [](const auto& p) { return p == glm::vec3(4, 5, 6); }));
}

TEST_CASE("Octree adjacent queries skip walk creation and retain early exits", "[geometry]")
{
    unsigned built = 0;
    const auto makeWalk = [&] {
        ++built;
        W8OctreeWalk walk{};
        walk.cell = {0, 0, 0};
        walk.step = {1, 0, 0};
        walk.major_axis = 0;
        walk.minor_axis0 = 1;
        walk.minor_axis1 = 2;
        walk.count = 3;
        return walk;
    };
    std::vector<glm::ivec3> cells;
    visitOctreeSegment({0, 0, 0}, {0, 0, 1}, makeWalk, [&](const auto& cell, bool adjacent) {
        REQUIRE(adjacent);
        cells.push_back(cell);
        return true;
    });
    REQUIRE(built == 0);
    REQUIRE(cells == std::vector<glm::ivec3>{{0, 0, 0}, {0, 0, 1}});
    cells.clear();
    visitOctreeSegment({0, 0, 0}, {1, 0, 0}, makeWalk, [&](const auto& cell, bool adjacent) {
        REQUIRE(adjacent);
        cells.push_back(cell);
        return false;
    });
    REQUIRE(built == 0);
    REQUIRE(cells.size() == 1);
    cells.clear();
    visitOctreeSegment({0, 0, 0}, {2, 0, 0}, makeWalk, [&](const auto& cell, bool adjacent) {
        REQUIRE(!adjacent);
        cells.push_back(cell);
        return cells.size() < 2;
    });
    REQUIRE(built == 1);
    REQUIRE(cells == std::vector<glm::ivec3>{{0, 0, 0}, {1, 0, 0}});
}

TEST_CASE("Baked mesh colors preserve material and brightness rules", "[geometry]")
{
    const glm::vec3 environment{0.25f, 0.5f, 0.75f};
    Mesh mesh(0, 4);
    auto material = std::make_shared<Material>();
    material->setAmbient(glm::vec4(0.5f, 0.25f, 1, 1));
    auto zero_material = std::make_shared<Material>();
    zero_material->setAmbient(glm::vec4(0));
    mesh.setMaterial(material, 0, Mesh::SIDE_FRONT);
    mesh.SetAmbientColor({2, 3, 4}, environment, 2);
    const auto sunlight = mesh.GetVertexSunlight(true);
    const auto lights = mesh.GetVertexLights(true);
    for (unsigned vertex = 0; vertex < 4; ++vertex) {
        sunlight[vertex] = 0.25f * (vertex + 1);
        lights[vertex] = glm::vec3(vertex + 1);
    }
    const auto baked = [&] {
        mesh.applyPendingLighting();
        return mesh.getVertexDIG(0, 0);
    };
    const auto* base = baked();
    for (unsigned vertex = 0; vertex < 4; ++vertex)
        REQUIRE(close(base[vertex],
                      (glm::vec3(1, 0.75f, 4) * sunlight[vertex] + lights[vertex] + environment) *
                          2.0f));

    auto* materials = mesh.getVertexMaterialTable(0, Mesh::SIDE_FRONT, true);
    (*materials)[0] = material;
    (*materials)[1] = zero_material;
    (*materials)[2] = nullptr;
    (*materials)[3] = material;
    mesh.flags |= W8_MESH_VERTEX_LIGHTING_DIRTY;
    const auto* mixed = baked();
    const std::array<glm::vec3, 4> ambient{{{1, 0.75f, 4}, {0, 0, 0}, {2, 3, 4}, {1, 0.75f, 4}}};
    for (unsigned vertex = 0; vertex < 4; ++vertex)
        REQUIRE(close(mixed[vertex],
                      (ambient[vertex] * sunlight[vertex] + lights[vertex] + environment) * 2.0f));

    mesh.SetAmbientColor({2, 3, 4}, environment, 0);
    mesh.flags |= W8_MESH_VERTEX_LIGHTING_DIRTY;
    REQUIRE(baked()[0] == glm::vec3(0));
}

TEST_CASE("Static and frame normals share area weighting across shade seams", "[geometry][mesh]")
{
    Mesh mesh(3, 5);
    const std::array<glm::vec3, 5> positions{
        {{0, 0, 0}, {2, 0, 0}, {0, 3, 0}, {0, 0, 4}, {0, 0, 0}}};
    std::ranges::copy(positions, mesh.getVertexLoc());
    const std::array<glm::ivec3, 3> triangles{{{0, 1, 2}, {4, 3, 1}, {2, 2, 2}}};
    std::ranges::copy(triangles, mesh.getPolyVertex());
    auto* shades = mesh.getVertexShadeIndex(1);
    for (std::uint32_t vertex = 0; vertex < positions.size(); ++vertex) shades[vertex] = vertex;
    shades[4] = 0;
    const auto* normals = mesh.getVertexNormal();
    REQUIRE(close(normals[0], {0, 0.8f, 0.6f}));
    REQUIRE(normals[4] == normals[0]);
    REQUIRE(close(normals[1], normals[0]));
    REQUIRE(close(normals[2], {0, 0, 1}));
    REQUIRE(close(normals[3], {0, 1, 0}));

    mesh.InitializeVertexFrames(1);
    mesh.vertex_compression_scale = 1;
    auto* compressed = mesh.GetVertex(0);
    for (std::size_t vertex = 0; vertex < positions.size(); ++vertex)
        for (int axis = 0; axis < 3; ++axis)
            compressed[vertex * 3 + axis] = static_cast<short>(positions[vertex][axis]);
    mesh.FinalizeVertexFrame(0);
    const auto sampled = mesh.frameGeometry(0, 0.0f);
    REQUIRE(sampled.positions.size() == positions.size());
    REQUIRE(sampled.normals.size() == positions.size());
    REQUIRE(sampled.polygon_normals.size() == triangles.size());
    REQUIRE(sampled.positions[1] == positions[1]);
    REQUIRE_THROWS_AS(mesh.frameGeometry(1, 0.0f), std::out_of_range);
    const auto& frame_normals = sampled.normals;
    REQUIRE(frame_normals[4] == frame_normals[0]);
    for (std::size_t vertex = 0; vertex < positions.size(); ++vertex)
        REQUIRE(glm::length(frame_normals[vertex] - normals[vertex]) < 0.012f);
    std::array<glm::vec3, 1> short_destination{glm::vec3(99)};
    REQUIRE_FALSE(mesh.DecompressFrame(0, W8_MESH_FRAME_LOCATIONS, short_destination));
    REQUIRE(short_destination[0] == glm::vec3(99));
}

TEST_CASE("Frustum corner ordering preserves every point and shares point sphere and box tests",
          "[geometry][frustum]")
{
    const BoundingBox box{glm::vec3(-1), glm::vec3(1)};
    std::array<glm::vec3, 8> shuffled;
    constexpr std::array<unsigned, 8> permutation{7, 0, 5, 2, 6, 1, 4, 3};
    for (unsigned i = 0; i < shuffled.size(); ++i) shuffled[i] = box.corner(permutation[i]);
    const auto original = shuffled;
    const auto sorted = sortedFrustumCorners(shuffled);
    REQUIRE(shuffled == original);
    for (unsigned i = 0; i < sorted.size(); ++i)
        REQUIRE(sorted[i] == glm::vec3(i & 1 ? 1 : -1, i & 4 ? 1 : -1, i & 2 ? 1 : -1));
    const auto planes = BuildFrustumPlanes(sorted);
    REQUIRE(PointInsideFrustum(glm::vec3(0), planes));
    for (const auto& corner : sorted) REQUIRE(PointInsideFrustum(corner, planes));
    REQUIRE_FALSE(PointInsideFrustum({2, 0, 0}, planes));
    REQUIRE(SphereInsideFrustum({2, 0, 0}, 1, planes));
    REQUIRE_FALSE(SphereInsideFrustum({2, 0, 0}, 0.99f, planes));
    W8OctRegionVolume volume{};
    for (unsigned i = 0; i < planes.size(); ++i) volume.m_planes[i] = planes[i];
    for (unsigned i = 0; i < sorted.size(); ++i) volume.m_points[i + 1] = sorted[i];
    REQUIRE(volume.ContainsPoint(glm::vec3(0)));
    REQUIRE(BoundsInsideFrustum(volume, {glm::vec3(-0.5f), glm::vec3(0.5f)}));
    REQUIRE(BoundsInsideFrustum(volume, {glm::vec3(-2), glm::vec3(2)}));
    REQUIRE(BoundsInsideFrustum(volume, {{1, -1, -1}, {2, 1, 1}}));
    REQUIRE_FALSE(BoundsInsideFrustum(volume, {glm::vec3(2), glm::vec3(3)}));
}

TEST_CASE("Octree mesh links own bounded runs and mark one-based object ids", "[geometry][octree]")
{
    OctreeMeshLinks links{{0, 2, 0, 6, 99}, {2, 0, 1, 2, 3, 0, 2}};
    REQUIRE(links.alwaysVisible().size() == 1);
    REQUIRE(links.alwaysVisible()[0] == 2);
    REQUIRE(links.forMesh(1).size() == 3);
    REQUIRE(links.forMesh(2).empty());
    REQUIRE(links.forMesh(4).empty());
    REQUIRE(links.forMesh(5).empty());
    REQUIRE(links.forMesh(3).size() == 1); // An unterminated run stays inside the record buffer.
    int first = 0, last = 0;
    std::array<int*, 2> loaded{&first, &last};
    std::vector<bool> visible(2, false);
    links.markVisible(1, std::span<int* const>(loaded), visible);
    REQUIRE(visible[0]);
    REQUIRE(visible[1]); // The last loaded object has no following slot to consult.
    loaded[0] = nullptr;
    visible.assign(2, false);
    links.markVisible(1, std::span<int* const>(loaded), visible);
    REQUIRE_FALSE(visible[0]);
    REQUIRE(visible[1]);
    visible.assign(1, false);
    links.markVisible(1, std::span<int* const>(loaded), visible);
    REQUIRE_FALSE(visible[0]);
}

TEST_CASE("Prop bounds return complete boxes and register inclusive octree cells",
          "[geometry][octree]")
{
    Prop empty;
    REQUIRE(empty.computeBounds().minimum == glm::vec3(0));
    LevelSpatialIndex tree("", nullptr);
    auto mesh = std::make_shared<Mesh>(1, 3);
    mesh->getVertexLoc()[0] = {-2, -3, 4};
    mesh->getVertexLoc()[1] = {5, 6, -7};
    mesh->getVertexLoc()[2] = {1, 2, 3};
    mesh->getPolyVertex()[0] = {0, 1, 2};
    MeshInstance instance;
    instance.setModel(mesh);
    Prop prop;
    prop.initializeCollision(nullptr, &instance, "", 4, W8_FOOTSTEP_SURFACE_NONE,
                             W8_FOOTSTEP_MATERIAL_NONE);
    const auto bounds = prop.computeBounds();
    REQUIRE(bounds.minimum == glm::vec3(-2, -3, -7));
    REQUIRE(bounds.maximum == glm::vec3(5, 6, 4));
    REQUIRE(prop.computeBounds().minimum == bounds.minimum);
    tree.registry.cells = std::make_unique<W8OctreeObjectRegistry>();
    tree.m_spatial.m_bounds.minimum = glm::vec3(10);
    tree.m_spatial.m_node_extent = 2;
    const BoundingBox cells{{9, 10, 10}, {12, 10, 10}};
    tree.AddCollidablePropBounds(4, cells);
    REQUIRE(tree.registry.cells->by_cell.size() == 2);
    REQUIRE(tree.registry.cells->by_object.size() == 2);
    tree.AddCollidablePropBounds(4, cells);
    REQUIRE(tree.registry.cells->by_cell.size() == 2);
    REQUIRE(tree.registry.cells->by_object.size() == 2);
    BoundingBox endpoints{{0, 10, 0}, {0, 10, 0}};
    endpoints.include({1, -2, 20});
    REQUIRE(endpoints.minimum.y == -2); // Y is compared with Y even when Z lies above the start.
    REQUIRE(endpoints.maximum.z == 20);
}

TEST_CASE(
    "Conditional prop channels grow independently and path cells return coordinates with keys",
    "[geometry][octree]")
{
    Prop prop;
    for (unsigned index = 1; index <= 35; ++index) {
        REQUIRE(prop.RegisterPathSurface(index, {0, 0}));
        REQUIRE(prop.RegisterPathVertex(index + 100, {0, 0}, {1, 0}));
    }
    REQUIRE(prop.conditionalWaypoints().size() == 35);
    REQUIRE(prop.conditionalEdges().size() == 35);
    REQUIRE(prop.conditionalWaypoints().front() == 1);
    REQUIRE(prop.conditionalWaypoints().back() == 35);
    REQUIRE(prop.conditionalEdges().front() == 101);
    REQUIRE(prop.conditionalEdges().back() == 135);
    REQUIRE_FALSE(prop.RegisterPathSurface(77, {1, 0}));
    REQUIRE_FALSE(prop.RegisterPathVertex(77, {0, 0}, {0, 0}));
    REQUIRE(prop.RegisterPathSurface(1, {0, 0})); // Repeated ids retain their caller order.
    REQUIRE(prop.conditionalWaypoints().back() == 1);
    REQUIRE(prop.conditionalEdges().size() == 35);
    NavigationMesh pathing;
    pathing.level_bounds.minimum = glm::vec3(10);
    pathing.grid_scale = 2;
    const auto cell = pathing.pathCell({14, 900, 16});
    REQUIRE(cell.coordinates == glm::ivec2(2, 3));
    REQUIRE(cell.key == 0x30002);
    REQUIRE(pathing.pathCell({9, 0, 9}).coordinates == glm::ivec2(0));
    const auto negative = pathing.pathCell({6, 0, 8});
    REQUIRE(negative.coordinates == glm::ivec2(-2, -1));
    REQUIRE(negative.key == static_cast<unsigned>(-1) * 0x10000u + static_cast<unsigned>(-2));
}

TEST_CASE("Surface compilation keeps stable walkable runs and drops welded degeneracies",
          "[geometry]")
{
    std::array<W8GDSurface, 6> surfaces{};
    for (int index = 0; index < 6; ++index) {
        auto& surface = surfaces[index];
        surface.index = 100 + index;
        for (int corner = 0; corner < 3; ++corner)
            surface.vertex_indices[corner] = index * 3 + corner;
    }
    for (auto index : {0, 3, 5}) surfaces[index].flags = W8_GD_SURFACE_WALKABLE;
    surfaces[2].vertex_indices[2] = surfaces[2].vertex_indices[0];
    const auto order = surfaceGeometry::compilationOrder(surfaces);
    REQUIRE(order.indices == std::vector<int>{0, 3, 5, 1, 4});
    REQUIRE(order.walkable_count == 3);
    REQUIRE(surfaces[3].index == 103);
    const auto empty = surfaceGeometry::compilationOrder({});
    REQUIRE(empty.indices.empty());
    REQUIRE(empty.walkable_count == 0);
}

TEST_CASE("Prop surface geometry preserves walkable slope while clearing steep slope", "[geometry]")
{
    std::array<glm::vec3, 3> positions{{{0, -2, 0}, {0, -2, 2}, {2, -2, 0}}};
    W8GDSurface surface{};
    for (int corner = 0; corner < 3; ++corner) surface.vertex_indices[corner] = corner;
    surface.slope = 0.3f;
    surfaceGeometry::initializePropSurface(surface, positions);
    REQUIRE(surface.plane.normal == glm::vec3(0, 1, 0));
    REQUIRE(surface.plane.w == 2);
    REQUIRE(surface.flags == (W8_GD_SURFACE_PROP_GEOMETRY | W8_GD_SURFACE_WALKABLE | 1));
    REQUIRE(surface.slope == 0.3f);
    REQUIRE(surface.contact_margin == 500);
    std::swap(surface.vertex_indices[1], surface.vertex_indices[2]);
    surfaceGeometry::initializePropSurface(surface, positions);
    REQUIRE(surface.plane.normal == glm::vec3(0, -1, 0));
    REQUIRE(surface.flags == (W8_GD_SURFACE_PROP_GEOMETRY | 1));
    REQUIRE(surface.slope == 0);
}

TEST_CASE("octree camera snapshots use native basis columns and plane coefficients", "[geometry]")
{
    Camera3D camera;
    camera.transform.setLocation(glm::dvec3(3, -7, 11));
    const auto rotation = glm::mat3(glm::rotate(glm::mat4(1), .6f, glm::vec3(0, 1, 0)));
    camera.transform.setRotation(rotation);
    camera.setFOV(1.2, 1.5);
    W8OctreeView view;
    view.far_clip = 123;
    view.captureCamera(camera);
    REQUIRE(view.camera_location == glm::vec3(3, -7, 11));
    REQUIRE(view.rotation_column0 == rotation[0]);
    REQUIRE(view.rotation_column1 == rotation[1]);
    REQUIRE(view.camera_dof == glm::vec3(camera.transform.forward()));
    REQUIRE(view.horizontal_fov == static_cast<float>(camera.getHorizontalFOV()));
    REQUIRE(view.vertical_fov == static_cast<float>(camera.getVerticalFOV()));
    REQUIRE(view.horizontal_fov_cosine == static_cast<float>(std::cos(view.horizontal_fov)));
    REQUIRE(view.far_clip == 123);

    const BoundingBox bounds{{-1, -1, -1}, {1, 1, 1}};
    std::array<glm::vec3, 8> corners;
    for (unsigned i = 0; i < corners.size(); ++i) corners[i] = bounds.corner(i);
    const auto points = sortedFrustumCorners(corners);
    const auto packed = BuildFrustumPlanes(points);
    for (unsigned i = 0; i < packed.size(); ++i)
        view.frustum_planes[i] = {packed[i].normal, packed[i].w};
    REQUIRE(planeMath::containsSphere(view.frustum_planes, glm::vec3(0), 0));
    REQUIRE(planeMath::containsSphere(view.frustum_planes, glm::vec3(2, 0, 0), 1));
    REQUIRE_FALSE(planeMath::containsSphere(view.frustum_planes, glm::vec3(2, 0, 0), .99f));
    REQUIRE(SphereInsideFrustum({2, 0, 0}, 1, packed));

    view = {};
    REQUIRE(view.camera_location == glm::vec3(0));
    REQUIRE(view.frustum_planes[5] == glm::vec4(0));
}

TEST_CASE("Projected triangle queries retain visibility and collision boundary rules", "[geometry]")
{
    using triangleProjection::Boundary;
    for (unsigned axis = 0; axis < 3; ++axis) {
        const auto point = [axis](float u, float v) {
            glm::vec3 result(0);
            result[axis] = 123;
            result[(axis + 1) % 3] = u;
            result[(axis + 2) % 3] = v;
            return result;
        };
        const std::array triangle{point(0, 0), point(2, 0), point(0, 2)};
        REQUIRE(
            triangleProjection::contains(triangle, axis, point(.5f, .5f), Boundary::visibility));
        REQUIRE(triangleProjection::contains(triangle, axis, point(.5f, .5f), Boundary::collision));
        REQUIRE_FALSE(
            triangleProjection::contains(triangle, axis, point(.5f, 0), Boundary::visibility));
        REQUIRE(triangleProjection::contains(triangle, axis, point(.5f, 0), Boundary::collision));
        REQUIRE(triangleProjection::contains(triangle, axis, point(1, 1), Boundary::visibility));
        REQUIRE_FALSE(
            triangleProjection::contains(triangle, axis, point(1, 1), Boundary::collision));
        REQUIRE_FALSE(
            triangleProjection::contains(triangle, axis, point(3, 3), Boundary::collision));
    }
}

TEST_CASE("Surface contacts return positions distances and edge limits without output parameters",
          "[geometry]")
{
    const std::array<glm::vec3, 3> triangle{{{0, 0, 0}, {0, 2, 0}, {0, 0, 2}}};
    W8GDSurface surface{};
    surface.plane = {glm::vec3(1, 0, 0), 0};
    surface.contact_margin = 150; // Wall slope adjustment leaves a zero plane limit.
    const glm::vec3 from(3, .5f, .5f);
    const auto hit = surface.segmentContact(from, {-5, 0, 0}, triangle, false).contact;
    REQUIRE(hit);
    REQUIRE(glm::length(hit->point - glm::vec3(0, .5f, .5f)) < 1e-6f);
    REQUIRE(std::abs(hit->distance - 3) < 1e-6f);
    REQUIRE(hit->surface_limit == 0);
    REQUIRE(surface.distance == 0);
    surface.distance = 9;
    REQUIRE_FALSE(surface.segmentContact({3, 4, 4}, {-5, 0, 0}, triangle, false).contact);
    REQUIRE(surface.distance == 9);
    surface.flags = W8_GD_SURFACE_WALKABLE;
    const auto limit = surface.edgeContactLimit({0, .5f, -.2f}, triangle, 1);
    REQUIRE(limit);
    REQUIRE(std::abs(limit.value() - std::sqrt(.96f)) < 1e-6f);
    REQUIRE_FALSE(surface.edgeContactLimit({0, 4, 4}, triangle, 1));

    surface.flags = W8_GD_SURFACE_CROSSING;
    surface.contact_margin = 0;
    const auto crossing = surface.segmentContact(from, {-5, 0, 0}, triangle, false).contact;
    REQUIRE(crossing);
    REQUIRE(crossing->point == from);
    REQUIRE(std::abs(crossing->distance - 3) < 1e-6f);
    REQUIRE(crossing->surface_limit > 0);
    REQUIRE(surface.distance == 9);
    const glm::vec3 reverse_from(-2, .5f, .5f);
    const auto reverse = surface.segmentContact(reverse_from, {5, 0, 0}, triangle, false).contact;
    REQUIRE(reverse);
    REQUIRE(reverse.value().point == reverse_from);
    // Reverse crossings retain the full segment when the signed span is below one.
    REQUIRE(std::abs(reverse->distance - 5) < 1e-6f);
    REQUIRE(reverse->surface_limit < 0);
    REQUIRE(surface.distance == 9);
}

TEST_CASE("Environment crossings preserve live motion without changing authored settings",
          "[geometry]")
{
    LevelSpatialIndex spatial;
    spatial.environments.resize(2);
    spatial.environments[0].gravity_y = -100;
    spatial.environments[1].gravity_y = -20;
    const auto original_scale = spatial.environments[1].scale;
    ActiveEnvironment active;
    active.value.scale = 3;
    active.value.vector = {1, 2, 3};
    W8GDSurface boundary{};
    boundary.flags = W8_GD_SURFACE_ENVIRONMENT;
    boundary.trigger_index = 1;
    boundary.distance = 5;
    spatial.ProcessCrossedSurface(&boundary, active);
    REQUIRE(active.index == 1);
    REQUIRE(active.value.gravity_y == -20);
    REQUIRE(active.value.scale == 3);
    REQUIRE(active.value.vector == glm::vec3(1, 2, 3));
    REQUIRE(spatial.environments[1].scale == original_scale);
    REQUIRE(spatial.environments[1].vector == glm::vec3(0));
    boundary.distance = -5;
    boundary.trigger_index = 0;
    spatial.ProcessCrossedSurface(&boundary, active);
    REQUIRE(active.index == 1); // Exiting an unrelated volume does not select the default.
    boundary.trigger_index = 1;
    spatial.ProcessCrossedSurface(&boundary, active);
    REQUIRE(active.index == 0);
    REQUIRE(active.value.gravity_y == -100);
    REQUIRE(active.value.vector == glm::vec3(1, 2, 3));
}

TEST_CASE("Zero motion reports its stop state without changing collision data", "[geometry]")
{
    W8GDSurface surface{};
    surface.plane = {glm::vec3(1, 0, 0), 0};
    surface.distance = 42;
    const std::array<glm::vec3, 3> triangle{{{0, 0, 0}, {0, 2, 0}, {0, 0, 2}}};
    const auto result = surface.segmentContact({3, .5f, .5f}, {}, triangle, false);
    REQUIRE(result.stops_movement);
    REQUIRE_FALSE(result.contact);
    REQUIRE(surface.distance == 42);
}

TEST_CASE("Placement grids rotate candidate cells and retain ground height bands", "[geometry]")
{
    const glm::vec3 source(13, 20, -7);
    const wiz8::placement::Grid grid(0, 10);
    REQUIRE(grid.candidate(source, {2, 3}) == glm::vec3(-17, 20 + kWorldScale, 13));
    const wiz8::placement::Grid rotated(glm::half_pi<float>(), 10);
    const auto point = rotated.candidate(source, {2, 3});
    REQUIRE(glm::length(point - glm::vec3(33, 20 + kWorldScale, 23)) < 1e-4f);
    using wiz8::placement::grounded;
    REQUIRE_FALSE(grounded({1, .5f, 3}, 0, false));
    REQUIRE(grounded({1, 1, 3}, 0, false) == glm::vec3(1, 0, 3));
    REQUIRE(grounded({1, -2500, 3}, 0, false) == glm::vec3(1, 0, 3));
    REQUIRE_FALSE(grounded({1, 2501, 3}, 0, false));
    REQUIRE(grounded({1, 5000, 3}, 0, true) == glm::vec3(1, 0, 3));
    REQUIRE_FALSE(grounded({1, 5001, 3}, 0, true));
}

TEST_CASE("Camera motion inputs retain key ordering vertical gates and total speed limits",
          "[geometry]")
{
    REQUIRE(cameraMotionInput(W8_CAMERA_MOTION_FORWARD | W8_CAMERA_MOTION_BACKWARD, {0, 0, 10}, 5,
                              10, true, true) == glm::vec3(0, 0, 5));
    REQUIRE(cameraMotionInput(W8_CAMERA_MOTION_STRAFE_LEFT | W8_CAMERA_MOTION_STRAFE_RIGHT,
                              {-10, 0, 0}, 5, 10, true, true) == glm::vec3(-5, 0, 0));
    REQUIRE(cameraMotionInput(W8_CAMERA_MOTION_UP, {0, 3, 0}, 5, 10, false, true) ==
            glm::vec3(0, 3, 0));
    REQUIRE(cameraMotionInput(W8_CAMERA_MOTION_DOWN, {0, 3, 0}, 5, 10, true, false) ==
            glm::vec3(0, 3, 0));
    const auto diagonal = cameraMotionInput(
        W8_CAMERA_MOTION_FORWARD | W8_CAMERA_MOTION_STRAFE_RIGHT, {0, 0, 0}, 10, 10, true, true);
    REQUIRE(std::abs(glm::length(diagonal) - 10) < 1e-5f);
    REQUIRE(std::abs(diagonal.x - diagonal.z) < 1e-5f);
}

TEST_CASE("Camera rotation and movement share the level motion flag policy", "[geometry]")
{
    PartyMotion level;
    level.flags = W8_LEVEL_FLAG_MOVED_THIS_UPDATE;
    REQUIRE(level.resolveMotionFlags(W8_CAMERA_MOTION_FORWARD | W8_CAMERA_MOTION_FAST, true) ==
            (W8_CAMERA_MOTION_FORWARD | W8_CAMERA_MOTION_FAST));
    REQUIRE(level.flags == (W8_LEVEL_FLAG_MOVED_THIS_UPDATE | W8_LEVEL_FLAG_FAST_MOVEMENT));
    REQUIRE(level.resolveMotionFlags(W8_CAMERA_MOTION_LOOK_UP | W8_CAMERA_MOTION_FAST, false) ==
            W8_CAMERA_MOTION_LOOK_UP);
    REQUIRE(level.flags == W8_LEVEL_FLAG_MOVED_THIS_UPDATE);
    level.flags = W8_LEVEL_FLAG_MOVEMENT_ACTIVE | W8_LEVEL_FLAG_FAST_MOVEMENT;
    REQUIRE(level.resolveMotionFlags(W8_CAMERA_MOTION_FORWARD, true) ==
            (W8_CAMERA_MOTION_FORWARD | W8_CAMERA_MOTION_FAST));
    level.flags = W8_LEVEL_FLAG_MOVEMENT_ACTIVE;
    REQUIRE(level.resolveMotionFlags(W8_CAMERA_MOTION_FORWARD | W8_CAMERA_MOTION_FAST, true) ==
            W8_CAMERA_MOTION_FORWARD);
    level.flags = W8_LEVEL_FLAG_MOVEMENT_STOPPED;
    REQUIRE(level.resolveMotionFlags(W8_CAMERA_MOTION_FORWARD | W8_CAMERA_MOTION_LOOK_UP, true) ==
            W8_CAMERA_MOTION_LOOK_UP);
}

TEST_CASE("Paused camera APIs return complete initialized values", "[geometry]")
{
    const struct RestoreRuntime {
        bool resume = g_level_motion_resume_pending;
        ~RestoreRuntime()
        {
            ResetGameTimeAccumulator();
            g_level_motion_resume_pending = resume;
        }
    } restore;
    LevelSpatialIndex data;
    data.compiled_available = false;
    data.collision_available = true;
    ResetGameTimeAccumulator();
    GameTimeAccumulator().m_flags.paused = true;
    PartyCamera controller;
    controller.motion_started = true;
    g_level_motion_resume_pending = false;
    const auto basis = glm::mat3(glm::rotate(glm::mat4(1), .4f, glm::vec3(0, 1, 0)));
    const auto orientation = controller.cameraRotation(W8_CAMERA_MOTION_TURN_LEFT, basis);
    REQUIRE(orientation.rotation == basis);
    REQUIRE(orientation.movement_basis == basis);
    const auto motion = controller.cameraMotion(data, W8_CAMERA_MOTION_FORWARD, {1, 2, 3}, basis);
    REQUIRE_FALSE(motion.moved);
    REQUIRE(motion.displacement == glm::vec3(0));
    g_level_motion_resume_pending = true;
    GameTimeAccumulator().m_flags.raw_time = true;
    REQUIRE(
        controller.cameraMotion(data, W8_CAMERA_MOTION_FORWARD, {1, 2, 3}, basis).displacement ==
        glm::vec3(0));
}

TEST_CASE("Environment ambient scales scenes and lights while retaining the source colour",
          "[geometry]")
{
    Light light(nullptr);
    World world{};
    world.environment.lights = {&light};
    world.scene = std::make_unique<WorldScene>();
    auto& scene = *world.scene;
    const glm::vec3 colour(-0.5f, 0.5f, 2.0f);
    ApplyEnvironmentColour(&world, 0.75f, &colour);
    REQUIRE(scene.getAmbientLight() == glm::vec3(0, 0.375f, 1));
    REQUIRE(light.ambient == scene.getAmbientLight());
    REQUIRE(world.environment.colour == colour);
    REQUIRE(world.environment.intensity == 0.75f);
    SetWorldEnvironmentIntensity(&world, 0.5f);
    REQUIRE(scene.getAmbientLight() == glm::vec3(0, 0.25f, 1));
    REQUIRE(light.ambient == scene.getAmbientLight());
    REQUIRE(world.environment.colour == colour);
    world.scene = nullptr;
    glm::vec3 ambient(1);
    GetWorldLightValue(&world, &ambient);
    REQUIRE(ambient == glm::vec3(0));
}

TEST_CASE("Positional sound copies keep placement without sharing playback", "[sound]")
{
    Sound3D source("source.wav");
    source.position = {1, 2, 3};
    source.falloff = 50;
    Sound3D copy(source);
    source.position = {9, 8, 7};
    REQUIRE(copy.position == glm::vec3(1, 2, 3));
    REQUIRE(copy.falloff == 50);
    REQUIRE_FALSE(copy.sound_handle);
}

TEST_CASE("Material sharing preserves exact colors and releases unused resources",
          "[geometry][materials]")
{
    W8MaterialRecord record{};
    record.opacity = 1;
    record.diffuse = {0.5001f, 0.25f, 0.75f};
    auto first = LoadMaterial("", record);
    auto same = LoadMaterial("", record);
    REQUIRE(first.material == same.material);
    // Both colors became 0.50 in the old cache key.
    record.diffuse.x = 0.5002f;
    auto different = LoadMaterial("", record);
    REQUIRE(first.material != different.material);
    REQUIRE(first.material->parms.diffuse.x == 0.5001f);
    REQUIRE(different.material->parms.diffuse.x == 0.5002f);
    const std::weak_ptr<Material> weak = different.material;
    different = {};
    REQUIRE(weak.expired());
    std::fill(std::begin(record.texture_names[0]), std::end(record.texture_names[0]), 'x');
    REQUIRE_THROWS(LoadMaterial("", record));
}

TEST_CASE("Movement searches score approach and retreat independently", "[geometry][pathing]")
{
    NavigationMesh pathing;
    std::vector<W8PathSearchNode> nodes(2);
    nodes[0].position = {10, 0, 0};
    nodes[1].position = {100, 0, 0};
    const glm::vec3 target(0);
    const auto score = [&](unsigned short index, bool retreat) {
        return pathing.UpdateSearchNodeScore(index, &target, 5, 5, nodes, retreat);
    };
    const auto near_approach = score(0, false);
    const auto far_approach = score(1, false);
    REQUIRE(near_approach < far_approach);
    REQUIRE(score(0, true) > score(1, true));
    REQUIRE(score(0, false) == near_approach);
    REQUIRE(score(1, false) == far_approach);
}

TEST_CASE("Movement traces use each search's source and target heights", "[geometry][pathing]")
{
    World world;
    world.spatial = std::make_unique<LevelSpatialIndex>("", &world);
    world.spatial->collision_available = true;
    auto& data = *world.collisionIndex();
    // A low wall blocks the ground-level trace, but not the default eye-height trace.
    data.m_pVertices = {{500, 0, 0}, {500, 400, 0}, {500, 0, 1000}};
    data.m_pSurfaces.resize(2);
    auto& wall = data.m_pSurfaces[1];
    for (int index = 0; index < 3; ++index) wall.vertex_indices[index] = index;
    surfaceGeometry::initializePropSurface(wall, data.m_pVertices);
    wall.index = 1;
    auto& tree = *world.spatial;
    tree.registry.cells = std::make_unique<W8OctreeObjectRegistry>();
    tree.m_object_bit_count = 2;
    tree.m_spatial.m_bounds.minimum = glm::vec3(0);
    tree.m_spatial.m_node_extent = 1000;
    tree.m_leaf_grid_dimensions = {1, 1, 1};
    tree.m_leaf_lookup = {1};
    tree.m_leaves.resize(2);
    tree.m_leaves[1].gd_polygon_offset = 1;
    tree.m_gd_surface_index_stream = {0, 1, 1};
    NavigationMesh pathing(&tree);
    NavigatorMovement movement{};
    movement.target_position = {100, 100, 100};
    std::vector<W8PathSearchNode> nodes(1);
    nodes[0].position = {900, 100, 100};
    const W8MovementTrace low{.offset = glm::vec3(0), .target_height_offset = 0};
    const W8MovementTrace eye;
    REQUIRE_FALSE(pathing.SearchNodeSeesTarget(0, &movement, nodes, low));
    REQUIRE(pathing.SearchNodeSeesTarget(0, &movement, nodes, eye));
    REQUIRE_FALSE(pathing.SearchNodeSeesTarget(0, &movement, nodes, low));
    REQUIRE(movement.target_position == glm::vec3(100, 100, 100));
}

TEST_CASE("Ground probes report the level surface they settle on, including surface zero",
          "[geometry][octree]")
{
    World world;
    world.spatial = std::make_unique<LevelSpatialIndex>("", &world);
    world.spatial->collision_available = true;
    auto& data = *world.collisionIndex();
    data.m_pVertices = {{0, 0, 0}, {0, 0, 1000}, {1000, 0, 0}};
    data.m_pSurfaces.resize(1);
    auto& floor = data.m_pSurfaces[0];
    for (int index = 0; index < 3; ++index) floor.vertex_indices[index] = index;
    surfaceGeometry::initializePropSurface(floor, data.m_pVertices);
    floor.index = 0;
    auto& tree = *world.spatial;
    tree.m_object_bit_count = 1;
    tree.m_spatial.m_bounds.minimum = glm::vec3(0);
    tree.m_spatial.m_clipped_bounds.maximum = glm::vec3(1000);
    tree.m_spatial.m_node_extent = 1000;
    tree.m_leaf_grid_dimensions = {1, 1, 1};
    tree.m_leaf_lookup = {1};
    tree.m_leaves.resize(2);
    tree.m_leaves[1].gd_polygon_offset = 1;
    tree.m_gd_surface_index_stream = {0, 1, 0};

    const auto ground = tree.settleToGround({500, 100, 200}, false, 500);
    REQUIRE(ground.position == glm::vec3(500, 0, 200));
    REQUIRE(ground.contact);
    REQUIRE(std::get<GroundSurfaceContact>(*ground.contact).surface == 0);
    REQUIRE_FALSE(ground.prop());

    const auto miss = tree.settleToGround({900, 100, 900}, false, 500);
    REQUIRE(miss.position == glm::vec3(900, 100, 900));
    REQUIRE_FALSE(miss.contact);
}

TEST_CASE("Surface ray queries keep chance and skip filters local", "[geometry]")
{
    const std::array<glm::vec3, 3> vertices{{{0, 0, 0}, {0, 2, 0}, {0, 0, 2}}};
    W8GDSurface definition{};
    definition.plane = {glm::vec3(1, 0, 0), 0};
    definition.vertex_indices[0] = 0;
    definition.vertex_indices[1] = 1;
    definition.vertex_indices[2] = 2;
    definition.index = 37;
    definition.chance = 50;
    definition.flags = W8_GD_SURFACE_SKIP_FILTERED_TRACE;
    const std::array surfaces{definition};
    const PlaneSegment segment{{3, .5f, .5f}, {-2, .5f, .5f}, {-1, 0, 0}, 5, 5};
    const auto hit = traceSurfaces(surfaces, vertices, segment,
                                   {.mode = 1, .percent_roll = +[] { return 99u; }});
    REQUIRE(hit);
    REQUIRE(hit->surface_index == 37);
    REQUIRE(hit->distance == 3);
    REQUIRE(hit->point == glm::vec3(0, .5f, .5f));
    REQUIRE_FALSE(traceSurfaces(surfaces, vertices, segment,
                                {.mode = 1, .percent_roll = +[] { return 50u; }}));
    REQUIRE_FALSE(traceSurfaces(surfaces, vertices, segment, {.skip_filtered = true}));
    REQUIRE(traceSurfaces(surfaces, vertices, segment, {}));
    REQUIRE(surfaces[0].flags == definition.flags);
}

TEST_CASE("Cursor animation keeps rectangular artwork without baking sprite offsets",
          "[geometry][cursor]")
{
    SpriteSheet sprites;
    sprites.colors[7] = 0xffff;
    sprites.frames.push_back({.indices = std::vector<std::uint16_t>(301 * 19, 7),
                              .offset = {11, -4},
                              .width = 301,
                              .height = 19});
    const auto animation = VideoVObjectToTextureAnim(sprites, 0, 1, true);
    REQUIRE(animation->playback->textures.size() == 1);
    const auto* image = animation->playback->textures.front()->getSurface();
    REQUIRE(image);
    REQUIRE(image->w == 301);
    REQUIRE(image->h == 19);
    for (int y : {0, 18}) {
        const auto* row = reinterpret_cast<const std::uint16_t*>(
            static_cast<const unsigned char*>(image->pixels) + y * image->pitch);
        REQUIRE(row[0] == 0xffff);
        REQUIRE(row[300] == 0xffff);
    }
}

TEST_CASE("Recycled particles restart texture playback at their new birth", "[geometry][particles]")
{
    ScopedGame game;
    TexturePlayback playback;
    playback.animation_mode = W8_TEXTURE_ANIM_PLAY_ONCE;
    playback.frame_rate = 1;
    playback.textures = {std::make_shared<Texture>(), std::make_shared<Texture>(),
                         std::make_shared<Texture>()};
    ParticleSystem particles(nullptr, 1);
    particles.emitting = false;
    particles.speed_mode = W8_PARTICLE_SPEED_FIXED;
    particles.initial_speed = 0;
    particles.expiry_mode = W8_PARTICLE_EXPIRY_TEXTURE;
    particles.SetTexture(std::make_shared<Texture>(std::move(playback)));
    REQUIRE(particles.activateParticle(false));
    particles.AdvanceSimulation(game.game.time);
    particles.DeactivateParticle(0);
    game.game.time = std::chrono::milliseconds{10000};
    REQUIRE(particles.activateParticle(false));
    game.game.time += std::chrono::milliseconds{1000};
    particles.AdvanceSimulation(game.game.time);
    REQUIRE(particles.active_particle_count == 1);
    REQUIRE(particles.texture_frames.front()->playback->frame == 1);
}

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/euler_angles.hpp>

#include "compat/compiler.h"
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
#include "glm/matrix.hpp"
#include "surrender/mesh_resources.h"
#include "surrender/model_view_scope.h"
#include "surrender/plane_math.h"
#include "surrender/segment_math.h"
#include "surrender/srBoundingBox.h"
#include "surrender/srCamera.h"
#include "surrender/srClipPlane.h"
#include "surrender/srGERD.h"
#include "surrender/srIlluminator.h"
#include "surrender/srLight.h"
#include "surrender/srMaterial.h"
#include "surrender/srMeshModel.h"
#include "surrender/srModel.h"
#include "surrender/srModelInstance.h"
#include "surrender/srNode.h"
#include "surrender/srScene.h"
#include "surrender/srTriMeshPipeline.h"
#include "surrender/srTriangleCuller.h"
#include "surrender/srVertexPipe.h"
#include "surrender/triangle_projection.h"
#include "wiz8/camera_motion.h"
#include "wiz8/design_rect.h"
#include "wiz8/direction_angles.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/AniMesh.h"
#include "wiz8/engine_code/AnimObj.h"
#include "wiz8/engine_code/AnimRep.hpp"
#include "wiz8/engine_code/Camera.h"
#include "wiz8/engine_code/Emitter.h"
#include "wiz8/engine_code/Environment.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/GDProp.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/GameTimeAccumulator.h"
#include "wiz8/engine_code/Item.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/Navigator.h"
#include "wiz8/engine_code/OctBuildTree.h"
#include "wiz8/engine_code/OctMeshModel.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/PathAI.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/engine_code/ReadMesh.h"
#include "wiz8/engine_code/SoundEvent.h"
#include "wiz8/engine_code/UpdateMesh.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/game_timer.h"
#include "wiz8/engine_code/materials.h"
#include "wiz8/engine_code/mesh_links.h"
#include "wiz8/engine_code/octree_spatial.h"
#include "wiz8/engine_code/particle_modes.h"
#include "wiz8/engine_code/stCube.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/engine_code/stParticle.h"
#include "wiz8/engine_code/stSound3D.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/ground_shadow.h"
#include "wiz8/layouts/world.h"
#include "wiz8/local_code/ItemManager.h"
#include "wiz8/octree_walk.h"
#include "wiz8/placement_math.h"
#include "wiz8/surface_geometry.h"

static bool close(const glm::vec3& actual, const glm::vec3& expected, float tolerance = 1e-4f)
{
    return glm::length(actual - expected) < tolerance;
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
    REQUIRE((ground.position == a && !ground.hit));
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
        const auto projection = srSegmentMath::project(point, segment_from, segment_to);
        const auto planar_projection = srSegmentMath::project(planar, planar_from, planar_to);
        REQUIRE(std::abs(projection.distance - (x == 2 ? 5.f : std::sqrt(29.f))) < 1e-5f);
        REQUIRE(std::abs(planar_projection.distance - (x == 2 ? 3.f : std::sqrt(13.f))) < 1e-5f);
        const float expected_fraction = x < 0 ? 0 : x > 4 ? 1 : .5f;
        REQUIRE(projection.fraction == expected_fraction);
        REQUIRE(planar_projection.fraction == expected_fraction);
        REQUIRE(projection.point == glm::vec3(expected_fraction * 4, 0, 0));
        REQUIRE(planar_projection.point == glm::vec2(expected_fraction * 4, 0));
    }
    const auto degenerate = srSegmentMath::project(glm::vec3(0, 3, 4), segment_from, segment_from);
    const auto degenerate_planar =
        srSegmentMath::project(glm::vec2(0, 3), planar_from, planar_from);
    REQUIRE(degenerate.distance == 5);
    REQUIRE((degenerate.point == segment_from && degenerate.fraction == 0));
    REQUIRE(degenerate_planar.distance == 3);
    REQUIRE((degenerate_planar.point == planar_from && degenerate_planar.fraction == 0));

    W8EnvironRecord environment;
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

    GDCamera camera;
    camera.m_position = glm::vec3(3, 4, 5);
    camera.m_yaw = .25f;
    camera.m_pitch = -.5f;
    auto* previous_camera = g_gd_camera;
    g_gd_camera = &camera;
    W8World world{};
    const auto empty_location = cameraLocationOrZero(world);
    const auto empty_state = cameraState(world);
    srCamera node;
    node.setLocation(1.25, -2.5, 3.75);
    world.camera = &node;
    const auto live_location = cameraLocation();
    const auto node_location = cameraLocation(world);
    const auto state = cameraState(world);
    const auto rotation = cameraRotation(world);
    g_gd_camera = previous_camera;
    REQUIRE(empty_location == glm::vec3(0));
    REQUIRE(empty_state.position == glm::vec3(0, 0, 0));
    REQUIRE(live_location == glm::vec3(3, 4, 5));
    REQUIRE(node_location == glm::vec3(1.25f, -2.5f, 3.75f));
    REQUIRE(state.position == node_location);
    REQUIRE((state.yaw[0] == .25f && state.pitch[0] == -.5f));
    for (int index = 1; index < 6; ++index)
        REQUIRE((state.yaw[index] == 0 && state.pitch[index] == 0));
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
    delete camera.m_manual_input_timer;

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
    stMeshModel mesh(0, 2);
    auto* vertices = mesh.getVertexLoc();
    vertices[0] = glm::vec3(2, 3, 4);
    vertices[1] = glm::vec3(4, 5, 6);
    auto* linked = new stMeshModel(0, 2);
    linked->getVertexLoc()[0] = glm::vec3(6, 7, 8);
    linked->getVertexLoc()[1] = glm::vec3(8, 9, 10);
    mesh.AddPart(std::unique_ptr<stMeshModel>(linked));
    mesh.AddPart(std::make_unique<stMeshModel>(0, 0));
    const auto linked_bounds = linked->getBounds();
    REQUIRE(linked_bounds.box.minimum == glm::vec3(2, 3, 4));
    REQUIRE(linked_bounds.box.maximum == glm::vec3(8, 9, 10));
    REQUIRE(linked_bounds.sphere.center == glm::vec3(5, 6, 7));
    for (auto* member = &mesh; member; member = member->Next()) {
        REQUIRE(member->bounds_minimum == glm::vec3(2, 3, 4));
        REQUIRE(member->bounds_maximum == glm::vec3(8, 9, 10));
        REQUIRE(member->bounds_center == glm::vec3(5, 6, 7));
        REQUIRE(std::abs(member->bounds_radius - std::sqrt(27.0f)) < 1e-5f);
    }
    // Replacing all positions with negative values must not expand the box to the origin.
    for (auto* member = &mesh; member; member = member->Next()) {
        for (auto& vertex : std::span(member->getVertexLoc(), member->vertex_location_count))
            vertex = -vertex;
        member->setDirtyAll();
    }
    mesh.CalculateLinkedBounds();
    REQUIRE(mesh.bounds_minimum == glm::vec3(-8, -9, -10));
    REQUIRE(mesh.bounds_maximum == glm::vec3(-2, -3, -4));
    REQUIRE(mesh.findClosestVertex({-3, -4, -5}) == 0); // Equal distances choose the first vertex.
    stMeshModel zero(0, 2);
    zero.scaleToMaxRadius(10);
    zero.scaleToAverageRadius(10);
    REQUIRE(zero.getVertexLoc()[0] == glm::vec3(0, 0, 0));
    REQUIRE(zero.getMaxRadius() == 0);
}

static void baked_lighting()
{
    auto material = std::make_shared<srMaterial>();
    material->parms.diffuse = glm::vec4(0.5f, 1, 0.25f, 1);
    auto model = std::make_shared<stMeshModel>(0, 2);
    auto& mesh = *model;
    mesh.getVertexLoc()[0] = glm::vec3(1, 0, 0);
    mesh.getVertexLoc()[1] = glm::vec3(2, 0, 0);
    const auto normal = glm::normalize(glm::vec3(1, 1, 0));
    mesh.getVertexNormal()[0] = normal;
    mesh.getVertexNormal()[1] = normal;
    mesh.setMaterial(material, 0, srMeshModel::SIDE_FRONT);
    stModelInstance instance(nullptr);
    instance.setModel(model);
    instance.setLocation(10, 0, 0);
    instance.setScale(glm::dvec3(2, 1, 1));
    instance.setRotation(0, glm::half_pi<double>(), 0);
    struct TestLight : stLight {
        using stLight::stLight;
        ~TestLight() override = default;
    };
    TestLight light(nullptr);
    light.diffuse = glm::vec3(1, 0.5f, 1);
    light.setLocation(10, 4, -2);
    light.setLinearAttenuation(10, 1.0f / 511);
    light.intensity = 1;
    REQUIRE(BakeInstanceVertexLighting(&instance, &light, false));
    const float first_contribution = 2 / std::sqrt(5.0f) * 0.6f;
    const float second_contribution = 0.6f * (1 - std::sqrt(20.0f) / 10);
    const auto colors = mesh.GetVertexLights(false, -1);
    REQUIRE(close(colors[0], glm::vec3(0.5f, 0.5f, 0.25f) * first_contribution));
    REQUIRE(close(colors[1], glm::vec3(0.5f, 0.5f, 0.25f) * second_contribution));
    REQUIRE((mesh.vertex_lighting_ready && (mesh.flags & W8_MESH_VERTEX_LIGHTING_DIRTY)));

    mesh.setMaterial(nullptr, 0, srMeshModel::SIDE_FRONT);
    colors[0] = glm::vec3(0);
    colors[1] = glm::vec3(0);
    REQUIRE(BakeInstanceVertexLighting(&instance, &light, false));
    REQUIRE(close(colors[0], light.diffuse * first_contribution));
    REQUIRE(close(colors[1], light.diffuse * second_contribution));
    mesh.setMaterial(material, 0, srMeshModel::SIDE_FRONT);

    colors[0] = glm::vec3(0);
    colors[1] = glm::vec3(0);
    const auto world_normal = glm::normalize(glm::vec3(0, 2, -1));
    const auto position = glm::dvec3(glm::vec3(10, 0, -2) + world_normal * 0.5f);
    light.setLocation(glm::dvec3(position));
    light.setLinearAttenuation(1, 1.0f / 511);
    REQUIRE(BakeInstanceVertexLighting(&instance, &light, false));
    REQUIRE(close(colors[0], glm::vec3(0.25f, 0.25f, 0.125f)));
    REQUIRE(colors[1] == glm::vec3(0, 0, 0));

    colors[0] = glm::vec3(0);
    light.setLocation(10, 0, -2); // Coincident light and vertex remain finite.
    REQUIRE(BakeInstanceVertexLighting(&instance, &light, false));
    REQUIRE(colors[0] == glm::vec3(0, 0, 0));
    light.setName("Sun");
    light.setLocation(glm::dvec3(position));
    REQUIRE(BakeInstanceVertexLighting(&instance, &light, true));
    REQUIRE(colors[0] == glm::vec3(0, 0, 0));
    auto world = instance.getWorldSpaceMatrix<glm::mat4>();
    mesh.flags &= ~W8_MESH_VERTEX_LIGHTING_DIRTY;
    mesh.RevealVerticesNear({10, 0, -2}, 0.25f, world);
    REQUIRE((colors[0] == glm::vec3(1, 1, 1) && colors[1] == glm::vec3(0, 0, 0)));
    REQUIRE(mesh.flags & W8_MESH_VERTEX_LIGHTING_DIRTY);
    mesh.RevealVerticesNear({10, 0, -4}, 0.25f, world);
    REQUIRE((colors[0] == glm::vec3(1, 1, 1) && colors[1] == glm::vec3(1, 1, 1)));
    colors[1] = glm::vec3(0);
    mesh.RevealVerticesNear({10, 0, -4}, -1, world);
    REQUIRE(colors[1] == glm::vec3(0, 0, 0));
    mesh.RevealVerticesNear(glm::vec3(std::numeric_limits<float>::quiet_NaN()), 1, world);
    REQUIRE(colors[1] == glm::vec3(0, 0, 0));
    instance.setModel(nullptr);
}

static void renderer_bounds()
{
    srGERD renderer;
    renderer.matrixMode(srGERD::MATRIX_PROJECTION);
    renderer.loadIdentity();
    renderer.matrixMode(srGERD::MATRIX_MODELVIEW);
    renderer.loadIdentity();
    REQUIRE((renderer.testBoundingSphere({0, 0, 0}, 0.1f) != srGERD::VISIBILITY_OUTSIDE));
    for (int axis = 0; axis < 3; ++axis)
        for (float sign : {-1.0f, 1.0f}) {
            glm::vec3 position(0);
            position[axis] = sign * 2;
            REQUIRE((renderer.testBoundingSphere(position, 0.1f) == srGERD::VISIBILITY_OUTSIDE));
        }
    srMeshModel empty;
    const auto empty_bounds = empty.getBounds();
    REQUIRE(empty_bounds.box.minimum == glm::vec3(0));
    REQUIRE(empty_bounds.box.maximum == glm::vec3(0));
    REQUIRE(empty_bounds.sphere.center == glm::vec3(0));
    REQUIRE(empty_bounds.sphere.radius == 0);

    srMeshModel mesh(0, 8);
    mesh.setBounds({2, 2, 2}, {3, 3, 3}, {0, 0, 0}, 0.1f);
    mesh.render_control = 0;
    REQUIRE_FALSE(mesh.isVisible(renderer)); // Visible sphere, rejected box.
    mesh.enable(srMeshModel::CONTROL_SKIP_AUTO_BOX);
    REQUIRE(mesh.isVisible(renderer));
    mesh.setBounds({-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}, {3, 0, 0}, 0.1f);
    mesh.render_control = 0;
    REQUIRE_FALSE(mesh.isVisible(renderer)); // Rejected sphere, visible box.
    mesh.enable(srMeshModel::CONTROL_SKIP_AUTO_SPHERE);
    REQUIRE(mesh.isVisible(renderer));
    mesh.setBounds({2, 2, 2}, {3, 3, 3}, {3, 0, 0}, 0.1f);
    mesh.enable(srMeshModel::CONTROL_SKIP_AUTO_BOX);
    REQUIRE(mesh.isVisible(renderer));
    mesh.disable(srMeshModel::CONTROL_SKIP_AUTO_BOX);
    mesh.vertex_location_count = 7;
    REQUIRE(mesh.isVisible(renderer)); // Small meshes bypass box culling.

    renderer.loadMatrix(
        glm::scale(glm::translate(glm::mat4(1), glm::vec3(1, 2, 3)), glm::vec3(-2, 3, 4)));
    const auto sphere = renderer.eyeSpaceBounds({{2, 3, 4}, 2});
    REQUIRE(sphere.center == glm::vec3(-3, 11, 19));
    REQUIRE(sphere.radius == 8);
    renderer.loadIdentity();
    renderer.pushClipPlane({1, 0, 0, 0}, srGERD::CLIPMODE_POSITIONAL_0);
    REQUIRE((renderer.testBoundingSphere({-0.5f, 0, 0}, 0.1f) == srGERD::VISIBILITY_OUTSIDE));
    REQUIRE((renderer.testBoundingSphere({0.5f, 0, 0}, 0.1f) != srGERD::VISIBILITY_OUTSIDE));
    renderer.popClipPlane();
    renderer.matrixMode(srGERD::MATRIX_PROJECTION);
    renderer.loadMatrix(glm::ortho(-2.0f, 4.0f, -3.0f, 5.0f, 1.0f, 10.0f));
    REQUIRE((renderer.testBoundingSphere({3, 0, -4}, 0.1f) != srGERD::VISIBILITY_OUTSIDE));
    REQUIRE((renderer.testBoundingSphere({5, 0, -4}, 0.1f) == srGERD::VISIBILITY_OUTSIDE));
    REQUIRE((renderer.testBoundingSphere({0, 6, -4}, 0.1f) == srGERD::VISIBILITY_OUTSIDE));
    REQUIRE((renderer.testBoundingSphere({0, -4, -4}, 0.1f) == srGERD::VISIBILITY_OUTSIDE));
    renderer.loadMatrix(glm::perspective(glm::half_pi<float>(), 1.0f, 1.0f, 10.0f));
    REQUIRE((renderer.testBoundingSphere({0, 0, -4}, 0.1f) != srGERD::VISIBILITY_OUTSIDE));
    REQUIRE((renderer.testBoundingSphere({0, 0, -0.5f}, 0.1f) == srGERD::VISIBILITY_OUTSIDE));
    REQUIRE((renderer.testBoundingSphere({0, 0, -12}, 0.1f) == srGERD::VISIBILITY_OUTSIDE));
    REQUIRE((renderer.testBoundingSphere({8, 0, -4}, 0.1f) == srGERD::VISIBILITY_OUTSIDE));
}

static void triangle_visibility()
{
    const std::array<glm::vec3, 9> positions{{{-0.5f, -0.5f, 0},
                                              {0.5f, -0.5f, 0},
                                              {0, 0.5f, 0},
                                              {2, -0.5f, 0},
                                              {3, -0.5f, 0},
                                              {2.5f, 0.5f, 0},
                                              {0, 0.25f, 0},
                                              {2.5f, 0, 0},
                                              {0, -0.25f, 0}}};
    const std::array<glm::ivec3, 3> triangles{{{0, 1, 2}, {3, 4, 5}, {6, 7, 8}}};
    const std::array<glm::vec4, 6> planes{
        {{1, 0, 0, 1}, {-1, 0, 0, 1}, {0, 1, 0, 1}, {0, -1, 0, 1}, {0, 0, 1, 1}, {0, 0, -1, 1}}};
    const glm::mat4 identity(1);
    std::array<w8_ulong, 3> indices;
    srTriangleCuller::Input input(identity, identity);
    input.cull_mode = 2;
    input.triangles = triangles;
    input.vertices = positions;
    input.clip_planes = planes;
    input.clip_mask = 0x3f;
    auto output = srTriangleCuller::cull(input);
    REQUIRE((output.indices == std::vector<w8_ulong>{0, 2}));
    REQUIRE((output.vertexCount() == 6));
    const std::array<w8_ulong, 6> expected{{0, 1, 2, 6, 7, 8}};
    for (unsigned i = 0; i < expected.size(); ++i)
        REQUIRE((output.avt[i] == expected[i] && output.vertex_remap[expected[i]] == i));
    // Active triangle filtering must keep source order without changing the input list.
    indices = {2, 1, 0};
    const std::array<glm::vec4, 3> facing{{{0, 0, 0, 1}, {0, 0, 0, -1}, {0, 0, 0, 1}}};
    input.active_triangles = indices;
    input.polygon_planes = facing;
    input.cull_mode = 0;
    input.clip_mask = 0;
    output = srTriangleCuller::cull(input);
    REQUIRE((output.indices == std::vector<w8_ulong>{2, 0}));
    REQUIRE((indices == std::array<w8_ulong, 3>{2, 1, 0}));
    input.active_triangles = std::span<const w8_ulong>{};
    output = srTriangleCuller::cull(input);
    REQUIRE((output.indices.empty() && output.avt.empty() && output.vertex_remap.empty()));
    const auto clip = srTriangleCuller::getClipMask({1, 0, 0}, 0, planes, 0x3f);
    REQUIRE((clip.mask == (1u << 1) && std::isfinite(clip.depth)));
}

TEST_CASE("Sprite and cursor meshes preserve face winding and UV seams", "[geometry]")
{
    // This factory must work without the former modeller initialization protocol.
    std::unique_ptr<srModelInstance> sprite(
        MakePolygonBrush(nullptr, nullptr, 2, 4, 0.125f, 0.25f, 0.5f, 0.625f, false));
    auto* quad = dynamic_cast<srMeshModel*>(sprite->getModel());
    REQUIRE(quad != nullptr);
    REQUIRE(quad->getPolygonCount() == 2);
    REQUIRE(quad->getVertexCount() == 4);
    for (const auto& face : quad->poly_vertices) {
        const auto a = quad->vertex_locations[face.x];
        const auto b = quad->vertex_locations[face.y];
        const auto c = quad->vertex_locations[face.z];
        REQUIRE(glm::cross(b - a, c - a).z < 0);
    }
    for (int i = 0; i < 4; ++i) {
        const auto position = quad->vertex_locations[i];
        const glm::vec2 expected((position.x / 2 + 0.5f) * 0.5f + 0.125f,
                                 (0.5f - position.y / 4) * 0.625f + 0.25f);
        REQUIRE(quad->texcoords[0][0][i] == expected);
    }
    std::unique_ptr<srModelInstance> empty(
        MakePolygonBrush(nullptr, nullptr, 0, 4, 0, 0, 1, 1, false));
    auto* empty_quad = dynamic_cast<srMeshModel*>(empty->getModel());
    REQUIRE(empty_quad != nullptr);
    REQUIRE(empty_quad->getPolygonCount() == 0);
    REQUIRE(empty_quad->getVertexCount() == 0);
    const auto cursor = CreateWorldCursorCube();
    auto* instance = cursor->node.get();
    REQUIRE(instance != nullptr);
    auto* cube = dynamic_cast<srMeshModel*>(instance->getModel());
    REQUIRE(cube != nullptr);
    REQUIRE(cube->getPolygonCount() == 12);
    REQUIRE(cube->getVertexCount() == 24);
    REQUIRE(cube->getMaterial(0, srMeshModel::SIDE_FRONT) != nullptr);
    REQUIRE(cube->getMaterial(0, srMeshModel::SIDE_BACK) == nullptr);
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

TEST_CASE("Triangle visibility", "[geometry]")
{
    triangle_visibility();
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
    auto mesh = std::make_shared<srMeshModel>(0, 2);
    mesh->getVertexLoc()[0] = glm::vec3(-4, -3, -2);
    mesh->getVertexLoc()[1] = glm::vec3(2, 3, 4);
    srModel& model = *mesh;
    auto box = model.getBounds().box;
    const auto sphere = model.getBounds().sphere;
    REQUIRE(box.minimum == glm::vec3(-4, -3, -2));
    REQUIRE(box.maximum == glm::vec3(2, 3, 4));
    REQUIRE(sphere.center == glm::vec3(-1, 0, 1));
    REQUIRE(std::abs(sphere.radius - std::sqrt(27.0f) * 1.00001f) < 1e-5f);
    box.minimum = glm::vec3(99);
    REQUIRE(model.getBounds().box.minimum == glm::vec3(-4, -3, -2));

    srModelInstance instance;
    instance.setModel(mesh);
    srNode& node = instance;
    const auto local = node.getLocalBounds();
    REQUIRE(local.state == 1);
    REQUIRE(local.minimum == model.getBounds().box.minimum);
    REQUIRE(local.maximum == model.getBounds().box.maximum);
    REQUIRE(local.center == sphere.center);
    REQUIRE(local.radius == sphere.radius);

    auto draw = mesh->getTriMesh();
    draw.bounds_radius = 99;
    REQUIRE(mesh->getTriMesh().bounds_radius == sphere.radius);
    mesh->getVertexLoc()[1] = glm::vec3(4, 5, 6);
    mesh->setDirtyAll();
    REQUIRE(model.getBounds().box.maximum == glm::vec3(4, 5, 6));
    REQUIRE(node.getLocalBounds().maximum == glm::vec3(4, 5, 6));

    srNode empty;
    REQUIRE(empty.getLocalBounds().state == 0);
    empty.setFlag(srNode::FLAG_GLOBAL);
    const auto global = empty.getLocalBounds();
    REQUIRE(global.state == 2);
    REQUIRE(global.minimum == glm::vec3(0));
    REQUIRE(global.maximum == glm::vec3(0));
    REQUIRE(global.center == glm::vec3(0));
    REQUIRE(global.radius == 0);
    instance.setModel(nullptr);
    instance.setFlag(srNode::FLAG_GLOBAL);
    const auto missing = node.getLocalBounds();
    REQUIRE(missing.state == 0);
    REQUIRE(missing.minimum == glm::vec3(0));
    REQUIRE(missing.maximum == glm::vec3(0));
    REQUIRE(missing.center == glm::vec3(0));
    REQUIRE(missing.radius == 0);

    stMeshModel linked(0, 1);
    linked.getVertexLoc()[0] = glm::vec3(-2, -4, -6);
    auto part = std::make_unique<stMeshModel>(0, 1);
    part->getVertexLoc()[0] = glm::vec3(2, 4, 6);
    linked.AddPart(std::move(part));
    srModel& linked_model = linked;
    REQUIRE(linked_model.getBounds().box.minimum == glm::vec3(-2, -4, -6));
    REQUIRE(linked_model.getBounds().box.maximum == glm::vec3(2, 4, 6));
    REQUIRE(linked_model.getBounds().sphere.center == glm::vec3(0));
    REQUIRE(std::abs(linked_model.getBounds().sphere.radius - std::sqrt(56.0f)) < 1e-5f);
}

TEST_CASE("Material query overrides do not mutate the cache", "[geometry]")
{
    stMaterial material;
    material.setDiffuse(glm::vec4(.2f, .3f, .4f, .8f));
    material.setEmissive(glm::vec4(.1f, .2f, .3f, 1));
    const auto original = material.getMaterialInfo();
    g_material_diffuse_scale_enabled = true;
    g_material_diffuse_scale = .5f;
    g_material_emissive_override_enabled = true;
    g_material_emissive_override = .75f;
    const auto overridden = material.getMaterialInfo();
    g_material_diffuse_scale_enabled = false;
    g_material_emissive_override_enabled = false;
    REQUIRE(overridden.diffuse == glm::vec4(.2f, .3f, .4f, .4f));
    REQUIRE(overridden.emissive == glm::vec4(.75f, .75f, .75f, 1));
    REQUIRE(material.getMaterialInfo().diffuse == original.diffuse);
    REQUIRE(material.getMaterialInfo().emissive == original.emissive);
    material.setDiffuse(glm::vec4(.5f));
    REQUIRE(material.getMaterialInfo().diffuse == glm::vec4(.5f));
    REQUIRE(original.diffuse == glm::vec4(.2f, .3f, .4f, .8f));
}

TEST_CASE("Baked lighting", "[geometry]")
{
    baked_lighting();
}

TEST_CASE("navigator route growth, copying and movement")
{
    const glm::vec3 origin(0, 0, 0), end(3, 0, 4);
    W8NavigatorAttachment route(&origin, &end);
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
    W8NavigatorAttachment copied;
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
    W8NavigatorAttachment aliased(&origin, &end);
    aliased.path_position_index = 9;
    aliased.AppendPathPosition(&aliased.path[1].position, 7);
    REQUIRE((aliased.path[9].position == end && aliased.path[9].value == 7));
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
    const struct RestoreWorld {
        std::unique_ptr<W8World> previous = std::exchange(g_world, std::make_unique<W8World>());
        ~RestoreWorld()
        {
            DestroyWorld(g_world);
            g_world = std::move(previous);
        }
    } restore;
    W8World& world = *g_world;
    constexpr unsigned faces = 42;
    auto mesh = std::make_shared<stMeshModel>(faces, 3);
    mesh->getVertexLoc()[0] = {-1, 0, -1};
    mesh->getVertexLoc()[1] = {0, 0, 1};
    mesh->getVertexLoc()[2] = {1, 0, -1};
    for (unsigned i = 0; i < faces; ++i) mesh->getPolyVertex()[i] = {0, 1, 2};
    stModelInstance instance(nullptr);
    instance.setModel(mesh);
    world.psrMeshes = {nullptr, &instance};
    W8Octree tree(nullptr, nullptr);
    tree.m_meshCount = 2;
    tree.m_alpha_bits.assign(2, false);
    tree.m_current_regions.assign(2, false);
    tree.m_visited_polygon_bits.assign(faces + 1, false);
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
}

TEST_CASE("Octree queries own dense results across later queries", "[geometry][octree]")
{
    W8Octree tree(nullptr, nullptr);
    tree.m_spatial.m_bounds.minimum = {0, 0, 0};
    tree.m_spatial.m_bounds.maximum = {1000, 1000, 1000};
    tree.m_spatial.m_node_extent = 1000;
    tree.m_leaf_grid_dimensions = {1, 1, 1};
    tree.object_registry = new W8OctreeObjectRegistry;
    tree.m_visited_object_bits.assign(10001, false);
    const glm::ivec3 cell(0, 0, 0);
    // Populate the query index directly. RegisterObjectCell also updates the
    // reverse index, navigator entries and de-duplicates one insertion at a
    // time; none of those operations is under test here. With 10k objects in
    // one cell, repeating those updates dominates this query regression.
    auto& by_cell = tree.object_registry->by_cell;
    by_cell.reserve(10001);
    const auto cell_key = PackOctreeCellKey(cell.x, cell.y, cell.z);
    for (unsigned id = 1; id <= 10001; ++id)
        by_cell.emplace(cell_key, PackOctreeObjectKey(W8_OCTREE_KIND_LOCATION, id));
    const srBoundingBox bounds{{0, 0, 0}, {999, 999, 999}};
    const auto dense = tree.queryObjects(bounds, W8_OCTREE_KIND_LOCATION, -1);
    REQUIRE(dense.size() == 10000);

    // The 10,000-result query above verifies the fixed cap. nearbyLocations
    // delegates to queryObjects, so exercise its bounds on the smaller data
    // set below rather than traversing the same 10,000 entries again.
    tree.object_registry->by_cell.clear();
    tree.object_registry->by_object.clear();
    for (int id = 1; id <= 512; ++id)
        tree.object_registry->RegisterObjectCell(W8_OCTREE_KIND_LOCATION, id, cell);
    const auto nearby = tree.nearbyLocations({500, 500, 500}, 1);
    REQUIRE(nearby.size() == 512);
    const auto locations = tree.locationsInBox(bounds, 7);
    REQUIRE(locations.size() == 511);
    REQUIRE(std::ranges::find(locations, 7u) == locations.end());
    REQUIRE(tree.locationsInBox(bounds, 0) == nearby);
    REQUIRE(tree.queryObjects(bounds, W8_OCTREE_KIND_PROP, -1).empty());
    REQUIRE(tree.locationsInBox({{2000, 2000, 2000}, {3000, 3000, 3000}}, 0).empty());
    REQUIRE(tree.locationsInBox({{999, 999, 999}, {0, 0, 0}}, 0).size() == 512);
}

TEST_CASE("Octree walks retain ordered crossings, early exits and coordinate rounding",
          "[geometry][octree]")
{
    W8Octree tree(nullptr, nullptr);
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
    W8OctBuildTree tree(1000, std::nullopt, 16, 0);
    REQUIRE(tree.queryAlongSegment({0, 0, 0}, {0, 0, 0}, 1, 3).empty());
    tree.spatial.m_bounds.minimum = {0, 0, 0};
    tree.spatial.m_bounds.maximum = {100, 100, 100};
    tree.spatial.m_extent = 100;
    tree.root = std::make_unique<W8OctBuildNode>();
    tree.spatial.m_root = tree.root.get();
    std::vector<W8GDSurface> surfaces(10050);
    for (auto& surface : surfaces) tree.root->surfaces[1].push_back(&surface);
    const auto hits = tree.queryAlongSegment({50, 50, 50}, {0, 0, 0}, 100, 3);
    REQUIRE(hits.size() == surfaces.size());
    for (std::size_t index = 0; index < hits.size(); ++index) {
        REQUIRE(hits[index] == &surfaces[index]);
        REQUIRE((surfaces[index].flags & W8_GD_SURFACE_QUERY_VISITED) == 0);
    }

    W8OctBuildTree other(1000, std::nullopt, 16, 0);
    other.spatial.m_bounds.minimum = {0, 0, 0};
    other.spatial.m_bounds.maximum = {100, 100, 100};
    other.spatial.m_extent = 100;
    other.root = std::make_unique<W8OctBuildNode>();
    other.spatial.m_root = other.root.get();
    W8GDSurface separate{};
    other.AppendLink(other.spatial.m_root, &separate, 3);
    const auto other_hits = other.queryAlongSegment({50, 50, 50}, {0, 0, 0}, 100, 3);
    REQUIRE(other_hits == std::vector<W8GDSurface*>{&separate});
    REQUIRE(tree.queryAlongSegment({50, 50, 50}, {0, 0, 0}, 100, 3) == hits);
    REQUIRE(other.queryAlongSegment({500, 500, 500}, {0, 0, 0}, 1, 3).empty());
    REQUIRE(hits.front() == &surfaces.front());

    // Kind 9 uses the physical-surface bank and retains its visited-flag policy.
    surfaces[0].flags |= W8_GD_SURFACE_QUERY_VISITED;
    const auto mixed = tree.queryAlongSegment({50, 50, 50}, {0, 0, 0}, 100, 9);
    REQUIRE(mixed.size() == surfaces.size() - 1);
    REQUIRE(mixed.front() == &surfaces[1]);
    REQUIRE(mixed.back() == &surfaces.back());
    surfaces[0].flags &= ~W8_GD_SURFACE_QUERY_VISITED;
}

TEST_CASE("Swept query coordinates retain negative-cell policy and complete offsets",
          "[geometry][octree]")
{
    W8Octree tree(nullptr, nullptr);
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

TEST_CASE("Build bounds expand locally and region cells share typed boxes", "[geometry][octree]")
{
    const srBoundingBox level{glm::vec3(10, 20, 30), glm::vec3(1010, 2020, 3030)};
    W8OctBuildTree tree(1000, level, 16, 0);
    REQUIRE(level.minimum == glm::vec3(10, 20, 30));
    REQUIRE(level.maximum == glm::vec3(1010, 2020, 3030));
    REQUIRE(tree.spatial.GetClippedBounds().minimum == glm::vec3(-490, -480, -470));
    REQUIRE(tree.spatial.GetClippedBounds().maximum == glm::vec3(1510, 2520, 3530));
    REQUIRE(tree.spatial.bounds().maximum == glm::vec3(3510, 3520, 3530));
    tree.spatial.m_region_grid_cell = 25;
    const auto cell = tree.spatial.regionCellBounds(0xff0203);
    REQUIRE(cell.minimum == tree.spatial.bounds().minimum + glm::vec3(255, 2, 3) * 25.0f);
    REQUIRE(cell.maximum == cell.minimum + 25.0f);
    W8OctSpatialState child(&tree.spatial);
    REQUIRE(child.bounds().minimum == tree.spatial.bounds().minimum);
    REQUIRE(child.GetClippedBounds().maximum == tree.spatial.GetClippedBounds().maximum);
}

TEST_CASE("Item bounds preserve local extents and report absent world items", "[geometry][item]")
{
    W8Item item;
    auto* rep = static_cast<W8ItemRep*>(item.m_pRep);
    rep->m_psrMesh = nullptr;
    rep->bounds_minimum = {-5, -3, -1};
    rep->bounds_maximum = {7, 9, 11};
    rep->bounds_radius = 12;
    rep->location = {100, 200, 300};

    const auto local = item.GetCachedLocalBounds();
    const auto radius = item.GetBoundsRadius();
    auto world = GetItemWorldBounds(&item);
    REQUIRE(world);
    REQUIRE(local.minimum == glm::vec3(-5, -3, -1));
    REQUIRE(local.maximum == glm::vec3(7, 9, 11));
    REQUIRE(radius == 12);
    REQUIRE(world->minimum == glm::vec3(95, 197, 299));
    REQUIRE(world->maximum == glm::vec3(107, 209, 311));
    world->minimum = glm::vec3(0);
    REQUIRE(item.GetCachedLocalBounds().minimum == local.minimum);
    REQUIRE(GetItemWorldBounds(&item)->minimum == glm::vec3(95, 197, 299));

    const auto search = item.GetSearchPosition();
    REQUIRE(close(search, {100, 207.92f, 300}));
    rep->bounds_minimum = glm::vec3(0);
    rep->bounds_radius = 0;
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
    W8AnimRepBase rep;
    rep.parent_location = {10, 20, 30};
    const glm::vec3 local(1, 2, 3);
    rep.SetLocation(&local);
    const auto world = rep.GetLocation();
    const auto local_copy = rep.GetLocalLocation();
    auto rotation = rep.GetRotation();
    REQUIRE(world == glm::vec3(11, 22, 33));
    REQUIRE(local_copy == local);
    REQUIRE(rotation == glm::mat3(1));
    rotation[0][0] = 2;
    REQUIRE(rep.GetRotation() == glm::mat3(1));
    rep.SetRotation(&rotation);
    REQUIRE(rep.GetRotation() == rotation);
    const glm::vec3 next(-1, -2, -3);
    rep.SetLocation(&next);
    REQUIRE(rep.GetLocation() == glm::vec3(9, 18, 27));
    REQUIRE(world == glm::vec3(11, 22, 33));
    REQUIRE(local_copy == local);
}

TEST_CASE("Navigator releases replaced and cleared paths", "[geometry][navigation]")
{
    struct TrackedPath : W8PathAI {
        explicit TrackedPath(int& releases) : releases(releases) {}
        ~TrackedPath() override { ++releases; }
        int& releases;
    };

    int releases = 0;
    {
        W8Navigator navigator;
        auto first = std::make_unique<TrackedPath>(releases);
        const auto* borrowed = first.get();
        navigator.SetPathAI(std::move(first));
        REQUIRE(releases == 0);
        REQUIRE(navigator.GetPathAI() == borrowed);

        navigator.SetPathAI(std::make_unique<TrackedPath>(releases));
        REQUIRE(releases == 1);
        navigator.SetNavigationMode(1);
        REQUIRE(releases == 2);
        REQUIRE(navigator.GetPathAI()->animated);
        navigator.SetPathAI(std::make_unique<TrackedPath>(releases));
        navigator.SetPathAI(nullptr);
        REQUIRE(releases == 3);
        REQUIRE(navigator.GetPathAI() == nullptr);
        navigator.SetPathAI(nullptr);
        REQUIRE(releases == 3);

        navigator.SetPathAI(std::make_unique<TrackedPath>(releases));
    }
    REQUIRE(releases == 4);
}

TEST_CASE("Light assignment replaces owned definitions and copies paths independently",
          "[geometry][ownership]")
{
    struct Definition final : stLightDefinition {
        int& releases;
        explicit Definition(int& releases) : releases(releases) {}
        ~Definition() override { ++releases; }
        std::unique_ptr<stLightDefinition> Clone() const override
        {
            return std::make_unique<Definition>(releases);
        }
        bool IsEnabledForSubcycle(unsigned char) override { return false; }
    };
    int releases = 0;
    {
        stLight source;
        source.m_definition = std::make_unique<Definition>(releases);
        source.path_ai = std::make_unique<W8PathAI>();
        source.path_ai->nodes.emplace_back(1, 2, 3);
        source.path_ai->timed = true;
        stLight destination;
        destination.m_definition = std::make_unique<Definition>(releases);
        destination = source;
        REQUIRE(releases == 1);
        REQUIRE(destination.definition() != source.definition());
        REQUIRE(destination.path_ai->nodes == source.path_ai->nodes);
        REQUIRE_FALSE(destination.path_ai->timed);
        destination.path_ai->nodes.front().x = 99;
        REQUIRE(source.path_ai->nodes.front().x == 1);
        destination = source;
        REQUIRE(releases == 2);
    }
    REQUIRE(releases == 4);
}

TEST_CASE("Movement queries return velocity and waypoint snapshots", "[geometry][navigation]")
{
    W8Navigator navigator;
    navigator.movement.velocity = {1, 2, 3};
    const auto velocity = navigator.GetVelocity();
    navigator.movement.velocity = {4, 5, 6};
    REQUIRE(navigator.GetVelocity() == glm::vec3(4, 5, 6));
    REQUIRE(velocity == glm::vec3(1, 2, 3));

    W8NavigatorAttachment route;
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
    W8Prop prop;
    prop.Rep()->animation.emplace();
    auto& animation = *prop.Rep()->animation;
    prop.m_pRep->location = {10, 20, 30};
    prop.animation_position = {3, 5, 7};
    prop.previous_animation_position = {1, 2, 3};
    const auto static_position = prop.GetPosition();
    REQUIRE(static_position == glm::vec3(10, 20, 30));
    animation.path_lists = true;
    REQUIRE(prop.GetPosition() == prop.animation_position);
    REQUIRE(static_position == glm::vec3(10, 20, 30));
    prop.Rep()->animation_behaviour = W8_ANIMATION_PLAY_ONCE;
    prop.Rep()->frame_direction = W8_ANIMATION_FORWARD;
    prop.Rep()->first_frame = 0;
    prop.Rep()->last_frame = 3;
    prop.Rep()->subcycle = 1;
    const auto delta = prop.GetDelta();
    REQUIRE(delta == glm::vec3(2, 3, 4));
    REQUIRE(prop.Rep()->subcycle == 1);
    prop.Rep()->subcycle = 3;
    REQUIRE(prop.GetDelta() == glm::vec3(0, 0, 0));
    prop.Rep()->animation_behaviour = W8_ANIMATION_REPEAT;
    prop.Rep()->frame_method = W8_ANIMATION_WRAP;
    REQUIRE(prop.GetDelta() == glm::vec3(0, 0, 0));
    prop.Rep()->frame_method = W8_ANIMATION_PING_PONG;
    REQUIRE(prop.GetDelta() == delta);
    prop.Rep()->frame_direction = W8_ANIMATION_REVERSE;
    prop.Rep()->subcycle = 1;
    REQUIRE(prop.GetDelta() == delta);
}

TEST_CASE("Animation bounds retain composite caching and path values", "[geometry][animation]")
{
    W8AnimObj animation;
    animation.path_lists = true;
    animation.frame_count = 2;
    REQUIRE_FALSE(AnimObjGetBounds(&animation, 0, 2));
    REQUIRE_FALSE(AnimObjGetBounds(&animation, 0, 1));
    REQUIRE(animation.frame_bounds.size() == 2);
    REQUIRE_FALSE(animation.frame_bounds[1].known);
    for (int index = 0; index < 2; ++index) {
        auto mesh = std::make_unique<W8AniMesh>();
        mesh->flags.loaded = true;
        mesh->flags.radius_loaded = true;
        mesh->frame_count = 1;
        mesh->bounds_minimum = index == 0 ? glm::vec3(-1, -2, -3) : glm::vec3(-4, -1, -2);
        mesh->bounds_maximum = index == 0 ? glm::vec3(4, 5, 6) : glm::vec3(2, 8, 3);
        mesh->meshes.push_back(std::make_unique<stModelInstance>(nullptr));
        animation.meshes[0].push_back(std::move(mesh));
        auto path = std::make_unique<W8PathAI>();
        PathAIAddPoint(*path, glm::vec3(100, 200, 300));
        PathAIAddPoint(*path, glm::vec3(400, 500, 600));
        path->rotations.resize(path->nodes.size(), glm::mat3(1));
        PathAISetValue(path.get(), 0.75f);
        animation.paths[0].push_back(std::move(path));
    }
    const auto bounds = AnimObjGetBounds(&animation, 0, 1);
    REQUIRE(bounds);
    REQUIRE(bounds->minimum == glm::vec3(-4, -2, -3));
    REQUIRE(bounds->maximum == glm::vec3(4, 8, 6));
    REQUIRE(animation.frame_bounds[1].known);
    for (const auto& path : animation.paths[0]) REQUIRE(PathAIGetValue(path.get()) == 0.75f);
    const auto stamp = g_animesh_cache_stamp;
    animation.meshes[0][0]->bounds_minimum = {-10, -20, -30};
    const auto cached = AnimObjGetBounds(&animation, 0, 1);
    REQUIRE(cached);
    REQUIRE(cached->minimum == bounds->minimum);
    REQUIRE(cached->maximum == bounds->maximum);
    REQUIRE(g_animesh_cache_stamp == stamp);
    animation.frame_bounds[1].known = false;
    const auto refreshed = AnimObjGetBounds(&animation, 0, 1);
    REQUIRE(refreshed);
    REQUIRE(refreshed->minimum == glm::vec3(-10, -20, -30));
    for (const auto& path : animation.paths[0]) REQUIRE(PathAIGetValue(path.get()) == 0.75f);
}

TEST_CASE("Prop bounds aggregate frames without changing the current frame",
          "[geometry][animation]")
{
    W8Prop prop;
    prop.Rep()->animation.emplace();
    auto& animation = *prop.Rep()->animation;
    animation.path_lists = true;
    animation.frame_count = 3;
    animation.frame_bounds = {{true, {-4, -2, -8}, {3, 5, 6}},
                              {true, {-1, -9, -3}, {12, 2, 5}},
                              {true, {-3, -5, -6}, {4, 8, 9}}};
    prop.Rep()->subcycle = 1;
    const auto center = prop.GetCenterPosition();
    REQUIRE(center);
    REQUIRE(*center == glm::vec3(5.5f, -3.5f, 1));
    const auto bounds = prop.GetBounds();
    REQUIRE(bounds);
    REQUIRE(bounds->minimum == glm::vec3(-4, -9, -8));
    REQUIRE(bounds->maximum == glm::vec3(12, 8, 9));
    REQUIRE(prop.Rep()->subcycle == 1);
    animation.frame_bounds[2].known = false;
    REQUIRE_FALSE(prop.GetBounds());
    REQUIRE(prop.Rep()->subcycle == 1);
    animation.frame_bounds[1].known = false;
    REQUIRE_FALSE(prop.GetFrameBounds());
    REQUIRE_FALSE(prop.GetCenterPosition());
    REQUIRE(bounds->maximum == glm::vec3(12, 8, 9));
}

TEST_CASE("Monster bounds scale and translate independent optional values", "[geometry][animation]")
{
    W8AnimObj animation;
    animation.frame_bounds = {{true, {-1, -2, -3}, {4, 5, 6}}};
    struct Monster : W8Monster {
        W8AnimObj* animation = nullptr;
        bool detached = false;
        W8AnimObj* GetCurrentAnimation() override { return animation; }
        W8EmitterHost* GetRepresentation() override
        {
            return detached ? nullptr : W8Monster::GetRepresentation();
        }
    } monster;
    monster.animation = &animation;
    monster.m_pRep->m_bLOD = 2;
    monster.m_pRep->subcycle = 0;
    monster.m_pRep->scale = 2;
    monster.movement.position = {10, 20, 30};
    const auto bounds = monster.GetAnimationBounds();
    REQUIRE(bounds);
    REQUIRE(bounds->minimum == glm::vec3(-2, -4, -6));
    REQUIRE(bounds->maximum == glm::vec3(8, 10, 12));
    const auto world = MonsterGetWorldAnimationBounds(&monster);
    REQUIRE(world);
    REQUIRE(world->minimum == bounds->minimum + monster.GetPosition());
    REQUIRE(world->maximum == bounds->maximum + monster.GetPosition());
    const auto center = monster.GetAnimationCenter();
    REQUIRE(center);
    REQUIRE(std::abs(center->y - monster.GetPosition().y - 14 * 0.66f) < 0.0001f);
    monster.detached = true;
    REQUIRE_FALSE(monster.GetAnimationBounds());
    REQUIRE_FALSE(monster.GetAnimationCenter());
    REQUIRE_FALSE(MonsterGetWorldAnimationBounds(&monster));
    monster.detached = false;
    monster.animation = nullptr;
    REQUIRE_FALSE(monster.GetAnimationBounds());
    REQUIRE_FALSE(monster.GetAnimationCenter());
    REQUIRE_FALSE(MonsterGetWorldAnimationBounds(&monster));
    REQUIRE_FALSE(MonsterGetWorldAnimationBounds(nullptr));
    REQUIRE(bounds->minimum == glm::vec3(-2, -4, -6));
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

TEST_CASE("Software backface selection preserves thresholds and active order", "[geometry]")
{
    std::array<glm::vec3, 3> positions{{{0, 0, 0}, {0, 0, 2}, {0, 0, -2}}};
    std::array<glm::ivec3, 3> triangles{{{0, 0, 0}, {1, 1, 1}, {2, 2, 2}}};
    const std::array<glm::vec3, 3> normals{{{0, 0, 1}, {0, 0, 1}, {0, 0, 1}}};
    srMeshModel::TriMesh mesh;
    mesh.polygon_count = triangles.size();
    mesh.positions = positions.data();
    mesh.poly_vertices = triangles.data();
    const auto positive = stMeshModel::facingPolygons(mesh, normals, glm::vec3(0), true, 0);
    REQUIRE(positive == std::vector<w8_ulong>{0, 2});
    REQUIRE(stMeshModel::facingPolygons(mesh, normals, glm::vec3(0), false, 0) ==
            std::vector<w8_ulong>{0, 1});
    REQUIRE(stMeshModel::facingPolygons(mesh, normals, glm::vec3(0), true, 1) ==
            std::vector<w8_ulong>{2});
    std::array<w8_ulong, 4> active{2, 1, 0, 2};
    mesh.active_polygons = std::span<const w8_ulong>(active);
    REQUIRE(stMeshModel::facingPolygons(mesh, normals, glm::vec3(0), true, 0) ==
            std::vector<w8_ulong>{2, 0, 2});
    REQUIRE(positive == std::vector<w8_ulong>{0, 2});
    mesh.active_polygons = std::span<const w8_ulong>{};
    REQUIRE(stMeshModel::facingPolygons(mesh, normals, glm::vec3(0), true, 0).empty());
}

TEST_CASE("Clip plane normalization preserves consumer transform ordering and degenerate planes",
          "[geometry][rendering]")
{
    srGERD renderer;
    renderer.matrixMode(srGERD::MATRIX_MODELVIEW);
    const auto scale = glm::scale(glm::mat4(1), glm::vec3(2, 3, 4));
    renderer.loadMatrix(scale);
    renderer.pushClipPlane({2, 0, 0, -2}, srGERD::CLIPMODE_POSITIONAL_0);
    // GERD normalizes in object space before the inverse-transpose transform.
    REQUIRE(renderer.getClipPlanes().planes[6] == glm::vec4(0.5f, 0, 0, -1));
    renderer.popClipPlane();
    // The CPU culler normalizes after its supplied covector transform.
    const auto plane =
        srTriangleCuller::transformClipPlane({2, 0, 0, -2}, glm::transpose(glm::inverse(scale)));
    REQUIRE(plane == glm::vec4(1, 0, 0, -2));
    REQUIRE(srTriangleCuller::transformClipPlane({0, 0, 0, 4}, glm::mat4(1)) ==
            glm::vec4(0, 0, 0, 4));
}

TEST_CASE("Empty software backface selections skip mesh submission", "[geometry]")
{
    srGERD renderer;
    renderer.setWinding(srGERD::WINDING_POSITIONAL_0);
    stMeshModel model(1, 3);
    auto* pipeline = &renderer.meshPipeline();
    std::array<glm::vec3, 3> positions{{{0, 0, 2}, {0, 0, 2}, {0, 0, 2}}};
    std::array<glm::ivec3, 1> triangles{{{0, 1, 2}}};
    const std::array<glm::vec3, 1> normals{{{0, 0, 1}}};
    srMeshModel::TriMesh mesh;
    mesh.vertex_count = positions.size();
    mesh.polygon_count = triangles.size();
    mesh.positions = positions.data();
    mesh.poly_vertices = triangles.data();
    mesh.control_flags = (1u << srMeshModel::CONTROL_FRONT) | (1u << srMeshModel::CONTROL_BACK);
    const auto before = pipeline->triangle_count;
    model.RenderTriMeshWithEquations(renderer, mesh, normals);
    REQUIRE(pipeline->triangle_count == before);
    mesh.polygon_count = 2;
    REQUIRE_THROWS_AS(model.RenderTriMeshWithEquations(renderer, mesh, normals),
                      std::invalid_argument);
    REQUIRE(pipeline->triangle_count == before);
}

TEST_CASE("Failed mesh materials release renderer and restore enable flags", "[geometry]")
{
    class FailingMaterial : public srMaterial {
    public:
        void preProcess(srVertexPipe&) override { throw std::runtime_error("material failed"); }
    };
    srGERD renderer;
    renderer.setWinding(srGERD::WINDING_POSITIONAL_0);
    renderer.toggle(srGERD::ENABLE_SORTED_RENDERING);
    auto* batch = &renderer.batchRenderer();
    renderer.toggle(srGERD::ENABLE_SORTED_RENDERING);
    renderer.toggle(srGERD::ENABLE_REVERSE_NORMALS);
    srMeshModel native(1, 3);
    stMeshModel extended(1, 3);
    std::array<glm::vec3, 3> positions{
        {{-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}, {0, 0.5f, 0.5f}}};
    std::array<glm::ivec3, 1> triangles{{{0, 1, 2}}};
    std::array<glm::vec4, 1> equations{{{0, 0, 1, -0.5f}}};
    const std::array<glm::vec3, 1> normals{{{0, 0, -1}}};
    srMeshModel::TriMesh mesh;
    mesh.vertex_count = positions.size();
    mesh.polygon_count = triangles.size();
    mesh.pass_count = 1;
    mesh.positions = positions.data();
    mesh.poly_vertices = triangles.data();
    mesh.poly_equations = equations.data();
    mesh.bounds_center = {0, 0, 0.5f};
    mesh.bounds_radius = 1;
    mesh.control_flags = (1u << srMeshModel::CONTROL_FRONT) |
                         (1u << srMeshModel::CONTROL_NO_FRONT_CULL) |
                         (1u << srMeshModel::CONTROL_SORTED_RENDERING);
    mesh.materials[0][0] = std::make_shared<FailingMaterial>();
    for (bool software : {false, true}) {
        if (software)
            REQUIRE_THROWS_AS(extended.RenderTriMeshWithEquations(renderer, mesh, normals),
                              std::runtime_error);
        else
            REQUIRE_THROWS_AS(native.renderTriMesh(renderer, mesh), std::runtime_error);
        REQUIRE_FALSE(renderer.isEnabled(srGERD::ENABLE_SORTED_RENDERING));
        REQUIRE(renderer.isEnabled(srGERD::ENABLE_REVERSE_NORMALS));
        renderer.toggle(srGERD::ENABLE_SORTED_RENDERING);
        auto* resumed = &renderer.batchRenderer();
        REQUIRE(resumed == batch);
        renderer.toggle(srGERD::ENABLE_SORTED_RENDERING);
    }
}

TEST_CASE("Plane contacts retain collision skin and occluder clearance", "[geometry]")
{
    const glm::vec4 plane(1, 0, 0, 0);
    const PlaneSegment crossing{{3, 4, 5}, {-7, 4, 5}, {-1, 0, 0}, 10, 10};
    for (auto mode : {PlaneContactMode::Collision, PlaneContactMode::Occlusion}) {
        const auto hit = planeContact(plane, crossing, mode);
        REQUIRE(hit.has_value());
        REQUIRE(hit->point == glm::vec3(0, 4, 5));
        REQUIRE(hit->distance == 3);
    }
    const PlaneSegment skin{{3, 4, 5}, {0.5f, 4, 5}, {-1, 0, 0}, 2.5f, 4};
    const auto endpoint = planeContact(plane, skin, PlaneContactMode::Collision);
    REQUIRE(endpoint.has_value());
    REQUIRE(endpoint->point == skin.end);
    REQUIRE(endpoint->distance == skin.length);
    REQUIRE_FALSE(planeContact(plane, skin, PlaneContactMode::Occlusion));
    const PlaneSegment nearer{{1.1f, 0, 0}, {-0.1f, 0, 0}, {-1, 0, 0}, 1.2f, 1.15f};
    const auto closest = planeContact(plane, nearer, PlaneContactMode::Collision);
    REQUIRE(closest.has_value());
    REQUIRE(std::abs(closest->distance - 1.1f) < 1e-6f);
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
    REQUIRE(start->point == inside_skin.start);
    REQUIRE(start->distance == 0);
    const auto surface_start = planeContact(plane, inside_skin, PlaneContactMode::SurfaceCollision);
    REQUIRE(surface_start.has_value());
    REQUIRE(surface_start->point == inside_skin.start);
    REQUIRE(surface_start->distance == 0.5f);
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
    struct RestoreGameData {
        W8GameData* previous = g_octree_game_data;
        ~RestoreGameData() { g_octree_game_data = previous; }
    } restore;
    W8GameData data(nullptr, true);
    data.m_pVertices = {{0, 0, 0}, {0, 2, 0}, {0, 0, 2}};
    g_octree_game_data = &data;
    const std::array<int, 3> indices{2, 0, 1};
    const auto triangle = surfaceTriangle(indices);
    REQUIRE(triangle);
    REQUIRE((*triangle)[0] == glm::vec3(0, 0, 2));
    REQUIRE((*triangle)[1] == glm::vec3(0));
    REQUIRE((*triangle)[2] == glm::vec3(0, 2, 0));
    data.m_pVertices[2] = {99, 99, 99};
    REQUIRE((*triangle)[0] == glm::vec3(0, 0, 2));
    REQUIRE(!surfaceTriangle(std::array<int, 3>{0, 1, 3}));
    REQUIRE(!surfaceTriangle(std::array<int, 3>{0, -1, 2}));
    g_octree_game_data = nullptr;
    REQUIRE(!surfaceTriangle(indices));
}

TEST_CASE("Particle activation values and billboard ordering", "[geometry]")
{
    stParticle empty(nullptr, 0);
    REQUIRE(!empty.activateParticle(true));
    stParticle particles(nullptr, 2);
    particles.direction_mode = W8_PARTICLE_DIRECTION_FIXED;
    particles.speed_mode = W8_PARTICLE_SPEED_FIXED;
    particles.initial_speed = 5;
    const auto first = particles.activateParticle(false);
    REQUIRE(first);
    REQUIRE(*first == 0);
    REQUIRE(particles.velocities[*first] == glm::vec3(0, -5, 0));
    REQUIRE(particles.activateParticle(false));
    REQUIRE(!particles.activateParticle(false));
    const auto replaced = particles.activateParticle(true);
    REQUIRE(replaced);
    REQUIRE(*replaced == 0);
    REQUIRE(particles.active_particle_count == 2);
    particles.emission_limit = particles.emission_count;
    REQUIRE(!particles.activateParticle(true));
    particles.DeactivateParticle(0);
    particles.DeactivateParticle(0);
    REQUIRE(particles.active_particle_count == 1);

    particles.emission_minimum = particles.emission_maximum = {1, 2, 3};
    particles.size_scale = 2;
    particles.setLocation(100, 200, 300);
    REQUIRE(particles.emissionPosition() == glm::vec3(102, 204, 306));
    particles.particle_size = 2;
    particles.particle_positions[0] = {10, 20, 30};
    particles.particle_positions[1] = {-10, -20, -30};
    glm::mat4 view(1);
    view[3] = {70, 80, 90, 1};
    const auto originalView = view;
    particles.PrepareRenderer(view);
    REQUIRE(view == originalView);
    const std::array<glm::vec3, 4> offsets{{{-2, 2, 0}, {2, 2, 0}, {2, -2, 0}, {-2, -2, 0}}};
    for (unsigned particle = 0; particle < 2; ++particle)
        for (unsigned corner = 0; corner < 4; ++corner)
            REQUIRE(particles.vertex_positions[particle * 4 + corner] ==
                    particles.particle_positions[particle] + offsets[corner]);
}

TEST_CASE("Bounds probes retain ordering across reversed and degenerate extents", "[geometry]")
{
    const srBoundingBox bounds{{1, 2, 3}, {5, 8, 13}};
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
    const auto reversed = srBoundingBox{bounds.maximum, bounds.minimum}.visibilitySamples();
    REQUIRE(reversed[0] == expected[0]);
    REQUIRE(reversed[1] == bounds.minimum);
    REQUIRE(reversed[2] == bounds.maximum);
    const auto point = srBoundingBox{{4, 5, 6}, {4, 5, 6}}.visibilitySamples();
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

TEST_CASE("Baked mesh colors preserve material and light-bank rules", "[geometry]")
{
    struct RestoreLighting {
        glm::vec3 offset = g_environment_offset;
        float scale = g_monster_light_scale;
        bool unlit = g_render_unlit;
        ~RestoreLighting()
        {
            g_environment_offset = offset;
            g_monster_light_scale = scale;
            g_render_unlit = unlit;
        }
    } restore;
    g_environment_offset = {0.25f, 0.5f, 0.75f};
    g_monster_light_scale = 2;
    g_render_unlit = false;
    stMeshModel mesh(0, 4);
    auto material = std::make_shared<srMaterial>();
    material->setAmbient(glm::vec4(0.5f, 0.25f, 1, 1));
    auto zero_material = std::make_shared<srMaterial>();
    zero_material->setAmbient(glm::vec4(0));
    mesh.setMaterial(material, 0, srMeshModel::SIDE_FRONT);
    mesh.SetAmbientColor({2, 3, 4});
    const auto sunlight = mesh.GetVertexSunlight(true);
    const auto lights = mesh.GetVertexLights(true, 0);
    for (unsigned vertex = 0; vertex < 4; ++vertex) {
        sunlight[vertex] = 0.25f * (vertex + 1);
        lights[vertex] = glm::vec3(vertex + 1);
    }
    const auto base = mesh.getTriMesh();
    for (unsigned vertex = 0; vertex < 4; ++vertex)
        REQUIRE(close(base.dig[0][vertex], (glm::vec3(1, 0.75f, 4) * sunlight[vertex] +
                                            lights[vertex] + g_environment_offset) *
                                               2.0f));

    auto* materials = mesh.getVertexMaterialTable(0, srMeshModel::SIDE_FRONT, true);
    materials->set(0, material);
    materials->set(1, zero_material);
    materials->set(2, nullptr);
    materials->set(3, material);
    mesh.flags |= W8_MESH_VERTEX_LIGHTING_DIRTY;
    const auto mixed = mesh.getTriMesh();
    const std::array<glm::vec3, 4> ambient{{{1, 0.75f, 4}, {0, 0, 0}, {2, 3, 4}, {1, 0.75f, 4}}};
    for (unsigned vertex = 0; vertex < 4; ++vertex)
        REQUIRE(close(mixed.dig[0][vertex],
                      (ambient[vertex] * sunlight[vertex] + lights[vertex] + g_environment_offset) *
                          2.0f));

    mesh.vertex_light_table = 1;
    const auto alternate = mesh.GetVertexLights(true, 1);
    std::ranges::fill(alternate, glm::vec3(3, 4, 5));
    mesh.flags |= W8_MESH_VERTEX_LIGHTING_DIRTY;
    const auto alternate_mesh = mesh.getTriMesh();
    REQUIRE(alternate_mesh.dig[0][0] == glm::vec3(6, 8, 10));
    g_monster_light_scale = 0;
    mesh.flags |= W8_MESH_VERTEX_LIGHTING_DIRTY;
    REQUIRE(mesh.getTriMesh().dig[0][0] == glm::vec3(0));
    g_render_unlit = true;
    mesh.flags |= W8_MESH_VERTEX_LIGHTING_DIRTY;
    mesh.getTriMesh();
    REQUIRE(mesh.flags & W8_MESH_VERTEX_LIGHTING_DIRTY);
}

TEST_CASE("Static and frame normals share area weighting across shade seams", "[geometry][mesh]")
{
    stMeshModel mesh(3, 5);
    const std::array<glm::vec3, 5> positions{
        {{0, 0, 0}, {2, 0, 0}, {0, 3, 0}, {0, 0, 4}, {0, 0, 0}}};
    std::ranges::copy(positions, mesh.getVertexLoc());
    const std::array<glm::ivec3, 3> triangles{{{0, 1, 2}, {4, 3, 1}, {2, 2, 2}}};
    std::ranges::copy(triangles, mesh.getPolyVertex());
    auto* shades = mesh.getVertexShadeIndex(1);
    for (w8_ulong vertex = 0; vertex < positions.size(); ++vertex) shades[vertex] = vertex;
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
    REQUIRE(sampled.normals.data() == mesh.GetVertexNormals(0, true));
    REQUIRE(sampled.polygon_normals.data() == mesh.GetPolygonNormals(0, true));
    REQUIRE_THROWS_AS(mesh.frameGeometry(1, 0.0f), std::out_of_range);
    const auto* frame_normals = mesh.GetVertexNormals(0, true);
    REQUIRE(frame_normals[4] == frame_normals[0]);
    for (std::size_t vertex = 0; vertex < positions.size(); ++vertex)
        REQUIRE(glm::length(frame_normals[vertex] - normals[vertex]) < 0.012f);
    std::array<glm::vec3, 1> short_destination{glm::vec3(99)};
    REQUIRE_FALSE(mesh.DecompressFrame(0, W8_MESH_FRAME_LOCATIONS, short_destination));
    REQUIRE(short_destination[0] == glm::vec3(99));
}

namespace {
class TraversalMarker : public srNode {
public:
    explicit TraversalMarker(srNode& parent) : srNode(&parent) {}
    void traverse(TraverseInfo& info) override
    {
        srNode::traverse(info);
        info.entries.push_back({this, PROCESS_RENDER});
    }
};
}

TEST_CASE("Scoped renderer effects preserve sibling order and global or disabled descent",
          "[geometry][rendering]")
{
    const auto check = []<class Effect>() {
        srNode root;
        auto* sibling = root.adoptChild(std::make_unique<TraversalMarker>(root));
        auto* effect = root.adoptChild(std::make_unique<Effect>(nullptr));
        auto* child = effect->adoptChild(std::make_unique<TraversalMarker>(*effect));
        effect->clearFlag(srNode::FLAG_GLOBAL);
        srNode::TraverseInfo info;
        root.traverse(info);
        REQUIRE(info.entries.size() == 4);
        REQUIRE(info.entries[0].node == sibling);
        REQUIRE(info.entries[1].node == effect);
        REQUIRE(info.entries[1].value == srNode::PROCESS_PUSH);
        REQUIRE(info.entries[2].node == child);
        REQUIRE(info.entries[3].node == effect);
        REQUIRE(info.entries[3].value == srNode::PROCESS_POP);
        REQUIRE(info.nodes.empty());

        effect->setFlag(srNode::FLAG_GLOBAL);
        info = {};
        root.traverse(info);
        REQUIRE(info.nodes.size() == 1);
        REQUIRE(info.nodes[0] == effect);
        REQUIRE(info.entries.size() == 2);
        REQUIRE(info.entries[0].node == sibling);
        REQUIRE(info.entries[1].node == child);

        effect->setFlag(srNode::FLAG_DISABLE);
        info = {};
        root.traverse(info);
        REQUIRE(info.nodes.empty());
        REQUIRE(info.entries.size() == 2);
        REQUIRE(info.entries[1].node == child);

        effect->setFlag(srNode::FLAG_TERMINATE);
        info = {};
        root.traverse(info);
        REQUIRE(info.entries.size() == 1);
        REQUIRE(info.entries[0].node == sibling);
    };
    check.template operator()<srClipPlane>();
    check.template operator()<srIlluminator>();
    check.template operator()<srLight>();
}

TEST_CASE("Wizardry light inactivity and missing definitions have distinct traversal policies",
          "[geometry][rendering]")
{
    struct RestoreFlags {
        unsigned saved = g_light_update_flags;
        ~RestoreFlags() { g_light_update_flags = saved; }
    } restore;
    g_light_update_flags = 1;
    stLight light(nullptr);
    light.setIntensity(1);
    light.clearFlag(srNode::FLAG_GLOBAL);
    auto* child = light.adoptChild(std::make_unique<TraversalMarker>(light));
    srNode::TraverseInfo info;
    light.traverse(info);
    REQUIRE(info.entries.empty()); // Active without a definition skips children too.
    light.setFlag(srNode::FLAG_DISABLE);
    light.traverse(info);
    REQUIRE(info.entries.size() == 1);
    REQUIRE(info.entries[0].node == child);
    light.clearFlag(srNode::FLAG_DISABLE);
    light.setIntensity(0);
    info = {};
    light.traverse(info);
    REQUIRE(info.entries.size() == 1);
    light.setIntensity(1);
    g_light_update_flags = 0;
    info = {};
    light.traverse(info);
    REQUIRE(info.entries.size() == 1);
    g_light_update_flags = 1;
    light.m_definition = std::make_unique<stParametricLightDefinition>();
    info = {};
    light.traverse(info);
    REQUIRE(info.entries.size() == 3);
    REQUIRE(info.entries[0].node == &light);
    REQUIRE(info.entries[1].node == child);
    REQUIRE(info.entries[2].node == &light);
}

TEST_CASE("Frustum corner ordering preserves every point and shares point sphere and box tests",
          "[geometry][frustum]")
{
    const srBoundingBox box{glm::vec3(-1), glm::vec3(1)};
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
    GDProp empty;
    REQUIRE(empty.computeBounds().minimum == glm::vec3(0));
    W8Octree tree(0, 0);
    struct RestoreOctree {
        W8Octree* previous;
        ~RestoreOctree() { g_octree = previous; }
    } restore{g_octree};
    g_octree = &tree;
    auto mesh = std::make_shared<stMeshModel>(1, 3);
    mesh->getVertexLoc()[0] = {-2, -3, 4};
    mesh->getVertexLoc()[1] = {5, 6, -7};
    mesh->getVertexLoc()[2] = {1, 2, 3};
    mesh->getPolyVertex()[0] = {0, 1, 2};
    srModelInstance instance;
    instance.setModel(mesh);
    GDProp prop(&instance, "", 4, 0, 0);
    const auto bounds = prop.computeBounds();
    REQUIRE(bounds.minimum == glm::vec3(-2, -3, -7));
    REQUIRE(bounds.maximum == glm::vec3(5, 6, 4));
    REQUIRE(prop.computeBounds().minimum == bounds.minimum);
    tree.object_registry = new W8OctreeObjectRegistry;
    tree.m_spatial.m_bounds.minimum = glm::vec3(10);
    tree.m_spatial.m_node_extent = 2;
    const srBoundingBox cells{{9, 10, 10}, {12, 10, 10}};
    tree.AddCollidablePropBounds(4, cells);
    REQUIRE(tree.object_registry->by_cell.size() == 2);
    REQUIRE(tree.object_registry->by_object.size() == 2);
    tree.AddCollidablePropBounds(4, cells);
    REQUIRE(tree.object_registry->by_cell.size() == 2);
    REQUIRE(tree.object_registry->by_object.size() == 2);
    srBoundingBox endpoints{{0, 10, 0}, {0, 10, 0}};
    endpoints.include({1, -2, 20});
    REQUIRE(endpoints.minimum.y == -2); // Y is compared with Y even when Z lies above the start.
    REQUIRE(endpoints.maximum.z == 20);
}

TEST_CASE(
    "Conditional prop channels grow independently and path cells return coordinates with keys",
    "[geometry][octree]")
{
    GDProp prop;
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
    W8PathingService pathing;
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

TEST_CASE("Mesh permutations keep GLM channels corners shades and active polygons aligned",
          "[geometry][rendering]")
{
    srMeshModel mesh(2, 3);
    mesh.setPassCount(2);
    const std::array<glm::vec3, 3> positions{{{1, 0, 0}, {0, 2, 0}, {0, 0, 3}}};
    std::ranges::copy(positions, mesh.getVertexLoc());
    auto* triangles = mesh.getPolyVertex();
    triangles[0] = {0, 1, 2};
    triangles[1] = {2, 0, 1};
    auto* shades = mesh.getVertexShadeIndex(1);
    shades[0] = shades[1] = 0;
    shades[2] = 2;
    mesh.setActivePolygonCount(1);
    mesh.getActivePolygonTable(1)[0] = 0;
    for (int pass = 0; pass < 2; ++pass) {
        auto* uv = mesh.getPolyUVIndex(pass, 1);
        uv[0] = {0, 7, 2}; // Separate UV indices outside the vertex range remain intact.
        uv[1] = {1, 2, 0};
        for (int i = 0; i < 3; ++i) {
            mesh.getVertexDIG(pass, 1)[i] = glm::vec3(10 * pass + i);
            mesh.getVertexDCG(pass, 1)[i] = glm::vec4(20 * pass + i);
            mesh.getVertexSCG(pass, 1)[i] = glm::vec4(30 * pass + i);
            for (int layer = 0; layer < 2; ++layer)
                mesh.getVertexTexCoords(pass, layer, 1)[i] = glm::vec2(40 * pass + 5 * layer + i);
        }
    }
    const std::array<w8_ulong, 2> polygon_order{1, 0};
    const std::array<w8_ulong, 3> vertex_order{2, 0, 1};
    mesh.reindexPolygons(polygon_order);
    mesh.reindexVertices(vertex_order);
    REQUIRE(mesh.getActivePolygonTable(0)[0] == 1);
    REQUIRE(triangles[0].x == 0);
    REQUIRE(triangles[0].y == 1);
    REQUIRE(triangles[0].z == 2);
    REQUIRE(triangles[1].x == 1);
    REQUIRE(triangles[1].y == 2);
    REQUIRE(triangles[1].z == 0);
    REQUIRE(shades[0] == 0);
    REQUIRE(shades[1] == 1);
    REQUIRE(shades[2] == 1);
    for (int pass = 0; pass < 2; ++pass) {
        const auto* uv = mesh.getPolyUVIndex(pass, 0);
        REQUIRE(uv[1].x == 1);
        REQUIRE(uv[1].y == 7);
        REQUIRE(uv[1].z == 0);
        for (int i = 0; i < 3; ++i) {
            REQUIRE(mesh.getVertexLoc()[i] == positions[vertex_order[i]]);
            REQUIRE(mesh.getVertexDIG(pass, 0)[i] == glm::vec3(10 * pass + vertex_order[i]));
            REQUIRE(mesh.getVertexDCG(pass, 0)[i] == glm::vec4(20 * pass + vertex_order[i]));
            REQUIRE(mesh.getVertexSCG(pass, 0)[i] == glm::vec4(30 * pass + vertex_order[i]));
            for (int layer = 0; layer < 2; ++layer)
                REQUIRE(mesh.getVertexTexCoords(pass, layer, 0)[i] ==
                        glm::vec2(40 * pass + 5 * layer + vertex_order[i]));
        }
    }
    const std::array<w8_ulong, 3> duplicate{0, 0, 2};
    REQUIRE_THROWS_AS(mesh.reindexVertices(duplicate), std::invalid_argument);
    REQUIRE_THROWS_AS(mesh.reindexVertices(polygon_order), std::invalid_argument);
    REQUIRE_THROWS_AS(mesh.reindexPolygons(vertex_order), std::invalid_argument);
    REQUIRE(mesh.getVertexLoc()[0] == positions[2]); // Invalid permutations change no tables.
}

TEST_CASE("Model-view scopes reject overflow without taking another scope's stack entry",
          "[geometry][rendering]")
{
    srGERD renderer;
    renderer.matrixMode(srGERD::MATRIX_MODELVIEW);
    renderer.loadIdentity();
    std::vector<srModelViewScope> transforms;
    transforms.reserve(32);
    for (int i = 0; i < 32; ++i) {
        transforms.emplace_back(renderer);
        renderer.translate(1, 0, 0);
    }
    REQUIRE_THROWS_AS(srModelViewScope(renderer), std::overflow_error);
    REQUIRE(renderer.getMatrix()[3].x == 32);
    while (!transforms.empty()) {
        transforms.pop_back();
        REQUIRE(renderer.getMatrix()[3].x == static_cast<float>(transforms.size()));
    }
}

TEST_CASE("Linked main and highlight draws share the instance termination boundary",
          "[geometry][rendering]")
{
    struct ProbeMesh : stMeshModel {
        ProbeMesh() : stMeshModel(0, 0) {}
        int draws = 0;
        int fail_on = 0;
        MeshResources<srTexture> textures;
        TriMesh getTriMesh() override
        {
            if (++draws == fail_on) throw std::runtime_error("mesh acquisition failed");
            TriMesh mesh;
            mesh.poly_textures[0][0] = &textures; // A table enables the highlight path.
            return mesh;
        }
    };
    srGERD renderer;
    auto model = std::make_shared<ProbeMesh>();
    auto part = std::make_unique<ProbeMesh>();
    auto& middle = *part;
    model->AddPart(std::move(part));
    part = std::make_unique<ProbeMesh>();
    auto& last = *part;
    model->AddPart(std::move(part));
    model->enable(srMeshModel::CONTROL_SKIP_AUTO_SPHERE);
    stModelInstance instance(nullptr);
    instance.setModel(model);
    instance.highlight_colour = {1, 0, 0, 1};
    srNode termination(&instance);
    termination.setFlag(srNode::FLAG_TERMINATE);
    const glm::vec4 ambient(0.2f, 0.3f, 0.4f, 1);
    renderer.setAmbientLight(ambient);
    renderer.setWinding(srGERD::WINDING_POSITIONAL_0);
    instance.RenderMeshes(renderer);
    REQUIRE(model->draws == 2);
    REQUIRE(middle.draws == 2);
    REQUIRE(last.draws == 0);
    REQUIRE(renderer.getAmbientLight() == ambient);
    model->draws = 0;
    model->fail_on = 2; // Fail after entering the reversed highlight pass.
    REQUIRE_THROWS_AS(instance.RenderMeshes(renderer), std::runtime_error);
    REQUIRE(renderer.getWinding() == srGERD::WINDING_POSITIONAL_0);
    REQUIRE(renderer.getAmbientLight() == ambient);
}

TEST_CASE("Failed instance materials restore picking lighting and material overrides",
          "[geometry][rendering]")
{
    struct FailingMaterial : srMaterial {
        void preProcess(srVertexPipe&) override { throw std::runtime_error("material failed"); }
    };
    srGERD renderer;
    renderer.setWinding(srGERD::WINDING_POSITIONAL_0);
    auto model = std::make_shared<stMeshModel>(1, 3);
    model->getVertexLoc()[0] = {-0.5f, -0.5f, 0.5f};
    model->getVertexLoc()[1] = {0.5f, -0.5f, 0.5f};
    model->getVertexLoc()[2] = {0, 0.5f, 0.5f};
    model->getPolyVertex()[0] = {0, 2, 1};
    model->setMaterial(std::make_shared<FailingMaterial>(), 0, srMeshModel::SIDE_FRONT);
    model->enable(srMeshModel::CONTROL_SKIP_AUTO_SPHERE);
    stModelInstance instance(nullptr);
    instance.setModel(model);
    instance.render_flags = stModelInstance::RENDER_NO_PICK;
    instance.diffuse_scale_enabled = true;
    instance.diffuse_scale = 0.75f;
    instance.emissive_override_enabled = true;
    instance.emissive_override = 0.25f;
    const auto diffuse_enabled = g_material_diffuse_scale_enabled;
    const auto emissive_enabled = g_material_emissive_override_enabled;
    const auto diffuse = g_material_diffuse_scale;
    const auto emissive = g_material_emissive_override;
    const glm::vec4 ambient(0.2f, 0.3f, 0.4f, 1);
    renderer.setAmbientLight(ambient);
    renderer.pushPick({{0, 0, 1}, nullptr, 7});
    REQUIRE_THROWS_AS(instance.RenderMeshes(renderer), std::runtime_error);
    REQUIRE(renderer.getAmbientLight() == ambient);
    REQUIRE(g_material_diffuse_scale_enabled == diffuse_enabled);
    REQUIRE(g_material_emissive_override_enabled == emissive_enabled);
    REQUIRE(g_material_diffuse_scale == diffuse);
    REQUIRE(g_material_emissive_override == emissive);
    REQUIRE_FALSE(renderer.isPickStackEmpty());
    const auto pick = renderer.popPick();
    REQUIRE(pick);
    REQUIRE(pick->triangle_index == 7);
}

TEST_CASE("Model render scopes restore GLM transforms and exclusions after a failed draw",
          "[geometry][rendering]")
{
    struct Probe : srModel {
        bool fail = false;
        w8_ulong mask = 0;
        glm::mat4 transform{1};
        Bounds getBounds() override { return {}; }
        void render(srGERD& renderer) override
        {
            mask = renderer.getExclusionMask();
            transform = renderer.getMatrix(srGERD::MATRIX_MODELVIEW);
            renderer.matrixMode(srGERD::MATRIX_PROJECTION);
            if (fail) throw std::runtime_error("injected model draw failure");
        }
    };
    srGERD renderer;
    auto model = std::make_shared<Probe>();
    srModelInstance instance;
    instance.setModel(model);
    instance.setLocation(3, 4, 5);
    instance.setExclusionMask(4);
    const glm::mat4 view = glm::translate(glm::mat4(1), glm::vec3(7, 8, 9));
    for (bool aligned : {false, true}) {
        instance.setAlignment(aligned);
        for (bool fail : {false, true}) {
            renderer.matrixMode(srGERD::MATRIX_MODELVIEW);
            renderer.loadMatrix(view);
            renderer.setExclusionMask(16);
            model->fail = fail;
            const srNode::ProcessInfo info{&renderer};
            if (fail)
                REQUIRE_THROWS_AS(instance.process(info, srNode::PROCESS_RENDER),
                                  std::runtime_error);
            else
                instance.process(info, srNode::PROCESS_RENDER);
            REQUIRE(model->mask == 20);
            REQUIRE(renderer.getExclusionMask() == 16);
            REQUIRE(renderer.getMatrixMode() == srGERD::MATRIX_MODELVIEW);
            REQUIRE(renderer.getMatrix(srGERD::MATRIX_MODELVIEW) == view);
            REQUIRE(model->transform != view);
        }
    }
}

TEST_CASE("Native octree trace reset snapshots aliased endpoints and clears hit state",
          "[geometry][octree]")
{
    W8OctreeTrace trace;
    REQUIRE(trace.start == glm::vec3(0));
    REQUIRE(trace.step == glm::vec3(0));
    REQUIRE(trace.hit_limit == 1.0e20f);
    trace.reset({1, 2, 3}, {4, 6, 3});
    REQUIRE(trace.length == 5);
    REQUIRE(trace.hit_limit == 5);
    REQUIRE(trace.step == glm::vec3(0.6f, 0.8f, 0));
    trace.state = 7;
    trace.hit_limit = 2;
    trace.reset(trace.end, trace.start);
    REQUIRE(trace.start == glm::vec3(4, 6, 3));
    REQUIRE(trace.end == glm::vec3(1, 2, 3));
    REQUIRE(trace.step == glm::vec3(-0.6f, -0.8f, 0));
    REQUIRE(trace.hit_limit == 5);
    REQUIRE(trace.state == 0);
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
    srCamera camera;
    camera.setLocation(glm::dvec3(3, -7, 11));
    const auto rotation = glm::mat3(glm::rotate(glm::mat4(1), .6f, glm::vec3(0, 1, 0)));
    camera.setRotation(rotation);
    camera.setFOV(1.2, 1.5);
    W8OctreeView view;
    view.far_clip = 123;
    view.captureCamera(camera);
    REQUIRE(view.camera_location == glm::vec3(3, -7, 11));
    REQUIRE(view.rotation_column0 == rotation[0]);
    REQUIRE(view.rotation_column1 == rotation[1]);
    REQUIRE(view.camera_dof == glm::vec3(camera.getWorldSpaceDOF()));
    REQUIRE(view.horizontal_fov == static_cast<float>(camera.getHorizontalFOV()));
    REQUIRE(view.vertical_fov == static_cast<float>(camera.getVerticalFOV()));
    REQUIRE(view.horizontal_fov_cosine == static_cast<float>(std::cos(view.horizontal_fov)));
    REQUIRE(view.far_clip == 123);

    const srBoundingBox bounds{{-1, -1, -1}, {1, 1, 1}};
    std::array<glm::vec3, 8> corners;
    for (unsigned i = 0; i < corners.size(); ++i) corners[i] = bounds.corner(i);
    const auto points = sortedFrustumCorners(corners);
    const auto packed = BuildFrustumPlanes(points);
    for (unsigned i = 0; i < packed.size(); ++i)
        view.frustum_planes[i] = {packed[i].normal, packed[i].w};
    REQUIRE(srPlaneMath::containsSphere(view.frustum_planes, glm::vec3(0), 0));
    REQUIRE(srPlaneMath::containsSphere(view.frustum_planes, glm::vec3(2, 0, 0), 1));
    REQUIRE_FALSE(srPlaneMath::containsSphere(view.frustum_planes, glm::vec3(2, 0, 0), .99f));
    REQUIRE(SphereInsideFrustum({2, 0, 0}, 1, packed));

    view = {};
    REQUIRE(view.camera_location == glm::vec3(0));
    REQUIRE(view.frustum_planes[5] == glm::vec4(0));
}

TEST_CASE("Projected triangle queries retain visibility and collision boundary rules", "[geometry]")
{
    using srTriangleProjection::Boundary;
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
            srTriangleProjection::contains(triangle, axis, point(.5f, .5f), Boundary::visibility));
        REQUIRE(
            srTriangleProjection::contains(triangle, axis, point(.5f, .5f), Boundary::collision));
        REQUIRE_FALSE(
            srTriangleProjection::contains(triangle, axis, point(.5f, 0), Boundary::visibility));
        REQUIRE(srTriangleProjection::contains(triangle, axis, point(.5f, 0), Boundary::collision));
        REQUIRE(srTriangleProjection::contains(triangle, axis, point(1, 1), Boundary::visibility));
        REQUIRE_FALSE(
            srTriangleProjection::contains(triangle, axis, point(1, 1), Boundary::collision));
        REQUIRE_FALSE(
            srTriangleProjection::contains(triangle, axis, point(3, 3), Boundary::collision));
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
    const auto hit = surface.segmentContact(from, {-5, 0, 0}, triangle);
    REQUIRE(hit);
    REQUIRE(glm::length(hit->point - glm::vec3(0, .5f, .5f)) < 1e-6f);
    REQUIRE(std::abs(hit->distance - 3) < 1e-6f);
    REQUIRE(surface.distance == 0);
    surface.distance = 9;
    REQUIRE_FALSE(surface.segmentContact({3, 4, 4}, {-5, 0, 0}, triangle));
    REQUIRE(surface.distance == 9);
    surface.flags = W8_GD_SURFACE_WALKABLE;
    const auto limit = surface.edgeContactLimit({0, .5f, -.2f}, triangle, 1);
    REQUIRE(limit);
    REQUIRE(std::abs(*limit - std::sqrt(.96f)) < 1e-6f);
    REQUIRE_FALSE(surface.edgeContactLimit({0, 4, 4}, triangle, 1));

    W8LevelDataRecord level{};
    const struct RestoreLevel {
        W8LevelDataRecord* previous;
        ~RestoreLevel() { g_level_data = previous; }
    } restore{g_level_data};
    g_level_data = &level;
    surface.flags = W8_GD_SURFACE_CROSSING;
    surface.contact_margin = 0;
    const auto crossing = surface.segmentContact(from, {-5, 0, 0}, triangle);
    REQUIRE(crossing);
    REQUIRE(crossing->point == from);
    REQUIRE(std::abs(crossing->distance - 3) < 1e-6f);
    REQUIRE(surface.distance > 0);
    const glm::vec3 reverse_from(-2, .5f, .5f);
    const auto reverse = surface.segmentContact(reverse_from, {5, 0, 0}, triangle);
    REQUIRE(reverse);
    REQUIRE(reverse->point == reverse_from);
    // Reverse crossings retain the full segment when the signed span is below one.
    REQUIRE(std::abs(reverse->distance - 5) < 1e-6f);
    REQUIRE(surface.distance < 0);
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
    W8LevelDataRecord level;
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
        W8EnvironRecord* environ = g_environ;
        W8LevelDataRecord* level = g_level_data;
        unsigned int* flags = g_level_flags;
        W8GameTimeAccumulator* timer = g_game_time_accumulator;
        W8GameData* octree = g_octree_game_data;
        bool resume = g_level_motion_resume_pending;
        ~RestoreRuntime()
        {
            g_environ = environ;
            g_level_data = level;
            g_level_flags = flags;
            g_game_time_accumulator = timer;
            g_octree_game_data = octree;
            g_level_motion_resume_pending = resume;
        }
    } restore;
    g_environ = nullptr;
    g_level_data = nullptr;
    g_game_time_accumulator = nullptr;
    W8GameData data(nullptr, true);
    g_game_time_accumulator = new W8GameTimeAccumulator;
    g_game_time_accumulator->m_flags.paused = true;
    g_level_data = new W8LevelDataRecord;
    g_level_motion_resume_pending = false;
    const auto basis = glm::mat3(glm::rotate(glm::mat4(1), .4f, glm::vec3(0, 1, 0)));
    const auto orientation = data.cameraRotation(W8_CAMERA_MOTION_TURN_LEFT, basis);
    REQUIRE(orientation.rotation == basis);
    REQUIRE(orientation.movement_basis == basis);
    const auto motion = data.cameraMotion(W8_CAMERA_MOTION_FORWARD, {1, 2, 3}, basis);
    REQUIRE_FALSE(motion.moved);
    REQUIRE(motion.displacement == glm::vec3(0));
    g_level_motion_resume_pending = true;
    g_game_time_accumulator->m_flags.raw_time = true;
    REQUIRE(data.cameraMotion(W8_CAMERA_MOTION_FORWARD, {1, 2, 3}, basis).displacement ==
            glm::vec3(0));
}

TEST_CASE("Environment ambient scales scenes and lights while retaining the source colour",
          "[geometry]")
{
    const struct RestoreLights {
        std::vector<stLight*> lights = g_environment_lights;
        ~RestoreLights() { g_environment_lights = lights; }
    } restore;
    stLight light(nullptr);
    g_environment_lights = {&light};
    W8World world{};
    world.static_scene = std::make_unique<srScene>();
    auto& scene = *world.static_scene;
    const glm::vec3 colour(-0.5f, 0.5f, 2.0f);
    ApplyEnvironmentColour(&world, 0.75f, &colour);
    REQUIRE(scene.getAmbientLight() == glm::vec3(0, 0.375f, 1));
    REQUIRE(light.ambient == scene.getAmbientLight());
    REQUIRE(world.environment_colour == colour);
    REQUIRE(world.environment_intensity == 0.75f);
    SetWorldEnvironmentIntensity(&world, 0.5f);
    REQUIRE(scene.getAmbientLight() == glm::vec3(0, 0.25f, 1));
    REQUIRE(light.ambient == scene.getAmbientLight());
    REQUIRE(world.environment_colour == colour);
    world.static_scene = nullptr;
    glm::vec3 ambient(1);
    GetWorldLightValue(&world, &ambient);
    REQUIRE(ambient == glm::vec3(0));
}

TEST_CASE("Positional sound copies register each live node once", "[scene][sound]")
{
    const auto initial = g_sound3d_instances.size();
    {
        stSound3D source("source.wav", nullptr);
        stSound3D assigned("assigned.wav", nullptr);
        assigned = source;
        assigned = source;
        stSound3D copied(source);
        auto owned = std::make_unique<stSound3D>("service.wav", nullptr);
        auto* borrowed = Retain3DSound(std::move(owned));
        REQUIRE(borrowed->getParent() == nullptr);
        REQUIRE(g_sound3d_instances.size() == initial + 4);
        Release3DSounds();
        Release3DSounds();
        REQUIRE(g_sound3d_instances.size() == initial + 3);
        for (auto* node : {&source, &assigned, &copied})
            REQUIRE(std::ranges::count(g_sound3d_instances, node) == 1);
    }
    REQUIRE(g_sound3d_instances.size() == initial);
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
    const std::weak_ptr<srMaterial> weak = different.material;
    different = {};
    REQUIRE(weak.expired());
    std::fill(std::begin(record.texture_names[0]), std::end(record.texture_names[0]), 'x');
    REQUIRE_THROWS(LoadMaterial("", record));
}

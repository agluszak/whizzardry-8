#include "glm/matrix.hpp"
#include "surrender/plane_math.h"
#include "wiz8/ground_shadow.h"
#include "wiz8/layouts/world.h"
#include "wiz8/engine_code/Camera.h"
#include "wiz8/engine_code/LevelFile.h"
#include "wiz8/surface_geometry.h"
#include "wiz8/navigation_flags.h"
#include "wiz8/engine_code/GDProp.h"
#include <stdexcept>
#include "wiz8/engine_code/mesh_links.h"
#include "surrender/srClipPlane.h"
#include "surrender/srIlluminator.h"
#include "surrender/srLight.h"
#include <optional>
#include <unordered_map>
#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/engine_code/Monster.h"
#include "glm/ext/vector_double3.hpp"
#include "wiz8/engine_code/particle_modes.h"
#include "wiz8/engine_code/stParticle.h"
#include "wiz8/engine_code/game_timer.h"
#include "surrender/srClock.h"
#include "glm/ext/vector_int3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "wiz8/octree_walk.h"
#include "wiz8/engine_code/ReadMesh.h"
#include "wiz8/engine_code/UpdateMesh.h"
#include "glm/ext/vector_int2.hpp"
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include <utility>

#include "glm/geometric.hpp"
#include "glm/ext/vector_float2.hpp"
#include "surrender_test_fixture.h"
#include "surrender/srBoundingBox.h"
#include "surrender/srBounder.h"
#include "surrender/model_view_scope.h"
#include "surrender/srMaterial.h"
#include "surrender/srVertexPipe.h"
#include "surrender/srModeler.h"
#include "surrender/srTriMeshPipeline.h"
#include "surrender/srGERD.h"
#include "surrender/srCamera.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "wiz8/engine_code/AnimRep.hpp"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/World.h"
#include "surrender/srTriangleCuller.h"
#include "surrender/srDD_SDLGPU.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/AnimObj.h"
#include "wiz8/engine_code/AniMesh.h"
#include "wiz8/engine_code/Emitter.h"
#include "wiz8/engine_code/PathAI.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/engine_code/materials.h"
#include "surrender/srNode.h"
#include "surrender/srModel.h"
#include "surrender/srModelInstance.h"
#include "wiz8/direction_angles.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Item.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/OctBuildPreTree.h"
#include "wiz8/engine_code/OctBuildTree.h"
#include "wiz8/engine_code/oct_build_traversal.h"
#include "wiz8/engine_code/OctPreTree.h"
#include "wiz8/engine_code/Navigator.h"
#include "wiz8/engine_code/SoundEvent.h"
#include "wiz8/local_code/ItemManager.h"
#include "compat/compiler.h"
#include "geometry.h"
#include "surrender/srMath.h"
#include "surrender/segment_math.h"
#include "surrender/srMeshModel.h"
#include "surrender/srShader.h"
#include "glm/fwd.hpp"


static bool close(const glm::vec3& actual, const glm::vec3& expected, float tolerance = 1e-4f)
{
    return glm::length(actual - expected) < tolerance;
}

static void bounds()
{
    const auto empty = boundingBox(std::span<const glm::vec3>{});
    REQUIRE((empty.minimum == glm::vec3(0) && empty.maximum == glm::vec3(0)));
    const std::array<srVector3, 3> points{{{-4, 2, -3}, {-1, 8, -7}, {-2, 5, -1}}};
    const auto box = boundingBox(std::span<const srVector3>(points));
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
    const srVector3 translation(10, 20, 30), scale(-2, 3, .5f);
    srVector3 minimum(-1, -2, -3), maximum(4, 5, 6);
    TransformBounds(&quarter_turn, &translation, &scale, &minimum, &maximum);
    REQUIRE(minimum == srVector3(-24, 57, 13.5f));
    REQUIRE(maximum == srVector3(-10, 72, 18));
    minimum = srVector3(0, 0, 0);
    maximum = srVector3(0, 0, 0);
    TransformBounds(&quarter_turn, &translation, &scale, &minimum, &maximum);
    REQUIRE((minimum == srVector3(-20, 60, 15) && maximum == minimum));
}

static void planes_and_segments()
{
    const srVector3 a(0, 2, 0), b(0, 2, 3), c(4, 2, 0);
    const auto ground = probeGround(a.glmValue());
    REQUIRE((ground.position == a.glmValue() && !ground.hit));
    const auto surface = groundSurfaceInfo(a.glmValue());
    REQUIRE(surface.height == 2);
    REQUIRE((surface.surface == 0 && surface.material == 0));
    W8Plane plane{};
    plane = trianglePlane(a.glmValue(), b.glmValue(), c.glmValue());
    REQUIRE(close(plane.normal.glmValue(), {0, 1, 0}));
    REQUIRE(std::abs(SignedPlaneDistance(plane, {5, 4, 7}) - 2) < 1e-5f);
    plane = trianglePlane(a.glmValue(), a.glmValue(), a.glmValue());
    REQUIRE((plane.normal == srVector3(0, 0, 0) && plane.w == 0));

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
    const auto degenerate_planar = srSegmentMath::project(glm::vec2(0, 3), planar_from, planar_from);
    REQUIRE(degenerate.distance == 5);
    REQUIRE((degenerate.point == segment_from && degenerate.fraction == 0));
    REQUIRE(degenerate_planar.distance == 3);
    REQUIRE((degenerate_planar.point == planar_from && degenerate_planar.fraction == 0));

    W8EnvironRecord environment;
    environment.scale = 2;
    environment.airborne = 0;
    environment.SetScaledMotion(glm::vec3(4, 6, 8));
    REQUIRE((environment.vector == srVector3(2, 3, 4) && environment.airborne == 1));

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
    srVector3 position(1, 2, 3);
    OffsetPositionByYawPitch(10, &position, glm::half_pi<float>(), 0);
    REQUIRE(close(position.glmValue(), {11, 2, 3}));
    OffsetPositionByYawPitch(5, &position, 0, glm::half_pi<float>());
    REQUIRE(close(position.glmValue(), {11, -3, 3}));

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
    REQUIRE(empty_state.position == srVector3(0, 0, 0));
    REQUIRE(live_location == glm::vec3(3, 4, 5));
    REQUIRE(node_location == glm::vec3(1.25f, -2.5f, 3.75f));
    REQUIRE(state.position.glmValue() == node_location);
    REQUIRE((state.yaw[0] == .25f && state.pitch[0] == -.5f));
    for (int index = 1; index < 6; ++index)
        REQUIRE((state.yaw[index] == 0 && state.pitch[index] == 0));
    REQUIRE(rotation == glm::mat3(1));
    camera.SetOrientation(glm::half_pi<float>(), 0);
    srVector3 forward;
    camera.GetForwardPoint(10, &forward);
    REQUIRE(close(forward.glmValue() - camera.m_position, {10, 0, 0}));
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

static void procedural_meshes()
{
    srModeler::Vertex a, b, midpoint;
    a.position = {0, 2, 4};
    b.position = {2, 4, 8};
    a.dcg[2] = {0.2f, 0.4f, 0.6f};
    b.dcg[2] = {0.4f, 0.8f, 1};
    a.dig[2] = {1, 2, 3};
    b.dig[2] = {3, 4, 5};
    a.scg[2] = {2, 4, 6};
    b.scg[2] = {4, 6, 8};
    a.weights[2] = 0;
    b.weights[2] = 1;
    a.uv[5] = {2, 4};
    b.uv[5] = {6, 8};
    midpoint.interpolate(a, b, 0.5f);
    REQUIRE((close(midpoint.position, {1, 3, 6})));
    REQUIRE((close(midpoint.dcg[2], {0.3f, 0.6f, 0.8f})));
    REQUIRE((close(midpoint.dig[2], {2, 3, 4})));
    REQUIRE((close(midpoint.scg[2], {3, 5, 7})));
    REQUIRE((midpoint.weights[2] == 0.5f && midpoint.uv[5] == glm::vec2(4, 6)));

    srModeler modeler;
    srModeler::Triangle triangle;
    modeler.createGrid(2, 3);
    REQUIRE((modeler.getTriangleCount() == 12));
    REQUIRE((std::abs(modeler.getMaxVertexDist() - std::sqrt(0.5)) < 1e-5));
    modeler.planarMap(0, 0, {});
    for (w8_ulong i = 0; i < modeler.getTriangleCount(); ++i) {
        triangle = *modeler.getTriangle(i);
        for (const auto& vertex : triangle.vertices)
            REQUIRE((glm::length(vertex.uv[0] - glm::vec2(vertex.position.x + 0.5f,
                                                       0.5f - vertex.position.y)) < 1e-5f));
        const auto normal =
            glm::cross(triangle.vertices[1].position - triangle.vertices[0].position,
                       triangle.vertices[2].position - triangle.vertices[0].position);
        REQUIRE((normal.z < 0));
    }
    modeler.autoSmooth(0.3, 1);
    srModeler::Triangle first, second;
    first = *modeler.getTriangle(0);
    second = *modeler.getTriangle(1);
    REQUIRE(((first.flags & second.flags) != 0));
    second.shaders[0].value ^= 1;
    modeler.setTriangle(1, second);
    modeler.autoSmooth(0.3, 0);
    first = *modeler.getTriangle(0);
    second = *modeler.getTriangle(1);
    REQUIRE(((first.flags & second.flags) == 0));
    modeler.addFromModeler(modeler);
    REQUIRE((modeler.getTriangleCount() == 24));
    modeler.scale({2, 3, 1});
    modeler.move({10, 20, 30});
    REQUIRE((modeler.getAxialBounds(srModeler::AXIS_X) == std::pair{9.f, 11.f}));

    REQUIRE_FALSE(modeler.getTriangle(modeler.getTriangleCount()).has_value());

    modeler.createSphere(8);
    REQUIRE((modeler.getTriangleCount() == 128));
    for (w8_ulong i = 0; i < modeler.getTriangleCount(); ++i) {
        triangle = *modeler.getTriangle(i);
        for (const auto& vertex : triangle.vertices)
            REQUIRE((std::abs(glm::length(vertex.position) - 0.5f) < 1e-5f));
        const auto normal =
            glm::cross(triangle.vertices[1].position - triangle.vertices[0].position,
                       triangle.vertices[2].position - triangle.vertices[0].position);
        REQUIRE((glm::dot(normal, triangle.vertices[0].position) >= -1e-7f));
    }
    srMeshModel sphere(0, 0);
    modeler.convert(sphere, 1);
    REQUIRE((sphere.vertex_location_count == 58 && sphere.polygon_count == 112));
    modeler.createTorus(8, 6, 0.4);
    REQUIRE((modeler.getTriangleCount() == 96));
    for (w8_ulong i = 0; i < modeler.getTriangleCount(); ++i) {
        triangle = *modeler.getTriangle(i);
        for (const auto& vertex : triangle.vertices) {
            const float ring = glm::length(glm::vec2(vertex.position)) - 0.5f;
            REQUIRE((std::abs(ring * ring + vertex.position.z * vertex.position.z - 0.04f) < 1e-5f));
        }
    }
    srMeshModel torus(0, 0);
    modeler.convert(torus, 1);
    REQUIRE((torus.vertex_location_count == 48 && torus.polygon_count == 96));
    modeler.discard();
    triangle = srModeler::Triangle{};
    triangle.vertices[0].position = {0, 0, 0};
    triangle.vertices[1].position = {2, 0, 0};
    triangle.vertices[2].position = {0, 2, 0};
    triangle.vertices[1].dcg[0] = {0.2f, 0.4f, 0.6f};
    triangle.vertices[1].dig[0] = {2, 3, 4};
    triangle.vertices[1].scg[0] = {0.5f, 0.6f, 0.7f};
    triangle.vertices[1].weights[0] = 0.25f;
    triangle.vertices[1].uv[0] = {1, 0};
    modeler.addTriangle(triangle);
    modeler.tesselateEdges(1.1);
    double area = 0;
    for (w8_ulong i = 0; i < modeler.getTriangleCount(); ++i) {
        triangle = *modeler.getTriangle(i);
        for (int edge = 0; edge < 3; ++edge)
            REQUIRE((glm::distance(triangle.vertices[edge].position,
                                triangle.vertices[(edge + 1) % 3].position) <= 1.10001f));
        area +=
            glm::length(glm::cross(triangle.vertices[1].position - triangle.vertices[0].position,
                                   triangle.vertices[2].position - triangle.vertices[0].position)) *
            0.5;
    }
    REQUIRE((std::abs(area - 2) < 1e-5));
    srMeshModel mesh(0, 0);
    modeler.convert(mesh, 1);
    REQUIRE((mesh.polygon_count == modeler.getTriangleCount()));
    const auto index = mesh.findClosestVertex({2, 0, 0});
    REQUIRE((mesh.getVertexLoc()[index] == glm::vec3(2, 0, 0)));
    REQUIRE((mesh.getVertexDCG(0, 0)[index] == glm::vec4(0.2f, 0.4f, 0.6f, 0.25f)));
    REQUIRE((mesh.getVertexDIG(0, 0)[index] == glm::vec3(2, 3, 4)));
    REQUIRE((mesh.getVertexSCG(0, 0)[index] == glm::vec4(0.5f, 0.6f, 0.7f, 1)));
    REQUIRE((mesh.getVertexTexCoords(0, 0, 0)[index] == glm::vec2(1, 0)));

    modeler.discard();
    modeler.autoSmooth(0.3, 1); // Empty models have no adjacency graph.
    modeler.addTriangle(srModeler::Triangle{});
    modeler.convert(mesh, 0);
    REQUIRE((mesh.vertex_location_count == 1 && mesh.polygon_count == 1));
    REQUIRE((mesh.getMaxRadius() == 0));

    // Concave polygons triangulate in either winding and on every coordinate plane.
    const std::array<glm::vec2, 5> outline{{{0, 0}, {2, 0}, {2, 2}, {1, 1}, {0, 2}}};
    for (int omitted = 0; omitted < 3; ++omitted)
        for (bool reversed : {false, true}) {
            srModeler::Polygon polygon(5);
            const int u = omitted == 0 ? 1 : 0, v = omitted == 2 ? 1 : 2;
            for (int i = 0; i < 5; ++i) {
                const auto point = outline[reversed ? 4 - i : i];
                polygon.vertices[i].position[u] = point.x;
                polygon.vertices[i].position[v] = point.y;
            }
            modeler.discard();
            modeler.addPolygon(polygon);
            REQUIRE((modeler.getTriangleCount() == 3));
            area = 0;
            for (w8_ulong i = 0; i < modeler.getTriangleCount(); ++i) {
                triangle = *modeler.getTriangle(i);
                area += glm::length(glm::cross(
                            triangle.vertices[1].position - triangle.vertices[0].position,
                            triangle.vertices[2].position - triangle.vertices[0].position)) *
                        0.5;
            }
            REQUIRE((std::abs(area - 3) < 1e-5));
        }
}

static void renderer_bounds()
{
    srGERD renderer(srCreateSDLGPUDevice(), "bounds");
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
    mesh.render_control.value = 0;
    REQUIRE_FALSE(mesh.isVisible(renderer)); // Visible sphere, rejected box.
    mesh.enable(srMeshModel::CONTROL_SKIP_AUTO_BOX);
    REQUIRE(mesh.isVisible(renderer));
    mesh.setBounds({-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}, {3, 0, 0}, 0.1f);
    mesh.render_control.value = 0;
    REQUIRE_FALSE(mesh.isVisible(renderer)); // Rejected sphere, visible box.
    mesh.enable(srMeshModel::CONTROL_SKIP_AUTO_SPHERE);
    REQUIRE(mesh.isVisible(renderer));
    mesh.setBounds({2, 2, 2}, {3, 3, 3}, {3, 0, 0}, 0.1f);
    mesh.enable(srMeshModel::CONTROL_SKIP_AUTO_BOX);
    REQUIRE(mesh.isVisible(renderer));
    mesh.disable(srMeshModel::CONTROL_SKIP_AUTO_BOX);
    mesh.vertex_location_count = 7;
    REQUIRE(mesh.isVisible(renderer)); // Small meshes bypass box culling.

    renderer.loadMatrix(glm::scale(glm::translate(glm::mat4(1), glm::vec3(1, 2, 3)),
                                    glm::vec3(-2, 3, 4)));
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
    const std::array<srVector3i, 3> triangles{{{0, 1, 2}, {3, 4, 5}, {6, 7, 8}}};
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

TEST_CASE_METHOD(SurRenderTestFixture, "Procedural meshes", "[geometry]")
{
    procedural_meshes();
}

TEST_CASE_METHOD(SurRenderTestFixture, "Renderer bounds", "[geometry]")
{
    renderer_bounds();
}

TEST_CASE_METHOD(SurRenderTestFixture, "Triangle visibility", "[geometry]")
{
    triangle_visibility();
}

TEST_CASE_METHOD(SurRenderTestFixture, "Bounding volumes", "[geometry]")
{
    bounds();
}

TEST_CASE_METHOD(SurRenderTestFixture, "Planes and segments", "[geometry]")
{
    planes_and_segments();
}

TEST_CASE_METHOD(SurRenderTestFixture, "Cameras and throwing", "[geometry]")
{
    camera_and_throw();
}

TEST_CASE_METHOD(SurRenderTestFixture, "Linked mesh bounds", "[geometry]")
{
    linked_mesh_bounds();
}

TEST_CASE_METHOD(SurRenderTestFixture, "Model bounds are independent values", "[geometry]")
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

TEST_CASE_METHOD(SurRenderTestFixture, "Material query overrides do not mutate the cache", "[geometry]")
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

TEST_CASE_METHOD(SurRenderTestFixture, "Baked lighting", "[geometry]")
{
    baked_lighting();
}

TEST_CASE("navigator route growth, copying and movement")
{
    const srVector3 origin(0, 0, 0), end(3, 0, 4);
    W8NavigatorAttachment route(&origin, &end);
    REQUIRE((route.MeasurePathLength() == 5));
    route.InitializeSegment(&origin, &end);
    route.path_position_index = 1;
    for (unsigned short i = 1; i <= 35; ++i) {
        const srVector3 position(i, 0, 0);
        route.AppendPathPosition(&position, i);
    }
    REQUIRE((route.path_position_index == 36 && route.path.size() > 36));
    for (unsigned short i = 1; i <= 35; ++i) {
        REQUIRE((route.path[i].position == srVector3(i, 0, 0)));
        REQUIRE((route.path[i].value == i));
    }
    route.path[36] = {srVector3(36, 0, 0), 0};
    W8NavigatorAttachment copied;
    copied.path[0].position = srVector3(-1, 0, 0);
    copied.CopyPathFrom(&route);
    REQUIRE((copied.path[0].position == srVector3(-1, 0, 0)));
    REQUIRE((copied.path_position_index == route.path_position_index));
    for (unsigned i = 1; i <= 36; ++i) {
        REQUIRE((copied.path[i].position == route.path[i].position));
        REQUIRE((copied.path[i].value == route.path[i].value));
    }
    route.path[1].position = srVector3(99, 0, 0);
    REQUIRE((copied.path[1].position == srVector3(1, 0, 0)));
    copied.InitializeSegment(&origin, &end);
    for (const auto& entry : copied.path) REQUIRE((entry.value == 0));
    REQUIRE((copied.MeasurePathLength() == 5));
    srVector3 current = origin, direction;
    REQUIRE((!copied.AdvancePositionWithDirection(&current, 2, &direction)));
    REQUIRE((close(current.glmValue(), {1.2f, 0, 1.6f})));
    REQUIRE((copied.AdvancePositionWithDirection(&current, 4, &direction)));
    REQUIRE((current == end));

    // An append may borrow a position from the same storage being grown.
    W8NavigatorAttachment aliased(&origin, &end);
    aliased.path_position_index = 9;
    aliased.AppendPathPosition(&aliased.path[1].position, 7);
    REQUIRE((aliased.path[9].position == end && aliased.path[9].value == 7));
}

TEST_CASE("Empty octree build geometry cleanup", "[geometry]")
{
    // The preprocessing arrays reserve slot zero for a sentinel. No real
    // records exist when the count is one, so index one must not be read.
    W8OctPreTreeGeometry geometry{};
    geometry.vertex_count = 1;
    geometry.m_polygon_count = 1;
    geometry.m_vertices =
        static_cast<W8OctPreTreeVertex*>(std::calloc(1, sizeof(W8OctPreTreeVertex)));
    geometry.m_polygons =
        static_cast<W8OctRegionPolygon*>(std::calloc(1, sizeof(W8OctRegionPolygon)));
    geometry.owned = std::malloc(8);
    REQUIRE(geometry.m_vertices);
    REQUIRE(geometry.m_polygons);
    REQUIRE(geometry.owned);

    geometry.Release();
    REQUIRE(geometry.m_vertices == nullptr);
    REQUIRE(geometry.m_polygons == nullptr);
    REQUIRE(geometry.owned == nullptr);
    REQUIRE(geometry.vertex_count == 0);
    REQUIRE(geometry.m_polygon_count == 0);
    geometry.Release();
}

TEST_CASE("Octree face run rejects invalid growth capacity", "[geometry]")
{
    W8OctPreTreeGeometry geometry{};
    int* faces = nullptr;
    REQUIRE(geometry.CheckArrayLength(&faces, 0, 0) == 0);
    REQUIRE(faces == nullptr);
    REQUIRE(geometry.CheckArrayLength(&faces, 65535, 5) == 0);
    REQUIRE(faces == nullptr);
}

TEST_CASE("Inclusive cell boxes retain order clipping and extreme endpoints", "[geometry][octree]")
{
    std::vector<glm::ivec3> cells;
    const auto append = [&](const glm::ivec3& cell) { cells.push_back(cell); };
    visitOctreeCellBox({-1, 2, 3}, {0, 3, 4}, append);
    const std::vector<glm::ivec3> expected{{-1, 2, 3}, {-1, 2, 4}, {-1, 3, 3}, {-1, 3, 4},
                                         {0, 2, 3}, {0, 2, 4}, {0, 3, 3}, {0, 3, 4}};
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

TEST_CASE("Ground shadow projection retains rotated scale center and coordinate axis order", "[geometry][rendering]")
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

TEST_CASE_METHOD(SurRenderTestFixture, "Ground polygon queries return owned sorted runs beyond thirty faces", "[geometry][octree]")
{
    W8World world{};
    const struct RestoreWorld {
        W8World* previous;
        ~RestoreWorld() { g_world = previous; }
    } restore{g_world};
    g_world = &world;
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
    tree.m_leaf_lookup = static_cast<w8_ulong*>(std::calloc(1, sizeof(w8_ulong)));
    tree.m_leaf_lookup[0] = 1;
    tree.m_leaves = static_cast<W8OctPreTreeLeaf*>(std::calloc(2, sizeof(W8OctPreTreeLeaf)));
    tree.m_leaves[1].polygon_offset = 1;
    tree.m_polygon_index_stream = static_cast<w8_ulong*>(std::calloc(faces + 2, sizeof(w8_ulong)));
    tree.m_polygon_index_stream[1] = faces;
    tree.m_aulPolyLookup = static_cast<w8_ulong*>(std::calloc(faces + 1, sizeof(w8_ulong)));
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
    tree.m_visited_object_bits.assign(11000, false);
    const glm::ivec3 cell(0, 0, 0);
    for (int id = 1; id <= 10500; ++id)
        tree.object_registry->RegisterObjectCell(W8_OCTREE_KIND_LOCATION, id, cell);
    const srBoundingBox bounds{{0, 0, 0}, {999, 999, 999}};
    const auto dense = tree.queryObjects(bounds, W8_OCTREE_KIND_LOCATION, -1);
    REQUIRE(dense.size() == 10000);
    const auto original = dense;
    const auto nearby = tree.nearbyLocations({500, 500, 500}, 1);
    REQUIRE(nearby.size() == 10000);
    REQUIRE(nearby == dense);
    const auto absent = tree.queryObjects(bounds, W8_OCTREE_KIND_PROP, -1);
    REQUIRE(absent.empty());
    REQUIRE(dense == original);
    REQUIRE(nearby == original);

    tree.object_registry->by_cell->clear();
    tree.object_registry->by_object->clear();
    for (int id = 1; id <= 512; ++id)
        tree.object_registry->RegisterObjectCell(W8_OCTREE_KIND_LOCATION, id, cell);
    const auto locations = tree.locationsInBox(bounds, 7);
    REQUIRE(locations.size() == 511);
    REQUIRE(std::ranges::find(locations, 7u) == locations.end());
    REQUIRE(tree.locationsInBox(bounds, 0).size() == 512);
    REQUIRE(tree.locationsInBox({{2000, 2000, 2000}, {3000, 3000, 3000}}, 0).empty());
    REQUIRE(tree.locationsInBox({{999, 999, 999}, {0, 0, 0}}, 0).size() == 512);
}

TEST_CASE("Octree walks retain ordered crossings, early exits and coordinate rounding", "[geometry][octree]")
{
    W8Octree tree(nullptr, nullptr);
    tree.m_spatial.m_bounds.minimum = {0, 0, 0};
    tree.m_spatial.m_node_extent = 100;
    const std::vector<glm::ivec3> diagonal{
        {0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {1, 1, 1},
        {2, 1, 1}, {2, 1, 2}, {2, 2, 2}};
    for (int signs = 0; signs < 8; ++signs) {
        glm::vec3 from(25), to(225);
        for (int axis = 0; axis < 3; ++axis)
            if ((signs & (1 << axis)) != 0) { from[axis] = 275; to[axis] = 75; }
        const auto walk = tree.cellWalk(from, to);
        auto expected = diagonal;
        for (auto& cell : expected)
            for (int axis = 0; axis < 3; ++axis)
                if ((signs & (1 << axis)) != 0) cell[axis] = 2 - cell[axis];
        std::vector<glm::ivec3> visited;
        walk.visitCells([&](const glm::ivec3& cell) { visited.push_back(cell); return true; });
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
        walk.visitCells([&](const glm::ivec3& cell) { visited.push_back(cell); return true; });
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
    stationary.visitCells([&](const glm::ivec3& cell) { visited.push_back(cell); return true; });
    REQUIRE((visited == std::vector<glm::ivec3>{{1, 2, 3}}));

    // The preprocessing walk deliberately retains its world-quantized start.
    tree.m_spatial.m_node_extent = 100.123f;
    const glm::vec3 boundary{100.121f, 25, 25};
    auto walk = tree.cellWalk(boundary, {350, 25, 25});
    REQUIRE(walk.cell.x == 1);
    REQUIRE(tree.cellAt(boundary).x == 0);
    walk.cell = tree.cellAt(boundary);
    visited.clear();
    walk.visitCells([&](const glm::ivec3& cell) { visited.push_back(cell); return true; });
    REQUIRE(visited.front().x == 0);
}

TEST_CASE("Build-tree queries own uncapped ordered results and independent state", "[geometry][octree]")
{
    W8OctBuildTree tree(1000, std::nullopt, 16, 0);
    REQUIRE(tree.queryAlongSegment({0, 0, 0}, {0, 0, 0}, 1, 3).empty());
    tree.spatial.m_bounds.minimum = {0, 0, 0};
    tree.spatial.m_bounds.maximum = {100, 100, 100};
    tree.spatial.m_extent = 100;
    tree.spatial.m_root = new W8OctBuildNode;
    tree.spatial.m_root->leaf_kind = 1;
    std::vector<W8GDSurface> surfaces(10050);
    W8OctBuildLink* tail = nullptr;
    for (auto& surface : surfaces) {
        auto& link = tree.links.emplace_back(W8OctBuildLink{&surface, nullptr});
        if (tail) tail->next = &link;
        else tree.spatial.m_root->links[3] = &link;
        tail = &link;
    }
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
    other.spatial.m_root = new W8OctBuildNode;
    W8GDSurface separate{};
    other.AppendLink(other.spatial.m_root, &separate, 3);
    const auto other_hits = other.queryAlongSegment({50, 50, 50}, {0, 0, 0}, 100, 3);
    REQUIRE(other_hits == std::vector<W8GDSurface*>{&separate});
    REQUIRE(tree.queryAlongSegment({50, 50, 50}, {0, 0, 0}, 100, 3) == hits);
    REQUIRE(other.queryAlongSegment({500, 500, 500}, {0, 0, 0}, 1, 3).empty());
    REQUIRE(hits.front() == &surfaces.front());

    // Kind 9 preserves list-3 order, then appends unseen list-4 surfaces.
    tree.AppendLink(tree.spatial.m_root, &surfaces[0], 4);
    tree.AppendLink(tree.spatial.m_root, &separate, 4);
    surfaces[0].flags |= W8_GD_SURFACE_QUERY_VISITED;
    const auto mixed = tree.queryAlongSegment({50, 50, 50}, {0, 0, 0}, 100, 9);
    REQUIRE(mixed.size() == surfaces.size() + 1);
    REQUIRE(mixed.front() == &surfaces[1]);
    REQUIRE(mixed[mixed.size() - 2] == &surfaces[0]);
    REQUIRE(mixed.back() == &separate);
    REQUIRE((surfaces[0].flags & W8_GD_SURFACE_QUERY_VISITED) == 0);
}

TEST_CASE("Swept query coordinates retain negative-cell policy and complete offsets", "[geometry][octree]")
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

TEST_CASE("Spatial bounds are independent snapshots of working and clipped extents", "[geometry][octree]")
{
    W8OctSpatialState state;
    const srVector3 minimum(-30, -20, -10), maximum(10, 20, 30);
    state.setWorkingBounds({minimum.glmValue(), maximum.glmValue()});
    state.m_clipped_bounds.minimum = {-3, -2, -1};
    state.m_clipped_bounds.maximum = {1, 2, 3};
    const auto working = state.GetWorkingBounds();
    auto clipped = state.GetClippedBounds();
    REQUIRE(working.minimum == minimum.glmValue());
    REQUIRE(working.maximum == maximum.glmValue());
    REQUIRE(clipped.minimum == glm::vec3(-3, -2, -1));
    REQUIRE(clipped.maximum == glm::vec3(1, 2, 3));
    clipped.minimum = glm::vec3(0);
    REQUIRE(state.GetClippedBounds().minimum == glm::vec3(-3, -2, -1));
    state.Reset();
    REQUIRE(state.GetWorkingBounds().minimum == glm::vec3(0));
    REQUIRE(state.GetClippedBounds().maximum == glm::vec3(0));
    REQUIRE(working.minimum == minimum.glmValue());
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
    REQUIRE(close(search.glmValue(), {100, 207.92f, 300}));
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
        W8ViewportRect saved = g_viewport;
        ~RestoreViewport() { g_viewport = saved; }
    } restore;
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

TEST_CASE("Representation transform queries preserve local and parent coordinates", "[geometry][transform]")
{
    W8AnimRepBase rep;
    rep.parent_location = {10, 20, 30};
    const srVector3 local(1, 2, 3);
    rep.SetLocation(&local);
    const auto world = rep.GetLocation();
    const auto local_copy = rep.GetLocalLocation();
    auto rotation = rep.GetRotation();
    REQUIRE(world == srVector3(11, 22, 33));
    REQUIRE(local_copy == local);
    REQUIRE(rotation == glm::mat3(1));
    rotation[0][0] = 2;
    REQUIRE(rep.GetRotation() == glm::mat3(1));
    rep.SetRotation(&rotation);
    REQUIRE(rep.GetRotation() == rotation);
    const srVector3 next(-1, -2, -3);
    rep.SetLocation(&next);
    REQUIRE(rep.GetLocation() == srVector3(9, 18, 27));
    REQUIRE(world == srVector3(11, 22, 33));
    REQUIRE(local_copy == local);
}

TEST_CASE_METHOD(SurRenderTestFixture, "Navigator releases replaced and cleared paths", "[geometry][navigation]")
{
    struct TrackedPath : W8PathAI {
        explicit TrackedPath(int& releases) : releases(releases) {}
        ~TrackedPath() override { ++releases; }
        int& releases;
    };

    int releases = 0;
    {
        W8Navigator navigator;
        auto* first = new TrackedPath(releases);
        navigator.SetPathAI(first);
        navigator.SetPathAI(first);
        REQUIRE(releases == 0);
        REQUIRE(navigator.GetPathAI() == first);

        navigator.SetPathAI(new TrackedPath(releases));
        REQUIRE(releases == 1);
        navigator.SetNavigationMode(1);
        REQUIRE(releases == 2);
        REQUIRE(navigator.GetPathAI()->animated);
        navigator.SetPathAI(new TrackedPath(releases));
        navigator.SetPathAI(nullptr);
        REQUIRE(releases == 3);
        REQUIRE(navigator.GetPathAI() == nullptr);
        navigator.SetPathAI(nullptr);
        REQUIRE(releases == 3);

        navigator.SetPathAI(new TrackedPath(releases));
    }
    REQUIRE(releases == 4);
}

TEST_CASE_METHOD(SurRenderTestFixture, "Movement queries return velocity and waypoint snapshots", "[geometry][navigation]")
{
    W8Navigator navigator;
    navigator.movement.velocity = {1, 2, 3};
    const auto velocity = navigator.GetVelocity();
    navigator.movement.velocity = {4, 5, 6};
    REQUIRE(navigator.GetVelocity() == srVector3(4, 5, 6));
    REQUIRE(velocity == srVector3(1, 2, 3));

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
    REQUIRE(waypoint == srVector3(4, 5, 6));
    route.path[0].position = {10, 11, 12};
    REQUIRE(waypoint == srVector3(4, 5, 6));
    route.path_cursor = route.path_position_index;
    REQUIRE(route.GetNextPosition() == route.path_destination);
    REQUIRE(start == srVector3(1, 2, 3));
}

namespace {
struct SharedClock {
    srClock clock;
    srClock* saved = std::exchange(g_shared_timer_base, &clock);
    ~SharedClock() { g_shared_timer_base = saved; }
};
}

TEST_CASE_METHOD(SurRenderTestFixture, "Prop position and delta queries preserve animation stepping", "[geometry][animation]")
{
    SharedClock shared_clock;
    W8Prop prop;
    prop.Rep()->animation = std::make_unique<W8AnimObj>().release();
    auto& animation = *prop.Rep()->animation;
    prop.m_pRep->location = {10, 20, 30};
    prop.animation_position = {3, 5, 7};
    prop.previous_animation_position = {1, 2, 3};
    const auto static_position = prop.GetPosition();
    REQUIRE(static_position == srVector3(10, 20, 30));
    animation.path_lists = true;
    REQUIRE(prop.GetPosition() == prop.animation_position);
    REQUIRE(static_position == srVector3(10, 20, 30));
    prop.Rep()->animation_behaviour = W8_ANIMATION_PLAY_ONCE;
    prop.Rep()->frame_direction = W8_ANIMATION_FORWARD;
    prop.Rep()->first_frame = 0;
    prop.Rep()->last_frame = 3;
    prop.Rep()->subcycle = 1;
    const auto delta = prop.GetDelta();
    REQUIRE(delta == srVector3(2, 3, 4));
    REQUIRE(prop.Rep()->subcycle == 1);
    prop.Rep()->subcycle = 3;
    REQUIRE(prop.GetDelta() == srVector3(0, 0, 0));
    prop.Rep()->animation_behaviour = W8_ANIMATION_REPEAT;
    prop.Rep()->frame_method = W8_ANIMATION_WRAP;
    REQUIRE(prop.GetDelta() == srVector3(0, 0, 0));
    prop.Rep()->frame_method = W8_ANIMATION_PING_PONG;
    REQUIRE(prop.GetDelta() == delta);
    prop.Rep()->frame_direction = W8_ANIMATION_REVERSE;
    prop.Rep()->subcycle = 1;
    REQUIRE(prop.GetDelta() == delta);
}

TEST_CASE_METHOD(SurRenderTestFixture, "Animation bounds retain composite caching and path values", "[geometry][animation]")
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
        mesh->meshes.push_back(std::make_unique<stModelInstance>(nullptr, srNode::Ownership::independent));
        animation.meshes[0].push_back(std::move(mesh));
        auto path = std::make_unique<W8PathAI>();
        PathAIAddPoint(*path, srVector3(100, 200, 300));
        PathAIAddPoint(*path, srVector3(400, 500, 600));
        path->rotations.resize(path->nodes.size(), glm::mat3(1));
        PathAISetValue(path.get(), 0.75f);
        animation.paths[0].push_back(std::move(path));
    }
    const auto bounds = AnimObjGetBounds(&animation, 0, 1);
    REQUIRE(bounds);
    REQUIRE(bounds->minimum == glm::vec3(-4, -2, -3));
    REQUIRE(bounds->maximum == glm::vec3(4, 8, 6));
    REQUIRE(animation.frame_bounds[1].known);
    for (const auto& path : animation.paths[0])
        REQUIRE(PathAIGetValue(path.get()) == 0.75f);
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
    for (const auto& path : animation.paths[0])
        REQUIRE(PathAIGetValue(path.get()) == 0.75f);
}

TEST_CASE_METHOD(SurRenderTestFixture, "Prop bounds aggregate frames without changing the current frame", "[geometry][animation]")
{
    SharedClock shared_clock;
    W8Prop prop;
    prop.Rep()->animation = std::make_unique<W8AnimObj>().release();
    auto& animation = *prop.Rep()->animation;
    animation.path_lists = true;
    animation.frame_count = 3;
    animation.frame_bounds = {{true, {-4, -2, -8}, {3, 5, 6}},
                              {true, {-1, -9, -3}, {12, 2, 5}},
                              {true, {-3, -5, -6}, {4, 8, 9}}};
    prop.Rep()->subcycle = 1;
    const auto center = prop.GetCenterPosition();
    REQUIRE(center);
    REQUIRE(*center == srVector3(5.5f, -3.5f, 1));
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

TEST_CASE_METHOD(SurRenderTestFixture, "Monster bounds scale and translate independent optional values", "[geometry][animation]")
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
    REQUIRE(world->minimum == bounds->minimum + monster.GetPosition().glmValue());
    REQUIRE(world->maximum == bounds->maximum + monster.GetPosition().glmValue());
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
    REQUIRE(animation.frame_bounds[0].minimum == srVector3(-1, -2, -3));
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
    std::array<srVector3i, 3> triangles{{{0, 0, 0}, {1, 1, 1}, {2, 2, 2}}};
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

TEST_CASE_METHOD(SurRenderTestFixture, "Clip plane normalization preserves consumer transform ordering and degenerate planes", "[geometry][rendering]")
{
    srGERD renderer(srCreateSDLGPUDevice(), "clip plane ordering");
    renderer.matrixMode(srGERD::MATRIX_MODELVIEW);
    const auto scale = glm::scale(glm::mat4(1), glm::vec3(2, 3, 4));
    renderer.loadMatrix(scale);
    renderer.pushClipPlane({2, 0, 0, -2}, srGERD::CLIPMODE_POSITIONAL_0);
    // GERD normalizes in object space before the inverse-transpose transform.
    REQUIRE(renderer.getClipPlanes().planes[6] == glm::vec4(0.5f, 0, 0, -1));
    renderer.popClipPlane();
    // The CPU culler normalizes after its supplied covector transform.
    const auto plane = srTriangleCuller::transformClipPlane({2, 0, 0, -2},
                                                           glm::transpose(glm::inverse(scale)));
    REQUIRE(plane == glm::vec4(1, 0, 0, -2));
    REQUIRE(srTriangleCuller::transformClipPlane({0, 0, 0, 4}, glm::mat4(1)) ==
            glm::vec4(0, 0, 0, 4));
}

TEST_CASE_METHOD(SurRenderTestFixture, "Empty software backface selections skip mesh submission", "[geometry]")
{
    srGERD renderer(srCreateSDLGPUDevice(), "empty software selection");
    renderer.setWinding(srGERD::WINDING_POSITIONAL_0);
    stMeshModel model(1, 3);
    auto* pipeline = srTriMeshPipeline::Get(&renderer);
    std::array<glm::vec3, 3> positions{{{0, 0, 2}, {0, 0, 2}, {0, 0, 2}}};
    std::array<srVector3i, 1> triangles{{{0, 1, 2}}};
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
    REQUIRE_THROWS_AS(model.RenderTriMeshWithEquations(renderer, mesh, normals), std::invalid_argument);
    REQUIRE(pipeline->triangle_count == before);
}

TEST_CASE_METHOD(SurRenderTestFixture, "Failed mesh materials release renderer and restore enable flags", "[geometry]")
{
    class FailingMaterial : public srMaterial {
    public:
        void preProcess(srVertexPipe&) override { throw std::runtime_error("material failed"); }
    };
    srGERD renderer(srCreateSDLGPUDevice(), "failed mesh submission");
    renderer.setWinding(srGERD::WINDING_POSITIONAL_0);
    // Reuse a CPU batch as sorted without requiring a window or GPU context.
    auto* batch = renderer.lockRenderer();
    batch->sorted = 1;
    renderer.unlockRenderer(batch, 0);
    renderer.toggle(srGERD::ENABLE_REVERSE_NORMALS);
    srMeshModel native(1, 3);
    stMeshModel extended(1, 3);
    std::array<glm::vec3, 3> positions{{{-0.5f, -0.5f, 0.5f},
                                      {0.5f, -0.5f, 0.5f}, {0, 0.5f, 0.5f}}};
    std::array<srVector3i, 1> triangles{{{0, 1, 2}}};
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
        auto* resumed = renderer.lockRenderer();
        renderer.unlockRenderer(resumed, 0);
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

TEST_CASE_METHOD(SurRenderTestFixture, "Particle activation values and billboard ordering", "[geometry]")
{
    struct RestoreClock {
        srClock* previous = g_shared_timer_base;
        ~RestoreClock() { g_shared_timer_base = previous; }
    } restore;
    srClock clock;
    g_shared_timer_base = &clock;
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
    const std::array<glm::vec3, 9> expected{{
        {3, 5, 8}, {5, 8, 13}, {1, 2, 3}, {1, 8, 13}, {1, 8, 3},
        {5, 8, 3}, {5, 2, 13}, {1, 2, 13}, {5, 2, 3}}};
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
    visitOctreeSegment({0, 0, 0}, {0, 0, 1}, makeWalk,
        [&](const auto& cell, bool adjacent) {
            REQUIRE(adjacent);
            cells.push_back(cell);
            return true;
        });
    REQUIRE(built == 0);
    REQUIRE(cells == std::vector<glm::ivec3>{{0, 0, 0}, {0, 0, 1}});
    cells.clear();
    visitOctreeSegment({0, 0, 0}, {1, 0, 0}, makeWalk,
        [&](const auto& cell, bool adjacent) {
            REQUIRE(adjacent);
            cells.push_back(cell);
            return false;
        });
    REQUIRE(built == 0);
    REQUIRE(cells.size() == 1);
    cells.clear();
    visitOctreeSegment({0, 0, 0}, {2, 0, 0}, makeWalk,
        [&](const auto& cell, bool adjacent) {
            REQUIRE(!adjacent);
            cells.push_back(cell);
            return cells.size() < 2;
        });
    REQUIRE(built == 1);
    REQUIRE(cells == std::vector<glm::ivec3>{{0, 0, 0}, {1, 0, 0}});
}

TEST_CASE_METHOD(SurRenderTestFixture, "Baked mesh colors preserve material and light-bank rules", "[geometry]")
{
    struct RestoreLighting {
        glm::vec3 offset = g_environment_offset;
        float scale = g_monster_light_scale;
        bool unlit = g_render_unlit;
        ~RestoreLighting() {
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
        REQUIRE(close(base.dig[0][vertex],
            (glm::vec3(1, 0.75f, 4) * sunlight[vertex] + lights[vertex] + g_environment_offset) * 2.0f));

    auto* materials = mesh.getVertexMaterial(0, srMeshModel::SIDE_FRONT, 1);
    materials[0] = material;
    materials[1] = zero_material;
    materials[2] = nullptr;
    materials[3] = material;
    mesh.flags |= W8_MESH_VERTEX_LIGHTING_DIRTY;
    const auto mixed = mesh.getTriMesh();
    const std::array<glm::vec3, 4> ambient{{{1, 0.75f, 4}, {0, 0, 0}, {2, 3, 4}, {1, 0.75f, 4}}};
    for (unsigned vertex = 0; vertex < 4; ++vertex)
        REQUIRE(close(mixed.dig[0][vertex],
            (ambient[vertex] * sunlight[vertex] + lights[vertex] + g_environment_offset) * 2.0f));

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

TEST_CASE_METHOD(SurRenderTestFixture, "Static and frame normals share area weighting across shade seams", "[geometry][mesh]")
{
    stMeshModel mesh(3, 5);
    const std::array<glm::vec3, 5> positions{{{0, 0, 0}, {2, 0, 0}, {0, 3, 0},
                                            {0, 0, 4}, {0, 0, 0}}};
    std::ranges::copy(positions, mesh.getVertexLoc());
    const std::array<srVector3i, 3> triangles{{{0, 1, 2}, {4, 3, 1}, {2, 2, 2}}};
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

TEST_CASE("Region geometry preserves octant order and point versus box boundaries", "[geometry][octree]")
{
    OctBuildPreTree tree(1, std::nullopt, 16, 0, 0);
    auto& points = tree.particle_regions.regions;
    auto& boxes = tree.prop_regions.regions;
    tree.spatial.m_extent = 2;
    tree.spatial.m_bounds.minimum = glm::vec3(0);
    tree.spatial.m_bounds.maximum = glm::vec3(2);
    tree.spatial.m_depth = 1;
    tree.spatial.m_root = new W8OctBuildNode;
    for (unsigned octant = 0; octant < 8; ++octant) {
        auto* node = new W8OctBuildNode;
        node->region = octant + 1;
        tree.spatial.m_root->children[octant] = node;
    }
    REQUIRE(tree.updateRegionForGeometry(glm::vec3(1), 10));
    REQUIRE(points.count(8) == 1);
    REQUIRE(points.size() == 1);
    REQUIRE_FALSE(tree.updateRegionForGeometry(glm::vec3(2, 1, 1), 20));
    REQUIRE(tree.updateRegionForGeometry(glm::vec3(0), 30));
    REQUIRE(points.count(1) == 1);
    REQUIRE(boxes.empty());
    REQUIRE(tree.updateRegionForGeometry(srBoundingBox{{1, 0, 0}, {2, 1, 1}}, 40));
    REQUIRE(boxes.count(5) == 1);
    REQUIRE(boxes.size() == 1);
    REQUIRE_FALSE(tree.updateRegionForGeometry(srBoundingBox{{2, 0, 0}, {3, 1, 1}}, 50));
    REQUIRE(tree.updateRegionForGeometry(srBoundingBox{glm::vec3(0.5f), glm::vec3(1.5f)}, 60));
    REQUIRE(tree.updateRegionForGeometry(srBoundingBox{glm::vec3(0.5f), glm::vec3(1.5f)}, 60));
    REQUIRE(boxes.size() == 9); // Repeated updates refresh existing entries.

    tree.spatial.m_level_kind = 1;
    W8OctSpatialState child(&tree.spatial);
    std::vector<unsigned> order;
    visitBuildChildren(tree.spatial, child, [&](const W8OctSpatialState& candidate, unsigned octant) {
        order.push_back(octant);
        REQUIRE(candidate.m_root->region == octant + 1);
        const glm::vec3 minimum(octant >> 2, (octant >> 1) & 1, octant & 1);
        REQUIRE(candidate.bounds().minimum == minimum);
        REQUIRE(candidate.bounds().maximum == minimum + 1.0f);
    });
    REQUIRE(order == std::vector<unsigned>{0, 1, 2, 3, 4, 5, 6, 7});
    std::array<W8OctRegionVolume, 3> volumes{};
    volumes[1].m_region = 11;
    volumes[2].m_region = 12;
    tree.spatial.m_region_volumes = volumes.data();
    tree.spatial.m_region_count = volumes.size();
    tree.particle_regions.clear();
    REQUIRE(tree.assignPointToVolumes(glm::vec3(0), 5, tree.particle_regions));
    REQUIRE(tree.assignPointToVolumes(glm::vec3(1), 5, tree.particle_regions));
    REQUIRE(points.count(11) == 1);
    REQUIRE(points.count(12) == 1);
    REQUIRE(points.size() == 2);
    REQUIRE(boxes.size() == 9);
    tree.spatial.m_region_count = 1;
    REQUIRE_FALSE(tree.assignPointToVolumes(glm::vec3(0), 6, tree.particle_regions));
    tree.spatial.m_region_volumes = nullptr;
    tree.spatial.m_region_count = 0;
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

TEST_CASE("Scoped renderer effects preserve sibling order and global or disabled descent", "[geometry][rendering]")
{
    SurRenderTestFixture runtime;
    const auto check = []<class Effect>() {
        srNode root;
        auto* sibling = new TraversalMarker(root);
        auto* effect = new Effect(&root);
        auto* child = new TraversalMarker(*effect);
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

TEST_CASE("Wizardry light inactivity and missing definitions have distinct traversal policies", "[geometry][rendering]")
{
    SurRenderTestFixture runtime;
    struct RestoreFlags {
        unsigned saved = g_light_update_flags;
        ~RestoreFlags() { g_light_update_flags = saved; }
    } restore;
    g_light_update_flags = 1;
    stLight light(nullptr);
    light.setIntensity(1);
    light.clearFlag(srNode::FLAG_GLOBAL);
    auto* child = new TraversalMarker(light);
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
    light.m_definition = new stParametricLightDefinition;
    info = {};
    light.traverse(info);
    REQUIRE(info.entries.size() == 3);
    REQUIRE(info.entries[0].node == &light);
    REQUIRE(info.entries[1].node == child);
    REQUIRE(info.entries[2].node == &light);
}

TEST_CASE("Frustum corner ordering preserves every point and shares point sphere and box tests", "[geometry][frustum]")
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

TEST_CASE("Region link builders return independent packed runs and enforce disk limits", "[geometry][octree]")
{
    OctRegionObjectLinks particles, props;
    particles.always_visible = {2};
    particles.regions.emplace(1, 1);
    particles.regions.emplace(1, 3);
    props.always_visible.assign(6000, 4); // Beyond the former fixed scratch capacity.
    const auto packed = particles.packedLinks(3);
    REQUIRE(packed.lookup.size() == 3);
    REQUIRE(packed.lookup[0] == 0);
    REQUIRE(packed.lookup[2] == 0);
    REQUIRE(packed.alwaysVisible().size() == 1);
    REQUIRE(packed.alwaysVisible()[0] == 2);
    REQUIRE(packed.forMesh(1).size() == 2);
    const auto [first, last] = particles.regions.equal_range(1);
    auto run = packed.forMesh(1);
    unsigned index = 0;
    for (auto entry = first; entry != last; ++entry) REQUIRE(run[index++] == entry->second);
    particles.clear();
    REQUIRE(particles.regions.empty());
    REQUIRE(particles.always_visible.empty());
    REQUIRE(packed.forMesh(1).size() == 2);
    REQUIRE(props.packedLinks(3).alwaysVisible().size() == 6000);
    props.always_visible.assign(65535, 4);
    REQUIRE_THROWS_AS(props.packedLinks(3), std::length_error);
    REQUIRE_THROWS_AS(particles.packedLinks(65536), std::length_error);
    REQUIRE(particles.packedLinks(3).records == std::vector<unsigned short>{0});
}

TEST_CASE("Build surface collections own results preserve order and exceed old scratch limits", "[geometry][octree]")
{
    std::array<int, 3> surfaces{1, 2, 3};
    W8OctBuildNode leaf;
    leaf.leaf_kind = 1;
    std::array<W8OctBuildLink, 6> links{{
        {&surfaces[0], nullptr}, {&surfaces[0], nullptr}, {&surfaces[1], nullptr},
        {&surfaces[0], nullptr}, {&surfaces[2], nullptr}, {&surfaces[1], nullptr}}};
    for (unsigned i = 1; i < links.size(); ++i) links[i - 1].next = &links[i];
    leaf.links[2] = links.data();
    const auto collected = leaf.linkedSurfaces(2, 2, 2);
    REQUIRE(collected == std::vector<void*>{&surfaces[0], &surfaces[1], &surfaces[2]});
    REQUIRE(leaf.linkedSurfaces(3, 2, 2).empty());
    REQUIRE(leaf.linkedSurfaces(2, 2, 1).empty());
    std::array<void*, 5> stored{&surfaces[2], &surfaces[1], &surfaces[2], &surfaces[0], nullptr};
    leaf.surface_arrays[2] = stored.data();
    REQUIRE(leaf.storedSurfaces(2) == std::vector<void*>{&surfaces[2], &surfaces[1], &surfaces[0]});
    REQUIRE(collected[0] == &surfaces[0]); // Subsequent collection does not overwrite a prior result.
    leaf.surface_arrays[2] = nullptr;
    std::vector<int> many(11000);
    std::vector<W8OctBuildLink> many_links(many.size());
    for (unsigned i = 0; i < many.size(); ++i)
        many_links[i] = {&many[i], i + 1 == many.size() ? nullptr : &many_links[i + 1]};
    leaf.links[2] = many_links.data();
    const auto large = leaf.linkedSurfaces(0, 0, 2);
    REQUIRE(large.size() == many.size());
    REQUIRE(large.front() == &many.front());
    REQUIRE(large.back() == &many.back());
    leaf.links[2] = nullptr;

    W8OctBuildNode root;
    root.children[0] = new W8OctBuildNode;
    root.children[7] = new W8OctBuildNode;
    root.children[0]->leaf_kind = root.children[7]->leaf_kind = 1;
    root.children[0]->links[2] = &links[0];
    root.children[7]->links[2] = &links[4];
    REQUIRE(root.linkedSurfaces(0, 1, 2) == collected);
    root.children[0]->links[2] = root.children[7]->links[2] = nullptr;
}

TEST_CASE("Pre-tree leaf conversion packs polygon and terrain runs with independent offsets", "[geometry][octree]")
{
    OctPreTree tree;
    tree.m_spatial.m_depth = 0;
    tree.m_leaf_count = 1;
    tree.m_leaves = static_cast<W8OctPreTreeLeaf*>(std::calloc(2, sizeof(W8OctPreTreeLeaf)));
    tree.m_polygon_index_stream = static_cast<w8_ulong*>(std::calloc(8, sizeof(w8_ulong)));
    tree.m_gd_surface_index_stream = static_cast<w8_ulong*>(std::calloc(8, sizeof(w8_ulong)));
    tree.polygon_cursor = 1;
    tree.m_gd_surface_stream_len = 2;
    W8OctRegionPolygon polygon{};
    polygon.ordinal = 17;
    W8GDSurface terrain{};
    terrain.index = 23;
    W8OctBuildNode leaf;
    leaf.leaf_kind = 1;
    const auto makeRun = [](void* surface) {
        auto** run = static_cast<void**>(std::malloc(3 * sizeof(void*)));
        run[0] = run[1] = surface;
        run[2] = nullptr;
        return run;
    };
    leaf.surface_arrays[2] = makeRun(&polygon);
    leaf.surface_arrays[3] = makeRun(&terrain);
    REQUIRE(leaf.ConvertToOctPreTree(0, &tree) == 1);
    REQUIRE(tree.m_leaves[1].polygon_offset == 1);
    REQUIRE(tree.m_leaves[1].gd_polygon_offset == 2);
    REQUIRE(tree.polygon_cursor == 3);
    REQUIRE(tree.m_gd_surface_stream_len == 4);
    REQUIRE(tree.m_polygon_index_stream[1] == 1);
    REQUIRE(tree.m_polygon_index_stream[2] == 17);
    REQUIRE(tree.m_gd_surface_index_stream[2] == 1);
    REQUIRE(tree.m_gd_surface_index_stream[3] == 23);
    REQUIRE(leaf.surface_arrays[2] == nullptr);
    REQUIRE(leaf.surface_arrays[3] == nullptr);
}

TEST_CASE_METHOD(SurRenderTestFixture, "Prop bounds return complete boxes and register inclusive octree cells", "[geometry][octree]")
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
    REQUIRE(tree.object_registry->by_cell->size() == 2);
    REQUIRE(tree.object_registry->by_object->size() == 2);
    tree.AddCollidablePropBounds(4, cells);
    REQUIRE(tree.object_registry->by_cell->size() == 2);
    REQUIRE(tree.object_registry->by_object->size() == 2);
    srBoundingBox endpoints{{0, 10, 0}, {0, 10, 0}};
    endpoints.include({1, -2, 20});
    REQUIRE(endpoints.minimum.y == -2); // Y is compared with Y even when Z lies above the start.
    REQUIRE(endpoints.maximum.z == 20);
}

TEST_CASE("Conditional path prop links share matching while retaining blocker flags", "[geometry][octree]")
{
    OctPreTree tree;
    std::array<GDPreProp, 2> meshes;
    W8LevelFileAnimObj empty{};
    meshes[0].ApplyAnimFrame(7, empty);
    meshes[1].ApplyAnimFrame(8, empty);
    std::array<W8PreProp, 2> props{};
    props[1].first_prop_number = 20;
    props[1].num_stop_meshes = meshes.size();
    props[1].pStopMeshes = meshes.data();
    tree.m_lNumSupports = tree.m_lNumBlocks = 1;
    tree.m_lSupports[0] = 20;
    tree.m_lBlocks[0] = 21;
    std::unordered_multimap<unsigned, CondPathNode*> links;
    REQUIRE(tree.InsertConditionalNodes(links, 123, 5, props));
    REQUIRE(links.size() == 2);
    const auto* support = links.find((7u << 16) | 2u)->second;
    const auto* blocker = links.find((8u << 16) | 2u)->second;
    REQUIRE(support->cell == 123);
    REQUIRE(support->value == 5);
    REQUIRE(blocker->cell == 123);
    REQUIRE(blocker->value == (5 | W8_PATH_CELL_BLOCKING_FRAME));
    for (const auto& [key, value] : links) std::free(value);
    links.clear();
    tree.m_lNumSupports = tree.m_lNumBlocks = 0;
    REQUIRE_FALSE(tree.InsertConditionalNodes(links, 123, 5, props));
    REQUIRE(links.empty());
}

TEST_CASE("Conditional prop channels grow independently and path cells return coordinates with keys", "[geometry][octree]")
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

TEST_CASE("Region merging rekeys every member and weights GLM centers by cell population", "[geometry][octree]")
{
    OctBuildPreTree tree(1, std::nullopt, 16, 8, 0);
    tree.spatial.m_root = new W8OctBuildNode;
    auto* first = tree.spatial.m_root->children[0] = new W8OctBuildNode;
    auto* second = tree.spatial.m_root->children[1] = new W8OctBuildNode;
    auto* neighbor = tree.spatial.m_root->children[4] = new W8OctBuildNode;
    first->region = second->region = 1;
    neighbor->region = 2;
    first->leaf_kind = second->leaf_kind = 2;
    neighbor->leaf_kind = 3;
    tree.spatial.m_region_grid_cell = 10;
    tree.region_centers = {{0, 0, 0}, {0, 3, -6}, {6, 9, 0}};
    tree.region_path_map.emplace(1, 0x01000000u);
    tree.region_path_map.emplace(1, 0x01000001u);
    tree.region_path_map.emplace(2, 0x01010000u);
    const int cell[] = {0x01000000, 1, 0, 0};
    REQUIRE(tree.MergeRegion(first, cell));
    REQUIRE(first->region == 2);
    REQUIRE(second->region == 2);
    REQUIRE(first->leaf_kind == 5);
    REQUIRE(second->leaf_kind == 5);
    REQUIRE(neighbor->leaf_kind == 5);
    REQUIRE(tree.region_path_map.count(1) == 0);
    REQUIRE(tree.region_path_map.count(2) == 3);
    REQUIRE(tree.region_centers[2] == glm::vec3(2, 5, -4));
    REQUIRE_FALSE(tree.MergeRegion(first, cell));
    OctBuildPreTree independent(1, std::nullopt, 16, 8, 0);
    REQUIRE(independent.region_path_map.empty());
    REQUIRE(independent.region_centers.empty());
}

TEST_CASE_METHOD(SurRenderTestFixture, "Mesh permutations keep GLM channels corners shades and active polygons aligned", "[geometry][rendering]")
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
                REQUIRE(mesh.getVertexTexCoords(pass, layer, 0)[i] == glm::vec2(40 * pass + 5 * layer + vertex_order[i]));
        }
    }
    const std::array<w8_ulong, 3> duplicate{0, 0, 2};
    REQUIRE_THROWS_AS(mesh.reindexVertices(duplicate), std::invalid_argument);
    REQUIRE_THROWS_AS(mesh.reindexVertices(polygon_order), std::invalid_argument);
    REQUIRE_THROWS_AS(mesh.reindexPolygons(vertex_order), std::invalid_argument);
    REQUIRE(mesh.getVertexLoc()[0] == positions[2]); // Invalid permutations change no tables.
}

TEST_CASE_METHOD(SurRenderTestFixture, "Model-view scopes reject overflow without taking another scope's stack entry", "[geometry][rendering]")
{
    srGERD renderer(srCreateSDLGPUDevice(), "matrix scope overflow");
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

TEST_CASE_METHOD(SurRenderTestFixture, "Linked main and highlight draws share the instance termination boundary", "[geometry][rendering]")
{
    struct ProbeMesh : stMeshModel {
        ProbeMesh() : stMeshModel(0, 0) {}
        int draws = 0;
        int fail_on = 0;
        std::shared_ptr<srTextureIFace> texture;
        TriMesh getTriMesh() override {
            if (++draws == fail_on) throw std::runtime_error("mesh acquisition failed");
            TriMesh mesh;
            mesh.poly_textures[0][0] = &texture; // A table enables the highlight path.
            return mesh;
        }
    };
    srGERD renderer(srCreateSDLGPUDevice(), "linked draw boundary");
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
    srNode termination(&instance, srNode::Ownership::independent);
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

TEST_CASE_METHOD(SurRenderTestFixture, "Failed instance materials restore picking lighting and material overrides", "[geometry][rendering]")
{
    struct FailingMaterial : srMaterial {
        void preProcess(srVertexPipe&) override { throw std::runtime_error("material failed"); }
    };
    srGERD renderer(srCreateSDLGPUDevice(), "failed instance draw");
    renderer.setWinding(srGERD::WINDING_POSITIONAL_0);
    auto* batch = renderer.lockRenderer();
    batch->sorted = 1;
    renderer.unlockRenderer(batch, 0);
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

TEST_CASE_METHOD(SurRenderTestFixture, "Bounds rejection restores transforms before returning or traversing children", "[geometry][rendering]")
{
    struct Child : srNode {
        explicit Child(srNode& parent) : srNode(&parent, Ownership::independent) {}
        bool visited = false;
        glm::mat4 view{0};
        void traverse(TraverseInfo& info) override {
            visited = true;
            view = info.renderer->getMatrix(srGERD::MATRIX_MODELVIEW);
        }
    };
    srGERD renderer(srCreateSDLGPUDevice(), "bounder scope");
    renderer.matrixMode(srGERD::MATRIX_PROJECTION);
    renderer.loadIdentity();
    renderer.matrixMode(srGERD::MATRIX_MODELVIEW);
    renderer.loadIdentity();
    srBounder bounder;
    bounder.setLocation(0.25, 0, 0);
    Child child(bounder);
    renderer.pushClipPlane({1, 0, 0, 0}, srGERD::CLIPMODE_POSITIONAL_0);
    srNode::TraverseInfo info;
    info.renderer = &renderer;
    const auto view = renderer.getMatrix(srGERD::MATRIX_MODELVIEW);
    const std::array<srNode::BoundInfo, 3> bounds{{
        {{-3, -1, -1}, {-2, 1, 1}, {-3, 0, 0}, 0.1f, 1}, // Sphere rejection.
        {{-3, -1, -1}, {-2, 1, 1}, {0, 0, 0}, 4, 1},    // Box rejection.
        {{0, -1, -1}, {1, 1, 1}, {0.5f, 0, 0}, 1, 1}}};
    for (std::size_t i = 0; i < bounds.size(); ++i) {
        bounder.setBounds(bounds[i]);
        child.visited = false;
        bounder.traverse(info);
        REQUIRE(renderer.getMatrix(srGERD::MATRIX_MODELVIEW) == view);
        REQUIRE(child.visited == (i == 2));
        if (child.visited) REQUIRE(child.view == view);
    }
    renderer.popClipPlane();
}

TEST_CASE_METHOD(SurRenderTestFixture, "Model render scopes restore GLM transforms and exclusions after a failed draw", "[geometry][rendering]")
{
    struct Probe : srModel {
        bool fail = false;
        w8_ulong mask = 0;
        glm::mat4 transform{1};
        Bounds getBounds() override { return {}; }
        void render(srGERD& renderer) override {
            mask = renderer.getExclusionMask();
            transform = renderer.getMatrix(srGERD::MATRIX_MODELVIEW);
            renderer.matrixMode(srGERD::MATRIX_PROJECTION);
            if (fail) throw std::runtime_error("injected model draw failure");
        }
    };
    srGERD renderer(srCreateSDLGPUDevice(), "render scope");
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
            if (fail) REQUIRE_THROWS_AS(instance.process(info, srNode::PROCESS_RENDER), std::runtime_error);
            else instance.process(info, srNode::PROCESS_RENDER);
            REQUIRE(model->mask == 20);
            REQUIRE(renderer.getExclusionMask() == 16);
            REQUIRE(renderer.getMatrixMode() == srGERD::MATRIX_MODELVIEW);
            REQUIRE(renderer.getMatrix(srGERD::MATRIX_MODELVIEW) == view);
            REQUIRE(model->transform != view);
        }
    }
}

TEST_CASE_METHOD(SurRenderTestFixture, "Material vertex splits preserve first-face attributes without a fixed scratch limit", "[geometry][rendering]")
{
    constexpr unsigned materials = 80;
    constexpr unsigned faces_count = 120;
    W8OctPreTreeGeometry geometry{};
    geometry.vertex_count = 5; // Three used vertices and one unused vertex after the sentinel.
    geometry.m_vertices = static_cast<W8OctPreTreeVertex*>(std::calloc(geometry.vertex_count, sizeof(W8OctPreTreeVertex)));
    REQUIRE(geometry.m_vertices);
    struct ReleaseVertices {
        W8OctPreTreeGeometry& geometry;
        ~ReleaseVertices() { std::free(geometry.m_vertices); }
    } release{geometry};
    std::vector<W8OctRegionPolygon> polygons(faces_count + 1);
    std::vector<int> faces(faces_count);
    geometry.m_polygon_count = polygons.size();
    geometry.m_polygons = polygons.data();
    for (unsigned source = 1; source < 5; ++source) {
        auto& vertex = geometry.m_vertices[source];
        vertex.m_vertex_index = source;
        vertex.position = glm::vec3(float(source), 2, 3);
        if (source < 4) {
            vertex.face_indices = faces.data();
            vertex.face_count = faces.size();
        }
    }
    for (unsigned face = 1; face <= faces_count; ++face) {
        faces[face - 1] = face;
        auto& polygon = polygons[face];
        polygon.material = (face - 1) % materials;
        polygon.kind = face % 3;
        for (unsigned corner = 0; corner < 3; ++corner) {
            polygon.vertices[corner] = &geometry.m_vertices[corner + 1];
            polygon.face.texture_coordinates[corner] = glm::vec2(face, corner);
        }
    }
    REQUIRE(geometry.splitVerticesByMaterial());
    REQUIRE(geometry.vertex_count == 3 * materials + 1);
    REQUIRE(geometry.vertex_count > 4 * 5); // Exceeds the former four-times allocation.
    for (unsigned face = 1; face <= faces_count; ++face) {
        const auto& polygon = polygons[face];
        for (unsigned corner = 0; corner < 3; ++corner) {
            const auto index = corner * materials + polygon.material + 1;
            const auto* vertex = polygon.vertices[corner];
            REQUIRE(vertex == &geometry.m_vertices[index]);
            REQUIRE(vertex->m_vertex_index == index);
            REQUIRE(vertex->m_material == static_cast<int>(polygon.material));
            REQUIRE(vertex->m_kind == (polygon.material + 1) % 3);
            REQUIRE(vertex->m_uv == glm::vec2(polygon.material + 1, corner));
            REQUIRE(vertex->position.x == float(corner + 1));
        }
    }
}

TEST_CASE("Native octree trace reset snapshots aliased endpoints and clears hit state", "[geometry][octree]")
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

TEST_CASE("Geometry compaction preserves weld chains, stable faces and normalized attributes", "[geometry][octree]")
{
    W8OctPreTreeGeometry geometry{};
    geometry.vertex_count = 7;
    geometry.m_polygon_count = 4;
    geometry.m_vertices = static_cast<W8OctPreTreeVertex*>(std::calloc(7, sizeof(W8OctPreTreeVertex)));
    geometry.m_polygons = static_cast<W8OctRegionPolygon*>(std::calloc(4, sizeof(W8OctRegionPolygon)));
    struct ReleaseGeometry {
        W8OctPreTreeGeometry& geometry;
        ~ReleaseGeometry() { std::free(geometry.m_vertices); std::free(geometry.m_polygons); }
    } release{geometry};
    REQUIRE(geometry.m_vertices);
    REQUIRE(geometry.m_polygons);
    for (w8_ulong vertex = 1; vertex < 7; ++vertex) {
        auto& record = geometry.m_vertices[vertex];
        record.m_vertex_index = vertex;
        record.position.x = static_cast<float>(vertex);
        record.m_normal_count = 30;
        record.m_normal = {0, 3, 4};
    }
    geometry.m_vertices[2].m_normal = {-0.0f, 0, 0};
    geometry.m_vertices[3].flags = W8OctPreTreeVertex::EXCLUDED;
    geometry.m_vertices[3].m_vertex_index = 1;
    geometry.m_vertices[5].flags = W8OctPreTreeVertex::EXCLUDED;
    geometry.m_vertices[5].m_vertex_index = 3;
    constexpr std::array<std::array<unsigned, 3>, 3> source_corners{{{1, 3, 2}, {2, 4, 2}, {5, 6, 2}}};
    for (unsigned face = 1; face < 4; ++face) {
        auto& polygon = geometry.m_polygons[face];
        polygon.ordinal = face + 10;
        polygon.material = face * 10;
        for (unsigned corner = 0; corner < 3; ++corner) {
            polygon.vertices[corner] = geometry.m_vertices + source_corners[face - 1][corner];
            polygon.face.texture_coordinates[corner] = glm::vec2(face, corner);
        }
    }
    geometry.m_polygons[2].degenerate = true;
    REQUIRE(geometry.compact());
    REQUIRE(geometry.vertex_count == 4);
    REQUIRE(geometry.m_polygon_count == 3);
    REQUIRE(geometry.m_vertices[1].position.x == 1);
    REQUIRE(geometry.m_vertices[2].position.x == 2);
    REQUIRE(geometry.m_vertices[3].position.x == 6);
    REQUIRE(geometry.m_vertices[1].m_normal == glm::vec3(0, 0.6f, 0.8f));
    REQUIRE(std::signbit(geometry.m_vertices[2].m_normal.x));
    REQUIRE(geometry.m_vertices[2].m_normal_count == 2);
    constexpr std::array<std::array<unsigned, 3>, 2> expected{{{1, 1, 2}, {1, 3, 2}}};
    for (unsigned face = 1; face < 3; ++face) {
        const auto& polygon = geometry.m_polygons[face];
        const unsigned original = face == 1 ? 1 : 3;
        REQUIRE(polygon.ordinal == face);
        REQUIRE(polygon.material == original * 10);
        for (unsigned corner = 0; corner < 3; ++corner) {
            REQUIRE(polygon.vertices[corner] == geometry.m_vertices + expected[face - 1][corner]);
            REQUIRE(polygon.face.vertices[corner] == expected[face - 1][corner]);
            REQUIRE(polygon.face.texture_coordinates[corner] == glm::vec2(original, corner));
        }
    }
}

TEST_CASE("Surface compilation keeps stable walkable runs and drops welded degeneracies", "[geometry]")
{
    std::array<W8GDSurface, 6> surfaces{};
    for (int index = 0; index < 6; ++index) {
        auto& surface = surfaces[index];
        surface.index = 100 + index;
        for (int corner = 0; corner < 3; ++corner) surface.vertex_indices[corner] = index * 3 + corner;
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

TEST_CASE("Animated prop affine geometry uses scaled paths and rotated native bounds", "[geometry]")
{
    W8SerializedMeshFace face{};
    for (int corner = 0; corner < 3; ++corner) face.vertices[corner] = corner;
    W8LevelFileFrame frame{};
    frame.mesh.num_vertices = 3;
    frame.mesh.num_faces = 1;
    frame.mesh.vertices = {{0, 0, 0}, {0, 0, 2}, {2, 0, 0}};
    frame.mesh.faces = {face};
    W8LevelFileScaledPathNode node{};
    node.path.position.Set(1, -2, 3);
    node.path.axis.Set(0, 2, 0);
    node.path.angle = 1.5707963267948966f;
    node.scale.Set(2, 3, 4);
    W8LevelFileTransform transform{};
    transform.num_frames = 1;
    transform.frames.push_back(std::move(frame));
    transform.pathAI.scaled = 2;
    transform.pathAI.scaled_paths = {node};
    W8LevelFileAnimObj animation{};
    animation.path_lists = 1;
    animation.transforms.push_back(std::move(transform));
    GDProp prop;
    prop.ApplyAnimFrame(0, animation);
    const auto bounds = prop.computeBounds();
    REQUIRE(glm::length(bounds.minimum - glm::vec3(500, -1000, -500)) < 0.01f);
    REQUIRE(glm::length(bounds.maximum - glm::vec3(4500, -1000, 1500)) < 0.01f);
}

TEST_CASE("Prop surface geometry preserves walkable slope while clearing steep slope", "[geometry]")
{
    std::array<srVector3, 3> positions{{{0, -2, 0}, {0, -2, 2}, {2, -2, 0}}};
    W8GDSurface surface{};
    for (int corner = 0; corner < 3; ++corner) surface.vertex_indices[corner] = corner;
    surface.slope = 0.3f;
    surfaceGeometry::initializePropSurface(surface, positions);
    REQUIRE(surface.plane.normal.glmValue() == glm::vec3(0, 1, 0));
    REQUIRE(surface.plane.w == 2);
    REQUIRE(surface.flags == (W8_GD_SURFACE_PROP_GEOMETRY | W8_GD_SURFACE_WALKABLE | 1));
    REQUIRE(surface.slope == 0.3f);
    REQUIRE(surface.contact_margin == 500);
    std::swap(surface.vertex_indices[1], surface.vertex_indices[2]);
    surfaceGeometry::initializePropSurface(surface, positions);
    REQUIRE(surface.plane.normal.glmValue() == glm::vec3(0, -1, 0));
    REQUIRE(surface.flags == (W8_GD_SURFACE_PROP_GEOMETRY | 1));
    REQUIRE(surface.slope == 0);
}

TEST_CASE("Octree build cleanup frees shared runs and retains the reserved record")
{
    W8OctPreTreeGeometry geometry{};
    geometry.vertex_count = geometry.m_polygon_count = 4;
    geometry.m_vertices = static_cast<W8OctPreTreeVertex*>(std::calloc(4, sizeof(W8OctPreTreeVertex)));
    geometry.m_polygons = static_cast<W8OctRegionPolygon*>(std::calloc(4, sizeof(W8OctRegionPolygon)));
    REQUIRE(geometry.m_vertices); REQUIRE(geometry.m_polygons);
    int sentinel = 0;
    geometry.m_vertices[0].face_indices = geometry.m_vertices[0].owned = &sentinel;
    geometry.m_polygons[0].face_indices = &sentinel;
    const auto run = [] { return static_cast<int*>(std::malloc(sizeof(int))); };
    geometry.m_vertices[1].face_indices = geometry.m_vertices[2].face_indices = run();
    geometry.m_vertices[3].face_indices = run();
    geometry.m_vertices[1].owned = run();
    geometry.m_vertices[3].owned = run();
    geometry.m_polygons[1].face_indices = geometry.m_polygons[2].face_indices = run();
    geometry.m_polygons[3].face_indices = run();
    geometry.Release();
    REQUIRE(geometry.vertex_count == 0); REQUIRE(geometry.m_polygon_count == 0);
    REQUIRE_FALSE(geometry.m_vertices); REQUIRE_FALSE(geometry.m_polygons);
    geometry.Release();
}

TEST_CASE_METHOD(SurRenderTestFixture, "octree camera snapshots use native basis columns and plane coefficients", "[geometry]")
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
        view.frustum_planes[i] = {packed[i].normal.glmValue(), packed[i].w};
    REQUIRE(srPlaneMath::containsSphere(view.frustum_planes, glm::vec3(0), 0));
    REQUIRE(srPlaneMath::containsSphere(view.frustum_planes, glm::vec3(2, 0, 0), 1));
    REQUIRE_FALSE(srPlaneMath::containsSphere(view.frustum_planes, glm::vec3(2, 0, 0), .99f));
    REQUIRE(SphereInsideFrustum({2, 0, 0}, 1, packed));

    view = {};
    REQUIRE(view.camera_location == glm::vec3(0));
    REQUIRE(view.frustum_planes[5] == glm::vec4(0));
}

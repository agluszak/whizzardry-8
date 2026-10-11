#include "glm/ext/vector_int3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/octree_walk.h"
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
#include "surrender/srMaterial.h"
#include "surrender/srModeler.h"
#include "surrender/srGERD.h"
#include "surrender/srCamera.h"
#include "surrender/srClock.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "wiz8/engine_code/AnimRep.hpp"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/game_timer.h"
#include "wiz8/engine_code/World.h"
#include "surrender/srTriangleCuller.h"
#include "surrender/srDD_SDLGPU.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/AnimObj.h"
#include "wiz8/engine_code/AniMesh.h"
#include "wiz8/engine_code/Monster.h"
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
    const auto empty = boundingBox({});
    REQUIRE((empty.minimum == glm::vec3(0) && empty.maximum == glm::vec3(0)));
    const std::array<srVector3, 3> points{{{-4, 2, -3}, {-1, 8, -7}, {-2, 5, -1}}};
    const auto box = boundingBox(points);
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
    SetPlaneFromThreePoints(&plane, &a, &b, &c);
    REQUIRE(close(plane.normal.glmValue(), {0, 1, 0}));
    REQUIRE(std::abs(SignedPlaneDistance(plane, {5, 4, 7}) - 2) < 1e-5f);
    SetPlaneFromThreePoints(&plane, &a, &a, &a);
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
    vertices[0] = srVector3(2, 3, 4);
    vertices[1] = srVector3(4, 5, 6);
    auto* linked = new stMeshModel(0, 2);
    linked->getVertexLoc()[0] = srVector3(6, 7, 8);
    linked->getVertexLoc()[1] = srVector3(8, 9, 10);
    mesh.AddPart(std::unique_ptr<stMeshModel>(linked));
    mesh.AddPart(std::make_unique<stMeshModel>(0, 0));
    linked->CalculateLinkedBounds();
    for (auto* member = &mesh; member; member = member->Next()) {
        REQUIRE(member->bounds_minimum == srVector3(2, 3, 4));
        REQUIRE(member->bounds_maximum == srVector3(8, 9, 10));
        REQUIRE(member->bounds_center == srVector3(5, 6, 7));
        REQUIRE(std::abs(member->bounds_radius - std::sqrt(27.0f)) < 1e-5f);
    }
    // Replacing all positions with negative values must not expand the box to the origin.
    for (auto* member = &mesh; member; member = member->Next()) {
        for (auto& vertex : std::span(member->getVertexLoc(), member->vertex_location_count))
            vertex = -vertex;
        member->setDirtyAll();
    }
    mesh.CalculateLinkedBounds();
    REQUIRE(mesh.bounds_minimum == srVector3(-8, -9, -10));
    REQUIRE(mesh.bounds_maximum == srVector3(-2, -3, -4));
    REQUIRE(mesh.findClosestVertex({-3, -4, -5}) == 0); // Equal distances choose the first vertex.
    stMeshModel zero(0, 2);
    zero.scaleToMaxRadius(10);
    zero.scaleToAverageRadius(10);
    REQUIRE(zero.getVertexLoc()[0] == srVector3(0, 0, 0));
    REQUIRE(zero.getMaxRadius() == 0);
}

static void baked_lighting()
{
    auto material = std::make_shared<srMaterial>();
    material->parms.diffuse = glm::vec4(0.5f, 1, 0.25f, 1);
    auto model = std::make_shared<stMeshModel>(0, 2);
    auto& mesh = *model;
    mesh.getVertexLoc()[0] = srVector3(1, 0, 0);
    mesh.getVertexLoc()[1] = srVector3(2, 0, 0);
    const auto normal = glm::normalize(glm::vec3(1, 1, 0));
    mesh.getVertexNormal()[0] = normal;
    mesh.getVertexNormal()[1] = normal;
    mesh.setMaterial(material, 0, srMeshModel::SIDE_FRONT);
    stModelInstance instance(nullptr);
    instance.setModel(model);
    instance.setLocation(10, 0, 0);
    instance.setScale(srVector3T<double>(2, 1, 1));
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
    auto* colors = mesh.GetVertexLights(false, -1);
    REQUIRE(close(colors[0].glmValue(), glm::vec3(0.5f, 0.5f, 0.25f) * first_contribution));
    REQUIRE(close(colors[1].glmValue(), glm::vec3(0.5f, 0.5f, 0.25f) * second_contribution));
    REQUIRE((mesh.vertex_lighting_ready && (mesh.flags & W8_MESH_VERTEX_LIGHTING_DIRTY)));

    mesh.setMaterial(nullptr, 0, srMeshModel::SIDE_FRONT);
    colors[0].SetZero();
    colors[1].SetZero();
    REQUIRE(BakeInstanceVertexLighting(&instance, &light, false));
    REQUIRE(close(colors[0].glmValue(), light.diffuse * first_contribution));
    REQUIRE(close(colors[1].glmValue(), light.diffuse * second_contribution));
    mesh.setMaterial(material, 0, srMeshModel::SIDE_FRONT);

    colors[0].SetZero();
    colors[1].SetZero();
    const auto world_normal = glm::normalize(glm::vec3(0, 2, -1));
    const auto position = glm::dvec3(glm::vec3(10, 0, -2) + world_normal * 0.5f);
    light.setLocation(srVector3T<double>(position));
    light.setLinearAttenuation(1, 1.0f / 511);
    REQUIRE(BakeInstanceVertexLighting(&instance, &light, false));
    REQUIRE(close(colors[0].glmValue(), glm::vec3(0.25f, 0.25f, 0.125f)));
    REQUIRE(colors[1] == srVector3(0, 0, 0));

    colors[0].SetZero();
    light.setLocation(10, 0, -2); // Coincident light and vertex remain finite.
    REQUIRE(BakeInstanceVertexLighting(&instance, &light, false));
    REQUIRE(colors[0] == srVector3(0, 0, 0));
    light.setName("Sun");
    light.setLocation(srVector3T<double>(position));
    REQUIRE(BakeInstanceVertexLighting(&instance, &light, true));
    REQUIRE(colors[0] == srVector3(0, 0, 0));
    auto world = instance.getWorldSpaceMatrix<glm::mat4>();
    mesh.flags &= ~W8_MESH_VERTEX_LIGHTING_DIRTY;
    mesh.RevealVerticesNear({10, 0, -2}, 0.25f, world);
    REQUIRE((colors[0] == srVector3(1, 1, 1) && colors[1] == srVector3(0, 0, 0)));
    REQUIRE(mesh.flags & W8_MESH_VERTEX_LIGHTING_DIRTY);
    mesh.RevealVerticesNear({10, 0, -4}, 0.25f, world);
    REQUIRE((colors[0] == srVector3(1, 1, 1) && colors[1] == srVector3(1, 1, 1)));
    colors[1].SetZero();
    mesh.RevealVerticesNear({10, 0, -4}, -1, world);
    REQUIRE(colors[1] == srVector3(0, 0, 0));
    mesh.RevealVerticesNear(glm::vec3(std::numeric_limits<float>::quiet_NaN()), 1, world);
    REQUIRE(colors[1] == srVector3(0, 0, 0));
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
    REQUIRE((mesh.getVertexLoc()[index] == srVector3(2, 0, 0)));
    REQUIRE((mesh.getVertexDCG(0, 0)[index] == glm::vec4(0.2f, 0.4f, 0.6f, 0.25f)));
    REQUIRE((mesh.getVertexDIG(0, 0)[index] == srVector3(2, 3, 4)));
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
    std::array<srVector3, 513> vertices;
    std::array<float, 513> distances;
    std::array<w8_ulong, 513> flags{};
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        vertices[i] = srVector3(i % 3 ? 1 : -1, 0, 0);
        flags[i] = 1u << 2;
    }
    REQUIRE((srTriangleCuller::setClipFlags(flags.data(), distances.data(), vertices.data(),
                                         {1, 0, 0, 0}, 31, vertices.size(), 0)));
    for (std::size_t i = 0; i < vertices.size(); ++i)
        REQUIRE((flags[i] == ((1u << 2) | (i % 3 ? 0 : 1u << 31))));
    REQUIRE((srTriangleCuller::setClipFlags(flags.data(), distances.data(), vertices.data(),
                                         {1, 0, 0, 0}, 5, vertices.size(), 1)));
    for (std::size_t i = 0; i < vertices.size(); ++i)
        REQUIRE((flags[i] == (i % 3 ? 0 : 1u << 5)));

    const std::array<srVector3, 9> positions{{{-0.5f, -0.5f, 0},
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
    std::array<w8_ulong, 9> active, remap;
    srTriangleCuller::Input input{};
    input.triangle_count = 3;
    input.vertex_count = positions.size();
    input.cull_mode = 2;
    input.triangles = triangles.data();
    input.vertices = positions.data();
    input.clip_planes = planes.data();
    input.model_view = input.inverse_model_view = &identity;
    input.clip_mask = 0x3f;
    srTriangleCuller::Output output{};
    output.indices = indices.data();
    output.avt = active.data();
    output.vertex_remap = remap.data();
    REQUIRE((srTriangleCuller::cull(output, input)));
    REQUIRE((output.triangle_count == 2 && indices[0] == 0 && indices[1] == 2));
    REQUIRE((output.vertex_count == 6));
    const std::array<w8_ulong, 6> expected{{0, 1, 2, 6, 7, 8}};
    for (unsigned i = 0; i < expected.size(); ++i)
        REQUIRE((active[i] == expected[i] && remap[expected[i]] == i));
    // Active triangle filtering must keep source order and allow in-place compaction.
    indices = {2, 1, 0};
    const std::array<glm::vec4, 3> facing{{{0, 0, 0, 1}, {0, 0, 0, -1}, {0, 0, 0, 1}}};
    input.active_triangles = indices.data();
    input.active_triangle_count = 3;
    input.projected_vertices = facing.data();
    input.cull_mode = 0;
    input.clip_mask = 0;
    REQUIRE((srTriangleCuller::cull(output, input)));
    REQUIRE((output.triangle_count == 2 && indices[0] == 2 && indices[1] == 0));
    input.active_triangle_count = 0;
    REQUIRE((!srTriangleCuller::cull(output, input)));
    REQUIRE((output.triangle_count == 0 && output.vertex_count == 0));
    const auto clip = srTriangleCuller::getClipMask({1, 0, 0}, 0, planes.data(), 0x3f);
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
    mesh->getVertexLoc()[0] = srVector3(-4, -3, -2);
    mesh->getVertexLoc()[1] = srVector3(2, 3, 4);
    srModel& model = *mesh;
    auto box = model.getBoundingBox();
    const auto sphere = model.getBoundingSphere();
    REQUIRE(box.minimum == glm::vec3(-4, -3, -2));
    REQUIRE(box.maximum == glm::vec3(2, 3, 4));
    REQUIRE(sphere.center == glm::vec3(-1, 0, 1));
    REQUIRE(std::abs(sphere.radius - std::sqrt(27.0f) * 1.00001f) < 1e-5f);
    box.minimum = glm::vec3(99);
    REQUIRE(model.getBoundingBox().minimum == glm::vec3(-4, -3, -2));

    srModelInstance instance;
    instance.setModel(mesh);
    srNode& node = instance;
    const auto local = node.getLocalBounds();
    REQUIRE(local.state == 1);
    REQUIRE(local.minimum == model.getBoundingBox().minimum);
    REQUIRE(local.maximum == model.getBoundingBox().maximum);
    REQUIRE(local.center == sphere.center);
    REQUIRE(local.radius == sphere.radius);

    auto draw = mesh->getTriMesh();
    draw.bounds_radius = 99;
    REQUIRE(mesh->getTriMesh().bounds_radius == sphere.radius);
    mesh->getVertexLoc()[1] = srVector3(4, 5, 6);
    mesh->setDirtyAll();
    REQUIRE(model.getBoundingBox().maximum == glm::vec3(4, 5, 6));
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
    linked.getVertexLoc()[0] = srVector3(-2, -4, -6);
    auto part = std::make_unique<stMeshModel>(0, 1);
    part->getVertexLoc()[0] = srVector3(2, 4, 6);
    linked.AddPart(std::move(part));
    srModel& linked_model = linked;
    REQUIRE(linked_model.getBoundingBox().minimum == glm::vec3(-2, -4, -6));
    REQUIRE(linked_model.getBoundingBox().maximum == glm::vec3(2, 4, 6));
    REQUIRE(linked_model.getBoundingSphere().center == glm::vec3(0));
    REQUIRE(std::abs(linked_model.getBoundingSphere().radius - std::sqrt(56.0f)) < 1e-5f);
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

TEST_CASE("Octree queries own dense results across later queries", "[geometry][octree]")
{
    W8Octree tree(nullptr, nullptr);
    tree.m_spatial.m_minimum = {0, 0, 0};
    tree.m_spatial.m_maximum = {1000, 1000, 1000};
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
    tree.m_spatial.m_minimum = {0, 0, 0};
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
    W8OctBuildTree tree(1000, nullptr, nullptr, 16, 0);
    REQUIRE(tree.queryAlongSegment({0, 0, 0}, {0, 0, 0}, 1, 3).empty());
    tree.spatial.m_minimum = {0, 0, 0};
    tree.spatial.m_maximum = {100, 100, 100};
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

    W8OctBuildTree other(1000, nullptr, nullptr, 16, 0);
    other.spatial.m_minimum = {0, 0, 0};
    other.spatial.m_maximum = {100, 100, 100};
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
    tree.m_spatial.m_minimum = {0, 0, 0};
    tree.m_spatial.m_maximum = {100, 100, 100};
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

TEST_CASE("Spatial bounds are independent snapshots of working and clipped extents", "[geometry][octree]")
{
    W8OctSpatialState state;
    const srVector3 minimum(-30, -20, -10), maximum(10, 20, 30);
    state.SetWorkingBounds(&minimum, &maximum);
    state.m_clipped_minimum = {-3, -2, -1};
    state.m_clipped_maximum = {1, 2, 3};
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

#include <catch2/catch_test_macros.hpp>
#include "surrender/srBoundingBox.h"
#include "surrender/srMaterial.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/sr_api.h"
#include "wiz8/direction_angles.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/SoundEvent.h"
#include "wiz8/engine_code/PolyPick.h"
#include "wiz8/engine_code/IntervalGate.h"
#include "wiz8/local_code/ItemManager.h"
#include <glm/gtc/constants.hpp>
#include <array>
#include <cmath>
#include <limits>
#include <cstdio>
#include <cstdlib>


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
    REQUIRE(box.minimum == glm::vec3(-4, 2, -7));
    REQUIRE(box.maximum == glm::vec3(-1, 8, -1));
    REQUIRE(box.center() == glm::vec3(-2.5f, 5, -4));
    REQUIRE((box.contains({-4, 2, -7}) && !box.contains({0, 5, -4})));
    REQUIRE(box.overlaps({{-1, 8, -1}, {2, 9, 1}}));
    REQUIRE(!box.overlaps({{0, 2, -7}, {1, 8, -1}}));
    const auto matrix = glm::translate(glm::mat4(1), glm::vec3(10, 20, 30)) *
                        glm::eulerAngleY(glm::half_pi<float>());
    const auto rotated = box.transformed(matrix);
    REQUIRE(close(rotated.minimum, {3, 22, 31}));
    REQUIRE(close(rotated.maximum, {9, 28, 34}));
}

static void planes_and_segments()
{
    const srVector3 a(0, 2, 0), b(0, 2, 3), c(4, 2, 0);
    char surface = 99, material = 99;
    REQUIRE(GetGroundSurfaceInfo(&a, &surface, &material) == 2);
    REQUIRE((surface == 0 && material == 0));
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
    glm::mat4 world;
    instance.getWorldSpaceMatrix(world);
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

TEST_CASE("game_geometry_test")
{
    REQUIRE(srInit());
    bounds();
    planes_and_segments();
    camera_and_throw();
    linked_mesh_bounds();
    baked_lighting();
    REQUIRE(srExit());
    std::puts("ok: bounds, camera and collision geometry, mesh radii and baked lighting");

}

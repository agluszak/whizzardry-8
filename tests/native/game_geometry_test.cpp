#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <array>
#include <cmath>
#include <limits>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "glm/geometric.hpp"
#include "surrender_test_fixture.h"
#include "surrender/srBoundingBox.h"
#include "surrender/srMaterial.h"
#include "surrender/srModeler.h"
#include "surrender/srGERD.h"
#include "surrender/srTriangleCuller.h"
#include "surrender/srDD_SDLGPU.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/direction_angles.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/OctBuildPreTree.h"
#include "wiz8/engine_code/OctPreTree.h"
#include "wiz8/engine_code/Navigator.h"
#include "wiz8/engine_code/SoundEvent.h"
#include "wiz8/local_code/ItemManager.h"
#include "compat/compiler.h"
#include "geometry.h"
#include "surrender/srMath.h"
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
        REQUIRE((modeler.getTriangle(i, triangle)));
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
    modeler.getTriangle(0, first);
    modeler.getTriangle(1, second);
    REQUIRE(((first.flags & second.flags) != 0));
    second.shaders[0].value ^= 1;
    modeler.setTriangle(1, second);
    modeler.autoSmooth(0.3, 0);
    modeler.getTriangle(0, first);
    modeler.getTriangle(1, second);
    REQUIRE(((first.flags & second.flags) == 0));
    modeler.addFromModeler(modeler);
    REQUIRE((modeler.getTriangleCount() == 24));
    modeler.scale({2, 3, 1});
    modeler.move({10, 20, 30});
    float lower, upper;
    modeler.getAxialBounds(srModeler::AXIS_X, lower, upper);
    REQUIRE((lower == 9 && upper == 11));

    modeler.createSphere(8);
    REQUIRE((modeler.getTriangleCount() == 128));
    for (w8_ulong i = 0; i < modeler.getTriangleCount(); ++i) {
        modeler.getTriangle(i, triangle);
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
        modeler.getTriangle(i, triangle);
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
        modeler.getTriangle(i, triangle);
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
                modeler.getTriangle(i, triangle);
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
    float depth;
    const auto mask = srTriangleCuller::getClipMask({1, 0, 0}, 0, planes.data(), 0x3f, depth);
    REQUIRE((mask == (1u << 1) && std::isfinite(depth)));
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

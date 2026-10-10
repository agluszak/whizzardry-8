#include "surrender/srBoundingBox.h"
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
#include <cstdio>
#include <cstdlib>

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression);                          \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)

static bool close(const glm::vec3& actual, const glm::vec3& expected, float tolerance = 1e-4f)
{
    return glm::length(actual - expected) < tolerance;
}

static void bounds()
{
    const auto empty = boundingBox({});
    CHECK(empty.minimum == glm::vec3(0) && empty.maximum == glm::vec3(0));
    const std::array<srVector3, 3> points{{{-4, 2, -3}, {-1, 8, -7}, {-2, 5, -1}}};
    const auto box = boundingBox(points);
    CHECK(box.minimum == glm::vec3(-4, 2, -7));
    CHECK(box.maximum == glm::vec3(-1, 8, -1));
    CHECK(box.center() == glm::vec3(-2.5f, 5, -4));
    const auto matrix = glm::translate(glm::mat4(1), glm::vec3(10, 20, 30)) *
                        glm::eulerAngleY(glm::half_pi<float>());
    const auto rotated = box.transformed(matrix);
    CHECK(close(rotated.minimum, {3, 22, 31}));
    CHECK(close(rotated.maximum, {9, 28, 34}));
}

static void planes_and_segments()
{
    const srVector3 a(0, 2, 0), b(0, 2, 3), c(4, 2, 0);
    char surface = 99, material = 99;
    CHECK(GetGroundSurfaceInfo(&a, &surface, &material) == 2);
    CHECK(surface == 0 && material == 0);
    W8Plane plane{};
    SetPlaneFromThreePoints(&plane, &a, &b, &c);
    CHECK(close(plane.normal.glmValue(), {0, 1, 0}));
    CHECK(std::abs(SignedPlaneDistance(plane, {5, 4, 7}) - 2) < 1e-5f);
    SetPlaneFromThreePoints(&plane, &a, &a, &a);
    CHECK(plane.normal == srVector3(0, 0, 0) && plane.w == 0);

    const glm::vec3 start(0, 0, 0), end(2, 0, 2);
    const glm::vec3 crossing_a(0, 0, 2), crossing_b(2, 0, 0);
    CHECK(SegmentCrossesEdge(start, end, crossing_a, crossing_b, 1));
    CHECK(SegmentCrossesEdge(end, start, crossing_b, crossing_a, 1));
    const glm::vec3 parallel_a(0, 0, 1), parallel_b(1, 0, 2);
    CHECK(!SegmentCrossesEdge(start, end, parallel_a, parallel_b, 1));
    const glm::vec3 on_line(1, 0, 1), beyond(3, 0, 3), away(4, 0, 4);
    CHECK(SegmentCrossesEdge(start, end, on_line, beyond, 1));
    CHECK(!SegmentCrossesEdge(start, end, beyond, away, 1));
    CHECK(SegmentCrossesEdge(on_line, on_line, start, end, 1));
    CHECK(!SegmentCrossesEdge(parallel_a, parallel_a, start, end, 1));

    srVector3 vector(3, 4, 5), target(0, 2, 0), zero(0, 0, 0);
    CHECK(ProjectVectorOntoVector(&vector, &target));
    CHECK(vector == srVector3(0, 4, 0));
    CHECK(!ProjectVectorOntoVector(&vector, &zero));
    CHECK(vector == srVector3(0, 4, 0));
}

static void camera_and_throw()
{
    const auto up = directionAngles({0, 1, 0});
    CHECK(up.yaw == 0 && std::abs(up.pitch + glm::half_pi<float>()) < 1e-6f);
    const auto zero = directionAngles({0, 0, 0});
    CHECK(zero.yaw == 0 && zero.pitch == 0);
    srVector3 position(1, 2, 3);
    OffsetPositionByYawPitch(10, &position, glm::half_pi<float>(), 0);
    CHECK(close(position.glmValue(), {11, 2, 3}));
    OffsetPositionByYawPitch(5, &position, 0, glm::half_pi<float>());
    CHECK(close(position.glmValue(), {11, -3, 3}));

    GDCamera camera;
    camera.SetOrientation(glm::half_pi<float>(), 0);
    srVector3 forward;
    camera.GetForwardPoint(10, &forward);
    CHECK(close((forward - camera.m_position).glmValue(), {10, 0, 0}));
    glm::mat3 vertical(glm::eulerAngleX(glm::half_pi<float>()));
    camera.ApplyRotationMatrix(&vertical, nullptr);
    CHECK(std::isfinite(camera.m_yaw) && std::isfinite(camera.m_pitch));
    delete camera.m_manual_input_timer;

    const auto center = ItemThrowDirection({0.5f, 0.5f}, glm::mat3(1));
    const auto left = ItemThrowDirection({0, 0.5f}, glm::mat3(1));
    const auto right = ItemThrowDirection({1, 0.5f}, glm::mat3(1));
    const auto top = ItemThrowDirection({0.5f, 0}, glm::mat3(1));
    const auto bottom = ItemThrowDirection({0.5f, 1}, glm::mat3(1));
    CHECK(std::abs(glm::length(center) - 2500) < 0.01f);
    CHECK(std::abs(center.x) < 1e-4f && center.y < 0 && center.z > 0);
    CHECK(left.x < 0 && right.x > 0);
    CHECK(std::abs(left.y - center.y) < 1e-3f && std::abs(right.y - center.y) < 1e-3f);
    CHECK(top.y > center.y && bottom.y < center.y);
    CHECK(
        close(ItemThrowDirection({0.5f, 0.5f}, glm::mat3(glm::eulerAngleY(glm::half_pi<float>()))),
              {center.z, center.y, -center.x}, 0.01f));
}

int main()
{
    bounds();
    planes_and_segments();
    camera_and_throw();
    std::puts("ok: bounds, collision geometry, camera rotations and cursor throw directions");
}

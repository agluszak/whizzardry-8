#include "surrender/srClip.h"
#include "surrender/srLight.h"
#include "surrender/srMath.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>

#include <cstdlib>
#include <limits>

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression);                          \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)

static void CheckFloats(float actual, float expected)
{
    CHECK(std::bit_cast<unsigned>(actual) == std::bit_cast<unsigned>(expected));
}

static void ClippingAndLighting()
{
    std::array<srVector4, 513> vertices;
    std::array<SRDWORD, 513> culled;
    for (std::size_t i = 0; i < vertices.size(); ++i)
        vertices[i].Set(i % 3 ? 1 : -1, 0, 0, 1);
    srVector4 plane;
    plane.Set(1, 0, 0, 0);
    const auto count = srRender::collectOutside(culled, plane, vertices);
    CHECK(count == 171);
    for (SRDWORD i = 0; i < count; ++i)
        CHECK(culled[i] == i * 3);
    srVector4 boundary[] = {glm::vec4(-2, 0, 0, 1), glm::vec4(2, 0, 0, 1),  glm::vec4(0, -2, 0, 1),
                            glm::vec4(0, 2, 0, 1),  glm::vec4(0, 0, -2, 1), glm::vec4(0, 0, 2, 1),
                            glm::vec4(1, -1, 1, 1)};
    SRBYTE flags[7];
    srRender::clipFlags(flags, boundary);
    for (int i = 0; i < 6; ++i)
        CHECK(flags[i] == (1 << i));
    CHECK(flags[6] == 0);
    srMatrix4 identity;
    identity.SetIdentity();
    CHECK(srRender::testBoundingBox(identity, {-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}));
    CHECK(!srRender::testBoundingBox(identity, {2, 2, 2}, {3, 3, 3}));

    float light[] = {0, 0.01f, 0.5f, 1}, power[4], clamped[4];
    srLight::specularPower(power, {light, std::size(power)}, 1);
    CHECK(power[0] == 0 && power[1] > 0 && power[2] == 0.5f && power[3] == 1);
    srLight::specularPower(power, {light, std::size(power)}, 2);
    CHECK(power[0] == 0 && power[1] == 0 && power[2] == 0.25f && power[3] == 1);
    srLight::specularPower(power, {light, std::size(power)}, 127);
    srLight::specularPower(clamped, {light, std::size(clamped)}, 1000);
    for (int i = 0; i < 4; ++i)
        CheckFloats(power[i], clamped[i]);
}

static void PackedStorage()
{
    struct PackedPoint {
        unsigned char tag;
        srVector3 point;
    };
    static_assert(offsetof(PackedPoint, point) == 1);
    PackedPoint stored{0x5a, {2, -3, 4}};
    const glm::vec3 value = stored.point.glmValue();
    CHECK(value.x == 2 && value.y == -3 && value.z == 4);
    stored.point = value + glm::vec3(1, 2, 3);
    CHECK(stored.tag == 0x5a && stored.point.x == 3 && stored.point.y == -1 && stored.point.z == 7);

    stored.point.Normalize();
    CHECK(stored.tag == 0x5a && std::abs(stored.point.Length() - 1) < 1e-6f);

    srMatrix4 rows;
    rows.vectors[0].Set(2, 3, 5, 7);
    rows.vectors[1].Set(11, 13, 17, 19);
    rows.vectors[2].Set(23, 29, 31, 37);
    rows.vectors[3].Set(41, 43, 47, 53);
    const auto matrix = rows.glmValue();
    CHECK(matrix[3][0] == 7 && matrix[0][3] == 41);
    const srMatrix4 restored = matrix;
    for (int row = 0; row < 4; ++row)
        CHECK(restored.vectors[row].glmValue() == rows.vectors[row].glmValue());
    const auto transformed = matrix * glm::vec4(1, 2, 3, 1);
    CHECK(transformed.x == 30 && transformed.y == 107 && transformed.z == 211 &&
          transformed.w == 321);
}

static void VectorMethods()
{
    srVector3 point(3, 4, 0);
    CHECK(point.Length() == 5 && point.LengthSquared() == 25);
    CHECK(point.SetLength(10) == &point);
    CHECK(point == srVector3(6, 8, 0));
    CHECK(point.Normalize() == &point);
    CHECK(std::abs(point.Length() - 1) < 1e-6f);
    point.SetLength(-5);
    CHECK(std::abs(point.x + 3) < 1e-6f && std::abs(point.y + 4) < 1e-6f);

    srVector3 zero(-0.0f, 0, -0.0f);
    zero.Normalize()->SetLength(5);
    CheckFloats(zero.x, -0.0f);
    CheckFloats(zero.y, 0.0f);
    CheckFloats(zero.z, -0.0f);
    srVector3 tiny(std::numeric_limits<float>::denorm_min(), 0, 0);
    const auto before = tiny.x;
    tiny.Normalize();
    CheckFloats(tiny.x, before);

    srVector2T<double> horizontal(3, 4);
    horizontal.SetLength(10);
    CHECK(std::abs(horizontal.x - 6) < 1e-14 && std::abs(horizontal.y - 8) < 1e-14);
    horizontal.Normalize();
    CHECK(std::abs(horizontal.Length() - 1) < 1e-14);
    srVector4 color;
    color.Set(1, 2, 2, 4);
    CHECK(color.LengthSquared() == 25 && color.Length() == 5);

    // Supplied coefficients may deliberately encode scaling rather than an angle.
    srMatrix3T<float> rotation;
    rotation.SetIdentity();
    rotation.RotateAboutZ(2, 3);
    point.Set(1, 0, 0);
    CHECK(rotation.Transform(point) == srVector3(3, 2, 0));
    point.RotateAboutZ(2, 3);
    CHECK(point == srVector3(3, 2, 0));
    CHECK(rotation.TransformTransposed(srVector3(1, 0, 0)) == srVector3(3, -2, 0));
    rotation.SetIdentity();
    rotation.RotateAroundAxis(0.7, srVector3(0, 0, 0));
    const auto scaled = rotation.Transform(srVector3(1, 2, 3));
    CHECK(std::abs(scaled.x - std::cos(0.7)) < 1e-6f);
    CHECK(std::abs(scaled.z - 3 * std::cos(0.7)) < 1e-6f);
    rotation.SetIdentity();
    rotation.RotateAroundAxis(1, 0, srVector3(0, 0, 2));
    CHECK(rotation.Transform(srVector3(1, 0, 0)) == srVector3(0, 2, 0));
    CHECK(rotation.Transform(srVector3(0, 0, 1)) == srVector3(0, 0, 4));
}

int main()
{
    PackedStorage();
    VectorMethods();
    ClippingAndLighting();
    std::puts("ok: packed storage, guarded vector methods, rotations, clipping and lighting");
}

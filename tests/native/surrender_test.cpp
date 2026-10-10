#include "surrender/srBinFStream.h"
#include "surrender/srBinOStream.h"
#include "surrender/srClipPlane.h"
#include "surrender/srCore.h"
#include "surrender/srIlluminator.h"
#include "surrender/srLight.h"
#include "surrender/srModelInstance.h"
#include "surrender/srScene.h"
#include "surrender/srTriMeshPipeline.h"
#include "surrender/srVertexPipe.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numbers>
#include <random>
#include <type_traits>

#define CHECK(expression)                                                                          \
    do {                                                                                          \
        if (!(expression)) {                                                                      \
            fprintf(stderr, "line %d: %s\n", __LINE__, #expression);                               \
            return false;                                                                         \
        }                                                                                         \
    } while (0)

template <class T>
constexpr bool noncopyable = !std::is_copy_constructible_v<T> && !std::is_copy_assignable_v<T>;

static_assert(noncopyable<srVertexPipe>);
static_assert(noncopyable<srBinIFStream>);
static_assert(noncopyable<srBinOFStream>);
static_assert(noncopyable<srBinIOFStream>);

static_assert(std::is_copy_constructible_v<srBinIMStream>);
static_assert(std::is_copy_assignable_v<srBinIMStream>);
static_assert(std::is_copy_constructible_v<srBinOMStream>);
static_assert(std::is_copy_assignable_v<srBinOMStream>);
static_assert(std::is_destructible_v<srIlluminator>);
static_assert(std::is_destructible_v<srLight>);

template <class T>
static srMatrix3T<T> formerRotation(double sine, double cosine, const srVector3T<T>& axis,
                                  bool cosine_first)
{
    srMatrix3T<T> rotation;
    const double complement = 1.0 - cosine;
    rotation.vectors[0].x = static_cast<T>(axis.x * axis.x + (T(1) - axis.x * axis.x) * cosine);
    rotation.vectors[1].y = static_cast<T>(axis.y * axis.y + (T(1) - axis.y * axis.y) * cosine);
    rotation.vectors[2].z = static_cast<T>(axis.z * axis.z + (T(1) - axis.z * axis.z) * cosine);
    if (cosine_first) {
        rotation.vectors[0].y = static_cast<T>((complement * axis.y) * axis.x - axis.z * sine);
        rotation.vectors[0].z = static_cast<T>((complement * axis.z) * axis.x + axis.y * sine);
        rotation.vectors[1].x = static_cast<T>((complement * axis.y) * axis.x + axis.z * sine);
        rotation.vectors[1].z = static_cast<T>((complement * axis.z) * axis.y - axis.x * sine);
        rotation.vectors[2].x = static_cast<T>((complement * axis.z) * axis.x - axis.y * sine);
        rotation.vectors[2].y = static_cast<T>((complement * axis.z) * axis.y + axis.x * sine);
    } else {
        rotation.vectors[0].y = static_cast<T>(axis.x * axis.y * complement - axis.z * sine);
        rotation.vectors[0].z = static_cast<T>(axis.x * axis.z * complement + axis.y * sine);
        rotation.vectors[1].x = static_cast<T>(axis.y * axis.x * complement + axis.z * sine);
        rotation.vectors[1].z = static_cast<T>(axis.y * axis.z * complement - axis.x * sine);
        rotation.vectors[2].x = static_cast<T>(axis.z * axis.x * complement - axis.y * sine);
        rotation.vectors[2].y = static_cast<T>(axis.z * axis.y * complement + axis.x * sine);
    }
    return rotation;
}

template <class T> static bool rotations()
{
    constexpr double pi = std::numbers::pi;
    constexpr std::array angles = {0.0, 1e-12, -1e-7, pi / 2, -pi / 2, pi - 1e-7, pi,
                                  pi + 1e-7, 2 * pi, -2 * pi, 0.731, -2.193};
    const T epsilon = std::numeric_limits<T>::epsilon();
    std::mt19937 random(0x55D40);
    long double max_delta = 0;
    size_t changed = 0;
    for (int sample = 0; sample < 2048; ++sample) {
        srVector3T<T> axis;
        if (sample < 6) {
            axis.SetZero();
            if (sample / 2 == 0) axis.x = sample % 2 ? T(-1) : T(1);
            if (sample / 2 == 1) axis.y = sample % 2 ? T(-1) : T(1);
            if (sample / 2 == 2) axis.z = sample % 2 ? T(-1) : T(1);
        } else if (sample < 9) {
            axis.Set(T(1), epsilon, -epsilon);
            if (sample == 7) std::swap(axis.x, axis.y);
            if (sample == 8) std::swap(axis.x, axis.z);
        } else {
            auto component = [&] { return T(int(random() % 2000001) - 1000000) / T(1000000); };
            axis.Set(component(), component(), component());
        }
        axis *= T(1) / axis.Length();
        for (double angle : angles) {
            const double sine = std::sin(angle);
            const double cosine = std::cos(angle);
            const auto game = formerRotation(sine, cosine, axis, false);
            const auto renderer = formerRotation(sine, cosine, axis, true);
            srMatrix3T<T> actual;
            actual.SetIdentity();
            CHECK(actual.RotateAroundAxis(sine, cosine, axis) == &actual);
            CHECK(actual == game);
            srMatrix3T<T> by_angle;
            by_angle.SetIdentity();
            CHECK(by_angle.RotateAroundAxis(angle, axis) == &by_angle);
            CHECK(by_angle == actual);

            const long double a[] = {axis.x, axis.y, axis.z};
            for (int row = 0; row < 3; ++row) {
                const auto& v = actual.vectors[row];
                const auto& r = renderer.vectors[row];
                const T values[] = {v.x, v.y, v.z};
                const T prior[] = {r.x, r.y, r.z};
                for (int column = 0; column < 3; ++column) {
                    const auto delta = std::abs(static_cast<long double>(values[column]) - prior[column]);
                    max_delta = std::max(max_delta, delta);
                    changed += values[column] != prior[column];
                    CHECK(delta <= 2 * epsilon);
                    long double reference = a[row] * a[column] * (1.0L - cosine);
                    if (row == column) {
                        reference += cosine;
                    } else {
                        const int remaining = 3 - row - column;
                        const int sign = (column == (row + 1) % 3) ? -1 : 1;
                        reference += sign * a[remaining] * sine;
                    }
                    CHECK(std::abs(values[column] - reference) <= 4 * epsilon);
                }
                for (int other = 0; other < 3; ++other) {
                    const T dot = DotProduct(actual.vectors[row], actual.vectors[other]);
                    CHECK(std::abs(dot - T(row == other)) <= 8 * epsilon);
                }
            }

            srMatrix3T<T> composed;
            composed.SetIdentity();
            composed.RotateAboutY(0.37);
            auto expected = composed;
            expected.MultiplyBy(game);
            composed.RotateAroundAxis(sine, cosine, axis);
            CHECK(composed == expected);
        }
    }
    printf("%s rotation: max former-renderer delta %.9Lg (%zu changed entries)\n",
           std::is_same_v<T, float> ? "float" : "double", max_delta, changed);
    return true;
}

struct ModelInstance : srModelInstance {};

static bool interfaces()
{
    CHECK(srInit() == 1);
    {
        srScene scene;
        scene.setAmbientLight(0.3f, 0.4f, 0.5f);
        CHECK(scene.getAmbientLight() == srVector3T<float>(0.3f, 0.4f, 0.5f));
        scene.setAmbientLight(srVector3T<float>(0.6f, 0.7f, 0.8f));
        srVector3T<float> color;
        scene.getAmbientLight(color);
        CHECK(color == srVector3T<float>(0.6f, 0.7f, 0.8f));
        scene.setFogColor(0.1f, 0.2f, 0.3f);
        CHECK(scene.getFogColor() == srVector3T<float>(0.1f, 0.2f, 0.3f));
        scene.setFogColor(srVector3T<float>(0.4f, 0.5f, 0.6f));
        scene.getFogColor(color);
        CHECK(color == scene.getFogColor());

        ModelInstance model;
        CHECK(!model.isAligned());
        model.setAlignAxis(srVector3T<float>(3, 0, 4));
        CHECK(model.isAligned());
        CHECK(model.getAlignAxis() == srVector3T<float>(0.6f, 0, 0.8f));
        model.setAlignment(0);
        CHECK(!model.isAligned());
        model.setAlignment(2);
        CHECK(model.isAligned());
        model.setAlignAxis(srVector3T<float>(0, 0, 0));
        CHECK(model.getAlignAxis() == srVector3T<float>(0, 0, 0));
        CHECK(model.isAligned());
        model.setExclusionMask(0x12345678);
        CHECK(model.getExclusionMask() == 0x12345678);

        srClipPlane clip;
        srVector4T<float> plane;
        plane.Set(1, 2, 3, 4);
        clip.setClipPlane(plane);
        srVector4T<float> output;
        clip.getClipPlane(output);
        CHECK(output.x == 1 && output.y == 2 && output.z == 3 && output.w == 4);
        const auto returned = clip.getClipPlane();
        CHECK(returned.x == 1 && returned.y == 2 && returned.z == 3 && returned.w == 4);
        clip.setClipType(srClipPlane::CLIP_POSITIONAL_0);
        CHECK(clip.getClipType() == srClipPlane::CLIP_POSITIONAL_0);

        srIlluminator illuminator;
        srLight light;
    }
    CHECK(srExit() == 1);

    srBinOMStream stream;
    stream.putDWord(0x12345678);
    auto copy = stream;
    copy.seek(0);
    copy.putDWord(0x87654321);
    srBinIMStream original(stream.getPtr(), stream.getSize());
    CHECK(original.getDWord() == 0x12345678);
    srBinOMStream assigned;
    assigned = copy;
    srBinIMStream input(assigned.getPtr(), assigned.getSize());
    auto borrowed = input;
    CHECK(borrowed.getDWord() == 0x87654321 && input.tell() == 0);
    return true;
}

int main()
{
    if (!rotations<float>() || !rotations<double>() || !interfaces()) return 1;
    puts("ok: unified SurRender rotation, accessors and copy ownership");
}

#include "surrender/srVectorMath.h"
#include "surrender/srARGB.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <vector>

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

static void Elementary()
{
    std::mt19937 random(0x658e0);
    std::uniform_real_distribution<float> values(-4, 4);
    for (std::size_t count : {0, 1, 3, 16, 255, 256, 257, 513}) {
        std::vector<float> a(count), b(count), out(count), alias(count);
        std::ranges::generate(a, [&] { return values(random); });
        std::ranges::generate(b, [&] { return values(random); });
        alias = b;
        srMath::mul(alias, a, alias);
        for (std::size_t i = 0; i < count; ++i)
            CheckFloats(alias[i], a[i] * b[i]);
        srMath::mul(out, 0.25f, a);
        for (std::size_t i = 0; i < count; ++i)
            CheckFloats(out[i], 0.25f * a[i]);
        srMath::add(out, -2.0f, a);
        for (std::size_t i = 0; i < count; ++i)
            CheckFloats(out[i], -2.0f + a[i]);
        srMath::sub(out, 1.0f, a);
        for (std::size_t i = 0; i < count; ++i)
            CheckFloats(out[i], 1.0f - a[i]);
        alias = b;
        srMath::axpy(alias, a, b, alias);
        for (std::size_t i = 0; i < count; ++i)
            CheckFloats(alias[i], b[i] * b[i] + a[i]);
        srMath::neg(out, a);
        for (std::size_t i = 0; i < count; ++i)
            CheckFloats(out[i], -a[i]);
    }
    const float nan = std::numeric_limits<float>::quiet_NaN();
    float special[] = {-INFINITY, -1, -0.0f, 0, 0.5f, 1, 2, INFINITY, nan};
    float out[std::size(special)];
    srMath::clampUnit(out, {special, std::size(out)});
    for (std::size_t i = 0; i < std::size(special); ++i)
        CheckFloats(out[i], special[i] < 0 ? 0 : special[i] > 1 ? 1 : special[i]);
    srMath::clampMin(out, {special, std::size(out)}, 0.0f);
    for (std::size_t i = 0; i < std::size(special); ++i)
        CheckFloats(out[i], special[i] <= 0 ? 0 : special[i]);
    float zeros[] = {0, -0.0f};
    CHECK(srMath::isZero(zeros) && srMath::isZero({}));
    CHECK(!srMath::isZero(special));
    CHECK(!srMath::isZero({&nan, 1}));
    srMath::copyIndexed(std::span<SRDWORD>{}, nullptr, {});
    srMath::transform(std::span<srVector3>{}, {}, srMatrix4{});
}

static void Geometry()
{
    srMatrix4 matrix;
    matrix.SetIdentity();
    matrix.vectors[0].w = 10;
    matrix.vectors[1].w = -2;
    matrix.vectors[2].w = 3;
    srVector3 positions[] = {{1, 2, 3}, {-4, 5, -6}, {7, -8, 9}};
    srVector3 out[3], alias[3];
    srVector4 homogeneous[3];
    srMath::transform(out, {positions, std::size(out)}, matrix);
    srMath::transform(homogeneous, {positions, std::size(homogeneous)}, matrix);
    std::ranges::copy(positions, alias);
    srMath::transform(alias, {alias, std::size(alias)}, matrix);
    for (int i = 0; i < 3; ++i) {
        CHECK(out[i].x == positions[i].x + 10 && out[i].y == positions[i].y - 2);
        CHECK(out[i].z == positions[i].z + 3);
        CHECK(alias[i].x == out[i].x && alias[i].y == out[i].y && alias[i].z == out[i].z);
        CHECK(homogeneous[i].x == out[i].x && homogeneous[i].y == out[i].y);
        CHECK(homogeneous[i].z == out[i].z && homogeneous[i].w == 1);
    }
    SRDWORD indices[] = {2, 0, 2};
    srMath::transformIndexed(alias, positions, indices, matrix);
    for (int i = 0; i < 3; ++i) {
        CHECK(alias[i].x == out[indices[i]].x && alias[i].y == out[indices[i]].y);
        CHECK(alias[i].z == out[indices[i]].z);
    }
    srVector3 minimum, maximum;
    srMath::minMax(positions, minimum, maximum);
    CHECK(minimum.x == -4 && minimum.y == -8 && minimum.z == -6);
    CHECK(maximum.x == 7 && maximum.y == 5 && maximum.z == 9);
    float lengths[3];
    srMath::dir(out, {lengths, std::size(out)}, {positions, std::size(out)});
    for (int i = 0; i < 3; ++i) {
        CheckFloats(lengths[i], positions[i].Length());
        CHECK(std::fabs(out[i].Length() - 1) < 1e-6f);
    }
    srMath::normalize(alias, {positions, std::size(alias)}, 2);
    for (const auto& v : alias)
        CHECK(std::fabs(v.Length() - 2) < 1e-6f);
    float zero_length[] = {0};
    srVector3 zero[] = {{0, 0, 0}}, normalized[1];
    srMath::dir(normalized, {zero_length, std::size(normalized)}, {zero, std::size(normalized)});
    CHECK(zero_length[0] == 0 && std::isnan(normalized[0].x));
    srMath::normalize(normalized, {zero, std::size(normalized)}, 1);
    CHECK(std::isnan(normalized[0].x));
    srVector4 projected[7];
    projected[0].Set(-2, 0, 0, 1);
    projected[1].Set(2, 0, 0, 1);
    projected[2].Set(0, -2, 0, 1);
    projected[3].Set(0, 2, 0, 1);
    projected[4].Set(0, 0, -2, 1);
    projected[5].Set(0, 0, 2, 1);
    projected[6].Set(1, -1, 1, 1);
    SRBYTE flags[7];
    srMath::srGetClipFlags(flags, {projected, std::size(flags)});
    for (int i = 0; i < 6; ++i)
        CHECK(flags[i] == (1 << i));
    CHECK(flags[6] == 0);
    matrix.SetIdentity();
    srMath::transformOrtho(projected, {projected, std::size(projected)}, matrix);
    CHECK(projected[0].x == -2 && projected[0].w == 1);
    CHECK(srMath::srTestBoundingBox(matrix, {-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}));
    CHECK(!srMath::srTestBoundingBox(matrix, {2, 2, 2}, {3, 3, 3}));
    srMatrix4 product;
    srMath::mul(product, matrix, matrix);
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            CHECK((&product.vectors[row].x)[column] == (row == column));

    srMatrix4 scale;
    scale.SetIdentity();
    scale.vectors[0].x = 2;
    scale.vectors[1].y = 3;
    scale.vectors[2].z = 4;
    matrix.vectors[0].w = 10;
    matrix.vectors[1].w = -2;
    matrix.vectors[2].w = 3;
    srMath::mul(product, matrix, scale);
    srMath::transform(out, {positions, std::size(out)}, product);
    CHECK(out[0].x == 12 && out[0].y == 4 && out[0].z == 15);
    srMath::mul(product, scale, matrix);
    srMath::transform(out, {positions, std::size(out)}, product);
    CHECK(out[0].x == 22 && out[0].y == 0 && out[0].z == 24);

    srVector4 perspective[1];
    perspective[0].Set(1, 2, 3, 1);
    matrix.SetIdentity();
    matrix.vectors[0].x = 2;
    matrix.vectors[0].z = 0.5f;
    matrix.vectors[1].y = 3;
    matrix.vectors[1].z = -0.5f;
    matrix.vectors[2].z = 4;
    matrix.vectors[2].w = 5;
    matrix.vectors[3].z = -1;
    matrix.vectors[3].w = 0;
    srMath::transformPerspective(perspective, {perspective, std::size(perspective)}, matrix);
    CHECK(perspective[0].x == 3.5f && perspective[0].y == 4.5f);
    CHECK(perspective[0].z == 17 && perspective[0].w == -3);
}

static void TypedRanges()
{
    std::mt19937 random(0x661c0);
    std::uniform_real_distribution<float> values(-4, 4);
    for (std::size_t count : {0, 1, 3, 16, 255, 256, 257, 513}) {
        std::vector<srVector3> a(count), b(count), out(count + 1), alias(count);
        for (auto& value : a)
            value.Set(values(random), values(random), values(random));
        for (auto& value : b)
            value.Set(values(random), values(random), values(random));
        out.back().Set(91, 92, 93);
        const auto destination = std::span{out}.first(count);
        srMath::sub(destination, a, b);
        for (std::size_t i = 0; i < count; ++i) {
            CheckFloats(out[i].x, a[i].x - b[i].x);
            CheckFloats(out[i].y, a[i].y - b[i].y);
            CheckFloats(out[i].z, a[i].z - b[i].z);
        }
        for (float scale : {-0.0f, 0.25f, 1.5f}) {
            alias = a;
            srMath::mul(alias, scale, alias);
            for (std::size_t i = 0; i < count; ++i) {
                CheckFloats(alias[i].x, scale * a[i].x);
                CheckFloats(alias[i].y, scale * a[i].y);
                CheckFloats(alias[i].z, scale * a[i].z);
            }
        }
        for (float weight : {0.0f, 0.25f, 1.0f}) {
            alias = b;
            srMath::lerp(alias, a, alias, weight);
            for (std::size_t i = 0; i < count; ++i) {
                CheckFloats(alias[i].x, weight * a[i].x + (1.0f - weight) * b[i].x);
                CheckFloats(alias[i].y, weight * a[i].y + (1.0f - weight) * b[i].y);
                CheckFloats(alias[i].z, weight * a[i].z + (1.0f - weight) * b[i].z);
            }
        }
        srMath::sub(destination, out, b);
        CHECK(out.back().x == 91 && out.back().y == 92 && out.back().z == 93);
    }
    const float nan = std::numeric_limits<float>::quiet_NaN();
    srVector4 colors[3], original[3], output[4];
    colors[0].Set(-INFINITY, -1, -0.0f, 0);
    colors[1].Set(0.5f, 1, 2, INFINITY);
    colors[2].Set(nan, -0.0f, nan, -0.0f);
    colors[2].x = std::bit_cast<float>(0x7f800001U);
    colors[2].z = std::bit_cast<float>(0xffc12345U);
    std::ranges::copy(colors, original);
    output[3].Set(91, 92, 93, 94);
    srMath::clampUnit({output, 3}, colors);
    srMath::clampUnit(colors, colors);
    const auto clamp = [](float value) { return value < 0 ? 0 : value > 1 ? 1 : value; };
    for (int i = 0; i < 3; ++i) {
        CheckFloats(colors[i].x, clamp(original[i].x));
        CheckFloats(colors[i].y, clamp(original[i].y));
        CheckFloats(colors[i].z, clamp(original[i].z));
        CheckFloats(colors[i].w, clamp(original[i].w));
        CheckFloats(output[i].x, colors[i].x);
        CheckFloats(output[i].y, colors[i].y);
        CheckFloats(output[i].z, colors[i].z);
        CheckFloats(output[i].w, colors[i].w);
    }
    CHECK(output[3].x == 91 && output[3].y == 92 && output[3].z == 93 && output[3].w == 94);
    srMath::clampUnit(std::span<srVector4>{}, {});
    srMath::dir(std::span<srVector3>{}, {}, std::span<const srVector3>{});
    srMath::dir(std::span<srVector3>{}, {}, std::span<const srVector4>{});
}

static void IndexedAndLighting()
{
    srVector3 positions[] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
    SRDWORD indices[] = {2, 0, 2};
    srVector4 colors[3];
    srMath::copyIndexed(colors, positions, indices);
    for (int i = 0; i < 3; ++i)
        CHECK(colors[i].x == positions[indices[i]].x && colors[i].w == 1);
    srARGB packed[1];
    packed[0].red = 255;
    packed[0].green = 128;
    packed[0].blue = 0;
    packed[0].alpha = 64;
    SRDWORD one_index[] = {0};
    srMath::copyIndexed({colors, 1}, packed, one_index);
    CHECK(colors[0].x == 1 && colors[0].z == 0);
    CheckFloats(colors[0].y, 128 * (1.0f / 255.0f));
    CheckFloats(colors[0].w, 64 * (1.0f / 255.0f));
    float w[] = {0.25f, 0.5f, 0.75f};
    for (int i = 0; i < 3; ++i)
        colors[i].Set(positions[i].x, positions[i].y, positions[i].z, 1);
    srMath::copyW(colors, {w, std::size(colors)});
    for (int i = 0; i < 3; ++i)
        CHECK(colors[i].x == positions[i].x && colors[i].w == w[i]);
    srMath::copyW(colors, 0.125f);
    for (int i = 0; i < 3; ++i)
        CHECK(colors[i].x == positions[i].x && colors[i].w == 0.125f);
    srVector3i triangles[2];
    triangles[0].x = 0;
    triangles[0].y = 2;
    triangles[0].z = 4;
    triangles[1].x = 1;
    triangles[1].y = 3;
    triangles[1].z = 5;
    SRBYTE flags[6]{};
    SRDWORD selected[] = {1, 1}, active[6], inverse[6];
    srMath::srSetIndexed(flags, triangles, selected);
    CHECK(srMath::srCollectNonZero(active, {flags, std::size(active)}) == 3);
    CHECK(active[0] == 1 && active[1] == 3 && active[2] == 5);
    std::ranges::fill(inverse, 99);
    srMath::srRemapInverse(inverse, {active, 3});
    CHECK(inverse[1] == 0 && inverse[3] == 1 && inverse[5] == 2 && inverse[0] == 99);
    srVector3i remapped[2];
    srMath::srCopyIndexedRemap(remapped, triangles, selected, inverse);
    CHECK(remapped[0].x == 0 && remapped[0].y == 1 && remapped[0].z == 2);
    std::array<srVector4, 513> vertices;
    std::array<SRDWORD, 513> culled;
    for (std::size_t i = 0; i < vertices.size(); ++i)
        vertices[i].Set(i % 3 ? 1 : -1, 0, 0, 1);
    srVector4 plane;
    plane.Set(1, 0, 0, 0);
    const auto count = srMath::srCullNoClip(culled, plane, vertices);
    CHECK(count == 171);
    for (SRDWORD i = 0; i < count; ++i)
        CHECK(culled[i] == i * 3);
    float light[] = {0, 0.01f, 0.5f, 1}, power[4], clamped[4];
    srMath::srSpecularPow(power, {light, std::size(power)}, 1);
    CHECK(power[0] == 0 && power[1] > 0 && power[2] == 0.5f && power[3] == 1);
    srMath::srSpecularPow(power, {light, std::size(power)}, 2);
    CHECK(power[0] == 0 && power[1] == 0 && power[2] == 0.25f && power[3] == 1);
    srMath::srSpecularPow(power, {light, std::size(power)}, 127);
    srMath::srSpecularPow(clamped, {light, std::size(clamped)}, 1000);
    for (int i = 0; i < 4; ++i)
        CheckFloats(power[i], clamped[i]);
}

int main()
{
    // Math has no processor registration or srInit/srExit lifetime.
    Elementary();
    Geometry();
    TypedRanges();
    IndexedAndLighting();
    std::puts("ok: direct math, empty/in-place ranges, transforms, clipping, remaps and lighting");
}

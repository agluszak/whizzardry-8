#include "surrender/srVectorMath.h"
#include "surrender/srARGB.h"
#include "wiz8/compat/unaligned.h"

#include <algorithm>
#include <cassert>

namespace {
constexpr float clampUnitValue(float value)
{
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

constexpr double coefficients[0x12][4] = {
    {0, 1e-14, 1, 0},
    {-0.068979736114850004, 0.18179573075354999, 0.88948269483362996, -0.0032237222793199999},
    {-0.12576753110003999, 0.34382063299797, 0.78571136674362996, -0.0053941615606599999},
    {-0.17092256892982999, 0.48661079671178997, 0.68891153819327, -0.0067378060870700003},
    {-0.20507900130076001, 0.61089353418348002, 0.59915069732755, -0.0074389531232600002},
    {-0.22891455983919001, 0.71752488060732, 0.51637759148530005, -0.0076474469703200001},
    {-0.24312703917604001, 0.80744183884992005, 0.44045304108798999, -0.00748496029462},
    {-0.24841687096368001, 0.88162612503694004, 0.37117406021430999, -0.0070501036072999996},
    {-0.24547440268178, 0.94107687822182995, 0.30829269047641, -0.0064225898414199999},
    {-0.23497080142249999, 0.98679034859172998, 0.25153065736378, -0.0056666352687399996},
    {-0.21755174140842001, 1.01974500915189, 0.20059072482958001, -0.0048337418349100004},
    {-0.19383321994641001, 1.04089087244265, 0.15516544047627001, -0.0039649773154400002},
    {-0.16439899158457999, 1.05114205735211, 0.11494381932562001, -0.0030928469032899999},
    {-0.12979922349142001, 1.0513718575920701, 0.079616400344480004, -0.0022428316857099999},
    {-0.090550063549559995, 1.0424097253979201, 0.048879020037899998, -0.0014346549735899999},
    {-0.047133881807620001, 1.0250397112049401, 0.022435576368620001, -0.00068332584966999997},
    {-1e-14, 1.00000000000001, 0, 0},
    {-1e-14, 1.00000000000001, 0, 0},
};
constexpr float points[0x12][7] = {
    {0.000999900047f, 0.0316211991f, 0.177823499f, 0.421691209f, 0.649377584f, 0.805839717f,
     0.897685707f},
    {0.00474390015f, 0.0688759983f, 0.262442291f, 0.512291312f, 0.715745211f, 0.846017301f,
     0.919791996f},
    {0.0081093004f, 0.0900517032f, 0.300086111f, 0.547801077f, 0.740135908f, 0.860311508f,
     0.927529812f},
    {0.0111445002f, 0.1055675f, 0.324911505f, 0.570010126f, 0.75499022f, 0.86890173f, 0.932148993f},
    {0.0138889998f, 0.1178516f, 0.343295187f, 0.585914016f, 0.76545018f, 0.874900103f,
     0.935360909f},
    {0.0163755994f, 0.127967194f, 0.357724994f, 0.598101199f, 0.773370028f, 0.879414618f,
     0.937771082f},
    {0.0186313f, 0.136496499f, 0.369454414f, 0.607827604f, 0.779632986f, 0.882968307f,
     0.939663887f},
    {0.0206783991f, 0.143799901f, 0.379209489f, 0.615799904f, 0.784729183f, 0.885849416f,
     0.941195726f},
    {0.0225352999f, 0.150117606f, 0.387450188f, 0.622454882f, 0.788958073f, 0.888233185f,
     0.942461193f},
    {0.0242167003f, 0.155617207f, 0.394483387f, 0.628079116f, 0.792514384f, 0.890232801f,
     0.9435215f},
    {0.0257344991f, 0.160419807f, 0.400524408f, 0.632869899f, 0.795531213f, 0.891925573f,
     0.944418073f},
    {0.0270971991f, 0.164612293f, 0.405724406f, 0.636964977f, 0.798100889f, 0.893364906f,
     0.94517982f},
    {0.0283103995f, 0.168256894f, 0.410191387f, 0.640461802f, 0.800288618f, 0.89458847f,
     0.945826888f},
    {0.0293761995f, 0.171394899f, 0.413998604f, 0.643427312f, 0.802139223f, 0.895622194f,
     0.946373224f},
    {0.0302920006f, 0.174045995f, 0.417188197f, 0.645901084f, 0.803679705f, 0.896481812f,
     0.946827292f},
    {0.0310484003f, 0.176205605f, 0.419768512f, 0.647895396f, 0.804919481f, 0.897173107f,
     0.947192192f},
    {0.0316227004f, 0.177827701f, 0.421696186f, 0.649381399f, 0.805842102f, 0.897687078f,
     0.947463512f},
    {0.0316227004f, 0.177827701f, 0.421696186f, 0.649381399f, 0.805842102f, 0.897687078f,
     0.947463512f},
};
} // namespace

// FUNCTION: SURRENDER 0x10065EF0
void srMath::axpy(std::span<float> destination, std::span<const float> add_source,
                  std::span<const float> scale_source, std::span<const float> multiply_source)
{
    assert(add_source.size() >= destination.size());
    assert(scale_source.size() >= destination.size());
    assert(multiply_source.size() >= destination.size());
    const std::size_t count = destination.size();
    for (std::size_t index = 0; index < count; ++index) {
        destination[index] = scale_source[index] * multiply_source[index] + add_source[index];
    }
}

// FUNCTION: SURRENDER 0x10065FB0
void srMath::add(std::span<float> destination, float constant, std::span<const float> source)
{
    assert(source.size() >= destination.size());
    std::ranges::transform(source.first(destination.size()), destination.begin(),
                           [&](auto value_0) { return constant + value_0; });
}

// FUNCTION: SURRENDER 0x10066010
void srMath::sub(std::span<float> destination, float constant, std::span<const float> source)
{
    assert(source.size() >= destination.size());
    std::ranges::transform(source.first(destination.size()), destination.begin(),
                           [&](auto value_0) { return constant - value_0; });
}

// FUNCTION: SURRENDER 0x10066040
void srMath::sub(std::span<srVector3> destination, std::span<const srVector3> source_0,
                 std::span<const srVector3> source_1)
{
    assert(source_0.size() >= destination.size());
    assert(source_1.size() >= destination.size());
    std::ranges::transform(source_0.first(destination.size()), source_1.first(destination.size()),
                           destination.begin(),
                           [&](auto value_0, auto value_1) { return value_0 - value_1; });
}

// FUNCTION: SURRENDER 0x100660A0
void srMath::mul(std::span<float> destination, float constant, std::span<const float> source)
{
    assert(source.size() >= destination.size());
    std::ranges::transform(source.first(destination.size()), destination.begin(),
                           [&](auto value_0) { return constant * value_0; });
}

// FUNCTION: SURRENDER 0x100660D0
void srMath::mul(std::span<float> destination, std::span<const float> source_0,
                 std::span<const float> source_1)
{
    assert(source_0.size() >= destination.size());
    assert(source_1.size() >= destination.size());
    std::ranges::transform(source_0.first(destination.size()), source_1.first(destination.size()),
                           destination.begin(),
                           [&](auto value_0, auto value_1) { return value_0 * value_1; });
}

// FUNCTION: SURRENDER 0x100661C0
void srMath::lerp(std::span<srVector3> destination, std::span<const srVector3> target,
                  std::span<const srVector3> source, float constant)
{
    assert(target.size() >= destination.size());
    assert(source.size() >= destination.size());
    std::ranges::transform(target.first(destination.size()), source.first(destination.size()),
                           destination.begin(), [&](auto value_0, auto value_1) {
                               return srVector3{
                                   constant * value_0.x + (1.0f - constant) * value_1.x,
                                   constant * value_0.y + (1.0f - constant) * value_1.y,
                                   constant * value_0.z + (1.0f - constant) * value_1.z};
                           });
}

// FUNCTION: SURRENDER 0x10066270
void srMath::clampMin(std::span<float> destination, std::span<const float> source, float minimum)
{
    assert(source.size() >= destination.size());
    std::ranges::transform(source.first(destination.size()), destination.begin(),
                           [&](auto value_0) { return value_0 <= minimum ? minimum : value_0; });
}

// FUNCTION: SURRENDER 0x100662F0
void srMath::clampUnit(std::span<float> destination, std::span<const float> source)
{
    assert(source.size() >= destination.size());
    std::ranges::transform(source.first(destination.size()), destination.begin(), clampUnitValue);
}

void srMath::clampUnit(std::span<srVector4> destination, std::span<const srVector4> source)
{
    assert(source.size() >= destination.size());
    std::ranges::transform(source.first(destination.size()), destination.begin(),
                           [](const srVector4& value) {
                               srVector4 result;
                               result.x = clampUnitValue(value.x);
                               result.y = clampUnitValue(value.y);
                               result.z = clampUnitValue(value.z);
                               result.w = clampUnitValue(value.w);
                               return result;
                           });
}

// FUNCTION: SURRENDER 0x100663C0
void srMath::mulIndexed(std::span<float> destination, std::span<const float> linear_source,
                        const float* indexed_source, std::span<const SRDWORD> indices)
{
    assert(destination.size() >= indices.size());
    assert(linear_source.size() >= indices.size());
    const std::size_t count = indices.size();
    for (std::size_t index = 0; index < count; ++index) {
        SRDWORD source_index = indices[index];
        destination[index] = indexed_source[source_index] * linear_source[index];
    }
}

// FUNCTION: SURRENDER 0x100664B0
bool srMath::isZero(std::span<const float> source)
{
    return std::ranges::all_of(source, [](float value) { return value == 0.0f; });
}

// FUNCTION: SURRENDER 0x10066570
void srMath::invPoly(std::span<float> destination, std::span<const float> source,
                     const srVector3& poly)
{
    assert(source.size() >= destination.size());
    std::ranges::transform(
        source.first(destination.size()), destination.begin(),
        [&](auto value_0) { return 1.0f / ((poly.z * value_0 + poly.y) * value_0 + poly.x); });
}

// FUNCTION: SURRENDER 0x100665E0
void srMath::neg(std::span<float> destination, std::span<const float> source)
{
    assert(source.size() >= destination.size());
    std::ranges::transform(source.first(destination.size()), destination.begin(),
                           [&](auto value_0) { return -value_0; });
}

// FUNCTION: SURRENDER 0x10066640
void srMath::copyIndexed(std::span<srVector2> destination, const srVector2* source,
                         std::span<const SRDWORD> indices)
{
    assert(destination.size() >= indices.size());
    const std::size_t count = indices.size();
    for (std::size_t index = 0; index < count; ++index) {
        SRDWORD source_index = indices[index];
        destination[index] = source[source_index];
    }
}

// FUNCTION: SURRENDER 0x100666D0
void srMath::minMax(std::span<const srVector3> source, srVector3& minimum, srVector3& maximum)
{
    assert(!source.empty());
    const std::size_t count = source.size();
    minimum = source[0];
    maximum = source[0];
    w8_unaligned_float* minimum_components = &minimum.x;
    w8_unaligned_float* maximum_components = &maximum.x;
    for (std::size_t index = 1; index < count; ++index) {
        const w8_unaligned_float* components = &source[index].x;
        for (int component = 0; component < 3; ++component) {
            if (components[component] >= minimum_components[component]) {
                if (components[component] > maximum_components[component]) {
                    maximum_components[component] = components[component];
                }
            } else {
                minimum_components[component] = components[component];
            }
        }
    }
}

// FUNCTION: SURRENDER 0x10066760
void srMath::copy(std::span<srVector3> destination, std::span<const srVector4> source)
{
    assert(source.size() >= destination.size());
    const std::size_t count = destination.size();
    for (std::size_t index = 0; index < count; ++index) {
        destination[index].x = source[index].x;
        destination[index].y = source[index].y;
        destination[index].z = source[index].z;
    }
}

// FUNCTION: SURRENDER 0x100667D0
void srMath::add(std::span<srVector3> destination, const srVector3& constant,
                 std::span<const srVector3> vector_source)
{
    assert(vector_source.size() >= destination.size());
    std::ranges::transform(vector_source.first(destination.size()), destination.begin(),
                           [&](auto value_0) { return value_0 + constant; });
}

// FUNCTION: SURRENDER 0x10066820
void srMath::sub(std::span<srVector3> destination, const srVector3& constant,
                 std::span<const srVector3> vector_source)
{
    assert(vector_source.size() >= destination.size());
    std::ranges::transform(vector_source.first(destination.size()), destination.begin(),
                           [&](auto value_0) { return constant - value_0; });
}

// FUNCTION: SURRENDER 0x10066870
void srMath::mul(std::span<srVector3> destination, const srVector3& constant,
                 std::span<const srVector3> vector_source)
{
    assert(vector_source.size() >= destination.size());
    std::ranges::transform(vector_source.first(destination.size()), destination.begin(),
                           [&](auto value_0) { return value_0 * constant; });
}

void srMath::mul(std::span<srVector3> destination, float constant,
                 std::span<const srVector3> source)
{
    assert(source.size() >= destination.size());
    std::ranges::transform(
        source.first(destination.size()), destination.begin(), [constant](const srVector3& value) {
            return srVector3{constant * value.x, constant * value.y, constant * value.z};
        });
}

// FUNCTION: SURRENDER 0x10066AB0
void srMath::mul(std::span<srVector3> destination, std::span<const srVector3> vector_source,
                 std::span<const float> float_source)
{
    assert(vector_source.size() >= destination.size());
    assert(float_source.size() >= destination.size());
    const std::size_t count = destination.size();
    for (std::size_t index = 0; index < count; ++index) {
        destination[index].x = vector_source[index].x * float_source[index];
        destination[index].y = vector_source[index].y * float_source[index];
        destination[index].z = vector_source[index].z * float_source[index];
    }
}

// FUNCTION: SURRENDER 0x10066BF0
void srMath::length(std::span<float> destination, std::span<const srVector3> vectors)
{
    assert(vectors.size() >= destination.size());
    std::ranges::transform(vectors.first(destination.size()), destination.begin(),
                           [&](auto value_0) { return value_0.Length(); });
}

// FUNCTION: SURRENDER 0x10066C40
void srMath::dot(std::span<float> destination, const srVector3& constant,
                 std::span<const srVector3> vectors)
{
    assert(vectors.size() >= destination.size());
    std::ranges::transform(vectors.first(destination.size()), destination.begin(),
                           [&](auto value_0) { return DotProduct(value_0, constant); });
}

// FUNCTION: SURRENDER 0x10066C80
void srMath::dot(std::span<float> destination, const srVector4& constant,
                 std::span<const srVector3> vectors)
{
    assert(vectors.size() >= destination.size());
    std::ranges::transform(vectors.first(destination.size()), destination.begin(),
                           [&](auto value_0) {
                               return value_0.x * constant.x + value_0.y * constant.y +
                                      value_0.z * constant.z + constant.w;
                           });
}

// FUNCTION: SURRENDER 0x10066CC0
void srMath::dot(std::span<float> destination, std::span<const srVector3> vectors_0,
                 std::span<const srVector3> vectors_1)
{
    assert(vectors_0.size() >= destination.size());
    assert(vectors_1.size() >= destination.size());
    std::ranges::transform(vectors_0.first(destination.size()), vectors_1.first(destination.size()),
                           destination.begin(), [&](auto value_0, auto value_1) {
                               return DotProduct(value_0, value_1);
                           });
}

// FUNCTION: SURRENDER 0x10066D10
void srMath::normalize(std::span<srVector3> destination, std::span<const srVector3> vectors,
                       float length)
{
    assert(vectors.size() >= destination.size());
    const std::size_t count = destination.size();
    for (std::size_t index = 0; index < count; ++index) {
        const float magnitude = vectors[index].Length();
        const float scale = magnitude == 0.0f ? 0.0f : length / magnitude;
        destination[index] = vectors[index] * scale;
    }
}

// FUNCTION: SURRENDER 0x10066E70
void srMath::copyIndexed(std::span<srVector3> destination, const srVector3* source,
                         std::span<const SRDWORD> indices)
{
    assert(destination.size() >= indices.size());
    const std::size_t count = indices.size();
    for (std::size_t index = 0; index < count; ++index) {
        SRDWORD source_index = indices[index];
        destination[index] = source[source_index];
    }
}

// FUNCTION: SURRENDER 0x10066F10
void srMath::transform(std::span<srVector3> destination, std::span<const srVector3> vectors,
                       const srMatrix4& matrix)
{
    assert(vectors.size() >= destination.size());
    const std::size_t count = destination.size();
    const float* m = &matrix.vectors[0].x;
    for (std::size_t index = 0; index < count; ++index) {
        const srVector3 vector = vectors[index];
        destination[index].x = vector.z * m[2] + vector.x * m[0] + m[1] * vector.y + m[3];
        destination[index].y = vector.x * m[4] + m[5] * vector.y + vector.z * m[6] + m[7];
        destination[index].z = vector.y * m[9] + vector.z * m[10] + vector.x * m[8] + m[11];
    }
}

// FUNCTION: SURRENDER 0x10066FA0
void srMath::dir(std::span<srVector3> destination, std::span<float> lengths,
                 std::span<const srVector3> source)
{
    assert(lengths.size() >= destination.size());
    assert(source.size() >= destination.size());
    const std::size_t count = destination.size();
    for (std::size_t index = 0; index < count; ++index) {
        float length = source[index].Length();
        lengths[index] = length;
        destination[index].x = source[index].x / length;
        destination[index].y = source[index].y / length;
        destination[index].z = source[index].z / length;
    }
}

// FUNCTION: SURRENDER 0x10067010
void srMath::dir(std::span<srVector3> destination, std::span<float> lengths,
                 std::span<const srVector4> source)
{
    assert(lengths.size() >= destination.size());
    assert(source.size() >= destination.size());
    const std::size_t count = destination.size();
    for (std::size_t index = 0; index < count; ++index) {
        float length = source[index].xyz().Length();
        lengths[index] = length;
        destination[index].x = source[index].x / length;
        destination[index].y = source[index].y / length;
        destination[index].z = source[index].z / length;
    }
}

// FUNCTION: SURRENDER 0x100671D0
void srMath::add(std::span<srVector4> destination, const srVector4& constant,
                 std::span<const srVector4> vector_source)
{
    assert(vector_source.size() >= destination.size());
    std::ranges::transform(vector_source.first(destination.size()), destination.begin(),
                           [&](auto value_0) { return value_0 + constant; });
}

// FUNCTION: SURRENDER 0x100674F0
void srMath::mul(std::span<srVector4> destination, std::span<const srVector4> vector_source,
                 std::span<const float> float_source)
{
    assert(vector_source.size() >= destination.size());
    assert(float_source.size() >= destination.size());
    const std::size_t count = destination.size();
    for (std::size_t index = 0; index < count; ++index) {
        destination[index].x = vector_source[index].x * float_source[index];
        destination[index].y = vector_source[index].y * float_source[index];
        destination[index].z = vector_source[index].z * float_source[index];
        destination[index].w = vector_source[index].w * float_source[index];
    }
}

// FUNCTION: SURRENDER 0x10067720
void srMath::dot(std::span<float> destination, const srVector4& constant,
                 std::span<const srVector4> vectors)
{
    assert(vectors.size() >= destination.size());
    std::ranges::transform(vectors.first(destination.size()), destination.begin(),
                           [&](auto value_0) { return DotProduct(value_0, constant); });
}

// FUNCTION: SURRENDER 0x10067770
void srMath::dotIndexed(std::span<float> destination, const srVector4& constant,
                        const srVector4* vectors, std::span<const SRDWORD> indices)
{
    assert(destination.size() >= indices.size());
    const std::size_t count = indices.size();
    for (std::size_t index = 0; index < count; ++index) {
        SRDWORD source_index = indices[index];
        const srVector4* vector = &vectors[source_index];
        destination[index] = DotProduct(*vector, constant);
    }
}

// FUNCTION: SURRENDER 0x100678B0
void srMath::mulIndexed(std::span<srVector4> destination, const srVector4& constant,
                        const srVector4* indexed_source, std::span<const SRDWORD> indices)
{
    assert(destination.size() >= indices.size());
    const std::size_t count = indices.size();
    for (std::size_t index = 0; index < count; ++index) {
        SRDWORD source_index = indices[index];
        destination[index] = indexed_source[source_index] * constant;
    }
}

// FUNCTION: SURRENDER 0x10067910
void srMath::mulIndexed(std::span<srVector4> destination, std::span<const srVector4> linear_source,
                        const srVector4* indexed_source, std::span<const SRDWORD> indices)
{
    assert(destination.size() >= indices.size());
    assert(linear_source.size() >= indices.size());
    const std::size_t count = indices.size();
    for (std::size_t index = 0; index < count; ++index) {
        SRDWORD source_index = indices[index];
        destination[index] = linear_source[index] * indexed_source[source_index];
    }
}

// FUNCTION: SURRENDER 0x10067980
void srMath::copyIndexed(std::span<srVector4> destination, const srARGB* source,
                         std::span<const SRDWORD> indices)
{
    assert(destination.size() >= indices.size());
    const std::size_t count = indices.size();
    for (std::size_t index = 0; index < count; ++index) {
        SRDWORD source_index = indices[index];
        destination[index].x = source[source_index].red * (1.0f / 255.0f);
        destination[index].y = source[source_index].green * (1.0f / 255.0f);
        destination[index].z = source[source_index].blue * (1.0f / 255.0f);
        destination[index].w = source[source_index].alpha * (1.0f / 255.0f);
    }
}

// FUNCTION: SURRENDER 0x10067A10
void srMath::copyIndexed(std::span<srVector4> destination, const srVector4* source,
                         std::span<const SRDWORD> indices)
{
    assert(destination.size() >= indices.size());
    const std::size_t count = indices.size();
    for (std::size_t index = 0; index < count; ++index) {
        SRDWORD source_index = indices[index];
        destination[index] = source[source_index];
    }
}

// FUNCTION: SURRENDER 0x10067A60
void srMath::copyIndexed(std::span<srVector4> destination, const srVector3* source,
                         std::span<const SRDWORD> indices)
{
    assert(destination.size() >= indices.size());
    const std::size_t count = indices.size();
    for (std::size_t index = 0; index < count; ++index) {
        SRDWORD source_index = indices[index];
        destination[index].x = source[source_index].x;
        destination[index].y = source[source_index].y;
        destination[index].z = source[source_index].z;
        destination[index].w = 1.0f;
    }
}

// FUNCTION: SURRENDER 0x10067B50
void srMath::transformOrtho(std::span<srVector4> destination, std::span<const srVector4> source,
                            const srMatrix4& matrix)
{
    assert(source.size() >= destination.size());
    const std::size_t count = destination.size();
    const float* m = &matrix.vectors[0].x;
    for (std::size_t index = 0; index < count; ++index) {
        const srVector4 vector = source[index];
        destination[index].x = vector.w * m[3] + vector.x * m[0];
        destination[index].y = vector.y * m[5] + vector.w * m[7];
        destination[index].z = vector.w * m[11] + vector.z * m[10];
        destination[index].w = vector.w * m[15];
    }
}

// FUNCTION: SURRENDER 0x10067BC0
void srMath::transformPerspective(std::span<srVector4> destination,
                                  std::span<const srVector4> source, const srMatrix4& matrix)
{
    assert(source.size() >= destination.size());
    const std::size_t count = destination.size();
    const float* m = &matrix.vectors[0].x;
    for (std::size_t index = 0; index < count; ++index) {
        const srVector4 vector = source[index];
        destination[index].x = vector.z * m[2] + vector.x * m[0];
        destination[index].y = vector.y * m[5] + vector.z * m[6];
        destination[index].z = vector.z * m[10] + vector.w * m[11];
        destination[index].w = vector.z * m[14];
    }
}

// FUNCTION: SURRENDER 0x10067C30
void srMath::transform(std::span<srVector4> destination, std::span<const srVector4> vectors,
                       const srMatrix4& matrix)
{
    assert(vectors.size() >= destination.size());
    const std::size_t count = destination.size();
    const float* m = &matrix.vectors[0].x;
    for (std::size_t index = 0; index < count; ++index) {
        const srVector4 vector = vectors[index];
        destination[index].x =
            vector.y * m[1] + vector.z * m[2] + m[0] * vector.x + m[3] * vector.w;
        destination[index].y =
            vector.w * m[7] + vector.z * m[6] + m[4] * vector.x + m[5] * vector.y;
        destination[index].z =
            vector.z * m[10] + m[11] * vector.w + m[9] * vector.y + m[8] * vector.x;
        destination[index].w =
            vector.w * m[15] + m[13] * vector.y + m[12] * vector.x + vector.z * m[14];
    }
}

// FUNCTION: SURRENDER 0x10067D00
void srMath::transform(std::span<srVector4> destination, std::span<const srVector3> vectors,
                       const srMatrix4& matrix)
{
    assert(vectors.size() >= destination.size());
    const std::size_t count = destination.size();
    const float* m = &matrix.vectors[0].x;
    for (std::size_t index = 0; index < count; ++index) {
        const srVector3 vector = vectors[index];
        destination[index].x = vector.z * m[2] + vector.x * m[0] + m[1] * vector.y + m[3];
        destination[index].y = vector.x * m[4] + vector.z * m[6] + m[5] * vector.y + m[7];
        destination[index].z = vector.z * m[10] + m[9] * vector.y + vector.x * m[8] + m[11];
        destination[index].w = vector.x * m[12] + m[13] * vector.y + m[14] * vector.z + m[15];
    }
}

// FUNCTION: SURRENDER 0x10067DC0
void srMath::transformIndexed(std::span<srVector3> destination, const srVector3* source,
                              std::span<const SRDWORD> indices, const srMatrix4& matrix)
{
    assert(destination.size() >= indices.size());
    const std::size_t count = indices.size();
    const float* m = &matrix.vectors[0].x;
    for (std::size_t index = 0; index < count; ++index) {
        SRDWORD source_index = indices[index];
        const srVector3 vector = source[source_index];
        destination[index].x = vector.x * m[0] + vector.z * m[2] + m[1] * vector.y + m[3];
        destination[index].y = vector.x * m[4] + m[5] * vector.y + vector.z * m[6] + m[7];
        destination[index].z = vector.y * m[9] + vector.z * m[10] + vector.x * m[8] + m[11];
    }
}

// FUNCTION: SURRENDER 0x10067E60
void srMath::transformIndexed(std::span<srVector4> destination, const srVector3* source,
                              std::span<const SRDWORD> indices, const srMatrix4& matrix)
{
    assert(destination.size() >= indices.size());
    const std::size_t count = indices.size();
    const float* m = &matrix.vectors[0].x;
    for (std::size_t index = 0; index < count; ++index) {
        SRDWORD source_index = indices[index];
        const srVector3 vector = source[source_index];
        destination[index].x = vector.y * m[1] + vector.x * m[0] + vector.z * m[2] + m[3];
        destination[index].y = vector.x * m[4] + m[5] * vector.y + vector.z * m[6] + m[7];
        destination[index].z = vector.y * m[9] + vector.z * m[10] + vector.x * m[8] + m[11];
        destination[index].w = vector.z * m[14] + vector.x * m[12] + vector.y * m[13] + m[15];
    }
}

// FUNCTION: SURRENDER 0x10067F80
void srMath::axpy(std::span<srVector4> destination, std::span<const srVector4> add_source,
                  const srVector4& multiply_constant, std::span<const float> multiply_source)
{
    assert(add_source.size() >= destination.size());
    assert(multiply_source.size() >= destination.size());
    const std::size_t count = destination.size();
    for (std::size_t index = 0; index < count; ++index) {
        destination[index].x = multiply_constant.x * multiply_source[index] + add_source[index].x;
        destination[index].y = multiply_constant.y * multiply_source[index] + add_source[index].y;
        destination[index].z = multiply_source[index] * multiply_constant.z + add_source[index].z;
        destination[index].w = multiply_constant.w * multiply_source[index] + add_source[index].w;
    }
}

// FUNCTION: SURRENDER 0x10068180
void srMath::axpy(std::span<srVector4> destination, std::span<const srVector4> add_source,
                  const srVector4& multiply_constant, std::span<const float> multiply_source_0,
                  std::span<const float> multiply_source_1)
{
    assert(add_source.size() >= destination.size());
    assert(multiply_source_0.size() >= destination.size());
    assert(multiply_source_1.size() >= destination.size());
    const std::size_t count = destination.size();
    for (std::size_t index = 0; index < count; ++index) {
        destination[index].x =
            multiply_constant.x * multiply_source_0[index] * multiply_source_1[index] +
            add_source[index].x;
        destination[index].y =
            multiply_constant.y * multiply_source_0[index] * multiply_source_1[index] +
            add_source[index].y;
        destination[index].z =
            multiply_constant.z * multiply_source_0[index] * multiply_source_1[index] +
            add_source[index].z;
        destination[index].w =
            multiply_constant.w * multiply_source_0[index] * multiply_source_1[index] +
            add_source[index].w;
    }
}

// FUNCTION: SURRENDER 0x100683A0
void srMath::copyW(std::span<srVector4> destination, float constant)
{
    const std::size_t count = destination.size();
    for (std::size_t index = 0; index < count; ++index) {
        destination[index].w = constant;
    }
}

// FUNCTION: SURRENDER 0x100683C0
void srMath::copyW(std::span<srVector4> destination, std::span<const float> source)
{
    assert(source.size() >= destination.size());
    const std::size_t count = destination.size();
    for (std::size_t index = 0; index < count; ++index) {
        destination[index].w = source[index];
    }
}

/* The per-column term order of each product is a.x, a.y, a.w, a.z. */
// FUNCTION: SURRENDER 0x100685C0
void srMath::mul(srMatrix4& destination, const srMatrix4& source_0, const srMatrix4& source_1)
{
    assert(&destination != &source_0 && &destination != &source_1);
    float* result = &destination.vectors[0].x;
    const float* left = &source_0.vectors[0].x;
    const float* right = &source_1.vectors[0].x;
    for (int column = 0; column < 4; ++column) {
        float right_0 = right[column];
        float right_1 = right[4 + column];
        float right_2 = right[8 + column];
        float right_3 = right[12 + column];
        result[column] =
            right_2 * left[2] + right_3 * left[3] + right_1 * left[1] + right_0 * left[0];
        result[4 + column] =
            right_2 * left[6] + right_3 * left[7] + right_1 * left[5] + right_0 * left[4];
        result[8 + column] =
            right_2 * left[10] + right_3 * left[11] + right_1 * left[9] + right_0 * left[8];
        result[12 + column] =
            right_0 * left[12] + right_2 * left[14] + right_3 * left[15] + right_1 * left[13];
    }
}

// FUNCTION: SURRENDER 0x10068700
bool srMath::srTestBoundingBox(const srMatrix4& matrix, const srVector3& minimum,
                               const srVector3& maximum)
{
    const float* m = &matrix.vectors[0].x;
    float z_min = minimum.z * m[14];
    float y_min = m[13] * minimum.y;
    float w_min = m[12] * minimum.x + m[15] + y_min + z_min;
    if (w_min >= fabsf(m[0] * minimum.x + m[1] * minimum.y + m[2] * minimum.z + m[3]) &&
        w_min >= fabsf(m[5] * minimum.y + m[6] * minimum.z + m[4] * minimum.x + m[7]) &&
        w_min >= fabsf(m[9] * minimum.y + m[10] * minimum.z + m[8] * minimum.x + m[11])) {
        return true;
    }
    float xy_min = m[12] * minimum.x + m[15];
    float w_max = m[12] * maximum.x + m[15];
    float y_max = m[13] * maximum.y;
    float z_max = maximum.z * m[14];
    float w_000 = z_min + y_min + xy_min;
    float w_001 = z_max + y_min + xy_min;
    float w_010 = y_max + z_min + xy_min;
    float w_011 = z_max + y_max + xy_min;
    float w_100 = w_max + z_min + y_min;
    float w_101 = z_max + w_max + y_min;
    float w_110 = y_max + w_max + z_min;
    float w_111 = z_max + y_max + w_max;
    const float* row = m + 9;
    for (int plane = 2; plane >= 0; --plane, row -= 4) {
        float base_min = minimum.x * row[-1] + row[2];
        float ymin = row[0] * minimum.y;
        float zmin = row[1] * minimum.z;
        float base_max = row[-1] * maximum.x + row[2];
        float ymax = row[0] * maximum.y;
        float zmax = maximum.z * row[1];
        float v = zmin + ymin + base_min;
        if (-w_000 < v) {
            if (w_000 <= v && w_001 <= zmax + ymin + base_min && w_010 <= ymax + zmin + base_min &&
                w_011 <= zmax + ymax + base_min && w_100 <= base_max + zmin + ymin &&
                w_101 <= zmax + base_max + ymin && w_110 <= ymax + base_max + zmin &&
                w_111 <= zmax + ymax + base_max) {
                return false;
            }
        } else if (zmax + ymin + base_min <= -w_001 && ymax + zmin + base_min <= -w_010 &&
                   zmax + ymax + base_min <= -w_011 && base_max + zmin + ymin <= -w_100 &&
                   zmax + base_max + ymin <= -w_101 && ymax + base_max + zmin <= -w_110 &&
                   zmax + ymax + base_max <= -w_111) {
            return false;
        }
    }
    return true;
}

/* Table-driven specular power: the exponent is clamped to 127 and halved until it lies in [1,2);
   the fractional part of (reduced-1)*16 selects two rows of the coefficient table for
   lerp, and points[index+1][squarings] is the per-element dead-zone threshold below which the
   result is 0. */
// FUNCTION: SURRENDER 0x10068A80
void srMath::srSpecularPow(std::span<float> destination, std::span<const float> source,
                           float exponent)
{
    assert(source.size() >= destination.size());
    const std::size_t count = destination.size();
    SRDWORD squarings = 0;
    if (exponent > 127.0) {
        exponent = 127.0f;
    }
    while (exponent >= 2.0f) {
        exponent *= 0.5f;
        ++squarings;
    }
    float index_value = (exponent - 1.0f) * 16.0f;
    int index = static_cast<int>(index_value);
    index_value -= index;
    double complement = 1.0 - index_value;
    float coefficient_0 =
        (float)(index_value * coefficients[index + 1][0] + complement * coefficients[index][0]);
    float coefficient_1 =
        (float)(index_value * coefficients[index + 1][1] + complement * coefficients[index][1]);
    float coefficient_2 =
        (float)(index_value * coefficients[index + 1][2] + complement * coefficients[index][2]);
    float coefficient_3 =
        (float)(index_value * coefficients[index + 1][3] + complement * coefficients[index][3]);
    float threshold = points[index + 1][squarings];
    for (std::size_t element = 0; element < count; ++element) {
        if (source[element] > threshold) {
            float value = source[element];
            for (SRDWORD square = 0; square < squarings; ++square) {
                value *= value;
            }
            destination[element] =
                ((value * coefficient_0 + coefficient_1) * value + coefficient_2) * value +
                coefficient_3;
        } else {
            destination[element] = 0.0f;
        }
    }
}

// FUNCTION: SURRENDER 0x10068BC0
void srMath::srCopyIndexedRemap(std::span<srVector3i> destination, const srVector3i* source,
                                std::span<const SRDWORD> indices, const SRDWORD* remap)
{
    assert(destination.size() >= indices.size());
    const std::size_t count = indices.size();
    for (std::size_t index = 0; index < count; ++index) {
        SRDWORD source_index = indices[index];
        destination[index].x = remap[source[source_index].x];
        destination[index].y = remap[source[source_index].y];
        destination[index].z = remap[source[source_index].z];
    }
}

// FUNCTION: SURRENDER 0x10068C20
void srMath::srSetIndexed(SRBYTE* destination, const srVector3i* source,
                          std::span<const SRDWORD> indices)
{
    const std::size_t count = indices.size();
    for (std::size_t index = 0; index < count; ++index) {
        SRDWORD source_index = indices[index];
        destination[source[source_index].x] = 1;
        destination[source[source_index].y] = 1;
        destination[source[source_index].z] = 1;
    }
}

// FUNCTION: SURRENDER 0x10068D00
SRDWORD srMath::srCollectNonZero(std::span<SRDWORD> destination, std::span<const SRBYTE> source)
{
    assert(source.size() >= destination.size());
    const std::size_t count = destination.size();
    SRDWORD collected = 0;
    for (std::size_t index = 0; index < count; ++index) {
        if (source[index] != 0) {
            destination[collected] = index;
            ++collected;
        }
    }
    return collected;
}

// FUNCTION: SURRENDER 0x10068D30
void srMath::srRemapInverse(SRDWORD* destination, std::span<const SRDWORD> map)
{
    const std::size_t count = map.size();
    for (std::size_t index = 0; index < count; ++index) {
        destination[map[index]] = index;
    }
}

// FUNCTION: SURRENDER 0x10068DE0
SRDWORD srMath::srCullNoClip(std::span<SRDWORD> destination, const srVector4& constant,
                             std::span<const srVector4> vectors)
{
    assert(vectors.size() >= destination.size());
    const std::size_t count = destination.size();
    float dots[0x100];
    SRDWORD collected = 0;
    for (SRDWORD offset = 0; offset < count; offset += 0x100) {
        SRDWORD chunk = count - offset;
        if (chunk > 0x100) {
            chunk = 0x100;
        }
        srMath::dot({dots, chunk}, constant, vectors.subspan(offset, chunk));
        for (std::size_t index = 0; index < chunk; ++index) {
            if (dots[index] < 0.0f) {
                destination[collected] = index + offset;
                ++collected;
            }
        }
    }
    return collected;
}

// FUNCTION: SURRENDER 0x10068F40
void srMath::srGetClipFlags(std::span<SRBYTE> destination, std::span<const srVector4> source)
{
    assert(source.size() >= destination.size());
    const std::size_t count = destination.size();
    for (std::size_t index = 0; index < count; ++index) {
        float w = source[index].w;
        float negative_w = -w;
        SRBYTE flags = 0;
        if (negative_w > source[index].z) {
            flags = 0x10;
        }
        if (source[index].z > w) {
            flags |= 0x20;
        }
        if (negative_w > source[index].x) {
            flags |= 0x01;
        }
        if (source[index].x > w) {
            flags |= 0x02;
        }
        if (negative_w > source[index].y) {
            flags |= 0x04;
        }
        if (source[index].y > w) {
            flags |= 0x08;
        }
        destination[index] = flags;
    }
}

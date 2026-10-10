#pragma once

#include <fenv.h>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_access.hpp>

#include <span>
#include <type_traits>

/* Float-to-int through the FPU's current rounding mode (round to nearest, not truncation). */
inline w8_long srFloatToInt(double value)
{
    const double rounded = rint(value);
    // Retail FISTP stores a signed dword. LP64 lrint instead has a 64-bit
    // range and narrowing its integer-indefinite result can produce zero.
    if (!(rounded >= -2147483648.0 && rounded <= 2147483647.0)) {
        feraiseexcept(FE_INVALID);
        return (-2147483647 - 1);
    }
    return static_cast<w8_long>(rounded);
}

inline w8_long srFloatToInt(float value)
{
    return srFloatToInt(static_cast<double>(value));
}

template <class T> class srMatrix3T;

template <class T> class srVector2T {
public:
    // Value copies keep packed storage independent of GLM alignment.
    srVector2T(const glm::vec<2, T>& value) : x(value.x), y(value.y) {}
    glm::vec<2, T> glmValue() const
    {
        return {x, y};
    }

    srVector2T<T>() {}
    srVector2T<T>(T source_0, T source_1) : x(source_0), y(source_1) {}

    srVector2T<T>* Set(T source_0, T source_1)
    {
        x = source_0;
        y = source_1;
        return this;
    }

    void SetZero()
    {
        x = (T)0;
        y = (T)0;
    }

    int isValid() const
    {
        return std::isfinite(static_cast<double>(x)) && std::isfinite(static_cast<double>(y));
    }

    srVector2T<T>& operator*=(double scalar)
    {
        x = (T)(x * scalar);
        y = (T)(y * scalar);
        return *this;
    }

    T x;
    T y;
};

// Character/location records embed these vectors at unaligned retail offsets.
#pragma pack(push, 1)
template <class T> class srVector3T {
public:
    // Value copies keep packed storage independent of GLM alignment.
    srVector3T(const glm::vec<3, T>& value) : x(value.x), y(value.y), z(value.z) {}
    glm::vec<3, T> glmValue() const
    {
        return {x, y, z};
    }

    srVector3T<T>();
    srVector3T<T>(T source_0, T source_1, T source_2) : x(source_0), y(source_1), z(source_2) {}

    srVector2T<T> xz() const
    {
        return srVector2T<T>(x, z);
    }

    void SetZero();
    srVector3T<T>* Set(double source_0, double source_1, double source_2);

    template <class U> srVector3T<T>& operator=(const srVector3T<U>& source)
    {
        x = (T)source.x;
        y = (T)source.y;
        z = (T)source.z;
        return *this;
    }
    srVector3T<T>& operator=(T value)
    {
        x = value;
        y = value;
        z = value;
        return *this;
    }
    srVector3T<T>& operator+=(const srVector3T<T>& other);
    srVector3T<T>& operator-=(const srVector3T<T>& other);

    srVector3T<T>& operator*=(const srVector3T<T>& other);
    srVector3T<T>& operator*=(double scalar);
    srVector3T<T>& operator/=(double scalar);
    bool operator==(const srVector3T<T>& other) const;

    int isValid() const
    {
        return std::isfinite(static_cast<double>(x)) && std::isfinite(static_cast<double>(y)) &&
               std::isfinite(static_cast<double>(z));
    }

    T x;
    T y;
    T z;
};

#pragma pack(pop)

template <class T> srVector3T<T>::srVector3T() {}

template <class T> void srVector3T<T>::SetZero()
{
    x = (T)0;
    y = (T)0;
    z = (T)0;
}

template <class T>
srVector3T<T>* srVector3T<T>::Set(double source_0, double source_1, double source_2)
{
    x = (T)source_0;
    y = (T)source_1;
    z = (T)source_2;
    return this;
}

template <class T> srVector3T<T>& srVector3T<T>::operator+=(const srVector3T<T>& other)
{
    x += other.x;
    y += other.y;
    z += other.z;
    return *this;
}

template <class T> srVector3T<T>& srVector3T<T>::operator-=(const srVector3T<T>& other)
{
    x -= other.x;
    y -= other.y;
    z -= other.z;
    return *this;
}

template <class T> srVector3T<T>& srVector3T<T>::operator*=(const srVector3T<T>& other)
{
    x = other.x * x;
    y = other.y * y;
    z = other.z * z;
    return *this;
}

template <class T> srVector3T<T>& srVector3T<T>::operator*=(double scalar)
{
    x = (T)(x * scalar);
    y = (T)(y * scalar);
    z = (T)(z * scalar);
    return *this;
}

template <class T> srVector3T<T>& srVector3T<T>::operator/=(double scalar)
{
    double reciprocal = 1.0 / scalar;
    x = (T)(x * reciprocal);
    y = (T)(y * reciprocal);
    z = (T)(z * reciprocal);
    return *this;
}

template <class T> srVector3T<T> operator+(const srVector3T<T>& first, const srVector3T<T>& second)
{
    srVector3T<T> result;
    result.x = first.x + second.x;
    result.y = first.y + second.y;
    result.z = first.z + second.z;
    return result;
}

template <class T> srVector3T<T> operator-(const srVector3T<T>& first, const srVector3T<T>& second)
{
    srVector3T<T> result;
    result.x = first.x - second.x;
    result.y = first.y - second.y;
    result.z = first.z - second.z;
    return result;
}

template <class T> srVector3T<T> operator-(const srVector3T<T>& vector)
{
    srVector3T<T> result;
    result.x = -vector.x;
    result.y = -vector.y;
    result.z = -vector.z;
    return result;
}

template <class T> bool srVector3T<T>::operator==(const srVector3T<T>& other) const
{
    return x == other.x && y == other.y && z == other.z;
}

template <class T> srVector3T<T> operator*(const srVector3T<T>& vector, double scalar)
{
    srVector3T<T> result;
    result.x = (T)(vector.x * scalar);
    result.y = (T)(vector.y * scalar);
    result.z = (T)(vector.z * scalar);
    return result;
}

template <class T> srVector3T<T> operator*(double scalar, const srVector3T<T>& vector)
{
    srVector3T<T> result;
    result.x = (T)(vector.x * scalar);
    result.y = (T)(vector.y * scalar);
    result.z = (T)(vector.z * scalar);
    return result;
}

template <class T> srVector3T<T> operator*(const srVector3T<T>& first, const srVector3T<T>& second)
{
    srVector3T<T> result;
    result.x = first.x * second.x;
    result.y = first.y * second.y;
    result.z = first.z * second.z;
    return result;
}

template <class T> srVector3T<T> operator/(const srVector3T<T>& vector, double scalar)
{
    double reciprocal = 1.0 / scalar;
    srVector3T<T> result;
    result.x = (T)(vector.x * reciprocal);
    result.y = (T)(vector.y * reciprocal);
    result.z = (T)(vector.z * reciprocal);
    return result;
}

template <class T> class srVector4T {
public:
    // Value copies keep packed storage independent of GLM alignment.
    srVector4T(const glm::vec<4, T>& value) : x(value.x), y(value.y), z(value.z), w(value.w) {}
    glm::vec<4, T> glmValue() const
    {
        return {x, y, z, w};
    }

    srVector4T<T>();

    srVector3T<T> xyz() const
    {
        srVector3T<T> result;
        result.Set(x, y, z);
        return result;
    }

    template <class U> srVector4T<T>& operator=(const srVector4T<U>& source)
    {
        x = static_cast<T>(source.x);
        y = static_cast<T>(source.y);
        z = static_cast<T>(source.z);
        w = static_cast<T>(source.w);
        return *this;
    }

    srVector4T<T>* Set(T source_0, T source_1, T source_2, T source_3);

    int isValid() const
    {
        return std::isfinite(static_cast<double>(x)) && std::isfinite(static_cast<double>(y)) &&
               std::isfinite(static_cast<double>(z)) && std::isfinite(static_cast<double>(w));
    }

    srVector4T<T>& operator*=(double scalar);
    srVector4T<T>& operator=(T value)
    {
        x = value;
        y = value;
        z = value;
        w = value;
        return *this;
    }

    T x;
    T y;
    T z;
    T w;
};

template <class T> srVector4T<T>::srVector4T() {}

template <class T> srVector4T<T>* srVector4T<T>::Set(T source_0, T source_1, T source_2, T source_3)
{
    x = source_0;
    y = source_1;
    z = source_2;
    w = source_3;
    return this;
}

template <class T> srVector4T<T>& srVector4T<T>::operator*=(double scalar)
{
    x = (T)(x * scalar);
    y = (T)(y * scalar);
    z = (T)(z * scalar);
    w = (T)(w * scalar);
    return *this;
}

template <class T> srVector4T<T> operator-(const srVector4T<T>& vector)
{
    srVector4T<T> result;
    result.Set(-vector.x, -vector.y, -vector.z, -vector.w);
    return result;
}

template <class T> srVector4T<T> operator+(const srVector4T<T>& first, const srVector4T<T>& second)
{
    srVector4T<T> result;
    result.x = first.x + second.x;
    result.y = first.y + second.y;
    result.z = first.z + second.z;
    result.w = first.w + second.w;
    return result;
}

template <class T> srVector4T<T> operator-(const srVector4T<T>& first, const srVector4T<T>& second)
{
    srVector4T<T> result;
    result.x = first.x - second.x;
    result.y = first.y - second.y;
    result.z = first.z - second.z;
    result.w = first.w - second.w;
    return result;
}

template <class T> srVector4T<T> operator*(const srVector4T<T>& first, const srVector4T<T>& second)
{
    srVector4T<T> result;
    result.x = first.x * second.x;
    result.y = first.y * second.y;
    result.z = first.z * second.z;
    result.w = first.w * second.w;
    return result;
}

template <class T> srVector4T<T> operator*(const srVector4T<T>& vector, double scalar)
{
    srVector4T<T> result;
    result.x = (T)(vector.x * scalar);
    result.y = (T)(vector.y * scalar);
    result.z = (T)(vector.z * scalar);
    result.w = (T)(vector.w * scalar);
    return result;
}

template <class T> class srMatrix2T {
public:
    srMatrix2T() = default;
    srMatrix2T(const glm::mat<2, 2, T>& value)
    {
        for (int row = 0; row < 2; ++row)
            vectors[row] = glm::row(value, row);
    }
    glm::mat<2, 2, T> glmValue() const
    {
        glm::mat<2, 2, T> value;
        for (int row = 0; row < 2; ++row)
            value[row] = vectors[row].glmValue();
        return glm::transpose(value);
    }

    srVector2T<T> vectors[2];
};

template <class T> class srMatrix3T {
public:
    srMatrix3T() = default;
    srMatrix3T(const glm::mat<3, 3, T>& value)
    {
        for (int row = 0; row < 3; ++row)
            vectors[row] = glm::row(value, row);
    }
    glm::mat<3, 3, T> glmValue() const
    {
        glm::mat<3, 3, T> value;
        for (int row = 0; row < 3; ++row)
            value[row] = vectors[row].glmValue();
        return glm::transpose(value);
    }

    srMatrix3T<T>* SetRows(const srVector3T<T>& first, const srVector3T<T>& second,
                           const srVector3T<T>& third);

    void SetIdentity();

    bool operator==(const srMatrix3T<T>& other) const;

    srVector3T<T> vectors[3];
};

template <class T>
srMatrix3T<T>* srMatrix3T<T>::SetRows(const srVector3T<T>& first, const srVector3T<T>& second,
                                      const srVector3T<T>& third)
{
    vectors[0] = first;
    vectors[1] = second;
    vectors[2] = third;
    return this;
}

template <class T> void srMatrix3T<T>::SetIdentity()
{
    vectors[0] = srVector3T<T>((T)1, (T)0, (T)0);
    vectors[1] = srVector3T<T>((T)0, (T)1, (T)0);
    vectors[2] = srVector3T<T>((T)0, (T)0, (T)1);
}

template <class T> bool srMatrix3T<T>::operator==(const srMatrix3T<T>& other) const
{
    return vectors[0] == other.vectors[0] && vectors[1] == other.vectors[1] &&
           vectors[2] == other.vectors[2];
}

template <class T> class srMatrix4T
{
public:
    srMatrix4T() = default;
    srMatrix4T(const glm::mat<4, 4, T>& value)
    {
        for (int row = 0; row < 4; ++row)
            vectors[row] = glm::row(value, row);
    }
    glm::mat<4, 4, T> glmValue() const
    {
        glm::mat<4, 4, T> value;
        for (int row = 0; row < 4; ++row)
            value[row] = vectors[row].glmValue();
        return glm::transpose(value);
    }

    /* classifyMatrix on the model-view stack writes these from the 3x3 column lengths. */
    enum e_scaleType {
        SCALE_TYPE_UNIT = 0,       /* equal column lengths, all ~1 */
        SCALE_TYPE_UNIFORM = 1,    /* equal column lengths, not 1 */
        SCALE_TYPE_NON_UNIFORM = 2 /* unequal column lengths */
    };
    /* Zero patterns classifyMatrix distinguishes. */
    enum e_type {
        TYPE_GENERAL = 0,
        TYPE_AFFINE = 3,
        TYPE_IDENTITY = 4,
        TYPE_ORTHOGRAPHIC = 5,
        TYPE_PERSPECTIVE = 6
    };

    /* The renderer maintains float matrices and exposes double overloads.
       A member template leaves the same-precision implicit copy intact. */
    template <class U> srMatrix4T<T>& operator=(const srMatrix4T<U>& source)
    {
        for (int row = 0; row != 4; ++row) {
            vectors[row] = source.vectors[row];
        }
        return *this;
    }
    void SetIdentity()
    {
        vectors[0].Set(1, 0, 0, 0);
        vectors[1].Set(0, 1, 0, 0);
        vectors[2].Set(0, 0, 1, 0);
        vectors[3].Set(0, 0, 0, 1);
    }

    /* One Set per row, written out: each row's four results are computed from the
       unmodified row before Set stores them, and rows are updated in order; the matrices
       must not alias. Mixed-precision multiplication keeps the source coefficient width
       until Set narrows to T. sr.dll's out-of-line double instance (0x10055A60) is this
       unrolled form. */

    srMatrix4T<T>* Set(const srMatrix3T<T>& rotation, const srVector3T<T>& translation);

    srVector4T<T> vectors[4];
};

/* Assign *this = inverse(source). */

// srMatrix4T<float>::Set
template <class T>
srMatrix4T<T>* srMatrix4T<T>::Set(const srMatrix3T<T>& rotation, const srVector3T<T>& translation)
{
    srVector4T<T> target;
    const T* component = &translation.x;
    for (int row = 0; row < 3; ++row) {
        target.Set(rotation.vectors[row].x, rotation.vectors[row].y, rotation.vectors[row].z,
                   *component++);
        vectors[row] = target;
    }
    target.Set((T)0, (T)0, (T)0, (T)1);
    vectors[3] = target;
    return this;
}

template <class T> class srMatrix4x3T {
public:
    glm::mat<4, 4, T> glmValue() const
    {
        glm::mat<4, 4, T> value(1);
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 4; ++col)
                value[col][row] = rows[row].glmValue()[col];
        return value;
    }

    void SetIdentity();
    void SetRotation(const srMatrix3T<T>& rotation);
    srMatrix4x3T<T>* SetTranslation(const srVector3T<T>& translation);
    srMatrix4x3T<T>* Scale(const srVector3T<T>& scale);

    srVector4T<T> rows[3];
};

template <class T> void srMatrix4x3T<T>::SetIdentity()
{
    rows[0].x = static_cast<T>(1);
    rows[0].y = static_cast<T>(0);
    rows[0].z = static_cast<T>(0);
    rows[0].w = static_cast<T>(0);
    rows[1].x = static_cast<T>(0);
    rows[1].y = static_cast<T>(1);
    rows[1].z = static_cast<T>(0);
    rows[1].w = static_cast<T>(0);
    rows[2].x = static_cast<T>(0);
    rows[2].y = static_cast<T>(0);
    rows[2].z = static_cast<T>(1);
    rows[2].w = static_cast<T>(0);
}

template <class T> void srMatrix4x3T<T>::SetRotation(const srMatrix3T<T>& rotation)
{
    rows[0].x = rotation.vectors[0].x;
    rows[0].y = rotation.vectors[0].y;
    rows[0].z = rotation.vectors[0].z;
    rows[1].x = rotation.vectors[1].x;
    rows[1].y = rotation.vectors[1].y;
    rows[1].z = rotation.vectors[1].z;
    rows[2].x = rotation.vectors[2].x;
    rows[2].y = rotation.vectors[2].y;
    rows[2].z = rotation.vectors[2].z;
}

// srMatrix4x3T<float>::SetTranslation
template <class T>
srMatrix4x3T<T>* srMatrix4x3T<T>::SetTranslation(const srVector3T<T>& translation)
{
    rows[0].w = translation.x;
    rows[1].w = translation.y;
    rows[2].w = translation.z;
    return this;
}

// srMatrix4x3T<float>::Scale
template <class T> srMatrix4x3T<T>* srMatrix4x3T<T>::Scale(const srVector3T<T>& scale)
{
    rows[0].x *= scale.x;
    rows[1].x *= scale.x;
    rows[2].x *= scale.x;
    rows[0].y *= scale.y;
    rows[1].y *= scale.y;
    rows[2].y *= scale.y;
    rows[0].z *= scale.z;
    rows[1].z *= scale.z;
    rows[2].z *= scale.z;
    return this;
}

W8_ABI_ASSERT(sizeof(srMatrix4x3T<float>) == 0x30, "srMatrix4x3T_float_must_be_0x30");
W8_ABI_ASSERT(sizeof(srMatrix4x3T<double>) == 0x60, "srMatrix4x3T_double_must_be_0x60");
W8_ABI_ASSERT(sizeof(srMatrix2T<float>) == 0x10, "srMatrix2T_float_must_be_0x10");
W8_ABI_ASSERT(sizeof(srMatrix2T<double>) == 0x20, "srMatrix2T_double_must_be_0x20");

class srVector2i {
public:
    int x;
    int y;
};

class srVector3i {
public:
    srVector3i() {}

    int x;
    int y;
    int z;
};

class srVector4i {
public:
    int x;
    int y;
    int z;
    int w;
};

W8_ABI_ASSERT(sizeof(srVector4i) == 0x10, "srVector4i_must_be_0x10");

class srQuaternion {
public:
    float w;
    srVector3T<float> v;
};

W8_ABI_ASSERT(sizeof(srQuaternion) == 0x10, "srQuaternion_must_be_0x10");

// Short spellings for packed renderer storage, not computational math types.
using SRBYTE = unsigned char;
using SRDWORD = w8_ulong;
using srVector2 = srVector2T<float>;
using srVector3 = srVector3T<float>;
using srVector4 = srVector4T<float>;
using srMatrix4 = srMatrix4T<float>;

static_assert(sizeof(srVector3) == 12 && alignof(srVector3) == 1);
static_assert(sizeof(srVector4) == 16 && alignof(srVector4) == alignof(float));
static_assert(sizeof(srMatrix4) == 64 && alignof(srMatrix4) == alignof(float));

static_assert(std::is_trivially_copyable_v<srVector3>);
static_assert(std::is_trivially_copyable_v<srMatrix4>);

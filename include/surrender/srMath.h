#pragma once

#include <cmath>
#include <span>
#include <type_traits>

#include <fenv.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

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

// Character/location records embed these vectors at unaligned retail offsets.
#pragma pack(push, 1)
template<class T> class srVector3T {
public:
    // Value copies keep packed storage independent of GLM alignment.
    srVector3T(const glm::vec<3, T>& value) : x(value.x), y(value.y), z(value.z) {}
    glm::vec<3, T> glmValue() const
    {
        return {x, y, z};
    }

    T Length() const
    {
        return glm::length(glmValue());
    }
    T LengthSquared() const
    {
        const auto v = glmValue();
        return glm::dot(v, v);
    }
    // Zero vectors retain their components, including signed zeros.
    srVector3T* Normalize()
    {
        return SetLength(1);
    }
    srVector3T* SetLength(double length)
    {
        const auto v = glmValue();
        if (glm::dot(v, v) != 0) {
            *this = glm::normalize(v) * static_cast<T>(length);
        }
        return this;
    }
    srVector3T* Unitize()
    {
        return Normalize();
    }
    srVector3T& Transform(const glm::mat<3, 3, T>& matrix);
    srVector3T* RotateAboutX(double sine, double cosine);
    srVector3T* RotateAboutY(double sine, double cosine);
    srVector3T* RotateAboutZ(double sine, double cosine);

    srVector3T<T>();
    srVector3T<T>(T source_0, T source_1, T source_2) : x(source_0), y(source_1), z(source_2) {}

    glm::vec<2, T> xz() const
    {
        return glm::vec<2, T>(x, z);
    }

    void SetZero();
    srVector3T<T>* Set(double source_0, double source_1, double source_2);

    template<class U> srVector3T<T>& operator=(const srVector3T<U>& source)
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

template<class T> srVector3T<T>::srVector3T() {}

template<class T> void srVector3T<T>::SetZero()
{
    x = (T)0;
    y = (T)0;
    z = (T)0;
}

template<class T>
srVector3T<T>* srVector3T<T>::Set(double source_0, double source_1, double source_2)
{
    x = (T)source_0;
    y = (T)source_1;
    z = (T)source_2;
    return this;
}

template<class T> srVector3T<T>& srVector3T<T>::operator+=(const srVector3T<T>& other)
{
    x += other.x;
    y += other.y;
    z += other.z;
    return *this;
}

template<class T> srVector3T<T>& srVector3T<T>::operator-=(const srVector3T<T>& other)
{
    x -= other.x;
    y -= other.y;
    z -= other.z;
    return *this;
}

template<class T> srVector3T<T>& srVector3T<T>::operator*=(const srVector3T<T>& other)
{
    x = other.x * x;
    y = other.y * y;
    z = other.z * z;
    return *this;
}

template<class T> srVector3T<T>& srVector3T<T>::operator*=(double scalar)
{
    x = (T)(x * scalar);
    y = (T)(y * scalar);
    z = (T)(z * scalar);
    return *this;
}

template<class T> srVector3T<T>& srVector3T<T>::operator/=(double scalar)
{
    double reciprocal = 1.0 / scalar;
    x = (T)(x * reciprocal);
    y = (T)(y * reciprocal);
    z = (T)(z * reciprocal);
    return *this;
}

template<class T> srVector3T<T> operator+(const srVector3T<T>& first, const srVector3T<T>& second)
{
    srVector3T<T> result;
    result.x = first.x + second.x;
    result.y = first.y + second.y;
    result.z = first.z + second.z;
    return result;
}

template<class T> srVector3T<T> operator-(const srVector3T<T>& first, const srVector3T<T>& second)
{
    srVector3T<T> result;
    result.x = first.x - second.x;
    result.y = first.y - second.y;
    result.z = first.z - second.z;
    return result;
}

template<class T> srVector3T<T> operator-(const srVector3T<T>& vector)
{
    srVector3T<T> result;
    result.x = -vector.x;
    result.y = -vector.y;
    result.z = -vector.z;
    return result;
}

template<class T> bool srVector3T<T>::operator==(const srVector3T<T>& other) const
{
    return x == other.x && y == other.y && z == other.z;
}

template<class T> srVector3T<T> operator*(const srVector3T<T>& vector, double scalar)
{
    srVector3T<T> result;
    result.x = (T)(vector.x * scalar);
    result.y = (T)(vector.y * scalar);
    result.z = (T)(vector.z * scalar);
    return result;
}

template<class T> srVector3T<T> operator*(double scalar, const srVector3T<T>& vector)
{
    srVector3T<T> result;
    result.x = (T)(vector.x * scalar);
    result.y = (T)(vector.y * scalar);
    result.z = (T)(vector.z * scalar);
    return result;
}

template<class T> srVector3T<T> operator*(const srVector3T<T>& first, const srVector3T<T>& second)
{
    srVector3T<T> result;
    result.x = first.x * second.x;
    result.y = first.y * second.y;
    result.z = first.z * second.z;
    return result;
}

template<class T> srVector3T<T> operator/(const srVector3T<T>& vector, double scalar)
{
    double reciprocal = 1.0 / scalar;
    srVector3T<T> result;
    result.x = (T)(vector.x * reciprocal);
    result.y = (T)(vector.y * reciprocal);
    result.z = (T)(vector.z * reciprocal);
    return result;
}

class srVector2i {
public:
    int x;
    int y;
};

using SRBYTE = unsigned char;
using SRDWORD = w8_ulong;
using srVector3 = srVector3T<float>;
static_assert(sizeof(srVector3) == 12 && alignof(srVector3) == 1);
static_assert(std::is_trivially_copyable_v<srVector3>);

template<class T> srVector3T<T>& srVector3T<T>::Transform(const glm::mat<3, 3, T>& matrix)
{
    *this = matrix * glmValue();
    return *this;
}
template<class T> srVector3T<T>* srVector3T<T>::RotateAboutX(double sine, double cosine)
{
    *this = glm::vec<3, T>(glm::angleAxis(std::atan2(sine, cosine), glm::dvec3(1, 0, 0)) *
                           glm::dvec3(glmValue()));
    return this;
}
template<class T> srVector3T<T>* srVector3T<T>::RotateAboutY(double sine, double cosine)
{
    *this = glm::vec<3, T>(glm::angleAxis(std::atan2(sine, cosine), glm::dvec3(0, 1, 0)) *
                           glm::dvec3(glmValue()));
    return this;
}
template<class T> srVector3T<T>* srVector3T<T>::RotateAboutZ(double sine, double cosine)
{
    *this = glm::vec<3, T>(glm::angleAxis(std::atan2(sine, cosine), glm::dvec3(0, 0, 1)) *
                           glm::dvec3(glmValue()));
    return this;
}

template<glm::length_t N, class T, glm::qualifier Q>
glm::vec<N, T, Q> normalizedOrZero(const glm::vec<N, T, Q>& value)
{
    return glm::dot(value, value) == T(0) ? value : glm::normalize(value);
}
template<glm::length_t N, class T, glm::qualifier Q> bool isFinite(const glm::vec<N, T, Q>& value)
{
    return !glm::any(glm::isnan(value)) && !glm::any(glm::isinf(value));
}

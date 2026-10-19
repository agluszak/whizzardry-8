#pragma once

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

// A world-space pose. Attachments compute their poses explicitly; there are no
// parent links, transform notifications, or mutable matrix caches.
struct Transform3D {
    glm::dvec3 position{0};
    glm::dmat3 rotation{1};
    glm::dvec3 scale{1};

    glm::dvec3 getLocation() const { return position; }
    double getLocationX() const { return position.x; }
    double getLocationY() const { return position.y; }
    double getLocationZ() const { return position.z; }
    void setLocation(const glm::dvec3& value) { position = value; }
    void setLocation(double x, double y, double z) { position = {x, y, z}; }
    template<class Matrix = glm::dmat3> Matrix getRotation() const { return Matrix(rotation); }
    void setRotation(const glm::dmat3& value) { rotation = value; }
    void setRotation(const glm::mat3& value) { rotation = value; }
    void setRotation(double x, double y, double z)
    {
        rotation = glm::mat3_cast(glm::angleAxis(x, glm::dvec3(1, 0, 0)) *
                                  glm::angleAxis(y, glm::dvec3(0, 1, 0)) *
                                  glm::angleAxis(z, glm::dvec3(0, 0, 1)));
    }
    void setRotation(double amount, const glm::dvec3& direction)
    {
        const auto length = glm::length(direction);
        rotation =
            length > 0 ? glm::mat3_cast(glm::angleAxis(amount, direction / length)) : glm::dmat3(1);
    }
    void setRotation(const glm::dvec3& direction, double amount)
    {
        const auto yaw = std::atan2(direction.x, direction.z);
        const auto pitch = -std::atan2(direction.y, std::hypot(direction.x, direction.z));
        rotation = glm::mat3_cast(glm::angleAxis(yaw, glm::dvec3(0, 1, 0)) *
                                  glm::angleAxis(pitch, glm::dvec3(1, 0, 0)) *
                                  glm::angleAxis(amount, glm::dvec3(0, 0, 1)));
    }
    void setRotation(const glm::dvec3& first, const glm::dvec3& second, double amount)
    {
        setRotation(first - second, amount);
    }
    glm::dvec3 getScale() const { return scale; }
    glm::dvec3 magnitudes() const { return glm::abs(scale); }
    void setScale(const glm::dvec3& value) { scale = value; }
    void setScale(double value) { scale = glm::dvec3(value); }
    template<class Matrix = glm::dmat4> Matrix matrix() const
    {
        auto result = glm::scale(glm::dmat4(rotation), scale);
        result[3] = glm::dvec4(position, 1);
        return Matrix(result);
    }
    template<class Matrix = glm::dmat3> Matrix orientation() const
    {
        Matrix result(matrix<glm::dmat4>());
        for (int column = 0; column < 3; ++column) {
            const auto length = glm::length(result[column]);
            if (length > 0) result[column] /= length;
        }
        return result;
    }
    glm::dvec3 forward() const { return orientation()[2]; }
    void move(const glm::dvec3& offset) { position += rotation * offset; }
    void offsetLocation(const glm::dvec3& offset) { position += offset; }
    void offsetLocation(double x, double y, double z) { position += glm::dvec3(x, y, z); }
    void rotate(const glm::dmat3& value) { rotation *= value; }
    void rotate(double angle, const glm::dvec3& axis)
    {
        const auto length = glm::length(axis);
        if (length > 0) rotation *= glm::mat3_cast(glm::angleAxis(angle, axis / length));
    }
    void rotateX(double angle) { rotate(angle, {1, 0, 0}); }
    void setMatrix(const glm::dmat4& value)
    {
        position = value[3];
        rotation = value;
        scale = {glm::length(rotation[0]), glm::length(rotation[1]), glm::length(rotation[2])};
        for (int column = 0; column < 3; ++column)
            if (scale[column] > 0) rotation[column] /= scale[column];
    }
};

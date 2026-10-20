#include "wiz8/render/camera.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <utility>

#include <glm/common.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/matrix.hpp>
#include <glm/vector_relational.hpp>

#include "glm/ext/matrix_double3x3.hpp"
#include "glm/ext/matrix_double4x4.hpp"
#include "glm/ext/vector_double2.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/render/gpu_renderer.h"

void Camera3D::setProjectionType(e_project projection)
{
    if (projection == PROJECT_PERSPECTIVE) {
        flags &= ~(1UL << FLAG_PROJECTION_TYPE);
        return;
    }
    flags |= 1UL << FLAG_PROJECTION_TYPE;
}

DrawTransform Camera3D::drawTransform() const
{
    /* The view transform reverses the inverse world matrix's Z row. */
    auto view = glm::inverse(this->transform.matrix<glm::dmat4>());
    view = glm::row(view, 2, -glm::row(view, 2));

    const auto [near_plane, far_plane] = getClipRange();
    auto [rectangle, distance] = getViewPlane();
    const double scale = near_plane / distance;
    rectangle.left *= scale;
    rectangle.right *= scale;
    rectangle.top *= scale;
    rectangle.bottom *= scale;

    const float left = rectangle.left, right = rectangle.right;
    const float bottom = rectangle.bottom, top = rectangle.top;
    const float near_value = near_plane, far_value = far_plane;
    const auto projection = (flags & (1UL << FLAG_PROJECTION_TYPE)) == 0
                                ? glm::frustum(left, right, bottom, top, near_value, far_value)
                                : glm::ortho(left, right, bottom, top, near_value, far_value);
    return {.model_view = glm::mat4(view), .projection = projection};
}

void Camera3D::applyEnvironment(GpuRenderer& renderer) const
{
    renderer.setEnvironmentRange(environment_near, environment_far);
    renderer.setEnvironmentScaleFactor(environment_near_scale, environment_far_scale);
}

Camera3D::Camera3D()
{
    flags = 0;
    near_clip = 0.1;
    far_clip = 1000.0;
    environment_near = 0.0f;
    environment_far = 1000.0f;
    environment_near_scale = 0.0f;
    environment_far_scale = 1.0f;
    setFOV(3.141592653589793 * (1.0f / 180.0f) * 90.0f, 1.3333333333333333);
}

Camera3D::Rect Camera3D::getNormalizedViewPlane() const
{
    return {view_plane.left / view_plane_distance, view_plane.bottom / view_plane_distance,
            view_plane.right / view_plane_distance, view_plane.top / view_plane_distance};
}

Camera3D::ViewPlane Camera3D::getViewPlane() const
{
    return {view_plane, view_plane_distance};
}

void Camera3D::setEnvironmentRange(float near_range, float far_range)
{
    environment_near = near_range;
    environment_far = far_range;
}

void Camera3D::setEnvironmentScale(float near_scale, float far_scale)
{
    environment_near_scale = near_scale;
    environment_far_scale = far_scale;
}

std::pair<float, float> Camera3D::getEnvironmentRange() const
{
    return {environment_near, environment_far};
}

std::pair<float, float> Camera3D::getEnvironmentScale() const
{
    return {environment_near_scale, environment_far_scale};
}

void Camera3D::setClipRange(double near_plane, double far_plane)
{
    near_clip = near_plane;
    far_clip = far_plane;
    if (near_clip > far_clip) {
        double swap = near_clip;
        near_clip = far_clip;
        far_clip = swap;
    }
    if (near_clip < 5.9604644775390625e-08) {
        near_clip = 5.9604644775390625e-08;
    }
    if (far_clip < 5.9604644775390625e-08) {
        far_clip = 5.9604644775390625e-08;
    }
}

std::pair<double, double> Camera3D::getClipRange() const
{
    return {near_clip, far_clip};
}

void Camera3D::setViewPlane(const Rect& rectangle, double distance)
{
    if (distance > 5.9604644775390625e-08) {
        view_plane.left = rectangle.left;
        view_plane.bottom = rectangle.bottom;
        view_plane.right = rectangle.right;
        view_plane.top = rectangle.top;
        view_plane_distance = distance;
    }
}

void Camera3D::setViewPlane(double width, double height)
{
    constexpr double half_turn = 3.141592653589793;
    width = std::clamp(width, -half_turn, half_turn);
    height = std::clamp(height, -half_turn, half_turn);
    double half_width = tan(width * 0.5);
    view_plane_distance = 1.0;
    double half_height = tan(height * 0.5);
    Rect rectangle;
    rectangle.left = -half_width;
    rectangle.bottom = -half_height;
    rectangle.right = half_width;
    rectangle.top = half_height;
    view_plane = rectangle;
}

void Camera3D::setFOV(double field_of_view, double aspect_ratio)
{
    constexpr double minimum = 5.9604644775390625e-08;
    constexpr double maximum = 3.141592653589793 - minimum;
    const double angle = std::clamp(fabs(field_of_view), minimum, maximum);
    double half_angle = tan(angle * 0.5);
    view_plane_distance = 1.0;
    Rect rectangle;
    rectangle.left = -(half_angle * aspect_ratio);
    rectangle.bottom = -half_angle;
    rectangle.right = half_angle * aspect_ratio;
    rectangle.top = half_angle;
    view_plane = rectangle;
}

double Camera3D::getHorizontalFOV() const
{
    return atan(view_plane.right / view_plane_distance) -
           atan(view_plane.left / view_plane_distance);
}

double Camera3D::getVerticalFOV() const
{
    return atan(view_plane.top / view_plane_distance) -
           atan(view_plane.bottom / view_plane_distance);
}

void Camera3D::flipHorizontal()
{
    double swap = view_plane.left;
    view_plane.left = view_plane.right;
    view_plane.right = swap;
}

Camera3D::Projection Camera3D::project(const glm::dvec3& input)
{
    const auto rotation = this->transform.orientation<glm::dmat3>();
    const auto point = glm::transpose(rotation) * (input - this->transform.getLocation());
    if (point.z < std::min(near_clip, far_clip) || point.z > std::max(near_clip, far_clip) ||
        point.z == 0)
        return {glm::vec3(0), PROJECTION_RESULT_OUTSIDE_CLIP_RANGE};
    const glm::dvec2 start(view_plane.left, view_plane.top);
    const glm::dvec2 end(view_plane.right, view_plane.bottom);
    const auto uv = (glm::dvec2(point) * (view_plane_distance / point.z) - start) / (end - start);
    const bool inside = glm::all(glm::greaterThanEqual(uv, glm::dvec2(0))) &&
                        glm::all(glm::lessThanEqual(uv, glm::dvec2(1)));
    return {glm::vec3(uv, point.z),
            inside ? PROJECTION_RESULT_ACCEPTED : PROJECTION_RESULT_OUTSIDE_VIEW};
}

bool Camera3D::projectsAny(std::span<const glm::vec3> points)
{
    return std::any_of(points.begin(), points.end(), [&](const glm::vec3& point) {
        return project(glm::dvec3(point)).result == PROJECTION_RESULT_ACCEPTED;
    });
}

std::optional<glm::vec3> Camera3D::unproject(const glm::dvec3& input)
{
    const glm::dvec2 uv(input.x, input.y);
    if (input.z < std::min(near_clip, far_clip) || input.z > std::max(near_clip, far_clip) ||
        !glm::all(glm::greaterThanEqual(uv, glm::dvec2(0))) ||
        !glm::all(glm::lessThanEqual(uv, glm::dvec2(1))))
        return std::nullopt;

    const glm::dvec2 start(view_plane.left, view_plane.top);
    const glm::dvec2 end(view_plane.right, view_plane.bottom);
    const glm::dvec3 point(glm::mix(start, end, uv) * (input.z / view_plane_distance), input.z);
    glm::dmat3 rotation;
    rotation = this->transform.orientation<glm::dmat3>();
    return glm::vec3(rotation * point + this->transform.getLocation());
}

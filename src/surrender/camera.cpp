#include <glm/gtc/matrix_access.hpp>
#include <ostream>
#include <algorithm>
#include <cmath>
#include <utility>

#include "glm/common.hpp"
#include "glm/matrix.hpp"
#include "glm/vector_relational.hpp"
#include "surrender/srCamera.h"
#include "surrender/srStreamFlags.h"
#include "surrender/srGERD.h"
#include "compat/compiler.h"
#include "glm/ext/matrix_double3x3.hpp"
#include "glm/ext/matrix_double4x4.hpp"
#include "glm/ext/vector_double2.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "surrender/srFlags.h"
#include "surrender/srMath.h"
#include "surrender/srNode.h"
#include "surrender/srTypeRegistry.h"

/* Flag-name table; never assigned, so dump prints numeric bit indices. */
static const char* flag_names;

// FUNCTION: SURRENDER 0x10047D60
std::ostream& operator<<(std::ostream& stream, const srCamera::Rect& rectangle)
{
    stream << "{{" << rectangle.left << ',' << rectangle.bottom << "},{" << rectangle.right << ','
           << rectangle.top << "}}";
    return stream;
}

// FUNCTION: SURRENDER 0x10047DF0
void srCamera::process(const ProcessInfo& info, e_processType type)
{
    if (type == PROCESS_PUSH) {
        processPush(info.renderer);
        return;
    }
    processPop(info.renderer);
}

// FUNCTION: SURRENDER 0x10047E10
srCamera::e_project srCamera::getProjectionType() const
{
    return static_cast<e_project>(flags.value & (1UL << FLAG_PROJECTION_TYPE));
}

// FUNCTION: SURRENDER 0x10047E20
void srCamera::setProjectionType(e_project projection)
{
    if (projection == PROJECT_PERSPECTIVE) {
        flags.value &= ~(1UL << FLAG_PROJECTION_TYPE);
        return;
    }
    flags.value |= 1UL << FLAG_PROJECTION_TYPE;
}

// FUNCTION: SURRENDER 0x10047E50
void srCamera::processPush(srGERD* renderer)
{
    glm::dmat4 world;
    world = getWorldSpaceMatrix<glm::dmat4>();

    /* The view transform reverses the inverse world matrix's Z row. */
    glm::dmat4 view;
    view = glm::inverse(world);
    view = glm::row(view, 2, -glm::row(view, 2));

    renderer->matrixMode(srGERD::MATRIX_MODELVIEW);
    renderer->pushMatrix();
    renderer->loadMatrix(view);
    renderer->matrixMode(srGERD::MATRIX_PROJECTION);
    renderer->pushMatrix();
    renderer->loadIdentity();

    const auto [near_plane, far_plane] = getClipRange();

    auto [rectangle, distance] = getViewPlane();
    double scale = near_plane / distance;
    rectangle.left *= scale;
    rectangle.right *= scale;
    rectangle.top *= scale;
    rectangle.bottom *= scale;

    if ((flags.value & (1UL << FLAG_PROJECTION_TYPE)) == 0) {
        renderer->frustum(rectangle.left, rectangle.right, rectangle.bottom, rectangle.top,
                          near_plane, far_plane);
    } else {
        renderer->ortho(rectangle.left, rectangle.right, rectangle.bottom, rectangle.top,
                        near_plane, far_plane);
    }

    renderer->pushEnvironment();
    renderer->setEnvironmentRange(environment_near, environment_far);
    renderer->setEnvironmentScaleFactor(environment_near_scale, environment_far_scale);
}

// FUNCTION: SURRENDER 0x100482E0
void srCamera::processPop(srGERD* renderer)
{
    renderer->matrixMode(srGERD::MATRIX_MODELVIEW);
    renderer->popMatrix();
    renderer->matrixMode(srGERD::MATRIX_PROJECTION);
    renderer->popMatrix();
    renderer->popEnvironment();
}

// FUNCTION: SURRENDER 0x10048430
double srCamera::getAspectRatio() const
{
    return (view_plane.right - view_plane.left) / (view_plane.top - view_plane.bottom);
}

// FUNCTION: SURRENDER 0x10048450
srCamera::srCamera(srNode* parent)
    : srClassSupport<srCamera, srNode, 0x1400>(static_cast<srNode*>(0))
{
    flags.value = 0;
    near_clip = 0.1;
    far_clip = 1000.0;
    environment_near = 0.0f;
    environment_far = 1000.0f;
    environment_near_scale = 0.0f;
    environment_far_scale = 1.0f;
    if (parent != 0) {
        setParent(parent, 0);
    }
    setFOV(3.141592653589793 * (1.0f / 180.0f) * 90.0f, 1.3333333333333333);
}

// FUNCTION: SURRENDER 0x100485A0
srCamera::Rect srCamera::getNormalizedViewPlane() const
{
    return {view_plane.left / view_plane_distance, view_plane.bottom / view_plane_distance,
            view_plane.right / view_plane_distance, view_plane.top / view_plane_distance};
}

// FUNCTION: SURRENDER 0x100485F0
void srCamera::normalizeViewPlane()
{
    view_plane.left /= view_plane_distance;
    view_plane.bottom /= view_plane_distance;
    view_plane.right /= view_plane_distance;
    view_plane.top /= view_plane_distance;
    view_plane_distance = 1.0;
}

// FUNCTION: SURRENDER 0x10048650
srCamera::ViewPlane srCamera::getViewPlane() const
{
    return {view_plane, view_plane_distance};
}

// FUNCTION: SURRENDER 0x100486C0
void srCamera::setEnvironmentRange(float near_range, float far_range)
{
    environment_near = near_range;
    environment_far = far_range;
}

// FUNCTION: SURRENDER 0x100486E0
void srCamera::setEnvironmentScale(float near_scale, float far_scale)
{
    environment_near_scale = near_scale;
    environment_far_scale = far_scale;
}

// FUNCTION: SURRENDER 0x10048700
std::pair<float, float> srCamera::getEnvironmentRange() const
{
    return {environment_near, environment_far};
}

// FUNCTION: SURRENDER 0x10048720
std::pair<float, float> srCamera::getEnvironmentScale() const
{
    return {environment_near_scale, environment_far_scale};
}

// FUNCTION: SURRENDER 0x10048740
void srCamera::setClipRange(double near_plane, double far_plane)
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

// FUNCTION: SURRENDER 0x100487F0
std::pair<double, double> srCamera::getClipRange() const
{
    return {near_clip, far_clip};
}

// FUNCTION: SURRENDER 0x10048820
void srCamera::setViewPlane(const Rect& rectangle, double distance)
{
    if (distance > 5.9604644775390625e-08) {
        view_plane.left = rectangle.left;
        view_plane.bottom = rectangle.bottom;
        view_plane.right = rectangle.right;
        view_plane.top = rectangle.top;
        view_plane_distance = distance;
    }
}

// FUNCTION: SURRENDER 0x100488A0
void srCamera::setFocalLength(double focal_length, double aspect_ratio)
{
    if (focal_length < 5.9604644775390625e-08) {
        focal_length = 5.9604644775390625e-08;
    }
    if (aspect_ratio < 5.9604644775390625e-08) {
        aspect_ratio = 5.9604644775390625e-08;
    }
    double angle = atan(0.018 / focal_length);
    setViewPlane((angle + angle) * aspect_ratio, angle + angle);
}

// FUNCTION: SURRENDER 0x10048920
void srCamera::setViewPlane(double width, double height)
{
    if (width <= -3.141592653589793) {
        width = -3.141592653589793;
    } else if (width >= 3.141592653589793) {
        width = 3.141592653589793;
    }
    if (height <= -3.141592653589793) {
        height = -3.141592653589793;
    } else if (height >= 3.141592653589793) {
        height = 3.141592653589793;
    }
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

// FUNCTION: SURRENDER 0x10048A10
void srCamera::setFOV(double field_of_view, double aspect_ratio)
{
    double angle = fabs(field_of_view);
    double maximum = 3.141592653589793 - 5.9604644775390625e-08;
    if (angle <= 5.9604644775390625e-08) {
        angle = 5.9604644775390625e-08;
    } else if (angle >= maximum) {
        angle = maximum;
    }
    double half_angle = tan(angle * 0.5);
    view_plane_distance = 1.0;
    Rect rectangle;
    rectangle.left = -(half_angle * aspect_ratio);
    rectangle.bottom = -half_angle;
    rectangle.right = half_angle * aspect_ratio;
    rectangle.top = half_angle;
    view_plane = rectangle;
}

// FUNCTION: SURRENDER 0x10048AB0
double srCamera::getHorizontalFOV() const
{
    return atan(view_plane.right / view_plane_distance) -
           atan(view_plane.left / view_plane_distance);
}

// FUNCTION: SURRENDER 0x10048AE0
double srCamera::getVerticalFOV() const
{
    return atan(view_plane.top / view_plane_distance) -
           atan(view_plane.bottom / view_plane_distance);
}

// FUNCTION: SURRENDER 0x10048B10
void srCamera::flipVertical()
{
    double swap = view_plane.bottom;
    view_plane.bottom = view_plane.top;
    view_plane.top = swap;
}

// FUNCTION: SURRENDER 0x10048B40
void srCamera::flipHorizontal()
{
    double swap = view_plane.left;
    view_plane.left = view_plane.right;
    view_plane.right = swap;
}

// FUNCTION: SURRENDER 0x10048B70
void srCamera::dump(std::ostream& stream)
{
    srNode::dump(stream);
    w8_long flags = srGetStreamFlags(stream);
    srSetStreamFlags(stream, (flags & 0xfffffe7fL) | 0x40);
    stream.width(0x20);
    stream << "Control flags: ";
    if (this->flags.value == 0) {
        stream << "[NONE]";
    } else {
        stream << '[';
        int first = 1;
        const char* names = flag_names;
        for (w8_ulong bit = 0; bit < 0x20; ++bit) {
            if ((this->flags.value & (1UL << bit)) != 0) {
                if (first == 0) {
                    stream << ',';
                } else {
                    first = 0;
                }
                if (names == 0 || *names == '\0') {
                    stream << bit;
                } else {
                    while (*names != '\0' && *names != ',') {
                        stream << *names;
                        ++names;
                    }
                    if (*names == ',') {
                        ++names;
                    }
                }
            } else if (names != 0) {
                while (*names != '\0' && *names != ',') {
                    ++names;
                }
                if (*names == ',') {
                    ++names;
                }
            }
        }
        stream << ']';
    }
    stream << '\n';
    stream.width(0x20);
    stream << "  Viewplane: " << view_plane << '\n';
    stream.width(0x20);
    stream << "  Viewplane distance: " << view_plane_distance << '\n';
    stream.width(0x20);
    const auto [near_plane, far_plane] = getClipRange();
    stream << "  Object space Clip Range (near,far): " << near_plane << ',' << far_plane << '\n';
    stream.width(0x20);
    const auto [near_range, far_range] = getEnvironmentRange();
    stream << "  Environment Range (near,far): " << near_range << ',' << far_range << '\n';
    const auto [near_scale, far_scale] = getEnvironmentScale();
    stream << "  Environment Scale (near,far): " << near_scale << ',' << far_scale << '\n';
    srSetStreamFlags(stream, flags & 0x7fff);
}

// FUNCTION: SURRENDER 0x10048E30
srCamera::e_projectionResult srCamera::project(srVector3T<float>& output,
                                               const srVector3T<double>& input)
{
    glm::dmat3 rotation;
    rotation = getWorldSpaceRotation<glm::dmat3>();
    const auto point = glm::transpose(rotation) * (input.glmValue() - getWorldSpaceLocation());
    if (point.z < std::min(near_clip, far_clip) || point.z > std::max(near_clip, far_clip) ||
        point.z == 0) {
        output.SetZero();
        return PROJECTION_RESULT_OUTSIDE_CLIP_RANGE;
    }
    const glm::dvec2 start(view_plane.left, view_plane.top);
    const glm::dvec2 end(view_plane.right, view_plane.bottom);
    const auto uv = (glm::dvec2(point) * (view_plane_distance / point.z) - start) / (end - start);
    output = glm::vec3(uv, point.z);
    return glm::all(glm::greaterThanEqual(uv, glm::dvec2(0))) &&
                   glm::all(glm::lessThanEqual(uv, glm::dvec2(1)))
               ? PROJECTION_RESULT_ACCEPTED
               : PROJECTION_RESULT_OUTSIDE_VIEW;
}

// FUNCTION: SURRENDER 0x100490B0
int srCamera::unproject(srVector3T<float>& output, const srVector3T<double>& input)
{
    output.SetZero();
    const glm::dvec2 uv(input.x, input.y);
    if (input.z < std::min(near_clip, far_clip) || input.z > std::max(near_clip, far_clip) ||
        !glm::all(glm::greaterThanEqual(uv, glm::dvec2(0))) ||
        !glm::all(glm::lessThanEqual(uv, glm::dvec2(1))))
        return 0;

    const glm::dvec2 start(view_plane.left, view_plane.top);
    const glm::dvec2 end(view_plane.right, view_plane.bottom);
    const glm::dvec3 point(glm::mix(start, end, uv) * (input.z / view_plane_distance), input.z);
    glm::dmat3 rotation;
    rotation = getWorldSpaceRotation<glm::dmat3>();
    output = glm::vec3(rotation * point + getWorldSpaceLocation());
    return 1;
}

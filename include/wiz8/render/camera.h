#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/render/draw_inputs.h"
#include "wiz8/render/transform.h"

class WorldScene;

class Camera3D {
public:
    enum e_project { PROJECT_PERSPECTIVE = 0, PROJECT_ORTHOGRAPHIC = 1 };

    /* project(): a projected point inside the view is 0, outside its XY bounds is 1, outside the
       near/far interval is 2. */
    enum e_projectionResult {
        PROJECTION_RESULT_ACCEPTED = 0,
        PROJECTION_RESULT_OUTSIDE_VIEW = 1,
        PROJECTION_RESULT_OUTSIDE_CLIP_RANGE = 2
    };

    struct Projection {
        glm::vec3 position;
        e_projectionResult result;
    };

    struct Rect {
        double left;
        double bottom;
        double right;
        double top;
    };

    struct ViewPlane {
        Rect rectangle;
        double distance;
    };

    Camera3D();

    void flipHorizontal();
    void setClipRange(double near_plane, double far_plane);
    std::pair<double, double> getClipRange() const;
    std::pair<float, float> getEnvironmentRange() const;
    std::pair<float, float> getEnvironmentScale() const;
    double getHorizontalFOV() const;
    double getVerticalFOV() const;
    Projection project(const glm::dvec3& input);
    bool projectsAny(std::span<const glm::vec3> points);
    void setProjectionType(e_project projection);

    void setEnvironmentScale(float near_scale, float far_scale);
    void setFOV(double field_of_view, double aspect_ratio);
    void setViewPlane(const Rect& rectangle, double distance);
    void setViewPlane(double width, double height);
    ViewPlane getViewPlane() const;
    void setEnvironmentRange(float near_range, float far_range);

public:
    // The eye and clip transforms of this camera; no user clip planes.
    DrawTransform drawTransform() const;
    // The scene pass's depth cue: this camera's range with each scale clamped to [0, 1].
    DepthCue depthCue() const;

public:
    enum e_flag { FLAG_PROJECTION_TYPE = 0 };

    std::uint32_t flags = 0; /* 0x138 */
private:
    Rect view_plane; /* 0x140 */
    double view_plane_distance; /* 0x160 */
    double near_clip; /* 0x168 */
    double far_clip; /* 0x170 */
    float environment_near; /* 0x178 */
    float environment_far; /* 0x17c */
    float environment_near_scale; /* 0x180 */
    float environment_far_scale; /* 0x184 */

public:
    std::string name = "anonymous";
    const std::string& getName() const { return name; }
    void setName(std::string value) { name = std::move(value); }
    bool isNamed() const { return !name.empty() && name != "anonymous"; }
    bool visible = true;
    Transform3D transform;
};

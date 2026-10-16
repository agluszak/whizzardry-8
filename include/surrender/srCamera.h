#pragma once

#include <optional>
#include <span>
#include <utility>

#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "srNode.h"

class srGERD;

// VTABLE: SURRENDER 0x10076E40 srCamera
// class srCamera
class SR_DLL_IMPORT srCamera : public srNode {
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

    srCamera(srNode* parent = 0);

    virtual void process(const ProcessInfo& info, e_processType type) override;

    void flipHorizontal();
    void flipVertical();
    double getAspectRatio() const;
    void setClipRange(double near_plane, double far_plane);
    std::pair<double, double> getClipRange() const;
    std::pair<float, float> getEnvironmentRange() const;
    std::pair<float, float> getEnvironmentScale() const;
    double getHorizontalFOV() const;
    Rect getNormalizedViewPlane() const;
    e_project getProjectionType() const;
    double getVerticalFOV() const;
    Projection project(const glm::dvec3& input);
    bool projectsAny(std::span<const glm::vec3> points);
    void setProjectionType(e_project projection);
    void normalizeViewPlane();

    void setEnvironmentScale(float near_scale, float far_scale);
    void setFocalLength(double focal_length, double aspect_ratio);
    void setFOV(double field_of_view, double aspect_ratio);
    void setViewPlane(const Rect& rectangle, double distance);
    void setViewPlane(double width, double height);
    ViewPlane getViewPlane() const;
    void setEnvironmentRange(float near_range, float far_range);
    std::optional<glm::vec3> unproject(const glm::dvec3& input);

protected:
    void processPop(srGERD* renderer);
    void processPush(srGERD* renderer);

public:
    enum e_flag { FLAG_PROJECTION_TYPE = 0 };

    srFlags<e_flag> flags; /* 0x138 */
private:
    Rect view_plane; /* 0x140 */
    double view_plane_distance; /* 0x160 */
    double near_clip; /* 0x168 */
    double far_clip; /* 0x170 */
    float environment_near; /* 0x178 */
    float environment_far; /* 0x17c */
    float environment_near_scale; /* 0x180 */
    float environment_far_scale; /* 0x184 */
};

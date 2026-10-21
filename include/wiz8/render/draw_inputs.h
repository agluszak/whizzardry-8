#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <vsg/utils/LineSegmentIntersector.h>

#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "wiz8/math.h"

class FogParams;
class Light;
class MeshInstance;

// The transforms one mesh submission, visibility query or light pass uses.
// Callers compose them explicitly; the renderer keeps no current matrix.
struct DrawTransform {
    glm::mat4 model_view{1};
    glm::mat4 projection{1};

    // The same view with an object transform applied before it.
    DrawTransform withModel(const glm::mat4& model) const;
    DrawTransform withModelView(const glm::mat4& value) const;

    glm::mat4 inverseModelView() const;
    float maxModelViewScale() const;
    // The projection rescaled for clipping against the near plane.
    glm::mat4 projectClipNear() const;
};

// Brings an object-space plane into the eye space of `model_view`.
glm::vec4 eyeSpaceClipPlane(const glm::mat4& model_view, const glm::vec4& plane);

// Software triangle culling. Back and Front are each flipped by a reversed winding.
enum class FaceCull : std::uint8_t { Back, Front, None };
enum class Winding : std::uint8_t { Standard, Reversed };

// A cursor position tested against the pickable triangles of a scene pass.
struct Pick {
    // Normalized pick point: x and y are the cursor's viewport-space
    // coordinates; z starts at the far value 1.0 and keeps the nearest hit.
    glm::vec3 position{0, 0, 1};
    // The intersection retains VSG coordinates, primitive data and graph identity.
    // Its node path borrows the scene nodes until the next graph mutation.
    vsg::ref_ptr<vsg::LineSegmentIntersector::Intersection> hit;
};

// Distances and clamped amplitudes of the camera's depth cue.
struct DepthCue {
    float minimum = 0;
    float maximum = 0;
    float near_scale = 0;
    float far_scale = 0;
};

// Inputs every scene pass of a frame shares. The frame chooses them, not the
// scene, and passes them to each scene it renders.
struct SceneFrame {
    // Environment light added to ambient light before brightness scales it.
    glm::vec3 environment_offset{0};
    // Scales ambient and light colours.
    float brightness = 1;
    // Power-of-two size reduction for textures the pass makes resident.
    std::int32_t texture_reduction = 0;
    // Seconds on the render clock; scrolls texture coordinates.
    double time = 0;
};

// The scene-pass inputs of one submission: frame inputs, lighting, fog colour,
// depth cue and light exclusion. A scene pass builds one and each
// drawable adjusts its own copy; the renderer keeps none of it.
struct ScenePass {
    SceneFrame frame;
    glm::vec4 ambient_light{0};
    // The colour fogged fragments blend towards, in [0, 1].
    glm::vec4 fog_color{0};
    std::span<FogParams* const> fogs;
    DepthCue depth_cue;
    // Light and fog groups the submission ignores.
    std::uint32_t exclusion_mask = 0;
};

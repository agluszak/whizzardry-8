#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "wiz8/math.h"
#include "wiz8/spatial/bounding_sphere.h"

class FogParams;
class Light;
class MeshInstance;

// Eye-space planes: [0, 6) bound the view frustum and [6, 32) are user planes.
struct ClipPlanes {
    std::array<glm::vec4, 32> planes{};
    std::uint32_t mask = 0;
};

// The transforms one mesh submission, visibility query or light pass uses.
// Callers compose them explicitly; the renderer keeps no current matrix.
struct DrawTransform {
    static constexpr std::size_t max_user_clip_planes = 26;

    glm::mat4 model_view{1};
    glm::mat4 projection{1};
    // Eye-space user clip planes, borrowed from the scene pass that owns them.
    std::span<const glm::vec4> user_clip_planes;

    // The same view with an object transform applied before it.
    DrawTransform withModel(const glm::mat4& model) const;
    DrawTransform withModelView(const glm::mat4& value) const;

    glm::mat4 inverseModelView() const;
    float maxModelViewScale() const;
    // Inverse transpose of the model-view basis with model scaling removed.
    glm::mat4 normalMatrix() const;
    // The projection rescaled for clipping against the near plane.
    glm::mat4 projectClipNear() const;
    ClipPlanes clipPlanes() const;
    BoundingSphere eyeSpaceBounds(const BoundingSphere& sphere) const;
    bool sphereVisible(const glm::vec3& center, float radius) const;
    bool boxVisible(const glm::vec3& minimum, const glm::vec3& maximum) const;
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
    MeshInstance* selected_model{};
    std::uint32_t triangle_index{};
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
// depth cue, light exclusion and picking. A scene pass builds one and each
// drawable adjusts its own copy; the renderer keeps none of it.
struct ScenePass {
    SceneFrame frame;
    glm::vec4 ambient_light{0};
    // The colour fogged fragments blend towards, in [0, 1].
    glm::vec4 fog_color{0};
    std::span<Light* const> lights;
    std::span<FogParams* const> fogs;
    DepthCue depth_cue;
    // Light and fog groups the submission ignores.
    std::uint32_t exclusion_mask = 0;
    // Picks the submitted triangles are tested against, borrowed from the
    // pass's caller. A hit records `pick_owner`; empty disables picking.
    std::span<Pick> picks;
    MeshInstance* pick_owner = nullptr;
};

// The caller-supplied inputs of one mesh submission.
struct MeshState {
    DrawTransform transform;
    FaceCull cull = FaceCull::Back;
    Winding winding = Winding::Standard;
    // Lights back faces with negated normals.
    bool reverse_normals = false;
    // Queues the triangles for the depth-sorted pass instead of drawing them in order.
    bool sorted = false;
    std::int32_t polygon_offset = 0;
    ScenePass scene;
};

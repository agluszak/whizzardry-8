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

// Eye-space planes: [0, 6) bound the view frustum and [6, 32) are user planes.
struct ClipPlanes {
    std::array<glm::vec4, 32> planes{};
    w8_ulong mask = 0;
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

// The caller-supplied inputs of one mesh submission. Scene lighting, fog and
// picking still come from the renderer's current scene pass.
struct MeshState {
    DrawTransform transform;
    FaceCull cull = FaceCull::Back;
    Winding winding = Winding::Standard;
    // Lights back faces with negated normals.
    bool reverse_normals = false;
    // Queues the triangles for the depth-sorted pass instead of drawing them in order.
    bool sorted = false;
    w8_long polygon_offset = 0;
};

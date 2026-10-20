#pragma once

// Global back-to-front order for sorted (translucent) meshes.
//
// The native sorted renderer collects the triangles of every sorted mesh,
// particle and pass drawn in a scene, and draws them in one stable depth order.
// Interleaving triangles of different objects cannot be reproduced by sorting
// objects, so this keeps the triangle order and turns it into draw runs: each
// run is a span of a per-frame index buffer that uses one instance, pass and
// batch state. Geometry stays resident; only the reordered indices change.

#include <cstdint>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "resident_mesh.h"

namespace render_poc {

struct SortedDraw {
    const ResidentMesh* mesh = nullptr;
    glm::mat4 model_view{1};
    // GpuRenderer's project_clip_near matrix (see projectClipNear).
    glm::mat4 projection{1};
    // Current positions of the mesh (framePositions); only sorted meshes need them.
    std::span<const glm::vec3> positions;
};

struct OrderedRun {
    std::uint32_t draw = 0;  // index into the SortedDraw list
    std::uint32_t pass = 0;
    std::uint32_t batch = 0; // index into that pass's batches
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
};

struct TransparentOrder {
    // Render-vertex indices of the pass named by each run, in drawing order.
    std::vector<std::uint32_t> indices;
    std::vector<OrderedRun> runs;
};

// The native key: the negated sum of the corners' clip-space z plus three times
// the mesh's sort bias, as a sortable unsigned value. Native sorting runs after
// the CPU projection, so the bias is in clip units.
std::uint32_t depthKey(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, float bias);

// The projection the native renderer applies to sorted vertices: the projection
// matrix, rescaled when its far terms do not cancel (gpu_renderer.cpp).
glm::mat4 projectClipNear(const glm::mat4& projection);

TransparentOrder orderTransparent(std::span<const SortedDraw> draws);

} // namespace render_poc

#pragma once

// Equivalence check between a GPU vertex stage and the native CPU pipeline.
//
// For each distinct (instance, pass, shader), the native shadeVertices runs
// over every render vertex with the inputs the game's renderer would give it:
// every prepared scene light and fog layer (shadeVertices rejects the ones
// that do not reach the object), the instance's ambient, overrides and
// effective shader. A backend draws the same vertices as single-pixel points
// (wzProbePosition) that output their diffuse and specular results, reads the
// RGBA8 target back, and compares. Rasterization and texturing are not part of
// this check; it isolates lighting, light selection, fog, depth cue and
// animation.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "resident_mesh.h"
#include "shading_block.h"

class FogParams;
class Light;
class MeshInstance;

namespace render_poc {

struct ProbeSource {
    const MeshInstance* instance = nullptr;
    const ResidentMesh* resident = nullptr;
    glm::mat4 model_view{1};
};

struct ProbeCase {
    ProbeSource source;
    std::uint32_t pass = 0;
    std::uint32_t batch = 0;
    std::uint32_t shader = 0; // effective DrawShader word
    std::uint32_t first_slot = 0;
    std::vector<glm::vec4> expected_diffuse;  // one per render vertex of the pass
    std::vector<glm::vec4> expected_specular;
};

struct ProbePlan {
    std::vector<ProbeCase> cases;
    int width = 256;
    int height = 0; // diffuse rows in the lower half, specular rows in the upper
};

// `lights` must be prepared for the view (prepareLight / prepareLightAt).
ProbePlan planProbes(std::span<const ProbeSource> sources, std::span<Light* const> lights,
                     std::span<FogParams* const> fogs, const ShadingEnvironment& environment);

// The probe selector a backend binds for one case and mode (1 diffuse,
// 2 specular): mode, first slot, target width and height.
glm::vec4 probeSelector(const ProbePlan& plan, const ProbeCase& probe, int mode);

struct ProbeReport {
    std::size_t vertices = 0;
    float max_error = 0; // in 8-bit steps
    std::size_t mismatches = 0;
    std::string summary;
    bool passed() const { return mismatches == 0; }
};

// `rgba` holds width * height RGBA8 pixels, row 0 first as read back.
ProbeReport compareProbes(const ProbePlan& plan, std::span<const std::uint8_t> rgba);

} // namespace render_poc

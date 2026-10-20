#pragma once

// Equivalence check between the GPU vertex stage and the native CPU pipeline.
//
// For each distinct (instance, pass, shader) of a planned frame, the native
// shadeVertices runs over every render vertex with exactly the inputs the
// shader receives. A backend draws the same vertices as single-pixel points
// (wzProbePosition) that output their diffuse and specular results, reads the
// RGBA8 target back, and compares. Rasterization and texturing are not part of
// this check; it isolates the lighting, fog, depth-cue and animation math.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "demo_scene.h"

namespace render_poc {

struct ProbeCase {
    const PlannedDraw* draw = nullptr;
    std::uint32_t pass = 0;
    std::uint32_t batch = 0;
    std::uint32_t first_slot = 0;
    std::vector<glm::vec4> expected_diffuse;  // one per render vertex of the pass
    std::vector<glm::vec4> expected_specular;
};

struct ProbePlan {
    std::vector<ProbeCase> cases;
    int width = 256;
    int height = 0; // diffuse rows in the lower half, specular rows in the upper
};

ProbePlan planProbes(DemoScene& scene, const FramePlan& frame);

// The draw block a backend binds for one probe case and mode (1 diffuse, 2 specular).
DrawBlock probeDrawBlock(const ProbePlan& plan, const ProbeCase& probe, int mode);

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

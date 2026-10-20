#pragma once

#include <optional>

#include "glm/ext/vector_float4.hpp"
#include "wiz8/render/gpu_renderer.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_draw.h"

namespace meshSubmission {

class AmbientScope {
public:
    explicit AmbientScope(GpuRenderer& renderer)
        : renderer(renderer), previous(renderer.getAmbientLight())
    {
    }
    ~AmbientScope() { renderer.setAmbientLight(previous); }
    glm::vec4 ambient() const { return previous; }
    AmbientScope(const AmbientScope&) = delete;
    AmbientScope& operator=(const AmbientScope&) = delete;

private:
    GpuRenderer& renderer;
    glm::vec4 previous;
};

class PickScope {
public:
    PickScope(GpuRenderer& renderer, bool suppress) : renderer(renderer)
    {
        if (suppress) pick = renderer.popPick();
    }
    ~PickScope()
    {
        if (pick) renderer.pushPick(*pick);
    }
    PickScope(const PickScope&) = delete;
    PickScope& operator=(const PickScope&) = delete;

private:
    GpuRenderer& renderer;
    std::optional<GpuRenderer::Pick> pick;
};

// Side setup stays with the caller: native back faces reverse normals and may
// reuse the front material, whereas software culling supplies a polygon span.
// `configure` adjusts its side's copy of the state and returns the material side.
template<class ConfigureSide>
void drawSides(GpuRenderer& renderer, const Mesh::TriMesh& mesh, const MeshState& state,
               ConfigureSide configure)
{
    for (w8_long side = 1; side >= 0; --side) {
        if ((mesh.control_flags & (1u << side)) == 0) continue;
        auto side_state = state;
        const w8_long material_side = configure(side, side_state);
        renderer.submitMesh(mesh, side_state, material_side);
    }
}

} // namespace meshSubmission

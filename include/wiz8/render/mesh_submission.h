#pragma once

#include "compat/compiler.h"
#include "wiz8/render/draw_inputs.h"
#include "wiz8/render/gpu_renderer.h"
#include "wiz8/render/mesh.h"

namespace meshSubmission {

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

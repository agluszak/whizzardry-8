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

class WindingScope {
public:
    explicit WindingScope(GpuRenderer& renderer)
        : renderer(renderer), previous(renderer.getWinding())
    {
    }
    ~WindingScope() { renderer.setWinding(previous); }
    WindingScope(const WindingScope&) = delete;
    WindingScope& operator=(const WindingScope&) = delete;

private:
    GpuRenderer& renderer;
    GpuRenderer::e_winding previous;
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

class EnableScope {
public:
    explicit EnableScope(GpuRenderer& renderer, bool sorted)
        : renderer(renderer), previous(renderer.drawOptions())
    {
        if (sorted && !renderer.isEnabled(GpuRenderer::ENABLE_SORTED_RENDERING))
            renderer.toggle(GpuRenderer::ENABLE_SORTED_RENDERING);
    }
    ~EnableScope() { renderer.setDrawOptions(previous); }
    EnableScope(const EnableScope&) = delete;
    EnableScope& operator=(const EnableScope&) = delete;

private:
    GpuRenderer& renderer;
    std::uint32_t previous;
};

// Side setup stays with the caller: native back faces reverse normals and may
// reuse the front material, whereas software culling supplies a polygon span.
template<class ConfigureSide>
void drawSides(GpuRenderer& renderer, const Mesh::TriMesh& mesh, ConfigureSide configure)
{
    for (w8_long side = 1; side >= 0; --side) {
        if ((mesh.control_flags & (1u << side)) == 0) continue;
        renderer.submitMesh(mesh, configure(side));
    }
}

} // namespace meshSubmission

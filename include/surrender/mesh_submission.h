#pragma once

#include <cstdint>
#include <optional>
#include "glm/ext/vector_float4.hpp"

#include "compat/native.h"
#include "surrender/srGERD.h"
#include "surrender/srMeshModel.h"
#include "surrender/srTriMeshPipeline.h"

namespace srMeshSubmission {

class AmbientScope {
public:
    explicit AmbientScope(srGERD& renderer)
        : renderer(renderer), previous(renderer.getAmbientLight()) {}
    ~AmbientScope() { renderer.setAmbientLight(previous); }
    glm::vec4 ambient() const { return previous; }
    AmbientScope(const AmbientScope&) = delete;
    AmbientScope& operator=(const AmbientScope&) = delete;
private:
    srGERD& renderer;
    glm::vec4 previous;
};

class WindingScope {
public:
    explicit WindingScope(srGERD& renderer) : renderer(renderer), previous(renderer.getWinding()) {}
    ~WindingScope() { renderer.setWinding(previous); }
    WindingScope(const WindingScope&) = delete;
    WindingScope& operator=(const WindingScope&) = delete;
private:
    srGERD& renderer;
    srGERD::e_winding previous;
};

class PickScope {
public:
    PickScope(srGERD& renderer, bool suppress) : renderer(renderer)
    {
        if (suppress) pick = renderer.popPick();
    }
    ~PickScope() { if (pick) renderer.pushPick(*pick); }
    PickScope(const PickScope&) = delete;
    PickScope& operator=(const PickScope&) = delete;

private:
    srGERD& renderer;
    std::optional<srGERD::Pick> pick;
};

class EnableScope {
public:
    explicit EnableScope(srGERD& renderer, bool sorted) : renderer(renderer)
    {
        renderer.pushEnable();
        if (sorted && !renderer.isEnabled(srGERD::ENABLE_SORTED_RENDERING))
            renderer.toggle(srGERD::ENABLE_SORTED_RENDERING);
    }
    ~EnableScope() { renderer.popEnable(); }
    EnableScope(const EnableScope&) = delete;
    EnableScope& operator=(const EnableScope&) = delete;

private:
    srGERD& renderer;
};

// Side setup stays with the caller: native back faces reverse normals and may
// reuse the front material, whereas software culling supplies a polygon span.
template<class ConfigureSide, class ShaderAt>
void drawSides(srGERD& renderer, const srMeshModel::TriMesh& mesh,
               ConfigureSide configure, ShaderAt shaderAt)
{
    for (std::int32_t side = 1; side >= 0; --side) {
        if ((mesh.control_flags & (1u << side)) == 0) continue;
        auto& pipeline = srTriMeshPipeline::beginMesh(renderer, mesh);
        const auto material_side = configure(pipeline, side);
        for (std::int32_t pass = 0; pass < mesh.pass_count; ++pass)
            pipeline.AppendMeshPass(mesh, pass, material_side, shaderAt(pass));
        pipeline.FlushIfCurrent();
    }
}

} // namespace srMeshSubmission

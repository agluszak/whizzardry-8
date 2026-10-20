#pragma once

// Per-draw shader inputs for the GLSL port of shadeVertices.
//
// Every per-object decision that the CPU pipeline makes once per submission
// (which lights and fog layers reach the object, the depth-cue environment,
// material parameters with the instance's diffuse scale and emissive override)
// stays on the CPU and is packed here. The per-vertex equations run on the GPU.
// The block is a flat vec4 array so every library can bind it the same way:
// a uniform array (OpenGL libraries) or a uniform buffer (Vulkan).

#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>

#include <glm/glm.hpp>

#include "resident_mesh.h"
#include "wiz8/spatial/bounding_sphere.h"

class Light;
class FogParams;
class Material;

namespace render_poc {

inline constexpr int kMaxLights = 8;
inline constexpr int kMaxFogs = 4;
inline constexpr int kMaxMaterials = 16;
inline constexpr int kLightVec4s = 7;
inline constexpr int kMaterialVec4s = 6;
inline constexpr int kLightBase = 3;
inline constexpr int kFogBase = kLightBase + kMaxLights * kLightVec4s;
inline constexpr int kMaterialBase = kFogBase + kMaxFogs;
inline constexpr int kShadingVec4s = kMaterialBase + kMaxMaterials * kMaterialVec4s;

// The vertex stage's per-batch state. Packed the same way for every library.
// The last vec4 selects the verification probe (see lighting_check.h).
inline constexpr int kDrawVec4s = 6;

using ShadingBlock = std::array<glm::vec4, kShadingVec4s>;
using DrawBlock = std::array<glm::vec4, kDrawVec4s>;

// Camera-wide inputs, the values GpuRenderer hands shadeVertices.
struct ShadingEnvironment {
    glm::vec4 ambient_light{0};
    float environment_minimum = 0;
    float environment_maximum = 1000;
    float environment_scale = 0;
    float environment_inverse_scale = 1;
    double frame_time = 0;
};

// What a draw's vertex stage needs from its instance.
struct InstanceShading {
    BoundingSphere eye_bounds;
    std::uint32_t exclusion_mask = 0;
    std::optional<float> diffuse_scale;
    std::optional<float> emissive_override;
};

// `lights` and `fogs` must already be prepared for the current view
// (Light::beginPass derives the eye-space fields the shader reads).
ShadingBlock packShading(const ShadingEnvironment& environment, const InstanceShading& instance,
                         std::span<Light* const> lights, std::span<FogParams* const> fogs,
                         std::span<Material* const> materials);

// Fragment and vertex batch state: the final channels of the batch's shader,
// the pass's DrawMaterial flags, frame selection and the fixed-function
// combiner fields of DrawShader.
struct DrawState {
    std::uint32_t final_channels = 0;
    std::uint32_t draw_flags = 0;
    bool has_normals = false;
    int position_count = 0;
    int frame = 0;
    int next_frame = -1;
    float interpolation = 0;
    int frame_count = 1;
    std::uint32_t shader = 0;
    glm::vec4 fog_color{0};
};
DrawBlock packDraw(const DrawState& state);

// The depth, blend and write state of a DrawShader word, decoded once so each
// library only maps enumerations. Values keep DrawShader's numbering.
struct RasterState {
    std::uint32_t depth_compare = 3; // DrawShader::PASS_*
    bool depth_test = true;
    bool depth_write = true;
    bool color_write = true;
    bool blend = false;
    std::uint32_t source = 1;      // DrawShader::SRCBLEND_*
    std::uint32_t destination = 0; // DrawShader::DSTBLEND_*
};
RasterState rasterState(std::uint32_t shader);

// "#define WZ_..." lines that keep the GLSL layout in step with this header.
// Header-only so that a build-time tool can write it for offline compilers.
inline std::string glslLayoutDefinitions()
{
    return std::format("#define WZ_LIGHT_BASE {}\n#define WZ_LIGHT_VEC4S {}\n"
                       "#define WZ_FOG_BASE {}\n#define WZ_MATERIAL_BASE {}\n"
                       "#define WZ_MATERIAL_VEC4S {}\n#define WZ_SHADING_VEC4S {}\n"
                       "#define WZ_DRAW_VEC4S {}\n#define WZ_MAX_LIGHTS {}\n"
                       "#define WZ_MAX_FOGS {}\n#define WZ_GEOMETRY_WIDTH {}\n",
                       kLightBase, kLightVec4s, kFogBase, kMaterialBase, kMaterialVec4s,
                       kShadingVec4s, kDrawVec4s, kMaxLights, kMaxFogs, kGeometryTextureWidth);
}

// The two GLSL sources every backend embeds. They expect the host to define
// `vec4 wzShading(int)`, `vec4 wzDraw(int)` and `vec4 wzGeometry(int)` first.
std::string_view wizardryVertexGlsl();
std::string_view wizardryFragmentGlsl();

// Light::beginPass without a GpuRenderer: derive the eye-space fields from a
// view matrix the way the native renderer does for a light at `light.transform`.
void prepareLight(Light& light, const glm::mat4& view, float scene_brightness = 1);

} // namespace render_poc

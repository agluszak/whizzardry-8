#pragma once

// Shader inputs for the GLSL port of shadeVertices, grouped by how often they
// change and who owns them:
//
//   ViewBlock      per camera: environment, every prepared light and fog layer
//   InstanceBlock  per MeshInstance: frames, bounds, exclusion mask, overrides
//   PassBlock      per shared mesh pass: materials (scrolling advances per frame)
//   BatchBlock     per shared batch: the DrawShader combiner and written channels
//
// The shader core addresses one flat index space through wzShading(index):
// [0] the instance ambient, [1] the environment, [2] light and fog counts,
// lights from kLightBase, fog layers from kFogBase, then materials from
// kMaterialBase. A host routes each index to its block. Light and fog selection
// (shadeVertices' per-object rejections) runs in the shader, against the
// instance's eye-space bounds, so the view block is shared by every object.
// Blocks are std140-compatible: vec4 and uvec4 members only.

#include <array>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>

#include <glm/glm.hpp>

#include "resident_mesh.h"

class FogParams;
class Light;
class MeshInstance;

namespace render_poc {

inline constexpr int kMaxLights = 32;
inline constexpr int kMaxFogs = 4;
inline constexpr int kMaxMaterials = 16;
inline constexpr int kLightVec4s = 7;
inline constexpr int kMaterialVec4s = 6;
inline constexpr int kLightBase = 3;
inline constexpr int kFogBase = kLightBase + kMaxLights * kLightVec4s;
inline constexpr int kMaterialBase = kFogBase + kMaxFogs;

// Camera-wide inputs, the values GpuRenderer hands shadeVertices.
struct ShadingEnvironment {
    glm::vec4 ambient_light{0};
    float environment_minimum = 0;
    float environment_maximum = 1000;
    float environment_scale = 0;
    float environment_inverse_scale = 1;
    double frame_time = 0;
};

struct ViewBlock {
    // [1] environment, [2] counts, lights, then fog layers as (start,
    // 1 / (end - start), density, end). [0] is unused: ambient is per instance.
    std::array<glm::vec4, kMaterialBase> values{};
    glm::vec4 fog_color{0};
    // Group masks: lights first, then fog layers.
    std::array<glm::uvec4, kMaxLights + kMaxFogs> masks{};
};

struct InstanceBlock {
    glm::vec4 ambient{0};
    glm::vec4 frames{0, -1, 0, 0}; // frame, next frame or -1, interpolation
    glm::vec4 bounds{0};           // model-space centre and radius
    glm::vec4 overrides{0};        // diffuse scale on, value; emissive on, value
    glm::uvec4 masks{0};           // x: exclusion mask
};

struct PassBlock {
    std::array<glm::vec4, kMaxMaterials * kMaterialVec4s> materials{};
    glm::vec4 mesh{0}; // position count, frame count, has normals, DrawMaterial flags
};

struct BatchBlock {
    glm::vec4 channels{0}; // x: channels the vertex stage writes
    glm::vec4 modes{0};    // texturing, gradient, secondary gradient, fog mode
    glm::vec4 alpha_test{0};
    glm::vec4 probe{0};    // verification probe selector (lighting_check.h)
};

// `lights` must already be prepared for the view (prepareLightAt derives the
// eye-space fields the shader reads), in the order the view collected them.
ViewBlock packView(const ShadingEnvironment& environment, std::span<Light* const> lights,
                   std::span<FogParams* const> fogs, const glm::vec4& fog_color);
InstanceBlock packInstance(const MeshInstance& instance, const ResidentMesh& mesh,
                           const glm::vec4& scene_ambient);
PassBlock packPass(const ResidentMesh& mesh, std::size_t pass, double frame_time);
// `shader` is the effective DrawShader word (see effectiveShader).
BatchBlock packBatch(std::uint32_t shader);

// Per-instance rules of MeshInstance::RenderMeshes that a renderer must keep.
glm::vec4 instanceAmbient(const MeshInstance& instance, const glm::vec4& scene_ambient);
std::uint32_t effectiveShader(const MeshInstance& instance, std::uint32_t shader);
// The blended variant of `shader` that a fading (diffuse-scaled) instance uses.
std::uint32_t fadingShader(std::uint32_t shader);
bool drawsSorted(const MeshInstance& instance);

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
                       "#define WZ_MATERIAL_VEC4S {}\n#define WZ_MAX_LIGHTS {}\n"
                       "#define WZ_MAX_FOGS {}\n#define WZ_MAX_MATERIALS {}\n"
                       "#define WZ_GEOMETRY_WIDTH {}\n",
                       kLightBase, kLightVec4s, kFogBase, kMaterialBase, kMaterialVec4s, kMaxLights,
                       kMaxFogs, kMaxMaterials, kGeometryTextureWidth);
}

// The two GLSL sources every backend embeds; their headers list the functions
// a host defines first.
std::string_view wizardryVertexGlsl();
std::string_view wizardryFragmentGlsl();

// Light::beginPass without a GpuRenderer: derive the eye-space fields of a
// light whose complete model-view matrix is known, such as a light placed in a
// scene graph under other transforms.
void prepareLightAt(Light& light, const glm::mat4& model_view, float scene_brightness = 1);

} // namespace render_poc

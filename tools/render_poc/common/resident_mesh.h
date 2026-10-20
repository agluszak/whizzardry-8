#pragma once

// GPU-resident layout of a game Mesh, built once at load time.
//
// The native renderer rebuilds this information every frame: it decodes and
// interpolates frames, shades every vertex, expands UV seams and duplicates
// streams per pass before uploading. Here the layout is computed once and every
// per-frame quantity (frame numbers, matrices, lights) becomes a shader input.

#include <cstdint>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "wiz8/render/draw_shader.h"

class Mesh;
class Material;
class Texture;

namespace render_poc {

// The geometry texture stores every frame's positions followed by every
// frame's normals, one RGBA32F texel per value, wrapped at this width.
inline constexpr int kGeometryTextureWidth = 1024;

// One render vertex: a position index into the geometry texture plus the
// per-corner attributes the CPU pipeline gathered from the mesh tables.
struct ResidentVertex {
    // x position index, y material slot, z 1 when the corner's UV index is the
    // vertex itself (only those corners receive UV mapping), w vertex alpha.
    glm::vec4 index_material_alpha{0, 0, 1, 1};
    glm::vec4 texcoords{0}; // stage 0 xy, stage 1 zw
    glm::vec4 color{0};     // the pass's DIG colour, added to the lit diffuse
    glm::vec4 diffuse_multiplier{1};
    glm::vec4 specular_multiplier{1};
};
static_assert(sizeof(ResidentVertex) == 5 * sizeof(glm::vec4));

// A contiguous run of triangles sharing a texture and a shader, in the mesh's
// own triangle order (the native immediate renderer splits runs the same way).
struct ResidentBatch {
    Texture* texture = nullptr;
    DrawShader shader;
    // Channels the vertex stage writes for this shader (~shaderDisabledChannels).
    std::uint32_t final_channels = 0;
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
};

struct ResidentPass {
    std::vector<ResidentVertex> vertices;
    std::vector<std::uint32_t> indices;
    // Source mesh polygon of each triangle, used for transparent sorting.
    std::vector<std::uint32_t> polygons;
    std::vector<ResidentBatch> batches;
    // Material table addressed by ResidentVertex::index_material_alpha.y.
    std::vector<Material*> materials;
    // DrawMaterial::HAS_* flags of the pass.
    std::uint32_t draw_flags = 0;
    bool has_texcoord0 = false;
};

struct ResidentGeometry {
    int position_count = 0;
    int frame_count = 0;
    bool has_normals = false;
    int width = kGeometryTextureWidth;
    int height = 0;
    std::vector<glm::vec4> texels;
};

struct ResidentMesh {
    ResidentGeometry geometry;
    std::vector<ResidentPass> passes;
    std::vector<glm::ivec3> triangles; // position indices, for CPU sorting
    glm::vec3 bounds_center{0};
    float bounds_radius = 0;
    float sort_bias = 0;
    bool sorted = false;
    // Native sorting offsets each further pass's keys only when no pass has a
    // UV-corner table (see mesh_batches.cpp's flat and dedup paths).
    bool offset_pass_keys = true;
};

// Builds the resident layout from the front side of `mesh`, uploading every
// animation frame decoded by the mesh's own frame codec.
ResidentMesh buildResidentMesh(Mesh& mesh);

// Positions of one frame interpolated exactly as Mesh::GetVertexLocations does;
// only transparent meshes need them on the CPU (for sorting).
std::vector<glm::vec3> framePositions(const ResidentGeometry& geometry, unsigned frame,
                                      float interpolation);

// Frame selection uniforms: current frame, next frame (or -1), interpolation.
struct FrameSelection {
    int frame = 0;
    int next = -1;
    float interpolation = 0;
};
FrameSelection selectFrame(const ResidentGeometry& geometry, unsigned frame, float interpolation);

} // namespace render_poc

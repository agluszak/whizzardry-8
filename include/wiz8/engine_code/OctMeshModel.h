#pragma once

#include <memory>
#include <vector>

#include "glm/ext/vector_int3.hpp"

class srMaterial;
class srShader;
class srTexture;
class stMaterial;
class stMeshModel;

#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"

namespace wiz8 {
class File;
}
struct W8MeshMaterial;

/* Engine Code\OctSubMesh.cpp's serialized mesh workspace. The assertion in
   its write method names the original class OctMeshModel; the adjacent
   constructor, destructor, read, and write bodies all use this same 0x48-byte
   receiver. Only fields whose roles are established by those bodies are named. */
class OctMeshModel {
public:
    OctMeshModel() = default;
    // Owns its arrays: moves transfer them, copies are not allowed.
    OctMeshModel(OctMeshModel&& other) noexcept = default;
    OctMeshModel& operator=(OctMeshModel&& other) noexcept = default;
    OctMeshModel(const OctMeshModel&) = delete;
    OctMeshModel& operator=(const OctMeshModel&) = delete;
    std::unique_ptr<stMeshModel> Read(wiz8::File* file,
                                      const std::vector<W8MeshMaterial>& materials);
    bool Write(wiz8::File& hFile); /* 0x0049E5D0 */

    short version{};
    int m_link_index{};
    int next_link{};
    int m_material_index{};
    int m_map_count{};                       /* m_psrMap entry count */
    std::vector<glm::vec3> m_vertex_locations; /* m_psrVertLoc */
    std::vector<glm::vec2> m_vertex_map;       /* m_psrMap */
    std::vector<int> m_vertex_materials;               /* m_plVertMats */
    std::vector<glm::ivec3> m_poly_vertices;           /* m_psrPolyVertex */
    std::vector<glm::ivec3> m_poly_uv_index;           /* m_psrPolyUVIndex */
    std::vector<int> m_poly_textures;                  /* m_plPolyTextures */
    std::vector<glm::vec3> m_vertex_normals;   /* m_pVertNorms */
    std::vector<glm::vec3> m_vertex_lights;    /* m_pVertLights */
    std::vector<glm::vec4> m_poly_equations;   /* m_psrPolyEqtns */
    std::vector<std::vector<float>> m_sun_lights;                  /* m_ppflSunLights */
    unsigned int m_packed_header{};
    int m_vertex_count{};
    int m_polygon_count{};
};

/* The first Read call snapshots material/texture/render-flag zero as the
   shared default rendering state for the path and trace meshes. */
extern W8MeshMaterial g_oct_mesh_default;
void ReleaseOctMeshDefaults();

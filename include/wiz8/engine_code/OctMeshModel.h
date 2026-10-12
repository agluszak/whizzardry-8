#pragma once

#include <cstdint>
#include "glm/ext/vector_int3.hpp"

#include <memory>
#include <vector>

#include "wiz8/engine_code/materials.h"

#include "wiz8/filesystem.h"

class srMaterialIFace;
class srShader;
class srTextureIFace;
class stMaterial;
class stMeshModel;

#include "surrender/srMath.h"

/* Engine Code\OctSubMesh.cpp's serialized mesh workspace. The assertion in
   its write method names the original class OctMeshModel; the adjacent
   constructor, destructor, read, and write bodies all use this same 0x48-byte
   receiver. Only fields whose roles are established by those bodies are named. */
class OctMeshModel {
public:
    OctMeshModel();  /* 0x0049E4C0 */
    ~OctMeshModel(); /* 0x0049E500 */
    // Owns its arrays: moves transfer them, copies are not allowed.
    OctMeshModel(OctMeshModel&& other) noexcept;
    OctMeshModel& operator=(OctMeshModel&& other) noexcept;
    OctMeshModel(const OctMeshModel&) = delete;
    OctMeshModel& operator=(const OctMeshModel&) = delete;
    std::unique_ptr<stMeshModel> Read(wiz8::File* file, const std::vector<W8MeshMaterial>& materials);
    bool Write(wiz8::File& hFile); /* 0x0049E5D0 */

    std::int16_t version;
    std::int16_t padding_02;
    std::int32_t m_link_index;
    std::int32_t next_link;
    std::int32_t m_material_index;
    std::int32_t m_map_count;                       /* m_psrMap entry count */
    srVector3T<float>* m_vertex_locations; /* m_psrVertLoc */
    glm::vec2* m_vertex_map;       /* m_psrMap */
    std::int32_t* m_vertex_materials;               /* m_plVertMats */
    glm::ivec3* m_poly_vertices;           /* m_psrPolyVertex */
    glm::ivec3* m_poly_uv_index;           /* m_psrPolyUVIndex */
    std::int32_t* m_poly_textures;                  /* m_plPolyTextures */
    srVector3T<float>* m_vertex_normals;   /* m_pVertNorms */
    srVector3T<float>* m_vertex_lights;    /* m_pVertLights */
    glm::vec4* m_poly_equations;   /* m_psrPolyEqtns */
    float** m_sun_lights;                  /* m_ppflSunLights */
    std::uint32_t m_packed_header;
    std::int32_t m_vertex_count;
    std::int32_t m_polygon_count;
};

/* The first Read call snapshots material/texture/render-flag zero as the
   shared default rendering state for the path and trace meshes. */
extern W8MeshMaterial g_oct_mesh_default;
void ReleaseOctMeshDefaults();

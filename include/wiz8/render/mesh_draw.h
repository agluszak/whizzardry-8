#pragma once
#include "wiz8/render/mesh.h"
#include "wiz8/render/vertex_shading.h"

struct MeshPass {
    Texture* textures[2]{};
    DrawShader shader;
    // Borrowed tables are consumed synchronously and copied into owned draw batches.
    const MeshResources<Texture>* texture_tables[2]{};
    const DrawShader* shaders = nullptr;
    const glm::vec2* texcoords = nullptr;
    /* The mesh's per-triangle poly-UV corner source table. */
    const glm::ivec3* poly_uv = nullptr;
};
struct MeshDrawPass {
    DrawMaterial vertex;
    MeshPass render;
};
MeshDrawPass makeMeshDrawPass(const Mesh::TriMesh& mesh, w8_long pass, w8_long material_side);

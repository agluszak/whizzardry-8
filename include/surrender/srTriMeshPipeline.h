#pragma once

#include <memory>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int3.hpp"
#include "srMaterial.h"
#include "srMath.h"
#include "srMeshModel.h"
#include "srShader.h"
#include "srTexture.h"
#include "srVertexPipe.h"

class srGERD;
class srMaterial;

/* Mesh-processing scratch owned by one renderer. */
class srTriMeshPipeline {
public:
    enum { FRUSTUM_CLIPPING = 1u, LIMIT_VERTEX_BATCHES = 2u };

    struct Pass {
        srTexture* textures[2]{};
        srShader shader;
        /* Borrowed per-polygon texture tables. FlushSlots consumes them synchronously in
           Renderer::render, which copies them into owned batch data. */
        const std::shared_ptr<srTexture>* texture_tables[2]{};
        const srShader* shaders = nullptr;
        const glm::vec2* texcoords = nullptr;
        /* The mesh's per-triangle poly-UV corner source table. */
        const glm::ivec3* poly_uv = nullptr;
    };
    enum e_boundsSource { BOUNDS_FROM_VERTICES = 0, BOUNDS_SPHERE = 1, BOUNDS_BOX = 2 };
    using Record = srVertexPipe::Record;

    static srTriMeshPipeline& beginMesh(srGERD& renderer, const srMeshModel::TriMesh& mesh);
    void SetFlags(srShader shader);
    void Reset(srGERD* renderer);
    void Flush();
    void PrepareSlot();
    void SetMeshBounds(const srMeshModel::TriMesh& mesh);
    void AppendMeshPass(const srMeshModel::TriMesh& mesh, w8_long pass, w8_long material_side,
                        srShader pass_shader);

    virtual void FlushSlots();
    virtual ~srTriMeshPipeline() = default;

    std::vector<srVertexProcessor*> vertex_processors;
    Record* current_record;
    Pass* current_pass;
    w8_ulong triangle_count;
    w8_ulong vertex_count;
    /* Bit 0: run getClipMask (frustum 0x3f plus user planes in bits 6+).
       Bit 1: vertex/triangle batch-limit path. Reset always set both. */
    w8_ulong flags;
    srMeshModel::PolygonSelection active_triangles;
    const glm::vec4* projected_vertices;
    const glm::ivec3* triangles;
    /* stParticle stores its vertex positions here; Reset null it. */
    const glm::vec3* positions;
    const glm::vec3* vertex_extras;
    float sort_bias;
    glm::vec3 bounds_minimum;
    glm::vec3 bounds_maximum;
    glm::vec3 bounds_center;
    float bounds_radius;
    e_boundsSource bounds_source;
    w8_ulong unknown_70;
    srShader shader;
    srTexture* texture0;
    srTexture* texture1;
    srMaterial* material;
    w8_ulong slot_count;
    srGERD* renderer;
    srVertexPipe vertex_pipe;
    /* PrepareSlot may relocate these vectors; it rebinds current_record/current_pass afterwards.
       Completed slots keep their borrowed tables until FlushSlots finishes consuming them. */
    std::vector<Record> records;
    std::vector<Pass> passes;
    std::vector<srVertexArray> vertex_arrays;

private:
    friend class srGERD;
    srTriMeshPipeline();
};

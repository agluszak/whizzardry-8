#pragma once

#include <memory>
#include <vector>
#include "srMaterialIFace.h"
#include "srMath.h"
#include <memory>
#include "srShader.h"
#include "srTextureIFace.h"
#include "srVertexPipe.h"

class srGERD;
class srMaterialIFace;

/* Shared lazy singleton behind srTriMeshPipeline::pipe; owns an srVertexPipe. */
class srTriMeshPipeline {
public:
    enum { FRUSTUM_CLIPPING = 1u, LIMIT_VERTEX_BATCHES = 2u };

    struct Pass {
        srTextureIFace* textures[2]{};
        srShader shader;
        /* Borrowed per-polygon texture tables. FlushSlots consumes them synchronously in
           Renderer::render before unlockRenderer queues owned render data. */
        const std::shared_ptr<srTextureIFace>* texture_tables[2]{};
        const srShader* shaders = nullptr;
        const glm::vec2* texcoords = nullptr;
        /* The mesh's per-triangle poly-UV corner source table. */
        const srVector3i* poly_uv = nullptr;
    };
    enum e_boundsSource { BOUNDS_FROM_VERTICES = 0, BOUNDS_SPHERE = 1, BOUNDS_BOX = 2 };
    using Record = srVertexPipe::Record;

    static srTriMeshPipeline* Get(srGERD* renderer);
    void SetFlags(srShader shader);
    void Reset(srGERD* renderer);
    void Flush();
    void PrepareSlot();

    inline void FlushIfCurrent()
    {
        if (this == pipe) {
            Flush();
        }
    }

    virtual void FlushSlots();
    virtual ~srTriMeshPipeline() = default;

    std::vector<srVertexProcessor*> vertex_processors;
    std::vector<w8_ulong> culler_scratch;
    Record* current_record;
    Pass* current_pass;
    w8_ulong triangle_count;
    w8_ulong vertex_count;
    w8_ulong active_triangle_count;
    /* Bit 0: run getClipMask (frustum 0x3f plus user planes in bits 6+).
       Bit 1: vertex/triangle batch-limit path. Reset/Get always set both. */
    w8_ulong flags;
    const w8_ulong* active_triangles;
    const glm::vec4* projected_vertices;
    const srVector3i* triangles;
    /* stParticle stores its vertex positions here; Reset/Get null it. */
    const srVector3T<float>* positions;
    const srVector3T<float>* vertex_extras;
    float sort_bias;
    glm::vec3 bounds_minimum;
    glm::vec3 bounds_maximum;
    glm::vec3 bounds_center;
    float bounds_radius;
    e_boundsSource bounds_source;
    w8_ulong unknown_70;
    srShader shader;
    srTextureIFace* texture0;
    srTextureIFace* texture1;
    srMaterialIFace* material;
    w8_ulong slot_count;
    srGERD* renderer;
    std::unique_ptr<srVertexPipe> vertex_pipe;
    /* PrepareSlot may relocate these vectors; it rebinds current_record/current_pass afterwards.
       Completed slots keep their borrowed tables until FlushSlots finishes consuming them. */
    std::vector<Record> records;
    std::vector<Pass> passes;
    std::vector<srVertexArray> vertex_arrays;

protected:
    /* srExit releases the singleton through this protected static. */
    friend SR_DLL_IMPORT int __cdecl srExit(void);
    static SR_DLL_IMPORT srTriMeshPipeline* pipe;

private:
    srTriMeshPipeline();
};


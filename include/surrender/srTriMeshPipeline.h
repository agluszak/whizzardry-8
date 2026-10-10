#pragma once

#include <memory>
#include <vector>
#include "srMaterialIFace.h"
#include "srMath.h"
#include "srPtr.h"
#include "srShader.h"
#include "srTextureIFace.h"
#include "srVertexPipe.h"

class srGERD;
class srMaterialIFace;

/* Shared lazy singleton behind srTriMeshPipeline::pipe; owns an srVertexPipe. */
class srTriMeshPipeline {
public:
    enum { FRUSTUM_CLIPPING = 1u, LIMIT_VERTEX_BATCHES = 2u };
    using Record = srVertexPipe::Record;

    struct Pass {
        srTextureIFace* textures[2]{};
        srShader shader;
        /* Borrowed per-stage srPtr<srTextureIFace> or stTextureAnim* tables.
           The renderer reads pointer-sized entries without owning the tables. FlushSlots consumes
           them synchronously in Renderer::render before unlockRenderer queues owned render data. */
        void* texture_tables[2]{};
        const srShader* shaders = nullptr;
        const srVector2T<float>* texcoords = nullptr;
        /* The mesh's per-triangle poly-UV corner source table. */
        const srVector3i* poly_uv = nullptr;
    };

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
    const srVector4T<float>* projected_vertices;
    const srVector3i* triangles;
    /* stParticle stores its vertex positions here; Reset/Get null it. */
    const srVector3T<float>* positions;
    const srVector3T<float>* vertex_extras;
    float sort_bias;
    srVector3T<float> bounds_minimum;
    srVector3T<float> bounds_maximum;
    srVector3T<float> bounds_center;
    float bounds_radius;
    enum e_boundsSource { BOUNDS_FROM_VERTICES = 0, BOUNDS_SPHERE = 1, BOUNDS_BOX = 2 };
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

W8_ABI_ASSERT(sizeof(srTriMeshPipeline) == 0xac, "srTriMeshPipeline_must_be_0xac");

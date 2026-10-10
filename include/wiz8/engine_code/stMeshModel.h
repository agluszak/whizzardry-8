#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "surrender/srMeshModel.h"
#include "surrender/srTypeRegistry.h"

/* Engine Code\stMeshModel.cpp. Compressed frame bytes retain their asset format;
   float caches and render scratch are owned independently of the base mesh. */
enum W8MeshFrameData {
    W8_MESH_FRAME_LOCATIONS = 1,
    W8_MESH_FRAME_VERTEX_NORMALS = 2,
    W8_MESH_FRAME_POLYGON_NORMALS = 4
};

enum W8MeshModelFlag {
    W8_MESH_SORTED_RENDERING = 1,
    W8_MESH_VERTEX_LIGHTING_DIRTY = 2,
    W8_MESH_HAS_FRAME_STORAGE = 4
};

// VTABLE: WIZ8 0x005ec454 stMeshModel
// VTABLE: WIZ8 0x005ec4b0 srClassSupport<stMeshModel, srMeshModel, 65539>
class stMeshModel : public srClassSupport<stMeshModel, srMeshModel, 0x10003> {
public:
    static const char* sGetClassName()
    {
        return "stMeshModel";
    }

    stMeshModel(w8_long polygons, w8_long vertices);
    virtual ~stMeshModel() override;       /* 0x00470ED0 */
    // The registry's clone callback assigns into an already registered instance;
    // the inherited support-class copy constructor cannot construct owned state.
    stMeshModel(const stMeshModel&) = delete;
    stMeshModel& operator=(const stMeshModel& other);
    virtual srClass* vInstance() override; /* 0x004748c0 */
    virtual int getBoundingSphere(srVector3T<float>& center,
                                  float& radius) override; /* 0x00471dd0 */
    virtual int getBoundingBox(srVector3T<float>& minimum,
                               srVector3T<float>& maximum) override; /* 0x00471d80 */
    virtual void getTriMesh(TriMesh& mesh) override;                 /* 0x004727e0 */
    /* Recomputes the union bounds over every model in the previous/next chain
       and pushes them to each member via srMeshModel::setBounds. */
    void CalculateLinkedBounds();                 /* 0x00471e10 */
    virtual const TriMesh& getTriMesh() override; /* 0x00472270 */
    virtual void renderTriMesh(class srGERD& renderer,
                               const TriMesh& mesh) override; /* 0x00470360 */
    /* Shared Wizardry-extended tri-mesh submit. `poly_normals` null skips
       the software backface pass; non-null callers supply polygon normals used
       to build the active-polygon scratch at 0x00659ce0. */
    void RenderTriMeshWithEquations(class srGERD& renderer, const TriMesh& mesh,
                                    const srVector3T<float>* poly_normals); /* 0x00470380 */

    int FindMappedIndex(short key); /* 0x004712D0 */
    void SetMappedVertex(short vertex, short key);
    void LinkTo(stMeshModel* other);      /* 0x00471D60 */
    short* GetVertex(unsigned int frame); /* 0x00471AA0 */
    int FindSkinTable(std::string_view name);
    int CreateSkinTable(std::string_view name, int base_table);
    srPtr<srTextureIFace>* GetTextureTable(int table); /* 0x00473720 */
    /* Active-polygon index list for a texture table; writes the count through
       `count_out`. Null draws all polygons, non-null with count zero draws none. */
    w8_ulong* GetActivePolygons(w8_long* count_out, int table, bool flag);
    void RemoveSkinTable(int index);
    void RemoveSkinTablesForCycle(std::string_view cycle_name);
    srVector3T<float>* GetVertexLocations(unsigned int frame, bool load, float interpolation);
    srVector3T<float>* GetVertexNormals(unsigned int frame, bool load); /* 0x00471CA0 */
    srVector3T<float>* GetPolygonNormals(unsigned int frame, bool load);
    void ClearVertexLights();
    void SetAmbientColor(const srVector3T<float>& color);
    unsigned char AllocateFrameBuffers(unsigned int uiFrame, unsigned char flags); /* 0x00471720 */
    srVector3T<float>* GetVertexLights(bool initialize, int table);                /* 0x00472100 */
    float* GetVertexSunlight(bool initialize);                                     /* 0x004721E0 */
    void NotifyLinkedModel(stMeshModel* previous_model);
    void InitializeVertexFrames(int frames); /* 0x00473B00 */
    int ReleaseDecompressedFrames();         /* 0x004739E0 */
    void FinalizeVertexFrame(int frame);
    /* Bounds `frame`'s vertex table into `minimum`/`maximum`, decompressing a
       scratch copy when the frame is not resident. */
    void GetFrameBounds(int frame, srVector3T<float>* minimum, srVector3T<float>* maximum);
    unsigned char DecompressFrame(int frame, unsigned char flags,
                                  srVector3T<float>* destination); /* 0x00471930 */
    void ComputeFrameNormals(int frame);                           /* 0x004729F0 */
    void ClearAutomapPolygonFilter();
    void ApplyAutomapPolygonFilter(std::span<const std::string_view> excluded_textures);

    stMeshModel* next;     /* 0x398 */
    stMeshModel* previous; /* 0x39c */
    unsigned int flags;
    srVector3T<float> ambient_color;
    int vertex_light_table;
    /* m_pVertLights: per-vertex static lighting, zero-filled on demand; table
       -1 selects vertex_light_table. */
    std::vector<srVector3T<float> > vertex_lights[2];
    /* Per-vertex sunlight intensity, filled with 1.0f on demand. */
    std::vector<float> vertex_sunlight;
    unsigned char duplicate_on_reuse;
    /* Set once both vertex lights and sunlight exist. */
    bool vertex_lighting_ready;
    unsigned char padding_3ce[2];
    struct Frame {
        // m_psCompVertexLoc / m_pbCompVertexNormal / m_pbCompPolyNormal:
        // three shorts per location and three signed-byte components per normal.
        std::vector<short> compressed_locations;
        std::vector<unsigned char> compressed_vertex_normals;
        std::vector<unsigned char> compressed_polygon_normals;
        // m_pVertexLoc / m_pVertexNormal / m_pPolyNormal. Disengaged means
        // absent, even on a zero-vertex model. Engaged storage counts toward
        // g_decompressed_mesh_bytes and is dropped only by explicit eviction.
        std::optional<std::vector<srVector3T<float>>> locations;
        std::optional<std::vector<srVector3T<float>>> vertex_normals;
        std::optional<std::vector<srVector3T<float>>> polygon_normals;
    };
    std::vector<Frame> frames; // Empty for a static base-class mesh.

    struct Skin {
        int id;
        std::string name;
        std::vector<srPtr<srTextureIFace>> textures;
        // An absent selection draws all polygons; an engaged empty selection
        // draws none. The checked bit distinguishes that from a pending scan.
        std::optional<std::vector<w8_ulong>> active_polygons;
        bool blanking_checked = false;
    };
    std::vector<Skin> skins;
    std::unordered_map<short, short> mapped_vertices; // key -> vertex
    w8_ulong last_decompress_release_tick;
    float vertex_compression_scale;
    // m_pLerpBuffer: stable interpolation scratch, outside the cache budget.
    std::vector<srVector3T<float>> lerp_buffer;
    std::optional<std::vector<w8_ulong>> automap_polygons;
    bool automap_filter_active;
};

W8_ABI_ASSERT(sizeof(stMeshModel) == 0x464, "stMeshModel_size_must_be_0x464");

/* Every mesh model whose frame storage has been initialized. */
extern std::vector<stMeshModel*> g_mesh_models; /* 0x00659CB8 */
/* Bytes currently held by decompressed per-frame float caches. */
extern int g_decompressed_mesh_bytes; /* 0x0065A0E8 */
/* Scratch active-polygon indices filled by software backface cull in
   RenderTriMeshWithEquations when an equation table is supplied. */
extern std::vector<w8_ulong> g_software_cull_active_polygons; /* 0x00659CE0 */

/* True when all three components of the vector are zero; the vertex-lighting
   code uses it to decide between a plain copy and a per-vertex offset. */
int __fastcall IsZeroVector(const srVector3T<float>* vector);
/* dest[i] = source[i] + offset for `count` vectors, or a plain copy when the
   offset is zero. */
void OffsetVertices(srVector3T<float>* destination, const srVector3T<float>* source,
                    const srVector3T<float>* offset, int count);
/* Release least-recently-used decompressed frame caches until `needed` bytes
   have been freed; 0 when the registry cannot supply them. A requesting model
   is retained because its renderer may already hold borrowed frame pointers. */
unsigned char ReclaimDecompressedBytes(unsigned int needed,
                                      const stMeshModel* retained_model = nullptr);

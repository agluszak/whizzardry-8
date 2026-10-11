#pragma once

#include "glm/ext/vector_float3.hpp"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <cstddef>

#include "surrender/srBoundingBox.h"
#include "surrender/srMeshModel.h"
#include "surrender/srTypeRegistry.h"
#include "compat/compiler.h"
#include "compat/native.h"
#include "glm/ext/matrix_float4x4.hpp"
#include "surrender/srMath.h"

class srTextureIFace;

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
    virtual ~stMeshModel() override; /* 0x00470ED0 */
    stMeshModel(const stMeshModel&) = delete;
    stMeshModel& operator=(const stMeshModel&) = delete;
    Bounds getBounds() override;
    /* Recomputes the union bounds over the head and its parts and pushes them
       to each mesh via srMeshModel::setBounds. */
    void CalculateLinkedBounds(); /* 0x00471e10 */
    void RevealVerticesNear(const glm::vec3& position, float range, const glm::mat4& world);
    virtual TriMesh getTriMesh() override; /* 0x00472270 */
    virtual void renderTriMesh(class srGERD& renderer,
                               const TriMesh& mesh) override; /* 0x00470360 */
    /* Shared Wizardry-extended tri-mesh submit. An empty `poly_normals` span skips
       the software backface pass; other callers supply counted polygon normals used
       to select the active polygons for this submission. */
    void RenderTriMeshWithEquations(class srGERD& renderer, const TriMesh& mesh,
                                    std::span<const glm::vec3> poly_normals); /* 0x00470380 */
    static std::vector<w8_ulong> facingPolygons(const TriMesh& mesh,
                                                std::span<const glm::vec3> normals,
                                                const glm::vec3& eye, bool positive_winding,
                                                float threshold);

    int FindMappedIndex(short key); /* 0x004712D0 */
    void SetMappedVertex(short vertex, short key);
    void AddPart(std::unique_ptr<stMeshModel> part);
    /* The mesh submitted after this one in its model, or null. */
    stMeshModel* Next() const;
    short* GetVertex(unsigned int frame); /* 0x00471AA0 */
    int FindSkinTable(std::string_view name);
    int CreateSkinTable(std::string_view name, int base_table);
    std::shared_ptr<srTextureIFace>* GetTextureTable(int table); /* 0x00473720 */
    /* No selection draws all polygons; an engaged empty span draws none.
       The view is borrowed until its skin or automap filter is changed. */
    PolygonSelection GetActivePolygons(int table, bool check_blanking);
    void RemoveSkinTable(int index);
    void RemoveSkinTablesForCycle(std::string_view cycle_name);
    // Borrowed frame caches; interpolated positions reuse the model's lerp buffer.
    struct FrameGeometry {
        std::span<glm::vec3> positions;
        std::span<glm::vec3> normals;
        std::span<const glm::vec3> polygon_normals;
    };
    FrameGeometry frameGeometry(unsigned int frame, float interpolation);
    glm::vec3* GetVertexLocations(unsigned int frame, bool load, float interpolation);
    glm::vec3* GetVertexNormals(unsigned int frame, bool load); /* 0x00471CA0 */
    glm::vec3* GetPolygonNormals(unsigned int frame, bool load);
    void ClearVertexLights();
    void SetAmbientColor(const glm::vec3& color);
    unsigned char AllocateFrameBuffers(unsigned int uiFrame, unsigned char flags); /* 0x00471720 */
    std::span<glm::vec3> GetVertexLights(bool initialize, int table);              /* 0x00472100 */
    std::span<float> GetVertexSunlight(bool initialize);                           /* 0x004721E0 */
    void InitializeVertexFrames(int frames);                                       /* 0x00473B00 */
    int ReleaseDecompressedFrames();                                               /* 0x004739E0 */
    void FinalizeVertexFrame(int frame);
    /* Return `frame`'s vertex bounds, decompressing a scratch copy when the frame
       is not resident. Invalid frames and empty meshes return zero bounds. */
    srBoundingBox GetFrameBounds(int frame);
    unsigned char DecompressFrame(int frame, unsigned char flags,
                                  std::span<glm::vec3> destination); /* 0x00471930 */
    void ComputeFrameNormals(int frame);                             /* 0x004729F0 */
    void ClearAutomapPolygonFilter();
    void ApplyAutomapPolygonFilter(std::span<const std::string_view> excluded_textures);

    /* A model is a head mesh plus one part per further render state. The head
       owns its parts; a part refers back to its head. */
    std::vector<std::unique_ptr<stMeshModel>> parts;
    stMeshModel* head = nullptr;

    /* The head (or a lone mesh) followed by its parts, in submission order. */
    class Meshes {
    public:
        explicit Meshes(stMeshModel* first) : first(first) {}
        class iterator {
        public:
            stMeshModel* operator*() const
            {
                return index == 0 ? first : first->parts[index - 1].get();
            }
            iterator& operator++()
            {
                ++index;
                return *this;
            }
            bool operator==(const iterator&) const = default;

            stMeshModel* first;
            std::size_t index;
        };
        iterator begin() const
        {
            return {first, 0};
        }
        iterator end() const
        {
            return {first, first ? first->parts.size() + 1 : 0};
        }

    private:
        stMeshModel* first;
    };
    struct Frame {
        // m_psCompVertexLoc / m_pbCompVertexNormal / m_pbCompPolyNormal:
        // three shorts per location and three signed-byte components per normal.
        std::vector<short> compressed_locations;
        std::vector<unsigned char> compressed_vertex_normals;
        std::vector<unsigned char> compressed_polygon_normals;
        // m_pVertexLoc / m_pVertexNormal / m_pPolyNormal. Disengaged means
        // absent, even on a zero-vertex model. Engaged storage counts toward
        // g_decompressed_mesh_bytes and is dropped only by explicit eviction.
        std::optional<std::vector<glm::vec3>> locations;
        std::optional<std::vector<glm::vec3>> vertex_normals;
        std::optional<std::vector<glm::vec3>> polygon_normals;
    };

    struct Skin {
        int id;
        std::string name;
        std::vector<std::shared_ptr<srTextureIFace>> textures;
        // An absent selection draws all polygons; an engaged empty selection
        // draws none. The checked bit distinguishes that from a pending scan.
        std::optional<std::vector<w8_ulong>> active_polygons;
        bool blanking_checked = false;
    };
    unsigned int flags;
    glm::vec3 ambient_color;
    int vertex_light_table;
    /* m_pVertLights: per-vertex static lighting, zero-filled on demand; table
       -1 selects vertex_light_table. */
    std::vector<glm::vec3> vertex_lights[2];
    /* Per-vertex sunlight intensity, filled with 1.0f on demand. */
    std::vector<float> vertex_sunlight;
    /* Set once both vertex lights and sunlight exist. */
    bool vertex_lighting_ready;
    unsigned char padding_3ce[2];
    std::vector<Frame> frames; // Empty for a static base-class mesh.
    std::vector<Skin> skins;
    std::unordered_map<short, short> mapped_vertices; // key -> vertex
    w8_ulong last_decompress_release_tick;
    float vertex_compression_scale;
    // m_pLerpBuffer: stable interpolation scratch, outside the cache budget.
    std::vector<glm::vec3> lerp_buffer;
    std::optional<std::vector<w8_ulong>> automap_polygons;
    bool automap_filter_active;

private:
    void updateBakedLighting();
    glm::vec3* frameTable(unsigned int frame, bool load, unsigned char kind);
};

/* Every mesh model whose frame storage has been initialized. */
extern std::vector<stMeshModel*> g_mesh_models; /* 0x00659CB8 */
/* Bytes currently held by decompressed per-frame float caches. */
extern int g_decompressed_mesh_bytes; /* 0x0065A0E8 */

/* True when all three components of the vector are zero. */
int __fastcall IsZeroVector(const srVector3T<float>* vector);
/* Release least-recently-used decompressed frame caches until `needed` bytes
   have been freed; 0 when the registry cannot supply them. A requesting model
   is retained because its renderer may already hold borrowed frame pointers. */
unsigned char ReclaimDecompressedBytes(unsigned int needed,
                                       const stMeshModel* retained_model = nullptr);

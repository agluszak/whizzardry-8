#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int3.hpp"
#include "wiz8/math.h"
#include "wiz8/render/draw_inputs.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/material.h"
#include "wiz8/render/mesh_resources.h"
#include "wiz8/render/texture.h"
#include "wiz8/spatial/bounding_box.h"
#include "wiz8/spatial/bounding_sphere.h"

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

class GpuRenderer;

class Mesh {

public:
    struct Bounds {
        BoundingBox box;
        BoundingSphere sphere;
    };
    std::string name{"anonymous"};
    const std::string& getName() const { return name; }
    void setName(std::string value) { name = std::move(value); }

public:
    using PolygonSelection = std::optional<std::span<const std::uint32_t>>;
    /* Front/back table indices, corresponding to CONTROL_FRONT/BACK. */
    enum e_side : int { SIDE_FRONT = 0, SIDE_BACK = 1 };
    /* Bit indices into dirty_flags. calculateBounds, calculatePolygonNormals,
       calculateVertexNormals and updateTriMesh clear bits 0, 1, 2 and 3. */
    enum e_flags {
        DIRTY_BOUNDS = 0,
        DIRTY_POLYGON_NORMALS = 1,
        DIRTY_VERTEX_NORMALS = 2,
        DIRTY_TRI_MESH = 3
    };
    /* Bit indices into render_control. renderTriMesh tests bits 0/1 as
       front/back sides. updateTriMesh skips auto box when bit 4 is set and
       auto sphere when bit 5 is set. */
    enum e_control {
        CONTROL_FRONT = 0,
        CONTROL_BACK = 1,
        CONTROL_REUSE_FRONT_MATERIAL = 2,
        CONTROL_NO_FRONT_CULL = 3,
        CONTROL_SKIP_AUTO_BOX = 4,
        CONTROL_SKIP_AUTO_SPHERE = 5,
        CONTROL_SORTED_RENDERING = 6
    };
    enum { MAX_PASSES = 4 };
    /* Borrowed triangle-mesh view: updateTriMesh fills it from the live tables. Keep the model
       and its table storage unchanged until submitMesh finishes submission. */
    struct TriMesh {
        std::int32_t vertex_count = 0;
        std::int32_t polygon_count = 0;
        std::int32_t pass_count = 0;
        std::uint32_t control_flags = 0;
        const glm::ivec3* poly_vertices = nullptr;
        const glm::vec4* poly_equations = nullptr;
        const glm::vec2* texcoords[4][2]{};
        const glm::vec3* positions = nullptr;
        const glm::vec3* normals = nullptr;
        const glm::vec3* dig[4]{};
        const glm::vec4* dcg[4]{};
        const glm::vec4* scg[4]{};
        std::shared_ptr<Material> materials[4][2];
        std::shared_ptr<Texture> textures[4][2];
        DrawShader shaders[4];
        MeshResources<Material>* vertex_materials[4][2]{};
        MeshResources<Texture>* poly_textures[4][2]{};
        DrawShader* poly_shaders[4]{};
        const glm::ivec3* poly_uv[4]{};
        glm::vec3 bounds_minimum{0.0f, 0.0f, 0.0f};
        glm::vec3 bounds_maximum{0.0f, 0.0f, 0.0f};
        glm::vec3 bounds_center{0.0f, 0.0f, 0.0f};
        float bounds_radius = 0.0f;
        float sort_bias = 0.0f;
        std::optional<float> diffuse_scale;
        std::optional<float> emissive_override;
        PolygonSelection active_polygons;
        std::span<const float> vertex_alpha;
    };

    Mesh(std::int32_t polygons = 0, std::int32_t vertices = 0);
    Mesh(const Mesh&) = delete;

    void reset(std::int32_t polygons, std::int32_t vertices);
    void scale(const glm::vec3& scale);
    void relocateVertices(const glm::vec3& offset);
    double getAverageRadius();
    double getMaxRadius();
    void scaleToAverageRadius(double radius);
    void scaleToMaxRadius(double radius);
    std::int32_t findClosestVertex(const glm::vec3& point);
    Mesh& operator=(const Mesh&) = delete;

    Bounds getBounds();
    bool isVisible(const DrawTransform& transform);
    void reindexPolygons(std::span<const std::uint32_t> indices);
    void reindexVertices(std::span<const std::uint32_t> indices);
    TriMesh getTriMesh();
    void renderTriMesh(class GpuRenderer& renderer, const TriMesh& mesh, MeshState state);
    MeshResources<Texture>* getPolygonTextureTable(std::int32_t pass, std::int32_t layer,
                                                   bool create);
    glm::ivec3* getPolyVertex();
    glm::ivec3* getPolyUVIndex(std::int32_t layer, int table);
    glm::vec2* getVertexTexCoords(std::int32_t vertex, std::int32_t layer, int table);
    MeshResources<Material>* getVertexMaterialTable(std::int32_t pass, e_side side, bool create);
    std::uint32_t* getVertexShadeIndex(int table);
    glm::vec3* getVertexNormal();
    glm::vec4* getPolyEq();
    glm::vec3* getVertexDIG(std::int32_t vertex, int table);
    glm::vec4* getVertexSCG(std::int32_t vertex, int table);
    glm::vec4* getVertexDCG(std::int32_t vertex, int table);
    Material* getMaterial(std::int32_t polygon, e_side side) const;
    Texture* getTexture(std::int32_t polygon, std::int32_t layer) const;
    void setMaterial(std::shared_ptr<Material> material, std::int32_t polygon, e_side side);
    void setTexture(std::shared_ptr<Texture> texture, std::int32_t polygon, std::int32_t layer);
    // RECOMP: ?setDirty@Mesh@@QAEXW4e_flags@1@@Z
    void setDirty(e_flags flag)
    {
        std::uint32_t mask = 1 << flag;
        if ((dirty_flags & mask) == 0) {
            dirty_flags |= mask | (1u << DIRTY_TRI_MESH);
        }
    }
    // RECOMP: ?clearDirty@Mesh@@QAEXW4e_flags@1@@Z
    void clearDirty(e_flags flag) { dirty_flags &= ~(1u << flag); }
    DrawShader* getPolyShader(std::int32_t polygon, int layer);
    DrawShader getShader(std::int32_t polygon) const;
    void setShader(DrawShader shader, std::int32_t pass);
    void setUVCount(std::int32_t count);
    std::int32_t getUVCount() const;
    void setActivePolygonCount(std::int32_t count);
    std::int32_t getActivePolygonCount();
    std::int32_t getPassCount() const;
    std::int32_t getPolygonCount() const;
    std::int32_t getVertexCount() const;
    void setPassCount(std::int32_t count);
    void setSortBias(float bias)
    {
        sort_bias = bias;
        setDirty(DIRTY_TRI_MESH);
    }
    float getSortBias() const;
    void disable(e_control control);
    void enable(e_control control)
    {
        render_control |= 1u << control;
        setDirty(DIRTY_TRI_MESH);
    }
    void setDirtyAll()
    {
        setDirty(DIRTY_BOUNDS);
        setDirty(DIRTY_POLYGON_NORMALS);
        setDirty(DIRTY_VERTEX_NORMALS);
        setDirty(DIRTY_TRI_MESH);
    }
    std::uint32_t* getActivePolygonTable(int table);
    glm::vec3* getVertexLoc();
    /* Fills the cached AABB/sphere (bounds_minimum..bounds_radius)
       from the supplied box and center/radius. */
    void setBounds(const glm::vec3& minimum, const glm::vec3& maximum, const glm::vec3& center,
                   float radius);

protected:
    void updateTriMesh();
    void calculateBounds();
    void calculatePolygonNormals();
    void calculateVertexNormals();

public:
    /* setMaterial indexes [pass][side]. */
    std::array<std::array<std::shared_ptr<Material>, 2>, MAX_PASSES> materials;
    std::array<std::array<std::shared_ptr<Texture>, 2>, MAX_PASSES> textures;
    std::array<DrawShader, MAX_PASSES> shaders;
    // Assignment tables are created on demand. Their resource pools own each
    // distinct material/texture once; vertices and polygons hold only indices.
    std::array<std::array<MeshResources<Texture>, 2>, MAX_PASSES> poly_textures;
    std::array<std::vector<DrawShader>, MAX_PASSES> poly_shaders;
    std::array<std::array<MeshResources<Material>, 2>, MAX_PASSES> vertex_materials;
    std::vector<glm::ivec3> poly_vertices;
    std::array<std::vector<glm::ivec3>, MAX_PASSES> poly_uv_indices;
    std::vector<glm::vec4> poly_equations;
    std::array<std::array<std::vector<glm::vec2>, 2>, MAX_PASSES> texcoords;
    std::array<std::vector<glm::vec3>, MAX_PASSES> dig;
    std::array<std::vector<glm::vec4>, MAX_PASSES> dcg;
    std::array<std::vector<glm::vec4>, MAX_PASSES> scg;
    std::vector<glm::vec3> vertex_locations;
    std::vector<glm::vec3> vertex_normals;
    std::vector<std::uint32_t> vertex_shade_indices;
    std::vector<std::uint32_t> active_polygons;
    std::int32_t active_polygon_count = 0;
    glm::vec3 bounds_minimum{0.0f, 0.0f, 0.0f};
    glm::vec3 bounds_maximum{0.0f, 0.0f, 0.0f};
    glm::vec3 bounds_center{0.0f, 0.0f, 0.0f};
    float bounds_radius = 0.0f;
    std::int32_t pass_count = 1;
    std::int32_t vertex_location_count = 0;
    std::int32_t polygon_count = 0;
    std::int32_t uv_count = 0;
    float sort_bias = 0.0f;
    TriMesh tri_mesh;
    std::uint32_t dirty_flags = 0;
    std::uint32_t render_control = 0;

public:
    ~Mesh();
    /* Recomputes the union bounds over the head and its parts and pushes them
       to each mesh via Mesh::setBounds. */
    void calculateCompoundBounds();                 /* 0x00471e10 */
    void RevealVerticesNear(const glm::vec3& position, float range, const glm::mat4& world);
    /* Shared Wizardry-extended tri-mesh submit. An empty `poly_normals` span skips
       the software backface pass; other callers supply counted polygon normals used
       to select the active polygons for this submission. */
    void RenderTriMeshWithEquations(class GpuRenderer& renderer, const TriMesh& mesh,
                                    std::span<const glm::vec3> poly_normals,
                                    const MeshState& state); /* 0x00470380 */
    static std::vector<std::uint32_t> facingPolygons(const TriMesh& mesh,
                                                     std::span<const glm::vec3> normals,
                                                     const glm::vec3& eye, bool positive_winding,
                                                     float threshold);

    int FindMappedIndex(short key) const; /* 0x004712D0 */
    void SetMappedVertex(int vertex, short key);
    void AddPart(std::unique_ptr<Mesh> part);
    /* The mesh submitted after this one in its model, or null. */
    short* GetVertex(unsigned int frame); /* 0x00471AA0 */
    int FindSkinTable(std::string_view name);
    int CreateSkinTable(std::string_view name, int base_table);
    MeshResources<Texture>* GetTextureTable(int table); /* 0x00473720 */
    /* No selection draws all polygons; an engaged empty span draws none.
       The view is borrowed until its skin or automap filter is changed. */
    PolygonSelection GetActivePolygons(int table, bool check_blanking);
    void RemoveSkinTable(int index);
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
    void SetAmbientColor(const glm::vec3& color, const glm::vec3& environment = glm::vec3(0),
                         float brightness = 1.0f);
    bool AllocateFrameBuffers(unsigned int uiFrame, unsigned char flags); /* 0x00471720 */
    std::span<glm::vec3> GetVertexLights(bool initialize, int table); /* 0x00472100 */
    std::span<float> GetVertexSunlight(bool initialize); /* 0x004721E0 */
    void InitializeVertexFrames(int frames); /* 0x00473B00 */
    int ReleaseDecompressedFrames(); /* 0x004739E0 */
    void FinalizeVertexFrame(int frame);
    /* Return `frame`'s vertex bounds, decompressing a scratch copy when the frame
       is not resident. Invalid frames and empty meshes return zero bounds. */
    BoundingBox GetFrameBounds(int frame);
    bool DecompressFrame(int frame, unsigned char flags,
                         std::span<glm::vec3> destination); /* 0x00471930 */
    void ComputeFrameNormals(int frame); /* 0x004729F0 */
    void ClearAutomapPolygonFilter();
    void ApplyAutomapPolygonFilter(std::span<const std::string_view> excluded_textures);

    // Further material submeshes are owned values; parts carry no parent links.
    std::vector<std::unique_ptr<Mesh>> parts;

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
        std::string name{"anonymous"};
        MeshResources<Texture> textures;
        // An absent selection draws all polygons; an engaged empty selection
        // draws none. The checked bit distinguishes that from a pending scan.
        std::optional<std::vector<std::uint32_t>> active_polygons;
        bool blanking_checked = false;
    };
    unsigned int flags;
    glm::vec3 ambient_color;
    glm::vec3 environment_offset{0};
    float brightness = 1.0f;
    int vertex_light_table;
    /* m_pVertLights: per-vertex static lighting, zero-filled on demand; table
       -1 selects vertex_light_table. */
    std::vector<glm::vec3> vertex_lights[2];
    /* Per-vertex sunlight intensity, filled with 1.0f on demand. */
    std::vector<float> vertex_sunlight;
    /* Set once both vertex lights and sunlight exist. */
    bool vertex_lighting_ready;
    std::vector<Frame> frames; // Empty for a static base-class mesh.
    std::list<Skin> skins;
    std::unordered_map<short, int> mapped_vertices; // wire marker key -> runtime vertex index
    std::uint64_t last_decompress_release_tick;
    float vertex_compression_scale;
    // m_pLerpBuffer: stable interpolation scratch, outside the cache budget.
    std::vector<glm::vec3> lerp_buffer;
    std::optional<std::vector<std::uint32_t>> automap_polygons;
    bool automap_filter_active;

private:
    void updateBakedLighting();
    glm::vec3* frameTable(unsigned int frame, bool load, unsigned char kind);
};

// Iterate concrete owned submeshes without a linked-list compatibility API.
inline auto meshParts(Mesh* first, bool include_parts = true)
{
    const auto count = first ? std::size_t{1} + (include_parts ? first->parts.size() : 0) : 0;
    return std::views::iota(std::size_t{0}, count) |
           std::views::transform([first](std::size_t index) {
               return index == 0 ? first : first->parts[index - 1].get();
           });
}
struct MappedVertex {
    Mesh* mesh;
    int index;
};
std::optional<MappedVertex> findMappedVertex(Mesh* model, int key);

/* Every mesh model whose frame storage has been initialized. */
extern std::vector<Mesh*> g_mesh_models; /* 0x00659CB8 */
/* Bytes currently held by decompressed per-frame float caches. */
extern int g_decompressed_mesh_bytes; /* 0x0065A0E8 */

/* True when all three components of the vector are zero. */
/* Release least-recently-used decompressed frame caches until `needed` bytes
   have been freed; 0 when the registry cannot supply them. A requesting model
   is retained because its renderer may already hold borrowed frame pointers. */
bool ReclaimDecompressedBytes(unsigned int needed, const Mesh* retained_model = nullptr);

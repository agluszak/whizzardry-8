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

#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int3.hpp"
#include "wiz8/math.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/material.h"
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

namespace render {
struct MeshAsset;
}

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
    /* Bit indices into dirty_flags. calculateBounds, calculatePolygonNormals
       and calculateVertexNormals clear bits 0, 1 and 2. */
    enum e_flags { DIRTY_BOUNDS = 0, DIRTY_POLYGON_NORMALS = 1, DIRTY_VERTEX_NORMALS = 2 };
    /* Bit indices into render_control. Rendering tests bits 0/1 as
       front/back sides. Bits 4 and 5 mark the box and sphere as authored
       rather than derived from the vertices (the sky sets both). */
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
    Mesh(std::int32_t polygons = 0, std::int32_t vertices = 0);
    Mesh(const Mesh&) = delete;

    void reset(std::int32_t polygons, std::int32_t vertices);
    void scale(const glm::vec3& scale);
    void relocateVertices(const glm::vec3& offset);
    Mesh& operator=(const Mesh&) = delete;

    Bounds getBounds();
    /* Bakes pending vertex lighting into the first colour table. Rendering calls
       this before it reads the mesh's tables. */
    void applyPendingLighting();
    std::shared_ptr<render::MeshAsset> renderAsset();
    std::vector<std::shared_ptr<Texture>>* getPolygonTextureTable(std::int32_t pass,
                                                                  std::int32_t layer, bool create);
    glm::ivec3* getPolyVertex();
    glm::ivec3* getPolyUVIndex(std::int32_t layer, int table);
    glm::vec2* getVertexTexCoords(std::int32_t vertex, std::int32_t layer, int table);
    std::vector<std::shared_ptr<Material>>* getVertexMaterialTable(std::int32_t pass, e_side side,
                                                                   bool create);
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
        invalidateRenderAsset();
        dirty_flags |= 1u << flag;
    }
    /* Drops the cached render asset after a change to the mesh's tables. */
    void invalidateRenderAsset() { render_asset_.reset(); }
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
        invalidateRenderAsset();
    }
    float getSortBias() const;
    void disable(e_control control);
    void enable(e_control control)
    {
        render_control |= 1u << control;
        invalidateRenderAsset();
    }
    void setDirtyAll()
    {
        setDirty(DIRTY_BOUNDS);
        setDirty(DIRTY_POLYGON_NORMALS);
        setDirty(DIRTY_VERTEX_NORMALS);
    }
    std::uint32_t* getActivePolygonTable(int table);
    glm::vec3* getVertexLoc();
    /* Fills the cached AABB/sphere (bounds_minimum..bounds_radius)
       from the supplied box and center/radius. */
    void setBounds(const glm::vec3& minimum, const glm::vec3& maximum, const glm::vec3& center,
                   float radius);

protected:
    void calculateBounds();
    void calculatePolygonNormals();
    void calculateVertexNormals();

public:
    /* setMaterial indexes [pass][side]. */
    std::array<std::array<std::shared_ptr<Material>, 2>, MAX_PASSES> materials;
    std::array<std::array<std::shared_ptr<Texture>, 2>, MAX_PASSES> textures;
    std::array<DrawShader, MAX_PASSES> shaders;
    // Per-polygon texture and per-vertex material tables are created on demand.
    std::array<std::array<std::vector<std::shared_ptr<Texture>>, 2>, MAX_PASSES> poly_textures;
    std::array<std::vector<DrawShader>, MAX_PASSES> poly_shaders;
    std::array<std::array<std::vector<std::shared_ptr<Material>>, 2>, MAX_PASSES> vertex_materials;
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
    std::uint32_t dirty_flags = 0;
    std::uint32_t render_control = 0;

public:
    /* Recomputes the union bounds over the head and its parts and pushes them
       to each mesh via Mesh::setBounds. */
    void calculateCompoundBounds();                 /* 0x00471e10 */

    int FindMappedIndex(short key) const; /* 0x004712D0 */
    void SetMappedVertex(int vertex, short key);
    void AddPart(std::unique_ptr<Mesh> part);
    /* The mesh submitted after this one in its model, or null. */
    short* GetVertex(unsigned int frame); /* 0x00471AA0 */
    int FindSkinTable(std::string_view name);
    int CreateSkinTable(std::string_view name, int base_table);
    bool replaceSkinTexture(int table, std::string_view old_name,
                            const std::shared_ptr<Texture>& replacement);
    std::vector<std::shared_ptr<Texture>>* GetTextureTable(int table); /* 0x00473720 */
    /* No selection draws all polygons; an engaged empty span draws none.
       The view is borrowed until its skin is changed. */
    PolygonSelection GetActivePolygons(int table, bool check_blanking);
    void RemoveSkinTable(int index);
    /* One animation frame decoded from its compressed tables. Positions
       interpolate toward the next frame; normals are this frame's own. */
    struct FrameGeometry {
        std::vector<glm::vec3> positions;
        std::vector<glm::vec3> normals;
        std::vector<glm::vec3> polygon_normals;
    };
    FrameGeometry frameGeometry(unsigned int frame, float interpolation) const;
    /* One decoded table (a W8MeshFrameData kind) of `frame`; empty when there is no such frame. */
    std::vector<glm::vec3> decodeFrame(unsigned int frame, unsigned char kind) const;
    /* One vertex's position in `frame`, for attachments that follow a source frame. */
    std::optional<glm::vec3> framePosition(unsigned int frame, int vertex) const;
    void SetAmbientColor(const glm::vec3& color, const glm::vec3& environment = glm::vec3(0),
                         float brightness = 1.0f);
    std::span<glm::vec3> GetVertexLights(bool initialize); /* 0x00472100 */
    std::span<float> GetVertexSunlight(bool initialize); /* 0x004721E0 */
    void InitializeVertexFrames(int frames); /* 0x00473B00 */
    void FinalizeVertexFrame(int frame);
    /* Return `frame`'s vertex bounds. Invalid frames and empty meshes return zero bounds. */
    BoundingBox GetFrameBounds(int frame) const;
    bool DecompressFrame(int frame, unsigned char flags,
                         std::span<glm::vec3> destination) const; /* 0x00471930 */
    void ComputeFrameNormals(int frame); /* 0x004729F0 */

    // Further material submeshes are owned values; parts carry no parent links.
    std::vector<std::unique_ptr<Mesh>> parts;

    struct Frame {
        // m_psCompVertexLoc / m_pbCompVertexNormal / m_pbCompPolyNormal:
        // three shorts per location and three signed-byte components per normal.
        std::vector<short> compressed_locations;
        std::vector<unsigned char> compressed_vertex_normals;
        std::vector<unsigned char> compressed_polygon_normals;
    };

    struct Skin {
        int id;
        std::string name{"anonymous"};
        std::vector<std::shared_ptr<Texture>> textures;
        // An absent selection draws all polygons; an engaged empty selection
        // draws none. The checked bit distinguishes that from a pending scan.
        std::optional<std::vector<std::uint32_t>> active_polygons;
        bool blanking_checked = false;
    };
    unsigned int flags;
    glm::vec3 ambient_color;
    glm::vec3 environment_offset{0};
    float brightness = 1.0f;
    /* m_pVertLights: per-vertex static lighting, zero-filled on demand. */
    std::vector<glm::vec3> vertex_lights;
    /* Per-vertex sunlight intensity, filled with 1.0f on demand. */
    std::vector<float> vertex_sunlight;
    /* Set once both vertex lights and sunlight exist. */
    bool vertex_lighting_ready;
    std::vector<Frame> frames; // Empty for a static base-class mesh.
    std::list<Skin> skins;
    std::unordered_map<short, int> mapped_vertices; // wire marker key -> runtime vertex index
    float vertex_compression_scale;
    // The base texture table's selection once its blank-texture scan has run.
    std::optional<std::vector<std::uint32_t>> base_polygons;
    bool base_blanking_checked = false;

private:
    std::shared_ptr<render::MeshAsset> render_asset_;
    void updateBakedLighting();
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

#pragma once

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int3.hpp"
#include "srFlags.h"
#include "srMaterial.h"
#include "srMath.h"
#include "srModel.h"
#include "srShader.h"
#include "srTexture.h"

// VTABLE: SURRENDER 0x10076D48 srMeshModel
class srMeshModel : public srModel {
public:
    using PolygonSelection = std::optional<std::span<const w8_ulong>>;
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
       and its table storage unchanged until renderTriMesh/FlushSlots finishes submission. */
    struct TriMesh {
        w8_long vertex_count = 0;
        w8_long polygon_count = 0;
        w8_long pass_count = 0;
        w8_ulong control_flags = 0;
        glm::ivec3* poly_vertices = nullptr;
        glm::vec4* poly_equations = nullptr;
        glm::vec2* texcoords[4][2]{};
        glm::vec3* positions = nullptr;
        glm::vec3* normals = nullptr;
        glm::vec3* dig[4]{};
        glm::vec4* dcg[4]{};
        glm::vec4* scg[4]{};
        std::shared_ptr<srMaterial> materials[4][2];
        std::shared_ptr<srTextureIFace> textures[4][2];
        srShader shaders[4];
        std::shared_ptr<srMaterialIFace>* vertex_materials[4][2]{};
        std::shared_ptr<srTextureIFace>* poly_textures[4][2]{};
        srShader* poly_shaders[4]{};
        glm::ivec3* poly_uv[4]{};
        glm::vec3 bounds_minimum{0.0f, 0.0f, 0.0f};
        glm::vec3 bounds_maximum{0.0f, 0.0f, 0.0f};
        glm::vec3 bounds_center{0.0f, 0.0f, 0.0f};
        float bounds_radius = 0.0f;
        float sort_bias = 0.0f;
        PolygonSelection active_polygons;
    };

    SR_DLL_IMPORT srMeshModel(w8_long polygons = 0, w8_long vertices = 0);
    srMeshModel(const srMeshModel&) = delete;

    SR_DLL_IMPORT void reset(w8_long polygons, w8_long vertices);
    SR_DLL_IMPORT void scale(const glm::vec3& scale);
    void applyMatrix(const glm::mat3& matrix);
    SR_DLL_IMPORT void relocateVertices(const glm::vec3& offset);
    void centerVertices();
    double getAverageRadius();
    double getMaxRadius();
    void scaleToAverageRadius(double radius);
    void scaleToMaxRadius(double radius);
    void flipFaces();
    w8_long findClosestVertex(const glm::vec3& point);
    srMeshModel& operator=(const srMeshModel&) = delete;

    Bounds getBounds() override;
    bool isVisible(srGERD& renderer);
    SR_DLL_IMPORT virtual void render(class srGERD& renderer) override;
    SR_DLL_IMPORT virtual void reindexPolygons(std::span<const w8_ulong> indices);
    SR_DLL_IMPORT virtual void reindexVertices(std::span<const w8_ulong> indices);
    SR_DLL_IMPORT virtual TriMesh getTriMesh();
    SR_DLL_IMPORT virtual void renderTriMesh(class srGERD& renderer, const TriMesh& mesh);
    SR_DLL_IMPORT std::shared_ptr<srTextureIFace>* getPolyTexture(w8_long polygon, w8_long layer,
                                                                  int table);
    SR_DLL_IMPORT glm::ivec3* getPolyVertex();
    SR_DLL_IMPORT glm::ivec3* getPolyUVIndex(w8_long layer, int table);
    SR_DLL_IMPORT glm::vec2* getVertexTexCoords(w8_long vertex, w8_long layer, int table);
    SR_DLL_IMPORT std::shared_ptr<srMaterialIFace>* getVertexMaterial(w8_long vertex, e_side side,
                                                                      int table);
    SR_DLL_IMPORT w8_ulong* getVertexShadeIndex(int table);
    SR_DLL_IMPORT glm::vec3* getVertexNormal();
    SR_DLL_IMPORT glm::vec4* getPolyEq();
    SR_DLL_IMPORT glm::vec3* getVertexDIG(w8_long vertex, int table);
    glm::vec4* getVertexSCG(w8_long vertex, int table);
    glm::vec4* getVertexDCG(w8_long vertex, int table);
    SR_DLL_IMPORT srMaterialIFace* getMaterial(w8_long polygon, e_side side) const;
    SR_DLL_IMPORT srTextureIFace* getTexture(w8_long polygon, w8_long layer) const;
    SR_DLL_IMPORT void setMaterial(std::shared_ptr<srMaterialIFace> material, w8_long polygon,
                                   e_side side);
    SR_DLL_IMPORT void setTexture(std::shared_ptr<srTextureIFace> texture, w8_long polygon,
                                  w8_long layer);
    // FUNCTION: SURRENDER 0x10041710 SYMBOL
    // RECOMP: ?setDirty@srMeshModel@@QAEXW4e_flags@1@@Z
    void setDirty(e_flags flag)
    {
        w8_ulong mask = 1 << flag;
        if ((dirty_flags.value & mask) == 0) {
            dirty_flags.set(flag, 1);
            dirty_flags.set(DIRTY_TRI_MESH, 1);
        }
    }
    // FUNCTION: SURRENDER 0x10041750 SYMBOL
    // RECOMP: ?clearDirty@srMeshModel@@QAEXW4e_flags@1@@Z
    void clearDirty(e_flags flag) { dirty_flags.set(flag, 0); }
    // FUNCTION: SURRENDER 0x10041770 SYMBOL
    // RECOMP: ?testDirty@srMeshModel@@QBEHW4e_flags@1@@Z
    int testDirty(e_flags flag) const { return (dirty_flags.value & (1 << flag)) != 0; }
    SR_DLL_IMPORT srShader* getPolyShader(w8_long polygon, int layer);
    SR_DLL_IMPORT srShader getShader(w8_long polygon) const;
    SR_DLL_IMPORT void setShader(srShader shader, w8_long pass);
    SR_DLL_IMPORT void setUVCount(w8_long count);
    w8_long getUVCount() const;
    SR_DLL_IMPORT void setActivePolygonCount(w8_long count);
    SR_DLL_IMPORT w8_long getActivePolygonCount();
    w8_long getPassCount() const;
    w8_long getPolygonCount() const;
    w8_long getVertexCount() const;
    void setPassCount(w8_long count);
    // FUNCTION: SURRENDER 0x10041790
    void setSortBias(float bias)
    {
        sort_bias = bias;
        setDirty(DIRTY_TRI_MESH);
    }
    float getSortBias() const;
    void disable(e_control control);
    // FUNCTION: SURRENDER 0x10041830
    void enable(e_control control)
    {
        render_control.set(control, 1);
        setDirty(DIRTY_TRI_MESH);
    }
    int isEnabled(e_control control) const;
    // FUNCTION: SURRENDER 0x10041660
    void setDirtyAll()
    {
        setDirty(DIRTY_BOUNDS);
        setDirty(DIRTY_POLYGON_NORMALS);
        setDirty(DIRTY_VERTEX_NORMALS);
        setDirty(DIRTY_TRI_MESH);
    }
    void setDirtyBounds();
    void setDirtyNormals();
    SR_DLL_IMPORT w8_ulong* getActivePolygonTable(int table);
    SR_DLL_IMPORT glm::vec3* getVertexLoc();
    /* Fills the cached AABB/sphere (bounds_minimum..bounds_radius)
       from the supplied box and center/radius. */
    SR_DLL_IMPORT void setBounds(const glm::vec3& minimum, const glm::vec3& maximum,
                                 const glm::vec3& center, float radius);

protected:
    SR_DLL_IMPORT virtual void updateTriMesh();
    SR_DLL_IMPORT virtual void calculateBounds();
    SR_DLL_IMPORT virtual void calculatePolygonNormals();
    SR_DLL_IMPORT virtual void calculateVertexNormals();

public:
    /* setMaterial indexes [pass][side]. */
    std::array<std::array<std::shared_ptr<srMaterialIFace>, 2>, MAX_PASSES> materials;
    std::array<std::array<std::shared_ptr<srTextureIFace>, 2>, MAX_PASSES> textures;
    std::array<srShader, MAX_PASSES> shaders;
    /* Empty vectors are absent tables, even if a previous resize retained capacity. Accessors
       allocate only on demand and return nullptr for absent tables, not vector::data(). */
    std::array<std::array<std::vector<std::shared_ptr<srTextureIFace>>, 2>, MAX_PASSES>
        poly_textures;
    std::array<std::vector<srShader>, MAX_PASSES> poly_shaders;
    std::array<std::array<std::vector<std::shared_ptr<srMaterialIFace>>, 2>, MAX_PASSES>
        vertex_materials;
    std::vector<glm::ivec3> poly_vertices;
    std::array<std::vector<glm::ivec3>, MAX_PASSES> poly_uv_indices;
    std::vector<glm::vec4> poly_equations;
    std::array<std::array<std::vector<glm::vec2>, 2>, MAX_PASSES> texcoords;
    std::array<std::vector<glm::vec3>, MAX_PASSES> dig;
    std::array<std::vector<glm::vec4>, MAX_PASSES> dcg;
    std::array<std::vector<glm::vec4>, MAX_PASSES> scg;
    std::vector<glm::vec3> vertex_locations;
    std::vector<glm::vec3> vertex_normals;
    std::vector<w8_ulong> vertex_shade_indices;
    std::vector<w8_ulong> active_polygons;
    w8_long active_polygon_count = 0;
    glm::vec3 bounds_minimum{0.0f, 0.0f, 0.0f};
    glm::vec3 bounds_maximum{0.0f, 0.0f, 0.0f};
    glm::vec3 bounds_center{0.0f, 0.0f, 0.0f};
    float bounds_radius = 0.0f;
    w8_long pass_count = 1;
    w8_long vertex_location_count = 0;
    w8_long polygon_count = 0;
    w8_long uv_count = 0;
    float sort_bias = 0.0f;
    TriMesh tri_mesh;
    srFlags<e_flags> dirty_flags;
    srFlags<e_control> render_control;
};

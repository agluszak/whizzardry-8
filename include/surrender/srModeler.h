#pragma once

#include <cstdint>
#include <array>
#include <span>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include "srMaterialIFace.h"
#include "srMath.h"
#include "srMeshModel.h"
#include "srShader.h"
#include "srTextureIFace.h"

// VTABLE: SURRENDER 0x10076C88 srModeler
class SR_DLL_IMPORT srModeler {
public:
    /* Axis selector indexing the position components. */
    enum e_axis { AXIS_X = 0, AXIS_Y = 1, AXIS_Z = 2 };

    struct MappingInfo {
        // FUNCTION: SURRENDER 0x10037BD0
        // RECOMP: ??0MappingInfo@srModeler@@QAE@W4e_axis@1@0MMMM@Z
        MappingInfo(e_axis axis_u = AXIS_X, e_axis axis_v = AXIS_Y, float u_scale = 1.0f,
                    float v_scale = 1.0f, float u_offset = 0.0f, float v_offset = 0.0f)
            : axis_u(axis_u), axis_v(axis_v), u_scale(u_scale), v_scale(v_scale),
              u_offset(u_offset), v_offset(v_offset)
        {
        }

        e_axis axis_u;
        e_axis axis_v;
        float u_scale;
        float v_scale;
        float u_offset;
        float v_offset;
    };

    /* A triangle vertex: position, the per-pass material pair (side-indexed), the three per-pass
       attribute vectors convert() feeds into the mesh's DCG/DIG/SCG streams, the eight UV slots
       (pass*2 + layer), and the per-pass weights convert() writes as the DCG alpha. */
    class Vertex {
    public:
        Vertex();
        void reset();
        void interpolate(const Vertex& first, const Vertex& second, float fraction);
        bool operator==(const Vertex& other) const = default;

        glm::vec3 position;
        std::uint32_t shade_index;
        std::shared_ptr<srMaterialIFace> materials[4][2];
        glm::vec3 dcg[4];
        glm::vec3 dig[4];
        glm::vec3 scg[4];
        glm::vec2 uv[8];
        float weights[4];
    };

    class Triangle {
    public:
        Triangle();
        void reset();
        void flipFacing();

        std::shared_ptr<srTextureIFace> textures[4][2];
        srShader shaders[4];
        Vertex vertices[3];
        std::uint32_t flags;
        std::uint32_t disabled;
    };

    class Polygon {
    public:
        Polygon(std::int32_t vertices);
        void reset();
        void reAllocate(std::int32_t vertices);

        std::shared_ptr<srTextureIFace> textures[4][2];
        srShader shaders[4];
        /* Engine Code\stCube.cpp assigns positions and UVs through this table
           after Polygon(4) allocates it. */
        std::vector<Vertex> vertices;
        std::int32_t vertex_count;
        std::uint32_t flags;
        std::uint32_t disabled;
    };

    srModeler();
    // FUNCTION: SURRENDER 0x1003BC70
    virtual ~srModeler() = default;

    void discard();

    std::uint32_t getTriangleCount() const;
    void setTriangleCount(std::uint32_t triangles);
    std::uint32_t addTriangle(const Triangle& triangle);
    std::optional<Triangle> getTriangle(std::uint32_t index) const;
    void setTriangle(std::uint32_t index, const Triangle& triangle);
    void setTriangleVertex(std::uint32_t triangle, std::uint32_t vertex, const Vertex& value);
    void flipTriangle(std::uint32_t triangle);
    void flipTriangles();
    void enableTriangle(std::uint32_t triangle);
    void disableTriangle(std::uint32_t triangle);
    std::uint32_t getEnabledTriangleCount();
    void removeDisabledTriangles();
    void disableDegenerateTriangles();
    void addFromModeler(srModeler& other);

    void scale(const glm::vec3& scale);
    void scale(std::uint32_t triangle, const glm::vec3& scale);
    void move(const glm::vec3& delta);
    void move(std::uint32_t triangle, const glm::vec3& delta);
    void rotate(const glm::mat3& matrix);
    void rotate(std::uint32_t triangle, const glm::mat3& matrix);
    double getMaxVertexDist();
    std::optional<std::pair<float, float>> getAxialBounds(e_axis axis) const;

    std::int32_t getPassCount() const;
    void setPassCount(std::int32_t passes);

    std::uint32_t getUniqueVertexCount();

    void createSphere(std::int32_t detail);
    void createTorus(std::int32_t major_segments, std::int32_t minor_segments, double radius);
    void createGrid(std::int32_t columns, std::int32_t rows);
    void tesselateEdges(double threshold);
    void tesselateEdges(std::uint32_t triangle, double threshold);
    /* Delegates to the file-local AutoSmoother worker in modeler.cpp: it
       builds per-shade-vertex triangle adjacency, tests each coincident pair's
       facing/materials, and floods group bits back into flags. */
    void autoSmooth(double threshold, std::int32_t smooth);

    void addPolygon(const Polygon& polygon);
    void planarMap(std::int32_t pass, std::int32_t layer, const MappingInfo& mapping);
    void planarMapAbsolute(std::int32_t pass, std::int32_t layer, const MappingInfo& mapping);
    void cylinderMap(std::int32_t pass, std::int32_t layer, const MappingInfo& mapping);
    void removeMapping(std::int32_t pass, std::int32_t layer);

    void setMaterial(std::shared_ptr<srMaterialIFace> material, std::int32_t pass, srMeshModel::e_side side);
    void setTexture(std::shared_ptr<srTextureIFace> texture, std::int32_t pass, std::int32_t layer);
    void setShader(srShader shader, std::int32_t pass);
    /* A nonzero remove_degenerate disables and drops triangles with
       coincident corners before building the mesh (the game passes 1). */
    void convert(srMeshModel& model, std::int32_t remove_degenerate);

    /* getUniqueVertexList's deduplication table: an entry pool, 1024
       position-hash buckets chaining entries, and the per-source-vertex
       result table written through during hashing. entries[i].shade_index is
       the representative index convert() copies into the mesh's vertex shade
       table. */
    struct VertexHash {
        struct Entry {
            std::uint32_t flags;
            /* Signed: the AutoSmoother worker's max scan in modeler.cpp
               compares it against its long vertex count with a signed JGE. */
            std::int32_t shade_index;
            Vertex* vertex;
            Entry* next;
            std::uint32_t index;
        };

        VertexHash(std::uint32_t vertex_count);

        static std::uint32_t hash(double x, double y, double z);

        std::vector<Entry> entries;
        std::array<Entry*, 1024> buckets{};
        std::vector<Entry*> table;
        std::uint32_t unique_count;
    };

private:
    std::unique_ptr<VertexHash> getUniqueVertexList();
    bool isClockwise(std::span<const glm::vec2> points);

    std::uint32_t triangle_count;
    std::vector<Triangle> triangles;
    std::int32_t pass_count;
};


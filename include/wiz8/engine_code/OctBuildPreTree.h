#ifndef WIZ8_ENGINE_CODE_OCT_BUILD_PRE_TREE_H
#define WIZ8_ENGINE_CODE_OCT_BUILD_PRE_TREE_H

#include <cstdint>
#include "wiz8/engine_code/mesh_links.h"

#include <optional>
#include <span>
#include <vector>
#include <cstddef>

#include <unordered_map>
#include <variant>
#include "glm/ext/vector_float3.hpp"
#include "surrender/srBoundingBox.h"

#include "wiz8/engine_code/OctBuildTree.h"
#include "wiz8/engine_code/ReadMesh.h"
#include "wiz8/geometry.h"
#include "compat/compiler.h"
#include "surrender/srMath.h"

struct W8LevelFileParticleSystem;
struct W8LevelFileProp;
class OctPreTree;
struct W8OctSpatialState;

extern const float g_float_three;

struct W8OctPreTreeVertex;
struct W8OctPreTreeGeometry;

struct W8OctRegionPolygon {
    enum { AXIS_MASK = 3u, MULTIPLE_REGIONS = 4u, CENTER_IN_REGION = 8u };
    /* & 3 selects the axis the plane test uses; bit2 marks a vertex shared
       across regions (cleared with bit3 after the duplicate pass). */
    std::uint32_t flags;
    /* 1-based ordinal into the geometry polygon array. */
    std::uint32_t ordinal;
    W8Plane plane; /* unit normal plus signed distance */
    srVector3T<float> position;
    /* Canonical material-group index assigned by the material sort. */
    std::uint32_t material;
    /* The per-polygon texture/material index CreateSubMeshes copies into
       OctMeshModel's m_plPolyTextures row. */
    std::uint32_t texture;
    /* The automesh kind (1..3) SplitMeshes partitions polygon lists on. */
    std::uint32_t kind;
    /* Set by the polygon builder when the face collapses. */
    std::uint8_t degenerate;
    bool visited;
    std::uint16_t region;
    /* Corner vertices of the shared build-vertex array; the material sort
       and SplitVertices repoint these at split copies. */
    W8OctPreTreeVertex* vertices[3];
    std::uint16_t face_count;
    std::uint8_t padding_42[2];
    /* Growable per-polygon run the cleanup releases. */
    std::int32_t* face_indices;
    /* Whole source mesh face copied by the polygon builder. */
    W8ReadMeshFace face;
    std::uint8_t padding_71[3];

    /* Tests the polygon's representative point against six frustum planes;
       inside means every plane distance is non-negative. */
    bool InsideFrustumPlanes(std::span<const W8Plane, 6> planes) const;
    bool containsPoint(const srBoundingBox& bounds) const;
};

extern std::int32_t g_build_node_instances;

struct OctRegionObjectLinks {
    std::unordered_multimap<std::uint16_t, std::int16_t> regions;
    std::vector<std::int16_t> always_visible;

    OctreeMeshLinks packedLinks(std::size_t region_count) const;
    void clear() { regions.clear(); always_visible.clear(); }
};

/* Retail assertion text gives the class and member names directly:
   "OctBuildPreTree::m_ppPolyList too long.", "m_pulRegPaths" and
   "m_psrvRegCenters" in Engine Code\OctBuildPreTree.cpp. */
struct OctBuildPreTree : W8OctBuildTree {
    OctBuildPreTree(float leaf_size, std::optional<srBoundingBox> bounds,
                    std::uint16_t item_limit, std::uint32_t path_capacity, std::int16_t extent_mode);
    OctPreTree* BuildOctPreTree();
    std::uint16_t BuildRegions();
    bool assignPointToVolumes(const glm::vec3& point, std::int16_t object, OctRegionObjectLinks& links);
    std::uint8_t BuildParticleRegions(std::span<const W8LevelFileParticleSystem> particles);
    std::uint8_t BuildGeometryRegions(std::span<const W8LevelFileProp> records, std::int32_t base_index,
                                       bool finalize);

    void AssignInitialRegions(const W8OctSpatialState* spatial);
    using RegionGeometry = std::variant<glm::vec3, srBoundingBox>;
    bool updateRegionForGeometry(const RegionGeometry& geometry, std::int16_t value);
    bool updateRegionMap(const W8OctSpatialState& state, const RegionGeometry& geometry, std::int16_t value);
    W8OctBuildNode* FindNode(std::uint32_t path);
    std::uint8_t MergeAdjacentRegion(W8OctBuildNode* node, std::uint32_t path);
    std::uint8_t MergeRegion(W8OctBuildNode* node, const std::int32_t* cell);
    void FinalizeRegionMapping();
    void AssignRegionFromSurfaces(const W8OctSpatialState* spatial);
    void ValidatePolygonRegions();
    void ValidateRegionBounds(std::span<const srBoundingBox> region_bounds);
    /* SortGeometry: welds duplicate vertices, drops degenerate polygons,
       repacks both arrays and re-inserts every polygon with mode 2. */
    std::uint8_t SortGeometry(W8OctPreTreeGeometry* geometry);
    /* Inserts one region polygon into the octree working state. */
    std::uint8_t InsertSurface(W8OctRegionPolygon* polygon, std::uint32_t mode);
    /* Recursive inserter for InsertSurface: subdivides to the leaf, collecting
       overlapping region ids on first touch and appending the polygon to the
       leaf's mode link list. */
    std::uint8_t InsertSurfaceRecursive(W8OctSpatialState* working, W8OctRegionPolygon* polygon,
                                         std::uint32_t mode);
    /* Fill the leaf's region-id list with every region volume overlapping
       `bounds`; grows a 50-entry scratch list on first use. */
    void FindLeafRegions(W8OctBuildNode* node, const W8BoundingBox* bounds);
    /* Loads the .rlk region file beside the level and folds its bounds into
       the build. */
    std::uint16_t LoadRegionFile(const char* stem);
    /* Walks the node tree remapping leaf region ids through region_remap. */
    void RemapNodeRegions(W8OctBuildNode* node, std::int32_t depth);
    /* Assigns a polygon's region from the region volume containing its
       representative point, falling back to the corner vertices' regions;
       marks multi-region polygons with flags bit2 and counts the assignment
       on the volume. */
    void AssignPolygonRegion(W8OctRegionPolygon* polygon);
    /* Region assignment pass run by SortGeometry: builds each vertex's
       polygon-reference run, assigns vertex and polygon regions against the
       region volumes, compacts dead regions and finishes in BuildRegions. */
    std::uint8_t AssignPolygonRegions(W8OctPreTreeGeometry* geometry);
    /* Resolves a shared polygon (flags bit2): histograms the neighboring
       polygons' regions collected through the corner vertices' face runs,
       picks the most frequent region the polygon actually touches, assigns it
       and counts it on the volume. Answers the polygon's region. */
    std::uint16_t SplitSharedPolygon(W8OctPreTreeGeometry* geometry, std::int32_t index);

    std::uint32_t path_capacity;
    std::uint16_t selected_depth;
    std::uint16_t padding_c2;
    std::uint32_t level_counts[10];
    std::vector<std::uint32_t> region_paths;
    bool mesh_linking;
    std::uint8_t padding_f5[3];
    std::vector<glm::vec3> region_centers;
    /* Region remap table indexed by old region id; RemapNodeRegions frees it
       after rewriting every leaf's region list through it. */
    std::uint16_t* region_remap;
    OctreeMeshLinks mesh_particle_links;
    OctreeMeshLinks mesh_prop_links;
    std::uint32_t particle_count;
    std::uint32_t prop_count;
    std::unordered_multimap<std::uint16_t, std::uint32_t> region_path_map;
    OctRegionObjectLinks particle_regions;
    OctRegionObjectLinks prop_regions;
    W8OctPreTreeGeometry* game_data;
    std::uint32_t unknown_138;
    std::uint32_t unknown_13c;
};

std::int32_t GetBuildNodeInstanceCount(void);

#endif

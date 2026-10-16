#ifndef WIZ8_ENGINE_CODE_OCT_BUILD_PRE_TREE_H
#define WIZ8_ENGINE_CODE_OCT_BUILD_PRE_TREE_H

#include <cstddef>
#include <optional>
#include <span>
#include <unordered_map>
#include <variant>
#include <vector>

#include "compat/compiler.h"
#include "glm/ext/vector_float3.hpp"
#include "surrender/srBoundingBox.h"
#include "wiz8/engine_code/OctBuildTree.h"
#include "wiz8/engine_code/ReadMesh.h"
#include "wiz8/engine_code/mesh_links.h"
#include "wiz8/geometry.h"

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
    w8_ulong flags;
    /* 1-based ordinal into the geometry polygon array. */
    w8_ulong ordinal;
    W8Plane plane; /* unit normal plus signed distance */
    glm::vec3 position{0};
    /* Canonical material-group index assigned by the material sort. */
    w8_ulong material;
    /* The per-polygon texture/material index CreateSubMeshes copies into
       OctMeshModel's m_plPolyTextures row. */
    w8_ulong texture;
    /* The automesh kind (1..3) SplitMeshes partitions polygon lists on. */
    w8_ulong kind;
    /* Set by the polygon builder when the face collapses. */
    unsigned char degenerate;
    bool visited;
    unsigned short region;
    /* Corner vertices of the shared build-vertex array; the material sort
       and SplitVertices repoint these at split copies. */
    W8OctPreTreeVertex* vertices[3];
    unsigned short face_count;
    unsigned char padding_42[2];
    /* Growable per-polygon run the cleanup releases. */
    int* face_indices;
    /* Whole source mesh face copied by the polygon builder. */
    W8ReadMeshFace face;
    unsigned char padding_71[3];

    /* Tests the polygon's representative point against six frustum planes;
       inside means every plane distance is non-negative. */
    bool InsideFrustumPlanes(std::span<const W8Plane, 6> planes) const;
    bool containsPoint(const srBoundingBox& bounds) const;
};

extern int g_build_node_instances;

struct OctRegionObjectLinks {
    std::unordered_multimap<unsigned short, short> regions;
    std::vector<short> always_visible;

    OctreeMeshLinks packedLinks(std::size_t region_count) const;
    void clear()
    {
        regions.clear();
        always_visible.clear();
    }
};

/* Retail assertion text gives the class and member names directly:
   "OctBuildPreTree::m_ppPolyList too long.", "m_pulRegPaths" and
   "m_psrvRegCenters" in Engine Code\OctBuildPreTree.cpp. */
struct OctBuildPreTree : W8OctBuildTree {
    std::vector<W8OctRegionVolume> m_region_volumes;
    OctBuildPreTree(float leaf_size, std::optional<srBoundingBox> bounds, unsigned short item_limit,
                    w8_ulong path_capacity, short extent_mode);
    OctPreTree* BuildOctPreTree();
    unsigned short BuildRegions();
    bool assignPointToVolumes(const glm::vec3& point, short object, OctRegionObjectLinks& links);
    unsigned char BuildParticleRegions(std::span<const W8LevelFileParticleSystem> particles);
    unsigned char BuildGeometryRegions(std::span<const W8LevelFileProp> records, int base_index,
                                       bool finalize);

    void AssignInitialRegions(const W8OctSpatialState* spatial);
    using RegionGeometry = std::variant<glm::vec3, srBoundingBox>;
    bool updateRegionForGeometry(const RegionGeometry& geometry, short value);
    bool updateRegionMap(const W8OctSpatialState& state, const RegionGeometry& geometry,
                         short value);
    W8OctBuildNode* FindNode(unsigned int path);
    unsigned char MergeAdjacentRegion(W8OctBuildNode* node, unsigned int path);
    unsigned char MergeRegion(W8OctBuildNode* node, const int* cell);
    void FinalizeRegionMapping();
    void AssignRegionFromSurfaces(const W8OctSpatialState* spatial);
    void ValidatePolygonRegions();
    void ValidateRegionBounds(std::span<const srBoundingBox> region_bounds);
    /* SortGeometry: welds duplicate vertices, drops degenerate polygons,
       repacks both arrays and re-inserts every polygon with mode 2. */
    unsigned char SortGeometry(W8OctPreTreeGeometry* geometry);
    /* Inserts one region polygon into the octree working state. */
    unsigned char InsertSurface(W8OctRegionPolygon* polygon, w8_ulong mode);
    /* Recursive inserter for InsertSurface: subdivides to the leaf, collecting
       overlapping region ids on first touch and appending the polygon to the
       leaf's mode link list. */
    unsigned char InsertSurfaceRecursive(W8OctSpatialState* working, W8OctRegionPolygon* polygon,
                                         w8_ulong mode);
    /* Fill the leaf's region-id list with every region volume overlapping
       `bounds`; grows a 50-entry scratch list on first use. */
    void FindLeafRegions(W8OctBuildNode* node, const srBoundingBox* bounds);
    /* Loads the .rlk region file beside the level and folds its bounds into
       the build. */
    unsigned short LoadRegionFile(const char* stem);
    /* Walks the node tree remapping leaf region ids through region_remap. */
    void RemapNodeRegions(W8OctBuildNode* node, int depth);
    /* Assigns a polygon's region from the region volume containing its
       representative point, falling back to the corner vertices' regions;
       marks multi-region polygons with flags bit2 and counts the assignment
       on the volume. */
    void AssignPolygonRegion(W8OctRegionPolygon* polygon);
    /* Region assignment pass run by SortGeometry: builds each vertex's
       polygon-reference run, assigns vertex and polygon regions against the
       region volumes, compacts dead regions and finishes in BuildRegions. */
    unsigned char AssignPolygonRegions(W8OctPreTreeGeometry* geometry);
    /* Resolves a shared polygon (flags bit2): histograms the neighboring
       polygons' regions collected through the corner vertices' face runs,
       picks the most frequent region the polygon actually touches, assigns it
       and counts it on the volume. Answers the polygon's region. */
    unsigned short SplitSharedPolygon(W8OctPreTreeGeometry* geometry, int index);

    w8_ulong path_capacity;
    unsigned short selected_depth;
    unsigned short padding_c2;
    w8_ulong level_counts[10];
    std::vector<w8_ulong> region_paths;
    bool mesh_linking;
    unsigned char padding_f5[3];
    std::vector<glm::vec3> region_centers;
    /* Region remap table indexed by old region id; RemapNodeRegions frees it
       after rewriting every leaf's region list through it. */
    unsigned short* region_remap;
    OctreeMeshLinks mesh_particle_links;
    OctreeMeshLinks mesh_prop_links;
    w8_ulong particle_count;
    w8_ulong prop_count;
    std::unordered_multimap<unsigned short, w8_ulong> region_path_map;
    OctRegionObjectLinks particle_regions;
    OctRegionObjectLinks prop_regions;
    W8OctPreTreeGeometry* game_data;
    w8_ulong unknown_138;
    w8_ulong unknown_13c;
};

int GetBuildNodeInstanceCount(void);

#endif

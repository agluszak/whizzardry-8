#pragma once

#include <cstdint>
#include "wiz8/engine_code/mesh_links.h"
#include <memory>

#include "wiz8/ground_probe.h"
#include "wiz8/octree_walk.h"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int3.hpp"
#include "surrender/srBoundingBox.h"

#include <vector>
#include <optional>
#include <unordered_map>

#include "surrender/srMath.h"
#include "wiz8/engine_code/OctPreTree.h"
#include "wiz8/geometry.h"
#include "compat/compiler.h"
#include "compat/native.h"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float4.hpp"

struct OctreeCellCoordinates {
    glm::ivec3 cell;
    glm::vec3 offset;
    bool inside;
};

struct NavigatorPlacement {
    glm::vec3 source;
    std::vector<glm::vec3> positions;
};

class GDProp;
class W8Prop;
class stParticle;
class stModelInstance;
namespace wiz8 { class File; }

typedef std::unordered_multimap<std::uint32_t, std::int32_t> W8OctreeIndex;

class W8PathingService;
class PrePathing;
class OctMeshModel;
struct CondPathNode;
struct W8LevelFile;
struct W8LevelFileAnimObj;
struct W8PreProp;
struct W8World;
struct W8GameData;
struct W8NavigatorMovementState;

// Native segment state; disk geometry retains its explicit record types.
struct W8OctreeTrace {
    glm::vec3 start{0};
    glm::vec3 end{0};
    glm::vec3 step{0};
    float hit_limit = 1.0e20f;
    float length = 0;
    std::uint16_t state = 0;
    std::uint16_t pad_2e = 0;

    W8OctreeTrace() = default;
    W8OctreeTrace(const glm::vec3& from, const glm::vec3& to);
    void reset(const glm::vec3& from, const glm::vec3& to);
};

/* One 0x10-byte entry of the .oct file's submesh table. Field +4 is the index
   into W8World::psrMeshes; the visibility update and UpdateMonsterLocation both
   resolve a region through it, and the update's reset pass writes the owning
   mesh back. Field +0 carries the render flags the visibility update sets and
   clears. */
struct W8OctSubmesh {
    std::uint32_t flags;
    std::int32_t mesh;
    /* The same-chain successor's record index (the build record's
       m_next_link). */
    std::uint32_t next_link;
    /* Reader max-scans this to size the g_octree_storage identity table. */
    std::uint32_t polygon_count;
};

/* The object classes the octree tracks. Kind 3 is not dynamic: it names the
   static GD-surface polygon streams stored inside each leaf record. The
   dynamic kinds pair an object id with a cell in W8OctreeObjectRegistry —
   collidable props (8), path waypoints (9), location objects such as
   monsters (12) and the navigator position record each location carries
   alongside its kind-12 entry (13). CollectObjectsInCell also treats any
   query kind above 12 as "both 12 and 13". */
enum W8OctreeObjectKind {
    W8_OCTREE_KIND_SURFACE = 3,
    W8_OCTREE_KIND_PROP = 8,
    W8_OCTREE_KIND_WAYPOINT = 9,
    W8_OCTREE_KIND_LOCATION = 0xc,
    W8_OCTREE_KIND_NAVIGATOR = 0xd,
};

/* Registry keys. An object key carries the kind in its high half and the
   object id in its low half; a cell key packs the three coordinates a byte
   apart with a +1 in the low byte so that cell (0,0,0) never collides with
   an empty slot. */
inline std::uint32_t PackOctreeObjectKey(std::uint32_t kind, std::uint32_t id)
{
    return kind * 0x10000 + (id & 0xffff);
}
inline std::uint32_t PackOctreeCellKey(std::int32_t x, std::int32_t y, std::int32_t z)
{
    return (x * 0x100 + y) * 0x100 + 1 + z;
}
inline std::uint32_t OctreeKeyKind(std::uint32_t key)
{
    return key >> 0x10;
}
inline std::uint32_t OctreeKeyId(std::uint32_t key)
{
    return key & 0xffff;
}
inline std::int32_t OctreeCellKeyX(std::int32_t key)
{
    return (key - 1) >> 0x10;
}
inline std::int32_t OctreeCellKeyY(std::int32_t key)
{
    return ((key - 1) >> 8) & 0xff;
}
inline std::int32_t OctreeCellKeyZ(std::int32_t key)
{
    return (key - 1) & 0xff;
}

class W8OctreeObjectRegistry {
public:
    /* Retail emits both lifecycle bodies out of line in the Octree TU: the
       constructor at 0x00436840 and the destructor at 0x00436B20. */
    W8OctreeObjectRegistry();
    ~W8OctreeObjectRegistry() = default;

    std::unique_ptr<W8OctreeIndex> by_cell;
    std::unique_ptr<W8OctreeIndex> by_object;

    /* Returns whether the pairing ended up recorded; every recovered caller
       discards it. */
    bool RegisterObjectCell(W8OctreeObjectKind kind, std::int32_t id, const glm::ivec3& point);
    bool MoveObjectToCell(W8OctreeObjectKind kind, std::int32_t id, const glm::ivec3& point);
    bool UnregisterObject(W8OctreeObjectKind kind, std::int32_t id);
};

/* The two compact records stored in an OCT file.  A branch is its two shorts
   followed by eight child indices; a leaf retains offsets into the region and
   two polygon-index streams.  The rest of the leaf is still positional. */
struct W8OctPreTreeBranch {
    std::uint16_t provisional_region;
    /* The region/owner id VerifyPolygonRegions and the runtime region reads
       compare against a polygon's region. */
    std::uint16_t region;
    std::uint32_t children[8];
};

struct W8OctPreTreeLeaf {
    /* Bit 0 is a runtime flag WriteOctFile clears before serialization. */
    std::uint32_t flags;
    std::uint32_t region_offset;
    std::uint32_t polygon_offset;
    std::uint32_t gd_polygon_offset;
    /* Stream offsets for object kinds 4-9: QueryKinds indexes the leaf as a
       flat ten-dword table (kind + leaf_index * 10). */
    std::uint32_t kind_offsets[6];
};

/* Native camera snapshot and visibility frustum; level-file planes retain
   their packed W8Plane representation independently. */
class srCamera;
struct W8OctreeView {
    glm::vec3 camera_location{0};
    glm::vec3 camera_dof{0};
    glm::vec3 rotation_column0{0};
    glm::vec3 rotation_column1{0};
    float horizontal_fov{};
    float vertical_fov{};
    float horizontal_fov_cosine{};
    float vertical_fov_cosine{};
    float far_clip{};
    glm::ivec3 visible_cells{0};
    std::array<glm::vec4, 6> frustum_planes{};

    void captureCamera(const srCamera& camera);
};

class W8Octree {
public:
    W8Octree(const char* path, W8GameData** game_data);
    /* Preprocessing calls retain this receiver even when a body uses only
       global build state. */
    char BuildPreprocessedFiles(const char* level_path);
    void OctBuildOptions(char* stem);
    void Reset();
    void Initialize(const W8OctFileHeader* header);
    ~W8Octree();
    void AddLoadedProp(W8Prop* prop);
    void AddLoadedParticle(stParticle* particle);
    /* Store the prop-sunlight bit array once it has been given a size. */
    void SetPropSunBits(std::vector<bool> bits);
    /* Whether prop `offset` past prop_sun_base has its sunlight bit; a
       negative offset checkpoints the shared index into the base. */
    bool TestPropSunBit(std::int32_t offset);
    void AddCollidablePropBounds(std::int32_t index, const srBoundingBox& bounds);
    void VisitPointCopy(std::uint16_t location_id, srVector3T<float>* position);
    /* Quantize relative to the spatial minimum, truncating toward zero. */
    glm::ivec3 cellAt(const glm::vec3& position) const; /* 0x00431440 */
    std::uint32_t FindLeaf(const glm::ivec3& point);
    void UpdateMonsterLocation(std::uint16_t location_id, const srVector3T<float>* position);
    /* Object-kind values the query machinery dispatches on: 3 = GD triangle,
       8 = collidable-prop polygon reference, 9 = path waypoint, 12 = location entry,
       13 = secondary location entry. Registry values pack kind into the high
       half and id+1 into the low half; cell keys pack x/y/z bytes with a +1
       sentinel. */
    void UnregisterLocationObjects(std::uint32_t location_id); /* 0x0042E650 */
    void UnregisterLocationObject(std::uint32_t location_id,
                                  W8OctreeObjectKind kind); /* 0x0042E880 */
    std::vector<std::uint32_t> queryAlongSegment(const glm::vec3& origin, const glm::vec3& delta,
                                           float extent, std::uint16_t kind);
    /* Kind-12 box query; `exclusion` 0 maps to none. */
    std::vector<std::uint32_t> locationsInBox(const srBoundingBox& bounds,
                                       std::uint16_t exclusion); /* 0x0042EF00 */
    /* AABB occupancy test: GD triangles, kind-12 location objects (with each
       monster's navigator radius) and collidable-prop surfaces. */
    bool TestBoxOccupied(const srVector3T<float>* lower,
                                  const srVector3T<float>* upper); /* 0x0042EF30 */
    /* Append the objects of `kind` inside one cell to the shared query
       buffer; the registry path deduplicates through m_visited_object_bits. */
    std::uint32_t CollectObjectsInCell(const glm::ivec3& cell,
                                      std::uint16_t kind); /* 0x0042F400 */
    /* Bounds-checked cell -> leaf index: the direct leaf grid when present,
       else a masked descent through the branch tree. */
    std::uint32_t LeafIndexForCell(const glm::ivec3& cell); /* 0x00433730 */
    /* Descend the branch tree while `masked_cell`'s leading mask word keeps
       the current level bit set, choosing the octant from the three
       coordinate words. */
    std::int32_t DescendByMask(const std::uint32_t* masked_cell); /* 0x004336D0 */
    std::uint32_t GetSectorForPosition(const srVector3T<float>* position);
    bool HasLineOfSight(const srVector3T<float>* from, srVector3T<float>* to, bool allow_fallback);
    /* Paths `from` toward `to`; on success `range` returns the path cost and
       `hops` the reached-waypoint count. */
    bool TestNoiseLineOfSight(const srVector3T<float>* from, srVector3T<float>* to,
                                       float* range, std::int32_t* hops); /* 0x00434220 */
    std::int16_t TraceLineOfSight(const srVector3T<float>* from, srVector3T<float>* to, bool trace_world,
                           std::int32_t from_location_id, std::int32_t to_location_id, bool visit_octree,
                           std::int32_t trace_mode);
    void AdjustPortalDestination(srVector3T<float>* destination, const srVector3T<float>* source);
    W8OctreeWalk cellWalk(const glm::vec3& from, const glm::vec3& to) const;
    /* Reset the shared buffer and collect one cell's leaf object ids. */
    std::int32_t ProbeCellForTrace(const glm::ivec3& cell); /* 0x00435B00 */
    /* Reset vs append variants collecting one cell's leaf polygon references
       (mapped through m_aulPolyLookup into (mesh<<16)|polygon keys). */
    std::int32_t ProbeCellForBlockers(const glm::ivec3& cell); /* 0x00435C40 */
    void AppendBlockerStream(const std::uint32_t* stream);
    std::int32_t ProbeCellForBlockersAppend(const glm::ivec3& cell); /* 0x00435DA0 */
    /* Test every buffered (mesh<<16)|polygon key's triangle against the trace
       ray; on a closer hit, end returns the contact point. */
    bool TestProbeResult(W8OctreeTrace* trace); /* 0x00435F00 */
    struct PropTraceHit {
        glm::vec3 position;
        std::int32_t prop_id;
    };
    std::optional<PropTraceHit> traceProps(const glm::vec3& from, const glm::vec3& to,
                                         std::int32_t skip_flag, std::int32_t gate);
    struct SphereTraceHit {
        glm::vec3 position;
        std::int32_t location_id;
    };
    /* Sphere contact and its location id; no value means the segment missed.
       Target selects the probe set; excluded_location skips its endpoint.
       The location winner and final collider offset retain the existing
       selection behavior. */
    std::optional<SphereTraceHit> traceSphereHit(const glm::vec3& from, const glm::vec3& to,
        std::int32_t excluded, std::int32_t target, std::int32_t excluded_location, std::uint32_t flags,
        bool noise_adjust); /* 0x004353F0 */
    /* Navigator placement query: retail callers pass modes such as 5, 10, 20
       and 30; retail callers load the octree into ecx and the body returns
       with ret 0x28, so this is an ordinary member. `source`'s y may be
       snapped to the settled ground height. */
    /* Sibling scatter query to navigatorPositions: walks fixed lateral
       columns over ten rings instead of the mode-driven cell grid, and can
       flatten every accepted position back to the source height. */
    std::vector<glm::vec3> scatterPositions(const glm::vec3& position, float yaw, float spacing,
                                           std::uint32_t count, bool proximity_check, bool flatten_y);
    NavigatorPlacement navigatorPositions(const glm::vec3& source, float yaw, float radius,
                                           std::uint32_t count, bool first_only, bool settle_any_height,
                                           bool avoid_triggers, std::int32_t mode, bool require_waypoint_span);
    std::uint32_t AdvanceNavigator(W8NavigatorMovementState* movement, float radius,
                                  float separation);
    std::uint8_t PrepareNavigatorTarget(W8NavigatorMovementState* movement, float radius,
                                         float separation);
    std::uint8_t PrepareNavigatorPatrol(W8NavigatorMovementState* movement, float minimum,
                                         float maximum);
    bool LinkNavigatorTarget(W8NavigatorMovementState* movement,
                                      const srVector3T<float>* target, float separation);
    void GetPathSurfaceNormal(const srVector3T<float>* position, srVector3T<float>* normal);
    GroundProbe settleToGround(const glm::vec3& position, bool test_props, float limit); /* 0x00433820 */
    /* Clamp `position` to the clipped ceiling, probe the ground one
       world-scale unit lower and keep the settled height on a hit. */
    /* Returns the ground-hit flag in AL; pathing callers test it. */
    bool SnapToGround(srVector3T<float>* position, char mode); /* 0x00431D20 */
    void RegisterNavigatorCell(std::int32_t id, const srVector3T<float>* position);
    /* Owned query results remain valid across subsequent octree queries.
       IDs retain cell traversal order and the existing 10,000-entry limit. */
    std::vector<std::uint32_t> queryObjects(const srBoundingBox& bounds, std::uint16_t kind,
                                      std::int32_t excluded); /* 0x0042F280 */
    std::vector<std::uint32_t> nearbyLocations(const glm::vec3& position, float spacing);
    /* Refresh the pathing service's debug preview from the world cursor,
       falling back to the camera eye when the cursor is unset. */
    void UpdatePathVisualization(); /* 0x00434170 */
    void UpdateCameraVisibility();
    void UpdateVisibility();
    bool UpdateWorldTrace();
    /* Store `path` with its extension stripped into m_owned_0c0; the sibling
       data files are then derived from the stem. */
    bool SetPathStem(const char* path); /* 0x0042CF90 */
    bool SavePoints(char* path);
    bool LoadPointFiles(const char* level_name);
    bool ReadRegionLinkFile(const char* level_name);
    /* Region containing `point` (1-based index into the volume array), else
       the packed auto-region key (level<<24)|cell. */
    std::uint32_t RegionKeyForPoint(const srVector3T<float>* point); /* 0x00432720 */
    /* Link `region_key` to every mesh marked in m_projected_regions,
       inserting unseen (region, mesh) pairs into m_pRegionLinks. */
    void RecordRegionMeshLinks(std::uint32_t region_key); /* 0x004327F0 */
    /* Sweep the camera around `point`, mark cells whose meshes still draw
       into m_projected_regions and return `point`'s region key (zero when
       nothing linked). */
    std::uint32_t SampleRegionLinks(const srVector3T<float>* point, bool descend, bool clear_sets,
                                   std::uint32_t region_key); /* 0x00431E10 */
    /* Rebuild the region-link table by camera-sampling the region grid;
       `rebuild_all` sweeps every cell and discards the saved point list, a
       zero value samples a sparse checkerboard plus the stored points. */
    void BuildRegionLinks(bool rebuild_all); /* 0x004314C0 */
    bool SaveRegionLinks(char* path);
    bool ValidateRegionMeshLinks();
    /* Collect the live (mesh<<16)|polygon keys whose triangles overlap the
       (x±radius, y-height..y, z±radius) box, sorted in an owned result. */
    std::vector<std::uint32_t> polygonsNearPoint(const glm::vec3& center, float radius,
                                             float height); /* 0x00438780 */
    std::int32_t CountBadRegionMeshLinks(W8OctSpatialState* spatial);
    void ToggleUpdateSuspension(W8World* world);
    void MarkMeshLinksVisible(std::uint32_t mesh);
    /* Collect the model instances whose bounds reach within `radius` of
       `point`, through the region cells and volumes the sphere touches. */
    std::vector<stModelInstance*> modelsNearPoint(const glm::vec3& center, float radius,
                                                  std::uint32_t flags, bool only_accumulated); /* 0x0042F9A0 */
    OctreeCellCoordinates cellCoordinates(const glm::vec3& location) const;
    OctreeCellCoordinates projectRegionsAt(const glm::vec3& location);
    void CollectVisibleCells();
    /* Project every candidate region volume against the frustum planes and
       mark the visible ones in the current region set. */
    void MarkVisibleRegions(); /* 0x004301C0 */
    /* Build the six frustum planes from the camera basis and far clip. */
    void BuildFrustumPlanes(); /* 0x004302E0 */
    std::int16_t ProjectLinkedRegionsForLocation(srVector3T<float>* location,
                                          std::uint16_t* region_list); /* 0x00431050 */

    bool HasLoadError() const
    {
        return (m_spatial.flags & 0x80000000) != 0;
    }
    std::uint32_t GetMeshCount() const
    {
        return m_spatial.submesh_count;
    }

public:
    /* Same proven 0x9c value used by the level build tree.  Construction and
       teardown operate on the offset-zero subobject, but current evidence does
       not distinguish first-member composition from inheritance, so the
       declaration makes the narrower composition claim. */
    W8OctSpatialState m_spatial;
    W8OctPreTreeBranch* m_branches;
    W8OctPreTreeLeaf* m_leaves;
    srVector3T<std::uint32_t> m_leaf_grid_dimensions;
    std::uint32_t* m_leaf_lookup;
    std::uint32_t m_branch_count;
    std::uint32_t m_leaf_count;
    W8OctreeObjectRegistry* object_registry;
    char* m_owned_0c0;
    bool m_fAccumulating;
    std::uint8_t m_padding_0c5[3];
    std::uint32_t m_vertex_count;
    std::uint32_t m_leaf_polygon_stream_len;
    std::uint32_t* m_polygon_index_stream;
    /* ReadOctFile's allocation assertion calls this the "Poly Lookup table":
       polygon index to (kind<<16)|id object key. */
    std::uint32_t* m_aulPolyLookup;
    W8OctSubmesh* m_pSubmeshes;
    /* Retail m_pAlphaBits; empty without a mesh count. Link tables retain
       unsigned-short disk records but use native owned containers. */
    std::vector<bool> m_alpha_bits;
    OctreeMeshLinks m_particle_links;
    OctreeMeshLinks m_prop_links;
    std::uint32_t m_ulNumParticles;
    std::vector<bool> m_linked_particles;
    std::vector<bool> m_visible_particles;
    std::vector<bool> m_linked_props;
    std::vector<bool> m_visible_props;
    W8Prop** m_papProps;
    stParticle** m_papParticles;
    std::uint16_t m_usNumPropsLoaded;
    std::uint16_t m_usNumParticlesLoaded;
    /* Registered prop id the last prop trace or test_props snap hit; -1 when
       the ground resolve touched only static geometry. */
    std::int32_t current_prop;
    std::uint32_t m_gd_surface_stream_len;
    std::uint32_t m_trigger_count;
    std::uint32_t* m_gd_surface_index_stream;
    /* Trigger list: serialized as 2-byte elements (ReadOctFile allocates
       count * 2 + 4) even though WriteOctFile emits them four bytes wide. */
    std::uint16_t* m_trigger_indices;
    std::uint32_t m_trace_skip_flag;
    std::uint32_t m_region_list_len;
    std::uint32_t m_unknown_13c;
    /* The leaf-level mask: VerifyPolygonRegions rebuilds it as
       (1 << m_leaf_level) - 1 and the packed-cell writers emit it as the top
       byte of each (mask<<24 | x<<16 | y<<8 | z) key. */
    std::uint32_t m_region_mask;
    std::uint32_t m_depth_mask;
    std::uint16_t* m_region_index_stream;
    std::uint8_t* m_pfRegsVisited;
    std::unordered_multimap<std::uint32_t, std::uint16_t>* m_pRegionLinks;
    std::vector<bool> m_projected_regions;
    std::vector<bool> m_current_regions;
    std::vector<bool> m_previous_regions;
    bool m_reset_visibility;
    bool m_region_links_ready;
    bool m_projected_regions_valid;
    /* Set after a location matches a region, cleared before visibility
       collection. No consumer or Reset initialization is established. */
    bool m_location_region_matched;
    bool m_region_links_dirty;
    bool m_points_dirty;
    std::uint8_t m_padding_16e[2];
    std::uint32_t m_point_count;
    srVector3T<float>* m_sample_points;
    /* Region-link sample cell size read from .oct offset 0xac; the link
       builder strides the x/z grid by it (times three for a sparse pass). */
    float m_region_cell;
    float m_path_clearance;
    W8PathingService* pathing;
    std::int32_t prop_sun_base; /* 0x184: this octree's base index into the shared
                              prop-sunlight bit stream */
    std::uint32_t m_ulNumProps;
    /* Retail m_pPropSunBits, named by ReadOctFile's assertion at 0x0042CAA4. */
    std::vector<bool> m_sunlit_props; /* empty when the level has none */
    std::vector<bool> m_visited_polygon_bits;
    std::vector<bool> m_visited_object_bits;
    std::vector<bool> m_accumulated_regions;
    std::uint32_t m_root_mesh_count;
    std::uint32_t m_kind1_submesh_count;
    std::uint32_t m_alpha_polygon_count;
    std::uint32_t m_meshCount;
    std::uint32_t m_gd_result_count;
    std::uint32_t* m_aulGDObjs; /* 0x1bc */
    W8OctreeView view;
    std::uint32_t m_unknown_27c[6];
    bool m_visibility_suspended;
    std::uint8_t m_padding_295;
    /* The build's directional-sun count: the driver stores the light total
       and CreateSubMeshes emits it as each OctMeshModel's version and
       sizes the per-sun vertex light arrays from it. */
    std::uint16_t m_sun_count;
    std::uint8_t m_padding_298;
    std::uint8_t m_unknown_299;
    std::uint8_t m_padding_29a[2];
};

class OctPreTree : public W8Octree {
public:
    OctPreTree();
    ~OctPreTree();

    /* Automesh index -> packed cell (z | y<<8 | x<<16 | mask<<24) map the
       verify passes walk to bound-check each automesh's vertices. */
    std::unordered_multimap<std::uint16_t, std::uint32_t> automesh_cells;
    PrePathing* pre_pathing;
    /* The path-node scratch block BuildPathLists/PathNodeObstructed fill:
       created-node count, then the runs of registered prop ids the node
       rests on (supports) and that overlap its clearance box (blocks).
       m_lNumSupports/m_lNumBlocks are the original names from the
       PathNodeObstructed assertion text.  Both appends write the slot before
       the `> 29` assertion runs, so a 30th entry overruns the array exactly
       like retail. */
    std::int32_t path_node_count;
    std::int32_t m_lNumSupports;
    std::int32_t m_lNumBlocks;
    std::int32_t m_lSupports[30];
    std::int32_t m_lBlocks[30];
    std::uint32_t polygon_cursor;
    W8OctPreTreeGeometry* game_data;
    std::uint32_t unknown_3a8;
    std::uint32_t unknown_3ac;
    std::uint32_t deepest_link_list;
    /* Path-node grid pitch: BuildPathLists sets it to m_region_cell * 2. */
    float path_node_extent;
    /* The registered prop objects the path-bounds test collides against;
       0x0046BEC0 reads m_surface_count and the collidable flag on each. */
    std::vector<GDProp*>* props;

    /* Walks the `from`-`to` segment through the leaf grid, collecting each
       visited leaf's region-polygon ids and plane/slab-testing them. Answers
       whether the segment is unobstructed; the light-visibility callers
       accumulate its result. */
    bool SegmentClear(const glm::vec3& from, const glm::vec3& to);
    /* Resets the collected-id run and appends every not-yet-seen polygon id
       the leaf under `cell` lists. */
    void CollectLeafPolygons(const glm::ivec3& cell);
    /* Tests the collected region polygons' planes against the trace segment;
       a polygon blocks only when the ray pierces at least 5.0f past its plane
       (or starts within 1.0f in front) and the contact lands inside it. */
    bool TestCollectedPolygons(W8OctreeTrace* trace);
    /* Serializes the finished octree to NewLevel.oct. */
    std::uint8_t WriteOctFile(W8OctPreTreeGeometry* geometry, W8GameData* game_data);
    /* Partitions the geometry into submesh records, emits the OctMeshModel
       array and fills m_pSubmeshes/m_aulPolyLookup. */
    std::vector<OctMeshModel> CreateSubMeshes(W8OctPreTreeGeometry* geometry);
    std::uint32_t SplitMeshes(W8OctPreTreeGeometry* geometry, W8OctSubmeshBuild* records);
    std::uint32_t AllocateSubMesh(W8OctSubmeshBuild* records);
    std::uint32_t SplitUVMaps(W8OctSubmeshBuild* record, W8OctPreTreeGeometry* geometry);
    void VerifyPolygonRegions();
    void VerifyAutoMeshes(W8OctPreTreeGeometry* geometry, W8OctSubmeshBuild* records);
    bool BuildPathLists(W8GameData* game_data, W8LevelFile& level,
                                 std::uint32_t min_component_percent);
    char PathNodeObstructed(const srVector3T<float>* node_position);
    bool InsertConditionalNodes(std::unordered_multimap<std::uint32_t, CondPathNode*>& nodes,
                                         std::uint32_t cell, std::uint32_t node, std::span<const W8PreProp> preprops);
    /* Tests the bounds box against static surfaces and registered props;
       0 clear, 1 blocked, 3 clear but prop ids were recorded in m_lBlocks. */
    char TestPathPropBounds(const srBoundingBox& bounds);
    std::int32_t CreatePathProps(const W8LevelFile& level, W8PreProp** preprops);
    char PropFramesDiffer(const W8LevelFileAnimObj& anim, std::uint16_t first, std::uint16_t last);
};

extern W8Octree* g_octree;
extern OctPreTree* g_oct_pre_tree;

/* The SGP /NOOCT startup switch sets this flag; an Octree-unit body reads it. */
void NoOct(void);
extern bool g_octree_disabled;

bool __stdcall IsNavigatorAtTarget(W8NavigatorMovementState* movement);

extern std::uint32_t* g_octree_storage_;
extern std::uint32_t* g_octree_state;
extern stModelInstance* g_octree_trace_node;
extern const float g_octree_cell_scale;
extern std::uint32_t g_octree_bytes_read;
extern std::int32_t g_prop_sun_index;
extern bool g_octree_update_suspended;
extern bool g_octree_trace_enabled;
/* Renderer switches the region-link build toggles: suppress baked vertex
   lighting, force front-face culling and strip textures while sampling. */
extern bool g_render_unlit;
extern bool g_render_cull_front;
/* Inverted-depth / alternate pass-compare mode; renderTriMesh forces GEQUAL
   and the frame clear path uses a zero clear-depth while this is set. */
extern bool g_inverted_depth_render;
extern bool g_render_untextured;

std::int32_t CheckLevelAssetSet(const char* level_path);

std::uint32_t* __fastcall PackColourToLong(std::uint32_t* color, double alpha, double red,
                                           double green, double blue);

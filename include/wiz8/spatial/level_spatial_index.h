#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int3.hpp"
#include "glm/ext/vector_uint3.hpp"
#include "wiz8/audio_resources.h"
#include "wiz8/camera_motion.h"
#include "wiz8/geometry.h"
#include "wiz8/ground_probe.h"
#include "wiz8/octree_walk.h"
#include "wiz8/platform/IntervalGate.h"
#include "wiz8/retail_opaque_records.h"
#include "wiz8/spatial/bounding_box.h"
#include "wiz8/spatial/mesh_links.h"
#include "wiz8/spatial/navigation.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/spatial/octree_geometry.h"
#include "wiz8/world/environment_state.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/party_motion.h"

namespace wiz8 {
class File;
}

/* Current-level movement state. Contact is refreshed by walkable-surface
   response; movement can stop at its distance limit or through a reset.
   Fast movement selects the running stamina/noise path. */

extern bool g_shared_timers_paused;

enum W8MoveTimerAction {
    W8_MOVE_TIMER_SUSPEND = 1,
    W8_MOVE_TIMER_FRAME_DELTA = 2,
    W8_MOVE_TIMER_RESUME = 4,
    W8_MOVE_TIMER_RESUME_MAIN_GAME = 8
};
float MoveTimer(W8MoveTimerAction action);

void ResetLevelMovement(float movement_limit, bool reset, bool fast_move); /* 0x0041EEE0 */

extern float g_navigator_gravity;
extern float g_default_motion_limit;
extern float g_default_momentum_scale;

/* The current level-data record at 0x00652DAC — a 0xf4-byte allocation whose
   first 0xac bytes mix flag, counter and camera-vector state. GameData.cpp
   establishes the flag word and optional vector; Camera.cpp establishes the
   two derived forward vectors and the scale used to produce the second.
   Retail allocates 0xf4 and its constructor builds a W8IntervalGate at +0xc4;
   the record's destructor at 0x00421890 exists only to tear that member down.
   The bare `add ecx,0xc4` body is member teardown, not an adjustor thunk. */

class SurfaceIndex;
class Trigger;
class Camera3D;
class MeshInstance;
class LevelSpatialIndex;
struct World;

/* One environment record: seventeen dwords mixing counters and factors. */

/* Switch-interface record, 0xc bytes in the processed level data: the count
   and first index of the interface's state records in LevelSpatialIndex::m_pStates.
   Field +0 is the interface's own id. Retail's SetInterfaceState assertion
   names the first-index member iStates. */
struct W8GDInterface {
    int id;
    int state_count;
    int iStates;
};

/* One switch-interface state, 0xc bytes: the group id the selected state is
   compared against, and the count/first-index slice of conditional-poly
   surface indexes in LevelSpatialIndex::m_piCondPolys. */
struct W8GDInterfaceState {
    int group;
    int poly_count;
    int poly_first;
};

/* The processed game-data record the octree and world own. The proven prefix
   above is consumed by GameData.cpp and OctBuildTree.cpp; the constructor
   below establishes the rest: two bit sets, paired count/allocation blocks,
   a counted pointer array, the environment count/array pair, and a trailing
   flag. Only straightforward storage is claimed past the prefix. */
struct CollisionRuntime {
    int integrated_triggers = 0;
    std::vector<bool> pending_triggers;
    std::vector<bool> active_triggers;
    std::vector<std::optional<bool>> conditional_disabled;
    std::unordered_map<unsigned int, float> crossing_margins;
};

struct OctreeCellCoordinates {
    glm::ivec3 cell;
    glm::vec3 offset;
    bool inside;
};

struct NavigatorPlacement {
    glm::vec3 source;
    std::vector<glm::vec3> positions;
};

class Prop;
class Prop;
class ParticleSystem;
class MeshInstance;
namespace wiz8 {
class File;
}

typedef std::unordered_multimap<unsigned int, int> W8OctreeIndex;

class CompiledMesh;
struct CondPathNode;
struct World;
class LevelSpatialIndex;
struct NavigatorMovement;

// Native segment state; disk geometry retains its explicit record types.
// The keys one trace or query collects, and the sets that keep each key to a
// single entry. Each query owns one, so no query sees another's leftovers.
struct OctreeScratch {
    OctreeScratch(std::size_t polygon_bits, std::size_t object_bits)
        : visited_polygons(polygon_bits, false), visited_objects(object_bits, false)
    {
    }
    std::vector<std::uint32_t> keys;
    std::vector<bool> visited_polygons;
    std::vector<bool> visited_objects;
};

// The collidable props a prop trace tests.
struct PropTraceFilter {
    // Skip prop surfaces flagged out of filtered traces.
    bool skip_filtered = false;
    // Skip props that are not attached to the level.
    bool attached_only = false;
};

/* Location spheres a sight trace also tests, through traceSphereHit: `source`
   is skipped (-1 skips none), and `target` selects the probe set and is
   skipped as well. */
struct SightLocations {
    int source;
    int target;
    bool noise_adjust = false;
};

struct SightQuery {
    // Collidable props block the line as well as level geometry.
    bool props = true;
    // Absent when location spheres do not block the line.
    std::optional<SightLocations> locations;
};

enum class SightBlocker : unsigned char {
    // Level geometry or a collidable prop.
    Obstacle,
    // A location sphere: a monster or the party.
    Location,
};

struct SightHit {
    glm::vec3 point;
    SightBlocker blocker;
};

/* One 0x10-byte entry of the .oct file's submesh table. Field +4 is the index
   into World::psrMeshes; the visibility update and UpdateMonsterLocation both
   resolve a region through it, and the update's reset pass writes the owning
   mesh back. Field +0 carries the render flags the visibility update sets and
   clears. */
struct W8OctSubmesh {
    std::uint32_t flags;
    int mesh;
    /* The same-chain successor's record index (the build record's
       m_next_link). */
    std::uint32_t next_link;
    /* Reader max-scans this to size the g_octree_storage identity table. */
    unsigned int polygon_count;
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
inline unsigned int PackOctreeObjectKey(unsigned int kind, unsigned int id)
{
    return kind * 0x10000 + (id & 0xffff);
}
inline unsigned int PackOctreeCellKey(int x, int y, int z)
{
    const auto ux = static_cast<unsigned int>(x);
    const auto uy = static_cast<unsigned int>(y);
    const auto uz = static_cast<unsigned int>(z);
    return (ux * 0x100 + uy) * 0x100 + 1 + uz;
}
inline unsigned int OctreeKeyKind(unsigned int key)
{
    return key >> 0x10;
}
inline unsigned int OctreeKeyId(unsigned int key)
{
    return key & 0xffff;
}
inline int OctreeCellKeyZ(int key)
{
    return (key - 1) & 0xff;
}

class W8OctreeObjectRegistry {
public:
    W8OctreeIndex by_cell;
    W8OctreeIndex by_object;

    /* Returns whether the pairing ended up recorded; every recovered caller
       discards it. */
    bool RegisterObjectCell(W8OctreeObjectKind kind, int id, const glm::ivec3& point);
    bool MoveObjectToCell(W8OctreeObjectKind kind, int id, const glm::ivec3& point);
    bool UnregisterObject(W8OctreeObjectKind kind, int id);
};

/* The two compact records stored in an OCT file.  A branch is its two shorts
   followed by eight child indices; a leaf retains offsets into the region and
   two polygon-index streams.  The rest of the leaf is still positional. */
struct W8OctPreTreeBranch {
    unsigned short provisional_region;
    /* The region/owner id VerifyPolygonRegions and the runtime region reads
       compare against a polygon's region. */
    unsigned short region;
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

/* Native camera snapshot and visibility frustum. */
class Camera3D;
struct W8OctreeView {
    glm::vec3 camera_location{0};
    glm::vec3 camera_dof{0};
    glm::vec3 rotation_column0{0};
    glm::vec3 rotation_column1{0};
    float horizontal_fov{};
    float vertical_fov{};
    float horizontal_fov_cosine{};
    float far_clip{};
    std::array<glm::vec4, 6> frustum_planes{};

    // The view from `location` facing along `rotation`, through `camera`'s field of view.
    void captureView(const glm::vec3& location, const glm::mat3& rotation, const Camera3D& camera);
};

// Runtime registration and visibility are separate from immutable level geometry.
struct DynamicSpatialRegistry {
    std::unordered_map<std::size_t, std::size_t> region_meshes;
    std::unique_ptr<W8OctreeObjectRegistry> cells{};
    float maximum_navigation_radius = 500.0f;
    bool collecting_links{};
    int sun_index = 0;
    std::vector<bool> linked_particles{};
    std::vector<bool> visible_particles{};
    std::vector<bool> linked_props{};
    std::vector<bool> visible_props{};
    std::vector<Prop*> props{};
    std::vector<ParticleSystem*> particles{};
    std::vector<bool> projected_regions{};
    std::vector<bool> current_regions{};
    std::vector<bool> previous_regions{};
    bool reset_visibility{};
    bool projected_regions_valid{};
    std::vector<bool> accumulated_regions{};
    bool visibility_suspended{};
    W8OctreeView view;
};

class LevelSpatialIndex {
public:
    void ReadProcessedCollision(wiz8::File* handle); /* 0x00449240 */
    /* Reads one WGD vertex/polygon list: counts, the scaled vertex bank with
       unscaled bounds tracking, the face records, and — for the non-primary
       pass — the interface name and conditional-face records. */
    bool ReadWGDList(wiz8::File& file, int poly_type);
    /* Selects a switch interface's state: the matching state group's
       conditional polygons clear surface flag 0x10 while every other state's
       polygons set it. */
    signed char SetInterfaceState(int interface_id, int state); /* 0x0041C680 */
    /* Apply one surface the mover's trace crossed, nearest first: an
       environment boundary (flag 0x1000) swaps the active environment record,
       carrying the live blend fields across; a trigger surface runs its
       trigger in the crossing direction unless the bit sets already hold
       it. */
    void ProcessCrossedSurface(W8GDSurface* surface,
                               ActiveEnvironment& environment); /* 0x0041C770 */
    /* 0x0041AB40: advance camera under environment-load motion, tracing
       props/octree/geometry and applying crossed surfaces. */
    unsigned char AdvanceEnvironmentMotion(PartyMotion& motion, ActiveEnvironment& environment);
    /* 0x0041BD60: push the motion delta away from nearby active monsters. */
    unsigned char ProbeMonstersAlongMotion(glm::vec3* direction, glm::vec3* position, int mode);
    /* 0x0041B770: probe active collidable props along the motion segment;
       returns the nearest hit surface and may adjust `direction`. */
    W8GDSurface* ProbePropsAlongMotion(glm::vec3* direction, glm::vec3* position,
                                       glm::vec3* scratch, float* nearest_distance,
                                       W8Plane& hit_plane, PartyMotion& motion, bool& motion_active,
                                       std::map<const W8GDSurface*, W8GDSurface>& contacts);

    std::unique_ptr<SurfaceIndex> geometry_index;
    glm::vec3 minimum;
    glm::vec3 maximum;
    // Contiguous owned banks; counts are derived from container sizes.
    std::vector<glm::vec3> m_pVertices;
    int trigger_surface_base;
    int trigger_surface_count;
    int integrated_surface_count;
    std::vector<W8GDSurface> m_pSurfaces;
    int TrigSurfacesCount() const noexcept { return int(m_pTrigSurfaces.size()); }
    int TrigVerticesCount() const noexcept { return int(m_pTrigVertices.size()); }
    int m_iNumTriggers;
    std::vector<W8GDSurface> m_pTrigSurfaces;
    std::vector<glm::vec3> m_pTrigVertices;
    std::vector<Trigger*> m_ppTriggers;
    CollisionRuntime state;
    std::vector<W8GDInterface> m_pInterfaces;
    std::vector<W8GDInterfaceState> m_pStates;
    std::vector<int> m_piCondPolys;
    int VerticesCount() const noexcept { return int(m_pVertices.size()); }
    int SurfacesCount() const noexcept { return int(m_pSurfaces.size()); }
    int InterfacesCount() const noexcept { return int(m_pInterfaces.size()); }
    int StatesCount() const noexcept { return int(m_pStates.size()); }
    int EnvironsCount() const noexcept { return int(environments.size()); }
    std::vector<std::string> interface_names;
    std::vector<LevelEnvironment> environments;
    /* The party's movement tracker for this level. The first camera motion
       update restarts the shared timers so loading time is not integrated. */
    static bool LiftsCrossings(const PartyMotion& motion)
    {
        return (motion.flags & W8_LEVEL_FLAG_NO_SOUND_ENVIRONMENT) != 0;
    }
    /* Descriptive name for appending a plane's classified trigger triangle. */
    void AddTriggerTriangle(int trigger_index, int vertex_0, int vertex_1, int vertex_2);
    void IntegrateTriggers();
    void AddTriggerPlane(const glm::vec3* vertices, Trigger* trigger);
    struct PropTraceHit {
        glm::vec3 position;
        int prop_id;
    };
    /* Ray-test the listed collidable props in order (0x0041C0D0, 0x0041C140).
       Each hit shortens the segment, so the last prop that hits is the
       nearest; its contact is in world space. */
    std::optional<PropTraceHit> tracePropList(std::span<const std::uint32_t> ids,
                                              const PlaneSegment& segment,
                                              PropTraceFilter filter) const;
    std::optional<SurfaceRayHit>
    traceGeometry(const PlaneSegment& segment, SurfaceTraceFilter filter,
                  std::span<const unsigned int> surface_ids = {}) const;
    void initializeCollision();
    void adoptCollisionData(LevelSpatialIndex&& source);
    bool collision_available = false;
    bool compiled_available = true;

    DynamicSpatialRegistry registry;
    World* const owner_world;
    explicit LevelSpatialIndex(std::string_view path = {}, World* owner = nullptr);
    /* Preprocessing calls retain this receiver even when a body uses only
       global build state. */
    void Reset();
    void Initialize(const W8OctFileHeader* header);
    ~LevelSpatialIndex();
    LevelSpatialIndex(const LevelSpatialIndex&) = delete;
    LevelSpatialIndex& operator=(const LevelSpatialIndex&) = delete;
    void AddLoadedProp(Prop* prop);
    void AddLoadedParticles(std::span<ParticleSystem* const> particles);
    /* Whether prop `offset` past prop_sun_base has its sunlight bit; a
       negative offset checkpoints the shared index into the base. */
    bool TestPropSunBit(int offset);
    void AddCollidablePropBounds(int index, const BoundingBox& bounds);
    /* Quantize relative to the spatial minimum, truncating toward zero. */
    glm::ivec3 cellAt(const glm::vec3& position) const; /* 0x00431440 */
    std::uint32_t FindLeaf(const glm::ivec3& point) const;
    void UpdateMonsterLocation(unsigned short location_id, const glm::vec3& position);
    /* Object-kind values the query machinery dispatches on: 3 = GD triangle,
       8 = collidable-prop polygon reference, 9 = path waypoint, 12 = location entry,
       13 = secondary location entry. Registry values pack kind into the high
       half and id+1 into the low half; cell keys pack x/y/z bytes with a +1
       sentinel. */
    void UnregisterLocationObjects(unsigned int location_id); /* 0x0042E650 */
    void UnregisterLocationObject(unsigned int location_id,
                                  W8OctreeObjectKind kind); /* 0x0042E880 */
    std::vector<std::uint32_t> queryAlongSegment(const glm::vec3& origin, const glm::vec3& delta,
                                                 float extent, unsigned short kind) const;
    /* Kind-12 box query; `exclusion` 0 maps to none. */
    std::vector<std::uint32_t> locationsInBox(const BoundingBox& bounds,
                                              unsigned short exclusion) const; /* 0x0042EF00 */
    /* AABB occupancy test: GD triangles, kind-12 location objects (with each
       monster's navigator radius) and collidable-prop surfaces. */
    bool isBoxOccupied(const BoundingBox& box) const; /* 0x0042EF30 */
    /* Append the objects of `kind` inside one cell to the shared query
       buffer; the registry path deduplicates through the scratch object set. */
    unsigned int CollectObjectsInCell(const glm::ivec3& cell, unsigned short kind,
                                      OctreeScratch& scratch) const; /* 0x0042F400 */
    /* Bounds-checked cell -> leaf index: the direct leaf grid when present,
       else a masked descent through the branch tree. */
    unsigned int LeafIndexForCell(const glm::ivec3& cell) const; /* 0x00433730 */
    /* Descend the branch tree while `masked_cell`'s leading mask word keeps
       the current level bit set, choosing the octant from the three
       coordinate words. */
    int DescendByMask(const unsigned int* masked_cell) const; /* 0x004336D0 */
    unsigned int GetSectorForPosition(const glm::vec3* position) const;
    /* Whether no visible mesh polygon or attached collidable prop blocks the
       segment. */
    bool hasLineOfSight(const glm::vec3& from, const glm::vec3& to) const;
    // Whether any of `bounds`' visibility samples is in sight of `from`.
    bool hasLineOfSight(const glm::vec3& from, const BoundingBox& bounds) const;
    /* The walkable route a noise travels from `from` to `to` within `budget`
       when it is positive; no value when none exists. */
    std::optional<MeasuredPath> measureNoisePath(const glm::vec3& from, const glm::vec3& to,
                                                 float budget) const; /* 0x00434220 */
    /* The first thing blocking the segment: the nearer of the first level
       surface along the walked cells and any collidable prop, else the
       query's location spheres. No value means the line is clear. */
    std::optional<SightHit> traceSight(const glm::vec3& from, const glm::vec3& to,
                                       SightQuery query = {}) const;
    W8OctreeWalk cellWalk(const glm::vec3& from, const glm::vec3& to) const;
    /* Reset the shared buffer and collect one cell's leaf object ids. */
    std::size_t ProbeCellForTrace(const glm::ivec3& cell,
                                  OctreeScratch& scratch) const; /* 0x00435B00 */
    /* Collects one cell's leaf polygon references (mapped through
       m_aulPolyLookup into (mesh<<16)|polygon keys). */
    std::size_t ProbeCellForBlockers(const glm::ivec3& cell,
                                     OctreeScratch& scratch) const; /* 0x00435C40 */
    void AppendBlockerStream(const std::uint32_t* stream, OctreeScratch& scratch) const;
    /* Whether the segment meets any visible (mesh<<16)|polygon key's
       triangle. */
    bool meshPolygonsBlock(const PlaneSegment& segment,
                           std::span<const std::uint32_t> keys) const; /* 0x00435F00 */
    // The nearest collidable prop along the segment's walked cells.
    std::optional<PropTraceHit> traceProps(const glm::vec3& from, const glm::vec3& to,
                                           PropTraceFilter filter) const;
    struct SphereTraceHit {
        glm::vec3 position;
        int location_id;
    };
    /* Sphere contact and its location id; no value means the segment missed.
       Target selects the probe set; excluded_location skips its endpoint.
       The location winner and final collider offset retain the existing
       selection behavior. */
    std::optional<SphereTraceHit> traceSphereHit(const glm::vec3& from, const glm::vec3& to,
                                                 int excluded, int target, int excluded_location,
                                                 unsigned int flags,
                                                 bool noise_adjust) const; /* 0x004353F0 */
    /* Navigator placement query: retail callers pass modes such as 5, 10, 20
       and 30; retail callers load the octree into ecx and the body returns
       with ret 0x28, so this is an ordinary member. `source`'s y may be
       snapped to the settled ground height. */
    /* Sibling scatter query to navigatorPositions: walks fixed lateral
       columns over ten rings instead of the mode-driven cell grid, and can
       flatten every accepted position back to the source height. */
    std::vector<glm::vec3> scatterPositions(const glm::vec3& position, float yaw, float spacing,
                                            unsigned int count, bool proximity_check,
                                            bool flatten_y, std::optional<BoundingSphere> player);
    NavigatorPlacement navigatorPositions(const glm::vec3& source, float yaw, float radius,
                                          unsigned int count, bool first_only,
                                          bool settle_any_height, bool avoid_triggers, int mode,
                                          bool require_waypoint_span,
                                          std::optional<BoundingSphere> player);
    unsigned int AdvanceNavigator(NavigatorMovement* movement, float radius, float separation);
    unsigned char PrepareNavigatorTarget(NavigatorMovement* movement, float radius,
                                         float separation);
    unsigned char PrepareNavigatorPatrol(NavigatorMovement* movement, float minimum, float maximum);
    bool LinkNavigatorTarget(NavigatorMovement* movement, const glm::vec3* target,
                             float separation);
    /* The path surface's normal at `position`; straight up without a
       pathing service. */
    glm::vec3 pathSurfaceNormal(const glm::vec3& position) const;
    /* Settle a point onto the walkable surface or, when `test_props`, the
       collidable prop below it, starting `limit` above it. */
    GroundProbe settleToGround(const glm::vec3& position, bool test_props,
                               float limit) const; /* 0x00433820 */
    void RegisterNavigatorCell(int id, const glm::vec3& position);
    /* Owned query results remain valid across subsequent octree queries.
       IDs retain cell traversal order and the existing 10,000-entry limit. */
    std::vector<std::uint32_t> queryObjects(const BoundingBox& bounds, unsigned short kind,
                                            int excluded) const; /* 0x0042F280 */
    std::vector<std::uint32_t> nearbyLocations(const glm::vec3& position, float spacing);
    /* Refresh the pathing service's debug preview from the world cursor,
       falling back to the camera eye when the cursor is unset. */
    void UpdateCameraVisibility();
    MeshInstance* meshForRegion(std::size_t region) const;
    void UpdateVisibility();
    /* Store `path` with its extension stripped into m_path_stem; the sibling
       data files are then derived from the stem. */
    bool SetPathStem(std::string_view path);
    bool ReadRegionLinkFile(std::string_view level_name);
    /* Collect the live (mesh<<16)|polygon keys whose triangles overlap the
       (x±radius, y-height..y, z±radius) box, sorted in an owned result. */
    std::vector<std::uint32_t> polygonsNearPoint(const glm::vec3& center, float radius,
                                                 float height) const; /* 0x00438780 */
    void MarkMeshLinksVisible(unsigned int mesh);
    /* Collect the model instances whose bounds reach within `radius` of
       `point`, through the region cells and volumes the sphere touches. */
    std::vector<MeshInstance*> modelsNearPoint(const glm::vec3& center,
                                               float radius) const; /* 0x0042F9A0 */
    OctreeCellCoordinates cellCoordinates(const glm::vec3& location) const;
    OctreeCellCoordinates projectRegionsAt(const glm::vec3& location);
    void CollectVisibleCells();
    /* Project every candidate region volume against the frustum planes and
       mark the visible ones in the current region set. */
    void MarkVisibleRegions(); /* 0x004301C0 */
    /* Build the six frustum planes from the camera basis and far clip. */
    void BuildFrustumPlanes(); /* 0x004302E0 */
    short ProjectLinkedRegionsForLocation(glm::vec3* location,
                                          unsigned short* region_list); /* 0x00431050 */

    bool HasLoadError() const { return (m_spatial.flags & 0x80000000) != 0; }
    std::uint32_t GetMeshCount() const { return m_spatial.submesh_count; }

public:
    // Shared spatial queries borrow region volumes owned by this tree.
    W8OctSpatialState m_spatial;
    std::vector<W8OctRegionVolume> m_region_volumes; // Owns the spatial state's borrowed view.
    std::vector<W8OctPreTreeBranch> m_branches;
    std::vector<W8OctPreTreeLeaf> m_leaves;
    glm::uvec3 m_leaf_grid_dimensions;
    std::vector<std::uint32_t> m_leaf_lookup;
    std::uint32_t m_branch_count;
    std::uint32_t m_leaf_count;
    std::string m_path_stem;
    std::uint32_t m_vertex_count;
    std::uint32_t m_leaf_polygon_stream_len;
    std::vector<std::uint32_t> m_polygon_index_stream;
    /* ReadOctFile's allocation assertion calls this the "Poly Lookup table":
       polygon index to (kind<<16)|id object key. */
    std::vector<std::uint32_t> m_aulPolyLookup;
    std::vector<W8OctSubmesh> m_pSubmeshes;
    /* Retail m_pAlphaBits; empty without a mesh count. Link tables retain
       unsigned-short disk records but use native owned containers. */
    std::vector<bool> m_alpha_bits;
    OctreeMeshLinks m_particle_links;
    OctreeMeshLinks m_prop_links;
    std::uint32_t m_ulNumParticles;
    std::uint32_t m_gd_surface_stream_len;
    std::uint32_t m_trigger_count;
    std::vector<std::uint32_t> m_gd_surface_index_stream;
    /* Trigger list: serialized as 2-byte elements (ReadOctFile allocates
       count * 2 + 4) even though WriteOctFile emits them four bytes wide. */
    std::vector<unsigned short> m_trigger_indices;
    std::uint32_t m_region_list_len;
    /* The leaf-level mask: VerifyPolygonRegions rebuilds it as
       (1 << m_leaf_level) - 1 and the packed-cell writers emit it as the top
       byte of each (mask<<24 | x<<16 | y<<8 | z) key. */
    std::uint32_t m_region_mask;
    std::uint32_t m_depth_mask;
    std::vector<unsigned short> m_region_index_stream;
    std::unordered_multimap<unsigned int, unsigned short> m_region_links;
    bool m_region_links_ready;
    float m_path_clearance;
    std::unique_ptr<NavigationMesh> pathing;
    int prop_sun_base; /* 0x184: this octree's base index into the shared
                              prop-sunlight bit stream */
    std::uint32_t m_ulNumProps;
    /* Retail m_pPropSunBits, named by ReadOctFile's assertion at 0x0042CAA4. */
    std::vector<bool> m_sunlit_props; /* empty when the level has none */
    /* Sizes of the per-query dedupe sets; fixed once the level is loaded. */
    std::size_t m_polygon_bit_count = 0;
    std::size_t m_object_bit_count = 0;
    std::uint32_t m_root_mesh_count;
    std::uint32_t m_kind1_submesh_count;
    std::uint32_t m_alpha_polygon_count;
    std::uint32_t m_meshCount;
    /* The build's directional-sun count: the driver stores the light total
       and CreateSubMeshes emits it as each CompiledMesh's version and
       sizes the per-sun vertex light arrays from it. */
    unsigned short m_sun_count;
};

LevelSpatialIndex* GetWorldSpatialIndex();

bool IsNavigatorAtTarget(NavigatorMovement* movement);

extern const float g_octree_cell_scale;

int CheckLevelAssetSet(std::string_view level_path);

std::uint32_t* PackColourToLong(std::uint32_t* color, double alpha, double red, double green,
                                double blue);

/* The published level's movement tracker and active environment, or null
   when no level with game data is loaded. */
PartyMotion* LevelMotion();
LevelEnvironment* LevelEnviron();

void ResetInactiveLevelDataVectors(void);
// Resume the shared timers and restart the frame delta as a level is entered.
void UpdateGameDataRuntime();

void ClearLevelMovementStopped(void);
void ResetLevelDataVectors(void);
bool CanInterruptLevelMovement(void);
bool HasLevelWalkableContact(void); /* 0x0041F070 */
bool IsLevelFastMovement(void); /* 0x0041EFB0 */
void ClearLevelFastMovement(void); /* 0x0041EFD0 */
void SetLevelFastMovement(void); /* 0x0041EFE0 */
bool LevelMovedThisUpdate(void); /* 0x0041EFF0 */
bool HasLevelDataVector(void); /* 0x0041F010 */
void ResetCurrentEnvironment(void);
unsigned char SetEnvironmentLoadFlag(unsigned char flag); /* 0x0041AAE0 */
void BeginCameraSway(void);
void EndCameraSway(void);

bool IsLevelMovementStopped(void);
unsigned char ConsumeLevelElapsedTime(float* real_elapsed, float* frame_elapsed);

/* 0x00420BD0: settle a world point onto the octree ground through the
   GameData geometry index. Without an octree, return the original position
   with no contact. */
GroundProbe probeGround(const glm::vec3& position);

void ClearLevelMovementState(void); /* 0x0041F0C0 */

float GetCameraYawRadians();
struct CameraOrientation;
CameraOrientation cameraOrientation();
void BeginManualCameraControl();
void LevelCamera();
void CameraLookAt(const glm::vec3* position); /* 0x00420F90 */
void CameraSnapToTarget(const glm::vec3* target); /* 0x00420FB0 */
void TurnCameraToDegrees(float degrees);
void SetCameraYawDegrees(float degrees);
void ApplyCameraRotation(glm::mat3* rotation);
CameraOrientation setCameraOrientation(CameraOrientation orientation);
/* 0x00420F40: camera yaw in whole degrees plus an optional yaw-rotation
   matrix copy. */
int GetCameraYawAndRotation(glm::mat3* rotation);
/* 0x00421440: project `vector` onto `onto` in place; fails on a degenerate
   target direction. */
glm::vec3 ProjectVectorOntoVector(const glm::vec3& vector, const glm::vec3& onto);
// The party's eye position (world.h has the per-world form).
glm::vec3 cameraLocation();
int GetCameraYawDegrees(void);
/* 0x004215E0: point-visibility query through the world octree; false with no
   world, true for a loaded world without an octree. */
bool hasCameraLineOfSight(const glm::vec3& position);
/* 0x00420E20: start/stop the sustained movement footstep loop. */
void UpdateLevelMovementAudio(void);
/* 0x004EF9A0: fall-impact override handler owned by GameplayCode.cpp;
   declared in wiz8/local_code/GameplayCode.h. */

extern float g_camera_motion_clamp;
extern float g_camera_motion_divisor;
extern const double g_motion_delta_epsilon;
extern const double g_motion_vector_epsilon;
extern const float g_footstep_fall_threshold;

#ifndef WIZ8_GEOMETRY_H
#define WIZ8_GEOMETRY_H

#include "compat/ptr32.h"
#include "surrender/srMath.h"
#include "wiz8/float_constants.h"

#include <math.h>

/* Physical-surface masks recovered from retail collision/build consumers.
   WALKABLE/PATHFINDING also agree with Cosmic Forge's face editor. */
enum W8GDSurfaceFlags {
    W8_GD_SURFACE_AXIS_MASK = 0x00000003u,
    W8_GD_SURFACE_WALKABLE = 0x00000004u,
    W8_GD_SURFACE_COLLISION_PROCESSED = 0x00000008u,
    W8_GD_SURFACE_CONDITIONAL_DISABLED = 0x00000010u,
    W8_GD_SURFACE_EXPLICIT_SLOPE = 0x00000020u,
    W8_GD_SURFACE_PATHFINDING = 0x00000040u,
    W8_GD_SURFACE_CROSSING = 0x00000080u,
    W8_GD_SURFACE_PROP_GEOMETRY = 0x00000800u,
    W8_GD_SURFACE_ENVIRONMENT = 0x00001000u,
    W8_GD_SURFACE_QUERY_VISITED = 0x00002000u,
    W8_GD_SURFACE_SKIP_FILTERED_TRACE = 0x00008000u,
    W8_GD_SURFACE_CROSSING_MASK = W8_GD_SURFACE_CROSSING | W8_GD_SURFACE_ENVIRONMENT,
    W8_GD_SURFACE_INACTIVE_MASK =
        W8_GD_SURFACE_COLLISION_PROCESSED | W8_GD_SURFACE_CONDITIONAL_DISABLED
};

/* Axis-aligned minimum/maximum box used by Wizardry's runtime/build geometry.
   The region builder stores counted 0x18-byte arrays of these and the octree
   consumes one complete pair through AddCollidablePropBounds. Original source
   spelling is not recovered. */
struct W8BoundingBox {
    srVector3T<float> minimum;
    srVector3T<float> maximum;
};

W8_ABI_ASSERT(sizeof(W8BoundingBox) == 0x18, "W8BoundingBox_must_be_0x18");

/* Plane equation record n·p + w = 0: a unit normal plus the signed origin
   distance. The canonical coefficient quad shared by GD surfaces, build-time
   region polygons and frustum culling volumes. */
struct W8Plane {
    srVector3T<float> normal;
    float w;
};

W8_ABI_ASSERT(sizeof(W8Plane) == 0x10, "W8Plane_must_be_0x10");

struct W8GDSurface {
    unsigned int flags;
    unsigned int index;
    /* ENVIRONMENT selects the environment table; other crossing faces select triggers. */
    int trigger_index;
    int edge_link[3];
    int vertex_indices[3];
    /* The region word at +0x32 belongs to W8OctRegionPolygon, not this
       surface. */
    W8Plane plane;
    float distance;
    /* Hit plane ProbePropsAlongMotion fills for ResolveCollision. */
    W8_PTR32(W8Plane) hit_plane;
    unsigned char footstep_surface;  /* W8FootstepSurface selector */
    unsigned char footstep_material; /* W8FootstepMaterial selector */
    unsigned char positional[2];
    float contact_margin;
    unsigned int chance;
    float slope; /* face slope; generated surfaces derive it from plane.normal.y */

    /* The plane's unit normal. */
    const srVector3T<float>* Normal() const
    {
        return &plane.normal;
    }

    /* 0x0041CF90: segment-vs-surface test used by env motion. On a hit `from`
       advances to the contact point and `hit_distance` gets the travelled
       length; distance takes the surface's updated limit. */
    bool TestSegment(srVector3T<float>* from, const srVector3T<float>* direction,
                     float* hit_distance, srVector3T<float>* vertices);
    /* 0x0041D9D0: shrink `limit` to the remaining in-plane distance against
       the nearest triangle edge; fails when no edge improves it. */
    bool ClampHitToEdge(const srVector3T<float>* point, const srVector3T<float>* vertices,
                        float* limit);
    /* 0x0041DC10: collision response for a hit surface; `origin` is advanced
       to `hit_point` and `direction` is bent along the contact plane. */
    bool ResolveCollision(srVector3T<float>* origin, const srVector3T<float>* hit_point,
                          srVector3T<float>* direction, int collision_index);
    /* 0x0041E8E0: whether moving `from` to `to` pushes this surface's
       centroid away from surface `surface_index`'s centroid. */
    bool CentroidsDiverging(int surface_index, const glm::vec3& from, const glm::vec3& to);
    /* 0x0041EA90: environment response for a walkable contact surface;
       adjusts `direction` and the active environ record. */
    bool ApplyEnvironContact(glm::vec3& direction);
};

W8_ABI_ASSERT(sizeof(W8GDSurface) == 0x4c, "W8GDSurface_must_be_0x4c");

/* Unit plane through three vertices. Degenerate triangles have a zero normal. */
inline void SetPlaneFromThreePoints(W8Plane* plane, const srVector3T<float>* first,
                                    const srVector3T<float>* second, const srVector3T<float>* third)
{
    const auto origin = first->glmValue();
    const auto normal =
        normalizedOrZero(glm::cross(second->glmValue() - origin, third->glmValue() - origin));
    plane->normal = normal;
    plane->w = -glm::dot(normal, origin);
}

/* Signed plane distance n·p + w. Independent TUs: 3d.cpp PointInsideFrustum
   0x0046D880 and stLight ContainsPoint 0x0049E460. No Wiz8 COMDAT. */
inline float SignedPlaneDistance(const W8Plane& plane, const srVector3T<float>& point)
{
    return glm::dot(plane.normal.glmValue(), point.glmValue()) + plane.w;
}

void ClassifySurfacePlane(const srVector3T<float>* vertices, W8GDSurface* surface);
void BuildTrianglePlane(W8Plane* plane, const srVector3T<float>* first,
                        const srVector3T<float>* second, const srVector3T<float>* third);
/* Segment intersection projected onto the plane perpendicular to axis. */
bool SegmentCrossesEdge(const glm::vec3& seg_start, const glm::vec3& seg_end,
                        const glm::vec3& edge_a, const glm::vec3& edge_b, unsigned int axis);

#endif


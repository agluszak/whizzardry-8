#ifndef WIZ8_GEOMETRY_H
#define WIZ8_GEOMETRY_H

#include <array>
#include <optional>
#include <span>

#include "glm/common.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/geometric.hpp"
#include "surrender/plane_math.h"

struct W8EnvironRecord;
struct W8LevelDataRecord;
enum W8FootstepSurface : unsigned char;
enum W8FootstepMaterial : unsigned char;

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
    W8_GD_SURFACE_INACTIVE_MASK = W8_GD_SURFACE_COLLISION_PROCESSED |
        W8_GD_SURFACE_CONDITIONAL_DISABLED
};

/* Plane equation record n·p + w = 0: a unit normal plus the signed origin
   distance. The canonical coefficient quad shared by GD surfaces, build-time
   region polygons and frustum culling volumes. */
struct W8Plane {
    glm::vec3 normal{0};
    float w{};
};

struct PlaneContact {
    glm::vec3 point;
    float distance;
};

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
    const W8Plane* hit_plane;
    W8FootstepSurface footstep_surface;
    W8FootstepMaterial footstep_material;
    unsigned char positional[2];
    float contact_margin;
    unsigned int chance;
    float slope; /* face slope; generated surfaces derive it from plane.normal.y */

    /* The returned contact carries the position and travelled distance.
       A hit also stores the surface's contact limit in distance. */
    /* `lift_crossings` raises the start of crossing-surface tests by the
       collision height while the party has no sound environment. */
    std::optional<PlaneContact> segmentContact(const glm::vec3& from, const glm::vec3& direction,
                                               std::span<const glm::vec3, 3> vertices,
                                               bool lift_crossings);
    /* Shrink the limit against the nearest edge, if it improves the contact. */
    std::optional<float> edgeContactLimit(const glm::vec3& point,
                                          std::span<const glm::vec3, 3> vertices,
                                          float limit) const;
    /* 0x0041DC10: collision response for a hit surface; `origin` is advanced
       to `hit_point` and `direction` is bent along the contact plane. */
    bool ResolveCollision(W8LevelDataRecord& level, W8EnvironRecord& environ, glm::vec3* origin,
                          const glm::vec3* hit_point, glm::vec3* direction, int collision_index);
    /* 0x0041E8E0: whether moving `from` to `to` pushes this surface's
       centroid away from surface `surface_index`'s centroid. */
    bool CentroidsDiverging(int surface_index, const glm::vec3& from, const glm::vec3& to);
    /* 0x0041EA90: environment response for a walkable contact surface;
       adjusts `direction` and the active environ record. */
    bool ApplyEnvironContact(W8LevelDataRecord& level, W8EnvironRecord& environ,
                             glm::vec3& direction);
};

using RuntimeTriangle = std::array<glm::vec3, 3>;

template<class Indices>
inline RuntimeTriangle indexedTriangle(std::span<const glm::vec3> vertices, const Indices& indices)
{
    return {vertices[indices[0]], vertices[indices[1]], vertices[indices[2]]};
}

/* Unit plane through three vertices. The result is also the plane record
   stored in level geometry; degenerate triangles have a zero normal. */
inline W8Plane trianglePlane(const glm::vec3& first, const glm::vec3& second,
                             const glm::vec3& third)
{
    const auto plane = srPlaneMath::triangle(first, second, third);
    return {glm::vec3(plane), plane.w};
}

// Prefer x, then y, then z on ties, including a completely degenerate plane.
inline unsigned dominantPlaneAxis(const glm::vec3& normal)
{
    const auto magnitude = glm::abs(normal);
    unsigned result = 0;
    float largest = 0;
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (largest < magnitude[axis]) {
            largest = magnitude[axis];
            result = axis;
        }
    }
    return result;
}

struct PlaneSegment {
    glm::vec3 start;
    glm::vec3 end;
    glm::vec3 direction;
    float length;
    float limit;
};

enum class PlaneContactMode : unsigned char { Collision, SurfaceCollision, Occlusion };

// Occlusion requires a full five-unit exit behind a plane. Collision accepts
// the endpoint within its one-unit skin, unless a closer hit already exists.
inline std::optional<PlaneContact> planeContact(const glm::vec4& plane, const PlaneSegment& segment,
                                                PlaneContactMode mode)
{
    const glm::vec3 normal(plane);
    if (!(glm::dot(normal, segment.direction) <= 0)) return std::nullopt;
    const float front = glm::dot(normal, segment.start) + plane.w;
    if (!(front > 0 && front <= segment.limit)) return std::nullopt;
    // GD surface traces retain the positive front distance within the skin;
    // mesh probes and occlusion report the same contact at zero distance.
    if (front < 1)
        return PlaneContact{segment.start, mode == PlaneContactMode::SurfaceCollision ? front : 0};
    const float clearance = mode == PlaneContactMode::Occlusion ? 5.0f : 1.0f;
    const float back = glm::dot(normal, segment.end) + plane.w;
    if (back >= clearance) return std::nullopt;
    const float exit = -back;
    if (mode == PlaneContactMode::Occlusion) {
        if (exit < clearance) return std::nullopt;
    } else if (!(exit >= clearance || segment.limit < segment.length)) {
        return PlaneContact{segment.end, segment.length};
    }
    const float distance = front / (exit + front) * segment.length;
    return PlaneContact{segment.start + segment.direction * distance, distance};
}

/* Signed plane distance n·p + w. Independent TUs: 3d.cpp PointInsideFrustum
   0x0046D880 and stLight ContainsPoint 0x0049E460. No Wiz8 COMDAT. */
inline float SignedPlaneDistance(const W8Plane& plane, const glm::vec3& point)
{
    return glm::dot(plane.normal, point) + plane.w;
}

void ClassifySurfacePlane(const glm::vec3* vertices, W8GDSurface* surface);
/* Segment intersection projected onto the plane perpendicular to axis. */
bool SegmentCrossesEdge(const glm::vec3& seg_start, const glm::vec3& seg_end,
                        const glm::vec3& edge_a, const glm::vec3& edge_b, unsigned int axis);

#endif

#include "wiz8/render/picking.h"

#include <glm/geometric.hpp>

#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/direction_angles.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/render/camera.h"
#include "wiz8/spatial/bounding_box.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/world/world.h"

/* Heading from source to target in the x/z plane. */
float GetHeadingAngle(const glm::vec3* source, const glm::vec3* target)
{
    return directionAngles((*target) - (*source)).yaw;
}

/* Pitch from source to target against the horizontal. */
float GetElevationAngle(const glm::vec3* source, const glm::vec3* target)
{
    return directionAngles((*target) - (*source)).pitch;
}

/* 0x004BE520: GetElevationAngle evaluated from the camera to the target;
   the demo string pool names it ElevationToTargetCPP. */
float ElevationToTargetCPP(const glm::vec3* target)
{
    glm::vec3 position = cameraLocation();
    return GetElevationAngle(target, &position);
}

float GetCameraFacingYaw(const glm::vec3* position)
{
    glm::vec3 camera = cameraLocation();
    return GetHeadingAngle(position, &camera);
}

/* 0x004BE650: GetHeadingAngle evaluated from the camera to the target; the
   sibling of ElevationToTargetCPP. */
float HeadingToTargetCPP(const glm::vec3* target)
{
    return GetCameraFacingYaw(target);
}

/* The monster twin of WorldItemEntity::DistanceToCamera - animation bounds for the Z
   offset and the monster's GrCycle-tail rep for the centre. Used by the
   monster-list scans in Targeting.cpp. */
float MonsterDistanceToCamera(Monster& monster)
{
    const auto bounds = monster.GetAnimationBounds();
    if (!bounds) {
        return -1.0f;
    }
    const glm::vec3 camera_location = cameraLocation();
    glm::vec3 center = monster.visual.location;
    center.z += (bounds->maximum.z - bounds->minimum.z) * 0.5f;
    return glm::length((camera_location - center));
}

/* The free-function twin of WorldItemEntity::DistanceToCamera - same bounds lookup and
   Z-offset centre, taking the item pointer explicitly for the item-list
   scans in ItemManager.cpp. */
float ItemDistanceToCamera(const WorldItemEntity& item)
{
    const auto bounds = item.GetCachedLocalBounds();
    const glm::vec3 camera_location = cameraLocation();
    glm::vec3 center = item.location;
    center.z += (bounds.maximum.z - bounds.minimum.z) * 0.5f;
    return glm::length((camera_location - center));
}

bool PointInsideBounds(const glm::vec3* point, const glm::vec3* minimum, const glm::vec3* maximum)
{
    return BoundingBox{(*minimum), (*maximum)}.contains((*point));
}

/* Project one world point through the active world's camera and report
   whether it stays in front of it. */
bool projectsToCamera(const glm::vec3& position)
{
    return GetWorld()->camera->project(glm::dvec3(position)).result ==
           Camera3D::PROJECTION_RESULT_ACCEPTED;
}

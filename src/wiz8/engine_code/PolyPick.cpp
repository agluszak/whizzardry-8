#include "wiz8/engine_code/PolyPick.h"

#include <glm/geometric.hpp>

#include "AnimRep.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "layouts/world.h"
#include "surrender/srBoundingBox.h"
#include "surrender/srCamera.h"
#include "wiz8/direction_angles.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Item.h"
#include "wiz8/engine_code/Monster.h"

/* Heading from source to target in the x/z plane. */
// FUNCTION: WIZ8 0x004BE420
float GetHeadingAngle(const glm::vec3* source, const glm::vec3* target)
{
    return directionAngles((*target) - (*source)).yaw;
}

/* Pitch from source to target against the horizontal. */
// FUNCTION: WIZ8 0x004BE490
float GetElevationAngle(const glm::vec3* source, const glm::vec3* target)
{
    return directionAngles((*target) - (*source)).pitch;
}

/* 0x004BE520: GetElevationAngle evaluated from the camera to the target;
   the demo string pool names it ElevationToTargetCPP. */
// FUNCTION: WIZ8 0x004BE520
float ElevationToTargetCPP(const glm::vec3* target)
{
    glm::vec3 position = cameraLocation();
    return GetElevationAngle(target, &position);
}

// FUNCTION: WIZ8 0x004BE5C0
float GetCameraFacingYaw(const glm::vec3* position)
{
    glm::vec3 camera = cameraLocation();
    return GetHeadingAngle(position, &camera);
}

/* 0x004BE650: GetHeadingAngle evaluated from the camera to the target; the
   sibling of ElevationToTargetCPP. */
// FUNCTION: WIZ8 0x004BE650
float HeadingToTargetCPP(const glm::vec3* target)
{
    return GetCameraFacingYaw(target);
}

/* The monster twin of W8Item::DistanceToCamera - animation bounds for the Z
   offset and the monster's GrCycle-tail rep for the centre. Used by the
   monster-list scans in Targeting.cpp. */
// FUNCTION: WIZ8 0x004BE710
float MonsterDistanceToCamera(W8Monster* monster)
{
    const auto bounds = monster->GetAnimationBounds();
    if (!bounds) {
        return -1.0f;
    }
    glm::dvec3 camera = GetWorld()->camera->getLocation();
    glm::vec3 camera_location;
    camera_location = glm::vec3(camera);
    glm::vec3 center = monster->m_pRep->location;
    center.z += (bounds->maximum.z - bounds->minimum.z) * 0.5f;
    return glm::length((camera_location - center));
}

/* The free-function twin of W8Item::DistanceToCamera - same bounds lookup and
   Z-offset centre, taking the item pointer explicitly for the item-list
   scans in ItemManager.cpp. */
// FUNCTION: WIZ8 0x004BE7C0
float ItemDistanceToCamera(W8Item* item)
{
    const auto bounds = item->GetCachedLocalBounds();
    glm::dvec3 camera = GetWorld()->camera->getLocation();
    glm::vec3 camera_location;
    camera_location = glm::vec3(camera);
    glm::vec3 center = item->m_pRep->location;
    center.z += (bounds.maximum.z - bounds.minimum.z) * 0.5f;
    return glm::length((camera_location - center));
}

// FUNCTION: WIZ8 0x004BE870
bool PointInsideBounds(const glm::vec3* point, const glm::vec3* minimum, const glm::vec3* maximum)
{
    return srBoundingBox{(*minimum), (*maximum)}.contains((*point));
}

/* Project one world point through the active world's camera and report
   whether it stays in front of it. */
// FUNCTION: WIZ8 0x004BE940
bool projectsToCamera(const glm::vec3& position)
{
    return GetWorld()->camera->project(glm::dvec3(position)).result ==
           srCamera::PROJECTION_RESULT_ACCEPTED;
}

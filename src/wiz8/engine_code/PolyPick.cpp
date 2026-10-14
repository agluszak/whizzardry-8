#include "glm/ext/vector_double3.hpp"

#include "wiz8/direction_angles.h"
#include "wiz8/engine_code/PolyPick.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Item.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/float_constants.h"
#include "surrender/srCamera.h"
#include "surrender/srBoundingBox.h"
#include "AnimRep.hpp"
#include "glm/ext/vector_float3.hpp"
#include "layouts/world.h"
#include "surrender/srMath.h"

/* 0x005ED1E8: the negative quarter turn the axis-aligned heading case returns. */
// GLOBAL: WIZ8 0x005ed1e8
const float g_negative_quarter_turn = -1.570796012878418f;

/* Heading from source to target in the x/z plane. */
// FUNCTION: WIZ8 0x004BE420
float GetHeadingAngle(const srVector3T<float>* source, const srVector3T<float>* target)
{
    return directionAngles(target->glmValue() - source->glmValue()).yaw;
}

/* Pitch from source to target against the horizontal. */
// FUNCTION: WIZ8 0x004BE490
float GetElevationAngle(const srVector3T<float>* source, const srVector3T<float>* target)
{
    return directionAngles(target->glmValue() - source->glmValue()).pitch;
}

/* 0x004BE520: GetElevationAngle evaluated from the camera to the target;
   the demo string pool names it ElevationToTargetCPP. */
// FUNCTION: WIZ8 0x004BE520
float ElevationToTargetCPP(const srVector3T<float>* target)
{
    srVector3 position = cameraLocation();
    return GetElevationAngle(target, &position);
}

// FUNCTION: WIZ8 0x004BE5C0
float GetCameraFacingYaw(const srVector3T<float>* position)
{
    srVector3 camera = cameraLocation();
    return GetHeadingAngle(position, &camera);
}

/* 0x004BE650: GetHeadingAngle evaluated from the camera to the target; the
   sibling of ElevationToTargetCPP. */
// FUNCTION: WIZ8 0x004BE650
float HeadingToTargetCPP(const srVector3T<float>* target)
{
    return GetCameraFacingYaw(target);
}

/* Euclidean distance between two world-space points. The retail callers at
   0x004F7577 and 0x004F759D are both in the ItemManager.cpp interval. */
// FUNCTION: WIZ8 0x004BE6D0
float DistanceBetweenPoints(const srVector3T<float>* first, const srVector3T<float>* second)
{
    srVector3T<float> delta = *first - *second;
    return delta.Length();
}

/* The monster twin of W8Item::DistanceToCamera - animation bounds for the Z
   offset and the monster's GrCycle-tail rep for the centre. Used by the
   monster-list scans in Targeting.cpp. */
// FUNCTION: WIZ8 0x004BE710
float MonsterDistanceToCamera(W8World* world, W8Monster* monster)
{
    const auto bounds = monster->GetAnimationBounds();
    if (!bounds) {
        return -1.0f;
    }
    srVector3T<double> camera = world->camera->getLocation();
    srVector3T<float> camera_location;
    camera_location = glm::vec3(camera.glmValue());
    srVector3T<float> center = monster->m_pRep->location;
    center.z += (bounds->maximum.z - bounds->minimum.z) * 0.5f;
    return (camera_location - center).Length();
}

/* The free-function twin of W8Item::DistanceToCamera - same bounds lookup and
   Z-offset centre, taking the item pointer explicitly for the item-list
   scans in ItemManager.cpp. */
// FUNCTION: WIZ8 0x004BE7C0
float ItemDistanceToCamera(W8World* world, W8Item* item)
{
    const auto bounds = item->GetCachedLocalBounds();
    srVector3T<double> camera = world->camera->getLocation();
    srVector3T<float> camera_location;
    camera_location = glm::vec3(camera.glmValue());
    srVector3T<float> center = item->m_pRep->location;
    center.z += (bounds.maximum.z - bounds.minimum.z) * 0.5f;
    return (camera_location - center).Length();
}

// FUNCTION: WIZ8 0x004BE870
bool PointInsideBounds(const srVector3T<float>* point, const srVector3T<float>* minimum,
                       const srVector3T<float>* maximum)
{
    return srBoundingBox{minimum->glmValue(), maximum->glmValue()}.contains(point->glmValue());
}

/* Project one world point through the active world's camera and report
   whether it stays in front of it. */
// FUNCTION: WIZ8 0x004BE940
bool projectsToCamera(const glm::vec3& position)
{
    return g_world->camera->project(glm::dvec3(position)).result == srCamera::PROJECTION_RESULT_ACCEPTED;
}

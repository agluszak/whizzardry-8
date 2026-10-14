#ifndef WIZ8_POLYPICK_H
#define WIZ8_POLYPICK_H

#include "glm/ext/vector_float3.hpp"

template<class T> class srVector3T;

/* Engine Code\PolyPick.cpp: world-space pick-angle and bounds helpers. */
float GetHeadingAngle(const srVector3T<float>* source, const srVector3T<float>* target);
float GetElevationAngle(const srVector3T<float>* source, const srVector3T<float>* target);
/* Camera-relative variants of the pair above: named by the demo's
   "ERROR: Suppressed bad float value in ElevationToTargetCPP" literal. */
float ElevationToTargetCPP(const srVector3T<float>* target);
float HeadingToTargetCPP(const srVector3T<float>* target);
float GetCameraFacingYaw(const srVector3T<float>* position);
/* Euclidean distance between two world-space points. */
float DistanceBetweenPoints(const srVector3T<float>* first, const srVector3T<float>* second);
struct W8World;
struct W8Item;
class W8Monster;

/* 0x004BE710: the monster twin - animation bounds and the GrCycle-tail rep. */
float MonsterDistanceToCamera(W8World* world, W8Monster* monster);
/* 0x004BE7C0: free-function twin of W8Item::DistanceToCamera for the item-list
   scans in ItemManager.cpp. */
float ItemDistanceToCamera(W8World* world, W8Item* item);
bool PointInsideBounds(const srVector3T<float>* point, const srVector3T<float>* minimum,
                       const srVector3T<float>* maximum);
bool projectsToCamera(const glm::vec3& position);

#endif

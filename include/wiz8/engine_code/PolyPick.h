#ifndef WIZ8_POLYPICK_H
#define WIZ8_POLYPICK_H

#include "glm/ext/vector_float3.hpp"

/* Engine Code\PolyPick.cpp: world-space pick-angle and bounds helpers. */
float GetHeadingAngle(const glm::vec3* source, const glm::vec3* target);
float GetElevationAngle(const glm::vec3* source, const glm::vec3* target);
/* Camera-relative variants of the pair above: named by the demo's
   "ERROR: Suppressed bad float value in ElevationToTargetCPP" literal. */
float ElevationToTargetCPP(const glm::vec3* target);
float HeadingToTargetCPP(const glm::vec3* target);
float GetCameraFacingYaw(const glm::vec3* position);
/* Euclidean distance between two world-space points. */
struct W8World;
struct W8Item;
class W8Monster;

/* 0x004BE710: the monster twin - animation bounds and the GrCycle-tail rep. */
float MonsterDistanceToCamera(W8World* world, W8Monster* monster);
/* 0x004BE7C0: free-function twin of W8Item::DistanceToCamera for the item-list
   scans in ItemManager.cpp. */
float ItemDistanceToCamera(W8World* world, W8Item* item);
bool PointInsideBounds(const glm::vec3* point, const glm::vec3* minimum, const glm::vec3* maximum);
bool projectsToCamera(const glm::vec3& position);

#endif

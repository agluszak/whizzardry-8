#pragma once

struct srBoundingBox;

#include <memory>
#include <array>
#include <span>
#include "glm/ext/vector_float3.hpp"

#include "compat/native.h"

class W8Monster;
class W8Prop;
struct W8World;
class srScene;
struct srBoundingBox;
class srNode;
class srModelInstance;
class srLight;
class stLight;
class stMeshModel;
class stModelInstance;
struct W8Item;
struct W8GameData;
struct W8OctRegionVolume;
struct W8BoundingBox;
struct W8Plane;
template <class T> class srVector3T;

void SetSceneAmbientLightWhite(srScene* scene);
void RemoveMonsterFromWorldList(W8World*, W8Monster* monster);
void AddMonsterToWorld(W8World*, W8Monster* monster);
void AddItemToWorld(W8World*, W8Item* item);
void RemoveItemFromWorld(W8World*, W8Item* item);
void SetModelInstanceChainExclusionMask(srModelInstance* node, int value);
std::unique_ptr<stModelInstance> DuplicateModelInstance(const stModelInstance& instance);
void UpdateWorldMonsters(W8World* world);
void WorldUpdateProps(W8World* world);
int WorldGetPropCount(W8World*);             /* 0x0046E600 */
W8Prop* WorldGetPropAt(W8World*, int index); /* 0x0046E620 */
void SetWorldSceneMeshShaderLowBits(W8World* owner, int argument);
void SetWorldSceneMeshShaderBit3(W8World* owner, int argument);
void FinalizeStaticScene(srScene* scene);
// Factories return detached instances. Scene ownership requires explicit adoption.
std::unique_ptr<stModelInstance> CreateModelInstance(std::shared_ptr<stMeshModel> model);

unsigned char FinalizeWorldScenes(srNode* node, srNode* dynamic_scene);
/* Mark an instance lit and bake dynamic-scene lights once (render_flags bit 1). */
unsigned char BakeInstanceVertexLightingIfNeeded(stModelInstance* instance,
                                                 srNode* dynamic_scene); /* 0x0046F4A0 */
unsigned char BakeInstanceVertexLighting(stModelInstance* instance, srNode* lights,
                                         bool walk_chain);
void SetSceneMeshShaderBit3(srNode* node, int argument);
void SetSceneMeshShaderLowBits(srNode* node, int argument);
/* 0x0046F760: assign every live world's mesh vertex-light table index and
   mark the mesh dirty for rebake (automap lighting uses table 1). */
void SetWorldMeshVertexLightTable(W8World* world, int table);

/* Report whether a point satisfies all six frustum planes. */
bool PointInsideFrustum(const glm::vec3& point, std::span<const W8Plane, 6> planes);
/* Report whether a sphere of `radius` at `point` reaches all six frustum
   planes (each plane distance may be as low as -radius). */
bool SphereInsideFrustum(const glm::vec3& point, float radius, std::span<const W8Plane, 6> planes);
/* Point-in-triangle test via dominant-axis projection: `axis` selects the two
   planar components used. */
bool PointInsideTriangle(std::span<const glm::vec3, 3> vertices, unsigned axis,
                         const glm::vec3& point);
/* Build the six frustum planes from the eight sorted corner points. */
std::array<W8Plane, 6> BuildFrustumPlanes(std::span<const glm::vec3, 8> points);
/* Order a volume's eight corner points into the canonical (y,z,x)-sorted
   sequence the frustum plane builder expects. */
std::array<glm::vec3, 8> sortedFrustumCorners(std::span<const glm::vec3, 8> points);
/* Report whether the six-float bounds box (min xyz, max xyz) intersects the
   region volume's frustum: true when a bounds corner satisfies all six
   planes or a volume corner lands inside the bounds. */
bool BoundsInsideFrustum(const W8OctRegionVolume& volume, const srBoundingBox& bounds);
bool traceLineOfSightToBounds(const glm::vec3& origin, const srBoundingBox& bounds);
bool hasLineOfSightToBounds(const glm::vec3& origin, const srBoundingBox& bounds);
bool targetMarkerVisible(const glm::vec3& eye, const srBoundingBox& bounds); /* 0x0046F820 */

void WorldUpdateLights(W8World* world);
float WorldGetRenderRange(W8World* world);
double WorldGetFarClip(W8World* world);
void WorldSetFarClip(W8World* world, float distance);
void WorldSetRenderRange(W8World* world, float value);
stLight* CreateLight(srNode* parent, const char* name);
stLight* CreateWorldLight(W8World* world, const char* name);
stLight* CreateRangedWorldLight(W8World* world, const char* name);
void ConfigureWorldLight(srLight* light, float range);
void WorldRemoveLight(W8World* world, stLight* light); /* 0x0046E250 */
void DestroyWorldLights(W8World* world);
void DetachWorldItemMeshes(W8World* world);
extern W8GameData* g_octree_game_data;
void __stdcall SetOctreeGameData(W8GameData* value); /* 0x0046D7D0 */

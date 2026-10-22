#pragma once

struct BoundingBox;

#include <array>
#include <chrono>
#include <memory>
#include <span>

#include "glm/ext/vector_float3.hpp"

class Monster;
class Prop;
struct World;
class WorldScene;
struct BoundingBox;
class MeshInstance;
class Light;
class Light;
class Mesh;
class MeshInstance;
struct WorldItemEntity;
class LevelSpatialIndex;
struct W8OctRegionVolume;
struct W8Plane;

void SetSceneAmbientLightWhite(WorldScene* scene);
void SetModelInstanceExclusionMask(MeshInstance* node, int value);
void UpdateWorldMonsters(World* world);
// Advance the world's prop animations by the step's `elapsed` time.
void WorldUpdateProps(World* world, std::chrono::milliseconds elapsed);
void SetWorldSceneMeshShaderLowBits(World* owner, int argument);
void SetWorldSceneMeshShaderBit3(World* owner, int argument);
void FinalizeStaticScene(World& world);
// Factories return detached instances. Scene ownership requires explicit adoption.
std::unique_ptr<MeshInstance> CreateModelInstance(std::shared_ptr<Mesh> model);

void FinalizeWorldScenes(WorldScene& scene);
/* Mark an instance lit and bake dynamic-scene lights once (render_flags bit 1). */
bool BakeInstanceVertexLightingIfNeeded(MeshInstance* instance, WorldScene* scene); /* 0x0046F4A0 */
bool BakeInstanceVertexLighting(MeshInstance* instance, std::span<Light* const> lights);
void SetSceneMeshShaderBit3(WorldScene* scene, int argument);
void SetSceneMeshShaderLowBits(WorldScene* scene, int argument);
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
bool BoundsInsideFrustum(const W8OctRegionVolume& volume, const BoundingBox& bounds);
bool traceLineOfSightToBounds(const glm::vec3& origin, const BoundingBox& bounds);
bool targetMarkerVisible(const glm::vec3& eye, const BoundingBox& bounds); /* 0x0046F820 */

void WorldUpdateLights(World* world);
double WorldGetFarClip(World* world);
void WorldSetFarClip(World* world, float distance);
void WorldSetRenderRange(World* world, float value);
std::unique_ptr<Light> CreateLight(WorldScene* scene, std::string_view name);
Light* CreateWorldLight(World* world, std::string_view name);
// The world takes ownership; the returned pointer is a borrowed handle until WorldRemoveLight.
Light* AddWorldLight(World* world, std::unique_ptr<Light> light);
Light* CreateRangedWorldLight(World* world, std::string_view name);
void ConfigureWorldLight(Light* light, float range);
void WorldRemoveLight(World* world, Light* light); /* 0x0046E250 */
void DetachWorldItemMeshes(World* world);
LevelSpatialIndex* GetWorldCollision();

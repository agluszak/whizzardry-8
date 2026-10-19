#pragma once

#include <functional>
#include <initializer_list>
#include <memory>
#include <string>

#include "glm/ext/vector_float3.hpp"
#include "surrender/srScene.h"
#include "wiz8/filesystem.h"
#include "wiz8/layouts/world.h"

class srNode;
class stParticle;
class Trigger;
class W8Prop;

W8World* GetWorld(void);
W8World* GetSecondaryWorld(void);
/* Publish a loaded world, returning the previous owner. Production loads and
   test fixtures use the same path. */
std::unique_ptr<W8World> ReplaceWorld(std::unique_ptr<W8World> world);
std::unique_ptr<W8World> ReplaceSkyWorld(std::unique_ptr<W8World> world);

std::unique_ptr<W8World> LoadWorld(std::optional<int> level_id, std::string source_path,
                                   bool use_octree = true);
void DetachAllWorldItems(void);
void UpdateWorlds(void);
void RefreshWorldView(void);
W8WorldCameraState cameraState(const W8World& world);
void SetWorldCameraState(W8World* world, W8World* source_world, W8WorldCameraState* state);
void RestoreWorldCameraState(W8World* source_world, W8WorldCameraState* state);
glm::mat3 cameraRotation(const W8World& world);
glm::vec3 cameraLocation(const W8World& world);
glm::vec3 cameraLocationOrZero(const W8World& world);
void SetWorldScenePosition(W8World* world, const glm::vec3* position);
stParticle* FindParticleByName(const char* name);
bool FindEntityByName(const char* name, glm::vec3* position, float* angle, glm::vec3* direction);
unsigned char AdjustWorldCollisionPosition(float radius, glm::vec3* position, bool check_items,
                                           bool check_monsters);
unsigned char FindNearbyFreePosition(float radius, glm::vec3* position, bool check_items,
                                     bool check_monsters);

/* Scene nodes of the loaded worlds (the level world, then the sky world).
   Lookups return the first type-compatible match by exact name. */
void ForEachWorldNode(const std::function<void(srNode*)>& visit);

template<class T> T* FindWorldNode(std::string_view name)
{
    for (W8World* world : {GetWorld(), GetSecondaryWorld()}) {
        if (world && world->static_scene.get()) {
            if (auto* node = world->static_scene->findChild<T>(name)) return node;
        }
    }
    return nullptr;
}

template<class T> void ForEachWorldNode(const std::function<void(T*)>& visit)
{
    ForEachWorldNode([&](srNode* node) {
        if (auto* match = dynamic_cast<T*>(node)) visit(match);
    });
}

/* APST chunk: serialize every world prop's animation state. The record is a
   fixed 64-byte name plus the six saved animation bytes. */
void SaveWorldProps(wiz8::File& handle);
/* APST chunk: restore saved prop animation state. The 0xDEADD00D signature
   selects the name-keyed format; older saves carry a bare count plus the
   object's id key. Unmatched records are decoded and ignored. */
struct W8Chunk;
void LoadWorldProps(W8World* world, W8Chunk& chunk);
void UpdateCameraPathStateByName(W8World* world, const char* name, int active);

#pragma once

#include <algorithm>
#include <array>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/filesystem.h"
#include "wiz8/monster_generators.h"
#include "wiz8/named_position.h"
#include "wiz8/render/light.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/render/visibility_grid.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/retail_opaque_records.h"
#include "wiz8/world/camera_paths.h"
#include "wiz8/world/camera_pose.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/party_camera.h"

class W8AmbientSound;
struct W8AmbientTrack;
struct W8LevelPopulation;
struct W8WorldCursorState;
struct W8WorldCursorNode;
struct WorldItemEntity;
class Monster;
class Prop;
class Light;
class Material;

class FogParams;
class Camera3D;
class MeshInstance;
class WorldScene;
class LevelSpatialIndex;
class Navigator;
class Missile;
class SpellVisual;
struct MonGen;
class Light;
class Prop;
class W8AmbientSound;
class Trigger;
class TriggerEvent;
class ParticleSystem;
struct KeyframeTrack;
class LevelSpatialIndex;

struct W8AutomapCells {
    float grid_cell_size = 2000.0f;
    std::vector<bool> visited;
    std::vector<bool> lit;
    std::vector<unsigned int> keys;
    std::unordered_map<unsigned int, int> index;
};

// Resources and borrowed indexes precede their consumers. Reverse member
// destruction removes entities before their scene, spatial, audio and population.
struct World {
    explicit World(int level_id = -1, std::string source_path = {});
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    const int level_id;
    const std::string source_path;
    std::unique_ptr<WorldScene> scene;
    EnvironmentState environment;
    std::unique_ptr<LevelSpatialIndex> spatial;
    LevelSpatialIndex* compiledIndex() const;
    LevelSpatialIndex* collisionIndex() const;
    AmbientAudioSystem audio;
    PartyCamera party_camera;
    std::unique_ptr<W8LevelPopulation> population;
    W8AutomapCells automap;
    std::unique_ptr<MeshVisibilityGrid> quads;
    // These indexes borrow resources owned below or by the scene.
    Camera3D* camera{};
    Light* camera_light{};
    MeshInstance* update_mesh_source{};
    std::vector<MeshInstance*> psrMeshes;
    std::vector<Prop*> collidable_props;
    std::vector<Navigator*> navigators;
    std::vector<Monster*> active_monsters;
    SpellVisual* target_cone = nullptr;
    unsigned int next_trigger_id = 1;
    int last_visible_prop_index = -1;
    int cursor_serial = 100;
    float render_range{};
    bool initial_camera_sway = false;
    glm::mat3 initial_camera_rotation{1.0f};
    std::vector<W8NamedPosition> named_positions;
    std::vector<std::unique_ptr<Light>> transient_lights;
    std::vector<std::unique_ptr<ParticleSystem>> particles;
    std::vector<std::unique_ptr<ParticleSystem>> retired_particles;
    std::vector<std::unique_ptr<W8CameraPath>> plsCameras;
    // Trigger destruction unregisters its owned event from this schedule.
    std::vector<TriggerEvent*> timed_events;
    std::vector<std::unique_ptr<TriggerEvent>> standalone_trigger_events;
    std::vector<std::unique_ptr<Trigger>> triggers;
    std::vector<std::unique_ptr<Prop>> plsProps;
    std::vector<std::unique_ptr<WorldItemEntity>> plsItems;
    std::vector<std::unique_ptr<W8WorldCursorNode>> cursor_nodes;
    std::unique_ptr<W8WorldCursorState> cursor;
    std::vector<std::unique_ptr<Monster>> monsters;
    std::vector<std::unique_ptr<SpellVisual>> spell_visuals;
    std::vector<std::unique_ptr<Missile>> missiles;
    W8EncounterState encounters;
};

extern int g_combat_round_counter;

/* Typed element access for the homogeneous world lists. */
inline Prop* GetWorldProp(World* world, int index)
{
    if (index < 0 || std::cmp_greater_equal(index, world->plsProps.size())) {
        return nullptr;
    }
    return world->plsProps[index].get();
}

inline W8CameraPath* GetWorldCameraPath(World* world, int index)
{
    if (index < 0 || std::cmp_greater_equal(index, world->plsCameras.size())) {
        return nullptr;
    }
    return world->plsCameras[index].get();
}

inline W8AmbientSound* GetWorldAmbientSound(World* world, int index)
{
    if (index < 0 || std::cmp_greater_equal(index, world->audio.emitters.size())) {
        return nullptr;
    }
    return world->audio.emitters[index].get();
}

class ParticleSystem;
class Trigger;
class Prop;

World* GetWorld(void);
World* GetSecondaryWorld(void);
/* Publish a loaded world, returning the previous owner. Production loads and
   test fixtures use the same path. */
std::unique_ptr<World> ReplaceWorld(std::unique_ptr<World> world);
std::unique_ptr<World> ReplaceSkyWorld(std::unique_ptr<World> world);

std::unique_ptr<World> LoadWorld(int level_id, std::string source_path, bool use_octree = true);
void DetachAllWorldItems(void);
void UpdateWorlds(void);
void RefreshWorldView(void);
CameraPose cameraState(const World& world);
void SetWorldCameraState(World* world, CameraPose* state);
void RestoreWorldCameraState(CameraPose* state);
glm::mat3 cameraRotation(const World& world);
glm::vec3 cameraLocation(const World& world);
glm::vec3 cameraLocationOrZero(const World& world);
void SetWorldScenePosition(World* world, const glm::vec3* position);
ParticleSystem* FindParticleByName(std::string_view name);
bool FindEntityByName(std::string_view name, glm::vec3* position, float* angle,
                      glm::vec3* direction);
unsigned char AdjustWorldCollisionPosition(float radius, glm::vec3* position, bool check_items,
                                           bool check_monsters);
unsigned char FindNearbyFreePosition(float radius, glm::vec3* position, bool check_items,
                                     bool check_monsters);

// Loaded-level lookup precedes the sky; names retain their exact asset spelling.
Light* FindWorldLight(World& world, std::string_view name);
Light* FindWorldLight(std::string_view name);
class ClipPlane;
ClipPlane* FindWorldClipPlane(std::string_view name);
void ForEachWorldLight(std::function_ref<void(Light*)> visit);

/* APST chunk: serialize every world prop's animation state. The record is a
   fixed 64-byte name plus the six saved animation bytes. */
void SaveWorldProps(wiz8::File& handle);
/* APST chunk: restore saved prop animation state. The 0xDEADD00D signature
   selects the name-keyed format; older saves carry a bare count plus the
   object's id key. Unmatched records are decoded and ignored. */
struct W8Chunk;
void LoadWorldProps(World* world, W8Chunk& chunk);
void UpdateCameraPathStateByName(World* world, std::string_view name, int active);

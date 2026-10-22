#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/filesystem.h"
#include "wiz8/monster_generators.h"
#include "wiz8/named_position.h"
#include "wiz8/render/light.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/retail_opaque_records.h"
#include "wiz8/world/camera_paths.h"
#include "wiz8/world/camera_pose.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/world_items.h"
#include "wiz8/world_cursor.h"

class NativeObject;
class LevelScript;
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
class Rng;
class MeshInstance;
class WorldScene;
class LevelSpatialIndex;
class Navigator;
class Missile;
class SpellVisual;
class SimulationSink;
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
    /* A world draws from its game's random stream and numbers new items from its game's
       counter, so building one advances them exactly as playing it does. */
    World(Rng& rng, int& next_item_id, int level_id = -1, std::string source_path = {});
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    const int level_id;
    const std::string source_path;
    std::unique_ptr<WorldScene> scene;
    // One frame result: the occluding intersection and its eligible gameplay subject.
    struct Selection {
        Pick intersection;
        std::variant<std::monostate, MeshInstance*, Prop*, NativeObject*> subject;
    } picked;
    void acceptPick(Pick intersection, bool in_combat);
    MeshInstance* selectedModel() const;
    Prop* selectedProp() const;
    NativeObject* selectedNativeObject() const;
    std::vector<std::unique_ptr<NativeObject>> native_objects;
    EnvironmentState environment;
    std::unique_ptr<LevelSpatialIndex> spatial;
    LevelSpatialIndex* compiledIndex() const;
    LevelSpatialIndex* collisionIndex() const;
    AmbientAudioSystem audio;
    PartyCamera party_camera;
    std::unique_ptr<W8LevelPopulation> population;
    WorldItems items;
    W8AutomapCells automap;
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
    std::vector<std::unique_ptr<Trigger>> triggers;
    // Destroy actions before the triggers they borrow. Only the update loop erases actions.
    std::vector<std::unique_ptr<TriggerEvent>> timed_events;
    TriggerEvent* findTimedAction(const Trigger* trigger) const;
    TriggerEvent& addTimedAction(Trigger* trigger);
    std::vector<std::unique_ptr<Prop>> plsProps;
    std::vector<std::unique_ptr<WorldItemEntity>> plsItems;
    std::vector<std::unique_ptr<W8WorldCursorNode>> cursor_nodes;
    std::unique_ptr<W8WorldCursorState> cursor;
    W8WorldCursorRecall cursor_recall;
    /* The *_definitions hold one loaded, never-live representation per monster
       species, spell visual and missile kind. Live entities are copied from them;
       they hold no scene attachments and no navigator registration. */
    std::vector<std::unique_ptr<Monster>> monster_definitions;
    std::vector<std::unique_ptr<Monster>> monsters;
    std::vector<std::unique_ptr<SpellVisual>> spell_visual_definitions;
    std::vector<std::unique_ptr<SpellVisual>> spell_visuals;
    std::vector<std::unique_ptr<Missile>> missile_definitions;
    std::vector<std::unique_ptr<Missile>> missiles;
    W8EncounterState encounters;
    // Where the entities report missile hits and release frames; null reports nowhere.
    SimulationSink* simulation = nullptr;
    /* The level's script, installed when the level is entered. Last, so it goes first:
       it borrows the entities above and its trigger callbacks refer to it. */
    std::unique_ptr<LevelScript> script;

    /* Allocate the next save identity of retail's shared prop sequence. Every prop,
       item, monster, missile and spell visual the world loads takes one, in load
       order, whether or not it keeps it. */
    int takeLegacyPropSaveId() { return ++next_legacy_prop_save_id_; }
    Rng& rng() const { return rng_; }

private:
    Rng& rng_;
    int next_legacy_prop_save_id_ = 1;
};

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

/* Transitional: the game's level and sky worlds (`Game::level`), or null with no game
   in progress. Loads and test fixtures publish worlds through `Level`. */
World* GetWorld(void);
World* GetSecondaryWorld(void);

std::unique_ptr<World> LoadWorld(Rng& rng, int& next_item_id, int level_id,
                                 std::string source_path);
void DetachAllWorldItems(void);
// Run the worlds over the step's `elapsed` time.
void UpdateWorlds(std::chrono::milliseconds elapsed);
void RefreshWorldView(void);
/* The party's pose is `World::party_camera`: the eye position and the orientation,
   with the retail pose bytes the save keeps. Gameplay queries, saves and the rendered
   views all read it; rendering never writes it. */
CameraPose partyPose(const World& world);
glm::mat3 cameraRotation(const World& world);
// The party's eye position.
glm::vec3 cameraLocation(const World& world);
/* Place the party's eye at `position`, as a level entrance, a portal or a reload does. The
   party navigator follows when the world is the active one. */
void placeParty(World& world, const glm::vec3& position);
// Restore a saved pose: the position, the orientation and the retail pose bytes.
void restorePartyPose(World& world, const CameraPose& pose);
ParticleSystem* FindParticleByName(std::string_view name);
bool FindEntityByName(std::string_view name, glm::vec3* position, float* angle,
                      glm::vec3* direction);
bool AdjustWorldCollisionPosition(float radius, glm::vec3* position, bool check_items,
                                  bool check_monsters);
bool FindNearbyFreePosition(float radius, glm::vec3* position, bool check_items,
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
void UpdateCameraPathStateByName(World* world, std::string_view name, int active);

#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/filesystem.h"
#include "wiz8/monster_generators.h"
#include "wiz8/named_position.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/render/camera_shake.h"
#include "wiz8/render/light.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/retail_opaque_records.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/world/automap.h"
#include "wiz8/world/camera_paths.h"
#include "wiz8/world/camera_pose.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/environment_state.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world_cursor.h"

class NativeObject;
class LevelScript;
class W8AmbientSound;
struct W8AmbientTrack;
struct W8LevelPopulation;
struct W8MonsterInfo;
struct W8MonsterRecord;
struct W8WorldItem;
struct GameData;
class Events;
class Rng;
struct W8SpellEffectEntry;
enum class W8MonsterRemoval : unsigned char;
struct W8WorldCursorState;
struct W8WorldCursorNode;
struct WorldItemEntity;
class Monster;
struct MonsterDefinition;
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
struct MissileDefinition;
class SpellVisual;
struct SpellVisualDefinition;
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

/* The party's presence in one world: its pose (the eye position and orientation, with
   the retail pose bytes the save keeps) and its collision and pathing proxy there, at
   navigation height below the eye. Gameplay queries, saves and the rendered views read
   the pose; rendering never writes it. Every change through `place` and `restore` moves
   the proxy with it. Characters and inventory stay with the game. */
class WorldParty {
public:
    explicit WorldParty(World& world, float reference_gravity = kDefaultGravity);
    PartyCamera camera;
    // Registered in the world's navigators like any other mover.
    Navigator navigator;

    CameraPose pose() const;
    glm::vec3 eye() const { return camera.m_position; }
    glm::mat3 rotation() const { return camera.rotationMatrix(); }
    // The proxy's position at navigation height below the eye, and its body radius.
    glm::vec3 position() const { return navigator.GetPosition(); }
    float radius() const { return navigator.radius; }
    // The sphere the world's other movers collide with.
    BoundingSphere collisionSphere() const;
    // Place the eye at `position`, as a level entrance, a portal or a reload does.
    void place(const glm::vec3& position);
    // Restore a saved pose: the position, the orientation and the retail pose bytes.
    void restore(const CameraPose& pose);
    // Move the proxy to the camera's pose after the camera moved by itself.
    void followCamera();
};

struct Story;

// Resources and borrowed indexes precede their consumers. Reverse member
// destruction removes population-owned bodies before their resources and indexes.
struct World {
    explicit World(int level_id = -1, std::string source_path = {}, TimerClockView clocks = {},
                   ShakeSystem* shakes = nullptr, float reference_gravity = kDefaultGravity);
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    const TimerClockView clocks;
    // Borrowed playthrough sequence owner; standalone geometry worlds have none.
    ShakeSystem* const shakes;
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
    Automap automap{*this};
    // These indexes borrow resources owned below or by the scene.
    Camera3D* camera{};
    Light* camera_light{};
    MeshInstance* update_mesh_source{};
    std::vector<MeshInstance*> psrMeshes;
    std::vector<Prop*> collidable_props;
    std::vector<Navigator*> navigators;
    // After the registry its navigator joins; destroyed before the spatial index.
    WorldParty party{*this};
    std::vector<Monster*> active_monsters;
    // Commands remove relationships immediately; this pass retains removed owners
    // until every script/update borrowing them has returned.
    void updateMonsters();
    void prepareMonsterBody(W8MonsterInfo& info, Rng& rng, bool unborn);
    void prepareMonsterBody(W8MonsterInfo& info, Rng& rng, bool unborn, float reference_gravity);
    std::unique_ptr<Monster> loadMonsterBody(const W8MonsterRecord& record, int location_id,
                                             Rng& rng);
    W8WorldItem* adoptItem(std::unique_ptr<W8WorldItem> item);
    bool settleItem(W8WorldItem* item);
    std::optional<glm::vec3> itemDropPosition(const glm::vec2& cursor) const;
    Trigger* loadTrigger(wiz8::File& file, const GameData& data, Rng& rng, Story& story);
    void updateNearbyItems(const GameData& data, Rng& rng, Events& events, float gravity);
    void activateItem(const GameData& data, Rng& rng, W8WorldItem* item);
    void addItemToSector(int sector, W8WorldItem* item);
    void removeItemFromSector(int sector, W8WorldItem* item);
    bool isRetiring(const Monster& monster) const;
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
    std::vector<std::unique_ptr<W8WorldCursorNode>> cursor_nodes;
    W8WorldCursorRecall cursor_recall;
    /* The *_definitions hold the loaded resources of each monster species, spell
       visual and missile kind; live entities are created from them. */
    std::vector<std::unique_ptr<MonsterDefinition>> monster_definitions;
    std::vector<std::unique_ptr<SpellVisualDefinition>> spell_visual_definitions;
    // Continuing casts borrow this world's visuals and missiles. Missile teardown
    // removes its references while this queue is still alive.
    std::vector<std::unique_ptr<W8SpellEffectEntry>> spell_effects;
    bool hasPendingSpellEffects() const;
    W8SpellEffectEntry* monsterControlEffect() const;
    void tickSpellEffects();
    void absorbMissileDamage(Missile& missile);
    void detachMissile(Missile& missile);
    std::vector<std::unique_ptr<SpellVisual>> spell_visuals;
    std::vector<std::unique_ptr<MissileDefinition>> missile_definitions;
    std::vector<std::unique_ptr<Missile>> missiles;
    // Records own their loaded bodies. Both these owners go before the resource
    // definitions, particles, navigation index and scene their bodies borrow.
    std::unique_ptr<W8LevelPopulation> population;
    std::unique_ptr<W8WorldCursorState> cursor;
    W8EncounterState encounters;
    // Where the entities report missile hits and release frames; null reports nowhere.
    SimulationSink* simulation = nullptr;
    /* The level's script, installed when the level is entered. Last, so it goes first:
       it borrows the entities above and its trigger callbacks refer to it. */
    std::unique_ptr<LevelScript> script;

private:
    std::optional<glm::vec3> adjustItemDropPosition(glm::vec3 position) const;
    void updateNearestMonsterGroupMembers();
    bool advanceFallingItem(W8WorldItem& item, float gravity);
    friend bool RemoveMonster(W8MonsterInfo*, W8MonsterRemoval);
    // Borrowed only during updateMonsters; the local batch is the temporary owner.
    std::vector<std::unique_ptr<W8MonsterInfo>>* monster_retirements_ = nullptr;
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

void RefreshWorldView(void);
ParticleSystem* FindParticleByName(std::string_view name);
bool FindEntityByName(std::string_view name, glm::vec3* position, float* angle,
                      glm::vec3* direction);
// World-space throw vector from normalized screen coordinates.
glm::vec3 ItemThrowDirection(const glm::vec2& cursor, const glm::mat3& camera_rotation);

// Loaded-level lookup precedes the sky; names retain their exact asset spelling.
Light* FindWorldLight(World& world, std::string_view name);
Light* FindWorldLight(std::string_view name);
class ClipPlane;
ClipPlane* FindWorldClipPlane(std::string_view name);
void ForEachWorldLight(std::function_ref<void(Light*)> visit);

/* APST chunk: serialize every world prop's animation state. The record is a
   fixed 64-byte name plus the six saved animation bytes. */
/* APST chunk: restore saved prop animation state. The 0xDEADD00D signature
   selects the name-keyed format; older saves carry a bare count plus the
   object's id key. Unmatched records are decoded and ignored. */
struct W8Chunk;
void UpdateCameraPathStateByName(World* world, std::string_view name, int active);

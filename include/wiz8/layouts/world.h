#ifndef WIZ8_LAYOUTS_WORLD_H
#define WIZ8_LAYOUTS_WORLD_H

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
class W8AmbientSound;
struct W8AmbientTrack;
struct W8WorldCursorState;
struct W8WorldCursorNode;
struct W8Item;
class W8Monster;
class W8Prop;
class srNode;
class stLight;
class srMaterial;

#include <algorithm>

#include "glm/ext/vector_float3.hpp"
#include "surrender/srScene.h"
#include "wiz8/engine_code/Camera.h"
#include "wiz8/engine_code/Environment.h"
#include "wiz8/engine_code/quad.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/stParticle.h"
#include "wiz8/named_position.h"

class srFog;
class srCamera;
class srModelInstance;
class srNode;
class srScene;
class stLevel;
class W8Octree;
class W8Missile;
class W8SpellVisual;
struct MonGen;
class stLight;
class W8Prop;
class W8AmbientSound;
class Trigger;
class W8TriggerEvent;
class stParticle;
struct W8PathAI;
struct W8GameData;

/* Six-float camera angle record: [0] is the live angle, the tail is
   serialized padding. */
typedef float W8CameraAngleRecord[6];

struct W8WorldCameraState {
    glm::vec3 position;
    W8CameraAngleRecord pitch;
    W8CameraAngleRecord yaw;
};

struct W8AutomapCells {
    float grid_cell_size = 2000.0f;
    std::vector<bool> visited;
    std::vector<bool> lit;
    std::vector<unsigned int> keys;
    std::unordered_map<unsigned int, int> index;
};

/* Runtime world, not a retail disk record. Collection storage belongs to the
   world. Destruction proceeds in three phases (stop activity, unregister borrowed indexes,
   destroy owned resources).
   collidable_props is a non-owning index. Light updates derive from owned lights and scene attachment. Named positions
   are owned values: lookups copy their data and never retain element addresses. */
struct W8World {
    explicit W8World(int level_id = -1, std::string source_path = {});
    std::unique_ptr<W8WorldCursorState> cursor;
    std::vector<std::unique_ptr<W8WorldCursorNode>> cursor_nodes;
    int cursor_serial = 100;
    const int level_id;
    const std::string source_path;
    ~W8World();
    W8World(const W8World&) = delete;
    W8World& operator=(const W8World&) = delete;
    unsigned int next_trigger_id = 1;
    W8AutomapCells automap;
    std::shared_ptr<srMaterial> cloud_material;
    std::vector<std::unique_ptr<W8Monster>> monsters;
    // Update membership borrows loaded monsters, including the visible cursor.
    std::vector<W8Monster*> active_monsters;
    std::vector<std::unique_ptr<W8Item>> plsItems;
    std::vector<std::unique_ptr<W8Prop>> plsProps;
    std::vector<std::unique_ptr<W8CameraPath>> plsCameras;
    std::vector<std::unique_ptr<W8AmbientSound>> plsAmbientSounds;
    std::vector<std::unique_ptr<W8AmbientTrack>> ambient_tracks;
    // Authored environment data belongs to the loaded scene, not the active session.
    std::array<glm::vec3, 256> environment_colours = EnvironmentColourRamp();
    std::array<glm::vec3, 256> light_colours = EnvironmentColourRamp();
    std::vector<stLight*> environment_lights;
    glm::vec3 environment_offset{0.0f};
    bool initial_camera_sway = false;
    glm::mat3 initial_camera_rotation{1.0f};
    bool fog_enabled = false;
    int last_light_phase = -1;
    int last_environment_colour_phase = -1;
    float environment_range_start = 0.75f;
    float environment_range_end = 1.0f;
    float view_distance{};
    float environment_intensity{};
    /* Snapshot of environment_intensity taken when a fade-out starts; the
       lighting transition multiplies g_light_scale by this base. */
    float environment_base_intensity{};
    glm::vec3 environment_colour{0.0f};
    stLevel* level{};
    std::unique_ptr<srScene> static_scene;
    std::unique_ptr<srFog> static_fog;
    std::unique_ptr<srFog> dynamic_fog;
    srNode* dynamic_scene{};
    srCamera* camera{};
    // Borrowed instances owned by the scene graph; includes the retail sentinel slot.
    std::vector<srModelInstance*> psrMeshes;
    std::unique_ptr<W8GameData> game_data;
    std::unique_ptr<W8Octree> octree;
    stLight* camera_light{};
    std::unique_ptr<W8Quad> quads;
    srModelInstance* update_mesh_source{};
    float render_range{};
    std::vector<srNode*> nodes_to_disable;
    // Lights created for this world (including monster-cycle copies); the world owns them.
    std::vector<std::unique_ptr<stLight>> transient_lights;
    std::vector<std::unique_ptr<W8SpellVisual>> spell_visuals;
    std::vector<std::unique_ptr<W8Missile>> missiles;
    std::vector<W8Prop*> collidable_props;
    std::vector<std::unique_ptr<MonGen>> monster_generators;
    std::vector<std::unique_ptr<Trigger>> triggers;
    // Ordered scheduling borrows trigger-owned events; standalone effects have their own owners.
    std::vector<W8TriggerEvent*> timed_events;
    std::vector<std::unique_ptr<W8TriggerEvent>> standalone_trigger_events;
    // Level particles: the world owns them; the dynamic scene only attaches them.
    std::vector<std::unique_ptr<stParticle>> particles;
    // Cycle particles that outlived their cycle while still emitting.
    std::vector<std::unique_ptr<stParticle>> retired_particles;
    std::vector<W8NamedPosition> named_positions;
};

extern int g_combat_round_counter;

/* Typed element access for the homogeneous world lists. */
inline W8Prop* GetWorldProp(W8World* world, int index)
{
    if (index < 0 || std::cmp_greater_equal(index, world->plsProps.size())) {
        return nullptr;
    }
    return world->plsProps[index].get();
}

inline W8CameraPath* GetWorldCameraPath(W8World* world, int index)
{
    if (index < 0 || std::cmp_greater_equal(index, world->plsCameras.size())) {
        return nullptr;
    }
    return world->plsCameras[index].get();
}

inline W8AmbientSound* GetWorldAmbientSound(W8World* world, int index)
{
    if (index < 0 || std::cmp_greater_equal(index, world->plsAmbientSounds.size())) {
        return nullptr;
    }
    return world->plsAmbientSounds[index].get();
}

#endif

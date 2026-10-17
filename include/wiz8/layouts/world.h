#ifndef WIZ8_LAYOUTS_WORLD_H
#define WIZ8_LAYOUTS_WORLD_H

#include <memory>
#include <utility>
#include <vector>
class W8AmbientSound;
struct W8Item;
class W8Monster;
class W8Prop;
class srNode;
class stLight;
class stMaterial;

#include <algorithm>

#include "glm/ext/vector_float3.hpp"
#include "surrender/srScene.h"
#include "wiz8/engine_code/Camera.h"
#include "wiz8/engine_code/quad.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/stParticle.h"
#include "wiz8/named_position.h"

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

/* Runtime world, not a retail disk record. Collection storage belongs to the
   world. Destruction proceeds in three phases (stop activity, unregister borrowed indexes,
   destroy owned resources).
   lights_to_update and collidable_props are non-owning indexes. Named positions
   are owned values: lookups copy their data and never retain element addresses. */
struct W8World {
    W8World();
    ~W8World();
    W8World(const W8World&) = delete;
    W8World& operator=(const W8World&) = delete;
    std::shared_ptr<stMaterial> cloud_material;
    std::vector<std::unique_ptr<W8Monster>> monsters;
    // Update membership borrows loaded monsters, including the visible cursor.
    std::vector<W8Monster*> active_monsters;
    std::vector<std::unique_ptr<W8Item>> plsItems;
    std::vector<std::unique_ptr<W8Prop>> plsProps;
    std::vector<std::unique_ptr<W8CameraPath>> plsCameras;
    std::vector<std::unique_ptr<W8AmbientSound>> plsAmbientSounds;
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
    srNode* dynamic_scene{};
    srCamera* camera{};
    // Borrowed instances owned by the scene graph; includes the retail sentinel slot.
    std::vector<srModelInstance*> psrMeshes;
    std::unique_ptr<W8GameData> game_data;
    std::unique_ptr<W8Octree> octree;
    stLight* camera_light{};
    bool m_loaded{};
    std::unique_ptr<W8Quad> quads;
    srModelInstance* update_mesh_source{};
    float render_range{};
    std::vector<srNode*> nodes_to_disable;
    // Lights created for this world (including monster-cycle copies); the world owns them.
    std::vector<std::unique_ptr<stLight>> transient_lights;
    std::vector<std::unique_ptr<W8SpellVisual>> spell_visuals;
    std::vector<std::unique_ptr<W8Missile>> missiles;
    std::vector<stLight*> lights_to_update;
    std::vector<W8Prop*> collidable_props;
    std::vector<std::unique_ptr<MonGen>> monster_generators;
    std::vector<std::unique_ptr<Trigger>> triggers;
    // Level particles: the world owns them; the dynamic scene only attaches them.
    std::vector<std::unique_ptr<stParticle>> particles;
    // Cycle particles that outlived their cycle while still emitting.
    std::vector<std::unique_ptr<stParticle>> retired_particles;
    std::vector<W8NamedPosition> named_positions;
};

/* The level world and the sky world. Each slot owns its world; see
   DestroyWorld for teardown. */
extern std::unique_ptr<W8World> g_world;
extern std::unique_ptr<W8World> g_secondary_world;
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

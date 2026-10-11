#ifndef WIZ8_LAYOUTS_WORLD_H
#define WIZ8_LAYOUTS_WORLD_H

#include <utility>
#include <memory>

#include <vector>
class W8AmbientSound;
struct W8Item;
class W8Monster;
class W8Prop;
class srNode;
class stLight;

#include "surrender/srMath.h"
#include "wiz8/environment_colour.h"
#include <algorithm>
#include "wiz8/named_position.h"
#include "wiz8/engine_code/Camera.h"
#include "wiz8/engine_code/quad.h"

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

#pragma pack(push, 1)
struct W8WorldCameraState {
    srVector3T<float> position;
    W8CameraAngleRecord pitch;
    W8CameraAngleRecord yaw;
};
#pragma pack(pop)
static_assert(sizeof(W8WorldCameraState) == 0x3c, "W8WorldCameraState_size");
static_assert(alignof(W8WorldCameraState) == 1, "W8WorldCameraState_alignment");

/* Runtime world, not a retail disk record. Collection storage belongs to the
   world. Cameras own their paths; raw entity pointers still follow the ordered
   DestroyWorld teardown.
   lights_to_update and collidable_props are non-owning indexes. Named positions
   are owned values: lookups copy their data and never retain element addresses. */
struct W8World {
    std::vector<W8Monster*> plsMonsters;
    std::vector<W8Item*> plsItems;
    std::vector<W8Prop*> plsProps;
    std::vector<std::unique_ptr<W8CameraPath>> plsCameras;
    std::vector<W8AmbientSound*> plsAmbientSounds;
    float environment_range_start;
    float environment_range_end;
    /* Third serialized environment channel (loaded from
       environment_colour.blue like the start/end fractions); never read. */
    float environment_range_blue;
    float view_distance;
    float environment_intensity;
    /* Snapshot of environment_intensity taken when a fade-out starts; the
       lighting transition multiplies g_light_scale by this base. */
    float environment_base_intensity;
    EnvironmentColour environment_colour;
    stLevel* level;
    srScene* static_scene;
    srNode* dynamic_scene;
    srCamera* camera;
    // Borrowed instances owned by the scene graph; includes the retail sentinel slot.
    std::vector<srModelInstance*> psrMeshes;
    W8GameData* game_data;
    W8Octree* octree;
    stLight* camera_light;
    unsigned char m_padding_058[0x11];
    bool m_loaded;
    unsigned char m_padding_06a[2];
    std::unique_ptr<W8Quad> quads;
    srModelInstance* update_mesh_source;
    float render_range0;
    float render_range;
    unsigned char m_unknown_07c[0x20];
    std::vector<srNode*> nodes_to_disable;
    std::vector<stLight*> transient_lights;
    std::vector<W8SpellVisual*> spell_visuals;
    std::vector<W8Missile*> missiles;
    std::vector<stLight*> lights_to_update;
    std::vector<W8Prop*> collidable_props;
    std::vector<MonGen*> monster_generators;
    std::vector<Trigger*> triggers;
    std::vector<stParticle*> particles;
    std::vector<W8NamedPosition> named_positions;
    unsigned char m_unknown_0d4[8];
};

extern W8World* g_world;
extern W8World* g_secondary_world;
extern int g_combat_round_counter;

/* Typed element access for the homogeneous world lists. */
inline W8Prop* GetWorldProp(W8World* world, int index)
{
    if (index < 0 || std::cmp_greater_equal(index, world->plsProps.size())) {
        return nullptr;
    }
    return world->plsProps[index];
}

inline W8CameraPath* GetWorldCameraPath(W8World* world, int index)
{
    if (index < 0 ||
        std::cmp_greater_equal(index, world->plsCameras.size())) {
        return nullptr;
    }
    return world->plsCameras[index].get();
}

inline W8AmbientSound* GetWorldAmbientSound(W8World* world, int index)
{
    if (index < 0 ||
        std::cmp_greater_equal(index, world->plsAmbientSounds.size())) {
        return nullptr;
    }
    return world->plsAmbientSounds[index];
}

#endif

#ifndef WIZ8_LAYOUTS_WORLD_H
#define WIZ8_LAYOUTS_WORLD_H

#include <vector>
class W8AmbientSound;
struct W8CameraPath;
struct W8Item;
class W8Monster;
class W8Prop;
class srNode;
class stLight;

#include "surrender/srMath.h"
#include "wiz8/environment_colour.h"
#include <vector>
#include <algorithm>
#include "wiz8/named_position.h"

class srCamera;
class srModelInstance;
class srNode;
class srScene;
class stLevel;
class W8Octree;
struct W8Quad;
class W8Missile;
class W8SpellVisual;
struct MonGen;
class stLight;
class W8Prop;
class W8AmbientSound;
struct W8CameraPath;
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
W8_ABI_ASSERT(sizeof(W8WorldCameraState) == 0x3c, "W8WorldCameraState_size");
W8_ABI_ASSERT(alignof(W8WorldCameraState) == 1, "W8WorldCameraState_alignment");

/* Engine Code\3dapi.cpp. CreateWorld allocates and zeroes exactly 0xdc bytes;
   the list/vector setup and teardown routines prove the owned fields below. */
struct W8World {
    std::vector<W8Monster*>* plsMonsters;
    std::vector<W8Item*>* plsItems;
    std::vector<W8Prop*>* plsProps;
    std::vector<W8CameraPath*>* plsCameras;
    std::vector<W8AmbientSound*>* plsAmbientSounds;
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
    srModelInstance** psrMeshes;
    W8GameData* game_data;
    W8Octree* octree;
    stLight* camera_light;
    unsigned char m_padding_058[0x11];
    bool m_loaded;
    unsigned char m_padding_06a[2];
    W8Quad* quads;
    srModelInstance* update_mesh_source;
    float render_range0;
    float render_range;
    unsigned char m_unknown_07c[0x20];
    std::vector<srNode*> nodes_to_disable;
    std::vector<stLight*> transient_lights;
    std::vector<W8SpellVisual*>* spell_visuals;
    std::vector<W8Missile*>* missiles;
    std::vector<stLight*>* lights_to_update;
    std::vector<W8Prop*>* collidable_props;
    std::vector<MonGen*>* monster_generators;
    std::vector<Trigger*>* triggers;
    std::vector<stParticle*>* particles;
    std::vector<W8NamedPosition*>* named_positions;
    unsigned char m_unknown_0d4[8];
};

W8_ABI_ASSERT(sizeof(W8World) == 0xdc, "W8World_must_be_0xdc");

extern W8World* g_world;
extern W8World* g_secondary_world;
extern int g_combat_round_counter;

/* Typed element access for the homogeneous world lists. */
inline W8Prop* GetWorldProp(W8World* world, int index)
{
    return static_cast<W8Prop*>((world->plsProps && static_cast<std::size_t>(index) < world->plsProps->size() ? (*world->plsProps)[index] : nullptr));
}

inline W8CameraPath* GetWorldCameraPath(W8World* world, int index)
{
    return static_cast<W8CameraPath*>((world->plsCameras && static_cast<std::size_t>(index) < world->plsCameras->size() ? (*world->plsCameras)[index] : nullptr));
}

inline W8AmbientSound* GetWorldAmbientSound(W8World* world, int index)
{
    return static_cast<W8AmbientSound*>((world->plsAmbientSounds && static_cast<std::size_t>(index) < world->plsAmbientSounds->size() ? (*world->plsAmbientSounds)[index] : nullptr));
}

#endif

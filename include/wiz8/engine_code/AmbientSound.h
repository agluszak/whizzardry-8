#pragma once

#include <cstdint>
#include <memory>

#include "wiz8/audio_resources.h"
#include "surrender/srMath.h"
#include "wiz8/engine_code/game_timer.h"

struct W8World;
struct W8RandomAmbient;
namespace wiz8 {
class File;
}  // namespace wiz8

struct W8AmbientSoundConfig {
    char wave_name[0x80];
};

/* Engine Code\AmbientSound.cpp. Offsets below describe the retail object;
   native emitters own their audio clip and no longer have that binary layout. */
class W8AmbientSound {
public:
    W8AmbientSound(); /* 0x00479040 */

    /* 0x004790D0: true when listener is inside the rotated/scaled min-max region */
    bool IsInsideRegion(const srVector3T<float>* listener);
    /* 0x00479350: listener-relative range test, group handoff, 3D re-aim, fade retarget */
    void UpdatePosition(const srVector3T<float>* listener);
    /* 0x00479970: per-frame service while in range; 'entered' forces an immediate roll */
    void Service(bool entered);
    /* 0x0047A310: step the 1.5-second shared-group volume fade */
    void UpdateFade();

    W8AmbientSound* FindNextMatching(const char* match_name, W8AmbientSound* previous);

    char* pacSoundName;          /* 0x000: assertion-backed at 0x47A790 */
    W8AmbientSoundConfig config; /* 0x004: wave filename; shared sounds match on it */
    std::uint8_t stopped;       /* 0x084: script-stopped via the ByName commands */
    std::uint8_t padding_085[3];
    srVector3T<float> position;  /* 0x088: emitter world position */
    std::uint32_t volume_min;     /* 0x094 */
    std::uint32_t volume_max;     /* 0x098 */
    std::uint32_t target_volume;  /* 0x09c: fade endpoint */
    std::uint32_t current_volume; /* 0x0a0: live fade value */
    std::int32_t speed_min;               /* 0x0a4: playback speed low bound */
    std::int32_t speed_max;               /* 0x0a8: playback speed high bound */
    std::int32_t time_min;                /* 0x0ac: random retrigger interval, low ms */
    std::int32_t time_max;                /* 0x0b0: random retrigger interval, high ms */
    float radius;                /* 0x0b4: audible range in world units */
    std::uint8_t in_range;      /* 0x0b8: listener currently inside radius/region */
    std::uint8_t looping;       /* 0x0b9: continuous loop vs random one-shot */
    std::uint8_t padding_0ba[2];
    wiz8::sound::Playback sound_handle;      // Current playback, shared during group handoff.
    std::shared_ptr<W8RandomAmbient> random; // One-shot schedule; empty for looping sounds.
    std::uint8_t shared;  /* 0x0c4: shared 2D group sound; voices hand off by name */
    std::uint8_t bounded; /* 0x0c5: region test enabled (v2 trigger field) */
    std::uint8_t padding_0c6[2];
    srVector3T<float> region_min;    /* 0x0c8 */
    srVector3T<float> region_max;    /* 0x0d4 */
    srVector3T<float> region_center; /* 0x0e0 */
    float region_angle;              /* 0x0ec: rotation about region_axis, radians */
    srVector3T<float> region_axis;   /* 0x0f0 */
    srVector3T<float> region_scale;  /* 0x0fc: per-axis region scale */
    W8GameTimer fade_timer;          /* 0x108 */
};

/* Reverb environment id indexing g_footstep_surfaces; retail's range
   check admits 9, one past the table's last entry (the ids are byte-sized in
   the record, so the parameters stay `char`). */
enum W8FootstepSurface {
    W8_FOOTSTEP_SURFACE_NONE = 0,
    W8_FOOTSTEP_SURFACE_SMALL_CAVE = 1,
    W8_FOOTSTEP_SURFACE_MEDIUM_CAVE = 2,
    W8_FOOTSTEP_SURFACE_LARGE_CAVE = 3,
    W8_FOOTSTEP_SURFACE_SMALL_ROOM = 4,
    W8_FOOTSTEP_SURFACE_MEDIUM_ROOM = 5,
    W8_FOOTSTEP_SURFACE_LARGE_ROOM = 6,
    W8_FOOTSTEP_SURFACE_OUTDOORS_FLAT = 7,
    W8_FOOTSTEP_SURFACE_OUTDOORS_CANYON = 8,
    W8_FOOTSTEP_SURFACE_MAX = 9,
};

/* Step material id indexing g_footstep_names; ids at and above
   CLIMB_LADDER bypass the per-surface naming path and play a single
   "Step_<name>.WAV". */
enum W8FootstepMaterial {
    W8_FOOTSTEP_MATERIAL_NONE = 0,
    W8_FOOTSTEP_MATERIAL_GRITTY = 1,
    W8_FOOTSTEP_MATERIAL_GRASS = 2,
    W8_FOOTSTEP_MATERIAL_STONE = 3,
    W8_FOOTSTEP_MATERIAL_SHALLOW_WATER = 4,
    W8_FOOTSTEP_MATERIAL_CREAKY_WOOD = 5,
    W8_FOOTSTEP_MATERIAL_SOLID_WOOD = 6,
    W8_FOOTSTEP_MATERIAL_HOLLOW_WOOD = 7,
    W8_FOOTSTEP_MATERIAL_METAL = 8,
    W8_FOOTSTEP_MATERIAL_GRAVEL = 9,
    W8_FOOTSTEP_MATERIAL_ROUGH_STONE = 10,
    W8_FOOTSTEP_MATERIAL_MARBLE = 11,
    W8_FOOTSTEP_MATERIAL_MUD = 12,
    W8_FOOTSTEP_MATERIAL_SAND = 13,
    W8_FOOTSTEP_MATERIAL_LEAVES = 14,
    W8_FOOTSTEP_MATERIAL_SNOW = 15,
    W8_FOOTSTEP_MATERIAL_CARPET = 16,
    W8_FOOTSTEP_MATERIAL_MAGIC = 17,
    W8_FOOTSTEP_MATERIAL_CLIMB_LADDER = 18,
    W8_FOOTSTEP_MATERIAL_CLIMB_ROCK = 19,
    W8_FOOTSTEP_MATERIAL_CLIMB_ROPE = 20,
    W8_FOOTSTEP_MATERIAL_SWIM_SURFACE = 21,
    W8_FOOTSTEP_MATERIAL_SWIM_UNDERWATER = 22,
    W8_FOOTSTEP_MATERIAL_CRAWL = 23,
    W8_FOOTSTEP_MATERIAL_FLY = 24,
    W8_FOOTSTEP_MATERIAL_MAX = 25,
};

/* Which footstep wave shape PlayFootstep builds: an ordinary numbered step, a
   single jump, or a numbered scuff. */
enum W8FootstepKind {
    W8_FOOTSTEP_KIND_STEP = 0,
    W8_FOOTSTEP_KIND_JUMP = 1,
    W8_FOOTSTEP_KIND_SCUFF = 2,
};

/* 0x00479030: drop every random schedule and stop the ambient group */
void StopAllAmbientSounds();

void BuildFootstepPath(char* path, std::int8_t surface, std::int8_t material, char kind,
                       std::int32_t variant);
wiz8::sound::Playback PlayFootstep(std::int8_t surface, std::int8_t material, W8FootstepKind kind);
void UpdateAmbientSounds(W8World* world);
void RepositionAmbientSounds(W8World* world);
std::uint8_t ReadAmbientSoundListFile(char* filename);

void PositionAmbientSoundByName(W8World* /* unused */, const char* name);
void StopAmbientSoundByName(W8World* /* unused */, const char* name);
void ToggleAmbientSoundByName(W8World* /* unused */, const char* name);
void DestroyAmbientSound(W8AmbientSound* ambient);
std::uint8_t AddAmbientSound(W8World* world, const char* name, const W8AmbientSoundConfig* config,
                              const srVector3T<float>* position,
                              const srVector3T<float>* region_min,
                              const srVector3T<float>* region_max, std::int32_t volume_min, std::int32_t volume_max,
                              std::int32_t time_min, std::int32_t time_max, std::int32_t speed_min, std::int32_t speed_max,
                              float radius, bool looping, std::uint8_t bounded,
                              const srVector3T<float>* region_center, float region_angle,
                              const srVector3T<float>* region_axis,
                              const srVector3T<float>* region_scale, std::uint8_t shared);

void SaveAmbientSoundList(wiz8::File& handle);
void LoadAmbientSoundList(wiz8::File& handle);

bool IsSoundEffectsMuted(void);
std::uint8_t GetSoundEffectsVolume(void);
void SetSoundEffectsVolume(std::uint8_t volume);
void SetSoundEffectsMuted(std::uint8_t muted);

extern std::uint8_t g_default_footstep_surface;
extern std::uint8_t g_default_footstep_material;
extern bool g_footstep_alternate;
extern std::int32_t g_previous_ambient_footstep_variant;
extern const char* g_footstep_names[];
extern const char* g_footstep_surfaces[];
/* Zeroed 128-byte ambient name image; only its first word is ever read — a
   `memcpy` of it followed by a memset seeds an empty fixed-size name. */
extern std::uint16_t g_empty_ambient_name;
